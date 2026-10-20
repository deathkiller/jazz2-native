#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"

#include <Containers/Function.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <cstdint>
#include <memory>

namespace Jazz2::Formats
{
	struct TileSetData;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	class IEditorContext;
	class UiStack;

	/**
		@brief Name of the extra tile set a level stores in itself, see @ref Formats::ExtraTileSetRecord::Embedded

		It can't be the name of a tile set file.
	*/
	inline constexpr char LevelTileSetName[] = ":level";

	/** @brief Returns the name of a tile set as it's shown */
	String GetTileSetLabel(StringView name);

	/** @brief Returns `true` if a tile set of the level (as @ref TileRef::Source) is stored in the level itself */
	bool IsLevelTileSet(const LevelDocument& document, std::uint8_t source);

	/** @brief Reads a whole tile set of the level (as @ref TileRef::Source), from its file or from the level, returns `false` if it can't */
	bool ReadTileSetOf(IEditorContext& session, std::uint8_t source, Formats::TileSetData& tileSet);

	/** @brief Writes a whole tile set into bytes as its file stores it */
	bool SerializeTileSet(const Formats::TileSetData& tileSet, SmallVectorImpl<std::uint8_t>& bytes);

	/**
	 * @brief Puts the colors an extra tile set is matched to into its 8-bit tiles, see @ref Formats::ExtraTileSetRecord::Remapped
	 *
	 * The tiles then have the indices of the palette of the level, as they were shown, and the tile set doesn't need
	 * to be matched anymore. Only a tile set the level stores in itself can be changed so, a file is shared by levels.
	 */
	void BakeRemapping(Formats::TileSetData& tileSet, const std::uint8_t* remapping);

	/**
	 * @brief Returns `true` if the level can refer to @p added more tiles of its tile sets
	 *
	 * Tile IDs are 16-bit and the animated tiles take the last ones, shows a message if it can't.
	 */
	bool CanAddTileIds(IEditorContext& session, std::int32_t added);

	/**
	 * @brief Returns a name for a new tile set made of @p base and @p suffix, as tile sets are named, that isn't taken yet
	 *
	 * Only the file name of @p base is taken, lowercase, without other characters than letters, digits, dashes and
	 * underscores, short enough for the number added to make it unique.
	 */
	String SuggestTileSetName(StringView base, StringView suffix);

	/** @brief Returns `true` if the tile set file used is the copy in the user content, which is written over then */
	bool IsOwnTileSetCopy(StringView name);

	/** @brief Fills the palette the level is shown in, its own one or the one of its default tile set, the first entry transparent */
	void GetLevelPalette(IEditorContext& session, std::uint32_t (&palette)[256]);

	/**
		@brief Where tiles are added to
	*/
	struct TileTarget
	{
		/** @brief Kind of the place */
		enum class Kind : std::uint8_t {
			/** A tile set the level uses already, @ref Source */
			TileSet,
			/** A new tile set file of @ref Name in the user content, the level then uses it as an extra one */
			NewTileSet,
			/** The tile set the level stores in itself, made if it has none yet */
			Level
		};

		/** @brief Kind of the place */
		Kind Type = Kind::TileSet;
		/** @brief The tile set as @ref TileRef::Source, of @ref Kind::TileSet */
		std::uint8_t Source = 0;
		/** @brief Name of the new tile set, of @ref Kind::NewTileSet */
		String Name;
	};

	/** @brief Adds a dropdown to choose where tiles are added to into a row, with the name of a new tile set next to it */
	void AddTileTargetRow(IEditorContext& session, UiStack* row, std::shared_ptr<TileTarget> target);
	/** @brief Adds a note about where the tiles go, for a tile set file shadowed by its copy in the user content, say */
	void AddTileTargetNote(IEditorContext& session, UiStack* parent, std::shared_ptr<TileTarget> target);

