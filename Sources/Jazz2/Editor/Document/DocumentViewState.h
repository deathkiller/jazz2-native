#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Formats/LevelFile.h"
#include "../../../nCine/Primitives/Vector2.h"

using namespace nCine;

namespace Jazz2::Editor
{
	class LevelDocument;

	/**
		@brief How a level was seen and played in the editor the last time, for the editor only

		Kept in the editor-only chunk `edoc` (see @ref Formats::LevelFile::IsEditorOnlyChunk()), so the exported level
		has none of it. The level opens where it was left --- the same place, zoom, layer and view toggles --- and is
		played the same way, with the same character, difficulty and players. The play-start marker is where
		@ref PlayStartMode::Marker starts the play-test.

		It's written whenever the level is saved, a change of it alone doesn't make the level unsaved, except for
		the marker, which is a part of the level a user places on purpose.
	*/
	struct DocumentViewState
	{
		/** @brief Tag of the chunk it's kept in */
		static constexpr std::uint32_t Chunk = Formats::LevelFile::MakeChunkTag('e', 'd', 'o', 'c');

		/** @brief Whether the camera was saved */
		bool HasCamera = false;
		/** @brief Center of the view in the level in pixels */
		Vector2f CameraCenter;
		/** @brief Zoom of the view relative to how large the game shows the level, so it fits any window */
		float CameraZoom = 1.0f;
		/** @brief Layer being edited, -1 if not saved */
		std::int32_t CurrentLayer = -1;
		/** @brief Whether the grid is shown */
		bool ShowGrid = true;
		/** @brief Whether events are shown */
		bool ShowEvents = true;
		/** @brief Whether the path of the last play-test is shown */
		bool ShowPlayPath = true;
		/** @brief Whether all layers are drawn aligned to the level */
		bool FlatLayers = false;
		/** @brief How collision masks are shown, 0 not at all, 1 over the tiles, 2 instead of them */
		std::uint8_t MaskMode = 0;
		/** @brief Panel of the picker, 0 tiles, 1 events */
		std::uint8_t PickerPanel = 0;

		/** @brief Whether the way the level is played was saved */
		bool HasPlaySettings = false;
		/** @brief Character to play with, see @ref PlayerType */
		std::uint8_t PlayCharacter = 1;
		/** @brief Difficulty, see @ref GameDifficulty */
		std::uint8_t PlayDifficulty = 2;
		/** @brief Game mode of multiplayer in local splitscreen, 0 alone */
		std::uint8_t PlayGameMode = 0;
		/** @brief Players of multiplayer in local splitscreen */
		std::uint8_t PlayPlayerCount = 2;

		/** @brief Whether the play-start marker is placed */
		bool HasPlayStart = false;
		/** @brief Position of the play-start marker in the level in pixels */
		Vector2i PlayStart;

		/** @brief Reads it from a level, returns `false` if the level has none */
		static bool Read(const LevelDocument& document, DocumentViewState& state);
		/** @brief Writes it into a level */
		void Write(LevelDocument& document) const;
	};
}

#endif
