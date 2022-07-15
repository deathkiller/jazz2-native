#pragma once

#include "../../Common.h"
#include "JJ2Block.h"
#include "JJ2Version.h"

#include <memory>
#include <string>

namespace Jazz2::Compatibility
{
    class JJ2Tileset // .j2t
    {
    public:
        static JJ2Tileset Open(const std::string& path, bool strictParser);

        void Convert(const std::string& targetPath);

        int MaxSupportedTiles() {
            return (_version == JJ2Version::BaseGame ? 1024 : 4096);
        }
    private:
        struct TilesetTileSection {
            bool Opaque;
            uint32_t ImageDataOffset;
            uint32_t AlphaDataOffset;
            uint32_t MaskDataOffset;

            std::unique_ptr<uint8_t[]> Image;
            std::unique_ptr<uint8_t[]> Mask;
        };

        std::string _name;
        JJ2Version _version;
        std::unique_ptr<uint8_t[]> _palette;
        std::unique_ptr<TilesetTileSection[]> _tiles;
        int _tileCount;

        JJ2Tileset();

        void LoadMetadata(JJ2Block& block);
        void LoadImageData(JJ2Block& imageBlock, JJ2Block& alphaBlock);
        void LoadMaskData(JJ2Block& block);
    };
}