#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../LevelHandler.h"
#include "MultiplayerGameMode.h"
#include "NetworkManager.h"

namespace Jazz2::Actors::Multiplayer
{
	class RemoteActor;
	class RemotePlayerOnServer;
}

namespace Jazz2::Multiplayer
{
	class MultiLevelHandler : public LevelHandler
	{
		DEATH_RUNTIME_OBJECT(LevelHandler);

		friend class ContentResolver;
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::LevelScriptLoader;
#endif
		friend class Actors::Multiplayer::RemotePlayerOnServer;

	public:
		MultiLevelHandler(IRootController* root, NetworkManager* networkManager);
		~MultiLevelHandler() override;

		bool Initialize(const LevelInitialization& levelInit) override;

		bool IsPausable() const override {
			return false;
		}

		float GetDefaultAmbientLight() const override;
		void SetAmbientLight(Actors::Player* player, float value) override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

		void AddActor(std::shared_ptr<Actors::ActorBase> actor) override;

		std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, const StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch) override;
		std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(const StringView identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) override;
		void WarpCameraToTarget(Actors::ActorBase* actor, bool fast = false) override;
		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider) override;
		void FindCollisionActorsByAABB(Actors::ActorBase* self, const AABBf& aabb, const std::function<bool(Actors::ActorBase*)>& callback) override;
		void FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(Actors::ActorBase*)>& callback) override;
		void GetCollidingPlayers(const AABBf& aabb, const std::function<bool(Actors::ActorBase*)> callback) override;

		void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, uint8_t* eventParams) override;
		void BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, const StringView nextLevel = {}) override;
		void HandleGameOver(Actors::Player* player) override;
		bool HandlePlayerDied(Actors::Player* player) override;
		void HandlePlayerWarped(Actors::Player* player, const Vector2f& prevPos, WarpFlags flags) override;
		void HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override;
		void HandlePlayerGems(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override;
		void SetCheckpoint(Actors::Player* player, const Vector2f& pos) override;
		void RollbackToCheckpoint(Actors::Player* player) override;
		void ActivateSugarRush(Actors::Player* player) override;
		void ShowLevelText(const StringView text) override;
		StringView GetLevelText(std::uint32_t textId, std::int32_t index = -1, std::uint32_t delimiter = 0) override;
		void OverrideLevelText(std::uint32_t textId, const StringView value) override;
		void LimitCameraView(Actors::Player* player, std::int32_t left, std::int32_t width) override;
		void ShakeCameraView(Actors::Player* player, float duration) override;
		void ShakeCameraViewNear(Vector2f pos, float duration) override;
		void SetTrigger(std::uint8_t triggerId, bool newState) override;
		void SetWeather(WeatherType type, uint8_t intensity) override;
		bool BeginPlayMusic(const StringView path, bool setDefault = false, bool forceReload = false) override;

		bool PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads = true) override;
		bool PlayerActionPressed(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) override;
		bool PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads = true) override;
		bool PlayerActionHit(std::int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad) override;
		float PlayerHorizontalMovement(std::int32_t index) override;
		float PlayerVerticalMovement(std::int32_t index) override;

		bool SerializeResumableToStream(Stream& dest) override;

		void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) override;

		void AttachComponents(LevelDescriptor&& descriptor) override;
		void SpawnPlayers(const LevelInitialization& levelInit) override;

		MultiplayerGameMode GetGameMode() const;
		bool SetGameMode(MultiplayerGameMode value);

		bool OnPeerDisconnected(const Peer& peer);
		bool OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength);

	protected:
		void BeforeActorDestroyed(Actors::ActorBase* actor) override;
		void ProcessEvents(float timeMult) override;
		void PrepareNextLevelInitialization(LevelInitialization& levelInit) override;

		bool HandlePlayerSpring(Actors::Player* player, const Vector2f& pos, const Vector2f& force, bool keepSpeedX, bool keepSpeedY);
		void HandlePlayerBeforeWarp(Actors::Player* player, const Vector2f& pos, WarpFlags flags);
		void HandlePlayerTakeDamage(Actors::Player* player, std::int32_t amount, float pushForce);
		void HandlePlayerRefreshAmmo(Actors::Player* player, WeaponType weaponType);
		void HandlePlayerRefreshWeaponUpgrades(Actors::Player* player, WeaponType weaponType);
		void HandlePlayerWeaponChanged(Actors::Player* player);

	private:
		enum class PeerState {
			Unknown,
			LevelLoaded,
			LevelSynchronized
		};

		struct PeerDesc {
			Actors::Multiplayer::RemotePlayerOnServer* Player;
			PeerState State;
			std::uint32_t LastUpdated;

			PeerDesc() {}
			PeerDesc(Actors::Multiplayer::RemotePlayerOnServer* player, PeerState state) : Player(player), State(state), LastUpdated(0) {}
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
			PlayerFlags Flags;
			std::uint64_t PressedKeys;
			std::uint64_t PressedKeysLast;
			//std::uint64_t WarpSeqNum;
			//float WarpTimeLeft;

			PlayerState();
			PlayerState(const Vector2f& pos, const Vector2f& speed);
		};

		static constexpr float UpdatesPerSecond = 16.0f; // ~62 ms interval
		static constexpr std::int64_t ServerDelay = 64;

		NetworkManager* _networkManager;
		MultiplayerGameMode _gameMode;
		bool _isServer;
		float _updateTimeLeft;
		bool _initialUpdateSent;
		HashMap<Peer, PeerDesc> _peerDesc; // Server: Per peer description
		HashMap<std::uint8_t, PlayerState> _playerStates; // Server: Per (remote) player state
		HashMap<std::uint32_t, std::shared_ptr<Actors::ActorBase>> _remoteActors; // Client: Actor ID -> Remote Actor created by server
		HashMap<Actors::ActorBase*, std::uint32_t> _remotingActors; // Server: Local Actor created by server -> Actor ID
		std::uint32_t _lastSpawnedActorId;	// Server: last assigned actor/player ID, Client: ID assigned by server
		std::uint64_t _seqNum; // Client: sequence number of the last update
		std::uint64_t _seqNumWarped; // Client: set to _seqNum from HandlePlayerWarped() when warped
		bool _suppressRemoting; // Server: if true, actor will not be automatically remoted to other players
		bool _ignorePackets;

		void SynchronizePeers();
		std::uint32_t FindFreeActorId();
		std::uint8_t FindFreePlayerId();

		static bool ActorShouldBeMirrored(Actors::ActorBase* actor);

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
		static constexpr std::int32_t PlotValueCount = 512;
		
		std::int32_t _plotIndex;
		float _actorsMaxCount;
		float _actorsCount[PlotValueCount];
		float _remoteActorsCount[PlotValueCount];
		float _remotingActorsCount[PlotValueCount];
		float _mirroredActorsCount[PlotValueCount];
		float _updatePacketMaxSize;
		float _updatePacketSize[PlotValueCount];
		float _compressedUpdatePacketSize[PlotValueCount];

		void ShowDebugWindow();
#endif
	};
}

#endif