#include "JJ2Tileset.h"
#include "JJ2Anims.h"
#include "JJ2Block.h"

#include <IO/DeflateStream.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

using namespace Death::IO;

namespace Jazz2::Compatibility
{
	bool JJ2Tileset::Open(const StringView path, bool strictParser)
	{
		auto s = fs::Open(path, FileAccess::Read);
		RETURNF_ASSERT_MSG(s->IsValid(), "Cannot open file for reading");

		// Skip copyright notice
		s->Seek(180, SeekOrigin::Current);

		JJ2Block headerBlock(s, 262 - 180);

		std::uint32_t magic = headerBlock.ReadUInt32();
		RETURNF_ASSERT_MSG(magic == 0x454C4954 /*TILE*/, "Invalid magic string");

		std::uint32_t signature = headerBlock.ReadUInt32();
		RETURNF_ASSERT_MSG(signature == 0xAFBEADDE, "Invalid signature");

		_name = headerBlock.ReadString(32, true);

		std::uint16_t versionNum = headerBlock.ReadUInt16();
		_version = (versionNum <= 512 ? JJ2Version::BaseGame : JJ2Version::TSF);

		std::int32_t recordedSize = headerBlock.ReadInt32();
		RETURNF_ASSERT_MSG(!strictParser || s->GetSize() == recordedSize, "Unexpected file size");

		// Get the CRC; would check here if it matches if we knew what variant it is AND what it applies to
		// Test file across all CRC32 variants + Adler had no matches to the value obtained from the file
		// so either the variant is something else or the CRC is not applied to the whole file but on a part
		/*int recordedCRC =*/ headerBlock.ReadInt32();

		// Read the lengths, uncompress the blocks and bail if any block could not be uncompressed
		// This could look better without all the copy-paste, but meh.
		int infoBlockPackedSize = headerBlock.ReadInt32();
		int infoBlockUnpackedSize = headerBlock.ReadInt32();
		int imageBlockPackedSize = headerBlock.ReadInt32();
		int imageBlockUnpackedSize = headerBlock.ReadInt32();
		int alphaBlockPackedSize = headerBlock.ReadInt32();
		int alphaBlockUnpackedSize = headerBlock.ReadInt32();
		int maskBlockPackedSize = headerBlock.ReadInt32();
		int maskBlockUnpackedSize = headerBlock.ReadInt32();

		JJ2Block infoBlock(s, infoBlockPackedSize, infoBlockUnpackedSize);
		JJ2Block imageBlock(s, imageBlockPackedSize, imageBlockUnpackedSize);
		JJ2Block alphaBlock(s, alphaBlockPackedSize, alphaBlockUnpackedSize);
		JJ2Block maskBlock(s, maskBlockPackedSize, maskBlockUnpackedSize);

		LoadMetadata(infoBlock);
		LoadImageData(imageBlock, alphaBlock);
		LoadMaskData(maskBlock);

		return true;
	}

	void JJ2Tileset::LoadMetadata(JJ2Block& block)
	{
		for (std::int32_t i = 0; i < 256; i++) {
			std::uint32_t color = block.ReadUInt32();
			color = (color & 0x00ffffff) | ((255 - ((color >> 24) & 0xff)) << 24);
			_palette[i] = color;
		}

		_tileCount = block.ReadInt32();

		// TODO: Use _tileCount instead of maxTiles ???
		std::int32_t maxTiles = GetMaxSupportedTiles();
		_tiles = std::make_unique<TilesetTileSection[]>(maxTiles);

		for (std::int32_t i = 0; i < maxTiles; ++i) {
			_tiles[i].Opaque = block.ReadBool();
		}

		// Block of unknown values, skip
		block.DiscardBytes(maxTiles);

		for (std::int32_t i = 0; i < maxTiles; ++i) {
			_tiles[i].ImageDataOffset = block.ReadUInt32();
		}

		// Block of unknown values, skip
		block.DiscardBytes(4 * maxTiles);

		for (std::int32_t i = 0; i < maxTiles; ++i) {
			_tiles[i].AlphaDataOffset = block.ReadUInt32();
		}

		// Block of unknown values, skip
		block.DiscardBytes(4 * maxTiles);

		for (std::int32_t i = 0; i < maxTiles; ++i) {
			_tiles[i].MaskDataOffset = block.ReadUInt32();
		}

		// We don't care about the flipped masks, those are generated on runtime
		block.DiscardBytes(4 * maxTiles);
	}