	/**
		@brief Shows the tile sets of the level --- the default one and the extra ones that follow it

		The tiles of the level follow every change of the tile sets, so a tile shows the same picture after an extra
		tile set is moved or another one before it is removed. Tiles of a removed tile set are cleared.
	*/
	void ShowTileSetsDialog(IEditorContext& session);

	/** @brief Makes a tile set the default one of the level as an undoable step, returns `false` if it can't be read */
	bool SetDefaultTileSet(IEditorContext& session, StringView name);
	/**
	 * @brief Adds an extra tile set with all of its tiles as an undoable step, returns `false` if it can't be
	 *
	 * With @p matchColors of -1, the colors of an 8-bit tile set of another palette than the level are matched to the
	 * palette of the level, 1 matches them and 0 doesn't, see @ref Formats::ExtraTileSetRecord::Remapped.
	 */
	bool AddExtraTileSet(IEditorContext& session, StringView name, std::int32_t matchColors = -1);

	/**
		@brief Asks for an image and adds its tiles to the end of a tile set of the level

		The tiles get the closest colors of the palette of the tile set, the tile set is saved into the user content,
		or into the level if it's the one the level stores in itself. Its tiles that were there keep their places, so
		does every tile of the level.
	*/
	void ShowAddTilesFromImageDialog(IEditorContext& session);

	/**
	 * @brief Adds the tiles of an image to the end of a tile set, as @ref ShowAddTilesFromImageDialog() does
	 *
	 * With @p skipExisting, a tile the tile set has already isn't added again. Returns `false` if nothing was added.
	 */
	bool AddTilesFromImage(IEditorContext& session, const TileTarget& target, StringView imagePath, bool skipExisting, bool dither);

	/** @brief How the tiles of an image are cut and matched to the colors, see @ref AddTilesFromImageAsync() */
	struct AddTilesOptions
	{
		/** @brief Whether a tile the tile set has already isn't added again */
		bool SkipExisting = true;
		/** @brief Whether colors between two of the palette are mixed by a regular pattern */
		bool Dither = false;
		/** @brief Whether colors between two of the palette are mixed by spreading the error (Floyd-Steinberg) */
		bool ErrorDiffusion = false;
		/** @brief Pixels left of the first column of tiles and above the first row */
		std::int32_t OffsetX = 0, OffsetY = 0;
		/** @brief Pixels between two tiles and between two rows of them */
		std::int32_t SpacingX = 0, SpacingY = 0;
		/** @brief Whether pixels of @ref ColorKey are transparent */
		bool UseColorKey = false;
		/** @brief Color of transparent pixels as `R | G << 8 | B << 16`, magenta */
		std::uint32_t ColorKey = 0x00FF00FFu;
		/** @brief Alpha from which a pixel is opaque and a pixel of the masks is solid */
		std::int32_t AlphaThreshold = 128;
	};

	/**
	 * @brief Adds the tiles of an image to the end of a tile set as @ref AddTilesFromImage() does, cut off the user interface
	 *
	 * How far it is is shown in a window that can stop it. @p onDone gets whether tiles were added, on the main thread.
	 */
	void AddTilesFromImageAsync(IEditorContext& session, const TileTarget& target, StringView imagePath, const AddTilesOptions& options, Function<void(bool)>&& onDone);

	/**
	 * @brief Adds all tiles of @p tiles to the end of a tile set, as one undoable step
	 *
	 * The tiles are taken as they are, true color ones stay true color. A tile set file is saved into the user
	 * content. @p first is then the first added tile as the level refers to it. Returns `false` if they can't be added.
	 */
	bool AppendTiles(IEditorContext& session, const TileTarget& target, const Formats::TileSetData& tiles, TileRef& first);

	/**
	 * @brief Copies a tile into the tiles of this level, and the level shows the copy everywhere it showed the tile
	 *
	 * The copy is as the level showed the tile, changed in the level or not, so it can be changed further in true
	 * color, only in this level. All of it is one undoable step. Returns `false` if it can't be copied.
	 */
	bool CopyTileIntoLevel(IEditorContext& session, const TileRef& tile, TileRef& copy);
}

#endif
