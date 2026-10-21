#pragma once

#include "../../Main.h"
#include "JJ2Block.h"
#include "JJ2Version.h"
#include "../Formats/TileSetFile.h"

#include <memory>

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	/**
		@brief Parses original `.j2t` tile set files
		
		Reads an original tile set, decoding its palette and per-tile image, alpha and collision mask
		data, and writes the converted tile set to the target path for use by the engine's tile maps.
	*/
	class JJ2Tileset
	{
	public:
		/** @{ @name Constants */

		/** @brief Size of a tile */
		static constexpr std::int32_t BlockSize = 32;

		/** @} */

		/** @brief Creates a new instance */
		JJ2Tileset() : _version(JJ2Version::Unknown) { }

		/** @brief Opens and parses the specified tile set file */
		bool Open(StringView path, bool strictParser);

		/** @brief Converts the tile set and writes the result to the specified target path */
		void Convert(StringView targetPath) const;
		/** @brief Copies the converted tile set, the same data the overload above writes */
		void Convert(Formats::TileSetData& data) const;

		/**
		 * @brief Converts only some tiles of a tile set file into memory
		 *
		 * The tiles of @p tiles, indices into the tile set, go into @p data in the same order, as @ref Convert() converts
		 * them but without their collision masks; one past the end of the tile set stays empty. @p tileCount is set to the
		 * number of tiles of the whole tile set. Unlike @ref Open(), which keeps the whole tile set, it takes little more
		 * memory than the tiles it returns, which is what the previews of the levels need where memory is scarce.
		 */
		static bool ConvertTiles(StringView path, ArrayView<const std::int32_t> tiles, Formats::TileSetData& data, std::int32_t& tileCount);

		/** @brief Returns maximum number of supported tiles */
		std::int32_t GetMaxSupportedTiles() const {
			return (_version == JJ2Version::BaseGame ? 1024 : 4096);
		}

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct TileOffsets {
			std::uint32_t Image;
			std::uint32_t Alpha;
			std::uint32_t Mask;
		};

		// Sizes of the blocks of the file, packed and unpacked
		struct BlockSizes {
			std::int32_t InfoPacked, InfoUnpacked;
			std::int32_t ImagePacked, ImageUnpacked;
			std::int32_t AlphaPacked, AlphaUnpacked;
			std::int32_t MaskPacked, MaskUnpacked;
		};
#endif

		static constexpr std::int32_t PaletteSize = 256;
		static constexpr std::uint32_t Is32bitTile = 0x80000000u;

		String _name;
		JJ2Version _version;
		// The tiles are converted as they're read, so a tile set is in memory only once, also while it's written
		Formats::TileSetData _data;

		bool ReadHeader(std::unique_ptr<Stream>& s, bool strictParser, BlockSizes& sizes);
		std::int32_t ReadMetadata(JJ2Block& block, std::uint32_t* palette, SmallVectorImpl<TileOffsets>& offsets) const;
		void LoadMaskData(JJ2Block& block, ArrayView<const TileOffsets> offsets);

		static void ReadPalette(JJ2Block& block, std::uint32_t* palette);
		static void ConvertPalette(const std::uint32_t* palette, std::uint32_t* target);
		static void DecodeTileImage(JJ2Block& imageBlock, JJ2Block& alphaBlock, std::uint32_t imageOffset, std::uint32_t alphaOffset, std::uint8_t* image);
	};
}