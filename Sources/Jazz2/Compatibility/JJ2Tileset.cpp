#include "JJ2Tileset.h"
#include "JJ2Anims.h"
#include "JJ2Block.h"

#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Compatibility
{
	bool JJ2Tileset::Open(StringView path, bool strictParser)
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
		_version = (versionNum == 0x300 ? JJ2Version::PlusExtension : (versionNum <= 0x200 ? JJ2Version::BaseGame : JJ2Version::TSF));

		std::int32_t recordedSize = headerBlock.ReadInt32();
		RETURNF_ASSERT_MSG(!strictParser || s->GetSize() == recordedSize, "Unexpected file size");

		// Get the CRC; would check here if it matches if we knew what variant it is AND what it applies to
		// Test file across all CRC32 variants + Adler had no matches to the value obtained from the file
		// so either the variant is something else or the CRC is not applied to the whole file but on a part
		/*int recordedCRC =*/ headerBlock.ReadInt32();

		// Read the lengths, uncompress the blocks and bail if any block could not be uncompressed
		// This could look better without all the copy-paste, but meh.
		std::int32_t infoBlockPackedSize = headerBlock.ReadInt32();
		std::int32_t infoBlockUnpackedSize = headerBlock.ReadInt32();
		std::int32_t imageBlockPackedSize = headerBlock.ReadInt32();
		std::int32_t imageBlockUnpackedSize = headerBlock.ReadInt32();
		std::int32_t alphaBlockPackedSize = headerBlock.ReadInt32();
		std::int32_t alphaBlockUnpackedSize = headerBlock.ReadInt32();
		std::int32_t maskBlockPackedSize = headerBlock.ReadInt32();
		std::int32_t maskBlockUnpackedSize = headerBlock.ReadInt32();

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
		std::uint8_t imageBuffer[BlockSize * BlockSize * 4];
		std::uint8_t maskBuffer[BlockSize * BlockSize / 8];

		std::int32_t maxTiles = GetMaxSupportedTiles();
		for (std::int32_t i = 0; i < maxTiles; i++) {
			auto& tile = _tiles[i];

			imageBlock.SeekTo(tile.ImageDataOffset & ~Is32bitTile);

			if (tile.ImageDataOffset & Is32bitTile) {
				// 32-bit image
				imageBlock.ReadRawBytes(imageBuffer, BlockSize * BlockSize * 4);
			} else {
				// 8-bit palette image
				imageBlock.ReadRawBytes(imageBuffer, BlockSize * BlockSize);
			}

			alphaBlock.SeekTo(tile.AlphaDataOffset);
			alphaBlock.ReadRawBytes(maskBuffer, sizeof(maskBuffer));

			if (tile.ImageDataOffset & Is32bitTile) {
				std::memcpy(tile.Image, imageBuffer, BlockSize * BlockSize * 4);
			} else {
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

		// Try to fix some known bugs in tilesets
		if (_name == "Castle 1"_s || _name == "Castle 1 Night"_s) {
			LOGI("Applying \"{}\" tileset mask fix", _name);
			// Spikes with empty mask
			auto& spikesWithEmptyMask = _tiles[189];
			auto& spikesWithCorrectMask = _tiles[184];
			std::memcpy(spikesWithEmptyMask.Mask, spikesWithCorrectMask.Mask, sizeof(spikesWithEmptyMask.Mask));
		} else if (_name == "Inferno Night"_s) {
			LOGI("Applying \"{}\" tileset mask fix", _name);
			// Solid tiles with empty mask
			static const std::int32_t SolidTilesWithEmptyMask[] = { 142, 143, 146, 152, 153, 156 };
			for (std::int32_t tileIdx : SolidTilesWithEmptyMask) {
				std::memset(_tiles[tileIdx].Mask, 0xFF, sizeof(_tiles[tileIdx].Mask));
			}
		} else if (_name == "Town House 1"_s || _name == "Town House 2"_s) {
			LOGI("Applying \"{}\" tileset mask fix", _name);
			// Vine/chain with empty mask
			for (std::int32_t tileIdx = 826; tileIdx <= 829; tileIdx++) {
				auto& mask = _tiles[tileIdx].Mask;
				if (std::all_of(mask, mask + sizeof(mask), [](std::uint8_t p) { return p == 0; })) {
					for (std::int32_t y = 6; y < 6 + 3; y++) {
						for (std::int32_t x = 0; x < BlockSize / 8; x++) {
							mask[y * (BlockSize / 8) + x] = 0xFF;
						}
					}
				}
			}
		}
	}

	void JJ2Tileset::Convert(StringView targetPath) const
	{
		// Rearrange tiles from '10 tiles per row' to '30 tiles per row'
		constexpr std::int32_t TilesPerRow = 30;

		std::int32_t tileCount = _tileCount;
		std::int32_t width = TilesPerRow * BlockSize;
		std::int32_t height = ((tileCount - 1) / TilesPerRow + 1) * BlockSize;

		auto so = fs::Open(targetPath, FileAccess::Write);
		ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

		constexpr std::uint8_t flags = 0x20 | 0x40; // Mask and palette included

		so->WriteValue<std::uint64_t>(0xB8EF8498E2BFBBEF);
		so->WriteValue<std::uint32_t>(0x0002208F | (flags << 24)); // Version 2 is reserved for sprites (or bigger images)

		// TODO: Use single channel instead
		so->WriteValue<std::uint8_t>(4);
		so->WriteValue<std::uint32_t>(width);
		so->WriteValue<std::uint32_t>(height);
		so->WriteValue<std::uint16_t>(tileCount);

		MemoryStream ms(1024 * 1024);
		{
			DeflateWriter co(ms);

			// Palette
			std::uint32_t palette[PaletteSize];
			std::memcpy(palette, _palette, sizeof(_palette));

			bool hasAlphaChannel = false;
			for (std::int32_t i = 1; i < std::int32_t(arraySize(palette)); i++) {
				if ((palette[i] & 0xff000000) != 0) {
					hasAlphaChannel = true;
					break;
				}
			}
			if (!hasAlphaChannel) {
				for (std::int32_t i = 1; i < std::int32_t(arraySize(palette)); i++) {
					palette[i] |= 0xff000000;
				}
			}

			// The first palette entry is fixed to transparent black
			palette[0] = 0x00000000;

			co.Write(palette, sizeof(palette));

			// Mark individual tiles as 32-bit or 8-bit
			for (std::int32_t i = 0; i < (tileCount + 7) / 8; i++) {
				std::uint32_t is32bitAggregated = 0;
				std::int32_t tilesPerByte = std::min(8, tileCount - i * 8);
				for (std::int32_t j = 0; j < tilesPerByte; j++) {
					const auto& tile = _tiles[i * 8 + j];
					if (tile.ImageDataOffset & Is32bitTile) {
						is32bitAggregated |= (1 << j);
					}
				}
				co.WriteValue<std::uint8_t>(is32bitAggregated);
			}

			// Mask
			co.WriteValue<std::uint32_t>(tileCount * sizeof(_tiles[0].Mask));
			for (std::int32_t i = 0; i < tileCount; i++) {
				const auto& tile = _tiles[i];
				co.Write(tile.Mask, sizeof(tile.Mask));
			}
		}

		so->WriteValue<std::int32_t>(ms.GetSize());
		so->Write(ms.GetBuffer(), ms.GetSize());

		// Diffuse
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(width * height * 4);

		for (std::int32_t i = 0; i < tileCount; i++) {
			const auto& tile = _tiles[i];

			std::int32_t ox = (i % TilesPerRow) * BlockSize;
			std::int32_t oy = (i / TilesPerRow) * BlockSize;
			if (tile.ImageDataOffset & Is32bitTile) {
				for (std::int32_t y = 0; y < BlockSize; y++) {
					for (std::int32_t x = 0; x < BlockSize; x++) {
						const auto* src = &tile.Image[(y * BlockSize + x) * 4];

						std::int32_t pixelIdx = (width * (oy + y) + ox + x) * 4;
						pixels[pixelIdx] = src[0];
						pixels[pixelIdx + 1] = src[1];
						pixels[pixelIdx + 2] = src[2];
						pixels[pixelIdx + 3] = src[3];
					}
				}
			} else {
				for (std::int32_t y = 0; y < BlockSize; y++) {
					for (std::int32_t x = 0; x < BlockSize; x++) {
						const auto& src = tile.Image[y * BlockSize + x];

						std::int32_t pixelIdx = (width * (oy + y) + ox + x) * 4;
						pixels[pixelIdx] = src;
						pixels[pixelIdx + 1] = src;
						pixels[pixelIdx + 2] = src;
						pixels[pixelIdx + 3] = (src == 0 ? 0 : 255);
					}
				}
			}
		}

		// TODO: Use single channel for 8-bit palette tiles instead
		JJ2Anims::WriteImageContent(*so, pixels.get(), width, height, 4);
	}
}