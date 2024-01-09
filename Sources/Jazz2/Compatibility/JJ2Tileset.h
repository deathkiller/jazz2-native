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
        static constexpr int BlockSize = 32;

        JJ2Tileset() : _version(JJ2Version::Unknown), _tileCount(0) { }

        bool Open(const StringView path, bool strictParser);

        void Convert(const StringView targetPath) const;

        int GetMaxSupportedTiles() const {
            return (_version == JJ2Version::BaseGame ? 1024 : 4096);
        }

    private:
        struct TilesetTileSection {
            bool Opaque;
            uint32_t ImageDataOffset;
            uint32_t AlphaDataOffset;
            uint32_t MaskDataOffset;

            uint8_t Image[BlockSize * BlockSize];
            //uint8_t Mask[BlockSize * BlockSize];
            uint8_t Mask[BlockSize * BlockSize / 8];
        };

        String _name;
        JJ2Version _version;
        uint32_t _palette[256];
        std::unique_ptr<TilesetTileSection[]> _tiles;
        int _tileCount;

        void LoadMetadata(JJ2Block& block);
        void LoadImageData(JJ2Block& imageBlock, JJ2Block& alphaBlock);
        void LoadMaskData(JJ2Block& block);
    };
}