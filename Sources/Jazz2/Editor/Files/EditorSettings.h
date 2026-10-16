#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::Editor
{
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;

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

		/** @brief Loads the settings, keeps the defaults if there are none */
		void Load();
		/** @brief Saves the settings */
		bool Save() const;
		/** @brief Moves a file to the top of the recent files */
		void AddRecentFile(StringView path);

		/** @brief Returns the path of the settings file */
		static String GetPath();
	};
}

#endif
