#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PngCodec.h"
#include "../../Formats/TileSetFile.h"

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
		@brief Reads 32 × 32 pixels of an image from a position as palette indices

		Pixels that are more transparent than opaque are index 0. An indexed image that has the same colors as
		@p palette keeps its indices, other colors are the closest entry of @p palette, as they look. Pixels outside of
		the image are transparent. Returns `true` if any color was changed to a close one.
	*/
	bool ReadTileFromImage(const PngImage& image, std::int32_t x, std::int32_t y, const std::uint32_t* palette, std::uint8_t* indices);

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
		/** @brief Whether a tile that is the same as one before it is left out */
		bool SkipDuplicates = false;
		/** @brief Image of the masks in the same layout, solid where it isn't transparent, or `nullptr` for the transparency of the tiles */
		const PngImage* Masks = nullptr;
	};

	/** @brief What @ref SliceTileSet() did */
	struct TileImportResult
	{
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
		Empty tiles at the end of the image are left out. Returns `false` if the image has no tile in it.
	*/
	bool SliceTileSet(const PngImage& image, const TileImportOptions& options, Formats::TileSetData& tileSet, TileImportResult& result);
}

#endif
