#pragma once

#if (defined(WITH_EDITOR) && defined(WITH_MULTIPLAYER)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PlayTest.h"
#include "../../Multiplayer/INetworkHandler.h"
#include "../../Multiplayer/MpLevelHandler.h"
#include "../../Multiplayer/NetworkManager.h"

#include <memory>

namespace Jazz2::Editor
{
	namespace Implementation
	{
		// The local session of a play-test in splitscreen, a base class so it's made before the level handler and released after it
		class PlayTestLocalSession : public Multiplayer::INetworkHandler
		{
		public:
			explicit PlayTestLocalSession(const PlayTestOptions& options);
			virtual ~PlayTestLocalSession();

			Multiplayer::ConnectionResult OnPeerConnected(const Multiplayer::Peer& peer, std::uint32_t clientData) override;
			void OnPeerDisconnected(const Multiplayer::Peer& peer, Multiplayer::Reason reason) override;
			void OnPacketReceived(const Multiplayer::Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data) override;

		protected:
			std::unique_ptr<Multiplayer::NetworkManager> _playTestNetwork;
		};
	}

	/**
		@brief Plays a level being edited as multiplayer in local splitscreen

		As @ref PlayTestLevelHandler, but the level runs a game mode of multiplayer for up to 4 players on one screen,
		as the game does for local splitscreen, so multiplayer starts, laps, bases and the rules of the game mode can be
		tried. The path of the first player is recorded.
	*/
	class PlayTestMpLevelHandler : private Implementation::PlayTestLocalSession, public Multiplayer::MpLevelHandler, public IPlayTest
	{
		DEATH_RUNTIME_OBJECT(Multiplayer::MpLevelHandler);

	public:
		/** @brief Creates a new instance with its own local session, the root controller is the editor session */
		PlayTestMpLevelHandler(IRootController* root, const PlayTestOptions& options);
		~PlayTestMpLevelHandler() override;

		/** @brief Loads the level from a document and spawns the players */
		bool Initialize(const LevelDocument& document, StringView levelName, StringView fullPath);

		LevelHandler& GetLevelHandler() override;
		Rendering::PlayerViewport* GetViewport() const override;
		Actors::Player* GetPlayer() const override;
		Texture* GetFrameTexture() const override;
		void Suspend() override;
		void Resume() override;

		bool IsEditorPlayTest() const override;
		void ExitEditor() override;
		bool SerializeResumableToStream(Stream& dest) override;
		void OnEndFrame() override;
		void HandleLevelChange(LevelInitialization&& levelInit) override;
		void HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags) override;
		bool HandlePlayerDied(Actors::Player* player) override;

	protected:
		bool LoadLevel(LevelDescriptor& descriptor) override;

	private:
		SmallVector<std::uint8_t, 0> _levelData;
		String _fullPath;
		PlayTestOptions _options;
	};
}

#endif
