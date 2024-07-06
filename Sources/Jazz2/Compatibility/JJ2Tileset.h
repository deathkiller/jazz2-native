#pragma once

#include "../../Common.h"
#include "JJ2Block.h"
#include "JJ2Version.h"

#include <memory>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	class JJ2Tileset // .j2t
	{
	public:
		static constexpr std::int32_t BlockSize = 32;

		JJ2Tileset() : _version(JJ2Version::Unknown), _tileCount(0) { }

		bool Open(const StringView path, bool strictParser);

		void Convert(const StringView targetPath) const;

		std::int32_t GetMaxSupportedTiles() const {
			return (_version == JJ2Version::BaseGame ? 1024 : 4096);
		}

	private:
		struct TilesetTileSection {
			bool Opaque;
			std::uint32_t ImageDataOffset;
			std::uint32_t AlphaDataOffset;
			std::uint32_t MaskDataOffset;

			std::uint8_t Image[BlockSize * BlockSize];
			//std::uint8_t Mask[BlockSize * BlockSize];
			std::uint8_t Mask[BlockSize * BlockSize / 8];
		};

		static constexpr std::int32_t PaletteSize = 256;

		String _name;
		JJ2Version _version;
		std::uint32_t _palette[PaletteSize];
		std::unique_ptr<TilesetTileSection[]> _tiles;
		std::int32_t _tileCount;

		void LoadMetadata(JJ2Block& block);
		void LoadImageData(JJ2Block& imageBlock, JJ2Block& alphaBlock);
		void LoadMaskData(JJ2Block& block);
	};
}