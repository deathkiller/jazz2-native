#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../LevelHandler.h"
#include "MpGameMode.h"
#include "NetworkManager.h"
#include "../Actors/Player.h"
#include "../UI/InGameConsole.h"

#include <Threading/Spinlock.h>

namespace Jazz2::Actors::Multiplayer
{
	class MpPlayer;
	class PlayerOnServer;
	class RemotablePlayer;
	class RemoteActor;
	class RemotePlayerOnServer;
}

namespace Jazz2::UI::Multiplayer
{
	class MpInGameCanvasLayer;
	class MpInGameLobby;
	class MpHUD;
}

namespace Jazz2::Multiplayer
{
	/**
		@brief Level handler of an online multiplayer game session

		@experimental
	*/
	class MpLevelHandler : public LevelHandler
	{
		DEATH_RUNTIME_OBJECT(LevelHandler);

		friend class ContentResolver;
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::LevelScriptLoader;
#endif
		friend class Actors::Multiplayer::PlayerOnServer;
		friend class Actors::Multiplayer::RemotablePlayer;
		friend class Actors::Multiplayer::RemotePlayerOnServer;
		friend class UI::Multiplayer::MpInGameCanvasLayer;
		friend class UI::Multiplayer::MpInGameLobby;
		friend class UI::Multiplayer::MpHUD;

	public:
		/** @brief Level state */
		enum class LevelState {
			Unknown,				/**< Unknown */
			InitialUpdatePending,	/**< Level was just created and initial update needs to be sent */
			PreGame,				/**< Pre-game is active */
			WaitingForMinPlayers,	/**< Pre-game ended, but not enough players connected yet */
			Countdown3,				/**< Countdown started (3) */
			Countdown2,				/**< Countdown continues (2) */
			Countdown1,				/**< Countdown continues (1) */
			Running,				/**< Round started */
			Ending,					/**< Round is ending */
			Ended					/**< Round ended */
		};

		MpLevelHandler(IRootController* root, NetworkManager* networkManager, LevelState levelState, bool enableLedgeClimb);
		~MpLevelHandler() override;

		bool Initialize(const LevelInitialization& levelInit) override;

		bool IsLocalSession() const override;
		bool IsPausable() const override;
		float GetHurtInvulnerableTime() const override;

		float GetDefaultAmbientLight() const override;
		void SetAmbientLight(Actors::Player* player, float value) override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;
		bool OnConsoleCommand(StringView line) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

		void AddActor(std::shared_ptr<Actors::ActorBase> actor) override;

