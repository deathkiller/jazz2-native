#include "J2vRecompressor.h"
#include "../VideoFormat.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <Containers/Array.h>
#include <Containers/Pair.h>
#include <Containers/GrowableArray.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Compatibility
{
	namespace
	{
		constexpr std::int32_t StreamCount = 4;
		/** @brief Header size: 16 bytes of signature, then width, height, bpp, delay, frames and 20 reserved */
		constexpr std::int32_t HeaderSize = 16 + 4 + 4 + 2 + 2 + 4 + 20;
		/** @brief Size of the chunks the re-encoded streams are split into, matching the player's read-ahead */
		constexpr std::int32_t TargetChunkSize = 16 * 1024;

		/** @brief One decoded stream, already inflated into memory */
		struct SourceStream
		{
			Array<std::uint8_t> Data;
			std::size_t Position = 0;
			bool Exhausted = false;

			std::uint8_t ReadByte()
			{
				if (Position >= Data.size()) {
					Exhausted = true;
					return 0;
				}
				return Data[Position++];
			}

			std::uint16_t ReadUint16()
			{
				std::uint16_t result = std::uint16_t(ReadByte());
				result |= std::uint16_t(ReadByte()) << 8;
				return result;
			}

			void Read(std::uint8_t* destination, std::size_t length)
			{
				std::size_t available = (Position < Data.size() ? std::min(length, Data.size() - Position) : 0);
				if (available > 0) {
					std::memcpy(destination, &Data[Position], available);
				}
				if (available < length) {
					std::memset(destination + available, 0, length - available);
					Exhausted = true;
				}
				Position += length;
			}

			void Skip(std::size_t length)
			{
				Position += length;
				if (Position > Data.size()) {
					Exhausted = true;
				}
			}
		};

		void WriteUint16(Array<std::uint8_t>& target, std::uint16_t value)
		{
			arrayAppend(target, std::uint8_t(value & 0xFF));
			arrayAppend(target, std::uint8_t((value >> 8) & 0xFF));
		}



		/** @brief Leaves a span of the previous frame in place */
		void EmitSkip(Array<std::uint8_t>& payload, std::int32_t length)
		{
			while (length > 0) {
				if (length <= VideoFormat::MaxShortSkip) {
					arrayAppend(payload, std::uint8_t(VideoFormat::CommandSkipBase + length - 1));
					break;
				}
				std::int32_t part = std::min(length, std::int32_t(0xFFFF));
				arrayAppend(payload, std::uint8_t(VideoFormat::CommandSkipLong));
				WriteUint16(payload, std::uint16_t(part));
				length -= part;
			}
		}

		/** @brief Stores a span of pixels as they are */
		void EmitLiterals(Array<std::uint8_t>& payload, const std::uint8_t* source, std::int32_t length)
		{
			while (length > 0) {
				if (length <= VideoFormat::MaxShortLiteral) {
					arrayAppend(payload, std::uint8_t(length - 1));
					arrayAppend(payload, arrayView(source, length));
					break;
				}
				std::int32_t part = std::min(length, std::int32_t(0xFFFF));
				arrayAppend(payload, std::uint8_t(VideoFormat::CommandLiteralLong));
				WriteUint16(payload, std::uint16_t(part));
				arrayAppend(payload, arrayView(source, part));
				source += part;
				length -= part;
			}
		}

		/** @brief Repeats one pixel value */
		void EmitRun(Array<std::uint8_t>& payload, std::uint8_t value, std::int32_t length)
		{
			while (length > 0) {
				if (length <= VideoFormat::MaxShortRun) {
					arrayAppend(payload, std::uint8_t(VideoFormat::CommandRunBase + length - VideoFormat::CommandRunMinLength));
					arrayAppend(payload, value);
					break;
				}
				std::int32_t part = std::min(length, std::int32_t(0xFFFF));
				if (length - part > 0 && length - part < VideoFormat::CommandRunMinLength) {
					// A remainder of 1-2 pixels has no run encoding, so leave a full short run instead
					part = length - VideoFormat::CommandRunMinLength;
				}
				arrayAppend(payload, std::uint8_t(VideoFormat::CommandRunLong));
				WriteUint16(payload, std::uint16_t(part));
				arrayAppend(payload, value);
				length -= part;
			}
		}

		/** @brief Inflates one of the four interleaved streams from the whole set of its chunks */
		bool InflateStream(Stream& input, const Array<Pair<std::int64_t, std::int32_t>>& chunks, Array<std::uint8_t>& target)
		{
			Array<std::uint8_t> compressed;
			for (const auto& chunk : chunks) {
				if (chunk.second() <= 0) {
					continue;
				}
				input.Seek(chunk.first(), SeekOrigin::Begin);
				std::size_t offset = compressed.size();
				arrayResize(compressed, NoInit, offset + chunk.second());
				if (input.Read(&compressed[offset], chunk.second()) != chunk.second()) {
					return false;
				}
			}

			if (compressed.size() <= 2) {
				return true;
			}

			// The first two bytes are the zlib header, the payload itself is raw deflate
			MemoryStream ms(compressed.data() + 2, std::int64_t(compressed.size() - 2));
			DeflateStream ds(ms);
			std::uint8_t buffer[16 * 1024];
			while (true) {
				std::int32_t bytesRead = ds.Read(buffer, sizeof(buffer));
				if (bytesRead <= 0) {
					break;
				}
				arrayAppend(target, arrayView(buffer, bytesRead));
			}
			return true;
		}
	}

	bool J2vRecompressor::Recompress(StringView sourcePath, StringView targetPath, std::int32_t downscale)
	{
		if (downscale < 1) {
			downscale = 1;
		}

		auto input = fs::Open(sourcePath, FileAccess::Read);
		if (!input->IsValid() || input->GetSize() < HeaderSize) {
			LOGE("Cannot open \"{}\"", sourcePath);
			return false;
		}

		char signature[16];
		input->Read(signature, sizeof(signature));
		if (std::strncmp(signature, "CineFeed", sizeof("CineFeed") - 1) != 0) {
			LOGE("\"{}\" is not a cinematic file", sourcePath);
			return false;
		}

		std::int32_t width = std::int32_t(input->ReadValueAsLE<std::uint32_t>());
		std::int32_t height = std::int32_t(input->ReadValueAsLE<std::uint32_t>());
		std::uint16_t bitsPerPixel = input->ReadValueAsLE<std::uint16_t>();
		std::uint16_t frameDelay = input->ReadValueAsLE<std::uint16_t>();
		std::int32_t frameCount = std::int32_t(input->ReadValueAsLE<std::uint32_t>());
		std::uint8_t reserved[20];
		input->Read(reserved, sizeof(reserved));

		if (width <= 0 || height <= 0 || frameCount <= 0) {
			LOGE("\"{}\" has unexpected dimensions", sourcePath);
			return false;
		}

		// Index the interleaved chunks, then inflate each stream as a whole
		Array<Pair<std::int64_t, std::int32_t>> chunks[StreamCount];
		std::int64_t offset = input->GetPosition();
		const std::int64_t fileSize = input->GetSize();
		while (offset + 4 <= fileSize) {
			for (std::int32_t i = 0; i < StreamCount && offset + 4 <= fileSize; i++) {
				input->Seek(offset, SeekOrigin::Begin);
				std::int32_t length = input->ReadValueAsLE<std::int32_t>();
				offset += 4;
				if (length < 0 || offset + length > fileSize) {
					length = std::int32_t(std::max(std::int64_t(0), fileSize - offset));
				}
				arrayAppend(chunks[i], Pair(offset, length));
				offset += length;
			}
		}

		SourceStream streams[StreamCount];
		for (std::int32_t i = 0; i < StreamCount; i++) {
			if (!InflateStream(*input, chunks[i], streams[i].Data)) {
				LOGE("Cannot decompress stream {} of \"{}\"", i, sourcePath);
				return false;
			}
		}

		const std::int32_t targetWidth = width / downscale;
		const std::int32_t targetHeight = height / downscale;

		auto frame = std::make_unique<std::uint8_t[]>(std::size_t(width) * height);
		auto previousFrame = std::make_unique<std::uint8_t[]>(std::size_t(width) * height);
		auto scaled = std::make_unique<std::uint8_t[]>(std::size_t(targetWidth) * targetHeight);
		auto previousScaled = std::make_unique<std::uint8_t[]>(std::size_t(targetWidth) * targetHeight);
		std::memset(previousFrame.get(), 0, std::size_t(width) * height);
		std::memset(previousScaled.get(), 0, std::size_t(targetWidth) * targetHeight);

		Array<std::uint32_t> frameSizes;
		Array<std::uint8_t> frameData;
		std::uint8_t palette[256 * 4] = {};
		std::int32_t framesWritten = 0;

		for (std::int32_t f = 0; f < frameCount; f++) {
			// Decode one frame exactly the way the player does
			bool paletteChanged = (streams[0].ReadByte() == 0x01);
			if (paletteChanged) {
				streams[3].Read(palette, sizeof(palette));
			}

			const std::int32_t totalPixels = width * height;
			for (std::int32_t y = 0; y < height; y++) {
				std::uint8_t* row = &frame[std::size_t(y) * width];
				std::uint8_t c;
				std::int32_t x = 0;
				while ((c = streams[0].ReadByte()) != 0x80) {
					// A dead stream keeps returning zeros (c = 0 with a zero run length), which would spin
					// here forever - bail out as soon as the short read is detected, like the player does
					if (streams[0].Exhausted) {
						LOGE("\"{}\" is truncated at frame {}", sourcePath, f);
						return false;
					}
					if (c < 0x80) {
						std::int32_t u = (c == 0x00 ? streams[0].ReadUint16() : c);
						std::int32_t fits = std::min(u, std::int32_t(width - x));
						if (fits > 0) {
							streams[3].Read(&row[x], std::size_t(fits));
						}
						// A run overhanging the row would be a corrupted frame; its bytes are still consumed
						// so the stream stays aligned with the following runs
						if (u > fits) {
							streams[3].Skip(std::size_t(u - fits));
						}
						x += u;
					} else {
						std::int32_t u = (c == 0x81 ? streams[0].ReadUint16() : c - 0x6A);
						std::int32_t n = streams[1].ReadUint16() + (streams[2].ReadByte() + y - 127) * width;
						std::int32_t fits = std::min(u, std::int32_t(width - x));
						if (fits > 0 && n >= 0 && n + fits <= totalPixels) {
							std::memcpy(&row[x], &previousFrame[n], std::size_t(fits));
						}
						x += u;
					}
				}
			}

			std::memcpy(previousFrame.get(), frame.get(), std::size_t(width) * height);

			// Downscale by picking every n-th pixel of every n-th row, matching what the player did at runtime
			if (downscale == 1) {
				std::memcpy(scaled.get(), frame.get(), std::size_t(targetWidth) * targetHeight);
			} else {
				for (std::int32_t y = 0; y < targetHeight; y++) {
					const std::uint8_t* src = &frame[std::size_t(y) * downscale * width];
					std::uint8_t* dst = &scaled[std::size_t(y) * targetWidth];
					for (std::int32_t x = 0; x < targetWidth; x++) {
						dst[x] = src[std::size_t(x) * downscale];
					}
				}
			}

			// Encode this frame as changes against the previous one: unchanged spans are skipped (the
			// player leaves the previous frame's pixels in place), repeated bytes become runs, and anything
			// else is stored literally. Decoding is memcpy/memset only - see VideoFormat.
			Array<std::uint8_t> payload;
			arrayAppend(payload, std::uint8_t(paletteChanged || f == 0 ? VideoFormat::FrameFlagPalette : 0x00));
			if (paletteChanged || f == 0) {
				arrayAppend(payload, arrayView(palette, sizeof(palette)));
			}

			const std::int32_t framePixels = targetWidth * targetHeight;
			std::int32_t i = 0;
			while (i < framePixels) {
				// Anything identical to the previous frame costs nothing but the skip itself
				if (f > 0) {
					std::int32_t skip = 0;
					while (i + skip < framePixels && scaled[i + skip] == previousScaled[i + skip]) {
						skip++;
					}
					if (skip > 0) {
						EmitSkip(payload, skip);
						i += skip;
						continue;
					}
				}

				// A run of one repeated value, but only where it beats storing the bytes themselves
				std::int32_t run = 1;
				while (i + run < framePixels && scaled[i + run] == scaled[i]
						&& (f == 0 || scaled[i + run] != previousScaled[i + run])) {
					run++;
				}
				if (run >= VideoFormat::CommandRunMinLength) {
					EmitRun(payload, scaled[i], run);
					i += run;
					continue;
				}

				// Otherwise gather literals up to the next skip or run worth emitting
				std::int32_t literal = 0;
				while (i + literal < framePixels) {
					std::int32_t j = i + literal;
					if (f > 0 && scaled[j] == previousScaled[j]) {
						std::int32_t skip = 0;
						while (j + skip < framePixels && scaled[j + skip] == previousScaled[j + skip]) {
							skip++;
						}
						if (skip >= 4) {
							break;
						}
					}
					std::int32_t sameRun = 1;
					while (j + sameRun < framePixels && scaled[j + sameRun] == scaled[j]) {
						sameRun++;
					}
					if (sameRun >= VideoFormat::CommandRunMinLength + 1) {
						break;
					}
					literal += sameRun;
				}
				if (literal <= 0) {
					literal = 1;
				}
				EmitLiterals(payload, &scaled[i], literal);
				i += literal;
			}
			arrayAppend(payload, std::uint8_t(VideoFormat::CommandEndOfFrame));

			arrayAppend(frameSizes, std::uint32_t(payload.size()));
			arrayAppend(frameData, arrayView(payload));

			std::memcpy(previousScaled.get(), scaled.get(), std::size_t(targetWidth) * targetHeight);
			framesWritten++;
		}

		auto output_ = fs::Open(targetPath, FileAccess::Write);
		if (!output_->IsValid()) {
			LOGE("Cannot open \"{}\" for writing", targetPath);
			return false;
		}

		output_->WriteValueAsLE<std::uint64_t>(VideoFormat::Signature);
		output_->WriteValue<std::uint8_t>(ContentFileType::Video);
		output_->WriteValueAsLE<std::uint16_t>(VideoFormat::CurrentVersion);
		output_->WriteValueAsLE<std::uint16_t>(std::uint16_t(targetWidth));
		output_->WriteValueAsLE<std::uint16_t>(std::uint16_t(targetHeight));
		output_->WriteValueAsLE<std::uint16_t>(frameDelay);
		output_->WriteValueAsLE<std::uint32_t>(std::uint32_t(framesWritten));
		output_->WriteValue<std::uint8_t>(VideoFormat::PixelFormatIndexed8);
		output_->WriteValue<std::uint8_t>(VideoFormat::CodecDeltaRle);
		output_->WriteValueAsLE<std::uint16_t>(0);	// No extension fields yet

		std::size_t position = 0;
		for (std::uint32_t frameSize : frameSizes) {
			output_->WriteValueAsLE<std::uint32_t>(frameSize);
			output_->Write(&frameData[position], std::int64_t(frameSize));
			position += frameSize;
		}

		if (!output_->IsValid()) {
			LOGE("Cannot write \"{}\"", targetPath);
			return false;
		}

		LOGI("Recompressed \"{}\": {}x{} -> {}x{}, {} frames, {} KB -> {} KB", fs::GetFileName(sourcePath),
			width, height, targetWidth, targetHeight, framesWritten,
			std::int32_t(fileSize / 1024), std::int32_t(output_->GetPosition() / 1024));
		return true;
	}
}
