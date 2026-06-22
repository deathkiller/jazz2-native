#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../LevelHandler.h"
#include "MpGameMode.h"
#include "Teams.h"
#include "NetworkManager.h"
#include "GameModes/GameModeFactory.h"
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
		@brief Level handler of a multiplayer game session (online or local splitscreen)
		
		Subclass of @ref LevelHandler that runs a multiplayer match on top of a @ref NetworkManager. It is the
		authoritative server in two configurations: an online session (@ref NetworkState::Listening), where it
		spawns and remotes actors to connected clients and processes their packets and commands, and a
		socket-less local splitscreen session (@ref NetworkState::Local) with only local players and no
		networking. As a client it applies state updates and forwards local input. It also manages the game
		mode, round flow, asset validation/streaming and the in-game lobby.

		@experimental
	*/
	class MpLevelHandler : public LevelHandler, public IServerStatusProvider, public IGameModeContext
	{
		DEATH_RUNTIME_OBJECT(LevelHandler, IServerStatusProvider);

#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::LevelScriptLoader;
#endif
		friend class Actors::Multiplayer::MpPlayer;
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

		/** @brief Asset type */
		enum class AssetType {
			Unknown,	/**< Unknown */
			Level,		/**< Level */
			TileSet,	/**< Tile set */
			Music,		/**< Music */
			Script		/**< Script */
		};

		/**
		 * @brief Creates a new instance
		 *
		 * @param root             Root controller
		 * @param networkManager   Network manager that handles the connection
		 * @param levelState       Initial level state
		 * @param enableLedgeClimb Whether ledge climbing is enabled
		 */
		MpLevelHandler(IRootController* root, NetworkManager* networkManager, LevelState levelState, bool enableLedgeClimb);
		~MpLevelHandler() override;

		bool Initialize(const LevelInitialization& levelInit) override;
		bool Initialize(Stream& src, std::uint16_t version) override;

		bool IsLocalSession() const override;
		bool IsServer() const override;
		std::uint32_t GetPlayerFurColor(const Actors::Player* player, std::uint32_t furColor) const override;
		bool IsPausable() const override;
		bool CanPlayersCollide() const override;
		bool CanActivateSugarRush() const override;
		bool CanEventDisappear(EventType eventType) const override;
		float GetHurtInvulnerableTime() const override;

		float GetDefaultAmbientLight() const override;
		void SetAmbientLight(Actors::Player* player, float value) override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;
		bool OnConsoleCommand(StringView line) override;

		void OnTouchEvent(const TouchEvent& event) override;

		void AddActor(std::shared_ptr<Actors::ActorBase> actor) override;

		std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch) override;
		std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) override;
		void WarpCameraToTarget(Actors::ActorBase* actor, bool fast = false) override;

		void BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, std::uint8_t* eventParams) override;
		void BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, StringView nextLevel = {}) override;

		void SendPacket(const Actors::ActorBase* self, ArrayView<const std::uint8_t> data) override;

		void HandleBossActivated(Actors::Bosses::BossBase* boss, Actors::ActorBase* initiator) override;
		void HandleLevelChange(LevelInitialization&& levelInit) override;
		void HandleGameOver(Actors::Player* player) override;
		bool HandlePlayerDied(Actors::Player* player) override;
		void HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags) override;
		void HandlePlayerPushed(Actors::Player* player) override;
		void HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount) override;
		void HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount) override;
		void SetCheckpoint(Actors::Player* player, Vector2f pos) override;
		void RollbackToCheckpoint(Actors::Player* player) override;
		void HandleActivateSugarRush(Actors::Player* player) override;
		void HandleCreateParticleDebrisOnPerish(const Actors::ActorBase* self, Actors::ParticleDebrisEffect effect, Vector2f speed) override;
		void HandleCreateSpriteDebris(const Actors::ActorBase* self, AnimState state, std::int32_t count) override;
		void ShowLevelText(StringView text, Actors::ActorBase* initiator = nullptr) override;
		StringView GetLevelText(std::uint32_t textId, std::int32_t index = -1, std::uint32_t delimiter = 0) override;
		void OverrideLevelText(std::uint32_t textId, StringView value) override;
		void LimitCameraView(Actors::Player* player, Vector2f playerPos, std::int32_t left, std::int32_t width) override;
		void OverrideCameraView(Actors::Player* player, float x, float y, bool topLeft = false) override;
		void ShakeCameraView(Actors::Player* player, float duration) override;
		void ShakeCameraViewNear(Vector2f pos, float duration) override;
		void SetTrigger(std::uint8_t triggerId, bool newState) override;
		void SetWeather(WeatherType type, std::uint8_t intensity) override;
		bool BeginPlayMusic(StringView path, bool setDefault = false, bool forceReload = false) override;

		bool PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad) override;
		bool PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad) override;
		float PlayerHorizontalMovement(Actors::Player* player) override;
		float PlayerVerticalMovement(Actors::Player* player) override;
		void PlayerExecuteRumble(Actors::Player* player, StringView rumbleEffect) override;

		bool SerializeResumableToStream(Stream& dest) override;

		void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) override;

		StringView GetLevelDisplayName() const override;

		/** @brief Returns current game mode */
		MpGameMode GetGameMode() const;
		/** @brief Returns the active game-mode rules object, or `nullptr` if the mode has not been migrated to @ref IGameMode yet */
		IGameMode* GetActiveGameMode() const;
		/** @brief Lets the active game mode draw its part of the HUD into @p hud; returns `false` if no migrated mode is active (caller should fall back to its own drawing) */
		bool DrawActiveGameModeHUD(IGameModeHUD& hud, Actors::Player* player, const Rectf& view);

		const ServerConfiguration& GetServerConfiguration() const override;
		ArrayView<Actors::Player* const> GetPlayers() const override;
		MpPlayerState& GetPlayerState(Actors::Player* player) override;
		bool IsSpectating(Actors::Player* player) const override;
		std::uint8_t GetCtfFlagStateCount() const override;
		std::uint8_t GetCtfFlagState(std::uint8_t team) const override;
		Vector2f GetSpawnPoint(Actors::Player* player) override;
		float GetElapsedFrames() const override;
		void EndGame(Actors::Player* winner) override;

		/** @brief Returns the number of laps required to finish a race (Race/TeamRace) */
		std::uint32_t GetTotalLaps() const;
		/** @brief Returns the current round state (synced to clients) */
		LevelState GetLevelState() const {
			return _levelState;
		}
		/** @brief Sets current game mode */
		bool SetGameMode(MpGameMode value);
		/** @brief Synchronizes current game mode with all peers without restarting round */
		bool SynchronizeGameMode();

		/** @brief Requests to change the local player's team (client) or applies it directly (host) */
		void RequestChangeTeam(std::uint8_t team);

		/** @brief Single row of the multiplayer scoreboard */
		struct PlayerScore {
			String Name;
			std::uint8_t Team;
			std::uint32_t Kills;
			std::uint32_t Deaths;
			std::uint32_t Points;
			std::uint32_t Extra;	// Laps (Race), treasure (Treasure Hunt) or captures (CTF), 0 otherwise
			std::int32_t PingMs;	// Round-trip time in ms, -1 if unknown (e.g., the local/host player)
			bool IsLocal;
		};

		/** @brief Returns the current scoreboard rows (built on the server and synced to clients) */
		const SmallVector<PlayerScore, 0>& GetScoreboard() const {
			return _scoreboard;
		}

		/** @brief Returns owner of the specified object or the player itself */
		static Actors::Multiplayer::MpPlayer* GetWeaponOwner(Actors::ActorBase* actor);

		// Server-only methods
		/** @brief Processes the specified server command */
		bool ProcessCommand(const Peer& peer, StringView line, bool isAdmin);
		/** @brief Sends the message to the specified peer */
		void SendMessage(const Peer& peer, UI::MessageLevel level, StringView message);
		/** @brief Sends the message to all authenticated peers */
		void SendMessageToAll(StringView message, bool asChatFromServer = false);

		/** @brief Enables or disables spectate mode for the specified player (server-side) */
		void SetPlayerSpectateMode(Actors::Player* player, SpectateMode mode);
		/** @brief Client-side method to request spectate mode */
		void RequestSpectateMode(bool enable);
		/** @brief Returns `true` if the local player is currently spectating */
		bool IsSpectating();
		/** @brief Returns `true` if spectate mode is enabled by the server */
		bool IsSpectateAvailable() const;
		/** @brief Shows the in-game lobby so the local player can (re)select a character, (re)joining the game on confirmation */
		void ShowCharacterSelectLobby();

		/** @brief Called when a peer disconnects from the server, see @ref INetworkHandler */
		bool OnPeerDisconnected(const Peer& peer);
		/** @brief Called when a packet is received, see @ref INetworkHandler */
		bool OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data);

		/** @brief Returns full path of the specified asset */
		static String GetAssetFullPath(AssetType type, StringView path, StaticArrayView<Uuid::Size, Uuid::Type> remoteServerId = {}, bool forWrite = false);

	protected:
		void AttachComponents(LevelDescriptor&& descriptor) override;
		std::unique_ptr<UI::HUD> CreateHUD() override;
		void SpawnPlayers(const LevelInitialization& levelInit) override;
		std::shared_ptr<Actors::Player> CreateResumablePlayer(std::int32_t index) override;
		void PrepareNextLevelInitialization(LevelInitialization& levelInit) override;
		bool IsCheatingAllowed() override;

		void BeforeActorDestroyed(Actors::ActorBase* actor) override;
		void ProcessEvents(float timeMult) override;

		void PauseGame() override;
		void ResumeGame() override;
		void ShowConsole() override;
		void HideConsole() override;

		/** @brief Called when a player entered a transition to change the level */
		void HandlePlayerLevelChanging(Actors::Player* player, ExitType exitType);
		/** @brief Called when a player pushes a solid object */
		bool HandlePlayerPush(Actors::Player* player, float pushSpeedX);
		/** @brief Called when a player interacts with a spring */
		bool HandlePlayerSpring(Actors::Player* player, Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY);
		/** @brief Called when a player is going to warp */
		void HandlePlayerBeforeWarp(Actors::Player* player, Vector2f pos, WarpFlags flags);
		/** @brief Called when a player changed modifier */
		void HandlePlayerSetModifier(Actors::Player* player, Actors::Player::Modifier modifier, const std::shared_ptr<Actors::ActorBase>& decor);
		/** @brief Called when a player freezes */
		void HandlePlayerFreeze(Actors::Player* player, float timeLeft);
		/** @brief Called when a player sets invulnerability */
		void HandlePlayerSetInvulnerability(Actors::Player* player, float timeLeft, Actors::Player::InvulnerableType type);
		/** @brief Called when a player sets score */
		void HandlePlayerSetScore(Actors::Player* player, std::int32_t value);
		/** @brief Called when a player sets health */
		void HandlePlayerSetHealth(Actors::Player* player, std::int32_t count);
		/** @brief Called when a player sets lives */
		void HandlePlayerSetLives(Actors::Player* player, std::int32_t count);
		/** @brief Called when a player takes a damage */
		void HandlePlayerTakeDamage(Actors::Player* player, std::int32_t amount, float pushForce);
		/** @brief Called when a player is bumped by another player to synchronize the resulting knockback */
		void HandlePlayerBumped(Actors::Player* player);
		/** @brief Returns `true` if players can stand on top of each other (per-level @ref ServerConfiguration::PlayerStacking) */
		bool IsPlayerStackingEnabled() const;
		/** @brief Returns the player actor the given player is standing/landing on (one-way platform check), or `nullptr` */
		Actors::ActorBase* FindPlayerToStandOn(Actors::Player* player, float timeMult) override;
		/** @brief Called when a player requests to synchronize weapon ammo */
		void HandlePlayerRefreshAmmo(Actors::Player* player, WeaponType weaponType);
		/** @brief Called when a player requests to synchronize weapon upgrades */
		void HandlePlayerRefreshWeaponUpgrades(Actors::Player* player, WeaponType weaponType);
		/** @brief Called when a player requests to morph to another type */
		void HandlePlayerMorphTo(Actors::Player* player, PlayerType type);
		/** @brief Called when a player changed duration of dizziness */
		void HandlePlayerSetDizzy(Actors::Player* player, float timeLeft);
		/** @brief Called when another player starts/stops standing on a player, to sync its cosmetic lift animation */
		void HandlePlayerSetBeingStoodOn(Actors::Player* player, bool beingStoodOn);
		/** @brief Called when a player sets a shield */
		void HandlePlayerSetShield(Actors::Player* player, ShieldType shieldType, float timeLeft);
		/** @brief Called when a player emits a weapon flare */
		void HandlePlayerEmitWeaponFlare(Actors::Player* player);
		/** @brief Called when a player changes their current weapon */
		void HandlePlayerWeaponChanged(Actors::Player* player, Actors::Player::SetCurrentWeaponReason reason);

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

		struct PlayerName {
			String Name;
			std::uint8_t Flags;
			std::uint32_t FurColor;
			std::uint8_t Team;
		};

		struct PlayerPositionInRound {
			std::uint32_t ActorID;
			std::uint32_t PositionInRound;
			std::uint32_t PointsInRound;
		};

		struct RaceCheckpoint {
			Vector2i Tile;			// Tile coordinates (multiply by TileSet::DefaultTileSize for world pixels)
			std::uint16_t Order;	// Ordering index along the track (raw JJ2+ TextID, or 0,1,2,... when auto-generated)
			std::uint8_t Group;		// Split/group id (JJ2+ Offset); 0 for auto-generated and single-track levels
		};

		struct MultiplayerSpawnPoint {
			Vector2f Pos;
			std::uint8_t Team;

			MultiplayerSpawnPoint(Vector2f pos, std::uint8_t team)
				: Pos(pos), Team(team) {}
		};

		enum class CtfFlagState : std::uint8_t {
			AtBase,		// Resting on its home base
			Carried,	// Being carried by an enemy player (CarrierPlayerIndex)
			Dropped		// Lying at DropPos after the carrier was killed/left
		};

		struct CtfFlag {
			std::uint8_t Team;
			Vector2f BasePos;
			Vector2f DropPos;
			CtfFlagState State;
			std::uint32_t CarrierPlayerIndex;
			std::shared_ptr<Actors::ActorBase> Actor;		// Server: local visual flag actor (host view; not remoted)
			std::shared_ptr<Actors::ActorBase> BaseActor;	// Server: local visual base structure (host view; not remoted)
		};

		// Client: per-team flag info mirrored from the server. The flag/base are rendered as client-local actors
		// (not remoted) driven by this state, so they don't depend on actor remoting/id reuse.
		struct CtfClientFlag {
			std::uint8_t State;
			Vector2f BasePos;
			Vector2f DropPos;
			std::uint32_t CarrierActorId;	// Valid when State == Carried (player index = remote actor id)
			std::shared_ptr<Actors::ActorBase> FlagActor;	// Client-local visual flag
			std::shared_ptr<Actors::ActorBase> BaseActor;	// Client-local visual base
		};

		struct PendingSfx {
			Actors::ActorBase* Actor;
			String Identifier;
			std::uint16_t Gain;
			std::uint16_t Pitch;

			PendingSfx(Actors::ActorBase* actor, String identifier, std::uint16_t gain, std::uint16_t pitch)
				: Actor(actor), Identifier(std::move(identifier)), Gain(gain), Pitch(pitch) {}
		};

		struct RequiredAsset {
			AssetType Type;
			std::uint32_t Crc32;
			String Path;
			String FullPath;
			std::int64_t Size;

			RequiredAsset(AssetType type, StringView path, StringView fullPath, std::int64_t size, std::uint32_t crc32)
				: Type(type), Crc32(crc32), FullPath(fullPath), Path(path), Size(size) {}
		};

		enum class VoteType : std::uint8_t {
			None,
			Restart,
			ResetPoints,
			Skip,
			Kick
		};