		std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch) override;
		std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) override;
		void WarpCameraToTarget(Actors::ActorBase* actor, bool fast = false) override;
		bool IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider) override;
		void FindCollisionActorsByAABB(const Actors::ActorBase* self, const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback) override;
		void FindCollisionActorsByRadius(float x, float y, float radius, Function<bool(Actors::ActorBase*)>&& callback) override;
		void GetCollidingPlayers(const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback) override;

		void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, std::uint8_t* eventParams) override;
		void BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, StringView nextLevel = {}) override;

		void HandleLevelChange(LevelInitialization&& levelInit) override;
		void HandleGameOver(Actors::Player* player) override;
		bool HandlePlayerDied(Actors::Player* player) override;
		void HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags) override;
		void HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override;
		void HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount) override;
		void SetCheckpoint(Actors::Player* player, Vector2f pos) override;
		void RollbackToCheckpoint(Actors::Player* player) override;
		void HandleActivateSugarRush(Actors::Player* player) override;
		void ShowLevelText(StringView text, Actors::ActorBase* initiator = nullptr) override;
		StringView GetLevelText(std::uint32_t textId, std::int32_t index = -1, std::uint32_t delimiter = 0) override;
		void OverrideLevelText(std::uint32_t textId, StringView value) override;
		void LimitCameraView(Actors::Player* player, std::int32_t left, std::int32_t width) override;
		void ShakeCameraView(Actors::Player* player, float duration) override;
		void ShakeCameraViewNear(Vector2f pos, float duration) override;
		void SetTrigger(std::uint8_t triggerId, bool newState) override;
		void SetWeather(WeatherType type, std::uint8_t intensity) override;
		bool BeginPlayMusic(StringView path, bool setDefault = false, bool forceReload = false) override;

		bool PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads = true) override;
		bool PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad) override;
		bool PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads = true) override;
		bool PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad) override;
		float PlayerHorizontalMovement(Actors::Player* player) override;
		float PlayerVerticalMovement(Actors::Player* player) override;
		void PlayerExecuteRumble(Actors::Player* player, StringView rumbleEffect) override;

		bool SerializeResumableToStream(Stream& dest) override;

		void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) override;

		/** @brief Returns current game mode */
		MpGameMode GetGameMode() const;
		/** @brief Sets current game mode */
		bool SetGameMode(MpGameMode value);

		/** @brief Returns owner of the specified object or the player itself */
		static Actors::Multiplayer::MpPlayer* GetWeaponOwner(Actors::ActorBase* actor);

		// Server-only methods
		/** @brief Processes the specified server command */
		bool ProcessCommand(const Peer& peer, StringView line, bool isAdmin);
		/** @brief Sends the message to the specified peer */
		void SendMessage(const Peer& peer, UI::MessageLevel level, StringView message);

		/** @brief Called when a peer disconnects from the server, see @ref INetworkHandler */
		bool OnPeerDisconnected(const Peer& peer);
		/** @brief Called when a packet is received, see @ref INetworkHandler */
		bool OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data);

	protected:
		void AttachComponents(LevelDescriptor&& descriptor) override;
		std::unique_ptr<UI::HUD> CreateHUD() override;
		void SpawnPlayers(const LevelInitialization& levelInit) override;
		bool IsCheatingAllowed() override;

		void BeforeActorDestroyed(Actors::ActorBase* actor) override;
		void ProcessEvents(float timeMult) override;

		/** @brief Called when a player entered a transition to change the level */
		void HandlePlayerLevelChanging(Actors::Player* player, ExitType exitType);
		/** @brief Called when a player interacts with a spring */
		bool HandlePlayerSpring(Actors::Player* player, Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY);
		/** @brief Called when a player is going to warp */
		void HandlePlayerBeforeWarp(Actors::Player* player, Vector2f pos, WarpFlags flags);
		/** @brief Called when a player changed modifier */
		void HandlePlayerSetModifier(Actors::Player* player, Actors::Player::Modifier modifier, const std::shared_ptr<Actors::ActorBase>& decor);
		/** @brief Called when a player takes a damage */
		void HandlePlayerTakeDamage(Actors::Player* player, std::int32_t amount, float pushForce);
		/** @brief Called when a player requests to synchronize weapon ammo */
		void HandlePlayerRefreshAmmo(Actors::Player* player, WeaponType weaponType);
		/** @brief Called when a player requests to synchronize weapon upgrades */
		void HandlePlayerRefreshWeaponUpgrades(Actors::Player* player, WeaponType weaponType);
		/** @brief Called when a player changed dizzy duration */
		void HandlePlayerSetDizzyTime(Actors::Player* player, float timeLeft);
		/** @brief Called when a player emits a weapon flare */
		void HandlePlayerEmitWeaponFlare(Actors::Player* player);
		/** @brief Called when a player changes their current weapon */
		void HandlePlayerWeaponChanged(Actors::Player* player);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct RemotingActorInfo {
			std::uint32_t ActorID;
			std::int32_t LastPosX;
			std::int32_t LastPosY;
			std::uint32_t LastAnimation;
			std::uint16_t LastRotation;
			std::uint16_t LastScaleX;
			std::uint16_t LastScaleY;
			std::uint8_t LastRendererType;
		};

		struct MultiplayerSpawnPoint {
			Vector2f Pos;
			std::uint8_t Team;

			MultiplayerSpawnPoint(Vector2f pos, std::uint8_t team)
				: Pos(pos), Team(team) {}
		};

		struct PendingSfx {
			Actors::ActorBase* Actor;
			String Identifier;
			std::uint16_t Gain;
			std::uint16_t Pitch;

			PendingSfx(Actors::ActorBase* actor, String identifier, std::uint16_t gain, std::uint16_t pitch)
				: Actor(actor), Identifier(std::move(identifier)), Gain(gain), Pitch(pitch) {}
		};
