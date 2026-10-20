#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PlayTestTypes.h"
#include "../../LevelHandler.h"
#include "../Document/LevelDocument.h"

#include <Containers/Function.h>

namespace Jazz2::Tiles
{
	class TileMap;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
#endif

	/**
		@brief A level being played from the editor, see @ref PlayTestLevelHandler
	*/
	class IPlayTest
	{
	public:
		IPlayTest() = default;
		virtual ~IPlayTest();

		IPlayTest(const IPlayTest&) = delete;
		IPlayTest& operator=(const IPlayTest&) = delete;

		/** @brief Returns the level handler that plays it */
		virtual LevelHandler& GetLevelHandler() = 0;
		/** @brief Returns the viewport of the first player, or `nullptr` */
		virtual Rendering::PlayerViewport* GetViewport() const = 0;
		/** @brief Returns the first player, or `nullptr` */
		virtual Actors::Player* GetPlayer() const = 0;
		/** @brief Returns the texture the last frame was drawn into before it was scaled to the screen */
		virtual Texture* GetFrameTexture() const = 0;
		/** @brief Stops the level for a while, it's kept as it is, its music and sound effects are paused */
		virtual void Suspend() = 0;
		/** @brief Continues the level after @ref Suspend(), with the sound effects it paused */
		virtual void Resume() = 0;

		/** @brief Returns the recorded path of the first player */
		PlayPath& GetPlayPath() {
			return _playPath;
		}
		/** @brief Returns `true` if the player didn't start where it was asked to, but at the start of the level */
		bool HasSpawnFallenBack() const {
			return _spawnFellBack;
		}

		/** @brief Called when the player chooses to leave the editor from the pause menu */
		Function<void()> OnExitEditor;

		/** @brief Writes the level to play into bytes as the game reads it */
		static bool SaveLevel(const LevelDocument& document, StringView levelName, SmallVectorImpl<std::uint8_t>& data);

	protected:
		PlayPath _playPath;
		bool _spawnFellBack = false;

		/**
		 * @brief Keeps everything loaded for as long as the play-test lives, called once its level was loaded
		 *
		 * The level shown by the editor is loaded again while a play-test is kept, and so is the level of the next
		 * play-test before the kept one is let go, neither of them may release what the play-test still uses, see
		 * @ref ContentResolver::RetainLoadedResources().
		 */
		void RetainResources();

	private:
		bool _resourcesRetained = false;
	};
}

#endif