#endif

		//static constexpr float UpdatesPerSecond = 16.0f; // ~62 ms interval
		static constexpr float UpdatesPerSecond = 30.0f; // ~33 ms interval
		static constexpr std::int64_t ServerDelay = 64;
		static constexpr float EndingDuration = 10 * FrameTimer::FramesPerSecond;
		static constexpr float TeamSwitchCooldownFrames = 5.0f * FrameTimer::FramesPerSecond;
		static constexpr float CtfTouchRadius = 40.0f;	// Pixel radius for picking up / returning / capturing flags

		NetworkManager* _networkManager;
		std::unique_ptr<IGameMode> _gameMode;
		float _updateTimeLeft;
		float _gameTimeLeft;
		LevelState _levelState;
		bool _isServer;
		bool _forceResyncPending;
		bool _enableSpawning;
		bool _enqueuedPlaylistChange; // Server: apply the next playlist entry once the end-of-level transition finishes
		HashMap<std::uint32_t, std::shared_ptr<Actors::ActorBase>> _remoteActors; // Client: Actor ID -> Remote Actor created by server
		HashMap<Actors::ActorBase*, RemotingActorInfo> _remotingActors; // Server: Local Actor created by server -> Info
		HashMap<std::uint32_t, PlayerName> _playerNames; // Client: Actor ID -> Player name (and flags)
		SmallVector<PlayerPositionInRound, 0> _positionsInRound; // Client: Actor ID -> Position In Round
		SmallVector<std::uint32_t, 0> _teamScores;	// Server: computed each check; Client: mirrored for the HUD (index = team id)
		SmallVector<CtfFlag, 0> _ctfFlags;			// Server: one flag per team in Capture The Flag
		std::uint32_t _ctfCaptures[MaxTeamCount];	// Server: per-team capture count (used as the team score in CTF)
		SmallVector<CtfClientFlag, 0> _ctfFlagStates; // Server + Client: per-team flag info for the HUD and carried-flag attachment
		SmallVector<PlayerScore, 0> _scoreboard;	// Server: built periodically; Client: mirrored for the scoreboard
		float _scoreboardSyncTime;					// Server: countdown until the next scoreboard broadcast
		SmallVector<MultiplayerSpawnPoint, 0> _multiplayerSpawnPoints;
		SmallVector<Vector2i, 0> _raceCheckpoints;					// Unordered, used for race position ranking
		SmallVector<RaceCheckpoint, 0> _orderedRaceCheckpoints;		// Ordered polyline for the minimap (server-built, synced to clients)
		SmallVector<Vector2i, 0> _raceStartMarkers;					// Warp "Set Lap" tiles shown as start/finish on the minimap (synced to clients)
		Vector2i _raceBoundsMin;									// Minimap extent in tiles (covers the whole area players can reach; synced to clients)
		Vector2i _raceBoundsMax;
		bool _raceCheckpointsOrdered;								// Whether _orderedRaceCheckpoints come from authored waypoints (trusted for progress-based ranking)
		SmallVector<PendingSfx, 0> _pendingSfx;
		std::uint32_t _lastSpawnedActorId;	// Server: last assigned actor/player ID, Client: ID assigned by server
		std::int32_t _waitingForPlayerCount;	// Client: number of players needed to start the game
		std::uint32_t _lastUpdated; // Server/Client: last update from the server
		std::uint64_t _seqNumWarped; // Client: set to _seqNum from HandlePlayerWarped() when warped
		Threading::Spinlock _lock;
		bool _suppressRemoting; // Server: if true, actor will not be automatically remoted to other players
		bool _ignorePackets;
		bool _enableLedgeClimb;
		bool _controllableExternal;
		bool _autoWeightTreasure;
		VoteType _activePoll;
		float _activePollTimeLeft;
		float _recalcPositionInRoundTime;
		float _overtimeTimeLeft;
		bool _overtimeStarted;
		std::uint32_t _overtimeFinishers;
		std::int32_t _limitCameraLeft;
		std::int32_t _limitCameraWidth;
		Vector2f _lastCheckpointPos;
		float _lastCheckpointLight;
		std::int32_t _totalTreasureCount;

		SmallVector<RequiredAsset, 0> _requiredAssets;