	void JJ2Tileset::LoadImageData(JJ2Block& imageBlock, JJ2Block& alphaBlock)
	{
		std::uint8_t imageBuffer[BlockSize * BlockSize];
		std::uint8_t maskBuffer[BlockSize * BlockSize / 8];

		std::int32_t maxTiles = GetMaxSupportedTiles();
		for (std::int32_t i = 0; i < maxTiles; i++) {
			auto& tile = _tiles[i];

			imageBlock.SeekTo(tile.ImageDataOffset);
			imageBlock.ReadRawBytes(imageBuffer, sizeof(imageBuffer));
			alphaBlock.SeekTo(tile.AlphaDataOffset);
			alphaBlock.ReadRawBytes(maskBuffer, sizeof(maskBuffer));

			for (std::int32_t j = 0; j < (BlockSize * BlockSize); j++) {
				std::uint8_t idx = imageBuffer[j];
				if (((maskBuffer[j / 8] >> (j % 8)) & 0x01) == 0x00) {
					// Empty mask
					idx = 0;
				}

				tile.Image[j] = idx;
			}
		}
	}

	void JJ2Tileset::LoadMaskData(JJ2Block& block)
	{
		//std::uint8_t maskBuffer[BlockSize * BlockSize / 8];

		std::int32_t maxTiles = GetMaxSupportedTiles();
		for (std::int32_t i = 0; i < maxTiles; i++) {
			auto& tile = _tiles[i];

			block.SeekTo(tile.MaskDataOffset);
			/*block.ReadRawBytes(maskBuffer, sizeof(maskBuffer));

			for (std::int32_t j = 0; j < 128; j++) {
				std::uint8_t idx = maskBuffer[j];
				for (std::int32_t k = 0; k < 8; k++) {
					std::int32_t pixelIdx = 8 * j + k;
					std::int32_t x = pixelIdx % BlockSize;
					std::int32_t y = pixelIdx / BlockSize;
					if (((idx >> k) & 0x01) == 0) {
						tile.Mask[y * BlockSize + x] = 0;
					} else {
						tile.Mask[y * BlockSize + x] = 1;
					}
				}
			}*/

			block.ReadRawBytes(tile.Mask, sizeof(tile.Mask));
		}
	}

	void JJ2Tileset::Convert(const StringView targetPath) const
	{
		// Rearrange tiles from '10 tiles per row' to '30 tiles per row'
		constexpr std::int32_t TilesPerRow = 30;

		std::int32_t width = TilesPerRow * BlockSize;
		std::int32_t height = ((_tileCount - 1) / TilesPerRow + 1) * BlockSize;

		auto so = fs::Open(targetPath, FileAccess::Write);
		ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

		constexpr std::uint8_t flags = 0x20 | 0x40; // Mask and palette included

		so->WriteValue<std::uint64_t>(0xB8EF8498E2BFBBEF);
		so->WriteValue<std::uint32_t>(0x0002208F | (flags << 24)); // Version 2 is reserved for sprites (or bigger images)

		// TODO: Use single channel instead
		so->WriteValue<std::uint8_t>(4);
		so->WriteValue<std::uint32_t>(width);
		so->WriteValue<std::uint32_t>(height);
		so->WriteValue<std::uint16_t>(_tileCount);

		MemoryStream ms(1024 * 1024);
		{
			DeflateWriter co(ms);

			// Palette
			std::uint32_t palette[PaletteSize];
			std::memcpy(palette, _palette, sizeof(_palette));

			bool hasAlphaChannel = false;
			for (std::int32_t i = 1; i < static_cast<std::int32_t>(arraySize(palette)); i++) {
				if ((palette[i] & 0xff000000) != 0) {
					hasAlphaChannel = true;
					break;
				}
			}
			if (!hasAlphaChannel) {
				for (std::int32_t i = 1; i < static_cast<std::int32_t>(arraySize(palette)); i++) {
					palette[i] |= 0xff000000;
				}
			}

			co.Write(palette, sizeof(palette));

			// Mask
			co.WriteValue<std::uint32_t>(_tileCount * sizeof(_tiles[0].Mask));
			for (std::int32_t i = 0; i < _tileCount; i++) {
				auto& tile = _tiles[i];
				co.Write(tile.Mask, sizeof(tile.Mask));
			}
		}

		so->WriteValue<std::int32_t>(ms.GetSize());
		so->Write(ms.GetBuffer(), ms.GetSize());

		// Image
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(width * height * 4);

		for (std::int32_t i = 0; i < _tileCount; i++) {
			auto& tile = _tiles[i];

			std::int32_t ox = (i % TilesPerRow) * BlockSize;
			std::int32_t oy = (i / TilesPerRow) * BlockSize;
			for (std::int32_t y = 0; y < BlockSize; y++) {
				for (std::int32_t x = 0; x < BlockSize; x++) {
					auto& src = tile.Image[y * BlockSize + x];

					pixels[(width * (oy + y) + ox + x) * 4] = src;
					pixels[(width * (oy + y) + ox + x) * 4 + 1] = src;
					pixels[(width * (oy + y) + ox + x) * 4 + 2] = src;
					pixels[(width * (oy + y) + ox + x) * 4 + 3] = (src == 0 ? 0 : 255);
				}
			}
		}

		// TODO: Use single channel instead
		JJ2Anims::WriteImageContent(*so, pixels.get(), width, height, 4);
	}
}