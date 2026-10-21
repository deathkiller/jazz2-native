#include "JJ2Tileset.h"
#include "JJ2Anims.h"
#include "JJ2Block.h"
#include "../Formats/TileSetFile.h"

#include <Containers/SmallVector.h>
#include <IO/FileSystem.h>

#include <algorithm>

using namespace Death::IO;

namespace Jazz2::Compatibility
{
	bool JJ2Tileset::Open(StringView path, bool strictParser)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid()) {
			LOGE("Cannot open file \"{}\" for reading", path);
			return false;
		}

		BlockSizes sizes;
		if (!ReadHeader(s, strictParser, sizes)) {
			return false;
		}

		JJ2Block infoBlock(s, sizes.InfoPacked, sizes.InfoUnpacked);
		std::uint32_t palette[PaletteSize];
		SmallVector<TileOffsets, 0> offsets;
		std::int32_t tileCount = ReadMetadata(infoBlock, palette, offsets);

		// The tiles go straight into the converted form, which is then written as it is (see Convert())
		_data.Resize(tileCount);
		ConvertPalette(palette, _data.Palette);
		{
			// The blocks are read one after another, the images are done with before the masks are unpacked
			JJ2Block imageBlock(s, sizes.ImagePacked, sizes.ImageUnpacked);
			JJ2Block alphaBlock(s, sizes.AlphaPacked, sizes.AlphaUnpacked);
			for (std::int32_t i = 0; i < tileCount; i++) {
				_data.TrueColor[i] = ((offsets[i].Image & Is32bitTile) != 0 ? 1 : 0);
				DecodeTileImage(imageBlock, alphaBlock, offsets[i].Image, offsets[i].Alpha,
					&_data.Images[std::size_t(i) * Formats::TileSetData::ImageBytesPerTile]);
			}
		}

		JJ2Block maskBlock(s, sizes.MaskPacked, sizes.MaskUnpacked);
		LoadMaskData(maskBlock, offsets);

		return true;
	}

	bool JJ2Tileset::ConvertTiles(StringView path, ArrayView<const std::int32_t> tiles, Formats::TileSetData& data, std::int32_t& tileCount)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid()) {
			LOGE("Cannot open file \"{}\" for reading", path);
			return false;
		}

		// Only the header is kept of the tile set, the tiles aren't loaded into it
		JJ2Tileset tileset;
		BlockSizes sizes;
		if (!tileset.ReadHeader(s, false, sizes)) {
			return false;
		}

		// The collision masks aren't needed, so their block isn't even read
		JJ2Block infoBlock(s, sizes.InfoPacked, sizes.InfoUnpacked);
		JJ2Block imageBlock(s, sizes.ImagePacked, sizes.ImageUnpacked);
		JJ2Block alphaBlock(s, sizes.AlphaPacked, sizes.AlphaUnpacked);

		std::uint32_t palette[PaletteSize];
		SmallVector<TileOffsets, 0> offsets;
		tileCount = tileset.ReadMetadata(infoBlock, palette, offsets);

		data.Resize(std::int32_t(tiles.size()));
		ConvertPalette(palette, data.Palette);
		for (std::size_t i = 0; i < tiles.size(); i++) {
			std::int32_t tile = tiles[i];
			if (tile < 0 || tile >= tileCount) {
				continue;
			}
			data.TrueColor[i] = ((offsets[tile].Image & Is32bitTile) != 0 ? 1 : 0);
			DecodeTileImage(imageBlock, alphaBlock, offsets[tile].Image, offsets[tile].Alpha, &data.Images[i * Formats::TileSetData::ImageBytesPerTile]);
		}

		return true;
	}

	bool JJ2Tileset::ReadHeader(std::unique_ptr<Stream>& s, bool strictParser, BlockSizes& sizes)
	{
		// Skip copyright notice
		s->Seek(180, SeekOrigin::Current);

		JJ2Block headerBlock(s, 262 - 180);

		std::uint32_t magic = headerBlock.ReadUInt32();
		DEATH_ASSERT(magic == 0x454C4954 /*TILE*/, "Invalid magic string", false);

		std::uint32_t signature = headerBlock.ReadUInt32();
		DEATH_ASSERT(signature == 0xAFBEADDE, "Invalid signature", false);

		_name = headerBlock.ReadString(32, true);

		std::uint16_t versionNum = headerBlock.ReadUInt16();
		_version = (versionNum == 0x300 ? JJ2Version::PlusExtension : (versionNum <= 0x200 ? JJ2Version::BaseGame : JJ2Version::TSF));

		std::int32_t recordedSize = headerBlock.ReadInt32();
		DEATH_ASSERT(!strictParser || s->GetSize() == recordedSize, "Unexpected file size", false);

		// Get the CRC; would check here if it matches if we knew what variant it is AND what it applies to
		// Test file across all CRC32 variants + Adler had no matches to the value obtained from the file
		// so either the variant is something else or the CRC is not applied to the whole file but on a part
		/*int recordedCRC =*/ headerBlock.ReadInt32();

		// Read the lengths, the blocks are uncompressed by the caller
		sizes.InfoPacked = headerBlock.ReadInt32();
		sizes.InfoUnpacked = headerBlock.ReadInt32();
		sizes.ImagePacked = headerBlock.ReadInt32();
		sizes.ImageUnpacked = headerBlock.ReadInt32();
		sizes.AlphaPacked = headerBlock.ReadInt32();
		sizes.AlphaUnpacked = headerBlock.ReadInt32();
		sizes.MaskPacked = headerBlock.ReadInt32();
		sizes.MaskUnpacked = headerBlock.ReadInt32();
		return true;
	}

	std::int32_t JJ2Tileset::ReadMetadata(JJ2Block& block, std::uint32_t* palette, SmallVectorImpl<TileOffsets>& offsets) const
	{
		ReadPalette(block, palette);

		// The file has a record for every tile the format can have, only the ones of the tile set are kept
		std::int32_t maxTiles = GetMaxSupportedTiles();
		std::int32_t tileCount = std::clamp<std::int32_t>(block.ReadInt32(), 0, maxTiles);
		std::int32_t unusedTiles = maxTiles - tileCount;
		offsets.resize_for_overwrite(tileCount);

		// Whether the tiles are opaque, and a block of unknown values, skip
		block.DiscardBytes(maxTiles + maxTiles);

		for (std::int32_t i = 0; i < tileCount; i++) {
			offsets[i].Image = block.ReadUInt32();
		}
		block.DiscardBytes(4 * unusedTiles);

		// Block of unknown values, skip
		block.DiscardBytes(4 * maxTiles);

		for (std::int32_t i = 0; i < tileCount; i++) {
			offsets[i].Alpha = block.ReadUInt32();
		}
		block.DiscardBytes(4 * unusedTiles);

		// Block of unknown values, skip
		block.DiscardBytes(4 * maxTiles);

		for (std::int32_t i = 0; i < tileCount; i++) {
			offsets[i].Mask = block.ReadUInt32();
		}
		block.DiscardBytes(4 * unusedTiles);

		// We don't care about the flipped masks, those are generated on runtime
		block.DiscardBytes(4 * maxTiles);

		return tileCount;
	}

	void JJ2Tileset::LoadMaskData(JJ2Block& block, ArrayView<const TileOffsets> offsets)
	{
		constexpr std::int32_t MaskBytes = Formats::TileSetData::MaskBytesPerTile;
		std::int32_t tileCount = _data.TileCount;
		auto mask = [this](std::int32_t tileIdx) {
			return &_data.Masks[std::size_t(tileIdx) * MaskBytes];
		};

		for (std::int32_t i = 0; i < tileCount; i++) {
			block.SeekTo(offsets[i].Mask);
			block.ReadRawBytes(mask(i), MaskBytes);
		}

		// Try to fix some known bugs in tilesets
		if ((_name == "Castle 1"_s || _name == "Castle 1 Night"_s) && tileCount > 189) {
			LOGI("Applying \"{}\" tileset mask fix", _name);
			// Spikes with empty mask
			std::memcpy(mask(189), mask(184), MaskBytes);
		} else if (_name == "Inferno Night"_s && tileCount > 156) {
			LOGI("Applying \"{}\" tileset mask fix", _name);
			// Solid tiles with empty mask
			static const std::int32_t SolidTilesWithEmptyMask[] = { 142, 143, 146, 152, 153, 156 };
			for (std::int32_t tileIdx : SolidTilesWithEmptyMask) {
				std::memset(mask(tileIdx), 0xFF, MaskBytes);
			}
		} else if ((_name == "Town House 1"_s || _name == "Town House 2"_s) && tileCount > 829) {
			LOGI("Applying \"{}\" tileset mask fix", _name);
			// Vine/chain with empty mask
			for (std::int32_t tileIdx = 826; tileIdx <= 829; tileIdx++) {
				std::uint8_t* tileMask = mask(tileIdx);
				if (std::all_of(tileMask, tileMask + MaskBytes, [](std::uint8_t p) { return p == 0; })) {
					for (std::int32_t y = 6; y < 6 + 3; y++) {
						for (std::int32_t x = 0; x < BlockSize / 8; x++) {
							tileMask[y * (BlockSize / 8) + x] = 0xFF;
						}
					}
				}
			}
		}
	}

	void JJ2Tileset::Convert(StringView targetPath) const
	{
		auto so = fs::Open(targetPath, FileAccess::Write);
		DEATH_ASSERT(so->IsValid(), "Cannot open file for writing", );

		// The image content goes in LZ4 blocks of one band of tiles each, see JJ2Anims::WriteImageContentLz4()
		bool lz4 = false;
#if defined(WITH_LZ4)
		lz4 = (JJ2Anims::PreferredImageCompression == JJ2Anims::ImageCompression::Lz4);
#endif
		Formats::TileSetFile::Write(*so, _data, lz4);
	}

	void JJ2Tileset::Convert(Formats::TileSetData& data) const
	{
		data = _data;
	}

	void JJ2Tileset::ReadPalette(JJ2Block& block, std::uint32_t* palette)
	{
		for (std::int32_t i = 0; i < PaletteSize; i++) {
			std::uint32_t color = block.ReadUInt32();
			color = (color & 0x00ffffff) | ((255 - ((color >> 24) & 0xff)) << 24);
			palette[i] = color;
		}
	}

	void JJ2Tileset::ConvertPalette(const std::uint32_t* palette, std::uint32_t* target)
	{
		std::memcpy(target, palette, PaletteSize * sizeof(std::uint32_t));

		bool hasAlphaChannel = false;
		for (std::int32_t i = 1; i < PaletteSize; i++) {
			if ((target[i] & 0xff000000) != 0) {
				hasAlphaChannel = true;
				break;
			}
		}
		if (!hasAlphaChannel) {
			for (std::int32_t i = 1; i < PaletteSize; i++) {
				target[i] |= 0xff000000;
			}
		}

		// The first palette entry is fixed to transparent black
		target[0] = 0x00000000;
	}

	void JJ2Tileset::DecodeTileImage(JJ2Block& imageBlock, JJ2Block& alphaBlock, std::uint32_t imageOffset, std::uint32_t alphaOffset, std::uint8_t* image)
	{
		std::uint8_t imageBuffer[BlockSize * BlockSize * 4];
		std::uint8_t maskBuffer[BlockSize * BlockSize / 8];

		imageBlock.SeekTo(imageOffset & ~Is32bitTile);

		if (imageOffset & Is32bitTile) {
			// 32-bit image
			imageBlock.ReadRawBytes(imageBuffer, BlockSize * BlockSize * 4);
		} else {
			// 8-bit palette image
			imageBlock.ReadRawBytes(imageBuffer, BlockSize * BlockSize);
		}

		alphaBlock.SeekTo(alphaOffset);
		alphaBlock.ReadRawBytes(maskBuffer, sizeof(maskBuffer));

		if (imageOffset & Is32bitTile) {
			std::memcpy(image, imageBuffer, BlockSize * BlockSize * 4);
		} else {
			for (std::int32_t j = 0; j < (BlockSize * BlockSize); j++) {
				std::uint8_t idx = imageBuffer[j];
				if (((maskBuffer[j / 8] >> (j % 8)) & 0x01) == 0x00) {
					// Empty mask
					idx = 0;
				}

				image[j] = idx;
			}
		}
	}
}
