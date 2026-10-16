#include "PngCodec.h"

#if defined(WITH_EDITOR)

#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::uint8_t Signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		// Largest image read, so a damaged size can't take all of the memory
		constexpr std::int64_t MaxPixels = std::int64_t(1) << 26;

		enum ColorType : std::uint8_t
		{
			Gray = 0,
			Rgb = 2,
			Palette = 3,
			GrayAlpha = 4,
			Rgba = 6
		};

		constexpr std::uint32_t ChunkTag(char a, char b, char c, char d)
		{
			return (std::uint32_t(std::uint8_t(a)) << 24) | (std::uint32_t(std::uint8_t(b)) << 16) | (std::uint32_t(std::uint8_t(c)) << 8) | std::uint32_t(std::uint8_t(d));
		}

		struct Crc32
		{
			std::uint32_t Table[256];

			Crc32()
			{
				for (std::uint32_t n = 0; n < 256; n++) {
					std::uint32_t c = n;
					for (std::int32_t k = 0; k < 8; k++) {
						c = ((c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1);
					}
					Table[n] = c;
				}
			}

			std::uint32_t Update(std::uint32_t crc, const std::uint8_t* data, std::size_t size) const
			{
				for (std::size_t i = 0; i < size; i++) {
					crc = Table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
				}
				return crc;
			}
		};

		const Crc32& GetCrc32()
		{
			static const Crc32 crc;
			return crc;
		}

		std::uint32_t ReadBigEndian(const std::uint8_t* p)
		{
			return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
		}

		void WriteBigEndian(std::uint8_t* p, std::uint32_t value)
		{
			p[0] = std::uint8_t(value >> 24);
			p[1] = std::uint8_t(value >> 16);
			p[2] = std::uint8_t(value >> 8);
			p[3] = std::uint8_t(value);
		}

		std::uint8_t Paeth(std::int32_t a, std::int32_t b, std::int32_t c)
		{
			std::int32_t p = a + b - c;
			std::int32_t pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
			return std::uint8_t(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
		}

		// Undoes the filter of a row, the previous row is all zeros for the first one
		bool Unfilter(std::uint8_t filter, std::uint8_t* row, const std::uint8_t* previous, std::size_t size, std::size_t bpp)
		{
			switch (filter) {
				case 0: return true;
				case 1: for (std::size_t i = bpp; i < size; i++) row[i] = std::uint8_t(row[i] + row[i - bpp]); return true;
				case 2: for (std::size_t i = 0; i < size; i++) row[i] = std::uint8_t(row[i] + previous[i]); return true;
				case 3:
					for (std::size_t i = 0; i < size; i++) {
						std::int32_t left = (i >= bpp ? row[i - bpp] : 0);
						row[i] = std::uint8_t(row[i] + ((left + previous[i]) >> 1));
					}
					return true;
				case 4:
					for (std::size_t i = 0; i < size; i++) {
						std::int32_t left = (i >= bpp ? row[i - bpp] : 0);
						std::int32_t upLeft = (i >= bpp ? previous[i - bpp] : 0);
						row[i] = std::uint8_t(row[i] + Paeth(left, previous[i], upLeft));
					}
					return true;
				default:
					return false;
			}
		}

		// Filters a row by the given filter into out, which has the filter byte first
		void Filter(std::uint8_t filter, const std::uint8_t* row, const std::uint8_t* previous, std::size_t size, std::size_t bpp, std::uint8_t* out)
		{
			out[0] = filter;
			for (std::size_t i = 0; i < size; i++) {
				std::int32_t left = (i >= bpp ? row[i - bpp] : 0);
				std::int32_t up = previous[i];
				std::int32_t upLeft = (i >= bpp ? previous[i - bpp] : 0);
				std::int32_t predicted;
				switch (filter) {
					case 1: predicted = left; break;
					case 2: predicted = up; break;
					case 3: predicted = (left + up) >> 1; break;
					case 4: predicted = Paeth(left, up, upLeft); break;
					default: predicted = 0; break;
				}
				out[1 + i] = std::uint8_t(row[i] - predicted);
			}
		}

		struct Header
		{
			std::int32_t Width, Height;
			std::uint8_t BitDepth, ColorType, Interlace;
			std::int32_t Channels;
			bool HasColorKey = false;
			std::uint16_t ColorKey[3] = {};
		};

		// The value of a sample of a row, of any depth
		std::uint32_t GetSample(const std::uint8_t* row, std::int32_t index, std::int32_t bitDepth)
		{
			switch (bitDepth) {
				case 16: return (std::uint32_t(row[index * 2]) << 8) | row[index * 2 + 1];
				case 8: return row[index];
				default: {
					std::int32_t bit = index * bitDepth;
					std::int32_t shift = 8 - bitDepth - (bit & 7);
					return (row[bit >> 3] >> shift) & ((1u << bitDepth) - 1);
				}
			}
		}

		// Converts a pixel of an unfiltered row to the image
		void StorePixel(const Header& header, const std::uint8_t* row, std::int32_t sourceX, PngImage& image, std::int32_t x, std::int32_t y)
		{
			std::size_t pos = std::size_t(y) * std::size_t(image.Width) + std::size_t(x);
			if (header.ColorType == Palette) {
				image.Pixels[pos] = std::uint8_t(GetSample(row, sourceX, header.BitDepth));
				return;
			}

			std::int32_t depth = header.BitDepth;
			std::uint32_t maxValue = (1u << depth) - 1;
			auto to8 = [depth, maxValue](std::uint32_t value) {
				return std::uint8_t((value * 255 + maxValue / 2) / maxValue);
			};
			std::uint32_t samples[4] = {};
			for (std::int32_t c = 0; c < header.Channels; c++) {
				samples[c] = GetSample(row, sourceX * header.Channels + c, depth);
			}
			std::uint8_t r, g, b, a = 255;
			switch (header.ColorType) {
				case Gray:
					r = g = b = to8(samples[0]);
					if (header.HasColorKey && samples[0] == header.ColorKey[0]) {
						a = 0;
					}
					break;
				case GrayAlpha:
					r = g = b = to8(samples[0]);
					a = to8(samples[1]);
					break;
				case Rgb:
					r = to8(samples[0]);
					g = to8(samples[1]);
					b = to8(samples[2]);
					if (header.HasColorKey && samples[0] == header.ColorKey[0] && samples[1] == header.ColorKey[1] && samples[2] == header.ColorKey[2]) {
						a = 0;
					}
					break;
				default:
					r = to8(samples[0]);
					g = to8(samples[1]);
					b = to8(samples[2]);
					a = to8(samples[3]);
					break;
			}
			std::uint8_t* out = &image.Pixels[pos * 4];
			out[0] = r;
			out[1] = g;
			out[2] = b;
			out[3] = a;
		}
	}

	void PngImage::Create(std::int32_t width, std::int32_t height, bool indexed)
	{
		Width = width;
		Height = height;
		Indexed = indexed;
		Pixels.clear();
		Pixels.resize(std::size_t(width) * std::size_t(height) * (indexed ? 1 : 4), 0);
	}

	std::uint32_t PngImage::GetColor(std::int32_t x, std::int32_t y) const
	{
		std::size_t pos = std::size_t(y) * std::size_t(Width) + std::size_t(x);
		if (Indexed) {
			return Palette[Pixels[pos]];
		}
		const std::uint8_t* p = &Pixels[pos * 4];
		return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
	}

	bool PngCodec::Decode(Stream& s, PngImage& image, String* error)
	{
		auto fail = [error](const char* message) {
			if (error != nullptr) {
				*error = message;
			}
			return false;
		};

		std::uint8_t signature[8];
		if (s.Read(signature, sizeof(signature)) != sizeof(signature) || std::memcmp(signature, Signature, sizeof(Signature)) != 0) {
			return fail("It isn't a PNG image");
		}

		Header header = {};
		bool hasHeader = false;
		std::uint32_t palette[256] = {};
		std::int32_t paletteSize = 0;
		SmallVector<std::uint8_t, 0> compressed;
		SmallVector<std::uint8_t, 0> chunk;
		const Crc32& crc = GetCrc32();

		for (;;) {
			std::uint8_t lengthAndType[8];
			if (s.Read(lengthAndType, sizeof(lengthAndType)) != sizeof(lengthAndType)) {
				return fail("The image ends too soon");
			}
			std::uint32_t length = ReadBigEndian(lengthAndType);
			std::uint32_t type = ReadBigEndian(lengthAndType + 4);
			if (length > 0x7FFFFFFF) {
				return fail("The image is damaged");
			}
			chunk.resize_for_overwrite(length + 4);
			std::memcpy(chunk.data(), lengthAndType + 4, 4);
			std::uint8_t storedCrc[4];
			if (s.Read(chunk.data() + 4, length) != std::int64_t(length) || s.Read(storedCrc, 4) != 4) {
				return fail("The image ends too soon");
			}
			if ((crc.Update(0xFFFFFFFFu, chunk.data(), chunk.size()) ^ 0xFFFFFFFFu) != ReadBigEndian(storedCrc)) {
				return fail("The image is damaged");
			}
			const std::uint8_t* data = chunk.data() + 4;

			if (type == ChunkTag('I', 'H', 'D', 'R')) {
				if (length < 13) {
					return fail("The image is damaged");
				}
				header.Width = std::int32_t(ReadBigEndian(data));
				header.Height = std::int32_t(ReadBigEndian(data + 4));
				header.BitDepth = data[8];
				header.ColorType = data[9];
				header.Interlace = data[12];
				if (header.Width <= 0 || header.Height <= 0 || std::int64_t(header.Width) * header.Height > MaxPixels ||
					data[10] != 0 || data[11] != 0 || header.Interlace > 1) {
					return fail("The image is too large or of an unknown kind");
				}
				switch (header.ColorType) {
					case Gray: header.Channels = 1; break;
					case Rgb: header.Channels = 3; break;
					case Palette: header.Channels = 1; break;
					case GrayAlpha: header.Channels = 2; break;
					case Rgba: header.Channels = 4; break;
					default: return fail("The image is of an unknown kind");
				}
				bool validDepth = (header.ColorType == Gray ? (header.BitDepth == 1 || header.BitDepth == 2 || header.BitDepth == 4 || header.BitDepth == 8 || header.BitDepth == 16)
					: (header.ColorType == Palette ? (header.BitDepth == 1 || header.BitDepth == 2 || header.BitDepth == 4 || header.BitDepth == 8)
					: (header.BitDepth == 8 || header.BitDepth == 16)));
				if (!validDepth) {
					return fail("The image is of an unknown kind");
				}
				hasHeader = true;
			} else if (type == ChunkTag('P', 'L', 'T', 'E')) {
				paletteSize = std::min(std::int32_t(length / 3), 256);
				for (std::int32_t i = 0; i < paletteSize; i++) {
					palette[i] = std::uint32_t(data[i * 3]) | (std::uint32_t(data[i * 3 + 1]) << 8) | (std::uint32_t(data[i * 3 + 2]) << 16) | 0xFF000000u;
				}
			} else if (type == ChunkTag('t', 'R', 'N', 'S')) {
				if (header.ColorType == Palette) {
					for (std::uint32_t i = 0; i < length && i < 256; i++) {
						palette[i] = (palette[i] & 0x00FFFFFFu) | (std::uint32_t(data[i]) << 24);
					}
				} else if (header.ColorType == Gray && length >= 2) {
					header.HasColorKey = true;
					header.ColorKey[0] = std::uint16_t((data[0] << 8) | data[1]);
				} else if (header.ColorType == Rgb && length >= 6) {
					header.HasColorKey = true;
					for (std::int32_t c = 0; c < 3; c++) {
						header.ColorKey[c] = std::uint16_t((data[c * 2] << 8) | data[c * 2 + 1]);
					}
				}
			} else if (type == ChunkTag('I', 'D', 'A', 'T')) {
				compressed.insert(compressed.end(), data, data + length);
			} else if (type == ChunkTag('I', 'E', 'N', 'D')) {
				break;
			} else if ((type & 0x20000000u) == 0) {
				// A critical chunk the image can't be read without
				return fail("The image uses a feature that isn't supported");
			}
		}
		if (!hasHeader || compressed.empty()) {
			return fail("The image is damaged");
		}
		if (header.ColorType == Palette && paletteSize == 0) {
			return fail("The image has no palette");
		}

		// Passes of Adam7, or a single one of the whole image
		struct Pass { std::int32_t X, Y, StepX, StepY; };
		static const Pass Adam7[7] = { { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 } };
		static const Pass Single[1] = { { 0, 0, 1, 1 } };
		const Pass* passes = (header.Interlace != 0 ? Adam7 : Single);
		std::int32_t passCount = (header.Interlace != 0 ? 7 : 1);

		std::size_t bitsPerPixel = std::size_t(header.BitDepth) * std::size_t(header.Channels);
		std::size_t bpp = std::max<std::size_t>(1, bitsPerPixel / 8);
		std::size_t rawSize = 0;
		for (std::int32_t p = 0; p < passCount; p++) {
			std::int32_t width = (header.Width - passes[p].X + passes[p].StepX - 1) / passes[p].StepX;
			std::int32_t height = (header.Height - passes[p].Y + passes[p].StepY - 1) / passes[p].StepY;
			if (width > 0 && height > 0) {
				rawSize += std::size_t(height) * (1 + (std::size_t(width) * bitsPerPixel + 7) / 8);
			}
		}

		SmallVector<std::uint8_t, 0> raw;
		raw.resize_for_overwrite(rawSize);
		{
			MemoryStream input(compressed.data(), std::int64_t(compressed.size()));
			DeflateStream inflater(input, std::int32_t(compressed.size()), false);
			if (inflater.Read(raw.data(), std::int64_t(rawSize)) != std::int64_t(rawSize)) {
				return fail("The image is damaged");
			}
		}

		image.Create(header.Width, header.Height, header.ColorType == Palette);
		std::memcpy(image.Palette, palette, sizeof(palette));
		image.PaletteSize = paletteSize;

		std::size_t offset = 0;
		SmallVector<std::uint8_t, 0> previous;
		for (std::int32_t p = 0; p < passCount; p++) {
			std::int32_t width = (header.Width - passes[p].X + passes[p].StepX - 1) / passes[p].StepX;
			std::int32_t height = (header.Height - passes[p].Y + passes[p].StepY - 1) / passes[p].StepY;
			if (width <= 0 || height <= 0) {
				continue;
			}
			std::size_t rowSize = (std::size_t(width) * bitsPerPixel + 7) / 8;
			previous.clear();
			previous.resize(rowSize, 0);
			for (std::int32_t y = 0; y < height; y++) {
				std::uint8_t filter = raw[offset];
				std::uint8_t* row = &raw[offset + 1];
				offset += 1 + rowSize;
				if (!Unfilter(filter, row, previous.data(), rowSize, bpp)) {
					return fail("The image is damaged");
				}
				for (std::int32_t x = 0; x < width; x++) {
					StorePixel(header, row, x, image, passes[p].X + x * passes[p].StepX, passes[p].Y + y * passes[p].StepY);
				}
				std::memcpy(previous.data(), row, rowSize);
			}
		}
		return true;
	}

	bool PngCodec::Encode(Stream& s, const PngImage& image)
	{
		if (image.Width <= 0 || image.Height <= 0 || image.Pixels.size() != std::size_t(image.Width) * std::size_t(image.Height) * (image.Indexed ? 1 : 4)) {
			return false;
		}

		const Crc32& crc = GetCrc32();
		auto writeChunk = [&s, &crc](const char* type, const std::uint8_t* data, std::size_t size) {
			std::uint8_t lengthBytes[4];
			WriteBigEndian(lengthBytes, std::uint32_t(size));
			s.Write(lengthBytes, 4);
			std::uint32_t value = crc.Update(0xFFFFFFFFu, reinterpret_cast<const std::uint8_t*>(type), 4);
			value = crc.Update(value, data, size) ^ 0xFFFFFFFFu;
			s.Write(type, 4);
			if (size > 0) {
				s.Write(data, std::int64_t(size));
			}
			std::uint8_t crcBytes[4];
			WriteBigEndian(crcBytes, value);
			s.Write(crcBytes, 4);
		};

		s.Write(Signature, sizeof(Signature));
		std::uint8_t ihdr[13];
		WriteBigEndian(ihdr, std::uint32_t(image.Width));
		WriteBigEndian(ihdr + 4, std::uint32_t(image.Height));
		ihdr[8] = 8;
		ihdr[9] = (image.Indexed ? Palette : Rgba);
		ihdr[10] = 0;
		ihdr[11] = 0;
		ihdr[12] = 0;
		writeChunk("IHDR", ihdr, sizeof(ihdr));

		if (image.Indexed) {
			std::int32_t paletteSize = std::clamp(image.PaletteSize, 1, 256);
			std::uint8_t plte[256 * 3];
			std::uint8_t trns[256];
			std::int32_t trnsSize = 0;
			for (std::int32_t i = 0; i < paletteSize; i++) {
				std::uint32_t color = image.Palette[i];
				plte[i * 3] = std::uint8_t(color);
				plte[i * 3 + 1] = std::uint8_t(color >> 8);
				plte[i * 3 + 2] = std::uint8_t(color >> 16);
				trns[i] = std::uint8_t(color >> 24);
				if (trns[i] != 255) {
					trnsSize = i + 1;
				}
			}
			writeChunk("PLTE", plte, std::size_t(paletteSize) * 3);
			if (trnsSize > 0) {
				writeChunk("tRNS", trns, std::size_t(trnsSize));
			}
		}

		// Each row with the filter that leaves the smallest differences, which compress best
		std::size_t bpp = (image.Indexed ? 1 : 4);
		std::size_t rowSize = std::size_t(image.Width) * bpp;
		MemoryStream compressed;
		{
			DeflateWriter deflater(compressed, 9, false);
			SmallVector<std::uint8_t, 0> zeros;
			zeros.resize(rowSize, 0);
			SmallVector<std::uint8_t, 0> candidate, best;
			candidate.resize_for_overwrite(rowSize + 1);
			best.resize_for_overwrite(rowSize + 1);
			for (std::int32_t y = 0; y < image.Height; y++) {
				const std::uint8_t* row = &image.Pixels[std::size_t(y) * rowSize];
				const std::uint8_t* previous = (y > 0 ? row - rowSize : zeros.data());
				// Indexed pixels aren't magnitudes, differences of them don't help
				std::uint8_t lastFilter = (image.Indexed ? 0 : 4);
				std::uint64_t bestCost = UINT64_MAX;
				for (std::uint8_t filter = 0; filter <= lastFilter; filter++) {
					Filter(filter, row, previous, rowSize, bpp, candidate.data());
					std::uint64_t cost = 0;
					for (std::size_t i = 1; i <= rowSize; i++) {
						cost += std::uint64_t(std::abs(std::int32_t(std::int8_t(candidate[i]))));
					}
					if (cost < bestCost) {
						bestCost = cost;
						std::swap(candidate, best);
					}
				}
				deflater.Write(best.data(), std::int64_t(rowSize + 1));
			}
			deflater.Dispose();
		}
		writeChunk("IDAT", compressed.GetBuffer(), std::size_t(compressed.GetSize()));
		writeChunk("IEND", nullptr, 0);
		return true;
	}
}

#endif
