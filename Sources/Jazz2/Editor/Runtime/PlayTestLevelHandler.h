#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PlayTest.h"

namespace Jazz2::Editor
{
	/**
		@brief Plays a level being edited

		An ordinary level handler, so the level plays exactly as in the game, but loaded from the level being edited
		and with the editor session as its root controller --- leaving the level returns to the editor. The player
		can start somewhere else than at the start of the level, and its path is recorded.
	*/
	class PlayTestLevelHandler : public LevelHandler, public IPlayTest
	{
		DEATH_RUNTIME_OBJECT(LevelHandler);

	public:
		/** @brief Creates a new instance, the root controller is the editor session */
		explicit PlayTestLevelHandler(IRootController* root);
		~PlayTestLevelHandler() override;

		/** @brief Loads the level from a document and spawns the player */
		bool Initialize(const LevelDocument& document, StringView levelName, StringView fullPath, const PlayTestOptions& options);

		LevelHandler& GetLevelHandler() override;
		Rendering::PlayerViewport* GetViewport() const override;
		Actors::Player* GetPlayer() const override;
		Texture* GetFrameTexture() const override;
		void Suspend() override;
		void Resume() override;

		bool CanOpenInEditor() const override;
		bool IsEditorPlayTest() const override;
		void ExitEditor() override;
		bool SerializeResumableToStream(Stream& dest) override;
		void OnEndFrame() override;
		void HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags) override;
		bool HandlePlayerDied(Actors::Player* player) override;

	protected:
		bool LoadLevel(LevelDescriptor& descriptor) override;
		LevelLoadOptions GetLevelLoadOptions() const override;
		void SpawnPlayers(const LevelInitialization& levelInit) override;

	private:
		SmallVector<std::uint8_t, 0> _levelData;
		String _fullPath;
		PlayTestOptions _options;
	};
}

#endif
