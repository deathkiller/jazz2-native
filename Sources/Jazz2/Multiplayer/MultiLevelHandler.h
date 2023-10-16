#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../LevelHandler.h"
#include "NetworkManager.h"

namespace Jazz2::Actors
{
	class RemoteActor;
}

namespace Jazz2::Multiplayer
{
	enum class MultiplayerLevelType
	{
		Unknown = 0,

		Battle,
		TeamBattle,
		CaptureTheFlag,
		Race,
		TreasureHunt,
		CoopStory
	};
	
	class MultiLevelHandler : public LevelHandler
	{
		friend class ContentResolver;
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::LevelScriptLoader;
#endif

	public:
		MultiLevelHandler(IRootController* root, NetworkManager* networkManager, const LevelInitialization& levelInit);
		~MultiLevelHandler() override;

		float GetAmbientLight() const override;
		void SetAmbientLight(float value) override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(int32_t width, int32_t height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

		void AddActor(std::shared_ptr<Actors::ActorBase> actor) override;

		std::shared_ptr<AudioBufferPlayer> PlaySfx(AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain = 1.0f, float pitch = 1.0f) override;
		std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) override;
		void WarpCameraToTarget(const std::shared_ptr<Actors::ActorBase>& actor, bool fast = false) override;
		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider) override;
		void FindCollisionActorsByAABB(Actors::ActorBase* self, const AABBf& aabb, const std::function<bool(Actors::ActorBase*)>& callback) override;
		void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(Actors::ActorBase*)>& callback) override;
		void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(Actors::ActorBase*)> callback) override;

		void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, uint8_t* eventParams) override;
		void BeginLevelChange(ExitType exitType, const StringView& nextLevel) override;
		void HandleGameOver() override;
		bool HandlePlayerDied(const std::shared_ptr<Actors::ActorBase>& player) override;
		void HandlePlayerWarped(const std::shared_ptr<Actors::ActorBase>& player, const Vector2f& prevPos, bool fast) override;
		void SetCheckpoint(Vector2f pos) override;
		void RollbackToCheckpoint() override;
		void ActivateSugarRush() override;
		void ShowLevelText(const StringView& text) override;
		void ShowCoins(int32_t count) override;
		void ShowGems(int32_t count) override;
		StringView GetLevelText(uint32_t textId, int32_t index = -1, uint32_t delimiter = 0) override;
		void OverrideLevelText(uint32_t textId, const StringView& value) override;
		void LimitCameraView(int left, int width) override;
		void ShakeCameraView(float duration) override;
		void SetWeather(WeatherType type, uint8_t intensity) override;
		bool BeginPlayMusic(const StringView& path, bool setDefault = false, bool forceReload = false) override;

		bool PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads = true) override;
		bool PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) override;
		bool PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads = true) override;
		bool PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) override;
		float PlayerHorizontalMovement(int32_t index) override;
		float PlayerVerticalMovement(int32_t index) override;

		void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) override;

		bool OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength);

	private:
		struct PeerState {
			Actors::Player* Player;
			std::uint32_t LastUpdated;

			PeerState() {}
			PeerState(Actors::Player* player) : Player(player), LastUpdated(0) {}
		};

		struct StateFrame {
			std::int64_t Time;
			Vector2f Pos;
		};

		enum class PlayerFlags {
			None = 0,

			SpecialMoveMask = 0x07,

			IsFacingLeft = 0x10,
			IsVisible = 0x20,
			IsActivelyPushing = 0x40,

			PressedUp = 0x100,
			PressedDown = 0x200,
			PressedLeft = 0x400,
			PressedRight = 0x800,
			PressedJump = 0x1000,
			PressedRun = 0x2000,

			JustWarped = 0x10000
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(PlayerFlags);

		struct PlayerState {
			StateFrame StateBuffer[4];
			std::int32_t StateBufferPos;
			PlayerFlags Flags;
			float WarpTimeLeft;

			PlayerState() {}

			PlayerState(const Vector2f& pos);
		};

		static constexpr std::int64_t ServerDelay = 64;

		NetworkManager* _networkManager;
		bool _isServer;
		float _updateTimeLeft;
		bool _initialUpdateSent;
		HashMap<Peer, PeerState> _peerStates; // Server: Per peer state
		HashMap<std::uint8_t, PlayerState> _playerStates; // Server: Per (remote) player state
		HashMap<std::uint32_t, std::shared_ptr<Actors::RemoteActor>> _remoteActors; // Client: Actor ID -> Remote Actor created by server
		std::uint32_t _lastPlayerIndex;	// Server: last assigned ID, Client: ID assigned by server
		bool _justWarped; // Client: set to true from HandlePlayerWarped()

		void OnLevelLoaded(const StringView& fullPath, const StringView& name, const StringView& nextLevel, const StringView& secretLevel,
			std::unique_ptr<Tiles::TileMap>& tileMap, std::unique_ptr<Events::EventMap>& eventMap,
			const StringView& musicPath, const Vector4f& ambientColor, WeatherType weatherType, uint8_t weatherIntensity, uint16_t waterLevel, SmallVectorImpl<String>& levelTexts);

		void UpdatePlayerLocalPos(Actors::Player* player, const PlayerState& playerState);
		void OnRemotePlayerPosReceived(PlayerState& playerState, const Vector2f& pos, PlayerFlags flags);
	};
}

#endif