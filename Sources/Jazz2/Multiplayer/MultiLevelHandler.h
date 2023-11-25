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
		MultiLevelHandler(IRootController* root, NetworkManager* networkManager);
		~MultiLevelHandler() override;

		bool Initialize(const LevelInitialization& levelInit) override;

		bool IsPausable() const override {
			return false;
		}

		float GetAmbientLight() const override;
		void SetAmbientLight(float value) override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(int32_t width, int32_t height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

		void AddActor(std::shared_ptr<Actors::ActorBase> actor) override;

		std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, const StringView& identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch) override;
		std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) override;
		void WarpCameraToTarget(Actors::ActorBase* actor, bool fast = false) override;
		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider) override;
		void FindCollisionActorsByAABB(Actors::ActorBase* self, const AABBf& aabb, const std::function<bool(Actors::ActorBase*)>& callback) override;
		void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(Actors::ActorBase*)>& callback) override;
		void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(Actors::ActorBase*)> callback) override;

		void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, uint8_t* eventParams) override;
		void BeginLevelChange(ExitType exitType, const StringView& nextLevel) override;
		void HandleGameOver() override;
		bool HandlePlayerDied(Actors::Player* player) override;
		bool HandlePlayerFireWeapon(Actors::Player* player, WeaponType& weaponType, std::uint16_t& ammoDecrease) override;
		bool HandlePlayerSpring(Actors::Player* player, const Vector2f& pos, const Vector2f& force, bool keepSpeedX, bool keepSpeedY) override;
		void HandlePlayerWarped(Actors::Player* player, const Vector2f& prevPos, bool fast) override;
		void SetCheckpoint(const Vector2f& pos) override;
		void RollbackToCheckpoint() override;
		void ActivateSugarRush() override;
		void ShowLevelText(const StringView& text) override;
		void ShowCoins(int32_t count) override;
		void ShowGems(int32_t count) override;
		StringView GetLevelText(uint32_t textId, int32_t index = -1, uint32_t delimiter = 0) override;
		void OverrideLevelText(uint32_t textId, const StringView& value) override;
		void LimitCameraView(int left, int width) override;
		void ShakeCameraView(float duration) override;
		void SetTrigger(std::uint8_t triggerId, bool newState) override;
		void SetWeather(WeatherType type, uint8_t intensity) override;
		bool BeginPlayMusic(const StringView& path, bool setDefault = false, bool forceReload = false) override;

		bool PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads = true) override;
		bool PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) override;
		bool PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads = true) override;
		bool PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) override;
		float PlayerHorizontalMovement(int32_t index) override;
		float PlayerVerticalMovement(int32_t index) override;

		bool SerializeResumableToStream(Stream& dest) override;

		void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) override;

		void AttachComponents(LevelDescriptor&& descriptor) override;

		bool OnPeerDisconnected(const Peer& peer);
		bool OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength);

	protected:
		void BeforeActorDestroyed(Actors::ActorBase* actor) override;
		void ProcessEvents(float timeMult) override;
		void PrepareNextLevelInitialization(LevelInitialization& levelInit) override;

	private:
		enum class PeerState {
			Unknown,
			LevelLoaded,
			LevelSynchronized
		};

		struct PeerDesc {
			Actors::Player* Player;
			PeerState State;
			std::uint32_t LastUpdated;

			PeerDesc() {}
			PeerDesc(Actors::Player* player, PeerState state) : Player(player), State(state), LastUpdated(0) {}
		};

		struct StateFrame {
			std::int64_t Time;
			Vector2f Pos;
			Vector2f Speed;
		};

		enum class PlayerFlags {
			None = 0,

			SpecialMoveMask = 0x07,

			IsFacingLeft = 0x10,
			IsVisible = 0x20,
			IsActivelyPushing = 0x40,

			JustWarped = 0x100
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(PlayerFlags);

		struct PlayerState {
			StateFrame StateBuffer[8];
			std::int32_t StateBufferPos;
			PlayerFlags Flags;
			std::uint32_t PressedKeys;
			std::uint64_t WarpSeqNum;
			float WarpTimeLeft;
			float DeviationTime;

			PlayerState();
			PlayerState(const Vector2f& pos, const Vector2f& speed);
		};

		static constexpr std::int64_t ServerDelay = 64;

		NetworkManager* _networkManager;
		bool _isServer;
		float _updateTimeLeft;
		bool _initialUpdateSent;
		HashMap<Peer, PeerDesc> _peerDesc; // Server: Per peer description
		HashMap<std::uint8_t, PlayerState> _playerStates; // Server: Per (remote) player state
		HashMap<std::uint32_t, std::shared_ptr<Actors::RemoteActor>> _remoteActors; // Client: Actor ID -> Remote Actor created by server
		HashMap<Actors::ActorBase*, std::uint32_t> _remotingActors; // Server: Local Actor created by server -> Actor ID
		std::uint32_t _lastSpawnedActorId;	// Server: last assigned actor/player ID, Client: ID assigned by server
		std::uint64_t _seqNum; // Client: sequence number of the last update
		std::uint64_t _seqNumWarped; // Client: set to _seqNum from HandlePlayerWarped() when warped
		bool _suppressRemoting; // Server: if true, actor will not be automatically remoted to other players
		bool _ignorePackets;

		void SynchronizePeers();
		void UpdatePlayerLocalPos(Actors::Player* player, PlayerState& playerState, float timeMult);
		void OnRemotePlayerPosReceived(PlayerState& playerState, const Vector2f& pos, const Vector2f speed, PlayerFlags flags);
	};
}

#endif