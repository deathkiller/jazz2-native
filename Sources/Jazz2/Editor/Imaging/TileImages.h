#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PngCodec.h"
#include "../../Formats/TileSetFile.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/Function.h>

namespace Jazz2::Editor
{
	/**
		@brief Puts the tiles of a tile set side by side into an image, rows of @p columns of them

		The image is indexed with the palette of the tile set if all of its tiles are 8-bit ones, entry 0 transparent,
		so it can be edited and read back without losing anything. It's RGBA otherwise, the 8-bit tiles in the colors of
		@p palette.
	*/
	PngImage MakeTileSetSheet(const Formats::TileSetData& tileSet, const std::uint32_t* palette, std::int32_t columns);

	/** @brief Puts the collision masks of the tiles of a tile set side by side, solid pixels black, the others transparent */
	PngImage MakeTileSetMaskSheet(const Formats::TileSetData& tileSet, std::int32_t columns);

	/** @brief Makes an image of a tile, 32 × 32 palette indices with the colors of @p palette, entry 0 transparent */
	PngImage MakeTileImage(const std::uint8_t* indices, const std::uint32_t* palette);

	/**
		@brief Returns a hash of a tile of a tile set, by which the same tiles are found

		Of the pixels (a byte each of an 8-bit tile, 4 of a true color one), the mask and whether it's a true color tile.
		Equal tiles have equal hashes, the tiles of equal hashes still have to be compared.
	*/
	std::uint64_t HashTileOf(const Formats::TileSetData& data, std::int32_t tile);

	/**
		@brief Reads 32 × 32 pixels of an image from a position as palette indices

		Pixels that are more transparent than opaque are index 0. An indexed image that has the same colors as
		@p palette keeps its indices, other colors are the closest entry of @p palette, as they look. Pixels outside of
		the image are transparent. Returns `true` if any color was changed to a close one.
	*/
	bool ReadTileFromImage(const PngImage& image, std::int32_t x, std::int32_t y, const std::uint32_t* palette, std::uint8_t* indices);

	/**
		@brief Makes an indexed image of @p size × @p size palette indices with the colors of @p palette, entry 0 transparent

		As a texture of its own of a layer is stored (see @ref Formats::LayerPropertiesChunk), so it can be edited and read
		back by @ref ReadTextureFromImage() without losing anything.
	*/
	PngImage MakeTextureImage(const std::uint8_t* indices, std::int32_t size, const std::uint32_t* palette);

	/**
		@brief Reads an image of @p size × @p size pixels as palette indices, as @ref ReadTileFromImage() reads a tile

		Returns `true` if any color was changed to a close one.
	*/
	bool ReadTextureFromImage(const PngImage& image, std::int32_t size, const std::uint32_t* palette, std::uint8_t* indices);

	/** @brief What the colors of the tiles of an image become, see @ref SliceTileSet() */
	enum class TileImportColors : std::uint8_t
	{
		OwnPalette,			/**< The indices of an indexed image, with its palette as the palette of the tile set */
		TrueColor,			/**< The colors as they are, true color tiles */
		MatchPalette,		/**< The closest colors of a palette, the palette of the tile set */
		Reduce				/**< The colors reduced to a palette made for them, with the locked entries of a palette kept, see @ref BuildPalette() */
	};

