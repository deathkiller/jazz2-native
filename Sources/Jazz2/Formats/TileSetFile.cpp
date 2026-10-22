#include "TileSetFile.h"
#include "../Compatibility/JJ2Anims.h"

#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#include <algorithm>
#include <cstring>
#include <memory>

using namespace Death::Containers;
using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Formats
{
	void TileSetData::Resize(std::int32_t tileCount)
	{
		TileCount = tileCount;
		Images.resize(std::size_t(tileCount) * ImageBytesPerTile);
		TrueColor.resize(std::size_t(tileCount));
		Masks.resize(std::size_t(tileCount) * MaskBytesPerTile);
	}

	bool TileSetData::HasTrueColorTiles() const
	{
		for (std::int32_t i = 0; i < TileCount; i++) {
			if (TrueColor[i] != 0) {
				return true;
			}
		}
		return false;
	}

	bool TileSetFile::ReadHeader(Stream& s, TileSetHeader& header)
	{
		// A header cut short would read as zeroes, which can make a valid-looking header of nothing
		constexpr std::int64_t HeaderSize = 8 + 2 + 1 + 1 + 1 + 4 + 4 + 2 + 4;
		const std::int64_t start = s.GetPosition();
		const std::int64_t size = s.GetSize();
		if (start >= 0 && size >= 0 && size - start < HeaderSize) {
			return false;
		}

		std::uint64_t signature = s.ReadValueAsLE<std::uint64_t>();
		std::uint16_t signature2 = s.ReadValueAsLE<std::uint16_t>();
		std::uint8_t version = s.ReadValue<std::uint8_t>();
		if (signature != Signature || signature2 != Signature2 || version != CurrentVersion) {
			return false;
		}

		header.Flags = s.ReadValue<std::uint8_t>();
		header.ChannelCount = s.ReadValue<std::uint8_t>();
		header.Width = s.ReadValueAsLE<std::uint32_t>();
		header.Height = s.ReadValueAsLE<std::uint32_t>();
		header.TileCount = s.ReadValueAsLE<std::uint16_t>();
		header.BodySize = s.ReadValueAsLE<std::int32_t>();

		const std::int64_t position = s.GetPosition();
		return Validate(header, size >= 0 && position >= 0 ? size - position : -1);
	}

	bool TileSetFile::Validate(const TileSetHeader& header, std::int64_t remainingBytes)
	{
		// The image content is decoded into buffers of the declared size, and the atlas is cut from it by whole tiles
		if (header.ChannelCount != 1 && header.ChannelCount != 4) {
			return false;
		}
		if (header.Width < std::uint32_t(TileSize) || header.Width > MaxImageWidth || (header.Width % TileSize) != 0 ||
			header.Height < std::uint32_t(TileSize) || (header.Height % TileSize) != 0) {
			return false;
		}
		// At least a tile (no writer makes a tile set of none), a cell for each, and at most a partly filled row more
		// than the count can hold
		const std::uint64_t columns = header.Width / TileSize;
		const std::uint64_t cells = columns * (header.Height / TileSize);
		if (header.TileCount == 0 || cells < header.TileCount || cells > std::uint64_t(UINT16_MAX) + 1 + columns) {
			return false;
		}
		// The deflated block always holds at least the palette
		if (header.BodySize <= 0 || (remainingBytes >= 0 && header.BodySize > remainingBytes)) {
			return false;
		}
		return true;
	}

	std::int32_t TileSetFile::GetBestColumnCount(std::int32_t tileCount)
	{
		// Rearrange the tiles into the row count whose image wastes the least space once rounded up to power-of-two
		// dimensions, within 1024x1024 where the tiles fit into it. No texture is decided by this, though: the image is
		// never uploaded as it is. The runtime copies every tile into an atlas of its own with a pixel of padding
		// around each (ContentResolver::BuildTilesetDiffuse) - on the consoles 15 tiles (510 px) per row and chunks of
		// at most 512 rows, so each chunk is one page even on the PSP and no tile ever lies across a page line;
		// elsewhere with this row count, split into textures the device can hold. It derives the row count back from
		// the width, so this is free to change, and a tile set with more tiles than 1024x1024 holds is just as fine.
		constexpr std::int32_t MaxTextureSize = 1024;
		std::int32_t bestColumns = std::max<std::int32_t>(1, MaxTextureSize / TileSize);
		std::int64_t bestCost = INT64_MAX;
		for (std::int32_t columns = 1; columns <= tileCount; columns++) {
			const std::int32_t candidateWidth = columns * TileSize;
			const std::int32_t candidateHeight = ((tileCount - 1) / columns + 1) * TileSize;
			if (candidateWidth > MaxTextureSize || candidateHeight > MaxTextureSize) {
				continue;
			}

			std::int32_t paddedWidth = 1, paddedHeight = 1;
			while (paddedWidth < candidateWidth) { paddedWidth <<= 1; }
			while (paddedHeight < candidateHeight) { paddedHeight <<= 1; }

			// Among layouts that pad to the same texture, prefer the one with fewer unused cells
			const std::int64_t emptyCells = std::int64_t(columns) * ((tileCount - 1) / columns + 1) - tileCount;
			const std::int64_t cost = std::int64_t(paddedWidth) * paddedHeight * 1024 + emptyCells;
			if (cost < bestCost) {
				bestCost = cost;
				bestColumns = columns;
			}
		}
		return bestColumns;
	}

	bool TileSetFile::ReadPalette(Stream& s, std::uint32_t* palette)
	{
		TileSetHeader header;
		if (!ReadHeader(s, header)) {
			return false;
		}

		// The palette comes first in the deflated block, the rest of it isn't needed
		DeflateStream uc(s, header.BodySize);
		std::uint8_t paletteBytes[256 * 4];
		if (uc.Read(paletteBytes, sizeof(paletteBytes)) != std::int64_t(sizeof(paletteBytes))) {
			return false;
		}
		for (std::int32_t i = 0; i < 256; i++) {
			palette[i] = std::uint32_t(paletteBytes[i * 4]) | (std::uint32_t(paletteBytes[i * 4 + 1]) << 8) |
				(std::uint32_t(paletteBytes[i * 4 + 2]) << 16) | (std::uint32_t(paletteBytes[i * 4 + 3]) << 24);
		}
		return true;
	}

	bool TileSetFile::Read(Stream& s, TileSetData& data)
	{
		TileSetHeader header;
		if (!ReadHeader(s, header) || (header.ChannelCount != 1 && header.ChannelCount != 4) ||
			header.Width < TileSize || (header.Width % TileSize) != 0 || header.Height < TileSize) {
			return false;
		}
#if !defined(WITH_LZ4)
		if ((header.Flags & ImageLz4) != 0) {
			return false;
		}
#endif

		// The whole image is decoded at once, so a few bytes must not claim a huge one: an operation of the image content
		// gives 64 pixels at most, and a block of LZ4 (one for a row of tiles) takes 5 bytes at least
		const std::int64_t size = s.GetSize();
		const std::int64_t position = s.GetPosition();
		if (size >= 0 && position >= 0) {
			const std::uint64_t imageBytesLeft = std::uint64_t(size - position - header.BodySize);
			const std::uint64_t imageBytesNeeded = ((header.Flags & ImageLz4) != 0
				? std::uint64_t(header.Height / TileSize) * 5
				: (std::uint64_t(header.Width) * header.Height + 63) / 64);
			if (imageBytesLeft < imageBytesNeeded) {
				return false;
			}
		}

		const std::int32_t tileCount = header.TileCount;
		const std::int32_t columns = std::int32_t(header.Width / TileSize);
		data.Resize(tileCount);
		data.Columns = columns;

		{
			// Every part of the block is checked to come whole, a damaged one would read as zeroes otherwise
			DeflateStream uc(s, header.BodySize);
			std::uint8_t paletteBytes[256 * 4];
			if (uc.Read(paletteBytes, sizeof(paletteBytes)) != std::int64_t(sizeof(paletteBytes))) {
				return false;
			}
			for (std::int32_t i = 0; i < 256; i++) {
				data.Palette[i] = std::uint32_t(paletteBytes[i * 4]) | (std::uint32_t(paletteBytes[i * 4 + 1]) << 8) |
					(std::uint32_t(paletteBytes[i * 4 + 2]) << 16) | (std::uint32_t(paletteBytes[i * 4 + 3]) << 24);
			}

			SmallVector<std::uint8_t, 0> is32bit;
			is32bit.resize((tileCount + 7) / 8);
			if (uc.Read(is32bit.data(), std::int64_t(is32bit.size())) != std::int64_t(is32bit.size())) {
				return false;
			}
			for (std::int32_t i = 0; i < tileCount; i++) {
				data.TrueColor[i] = ((is32bit[i / 8] >> (i % 8)) & 0x01);
			}

			std::uint8_t maskSizeBytes[4];
			if (uc.Read(maskSizeBytes, sizeof(maskSizeBytes)) != std::int64_t(sizeof(maskSizeBytes))) {
				return false;
			}
			std::uint32_t maskSize = std::uint32_t(maskSizeBytes[0]) | (std::uint32_t(maskSizeBytes[1]) << 8) |
				(std::uint32_t(maskSizeBytes[2]) << 16) | (std::uint32_t(maskSizeBytes[3]) << 24);
			std::uint32_t maskBytes = std::min(maskSize, std::uint32_t(data.Masks.size()));
			if (uc.Read(data.Masks.data(), maskBytes) != std::int64_t(maskBytes)) {
				return false;
			}

			if (!uc.IsValid()) {
				return false;
			}
			// The image content follows the compressed block
			uc.CeaseReading();
		}

		// Three bytes of slack, the decoder writes every pixel with four bytes (see JJ2Anims::ReadImageContent())
		const std::size_t imageBytes = std::size_t(header.Width) * header.Height * header.ChannelCount;
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(imageBytes + 3);
		if ((header.Flags & ImageLz4) != 0) {
#if defined(WITH_LZ4)
			// One block per row of tiles
			const std::size_t bandBytes = std::size_t(header.Width) * Compatibility::JJ2Anims::TilesetLz4BandRows * header.ChannelCount;
			for (std::size_t offset = 0; offset < imageBytes; offset += bandBytes) {
				const std::size_t bytes = std::min(bandBytes, imageBytes - offset);
				if (!Compatibility::JJ2Anims::ReadImageContentLz4(s, &pixels[offset], std::int32_t(bytes))) {
					return false;
				}
			}
#endif
		} else {
			Compatibility::JJ2Anims::ReadImageContent(s, pixels.get(), std::int32_t(header.Width), std::int32_t(header.Height), header.ChannelCount);
		}

		const std::int32_t rows = std::int32_t(header.Height / TileSize);
		for (std::int32_t i = 0; i < tileCount; i++) {
			const std::int32_t ox = (i % columns) * TileSize;
			const std::int32_t oy = (i / columns) * TileSize;
			std::uint8_t* tile = &data.Images[std::size_t(i) * TileSetData::ImageBytesPerTile];
			if (oy / TileSize >= rows) {
				// The image is too short for the tile count, the tile stays empty
				continue;
			}

			for (std::int32_t y = 0; y < TileSize; y++) {
				for (std::int32_t x = 0; x < TileSize; x++) {
					const std::size_t src = (std::size_t(header.Width) * (oy + y) + ox + x) * header.ChannelCount;
					if (data.TrueColor[i] != 0) {
						std::uint8_t* dst = &tile[(y * TileSize + x) * 4];
						if (header.ChannelCount == 4) {
							std::memcpy(dst, &pixels[src], 4);
						} else {
							std::memset(dst, 0, 4);
						}
					} else {
						// An 8-bit tile keeps its index in the first channel, in all of them in an RGBA image
						tile[y * TileSize + x] = pixels[src];
					}
				}
			}
		}

		return true;
	}

	bool TileSetFile::Write(Stream& target, const TileSetData& data, bool lz4)
	{
		const std::int32_t tileCount = data.TileCount;
		if (tileCount <= 0 || tileCount > UINT16_MAX) {
			return false;
		}

		// Put together in memory and written to the target at once, so a failed write (such as on a full disk) is noticed
		MemoryStream so(1024 * 1024);

		// No reader takes a wider image (see Validate())
		const std::int32_t columns = std::min<std::int32_t>(data.Columns > 0 ? data.Columns : GetBestColumnCount(tileCount),
			std::int32_t(MaxImageWidth / TileSize));
		const std::int32_t width = columns * TileSize;
		const std::int32_t height = ((tileCount - 1) / columns + 1) * TileSize;

		std::uint8_t flags = HasMask | HasPalette;
		if (lz4) {
#if defined(WITH_LZ4)
			flags |= ImageLz4;
#else
			return false;
#endif
		}

		so.WriteValueAsLE<std::uint64_t>(Signature);
		so.WriteValueAsLE<std::uint16_t>(Signature2);
		so.WriteValue<std::uint8_t>(CurrentVersion);
		so.WriteValue<std::uint8_t>(flags);

		// 8-bit palette tile sets (no true color tiles) are written as a single index channel: the palette is stored
		// in the file and entry 0 is fixed transparent, so the loader reproduces every color and on/off transparency
		// from the index alone - the alpha channel is redundant. Tile sets with any true color tile stay RGBA.
		const std::uint8_t channelCount = (data.HasTrueColorTiles() ? 4 : 1);
		so.WriteValue<std::uint8_t>(channelCount);
		so.WriteValueAsLE<std::uint32_t>(width);
		so.WriteValueAsLE<std::uint32_t>(height);
		so.WriteValueAsLE<std::uint16_t>(std::uint16_t(tileCount));

		MemoryStream ms(1024 * 1024);
		{
			DeflateWriter co(ms);

			for (std::int32_t i = 0; i < 256; i++) {
				co.WriteValueAsLE<std::uint32_t>(data.Palette[i]);
			}

			// Mark individual tiles as true color or 8-bit
			for (std::int32_t i = 0; i < (tileCount + 7) / 8; i++) {
				std::uint32_t is32bitAggregated = 0;
				std::int32_t tilesPerByte = std::min<std::int32_t>(8, tileCount - i * 8);
				for (std::int32_t j = 0; j < tilesPerByte; j++) {
					if (data.TrueColor[i * 8 + j] != 0) {
						is32bitAggregated |= (1 << j);
					}
				}
				co.WriteValue<std::uint8_t>(std::uint8_t(is32bitAggregated));
			}

			// Mask
			co.WriteValueAsLE<std::uint32_t>(std::uint32_t(tileCount * TileSetData::MaskBytesPerTile));
			co.Write(data.Masks.data(), std::int64_t(tileCount) * TileSetData::MaskBytesPerTile);
		}

		so.WriteValueAsLE<std::int32_t>(std::int32_t(ms.GetSize()));
		so.Write(ms.GetBuffer(), ms.GetSize());

		// Diffuse
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(std::size_t(width) * height * channelCount);
		for (std::int32_t i = 0; i < tileCount; i++) {
			const std::uint8_t* tile = &data.Images[std::size_t(i) * TileSetData::ImageBytesPerTile];

			const std::int32_t ox = (i % columns) * TileSize;
			const std::int32_t oy = (i / columns) * TileSize;
			if (data.TrueColor[i] != 0) {
				for (std::int32_t y = 0; y < TileSize; y++) {
					for (std::int32_t x = 0; x < TileSize; x++) {
						const std::uint8_t* src = &tile[(y * TileSize + x) * 4];
						std::size_t pixelIdx = (std::size_t(width) * (oy + y) + ox + x) * 4;
						pixels[pixelIdx] = src[0];
						pixels[pixelIdx + 1] = src[1];
						pixels[pixelIdx + 2] = src[2];
						pixels[pixelIdx + 3] = src[3];
					}
				}
			} else if (channelCount == 1) {
				// Single index channel; the palette (incl. fixed-transparent entry 0) is applied at load time
				for (std::int32_t y = 0; y < TileSize; y++) {
					for (std::int32_t x = 0; x < TileSize; x++) {
						pixels[std::size_t(width) * (oy + y) + ox + x] = tile[y * TileSize + x];
					}
				}
			} else {
				for (std::int32_t y = 0; y < TileSize; y++) {
					for (std::int32_t x = 0; x < TileSize; x++) {
						const std::uint8_t src = tile[y * TileSize + x];
						std::size_t pixelIdx = (std::size_t(width) * (oy + y) + ox + x) * 4;
						pixels[pixelIdx] = src;
						pixels[pixelIdx + 1] = src;
						pixels[pixelIdx + 2] = src;
						pixels[pixelIdx + 3] = (src == 0 ? 0 : 255);
					}
				}
			}
		}

#if defined(WITH_LZ4)
		if (lz4) {
			static_assert(Compatibility::JJ2Anims::TilesetLz4BandRows == TileSize, "A band has to be one row of tiles");
			Compatibility::JJ2Anims::WriteImageContentLz4(so, pixels.get(), width, height, channelCount, Compatibility::JJ2Anims::TilesetLz4BandRows);
		} else
#endif
		{
			Compatibility::JJ2Anims::WriteImageContent(so, pixels.get(), width, height, channelCount);
		}

		const std::int64_t size = so.GetSize();
		return (target.Write(so.GetBuffer(), size) == size && target.Flush() && target.IsValid());
	}
}
