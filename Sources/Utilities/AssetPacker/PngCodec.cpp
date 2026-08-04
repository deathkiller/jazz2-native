#include "PngCodec.h"

#include <cstring>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Core/Logger.h>
#include <IO/FileStream.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::Containers;
using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::AssetPacker
{
	namespace
	{
		constexpr std::uint8_t PngSignature[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

		constexpr std::uint8_t ColorTypeGrayscale = 0;
		constexpr std::uint8_t ColorTypeRgb = 2;
		constexpr std::uint8_t ColorTypePalette = 3;
		constexpr std::uint8_t ColorTypeGrayscaleAlpha = 4;
		constexpr std::uint8_t ColorTypeRgba = 6;

		constexpr std::uint8_t FilterNone = 0;
		constexpr std::uint8_t FilterSub = 1;
		constexpr std::uint8_t FilterUp = 2;
		constexpr std::uint8_t FilterAverage = 3;
		constexpr std::uint8_t FilterPaeth = 4;

		std::uint32_t Crc32(const std::uint8_t* data, std::size_t length, std::uint32_t crc = 0xFFFFFFFFu)
		{
			// Built once on first use rather than spelled out, which is 256 lines of noise for four of code
			static std::uint32_t table[256];
			static bool tableReady = false;
			if (!tableReady) {
				for (std::uint32_t i = 0; i < 256; i++) {
					std::uint32_t c = i;
					for (std::int32_t k = 0; k < 8; k++) {
						c = ((c & 1) != 0 ? (0xEDB88320u ^ (c >> 1)) : (c >> 1));
					}
					table[i] = c;
				}
				tableReady = true;
			}

			for (std::size_t i = 0; i < length; i++) {
				crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
			}
			return crc;
		}

		std::uint32_t ReadUint32BigEndian(const std::uint8_t* data)
		{
			return (std::uint32_t(data[0]) << 24) | (std::uint32_t(data[1]) << 16)
				 | (std::uint32_t(data[2]) << 8) | std::uint32_t(data[3]);
		}

		void WriteUint32BigEndian(std::uint8_t* data, std::uint32_t value)
		{
			data[0] = std::uint8_t(value >> 24);
			data[1] = std::uint8_t(value >> 16);
			data[2] = std::uint8_t(value >> 8);
			data[3] = std::uint8_t(value);
		}

		std::uint8_t PaethPredictor(std::int32_t a, std::int32_t b, std::int32_t c)
		{
			const std::int32_t p = a + b - c;
			const std::int32_t pa = std::abs(p - a);
			const std::int32_t pb = std::abs(p - b);
			const std::int32_t pc = std::abs(p - c);
			return std::uint8_t(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
		}

		std::uint8_t UnapplyFilter(std::uint8_t filter, std::uint8_t x, std::uint8_t a, std::uint8_t b, std::uint8_t c)
		{
			switch (filter) {
				case FilterNone: return x;
				case FilterSub: return std::uint8_t(x + a);
				case FilterUp: return std::uint8_t(x + b);
				case FilterAverage: return std::uint8_t(x + (a + b) / 2);
				case FilterPaeth: return std::uint8_t(x + PaethPredictor(a, b, c));
				default: return 0;
			}
		}

		/** @brief Writes a complete chunk, including its length and the checksum over type and payload */
		void WriteChunk(Stream& s, const char type[4], const std::uint8_t* payload, std::uint32_t payloadSize)
		{
			std::uint8_t header[8];
			WriteUint32BigEndian(header, payloadSize);
			std::memcpy(header + 4, type, 4);
			s.Write(header, sizeof(header));
			if (payloadSize > 0) {
				s.Write(payload, payloadSize);
			}

			std::uint32_t crc = Crc32(header + 4, 4);
			if (payloadSize > 0) {
				crc = Crc32(payload, payloadSize, crc);
			}

			std::uint8_t crcBytes[4];
			WriteUint32BigEndian(crcBytes, crc ^ 0xFFFFFFFFu);
			s.Write(crcBytes, sizeof(crcBytes));
		}
	}

	bool PngCodec::Read(StringView path, Image& image)
	{
		FileStream s(path, FileAccess::Read);
		if (!s.IsValid()) {
			LOGE("Cannot open \"{}\" for reading", path);
			return false;
		}

		const std::int64_t fileSize = s.GetSize();
		if (fileSize < std::int64_t(sizeof(PngSignature)) || fileSize > 64 * 1024 * 1024) {
			LOGE("\"{}\" is not a valid image", path);
			return false;
		}

		Array<std::uint8_t> file(NoInit, std::size_t(fileSize));
		s.Read(file.data(), fileSize);

		if (std::memcmp(file.data(), PngSignature, sizeof(PngSignature)) != 0) {
			LOGE("\"{}\" is not a PNG image", path);
			return false;
		}

		std::int32_t width = 0, height = 0;
		std::uint8_t bitDepth = 0, colorType = 0;
		bool headerParsed = false;

		std::uint8_t palette[256 * 4];
		std::int32_t paletteCount = 0;
		for (std::int32_t i = 0; i < 256; i++) {
			// Entries the file doesn't list stay opaque black, matching what a viewer would show
			palette[(i * 4) + 0] = palette[(i * 4) + 1] = palette[(i * 4) + 2] = 0;
			palette[(i * 4) + 3] = 255;
		}

		SmallVector<std::uint8_t, 0> compressed;

		std::size_t offset = sizeof(PngSignature);
		while (offset + 8 <= std::size_t(fileSize)) {
			const std::uint32_t length = ReadUint32BigEndian(&file[offset]);
			const std::uint8_t* type = &file[offset + 4];
			const std::uint8_t* payload = &file[offset + 8];
			if (offset + 12 + length > std::size_t(fileSize)) {
				LOGE("\"{}\" is corrupted", path);
				return false;
			}
			// Checksums are not verified - the game's own writer leaves them zeroed, and a file that got here
			// has already been read off disk intact
			offset += 12 + length;

			if (std::memcmp(type, "IHDR", 4) == 0) {
				if (length < 13) {
					LOGE("\"{}\" is corrupted", path);
					return false;
				}
				width = std::int32_t(ReadUint32BigEndian(payload));
				height = std::int32_t(ReadUint32BigEndian(payload + 4));
				bitDepth = payload[8];
				colorType = payload[9];
				const std::uint8_t interlace = payload[12];

				if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
					LOGE("\"{}\" has unsupported dimensions {}x{}", path, width, height);
					return false;
				}
				if (bitDepth != 8) {
					LOGE("\"{}\" is {}-bit, only 8-bit images are supported", path, bitDepth);
					return false;
				}
				if (interlace != 0) {
					LOGE("\"{}\" is interlaced, which is not supported", path);
					return false;
				}
				if (colorType != ColorTypeGrayscale && colorType != ColorTypeRgb && colorType != ColorTypePalette
					&& colorType != ColorTypeGrayscaleAlpha && colorType != ColorTypeRgba) {
					LOGE("\"{}\" has an unsupported color type {}", path, colorType);
					return false;
				}
				headerParsed = true;
			} else if (std::memcmp(type, "PLTE", 4) == 0) {
				paletteCount = std::int32_t(length / 3);
				if (paletteCount > 256) {
					paletteCount = 256;
				}
				for (std::int32_t i = 0; i < paletteCount; i++) {
					palette[(i * 4) + 0] = payload[(i * 3) + 0];
					palette[(i * 4) + 1] = payload[(i * 3) + 1];
					palette[(i * 4) + 2] = payload[(i * 3) + 2];
				}
			} else if (std::memcmp(type, "tRNS", 4) == 0 && colorType == ColorTypePalette) {
				for (std::uint32_t i = 0; i < length && i < 256; i++) {
					palette[(i * 4) + 3] = payload[i];
				}
			} else if (std::memcmp(type, "IDAT", 4) == 0) {
				const std::size_t previousSize = compressed.size();
				compressed.resize_for_overwrite(previousSize + length);
				std::memcpy(compressed.data() + previousSize, payload, length);
			} else if (std::memcmp(type, "IEND", 4) == 0) {
				break;
			}
		}

		if (!headerParsed || compressed.size() < 2) {
			LOGE("\"{}\" is corrupted", path);
			return false;
		}

		const std::int32_t sourceChannels = (colorType == ColorTypeRgba ? 4
			: (colorType == ColorTypeRgb ? 3
			: (colorType == ColorTypeGrayscaleAlpha ? 2 : 1)));
		const std::int32_t sourceStride = width * sourceChannels;

		// One filter byte per row, then that row's samples - exactly what the decode loop below consumes
		const std::size_t rawSize = std::size_t(height) * (1 + std::size_t(sourceStride));
		Array<std::uint8_t> raw(NoInit, rawSize);
		{
			// The two leading bytes are the zlib header; what follows is the raw deflate stream
			MemoryStream ms(compressed.data() + 2, std::int64_t(compressed.size()) - 2);
			DeflateStream uc(ms, std::int32_t(rawSize));
			if (uc.Read(raw.data(), std::int64_t(rawSize)) != std::int64_t(rawSize)) {
				LOGE("\"{}\" cannot be decompressed", path);
				return false;
			}
		}

		image.Width = width;
		image.Height = height;
		image.Pixels = std::make_unique<std::uint8_t[]>(std::size_t(width) * height * 4);

		Array<std::uint8_t> previousRow(ValueInit, std::size_t(sourceStride));
		std::size_t sourceOffset = 0;
		for (std::int32_t y = 0; y < height; y++) {
			const std::uint8_t filter = raw[sourceOffset++];
			std::uint8_t* row = &raw[sourceOffset];
			sourceOffset += sourceStride;

			for (std::int32_t i = 0; i < sourceStride; i++) {
				const std::uint8_t a = (i >= sourceChannels ? row[i - sourceChannels] : 0);
				const std::uint8_t c = (i >= sourceChannels ? previousRow[i - sourceChannels] : 0);
				row[i] = UnapplyFilter(filter, row[i], a, previousRow[i], c);
			}

			std::uint8_t* target = &image.Pixels[std::size_t(y) * width * 4];
			for (std::int32_t x = 0; x < width; x++) {
				const std::uint8_t* source = &row[x * sourceChannels];
				switch (colorType) {
					case ColorTypeRgba:
						std::memcpy(&target[x * 4], source, 4);
						break;
					case ColorTypeRgb:
						std::memcpy(&target[x * 4], source, 3);
						target[(x * 4) + 3] = 255;
						break;
					case ColorTypeGrayscale:
						target[(x * 4) + 0] = target[(x * 4) + 1] = target[(x * 4) + 2] = source[0];
						target[(x * 4) + 3] = 255;
						break;
					case ColorTypeGrayscaleAlpha:
						target[(x * 4) + 0] = target[(x * 4) + 1] = target[(x * 4) + 2] = source[0];
						target[(x * 4) + 3] = source[1];
						break;
					default:
						std::memcpy(&target[x * 4], &palette[source[0] * 4], 4);
						break;
				}
			}

			std::memcpy(previousRow.data(), row, sourceStride);
		}

		return true;
	}

	bool PngCodec::Write(StringView path, const Image& image)
	{
		if (!image || image.Width <= 0 || image.Height <= 0) {
			return false;
		}

		constexpr std::int32_t Channels = 4;
		const std::int32_t stride = image.Width * Channels;

		// Every row is filtered five ways and the one with the smallest total deviation from zero is kept,
		// which is the heuristic the PNG specification itself suggests - long runs of the same value survive
		// into the deflate stream, and a glyph atlas is mostly one such run
		MemoryStream filtered(std::int64_t(image.Height) * (1 + stride));
		Array<std::uint8_t> candidate(NoInit, std::size_t(stride));
		Array<std::uint8_t> best(NoInit, std::size_t(stride));
		Array<std::uint8_t> previousRow(ValueInit, std::size_t(stride));

		for (std::int32_t y = 0; y < image.Height; y++) {
			const std::uint8_t* row = &image.Pixels[std::size_t(y) * stride];

			std::uint8_t bestFilter = FilterNone;
			std::int64_t bestScore = INT64_MAX;
			for (std::uint8_t filter = FilterNone; filter <= FilterPaeth; filter++) {
				std::int64_t score = 0;
				for (std::int32_t i = 0; i < stride; i++) {
					const std::uint8_t a = (i >= Channels ? row[i - Channels] : 0);
					const std::uint8_t b = previousRow[i];
					const std::uint8_t c = (i >= Channels ? previousRow[i - Channels] : 0);

					std::uint8_t value;
					switch (filter) {
						case FilterSub: value = std::uint8_t(row[i] - a); break;
						case FilterUp: value = std::uint8_t(row[i] - b); break;
						case FilterAverage: value = std::uint8_t(row[i] - ((a + b) / 2)); break;
						case FilterPaeth: value = std::uint8_t(row[i] - PaethPredictor(a, b, c)); break;
						default: value = row[i]; break;
					}
					candidate[i] = value;
					score += (value < 128 ? value : 256 - value);
				}

				if (score < bestScore) {
					bestScore = score;
					bestFilter = filter;
					std::memcpy(best.data(), candidate.data(), stride);
				}
			}

			filtered.Write(&bestFilter, 1);
			filtered.Write(best.data(), stride);
			std::memcpy(previousRow.data(), row, stride);
		}

		MemoryStream compressed(filtered.GetSize() / 2 + 64);
		{
			// A PNG data stream is zlib-wrapped, so the writer has to emit the header and the checksum too
			DeflateWriter deflate(compressed, 9, false);
			deflate.Write(filtered.GetBuffer(), filtered.GetSize());
		}

		FileStream s(path, FileAccess::Write);
		if (!s.IsValid()) {
			LOGE("Cannot open \"{}\" for writing", path);
			return false;
		}

		s.Write(PngSignature, sizeof(PngSignature));

		std::uint8_t header[13];
		WriteUint32BigEndian(header, std::uint32_t(image.Width));
		WriteUint32BigEndian(header + 4, std::uint32_t(image.Height));
		header[8] = 8;					// Bit depth
		header[9] = ColorTypeRgba;
		header[10] = 0;					// Compression method
		header[11] = 0;					// Filter method
		header[12] = 0;					// Interlace method
		WriteChunk(s, "IHDR", header, sizeof(header));
		WriteChunk(s, "IDAT", compressed.GetBuffer(), std::uint32_t(compressed.GetSize()));
		WriteChunk(s, "IEND", nullptr, 0);

		return s.IsValid();
	}
}