	/** @brief How an image is made into a tile set, see @ref SliceTileSet() */
	struct TileImportOptions
	{
		/** @brief What the colors become */
		TileImportColors Colors = TileImportColors::OwnPalette;
		/** @brief Palette for @ref TileImportColors::MatchPalette, or the one @ref TileImportColors::Reduce starts from */
		const std::uint32_t* Palette = nullptr;
		/** @brief Entries of @ref Palette that @ref TileImportColors::Reduce keeps, 256 of them, or `nullptr` for none */
		const bool* Locked = nullptr;
		/** @brief Whether colors between two entries of the palette are mixed by an ordered pattern */
		bool Dither = false;
		/**
		 * @brief Whether colors between two entries of the palette are mixed by spreading the error of each pixel to the
		 *		pixels right of it and below it (Floyd-Steinberg) instead of the ordered pattern of @ref Dither
		 *
		 * The error goes over the whole image row by row, so tiles next to each other continue it.
		 */
		bool ErrorDiffusion = false;
		/** @brief Whether a tile that is the same as one before it is left out */
		bool SkipDuplicates = false;
		/** @brief Image of the masks in the same layout, solid where it isn't transparent, or `nullptr` for the transparency of the tiles */
		const PngImage* Masks = nullptr;
		/** @brief Pixels of the image left of the first column of tiles, as a sprite sheet with a margin has them */
		std::int32_t OffsetX = 0;
		/** @brief Pixels of the image above the first row of tiles */
		std::int32_t OffsetY = 0;
		/** @brief Pixels between two tiles next to each other, as a sprite sheet with gutters has them */
		std::int32_t SpacingX = 0;
		/** @brief Pixels between two rows of tiles */
		std::int32_t SpacingY = 0;
		/** @brief Whether pixels of @ref ColorKey are transparent, as images without alpha mark it */
		bool UseColorKey = false;
		/** @brief Color of transparent pixels as `R | G << 8 | B << 16`, magenta by default */
		std::uint32_t ColorKey = 0x00FF00FFu;
		/** @brief Alpha from which a pixel is opaque, and a pixel of @ref Masks is solid, 1 to 255 */
		std::uint8_t AlphaThreshold = 128;
		/** @brief Gets how far it is from 0 to 1 now and then, it stops when it returns `false`, see @ref TileImportError::Cancelled */
		mutable Death::Containers::Function<bool(float)> Progress;
	};

	/** @brief Why @ref SliceTileSet() couldn't make a tile set */
	enum class TileImportError : std::uint8_t
	{
		None,				/**< It could */
		TooSmall,			/**< The image is smaller than a tile */
		TooManyTiles,		/**< The tile set would have more tiles than @ref MaxImportedTiles */
		Cancelled			/**< @ref TileImportOptions::Progress stopped it */
	};

	/** @brief Number of columns and rows of tiles in an image with the layout of @p options, see @ref SliceTileSet() */
	nCine::Vector2i GetTileGrid(const PngImage& image, const TileImportOptions& options);

	/** @brief Most tiles a tile set can have, as its file stores the number of them in 16 bits */
	constexpr std::int32_t MaxImportedTiles = 65535;

	/** @brief What @ref SliceTileSet() did */
	struct TileImportResult
	{
		/** @brief Why it failed, if it did */
		TileImportError Error = TileImportError::None;
		/** @brief Tiles of the tile set */
		std::int32_t TileCount = 0;
		/** @brief The first tile of the image wasn't empty, so an empty one was put before it, the tiles are one further */
		bool AddedEmptyTile = false;
		/** @brief Empty tiles at the end of the image that were left out */
		std::int32_t TrimmedTiles = 0;
		/** @brief Some colors weren't in the palette, the closest ones were taken */
		bool ColorsChanged = false;
		/** @brief Tiles left out because they're the same as one before them */
		std::int32_t DuplicateTiles = 0;
		/** @brief Entries of the palette given a color by @ref TileImportColors::Reduce */
		std::int32_t ColorsAdded = 0;
	};

	/**
		@brief Makes a tile set of an image, tiles of 32 × 32 pixels row by row

		The first tile of a tile set is the empty one, an empty one is put first if the image doesn't start with one.
		Empty tiles at the end of the image are left out. A tile is empty if none of its pixels can be seen and its mask
		is empty. Returns `false` and why in @ref TileImportResult::Error if the image has no tile in it, or more than
		@ref MaxImportedTiles of them would be left --- that's found as soon as there are, before the rest is sliced.
	*/
	bool SliceTileSet(const PngImage& image, const TileImportOptions& options, Formats::TileSetData& tileSet, TileImportResult& result);
}

#endif
