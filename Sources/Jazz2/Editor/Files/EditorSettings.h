#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Settings of the level editor, kept in `Jazz2.Editor.json` in the configuration directory
	*/
	struct EditorSettings
	{
		/** @brief Most recently used files kept */
		static constexpr std::int32_t MaxRecentFiles = 10;

		/** @brief Whether a panel that can be docked floats, and where it floated the last time, in units */
		struct PanelPlacement
		{
			/** @brief Whether it floats over the level instead of being docked */
			bool Floating = false;
			/** @brief Left edge */
			float X = 0.0f;
			/** @brief Top edge */
			float Y = 0.0f;
			/** @brief Width, 0 if it has never floated */
			float Width = 0.0f;
			/** @brief Height */
			float Height = 0.0f;
		};

		/** @brief Scale of the interface, 0 to choose it by the size of the window */
		float UiScale = 0.0f;
		/** @brief Whether the grid is shown */
		bool ShowGrid = true;
		/** @brief Whether layers with an automatic speed stay still */
		bool PauseLayerMovement = false;
		/** @brief Whether a textured layer being edited is drawn as in the game instead of as plain tiles to be painted */
		bool PreviewTexturedLayers = false;
		/** @brief Tiles in a row of the tile picker */
		std::int32_t TilePickerColumns = 10;
		/** @brief Width of the panel on the right in units */
		float RightPanelWidth = 165.0f;
		/** @brief Height of the list of layers in the panel on the right in units */
		float LayerListHeight = 96.0f;
		/** @brief Width of the panel with the tiles and the events on the left in units */
		float PickerPanelWidth = 180.0f;
		/** @brief Layout of the interface, 0 by the size of the window, 1 for a desktop, 2 compact with panels in drawers */
		std::int32_t Layout = 0;
		/** @brief Whether all tools and commands are offered, or only the simple ones */
		bool Expert = true;
		/** @brief Where the panel with the tiles and the events is when it floats */
		PanelPlacement PickerPanelPlacement;
		/** @brief Where the panel on the right is when it floats */
		PanelPlacement RightPanelPlacement;
		/** @brief Character to play the level with, see @ref PlayerType */
		std::int32_t PlayCharacter = 1;
		/** @brief Difficulty to play the level at, see @ref GameDifficulty */
		std::int32_t PlayDifficulty = 2;
		/** @brief Game mode of multiplayer to play the level in local splitscreen, see @ref Multiplayer::MpGameMode, 0 to play alone */
		std::int32_t PlayGameMode = 0;
		/** @brief Players of multiplayer in local splitscreen */
		std::int32_t PlayPlayerCount = 2;
		/** @brief Files opened or saved recently, the most recent first */
		SmallVector<String, 0> RecentFiles;

		/** @brief Most recently used events and tiles kept, see @ref RecentEvents */
		static constexpr std::int32_t MaxRecentItems = 16;
		/** @brief Most favourite events and tiles kept */
		static constexpr std::int32_t MaxFavoriteItems = 64;

		/** @brief Recently used and favourite tiles of a tile set, by their index in it */
		struct TileSetFavorites
		{
			/** @brief Name of the tile set */
			String TileSet;
			/** @brief Tiles used recently, the most recent first */
			SmallVector<std::uint16_t, 0> Recent;
			/** @brief Favourite tiles, in the order they were added */
			SmallVector<std::uint16_t, 0> Favorites;
		};

		/** @brief Whether the frame of what the game shows is drawn around the pointer */
		bool ShowScreenFrame = false;
		/** @brief Frame drawn by @ref ShowScreenFrame, 0 the view of the game, 1 4:3, 2 16:9, 3 a half of a split screen */
		std::int32_t ScreenFrameKind = 0;
		/** @brief Problems shown by Check Level, 1 errors, 2 warnings, 4 information */
		std::int32_t CheckLevelFilter = 7;
		/** @brief Events chosen recently, by their type, the most recent first */
		SmallVector<std::uint16_t, 0> RecentEvents;
		/** @brief Favourite events, by their type */
		SmallVector<std::uint16_t, 0> FavoriteEvents;
		/** @brief Recently used and favourite tiles of each tile set */
		SmallVector<TileSetFavorites, 0> TileFavorites;
		/** @brief Theme of the interface, 0 for the usual one, 1 for high contrast, see @ref UiThemeKind */
		std::int32_t Theme = 0;
		/** @brief Whether movement of the interface and the view is kept to the least --- panels and lists don't glide, the view doesn't fly */
		bool ReducedMotion = false;
		/** @brief Whether text is in a plain readable font instead of the pixel font of the game, see @ref UiReadableFont */
		bool ReadableFont = false;
		/**
		 * @brief Program the script of a level is edited in, empty for the one the system opens such files with
		 *
		 * Set only in the file. The path of a program, or a name found in `PATH`, followed by arguments if needed, where
		 * `{file}` stands for the script --- the script is the last argument otherwise. A path with spaces followed by
		 * arguments has to be in quotes. For example `C:\Program Files\Notepad++\notepad++.exe` or
		 * `code --reuse-window {file}`, written in the file as `"C:\\Program Files\\Notepad++\\notepad++.exe"`.
		 */
		String ScriptEditor;

		/** @brief Options of the tools that are kept, see @ref ToolSettings */
		struct ToolOptions
		{
			/** @brief Whether the fill and the magic wand go on across the edges of a layer that repeats */
			bool AcrossSeams = true;
			/** @brief Whether the fill and the magic wand take a tile flipped as the same tile */
			bool IgnoreFlips = false;
			/** @brief Whether the fill, the rectangle and the shapes paint only empty tiles */
			bool OnlyEmpty = false;
			/** @brief Whether the magic wand selects all such tiles of the layer */
			bool WandGlobal = false;
			/** @brief Shape the shape tool draws, see @ref ToolShape */
			std::int32_t Shape = 0;
			/** @brief How the event tool places events, or that it only selects them, see @ref EventPlaceMode */
			std::int32_t EventMode = 0;
			/** @brief Jumps the measure tool shows, see @ref ToolSettings::MeasureArcs */
			std::int32_t MeasureArcs = 0x07;
			/** @brief Whether the jumps of the measure tool stop at solid tiles */
			bool MeasureCollisions = true;
			/** @brief Whether mirroring swaps the teams of events */
			bool MirrorSwapTeams = false;
		};
		/** @brief Options of the tools that are kept */
		ToolOptions Tools;

		/** @brief Options of play-tests and of what is shown of them that are kept, see @ref PlayTestController */
		struct PlayTestSettings
		{
			/** @brief How fast the time of the level runs, 1 for normal, see @ref PlayTestDebugOptions */
			float TimeScale = 1.0f;
			/** @brief Whether the hitboxes of the objects are drawn */
			bool ShowHitboxes = false;
			/** @brief Whether the tile events that make an area do something are drawn */
			bool ShowEventZones = false;
			/** @brief Whether the places where the player died in the last play-tests are shown over the level */
			bool ShowDeaths = false;
			/** @brief Character the reachability is found for, see @ref PlayerType, 0 for any of them, from the start of the one the level is played with */
			std::int32_t ReachCharacter = 0;
			/** @brief Whether the reachability is found from the starts of multiplayer too */
			bool ReachFromMpStarts = false;
		};
		/** @brief Options of play-tests that are kept */
		PlayTestSettings PlayTest;

		/** @brief Loads the settings, keeps the defaults if there are none */
		void Load();
		/** @brief Saves the settings */
		bool Save() const;
		/** @brief Moves a file to the top of the recent files */
		void AddRecentFile(StringView path);
		/** @brief Removes a file from the recent files, as when it was moved elsewhere */
		void RemoveRecentFile(StringView path);
		/** @brief Moves an event to the top of the recent events */
		void AddRecentEvent(std::uint16_t type);
		/** @brief Returns the recent and favourite tiles of a tile set, added if there are none yet */
		TileSetFavorites& GetTileFavorites(StringView tileSet);
		/** @brief Moves a tile of a tile set to the top of its recent tiles */
		void AddRecentTile(StringView tileSet, std::uint16_t index);

		/** @brief Returns the path of the settings file */
		static String GetPath();
	};
}

#endif
