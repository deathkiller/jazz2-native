#include "SpriteRepacker.h"

#include "../../Jazz2/Compatibility/JJ2Anims.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <Containers/SmallVector.h>
#include <Core/Logger.h>

using namespace Jazz2::Compatibility;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Gap left between two frames, the same the converted sheets leave */
		constexpr std::int32_t FrameSpacing = 2;

		/** @brief Returns whether a pixel of a sheet with the given channel count covers anything */
		inline bool IsCovered(const std::uint8_t* pixel, std::int32_t channelCount)
		{
			switch (channelCount) {
				case 1: return (pixel[0] != 0);		// Palette index, 0 is transparent
				case 2: return (pixel[1] != 0);		// Palette index and alpha
				case 3: return true;				// No transparency at all
				default: return (pixel[3] != 0);
			}
		}
	}

	bool SpriteRepacker::TryRepack(Stream& input, MemoryStream& output, StringView name)
	{
		// Header of a sprite sheet, see ContentResolver::RequestGraphicsAura()
		if (input.GetSize() < 39) {
			return false;
		}
		const std::uint64_t signature1 = input.ReadValueAsLE<std::uint64_t>();
		const std::uint16_t signature2 = input.ReadValueAsLE<std::uint16_t>();
		const std::uint8_t version = input.ReadValue<std::uint8_t>();
		const std::uint8_t flags = input.ReadValue<std::uint8_t>();
		if (signature1 != 0xB8EF8498E2BFBBEF || signature2 != 0x208F || version != 2 || (flags & 0x80) != 0x80 || (flags & 0x04) == 0x04 ||
			(flags & JJ2Anims::ImageContentLz4Flag) == JJ2Anims::ImageContentLz4Flag) {
			return false;
		}

		const std::uint8_t channelCount = input.ReadValue<std::uint8_t>();
		const std::uint32_t frameWidth = input.ReadValueAsLE<std::uint32_t>();
		const std::uint32_t frameHeight = input.ReadValueAsLE<std::uint32_t>();
		const std::uint8_t columns = input.ReadValue<std::uint8_t>();
		const std::uint8_t rows = input.ReadValue<std::uint8_t>();
		const std::uint16_t frameCount = input.ReadValueAsLE<std::uint16_t>();
		const std::uint16_t animDuration = input.ReadValueAsLE<std::uint16_t>();
		// Hotspot, coldspot and gunspot, all relative to the cell and so unaffected by where it moves
		std::uint16_t spots[6];
		for (std::uint16_t& spot : spots) {
			spot = input.ReadValueAsLE<std::uint16_t>();
		}

		const std::int32_t sheetWidth = std::int32_t(frameWidth) * columns;
		const std::int32_t sheetHeight = std::int32_t(frameHeight) * rows;
		if (frameCount <= 1 || (sheetWidth <= JJ2Anims::SheetPageSize && sheetHeight <= JJ2Anims::SheetPageSize)) {
			return false;
		}
		if (channelCount < 1 || channelCount > 4 || frameWidth > UINT16_MAX || frameHeight > UINT16_MAX ||
			frameCount > std::int32_t(columns) * rows) {
			LOGW("Cannot repack \"{}\": Unexpected layout", name);
			return false;
		}

		// A few bytes of slack past the last pixel, which the decoder has been documented to need
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(std::size_t(sheetWidth) * sheetHeight * channelCount + 3);
		JJ2Anims::ReadImageContent(input, pixels.get(), sheetWidth, sheetHeight, channelCount);

		// Trim every frame to what it covers, remembering where that begins inside the cell
		SmallVector<JJ2Anims::PackedFrame, 0> frames(frameCount);
		for (std::int32_t i = 0; i < frameCount; i++) {
			const std::int32_t cellX = (i % columns) * std::int32_t(frameWidth);
			const std::int32_t cellY = (i / columns) * std::int32_t(frameHeight);
			std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = -1, maxY = -1;
			for (std::int32_t y = 0; y < std::int32_t(frameHeight); y++) {
				const std::uint8_t* row = &pixels[(std::size_t(cellY + y) * sheetWidth + cellX) * channelCount];
				for (std::int32_t x = 0; x < std::int32_t(frameWidth); x++) {
					if (IsCovered(row + x * channelCount, channelCount)) {
						minX = std::min(minX, x);
						maxX = std::max(maxX, x);
						minY = std::min(minY, y);
						maxY = std::max(maxY, y);
					}
				}
			}

			JJ2Anims::PackedFrame& frame = frames[i];
			frame.X = frame.Y = 0;
			if (maxX < 0) {
				// Nothing to draw, but it still needs an area - a single transparent pixel
				frame.W = frame.H = 1;
				frame.OffsetX = frame.OffsetY = 0;
			} else {
				frame.W = maxX - minX + 1;
				frame.H = maxY - minY + 1;
				frame.OffsetX = minX;
				frame.OffsetY = minY;
			}
		}

		std::int32_t packedWidth, packedHeight;
		if (!JJ2Anims::PackRectangles(frames, FrameSpacing, packedWidth, packedHeight)) {
			LOGW("Cannot repack \"{}\": The frames do not fit into a {}x{} sheet", name, JJ2Anims::MaxSheetSize, JJ2Anims::MaxSheetSize);
			return false;
		}
		if (std::any_of(frames.begin(), frames.end(), JJ2Anims::LiesAcrossPageLine)) {
			LOGW("Frames of \"{}\" still lie across a {}-pixel page line of its {}x{} sheet, a platform that splits the texture into pages draws them cut off",
				name, JJ2Anims::SheetPageSize, packedWidth, packedHeight);
		}

		std::unique_ptr<std::uint8_t[]> packedPixels = std::make_unique<std::uint8_t[]>(std::size_t(packedWidth) * packedHeight * channelCount);
		std::memset(packedPixels.get(), 0, std::size_t(packedWidth) * packedHeight * channelCount);
		for (std::int32_t i = 0; i < frameCount; i++) {
			const JJ2Anims::PackedFrame& frame = frames[i];
			const std::int32_t sourceX = (i % columns) * std::int32_t(frameWidth) + frame.OffsetX;
			const std::int32_t sourceY = (i / columns) * std::int32_t(frameHeight) + frame.OffsetY;
			for (std::int32_t y = 0; y < frame.H; y++) {
				std::memcpy(&packedPixels[(std::size_t(frame.Y + y) * packedWidth + frame.X) * channelCount],
					&pixels[(std::size_t(sourceY + y) * sheetWidth + sourceX) * channelCount], std::size_t(frame.W) * channelCount);
			}
		}

#if defined(WITH_LZ4)
		const bool lz4 = (JJ2Anims::PreferredImageCompression == JJ2Anims::ImageCompression::Lz4);
#else
		const bool lz4 = false;
#endif

		output.WriteValueAsLE<std::uint64_t>(signature1);
		output.WriteValueAsLE<std::uint16_t>(signature2);
		output.WriteValue<std::uint8_t>(version);
		// The frames don't form a grid any more, so their positions are listed after the header
		output.WriteValue<std::uint8_t>(flags | 0x04 | (lz4 ? JJ2Anims::ImageContentLz4Flag : 0));
		output.WriteValue<std::uint8_t>(channelCount);
		output.WriteValueAsLE<std::uint32_t>(frameWidth);
		output.WriteValueAsLE<std::uint32_t>(frameHeight);
		output.WriteValue<std::uint8_t>(columns);
		output.WriteValue<std::uint8_t>(rows);
		output.WriteValueAsLE<std::uint16_t>(frameCount);
		output.WriteValueAsLE<std::uint16_t>(animDuration);
		for (std::uint16_t spot : spots) {
			output.WriteValueAsLE<std::uint16_t>(spot);
		}

		output.WriteValueAsLE<std::uint16_t>(std::uint16_t(packedWidth));
		output.WriteValueAsLE<std::uint16_t>(std::uint16_t(packedHeight));
		for (const JJ2Anims::PackedFrame& frame : frames) {
			output.WriteValueAsLE<std::uint16_t>(std::uint16_t(frame.X));
			output.WriteValueAsLE<std::uint16_t>(std::uint16_t(frame.Y));
			output.WriteValueAsLE<std::uint16_t>(std::uint16_t(frame.W));
			output.WriteValueAsLE<std::uint16_t>(std::uint16_t(frame.H));
			output.WriteValueAsLE<std::int16_t>(std::int16_t(frame.OffsetX));
			output.WriteValueAsLE<std::int16_t>(std::int16_t(frame.OffsetY));
		}

#if defined(WITH_LZ4)
		if (lz4) {
			JJ2Anims::WriteImageContentLz4(output, packedPixels.get(), packedWidth, packedHeight, channelCount, packedHeight);
		} else
#endif
		{
			JJ2Anims::WriteImageContent(output, packedPixels.get(), packedWidth, packedHeight, channelCount);
		}

		LOGI("Repacked \"{}\" from a {}x{} grid into a {}x{} sheet", name, sheetWidth, sheetHeight, packedWidth, packedHeight);
		return true;
	}

	bool SpriteRepacker::TryConvertToLz4(Stream& input, MemoryStream& output, StringView name)
	{
#if defined(WITH_LZ4)
		// Header of a sprite sheet, see ContentResolver::RequestGraphicsAura()
		constexpr std::int32_t HeaderSize = 39;
		const std::int64_t fileSize = input.GetSize();
		if (fileSize < HeaderSize) {
			return false;
		}
		std::uint8_t header[HeaderSize];
		if (input.Read(header, HeaderSize) != HeaderSize) {
			return false;
		}
		std::uint64_t signature1 = 0;
		for (std::int32_t i = 7; i >= 0; i--) {
			signature1 = (signature1 << 8) | header[i];
		}
		const std::uint16_t signature2 = std::uint16_t(header[8] | (header[9] << 8));
		const std::uint8_t version = header[10];
		const std::uint8_t flags = header[11];
		if (signature1 != 0xB8EF8498E2BFBBEF || signature2 != 0x208F || version != 2 || (flags & 0x80) != 0x80 ||
			(flags & JJ2Anims::ImageContentLz4Flag) == JJ2Anims::ImageContentLz4Flag) {
			return false;
		}

		const std::uint8_t channelCount = header[12];
		auto readU32 = [&header](std::int32_t at) {
			return std::uint32_t(header[at]) | (std::uint32_t(header[at + 1]) << 8) | (std::uint32_t(header[at + 2]) << 16) | (std::uint32_t(header[at + 3]) << 24);
		};
		const std::uint32_t frameWidth = readU32(13);
		const std::uint32_t frameHeight = readU32(17);
		const std::uint8_t columns = header[21];
		const std::uint8_t rows = header[22];
		const std::uint16_t frameCount = std::uint16_t(header[23] | (header[24] << 8));

		// A tightly packed sheet carries its own size and the rectangle of every frame, copied as they are
		SmallVector<std::uint8_t, 0> layout;
		std::int32_t sheetWidth, sheetHeight;
		if ((flags & 0x04) == 0x04) {
			layout.resize(4 + std::size_t(frameCount) * 12);
			if (input.Read(layout.data(), std::int64_t(layout.size())) != std::int64_t(layout.size())) {
				return false;
			}
			sheetWidth = layout[0] | (layout[1] << 8);
			sheetHeight = layout[2] | (layout[3] << 8);
		} else {
			sheetWidth = std::int32_t(frameWidth) * columns;
			sheetHeight = std::int32_t(frameHeight) * rows;
		}
		if (channelCount < 1 || channelCount > 4 || sheetWidth <= 0 || sheetHeight <= 0 || sheetWidth > JJ2Anims::MaxSheetSize * 4 ||
			sheetHeight > JJ2Anims::MaxSheetSize * 4) {
			LOGW("Cannot convert \"{}\" to LZ4: Unexpected layout", name);
			return false;
		}

		// A few bytes of slack past the last pixel, which the decoder has been documented to need
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(std::size_t(sheetWidth) * sheetHeight * channelCount + 3);
		JJ2Anims::ReadImageContent(input, pixels.get(), sheetWidth, sheetHeight, channelCount);

		header[11] = std::uint8_t(flags | JJ2Anims::ImageContentLz4Flag);
		output.Write(header, HeaderSize);
		if (!layout.empty()) {
			output.Write(layout.data(), std::int64_t(layout.size()));
		}
		JJ2Anims::WriteImageContentLz4(output, pixels.get(), sheetWidth, sheetHeight, channelCount, sheetHeight);
		return true;
#else
		static_cast<void>(input); static_cast<void>(output); static_cast<void>(name);
		return false;
#endif
	}
}