#endif

		//static constexpr float UpdatesPerSecond = 16.0f; // ~62 ms interval
		static constexpr float UpdatesPerSecond = 30.0f; // ~33 ms interval
		static constexpr std::int64_t ServerDelay = 64;
		static constexpr float EndingDuration = 10 * FrameTimer::FramesPerSecond;

		NetworkManager* _networkManager;
		float _updateTimeLeft;
		float _gameTimeLeft;
		LevelState _levelState;
		bool _isServer;
		bool _enableSpawning;
		HashMap<std::uint32_t, std::shared_ptr<Actors::ActorBase>> _remoteActors; // Client: Actor ID -> Remote Actor created by server
		HashMap<Actors::ActorBase*, RemotingActorInfo> _remotingActors; // Server: Local Actor created by server -> Info
		HashMap<std::uint32_t, String> _playerNames; // Client: Actor ID -> Player name
		SmallVector<Pair<std::uint32_t, std::uint32_t>, 0> _positionsInRound; // Client: Actor ID -> Position In Round
		SmallVector<MultiplayerSpawnPoint, 0> _multiplayerSpawnPoints;
		SmallVector<PendingSfx, 0> _pendingSfx;
		std::uint32_t _lastSpawnedActorId;	// Server: last assigned actor/player ID, Client: ID assigned by server
		std::int32_t _waitingForPlayerCount;	// Client: number of players needed to start the game
		std::uint64_t _seqNum; // Client: sequence number of the last update
		std::uint64_t _seqNumWarped; // Client: set to _seqNum from HandlePlayerWarped() when warped
		bool _suppressRemoting; // Server: if true, actor will not be automatically remoted to other players
		bool _ignorePackets;
		bool _enableLedgeClimb;
		bool _controllableExternal;
		Threading::Spinlock _lock;

#if defined(DEATH_DEBUG)
		std::int32_t _debugAverageUpdatePacketSize;
#endif

		std::unique_ptr<UI::Multiplayer::MpInGameCanvasLayer> _inGameCanvasLayer;
		std::unique_ptr<UI::Multiplayer::MpInGameLobby> _inGameLobby;

		void SynchronizePeers();
		std::uint32_t FindFreeActorId();
		std::uint8_t FindFreePlayerId();
		bool IsLocalPlayer(Actors::ActorBase* actor);
		void ApplyGameModeToAllPlayers(MpGameMode gameMode);
		void ApplyGameModeToPlayer(MpGameMode gameMode, Actors::Player* player);
		void ShowAlertToAllPlayers(StringView text);
		void SetControllableToAllPlayers(bool enable);
		void SendLevelStateToAllPlayers();
		void ResetAllPlayerStats();
		Vector2f GetSpawnPoint(PlayerType playerType);
		void WarpAllPlayersToStart();
		void CalculatePositionInRound();
		void CheckGameEnds();
		void EndGame(Actors::Multiplayer::MpPlayer* winner);
		void EndGameOnTimeOut();

		bool ApplyFromPlaylist();
		void SetWelcomeMessage(StringView message);
		void SetPlayerReady(PlayerType playerType);

		static bool ActorShouldBeMirrored(Actors::ActorBase* actor);
		static void InitializeCreateRemoteActorPacket(MemoryStream& packet, std::uint32_t actorId, const Actors::ActorBase* actor);

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