#if defined(DEATH_DEBUG)
		std::int32_t _debugAverageUpdatePacketSize;
#endif

		std::unique_ptr<UI::Multiplayer::MpInGameCanvasLayer> _inGameCanvasLayer;
		std::unique_ptr<UI::Multiplayer::MpInGameLobby> _inGameLobby;
		bool _changingCharacterInLobby; // Client: lobby was opened to re-pick a character mid-game (not initial join)

		void InitializeRequiredAssets();
		void SynchronizePeers(float timeMult);
		std::uint32_t FindFreeActorId();
		std::uint8_t FindFreePlayerId();
		std::int32_t GetNonSpectatePlayerCount();
		bool IsLocalPlayer(Actors::ActorBase* actor);
		void ApplyGameModeToAllPlayers(MpGameMode gameMode);
		void ApplyGameModeToPlayer(MpGameMode gameMode, Actors::Player* player);
		std::uint8_t GetTeamCount() const override;
		std::uint8_t FindSmallestTeam(Actors::Multiplayer::MpPlayer* exclude);
		std::uint8_t ResolveTeam(Actors::Multiplayer::MpPlayer* player, std::uint8_t requested);
		bool ChangePlayerTeam(Actors::Multiplayer::MpPlayer* player, std::uint8_t requestedTeam, bool fromAdmin);
		void RebalanceTeams(bool force);
		void BroadcastPlayerTeam(Actors::Multiplayer::MpPlayer* player);
		std::uint32_t ColorizeFurForTeam(std::uint32_t furColor, std::uint8_t team) const;
		void RecolorRemoteActor(std::uint32_t actorId, std::uint32_t furColor, std::uint8_t team);
		std::uint32_t GetTeamScore(std::uint8_t team) override;
		void SyncTeamScores();

		void BuildCtfBases();
		void UpdateCtf(float timeMult);
		void UpdateCtfClient();
		void DropCtfFlag(Actors::Player* player);
		void BuildScoreboard();
		void ShowAlertToAllPlayers(StringView text);
		void SetControllableToAllPlayers(bool enable);
		void SendLevelStateToAllPlayers();
		void ResetAllPlayerStats();
		Vector2f GetSpawnPoint(PlayerType playerType, std::uint8_t team = 0);
		void BuildRaceCheckpoints();
		void ConsolidateRaceCheckpoints();
		void ConsolidateOrderedRaceCheckpoints();
		void GenerateRaceCheckpointsFromGeometry();
		void WarpAllPlayersToStart();
		void RollbackLevelState();
		void CalculatePositionInRound(bool forceSend = false);
		void CheckGameEnds();
		void BeginOvertime(Actors::Multiplayer::MpPlayer* winner);
		void EndGame(Actors::Multiplayer::MpPlayer* winner);
		void EndGameWithTeam(std::uint8_t team) override;
		void EndGameOnTimeOut();

		bool ApplyFromPlaylist();
		void RestartPlaylist();
		void SkipInPlaylist();
		void ResetPeerPoints();
		void SetWelcomeMessage(StringView message);
		void SetPlayerReady(PlayerType playerType, std::uint8_t team);
		void BroadcastLocalPlayerIdle(bool isIdle);

		void EndActivePoll();

		static bool ActorShouldBeMirrored(Actors::ActorBase* actor);
		static std::int32_t GetTreasureWeight(std::uint8_t gemType);
		static bool PlayerShouldHaveUnlimitedHealth(MpGameMode gameMode);
		void InitializeValidateAssetsPacket(MemoryStream& packet);
		void InitializeLoadLevelPacket(MemoryStream& packet);
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