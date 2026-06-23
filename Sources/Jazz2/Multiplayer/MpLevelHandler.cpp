#include "MpLevelHandler.h"

#if defined(WITH_MULTIPLAYER)

#include "PacketTypes.h"
#include "../ContentResolver.h"
#include "../PreferencesCache.h"
#include "../UI/InGameConsole.h"
#include "../UI/Multiplayer/MpHUD.h"
#include "../UI/Multiplayer/MpInGameCanvasLayer.h"
#include "../UI/Multiplayer/MpInGameLobby.h"

#if defined(WITH_ANGELSCRIPT)
#	include "../Scripting/LevelScriptLoader.h"
#endif

#include "../../nCine/Application.h"
#include "../../nCine/I18n.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Primitives/Half.h"

#include "../Actors/Player.h"
#include "../Actors/Multiplayer/LocalPlayerOnServer.h"
#include "../Actors/Multiplayer/RemotablePlayer.h"
#include "../Actors/Multiplayer/RemotePlayerOnServer.h"
#include "../Actors/Multiplayer/RemoteActor.h"
#include "../Actors/Multiplayer/Flag.h"
#include "../Actors/Multiplayer/CtfBase.h"

#include "../Actors/Enemies/Bosses/BossBase.h"
#include "../Actors/Environment/AirboardGenerator.h"
#include "../Actors/Environment/SteamNote.h"
#include "../Actors/Environment/SwingingVine.h"
#include "../Actors/Solid/Bridge.h"
#include "../Actors/Solid/MovingPlatform.h"
#include "../Actors/Solid/PinballBumper.h"
#include "../Actors/Solid/PinballPaddle.h"
#include "../Actors/Solid/Pole.h"
#include "../Actors/Solid/SpikeBall.h"
#include "../Actors/Weapons/ElectroShot.h"
#include "../Actors/Weapons/ShotBase.h"
#include "../Actors/Weapons/TNT.h"

#include <float.h>
#include <queue>

#include <Containers/DateTime.h>
#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>
#include <Utf8.h>

using namespace Death::IO::Compression;
using namespace nCine;
using namespace Jazz2::Actors::Multiplayer;

namespace Jazz2::Multiplayer
{
	// The first 15 positions are awarded with points
	static const std::uint32_t PointsPerPosition[] = {
		20, 17, 15, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
	};

	static void WriteCarryOver(MemoryStream& packet, const PlayerCarryOver& c)
	{
		packet.WriteValue<std::uint8_t>((std::uint8_t)c.CurrentWeapon);
		packet.WriteValue<std::uint8_t>(c.Lives);
		packet.WriteValue<std::uint8_t>(c.FoodEaten);
		packet.WriteVariableInt32(c.Score);
		for (std::int32_t i = 0; i < 4; i++) {
			packet.WriteVariableInt32(c.Gems[i]);
		}
		for (std::int32_t i = 0; i < PlayerCarryOver::WeaponCount; i++) {
			packet.WriteValue<std::uint16_t>(c.Ammo[i]);
		}
		for (std::int32_t i = 0; i < PlayerCarryOver::WeaponCount; i++) {
			packet.WriteValue<std::uint8_t>(c.WeaponUpgrades[i]);
		}
	}

	static PlayerCarryOver ReadCarryOver(MemoryStream& packet, PlayerType type)
	{
		PlayerCarryOver c;
		c.Type = type;
		c.CurrentWeapon = (WeaponType)packet.ReadValue<std::uint8_t>();
		c.Lives = packet.ReadValue<std::uint8_t>();
		c.FoodEaten = packet.ReadValue<std::uint8_t>();
		c.Score = packet.ReadVariableInt32();
		for (std::int32_t i = 0; i < 4; i++) {
			c.Gems[i] = packet.ReadVariableInt32();
		}
		for (std::int32_t i = 0; i < PlayerCarryOver::WeaponCount; i++) {
			c.Ammo[i] = packet.ReadValue<std::uint16_t>();
		}
		for (std::int32_t i = 0; i < PlayerCarryOver::WeaponCount; i++) {
			c.WeaponUpgrades[i] = packet.ReadValue<std::uint8_t>();
		}
		return c;
	}

	struct TileCoordHash {
#if defined(DEATH_TARGET_32BIT)
		using IntHash = xxHash32Func<std::int32_t>;
#else
		using IntHash = xxHash64Func<std::int32_t>;
#endif

		std::size_t operator()(const Vector2i& coord) const {
			return IntHash()(coord.X) ^ (IntHash()(coord.Y) << 1);
		}
	};


	// TODO: levelState is unused, it needs to be set after LevelState::InitialUpdatePending is processed
	MpLevelHandler::MpLevelHandler(IRootController* root, NetworkManager* networkManager, MpLevelHandler::LevelState levelState, bool enableLedgeClimb)
		: LevelHandler(root), _networkManager(networkManager), _updateTimeLeft(1.0f), _gameTimeLeft(0.0f),
			_levelState(LevelState::InitialUpdatePending), _forceResyncPending(true), _enableSpawning(true), _enqueuedPlaylistChange(false), _lastSpawnedActorId(-1), _waitingForPlayerCount(0),
			_lastUpdated(0), _seqNumWarped(0), _suppressRemoting(false), _ignorePackets(false), _changingCharacterInLobby(false), _enableLedgeClimb(enableLedgeClimb),
			_controllableExternal(true), _autoWeightTreasure(false), _activePoll(VoteType::None), _activePollTimeLeft(0.0f), _recalcPositionInRoundTime(0.0f),
			_overtimeTimeLeft(0.0f), _overtimeStarted(false), _overtimeFinishers(0),
			_limitCameraLeft(0), _limitCameraWidth(0), _totalTreasureCount(0), _raceCheckpointsOrdered(false), _ctfCaptures{}, _scoreboardSyncTime(0.0f)
#if defined(DEATH_DEBUG)
			, _debugAverageUpdatePacketSize(0)
#endif
#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
			, _plotIndex(0), _actorsMaxCount(0.0f), _actorsCount{}, _remoteActorsCount{}, _remotingActorsCount{},
			_mirroredActorsCount{}, _updatePacketMaxSize(0.0f), _updatePacketSize{}, _compressedUpdatePacketSize{}
#endif
	{
		NetworkState state = networkManager->GetState();
		_isServer = (state == NetworkState::Listening || state == NetworkState::Local);
	}

	MpLevelHandler::~MpLevelHandler()
	{
	}

	bool MpLevelHandler::Initialize(const LevelInitialization& levelInit)
	{
		// Online sessions are never flagged as local; local splitscreen multiplayer is the only local session here
		DEATH_DEBUG_ASSERT(!levelInit.IsLocalSession || IsLocalSession());

		_suppressRemoting = true;
		bool initialized = LevelHandler::Initialize(levelInit);
		_suppressRemoting = false;
		if (!initialized) {
			return false;
		}

		InitializeRequiredAssets();

		if (_isServer) {
			// Reserve first 255 indices for players
			auto& serverConfig = _networkManager->GetServerConfiguration();
			_lastSpawnedActorId = UINT8_MAX;
			_autoWeightTreasure = (serverConfig.TotalTreasureCollected == 0);

			if (!IsLocalSession()) {
				// Local splitscreen players are spawned directly in SpawnPlayers() and have no asset-validation
				// handshake, so this remote-peer reset must not run (it would clear their just-set level state)
				for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
					peerDesc->LevelState = PeerLevelState::ValidatingAssets;
					peerDesc->LastUpdated = 0;
					if (peerDesc->RemotePeer) {
						peerDesc->Player = nullptr;
					}
				}
			}
		}

		auto& resolver = ContentResolver::Get();
		resolver.PreloadMetadataAsync("Interactive/PlayerJazz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerSpaz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerLori"_s);

		if (!ContentResolver::Get().IsHeadless()) {
			_inGameCanvasLayer = std::make_unique<UI::Multiplayer::MpInGameCanvasLayer>(this);
			_inGameCanvasLayer->setParent(_rootNode.get());
			_inGameLobby = std::make_unique<UI::Multiplayer::MpInGameLobby>(this);
		}

		return true;
	}

	bool MpLevelHandler::Initialize(Stream& src, std::uint16_t version)
	{
		// Restoring a resumable state - only local cooperative sessions are ever saved (see SerializeResumableToStream).
		// The base deserializes the level and players (the players are created as LocalPlayerOnServer via the overridden
		// CreateResumablePlayer); the multiplayer-specific setup is applied afterwards so the match runs like a fresh one.
		_suppressRemoting = true;
		bool initialized = LevelHandler::Initialize(src, version);
		_suppressRemoting = false;
		if (!initialized) {
			return false;
		}

		_lastSpawnedActorId = UINT8_MAX;

		auto& serverConfig = _networkManager->GetServerConfiguration();
		ApplyGameModeToAllPlayers(serverConfig.GameMode);

		if (!ContentResolver::Get().IsHeadless()) {
			_inGameCanvasLayer = std::make_unique<UI::Multiplayer::MpInGameCanvasLayer>(this);
			_inGameCanvasLayer->setParent(_rootNode.get());
			_inGameLobby = std::make_unique<UI::Multiplayer::MpInGameLobby>(this);
		}

		return true;
	}

	bool MpLevelHandler::IsLocalSession() const
	{
		return (_networkManager->GetState() == NetworkState::Local);
	}

	bool MpLevelHandler::IsServer() const
	{
		return _isServer;
	}

	std::uint32_t MpLevelHandler::GetPlayerFurColor(const Actors::Player* player, std::uint32_t furColor) const
	{
		if (auto* mpPlayer = runtime_cast<MpPlayer>(player)) {
			if (auto peerDesc = mpPlayer->GetPeerDescriptor()) {
				return ColorizeFurForTeam(furColor, peerDesc->Team);
			}
		}
		return furColor;
	}

	bool MpLevelHandler::IsPausable() const
	{
		// Online sessions can't pause, but local splitscreen multiplayer can (like single player / local co-op)
		return IsLocalSession();
	}

	bool MpLevelHandler::CanPlayersCollide() const
	{
		// Online sessions don't use the base Player-vs-Player bump path (which the local splitscreen handler enables);
		// player collisions are resolved server-side via PlayerOnServer and stacking via MpPlayer instead. Local
		// splitscreen has all players in the same simulation, so it uses the base bump path directly.
		return IsLocalSession();
	}

	bool MpLevelHandler::CanActivateSugarRush() const
	{
		const auto& serverConfig = _networkManager->GetServerConfiguration();
		return (serverConfig.GameMode == MpGameMode::Cooperation);
	}

	bool MpLevelHandler::CanEventDisappear(EventType eventType) const
	{
		if (eventType == EventType::Gem) {
			const auto& serverConfig = _networkManager->GetServerConfiguration();
			if (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt) {
				return false;
			}
		}

		return true;
	}

	float MpLevelHandler::GetHurtInvulnerableTime() const
	{
		const auto& serverConfig = _networkManager->GetServerConfiguration();
		return (serverConfig.GameMode == MpGameMode::Cooperation ? 180.0f : 80.0f);
	}

	float MpLevelHandler::GetDefaultAmbientLight() const
	{
		// TODO: Remove this override
		return LevelHandler::GetDefaultAmbientLight();
	}

	void MpLevelHandler::SetAmbientLight(Actors::Player* player, float value)
	{
		if (_isServer) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(player)) {
				// TODO: Send it to remote peer
				return;
			}
		}

		LevelHandler::SetAmbientLight(player, value);
	}

	void MpLevelHandler::OnBeginFrame()
	{
		LevelHandler::OnBeginFrame();

		if (_isServer) {
			// Send pending SFX
			for (const auto& sfx : _pendingSfx) {
				std::uint32_t actorId;
				{
					std::unique_lock lock(_lock);
					auto it = _remotingActors.find(sfx.Actor);
					if (it == _remotingActors.end()) {
						continue;
					}
					actorId = it->second.ActorID;
				}

				MemoryStream packet(12 + sfx.Identifier.size());
				packet.WriteVariableUint32(actorId);
				// TODO: sourceRelative
				// TODO: looping
				packet.WriteValue<std::uint16_t>(sfx.Gain);
				packet.WriteValue<std::uint16_t>(sfx.Pitch);
				packet.WriteVariableUint32((std::uint32_t)sfx.Identifier.size());
				packet.Write(sfx.Identifier.data(), (std::uint32_t)sfx.Identifier.size());

				_networkManager->SendTo([this](const Peer& peer) {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlaySfx, packet);
			}
			_pendingSfx.clear();
		} else {
			auto& input = _playerInputs[0];
			if (input.PressedActions != input.PressedActionsLast) {
				MemoryStream packet(12);
				packet.WriteVariableUint32(_lastSpawnedActorId);
				packet.WriteVariableUint64(_console->IsVisible() ? 0 : input.PressedActions);
				_networkManager->SendTo(AllPeers, NetworkChannel::UnreliableUpdates, (std::uint8_t)ClientPacketType::PlayerKeyPress, packet);
			}
		}
	}

	void MpLevelHandler::OnEndFrame()
	{
		LevelHandler::OnEndFrame();

		float timeMult = theApplication().GetTimeMult();
		std::uint32_t frameCount = theApplication().GetFrameCount();
		auto& serverConfig = _networkManager->GetServerConfiguration();

		if (_isServer) {
			// Update last pressed keys only if it wasn't done this frame yet (because of PlayerKeyPress packet)
			for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
				if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(peerDesc->Player)) {
					if (remotePlayerOnServer->UpdatedFrame != frameCount) {
						remotePlayerOnServer->UpdatedFrame = frameCount;
						remotePlayerOnServer->PressedKeysLast = remotePlayerOnServer->PressedKeys;
					}

					if (remotePlayerOnServer->PressedKeys == 0) {
						peerDesc->IdleElapsedFrames += timeMult;
						if (serverConfig.IdleKickTimeSecs > 0 && serverConfig.IdleKickTimeSecs <= (std::int32_t)(peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame)) {
							peerDesc->IdleElapsedFrames = 0.0f;
							_networkManager->Kick(peer, Reason::Idle);
						}
					} else {
						peerDesc->IdleElapsedFrames = 0.0f;
					}
				}
			}

			if DEATH_UNLIKELY(_activePoll != VoteType::None) {
				_activePollTimeLeft -= timeMult;
				if (_activePollTimeLeft <= 0.0f) {
					EndActivePoll();
				}
			}

			if DEATH_UNLIKELY(_overtimeStarted) {
				_overtimeTimeLeft -= timeMult;
				if (_overtimeTimeLeft <= 0.0f) {
					CheckGameEnds();
				}
			}

			// Periodically broadcast the scoreboard (player stats + ping) so the in-game scoreboard stays current,
			// and the team/flag state so late joiners and clients can place the CTF bases/flags
			_scoreboardSyncTime -= timeMult;
			if (_scoreboardSyncTime <= 0.0f) {
				_scoreboardSyncTime = FrameTimer::FramesPerSecond;
				BuildScoreboard();
				if (IsTeamGameMode(serverConfig.GameMode)) {
					SyncTeamScores();
				}
			}
		} else {
			// Keep carried flags glued to their carrier using the carrier's already-interpolated position, so the
			// flag moves in lockstep with the player instead of being interpolated independently (which looked choppy)
			UpdateCtfClient();
		}

		_updateTimeLeft -= timeMult;
		_gameTimeLeft -= timeMult;

		if (_updateTimeLeft < 0.0f) {
			_updateTimeLeft = FrameTimer::FramesPerSecond / UpdatesPerSecond;

			switch (_levelState) {
				case LevelState::InitialUpdatePending: {
					LOGI("Level \"{}\" is ready", _levelName);

					if (_isServer) {
						MemoryStream packet;
						InitializeValidateAssetsPacket(packet);
						_networkManager->SendTo([this](const Peer& peer) {
							auto peerDesc = _networkManager->GetPeerDescriptor(peer);
							return (peerDesc && peerDesc->IsAuthenticated);
						}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ValidateAssets, packet);

						if (serverConfig.GameMode == MpGameMode::Cooperation) {
							// Skip pre-game and countdown in cooperation
							_levelState = LevelState::Running;
						} else {
							_levelState = LevelState::PreGame;
							_gameTimeLeft = serverConfig.PreGameSecs * FrameTimer::FramesPerSecond;
							ShowAlertToAllPlayers(_("\n\nThe game will begin shortly!"));
						}
					} else {
						_levelState = LevelState::Running;

						std::uint8_t flags = 0;
						if (PreferencesCache::EnableLedgeClimb) {
							flags |= 0x02;
						}

						MemoryStream packet(1);
						packet.WriteValue<std::uint8_t>(flags);
						_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::LevelReady, packet);
					}
					break;
				}
				case LevelState::PreGame: {
					if (_isServer && _gameTimeLeft <= 0.0f) {
						// TODO: Check all players are ready
						std::int32_t playerCount = GetNonSpectatePlayerCount();
						if (playerCount >= (std::int32_t)serverConfig.MinPlayerCount) {
							_levelState = LevelState::Countdown3;
							_gameTimeLeft = FrameTimer::FramesPerSecond;
							SetControllableToAllPlayers(false);
							WarpAllPlayersToStart();
							SynchronizeGameMode();
							ResetAllPlayerStats();
							static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(3);
							LOGI("Starting round...");
						} else {
							_levelState = LevelState::WaitingForMinPlayers;
							_waitingForPlayerCount = (std::int32_t)serverConfig.MinPlayerCount - playerCount;
						}
						SendLevelStateToAllPlayers();
					}
					break;
				}
				case LevelState::WaitingForMinPlayers: {
					if (_isServer) {
						// TODO: Check all players are ready
						if (_waitingForPlayerCount <= 0) {
							_levelState = LevelState::Countdown3;
							_gameTimeLeft = FrameTimer::FramesPerSecond;
							SetControllableToAllPlayers(false);
							WarpAllPlayersToStart();
							SynchronizeGameMode();
							ResetAllPlayerStats();
							static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(3);
							SendLevelStateToAllPlayers();
							LOGI("Starting round...");
						}
					}
					break;
				}
				case LevelState::Countdown3: {
					if (_isServer && _gameTimeLeft <= 0.0f) {
						_levelState = LevelState::Countdown2;
						_gameTimeLeft = FrameTimer::FramesPerSecond;
						static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(2);
						SendLevelStateToAllPlayers();
						RollbackLevelState();
					}
					break;
				}
				case LevelState::Countdown2: {
					if (_isServer && _gameTimeLeft <= 0.0f) {
						_levelState = LevelState::Countdown1;
						_gameTimeLeft = FrameTimer::FramesPerSecond;
						static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(1);
						SendLevelStateToAllPlayers();
					}
					break;
				}
				case LevelState::Countdown1: {
					if (_isServer && _gameTimeLeft <= 0.0f) {
						_levelState = LevelState::Running;
						_gameTimeLeft = serverConfig.MaxGameTimeSecs * FrameTimer::FramesPerSecond;
						_recalcPositionInRoundTime = FrameTimer::FramesPerSecond;

						SetControllableToAllPlayers(true);
						static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(0);
						SendLevelStateToAllPlayers();
						CalculatePositionInRound(true);

						for (auto* player : _players) {
							auto* mpPlayer = static_cast<MpPlayer*>(player);
							auto peerDesc = mpPlayer->GetPeerDescriptor();
							mpPlayer->GetPeerDescriptor()->LapStarted = TimeStamp::now();

							// The player is invulnerable for a short time after starting a round
							player->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Blinking);

							if (peerDesc->RemotePeer) {
								MemoryStream packet(13);
								packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Laps);
								packet.WriteVariableUint32(mpPlayer->_playerIndex);
								packet.WriteVariableUint32(peerDesc->Laps);
								packet.WriteVariableUint32(serverConfig.TotalLaps);
								_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
							}
						}
					}
					break;
				}
				case LevelState::Running: {
					if (_isServer) {
						if (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::TeamRace) {
							_recalcPositionInRoundTime -= timeMult;
							if (_recalcPositionInRoundTime <= 0.0f) {
								_recalcPositionInRoundTime = FrameTimer::FramesPerSecond;
								CalculatePositionInRound();
							}
						}
						if (serverConfig.GameMode == MpGameMode::CaptureTheFlag) {
							UpdateCtf(timeMult);
						}
						if (serverConfig.GameMode != MpGameMode::Cooperation && serverConfig.MaxGameTimeSecs > 0 && _gameTimeLeft <= 0.0f) {
							EndGameOnTimeOut();
						}
					}
					break;
				}
				case LevelState::Ending: {
					if (_isServer && _gameTimeLeft <= 0.0f) {
						if (serverConfig.Playlist.size() > 1) {
							serverConfig.PlaylistIndex++;
							if (serverConfig.PlaylistIndex >= serverConfig.Playlist.size()) {
								serverConfig.PlaylistIndex = 0;
								if (serverConfig.RandomizePlaylist) {
									Random().Shuffle<PlaylistEntry>(serverConfig.Playlist);
								}
							}
							ApplyFromPlaylist();
						} else {
							// TODO: Reload the level
							LevelInitialization levelInit;
							PrepareNextLevelInitialization(levelInit);
							levelInit.LevelName = _levelName;
							levelInit.LastExitType = ExitType::Normal;
							HandleLevelChange(std::move(levelInit));
						}
					}
					break;
				}
			}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
			_plotIndex = (_plotIndex + 1) % PlotValueCount;

			_actorsCount[_plotIndex] = _actors.size();
			_actorsMaxCount = std::max(_actorsMaxCount, _actorsCount[_plotIndex]);
			_remoteActorsCount[_plotIndex] = 0;
			_mirroredActorsCount[_plotIndex] = 0;
			for (auto& actor : _remoteActors) {
				if (actor.second == nullptr) {
					continue;
				}

				bool isMirrored = (_isServer
					? ActorShouldBeMirrored(actor.second.get())
					: !runtime_cast<Actors::Multiplayer::RemoteActor>(actor.second));

				if (isMirrored) {
					_mirroredActorsCount[_plotIndex]++;
				} else {
					_remoteActorsCount[_plotIndex]++;
				}
			}
			_remotingActorsCount[_plotIndex] = _remotingActors.size();
#endif

			if (_isServer) {
				if (_networkManager->HasInboundConnections()) {
					std::uint32_t playerCount = GetNonSpectatePlayerCount();
					std::uint32_t actorCount = playerCount + (std::uint32_t)_remotingActors.size();

					MemoryStream packet(8 + actorCount * 24);
					packet.WriteVariableUint32(_lastUpdated);
					packet.WriteVariableUint64((std::uint64_t)_elapsedFrames);
					packet.WriteVariableUint32((actorCount << 1) | (_forceResyncPending ? 1 : 0));

					for (Actors::Player* player : _players) {
						auto* mpPlayer = static_cast<PlayerOnServer*>(player);

						// Skip spectate players - don't send their position to other clients
						if (mpPlayer->_playerType == PlayerType::Spectate) {
							continue;
						}

						/*Vector2f pos;
						auto it = _playerStates.find(player->_playerIndex);
						if (it != _playerStates.end()) {
							// Remote players
							pos = player->_pos;

							// TODO: This should be WarpUpdatesLeft
							if (it->second.WarpTimeLeft > 0.0f) {
								it->second.WarpTimeLeft -= timeMult;
								if (it->second.WarpTimeLeft <= 0.0f) {
									it->second.WarpTimeLeft = 0.0f;
									LOGW("Player warped without permission (possible cheating attempt)");
								}
							} else if (it->second.WarpTimeLeft < 0.0f) {
								it->second.WarpTimeLeft += timeMult;
								if (it->second.WarpTimeLeft >= 0.0f) {
									it->second.WarpTimeLeft = 0.0f;
									LOGW("Player failed to warp in time (possible cheating attempt)");
								}
							}
						} else {
							// Local players
							pos = player->_pos;
						}*/
						Vector2f pos = player->_pos;

						packet.WriteVariableUint32(player->_playerIndex);

						std::uint8_t flags = 0x01 | 0x02; // PositionChanged | AnimationChanged
						if (player->_renderer.isDrawEnabled()) {
							flags |= 0x04;
						}
						if (player->_renderer.AnimPaused) {
							flags |= 0x08;
						}
						if (player->_renderer.isFlippedX()) {
							flags |= 0x10;
						}
						if (player->_renderer.isFlippedY()) {
							flags |= 0x20;
						}
						if (mpPlayer->_justWarped) {
							mpPlayer->_justWarped = false;
							flags |= 0x40;
						}
						packet.WriteValue<std::uint8_t>(flags);

						packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 512.0f));
						packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 512.0f));
						packet.WriteVariableUint32((std::uint32_t)(player->_currentTransition != nullptr ? player->_currentTransition->State : player->_currentAnimation->State));

						float rotation = player->_renderer.rotation();
						if (rotation < 0.0f) rotation += fRadAngle360;
						packet.WriteValue<std::uint16_t>((std::uint16_t)(rotation * UINT16_MAX / fRadAngle360));
						Vector2f scale = player->_renderer.scale();
						packet.WriteValue<std::uint16_t>((std::uint16_t)Half{scale.X});
						packet.WriteValue<std::uint16_t>((std::uint16_t)Half{scale.Y});
						Actors::ActorRendererType rendererType = player->_renderer.GetRendererType();
						if (rendererType == Actors::ActorRendererType::Outline) {
							// Outline renderer type is local-only
							rendererType = Actors::ActorRendererType::Default;
						}
						packet.WriteValue<std::uint8_t>((std::uint8_t)rendererType);
					}

					// TODO: Does this need to be locked?
					{
						std::unique_lock lock(_lock);
						for (auto& [remotingActor, remotingActorInfo] : _remotingActors) {
							packet.WriteVariableUint32(remotingActorInfo.ActorID);

							std::int32_t newPosX = (std::int32_t)(remotingActor->_pos.X * 512.0f);
							std::int32_t newPosY = (std::int32_t)(remotingActor->_pos.Y * 512.0f);
							bool positionChanged = (_forceResyncPending || newPosX != remotingActorInfo.LastPosX || newPosY != remotingActorInfo.LastPosY);

							std::uint32_t newAnimation = (std::uint32_t)(remotingActor->_currentTransition != nullptr ? remotingActor->_currentTransition->State : (remotingActor->_currentAnimation != nullptr ? remotingActor->_currentAnimation->State : AnimState::Idle));
							float rotation = remotingActor->_renderer.rotation();
							if (rotation < 0.0f) rotation += fRadAngle360;
							std::uint16_t newRotation = (std::uint16_t)(rotation * UINT16_MAX / fRadAngle360);
							Vector2f newScale = remotingActor->_renderer.scale();
							std::uint16_t newScaleX = (std::uint16_t)Half{newScale.X};
							std::uint16_t newScaleY = (std::uint16_t)Half{newScale.Y};
							std::uint8_t newRendererType = (std::uint8_t)remotingActor->_renderer.GetRendererType();
							bool animationChanged = (_forceResyncPending || newAnimation != remotingActorInfo.LastAnimation || newRotation != remotingActorInfo.LastRotation ||
								newScaleX != remotingActorInfo.LastScaleX || newScaleY != remotingActorInfo.LastScaleY || newRendererType != remotingActorInfo.LastRendererType);

							std::uint8_t flags = 0;
							if (positionChanged) {
								flags |= 0x01;
							}
							if (animationChanged) {
								flags |= 0x02;
							}
							if (remotingActor->_renderer.isDrawEnabled()) {
								flags |= 0x04;
							}
							if (remotingActor->_renderer.AnimPaused) {
								flags |= 0x08;
							}
							if (remotingActor->_renderer.isFlippedX()) {
								flags |= 0x10;
							}
							if (remotingActor->_renderer.isFlippedY()) {
								flags |= 0x20;
							}
							packet.WriteValue<std::uint8_t>(flags);

							if (positionChanged) {
								packet.WriteValue<std::int32_t>(newPosX);
								packet.WriteValue<std::int32_t>(newPosY);

								remotingActorInfo.LastPosX = newPosX;
								remotingActorInfo.LastPosY = newPosY;
							}
							if (animationChanged) {
								packet.WriteVariableUint32(newAnimation);
								packet.WriteValue<std::uint16_t>(newRotation);
								packet.WriteValue<std::uint16_t>(newScaleX);
								packet.WriteValue<std::uint16_t>(newScaleY);
								packet.WriteValue<std::uint8_t>(newRendererType);

								remotingActorInfo.LastAnimation = newAnimation;
								remotingActorInfo.LastRotation = newRotation;
								remotingActorInfo.LastScaleX = newScaleX;
								remotingActorInfo.LastScaleY = newScaleY;
								remotingActorInfo.LastRendererType = newRendererType;
							}
						}
					}

					MemoryStream packetCompressed(1024);
					{
						DeflateWriter dw(packetCompressed);
						dw.Write(packet.GetBuffer(), packet.GetSize());
					}

#if defined(DEATH_DEBUG)
					_debugAverageUpdatePacketSize = lerp(_debugAverageUpdatePacketSize, (std::int32_t)(packet.GetSize() * UpdatesPerSecond), 0.04f * timeMult);
#endif
#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
					_updatePacketSize[_plotIndex] = packet.GetSize();
					_updatePacketMaxSize = std::max(_updatePacketMaxSize, _updatePacketSize[_plotIndex]);
					_compressedUpdatePacketSize[_plotIndex] = packetCompressed.GetSize();
#endif

					_networkManager->SendTo([this](const Peer& peer) {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
					}, _forceResyncPending ? NetworkChannel::Main : NetworkChannel::UnreliableUpdates, (std::uint8_t)ServerPacketType::UpdateAllActors, packetCompressed);

					_lastUpdated++;
					_forceResyncPending = false;

					SynchronizePeers(timeMult);
				} else {
#if defined(DEATH_DEBUG)
					_debugAverageUpdatePacketSize = 0;
#endif
				}
			} else {
				if (!_players.empty()) {
					Clock& c = nCine::clock();
					std::uint64_t now = c.now() * 1000 / c.frequency();
					auto player = _players[0];

					RemotePlayerOnServer::PlayerFlags flags = (RemotePlayerOnServer::PlayerFlags)player->_currentSpecialMove;
					if (player->IsFacingLeft()) {
						flags |= RemotePlayerOnServer::PlayerFlags::IsFacingLeft;
					}
					if (player->_renderer.isDrawEnabled()) {
						flags |= RemotePlayerOnServer::PlayerFlags::IsVisible;
					}
					if (player->_isActivelyPushing) {
						flags |= RemotePlayerOnServer::PlayerFlags::IsActivelyPushing;
					}
					if (_seqNumWarped != 0) {
						flags |= RemotePlayerOnServer::PlayerFlags::JustWarped;
					}
					if (PreferencesCache::EnableContinuousJump) {
						flags |= RemotePlayerOnServer::PlayerFlags::EnableContinuousJump;
					}

					if DEATH_UNLIKELY(_pauseMenu != nullptr) {
						flags |= RemotePlayerOnServer::PlayerFlags::InMenu;
					}
					if DEATH_UNLIKELY(_console->IsVisible()) {
						flags |= RemotePlayerOnServer::PlayerFlags::InConsole;
					}

					MemoryStream packet(20);
					packet.WriteVariableUint32(_lastSpawnedActorId);
					packet.WriteVariableUint64(now);
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.Y * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
					packet.WriteVariableUint32((std::uint32_t)flags);

					if (_seqNumWarped != 0) {
						packet.WriteVariableUint64(_seqNumWarped);
					}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
					_updatePacketSize[_plotIndex] = packet.GetSize();
					_compressedUpdatePacketSize[_plotIndex] = _updatePacketSize[_plotIndex];
					_updatePacketMaxSize = std::max(_updatePacketMaxSize, _updatePacketSize[_plotIndex]);
#endif

					_networkManager->SendTo(AllPeers, NetworkChannel::UnreliableUpdates, (std::uint8_t)ClientPacketType::PlayerUpdate, packet);
				}
			}
		}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
		ShowDebugWindow();

		if (PreferencesCache::ShowPerformanceMetrics) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();

			/*if (_isServer) {
				for (Actors::Player* player : _players) {
					char actorIdString[32];

					Vector2f pos;
					auto it = _playerStates.find(player->_playerIndex);
					if (it != _playerStates.end()) {
						for (std::size_t i = 0; i < arraySize(it->second.StateBuffer); i++) {
							auto posFrom = WorldPosToScreenSpace(it->second.StateBuffer[i].Pos);
							auto posTo = WorldPosToScreenSpace(it->second.StateBuffer[i].Pos + it->second.StateBuffer[i].Speed);

							drawList->AddLine(posFrom, posTo, ImColor(255, 200, 120, 220), 2.0f);
						}

						std::int32_t prevIdx = it->second.StateBufferPos - 1;
						while (prevIdx < 0) {
							prevIdx += std::int32_t(arraySize(it->second.StateBuffer));
						}

						auto posPrev = WorldPosToScreenSpace(it->second.StateBuffer[prevIdx].Pos);
						auto posLocal = WorldPosToScreenSpace(player->_pos);
						drawList->AddLine(posPrev, posLocal, ImColor(255, 60, 60, 220), 2.0f);

						formatString(actorIdString, "%i [%0.1f] %04x", player->_playerIndex, it->second.DeviationTime, it->second.Flags);
					} else {
						formatString(actorIdString, "%i", player->_playerIndex);
					}

					auto aabbMin = WorldPosToScreenSpace({ player->AABB.L, player->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}

				for (const auto& [actor, actorId] : _remotingActors) {
					char actorIdString[16];
					formatString(actorIdString, "%u", actorId);

					auto aabbMin = WorldPosToScreenSpace({ actor->AABB.L, actor->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}
			} else {
				for (Actors::Player* player : _players) {
					char actorIdString[16];
					formatString(actorIdString, "%i", player->_playerIndex);

					auto aabbMin = WorldPosToScreenSpace({ player->AABB.L, player->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}

				for (const auto& [actorId, actor] : _remoteActors) {
					char actorIdString[16];
					formatString(actorIdString, "%u", actorId);

					auto aabbMin = WorldPosToScreenSpace({ actor->AABBInner.L, actor->AABBInner.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}
			}*/
		}
#endif

		if (_isServer && _players.empty()) {
			// If no players are connected, slow the server down to save resources
			Thread::Sleep(500);
		}
	}

	void MpLevelHandler::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		LevelHandler::OnInitializeViewport(width, height);

		if (_inGameLobby != nullptr) {
			_inGameLobby->setParent(_upscalePass.GetNode());
		}
	}

	bool MpLevelHandler::OnConsoleCommand(StringView line)
	{
		if (LevelHandler::OnConsoleCommand(line)) {
			return true;
		}

		if (_isServer) {
			if (line.hasPrefix('/')) {
				_console->WriteLine(UI::MessageLevel::Echo, line);
				return ProcessCommand({}, line, true);
			}

			auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer);
			String prefixedMessage = "\f[c:#907060]"_s + peerDesc->PlayerName + ":\f[/c] "_s + line;
			_console->WriteLine(UI::MessageLevel::Echo, prefixedMessage);

			// Chat message
			MemoryStream packet(9 + prefixedMessage.size());
			packet.WriteVariableUint32(0); // TODO: Player index
			packet.WriteValue<std::uint8_t>((std::uint8_t)UI::MessageLevel::Chat);
			packet.WriteVariableUint32((std::uint32_t)prefixedMessage.size());
			packet.Write(prefixedMessage.data(), (std::uint32_t)prefixedMessage.size());

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState != PeerLevelState::Unknown);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packet);
		} else {
			// Chat message
			MemoryStream packet(9 + line.size());
			packet.WriteVariableUint32(_lastSpawnedActorId);
			packet.WriteValue<std::uint8_t>(0); // Reserved
			packet.WriteVariableUint32((std::uint32_t)line.size());
			packet.Write(line.data(), (std::uint32_t)line.size());
			_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::ChatMessage, packet);
		}

		return true;
	}

	void MpLevelHandler::OnTouchEvent(const TouchEvent& event)
	{
		LevelHandler::OnTouchEvent(event);

		if (_inGameLobby != nullptr && _inGameLobby->IsVisible() && _pauseMenu == nullptr && !_console->IsVisible()) {
			_inGameLobby->OnTouchEvent(event);
		}
	}

	void MpLevelHandler::AddActor(std::shared_ptr<Actors::ActorBase> actor)
	{
		LevelHandler::AddActor(actor);

		if (!_suppressRemoting && _isServer) {
			Actors::ActorBase* actorPtr = actor.get();

			std::uint32_t actorId;
			{
				std::unique_lock lock(_lock);
				actorId = FindFreeActorId();
				_remotingActors[actorPtr] = { actorId };

				// Store only used IDs on server-side
				_remoteActors[actorId] = nullptr;
			}

			if (ActorShouldBeMirrored(actorPtr)) {
				Vector2i originTile = actorPtr->_originTile;
				const auto& eventTile = _eventMap->GetEventTile(originTile.X, originTile.Y);
				if (eventTile.Event != EventType::Empty) {
					MemoryStream packet(24 + Events::EventSpawner::SpawnParamsSize);
					packet.WriteVariableUint32(actorId);
					packet.WriteVariableUint32((std::uint32_t)eventTile.Event);
					packet.Write(eventTile.EventParams, Events::EventSpawner::SpawnParamsSize);
					packet.WriteVariableUint32((std::uint32_t)eventTile.EventFlags);
					packet.WriteVariableInt32((std::int32_t)originTile.X);
					packet.WriteVariableInt32((std::int32_t)originTile.Y);
					packet.WriteVariableInt32((std::int32_t)actorPtr->_renderer.layer());

					_networkManager->SendTo([this](const Peer& peer) {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateMirroredActor, packet);
				}
			} else {
				MemoryStream packet;
				InitializeCreateRemoteActorPacket(packet, actorId, actorPtr);

				_networkManager->SendTo([this](const Peer& peer) {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
			}
		}
	}

	std::shared_ptr<AudioBufferPlayer> MpLevelHandler::PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch)
	{
		Vector3f adjustedPos = pos;

		if (_isServer) {
			std::uint32_t actorId; bool excludeSelf;
			if (auto* player = runtime_cast<Actors::Player>(self)) {
				actorId = player->_playerIndex;
				excludeSelf = (!identifier.hasPrefix("EndOfLevel"_s) && !identifier.hasPrefix("Pickup"_s) && !identifier.hasPrefix("Weapon"_s));

				if (sourceRelative) {
					// Remote players don't have local viewport, so SFX cannot be relative to them
					if (auto* remotePlayer = runtime_cast<RemotePlayerOnServer>(self)) {
						sourceRelative = false;
						Vector2f pos = remotePlayer->GetPos();
						adjustedPos.X += pos.X;
						adjustedPos.Y += pos.Y;
					}
				}
			} else {
				std::unique_lock lock(_lock);
				auto it = _remotingActors.find(self);
				actorId = (it != _remotingActors.end() ? it->second.ActorID : UINT32_MAX);
				excludeSelf = false;
			}

			if (actorId != UINT32_MAX) {
				MemoryStream packet(12 + identifier.size());
				packet.WriteVariableUint32(actorId);
				// TODO: sourceRelative
				// TODO: looping
				packet.WriteValue<std::uint16_t>(floatToHalf(gain));
				packet.WriteValue<std::uint16_t>(floatToHalf(pitch));
				packet.WriteVariableUint32((std::uint32_t)identifier.size());
				packet.Write(identifier.data(), (std::uint32_t)identifier.size());

				Actors::ActorBase* excludedPlayer = (excludeSelf ? self : nullptr);
				_networkManager->SendTo([this, excludedPlayer](const Peer& peer) {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized && (excludedPlayer == nullptr || excludedPlayer != peerDesc->Player));
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlaySfx, packet);
			} else {
				// Actor is probably not fully created yet, try it later again
				_pendingSfx.emplace_back(self, identifier, floatToHalf(gain), floatToHalf(pitch));
			}
		}

		return LevelHandler::PlaySfx(self, identifier, buffer, adjustedPos, sourceRelative, gain, pitch);
	}

	std::shared_ptr<AudioBufferPlayer> MpLevelHandler::PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain, float pitch)
	{
		if (_isServer) {
			MemoryStream packet(16 + identifier.size());
			packet.WriteVariableInt32((std::int32_t)pos.X);
			packet.WriteVariableInt32((std::int32_t)pos.Y);
			// TODO: looping
			packet.WriteValue<std::uint16_t>(floatToHalf(gain));
			packet.WriteValue<std::uint16_t>(floatToHalf(pitch));
			packet.WriteVariableUint32((std::uint32_t)identifier.size());
			packet.Write(identifier.data(), (std::uint32_t)identifier.size());

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayCommonSfx, packet);
		}

		return LevelHandler::PlayCommonSfx(identifier, pos, gain, pitch);
	}

	void MpLevelHandler::WarpCameraToTarget(Actors::ActorBase* actor, bool fast)
	{
		LevelHandler::WarpCameraToTarget(actor, fast);
	}

	void MpLevelHandler::BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, std::uint8_t* eventParams)
	{
		LevelHandler::BroadcastTriggeredEvent(initiator, eventType, eventParams);
	}

	void MpLevelHandler::BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, StringView nextLevel)
	{
		if (!_isServer || _nextLevelType != ExitType::None || _levelState != LevelState::Running) {
			// Level can be changed only by server
			return;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();

		if DEATH_UNLIKELY(_gameMode == nullptr) {
			// No active game mode (MpGameMode::Unknown) - nothing to change
			return;
		}

		// The active game mode decides what reaching the level exit means; the host carries out that decision.
		switch (_gameMode->OnLevelExitReached(*this, static_cast<MpPlayer*>(initiator))) {
			case LevelExitAction::Ignore:
				// Competitive modes with no level-exit win condition (Battle / TeamBattle / Capture The Flag)
				return;
			case LevelExitAction::WarpToStartIncrementLap: {
				// Race / TeamRace: warp the finisher back to the start line and count a lap (unless already warping)
				auto* mpPlayer = static_cast<MpPlayer*>(initiator);
				if (mpPlayer->_currentTransition == nullptr ||
					(mpPlayer->_currentTransition->State != AnimState::TransitionWarpIn && mpPlayer->_currentTransition->State != AnimState::TransitionWarpOut &&
						mpPlayer->_currentTransition->State != AnimState::TransitionWarpInFreefall && mpPlayer->_currentTransition->State != AnimState::TransitionWarpOutFreefall)) {
					Vector2f spawnPosition = GetSpawnPoint(mpPlayer->_playerTypeOriginal, mpPlayer->GetPeerDescriptor()->Team);
					mpPlayer->WarpToPosition(spawnPosition, WarpFlags::IncrementLaps);
				}
				return;
			}
			case LevelExitAction::EndGameForInitiator:
				// Treasure Hunt / Team Treasure Hunt: the escaping player (with enough treasure) wins the round
				CalculatePositionInRound();
				EndGame(static_cast<MpPlayer*>(initiator));
				return;
			case LevelExitAction::ChangeToNextLevel:
				break;	// Cooperation: proceed to the shared level-change code below
		}

		if (nextLevel.empty()) {
			nextLevel = _defaultNextLevel;
		}

		LOGD("Changing level to \"{}\" (0x{:.2x})", nextLevel, exitType);

		if ((nextLevel == ":end"_s || nextLevel == ":credits"_s) && !serverConfig.Playlist.empty()) {
			serverConfig.PlaylistIndex++;
			if (serverConfig.PlaylistIndex >= serverConfig.Playlist.size()) {
				serverConfig.PlaylistIndex = 0;
				if (serverConfig.RandomizePlaylist) {
					Random().Shuffle<PlaylistEntry>(serverConfig.Playlist);
				}
			}
			// Don't switch immediately - play the warp-out transition first (like a normal level change) and
			// apply the playlist entry once it finishes (see HandleLevelChange)
			_enqueuedPlaylistChange = true;
		}

		LevelHandler::BeginLevelChange(initiator, exitType, nextLevel);

		if ((exitType & ExitType::FastTransition) != ExitType::FastTransition) {
			float fadeOutDelay = _nextLevelTime - 40.0f;

			MemoryStream packet(4);
			packet.WriteVariableInt32((std::int32_t)fadeOutDelay);

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelLoaded);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::FadeOut, packet);
		}
	}

	void MpLevelHandler::SendPacket(const Actors::ActorBase* self, ArrayView<const std::uint8_t> data)
	{
		if DEATH_LIKELY(_isServer) {
			std::uint32_t targetActorId = 0;
			{
				std::unique_lock lock(_lock);
				auto it = _remotingActors.find(const_cast<Actors::ActorBase*>(self));
				if (it != _remotingActors.end()) {
					targetActorId = it->second.ActorID;
				}
			}
			if (targetActorId != 0) {
				MemoryStream packet(4 + data.size());
				packet.WriteVariableUint32(targetActorId);
				packet.Write(data.data(), data.size());

				_networkManager->SendTo([this](const Peer& peer) {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelLoaded);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::Rpc, packet);
			} else {
				LOGW("Remote actor not found");
			}
		} else {
			std::uint32_t targetActorId = 0;
			{
				std::unique_lock lock(_lock);
				for (const auto& [actorId, actor] : _remoteActors) {
					if (actor.get() == self) {
						targetActorId = actorId;
						break;
					}
				}
			}
			if (targetActorId != 0) {
				MemoryStream packet(4 + data.size());
				packet.WriteVariableUint32(targetActorId);
				packet.Write(data.data(), data.size());

				_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::Rpc, packet);
			} else {
				LOGW("Remote actor not found");
			}
		}
	}

	void MpLevelHandler::HandleBossActivated(Actors::Bosses::BossBase* boss, Actors::ActorBase* initiator)
	{
		constexpr float MinDistance = 128.0f;

		if (initiator == nullptr) {
			return;
		}

		Vector2f pos = initiator->_pos;
		for (auto* player : _players) {
			if (player == initiator || (player->_pos - pos).Length() < MinDistance) {
				continue;
			}

			player->WarpToPosition(pos, WarpFlags::Default);

			// TODO: Deactivate sugar rush
		}
	}

	void MpLevelHandler::HandleLevelChange(LevelInitialization&& levelInit)
	{
		// End-of-episode playlist change was deferred until the warp-out transition finished (see BeginLevelChange).
		// ApplyFromPlaylist() builds its own initialization and re-enters this method with the flag cleared.
		if (_enqueuedPlaylistChange) {
			_enqueuedPlaylistChange = false;
			if (ApplyFromPlaylist()) {
				return;
			}
		}

		levelInit.IsLocalSession = false;
		LevelHandler::HandleLevelChange(std::move(levelInit));
	}

	void MpLevelHandler::HandleGameOver(Actors::Player* player)
	{
		// In a local cooperation session the lives system is active (exactly like classic splitscreen co-op): when a
		// player runs out of lives the whole session ends and returns to the game-over screen. Competitive modes use
		// infinite respawns, and online sessions never reach this path at all - Player::OnPerishInner only calls
		// HandleGameOver in local sessions once lives are exhausted - so this is gated to local cooperation.
		if (IsLocalSession() && _networkManager->GetServerConfiguration().GameMode == MpGameMode::Cooperation) {
			LevelHandler::HandleGameOver(player);
		}
	}

	bool MpLevelHandler::HandlePlayerDied(Actors::Player* player)
	{
#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			// TODO: killer
			_scripts->OnPlayerDied(player, nullptr);
		}
#endif

		// TODO
		//return LevelHandler::HandlePlayerDied(player, collider);

		if (_isServer) {
			auto& serverConfig = _networkManager->GetServerConfiguration();
			auto* mpPlayer = static_cast<PlayerOnServer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			// If the dying player was carrying an enemy flag, drop it where they fell
			if (serverConfig.GameMode == MpGameMode::CaptureTheFlag) {
				DropCtfFlag(player);
			}

			bool canRespawn = (_enableSpawning && _nextLevelType == ExitType::None &&
				(_levelState != LevelState::Running || !serverConfig.Elimination || peerDesc->Deaths < serverConfig.TotalKills));
			bool shouldRollback = false;

			if (canRespawn && _activeBoss != nullptr) {
				bool anyoneAlive = false;
				for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
					if (peerDesc->Player && peerDesc->Player->_health > 0) {
						anyoneAlive = true;
						break;
					}
				}

				if (anyoneAlive) {
					// If at least one player is alive, don't rollback yet
					canRespawn = false;
				} else {
					if (_activeBoss->OnPlayerDied()) {
						_activeBoss = nullptr;
					}

					shouldRollback = true;
				}
			}

			if (canRespawn) {
				LimitCameraView(mpPlayer, mpPlayer->_pos, 0, 0);

				if (shouldRollback) {
					LOGI("Rolling back to checkpoint");
					for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
						if (peerDesc->Player && peerDesc->Player != player) {
							peerDesc->Player->Respawn(mpPlayer->_checkpointPos);

							if (peerDesc->RemotePeer) {
								peerDesc->LastUpdated = UINT64_MAX;
								static_cast<PlayerOnServer*>(peerDesc->Player)->_canTakeDamage = false;

								MemoryStream packet2(12);
								packet2.WriteVariableUint32(peerDesc->Player->_playerIndex);
								packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.X * 512.0f));
								packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.Y * 512.0f));
								_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet2);
							}
						}
					}

					RollbackLevelState();
					BeginPlayMusic(_musicDefaultPath);
				}

				if (_gameMode != nullptr) {
					// The game mode decides where the player respawns (its own checkpoint vs. a fresh spawn point) and
					// the spawn invulnerability. Cooperation keeps the checkpoint; competitive modes move to a spawn point.
					RespawnDecision respawn = _gameMode->DecideRespawn(*this, mpPlayer);
					if (!respawn.UseCheckpoint) {
						mpPlayer->_checkpointPos = respawn.SpawnPos;
					}
					if (respawn.InvulnerableSecs > 0.0f) {
						mpPlayer->SetInvulnerability(respawn.InvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Blinking);
					}
				}
			}

			if (_levelState != LevelState::Running) {
				if (peerDesc->RemotePeer) {
					peerDesc->LastUpdated = UINT64_MAX;
					mpPlayer->_canTakeDamage = false;

					MemoryStream packet2(12);
					packet2.WriteVariableUint32(mpPlayer->_playerIndex);
					packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.X * 512.0f));
					packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.Y * 512.0f));
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet2);
				}
				return canRespawn;
			}

			if (serverConfig.Elimination && peerDesc->Deaths >= serverConfig.TotalKills) {
				peerDesc->DeathElapsedFrames = _elapsedFrames;
			}

			if (canRespawn && peerDesc->RemotePeer) {
				peerDesc->LastUpdated = UINT64_MAX;
				mpPlayer->_canTakeDamage = false;

				MemoryStream packet2(12);
				packet2.WriteVariableUint32(mpPlayer->_playerIndex);
				packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.X * 512.0f));
				packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.Y * 512.0f));
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet2);
			}

			CheckGameEnds();

			return canRespawn;
		} else {
			auto* mpPlayer = static_cast<RemotablePlayer*>(player);
			if (mpPlayer->RespawnPending) {
				mpPlayer->RespawnPending = false;
				player->_checkpointPos = mpPlayer->RespawnPos;

				Clock& c = nCine::clock();
				std::uint64_t now = c.now() * 1000 / c.frequency();

				MemoryStream packetAck(24);
				packetAck.WriteVariableUint32(_lastSpawnedActorId);
				packetAck.WriteVariableUint64(now);
				packetAck.WriteValue<std::int32_t>((std::int32_t)(player->_checkpointPos.X * 512.0f));
				packetAck.WriteValue<std::int32_t>((std::int32_t)(player->_checkpointPos.Y * 512.0f));
				packetAck.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
				packetAck.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
				_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerAckWarped, packetAck);

				return true;
			}

			return false;
		}
	}

	void MpLevelHandler::HandlePlayerWarped(Actors::Player* player, Vector2f prevPos, WarpFlags flags)
	{
		LevelHandler::HandlePlayerWarped(player, prevPos, flags);

		/*if (_isServer) {
			auto it = _playerStates.find(player->_playerIndex);
			if (it != _playerStates.end()) {
				it->second.Flags |= PlayerFlags::JustWarped;
			}
		} else {
			_seqNumWarped = _seqNum;
		}*/

		if (_isServer) {
			auto* mpPlayer = static_cast<PlayerOnServer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			mpPlayer->_justWarped = true;

			if ((flags & WarpFlags::IncrementLaps) == WarpFlags::IncrementLaps && _levelState == LevelState::Running) {
				// Don't allow laps to be quickly incremented twice in a row
				if ((_elapsedFrames - peerDesc->LapsElapsedFrames) > 2.0f * FrameTimer::FramesPerSecond) {
					peerDesc->Laps++;
					auto now = TimeStamp::now();
					//peerDesc->LapsElapsedFrames += (now - peerDesc->LapStarted).seconds() * FrameTimer::FramesPerSecond;
					peerDesc->LapsElapsedFrames = _elapsedFrames;
					peerDesc->LapStarted = now;

					LOGI("Player {} finished lap ({})", mpPlayer->_playerIndex, peerDesc->Laps);

					CheckGameEnds();
				} else {
					LOGW("Player {} attempted to increment laps twice in a row", mpPlayer->_playerIndex);
				}
			}

			if (peerDesc->RemotePeer) {
				if ((flags & WarpFlags::IncrementLaps) == WarpFlags::IncrementLaps && _levelState == LevelState::Running) {
					auto& serverConfig = _networkManager->GetServerConfiguration();

					MemoryStream packet2(13);
					packet2.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Laps);
					packet2.WriteVariableUint32(mpPlayer->_playerIndex);
					packet2.WriteVariableUint32(peerDesc->Laps);
					packet2.WriteVariableUint32(serverConfig.TotalLaps);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet2);
				}

				MemoryStream packet(20);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_pos.X * 512.0f));
				packet.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_pos.Y * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_speed.X * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_speed.Y * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_externalForce.X * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_externalForce.Y * 512.0f));
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerMoveInstantly, packet);
			}
		} else {
			Clock& c = nCine::clock();
			std::uint64_t now = c.now() * 1000 / c.frequency();

			MemoryStream packetAck(24);
			packetAck.WriteVariableUint32(_lastSpawnedActorId);
			packetAck.WriteVariableUint64(now);
			packetAck.WriteValue<std::int32_t>((std::int32_t)(player->_pos.X * 512.0f));
			packetAck.WriteValue<std::int32_t>((std::int32_t)(player->_pos.Y * 512.0f));
			packetAck.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
			packetAck.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
			_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerAckWarped, packetAck);
		}
	}

	void MpLevelHandler::HandlePlayerPushed(Actors::Player* player)
	{
		// A server-side force was applied to the player (boss attack, explosion, bumper, ...). Resync the
		// resulting position/velocity to the owning client so it isn't overwritten by the client's own state.
		HandlePlayerBumped(player);
	}

	void MpLevelHandler::HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount)
	{
		//LevelHandler::HandlePlayerCoins(player, prevCount, newCount);

		if (_isServer) {
			// Coins are shared in cooperation, add it also to all other local players
			auto& serverConfig = _networkManager->GetServerConfiguration();
			if (serverConfig.GameMode == MpGameMode::Cooperation) {
				// Coins are shared in cooperation - the mode adds the increment onto every other local player
				if (_gameMode != nullptr) {
					_gameMode->OnCoinsCollected(*this, player, prevCount, newCount);
				}

				for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
					if (peerDesc->RemotePeer && peerDesc->Player) {
						MemoryStream packet(9);
						packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Coins);
						packet.WriteVariableUint32(peerDesc->Player->_playerIndex);
						packet.WriteVariableInt32(peerDesc->Player->_coins);
						_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
					}
				}

				_hud->ShowCoins(newCount);
			} else {
				auto* mpPlayer = static_cast<MpPlayer*>(player);
				auto peerDesc = mpPlayer->GetPeerDescriptor();

				if (peerDesc->RemotePeer) {
					MemoryStream packet(9);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Coins);
					packet.WriteVariableUint32(mpPlayer->_playerIndex);
					packet.WriteVariableInt32(newCount);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
				}

				// Show notification only for local players (which have assigned viewport)
				for (auto& viewport : _assignedViewports) {
					if (viewport->_targetActor == player) {
						_hud->ShowCoins(newCount);
						break;
					}
				}
			}
		}
	}

	void MpLevelHandler::HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount)
	{
		if (_isServer) {
			auto& serverConfig = _networkManager->GetServerConfiguration();
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt) {
				// Count gems as treasure
				if (newCount > prevCount && _levelState == LevelState::Running) {
					std::int32_t weightedCount = GetTreasureWeight(gemType);
					peerDesc->TreasureCollected += (newCount - prevCount) * weightedCount;

					if (peerDesc->RemotePeer) {
						MemoryStream packet(9);
						packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::TreasureCollected);
						packet.WriteVariableUint32(mpPlayer->_playerIndex);
						packet.WriteVariableUint32(peerDesc->TreasureCollected);
						_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
					}

					CheckGameEnds();
				}
			} else {
				// Show standard gems notification
				if (peerDesc->RemotePeer) {
					MemoryStream packet(10);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Gems);
					packet.WriteVariableUint32(mpPlayer->_playerIndex);
					packet.WriteValue<std::uint8_t>(gemType);
					packet.WriteVariableInt32(newCount);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);

					MemoryStream packet2(9);
					packet2.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::TreasureCollected);
					packet2.WriteVariableUint32(mpPlayer->_playerIndex);
					packet2.WriteVariableUint32(peerDesc->TreasureCollected);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet2);
				}

				// Show notification only for local players (which have assigned viewport)
				for (auto& viewport : _assignedViewports) {
					if (viewport->_targetActor == player) {
						_hud->ShowGems(gemType, newCount);
						break;
					}
				}
			}
		}
	}

	void MpLevelHandler::SetCheckpoint(Actors::Player* player, Vector2f pos)
	{
		LevelHandler::SetCheckpoint(player, pos);

		float ambientLight = _defaultAmbientLight.W;
		for (auto& viewport : _assignedViewports) {
			if (viewport->_targetActor == player) {
				ambientLight = viewport->_ambientLightTarget;
				break;
			}
		}

		_lastCheckpointPos = Vector2f(pos.X, pos.Y - 20.0f);
		_lastCheckpointLight = ambientLight;
	}

	void MpLevelHandler::RollbackToCheckpoint(Actors::Player* player)
	{
		// TODO: Remove this override
		LevelHandler::RollbackToCheckpoint(player);
	}

	void MpLevelHandler::HandleActivateSugarRush(Actors::Player* player)
	{
		// TODO: Remove this override
		LevelHandler::HandleActivateSugarRush(player);
	}

	void MpLevelHandler::HandleCreateParticleDebrisOnPerish(const Actors::ActorBase* self, Actors::ParticleDebrisEffect effect, Vector2f speed)
	{
		LevelHandler::HandleCreateParticleDebrisOnPerish(self, effect, speed);

		if (_isServer) {
			std::uint32_t targetActorId = 0;
			{
				std::unique_lock lock(_lock);
				auto it = _remotingActors.find(const_cast<Actors::ActorBase*>(self));
				if (it != _remotingActors.end()) {
					targetActorId = it->second.ActorID;
				}
			}
			if (targetActorId != 0) {
				MemoryStream packet(13);
				packet.WriteValue<std::uint8_t>((std::uint8_t)effect);
				packet.WriteVariableUint32(targetActorId);
				packet.WriteVariableInt32((std::int32_t)(speed.X * 100.0f));
				packet.WriteVariableInt32((std::int32_t)(speed.Y * 100.0f));

				_networkManager->SendTo([this](const Peer& peer) {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateDebris, packet);
			} else {
				LOGW("Remote actor not found");
			}
		}
	}

	void MpLevelHandler::HandleCreateSpriteDebris(const Actors::ActorBase* self, AnimState state, std::int32_t count)
	{
		LevelHandler::HandleCreateSpriteDebris(self, state, count);

		if (_isServer) {
			std::uint32_t targetActorId = 0;
			{
				std::unique_lock lock(_lock);
				auto it = _remotingActors.find(const_cast<Actors::ActorBase*>(self));
				if (it != _remotingActors.end()) {
					targetActorId = it->second.ActorID;
				}
			}
			if (targetActorId != 0) {
				MemoryStream packet(13);
				packet.WriteValue<std::uint8_t>(UINT8_MAX); // Effect
				packet.WriteVariableUint32(targetActorId);
				packet.WriteVariableUint32((std::uint32_t)state);
				packet.WriteVariableInt32(count);

				_networkManager->SendTo([this](const Peer& peer) {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateDebris, packet);
			} else {
				LOGW("Remote actor not found");
			}
		}
	}

	void MpLevelHandler::ShowLevelText(StringView text, Actors::ActorBase* initiator)
	{
		if (initiator == nullptr || IsLocalPlayer(initiator)) {
			// Pass through only messages for local players
			LevelHandler::ShowLevelText(text, initiator);
		}
	}

	StringView MpLevelHandler::GetLevelText(std::uint32_t textId, std::int32_t index, std::uint32_t delimiter)
	{
		return LevelHandler::GetLevelText(textId, index, delimiter);
	}

	void MpLevelHandler::OverrideLevelText(std::uint32_t textId, StringView value)
	{
		LevelHandler::OverrideLevelText(textId, value);

		if (_isServer) {
			std::uint32_t textLength = (std::uint32_t)value.size();

			MemoryStream packet(9 + textLength);
			packet.WriteValue<std::uint8_t>((std::uint8_t)LevelPropertyType::LevelText);
			packet.WriteVariableUint32(textId);
			packet.WriteVariableUint32(textLength);
			packet.Write(value.data(), textLength);

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelLoaded);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LevelSetProperty, packet);
		}
	}

	void MpLevelHandler::LimitCameraView(Actors::Player* player, Vector2f playerPos, std::int32_t left, std::int32_t width)
	{
		std::int32_t prevLeft = _limitCameraLeft;
		std::int32_t prevWidth = _limitCameraWidth;

		_limitCameraLeft = left;
		_limitCameraWidth = width;

		LevelHandler::LimitCameraView(player, playerPos, left, width);

		if (_isServer && (prevLeft != _limitCameraLeft || prevWidth != _limitCameraWidth)) {
			for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
				if (peerDesc->RemotePeer && peerDesc->Player) {
					MemoryStream packet(21);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::LimitCameraView);
					packet.WriteVariableUint32(peerDesc->Player->_playerIndex);
					packet.WriteVariableInt32(left);
					packet.WriteVariableInt32(width);
					packet.WriteVariableInt32((std::int32_t)playerPos.X);
					packet.WriteVariableInt32((std::int32_t)playerPos.Y);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
				}
			}
		}
	}

	void MpLevelHandler::OverrideCameraView(Actors::Player* player, float x, float y, bool topLeft)
	{
		// TODO: Include this state in initial sync
		LevelHandler::OverrideCameraView(player, x, y, topLeft);

		if (_isServer) {
			if (auto* mpPlayer = runtime_cast<RemotePlayerOnServer>(player)) {
				MemoryStream packet(14);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::OverrideCameraView);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)(x * 100.0f));
				packet.WriteVariableInt32((std::int32_t)(y * 100.0f));
				packet.WriteValue<std::uint8_t>(topLeft ? 1 : 0);
				_networkManager->SendTo(mpPlayer->GetPeerDescriptor()->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::ShakeCameraView(Actors::Player* player, float duration)
	{
		LevelHandler::ShakeCameraView(player, duration);

		if (_isServer) {
			if (auto* mpPlayer = runtime_cast<RemotePlayerOnServer>(player)) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::ShakeCameraView);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)(duration * 100.0f));
				_networkManager->SendTo(mpPlayer->GetPeerDescriptor()->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}
	
	void MpLevelHandler::ShakeCameraViewNear(Vector2f pos, float duration)
	{
		// TODO: This should probably be client local
		LevelHandler::ShakeCameraViewNear(pos, duration);

		constexpr float MaxDistance = 800.0f;

		if (_isServer) {
			for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
				if (peerDesc->RemotePeer && peerDesc->Player && (peerDesc->Player->_pos - pos).Length() <= MaxDistance) {
					MemoryStream packet(9);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::ShakeCameraView);
					packet.WriteVariableUint32(peerDesc->Player->_playerIndex);
					packet.WriteVariableInt32((std::int32_t)(duration * 100.0f));
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
				}
			}
		}
	}

	void MpLevelHandler::SetTrigger(std::uint8_t triggerId, bool newState)
	{
		LevelHandler::SetTrigger(triggerId, newState);

		if (_isServer) {
			MemoryStream packet(2);
			packet.WriteValue<std::uint8_t>(triggerId);
			packet.WriteValue<std::uint8_t>(newState);

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SetTrigger, packet);
		}
	}

	void MpLevelHandler::SetWeather(WeatherType type, std::uint8_t intensity)
	{
		// TODO: This should probably be client local
		LevelHandler::SetWeather(type, intensity);
	}

	bool MpLevelHandler::BeginPlayMusic(StringView path, bool setDefault, bool forceReload)
	{
		// TODO: Include this state in initial sync
		bool success = LevelHandler::BeginPlayMusic(path, setDefault, forceReload);

		if (_isServer) {
			std::uint8_t flags = 0;
			if (setDefault) flags |= 0x01;
			if (forceReload) flags |= 0x02;

			MemoryStream packet(6 + path.size());
			packet.WriteValue<std::uint8_t>((std::uint8_t)LevelPropertyType::Music);
			packet.WriteValue<std::uint8_t>(flags);
			packet.WriteVariableUint32((std::uint32_t)path.size());
			packet.Write(path.data(), (std::uint32_t)path.size());

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LevelSetProperty, packet);
		}

		return success;
	}

	bool MpLevelHandler::PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad)
	{
		if (_isServer) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(player)) {
				// PlayerChangeWeaponRequest is sent from the client side everytime, suppress these actions on the server
				if (action == PlayerAction::ChangeWeapon || (action >= PlayerAction::SwitchToBlaster && action <= PlayerAction::SwitchToThunderbolt)) {
					return false;
				}

				if ((remotePlayerOnServer->PressedKeys & (1ull << (std::int32_t)action)) != 0) {
					isGamepad = (remotePlayerOnServer->PressedKeys & (1ull << (32 + (std::int32_t)action))) != 0;
					return true;
				}

				isGamepad = false;
				return false;
			}
		}

		return LevelHandler::PlayerActionPressed(player, action, includeGamepads, isGamepad);
	}

	bool MpLevelHandler::PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad)
	{
		if (_isServer) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(player)) {
				// PlayerChangeWeaponRequest is sent from the client side everytime, suppress these actions on the server
				if (action == PlayerAction::ChangeWeapon || (action >= PlayerAction::SwitchToBlaster && action <= PlayerAction::SwitchToThunderbolt)) {
					return false;
				}

				if ((remotePlayerOnServer->PressedKeys & (1ull << (std::int32_t)action)) != 0 && (remotePlayerOnServer->PressedKeysLast & (1ull << (std::int32_t)action)) == 0) {
					isGamepad = (remotePlayerOnServer->PressedKeys & (1ull << (32 + (std::int32_t)action))) != 0;
					return true;
				}

				isGamepad = false;
				return false;
			}
		}

		return LevelHandler::PlayerActionHit(player, action, includeGamepads, isGamepad);
	}

	float MpLevelHandler::PlayerHorizontalMovement(Actors::Player* player)
	{
		if (_isServer) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(player)) {
				if ((remotePlayerOnServer->PressedKeys & (1ull << (std::int32_t)PlayerAction::Left)) != 0) {
					return -1.0f;
				} else if ((remotePlayerOnServer->PressedKeys & (1ull << (std::int32_t)PlayerAction::Right)) != 0) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerHorizontalMovement(player);
	}

	float MpLevelHandler::PlayerVerticalMovement(Actors::Player* player)
	{
		if (_isServer) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(player)) {
				if ((remotePlayerOnServer->PressedKeys & (1ull << (std::int32_t)PlayerAction::Up)) != 0) {
					return -1.0f;
				} else if ((remotePlayerOnServer->PressedKeys & (1ull << (std::int32_t)PlayerAction::Down)) != 0) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerVerticalMovement(player);
	}

	void MpLevelHandler::PlayerExecuteRumble(Actors::Player* player, StringView rumbleEffect)
	{
		if (_isServer) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(player)) {
				// Ignore remote players
				return;
			}
		}

		return LevelHandler::PlayerExecuteRumble(player, rumbleEffect);
	}

	bool MpLevelHandler::SerializeResumableToStream(Stream& dest)
	{
		// Online multiplayer sessions cannot be resumed. Local splitscreen cooperation can be resumed just like
		// classic local co-op - it serializes in the base format and restores as a regular LevelHandler session.
		// Competitive local modes are not resumable.
		if (!IsLocalSession() || GetGameMode() != MpGameMode::Cooperation) {
			return false;
		}

		return LevelHandler::SerializeResumableToStream(dest);
	}

	void MpLevelHandler::OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount)
	{
		if (_isServer) {
			MemoryStream packet(12);
			packet.WriteVariableInt32(tx);
			packet.WriteVariableInt32(ty);
			packet.WriteVariableInt32(amount);

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::AdvanceTileAnimation, packet);
		}
	}

	StringView MpLevelHandler::GetLevelDisplayName() const
	{
		return _levelDisplayName;
	}

	MpGameMode MpLevelHandler::GetGameMode() const
	{
		const auto& serverConfig = _networkManager->GetServerConfiguration();
		return serverConfig.GameMode;
	}
	IGameMode* MpLevelHandler::GetActiveGameMode() const
	{
		return _gameMode.get();
	}

	bool MpLevelHandler::DrawActiveGameModeHUD(IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		if (_gameMode == nullptr) {
			return false;
		}

		_gameMode->OnDrawHUD(*this, hud, player, view);
		return true;
	}

	const ServerConfiguration& MpLevelHandler::GetServerConfiguration() const
	{
		return _networkManager->GetServerConfiguration();
	}

	ArrayView<Actors::Player* const> MpLevelHandler::GetPlayers() const
	{
		return LevelHandler::GetPlayers();
	}

	MpPlayerState& MpLevelHandler::GetPlayerState(Actors::Player* player)
	{
		return *static_cast<MpPlayer*>(player)->GetPeerDescriptor();
	}

	bool MpLevelHandler::IsSpectating(Actors::Player* player) const
	{
		return (static_cast<MpPlayer*>(player)->GetPeerDescriptor()->IsSpectating != SpectateMode::None);
	}

	std::uint8_t MpLevelHandler::GetCtfFlagStateCount() const
	{
		return (std::uint8_t)_ctfFlagStates.size();
	}

	std::uint8_t MpLevelHandler::GetCtfFlagState(std::uint8_t team) const
	{
		return _ctfFlagStates[team].State;
	}

	Vector2f MpLevelHandler::GetSpawnPoint(Actors::Player* player)
	{
		auto peerDesc = static_cast<MpPlayer*>(player)->GetPeerDescriptor();
		return GetSpawnPoint(peerDesc->PreferredPlayerType, peerDesc->Team);
	}

	float MpLevelHandler::GetElapsedFrames() const
	{
		return LevelHandler::GetElapsedFrames();
	}

	void MpLevelHandler::EndGame(Actors::Player* winner)
	{
		EndGame(static_cast<MpPlayer*>(winner));
	}

	std::uint32_t MpLevelHandler::GetTotalLaps() const
	{
		const auto& serverConfig = _networkManager->GetServerConfiguration();
		return serverConfig.TotalLaps;
	}

	bool MpLevelHandler::SetGameMode(MpGameMode value)
	{
		if (!_isServer) {
			return false;
		}

		LOGI("Game mode set to {}", NetworkManager::GameModeToString(value));

		auto& serverConfig = _networkManager->GetServerConfiguration();
		serverConfig.GameMode = value;

		ApplyGameModeToAllPlayers(serverConfig.GameMode);
		SynchronizeGameMode();

		// The level didn't change, but switching to a race mode requires (re)computing the track and re-syncing it
		// to all clients (the per-peer sync only happens on level load)
		if (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::TeamRace) {
			BuildRaceCheckpoints();

			// Send the geometry only if the minimap is allowed; otherwise send an empty set to clear any minimap
			// the clients may have had (the ranking still uses the checkpoints server-side)
			bool allowMinimap = serverConfig.AllowMinimap;
			MemoryStream packet(24 + _orderedRaceCheckpoints.size() * 9 + _raceStartMarkers.size() * 8);
			packet.WriteVariableInt32(_raceBoundsMin.X);
			packet.WriteVariableInt32(_raceBoundsMin.Y);
			packet.WriteVariableInt32(_raceBoundsMax.X);
			packet.WriteVariableInt32(_raceBoundsMax.Y);
			packet.WriteVariableUint32(allowMinimap ? (std::uint32_t)_orderedRaceCheckpoints.size() : 0);
			if (allowMinimap) {
				for (const auto& cp : _orderedRaceCheckpoints) {
					packet.WriteVariableInt32(cp.Tile.X);
					packet.WriteVariableInt32(cp.Tile.Y);
					packet.WriteVariableUint32(cp.Order);
					packet.WriteValue<std::uint8_t>(cp.Group);
				}
			}
			packet.WriteVariableUint32(allowMinimap ? (std::uint32_t)_raceStartMarkers.size() : 0);
			if (allowMinimap) {
				for (const auto& m : _raceStartMarkers) {
					packet.WriteVariableInt32(m.X);
					packet.WriteVariableInt32(m.Y);
				}
			}

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState != PeerLevelState::Unknown);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncRaceCheckpoints, packet);
		}

		// When switching AWAY from Capture The Flag, destroy any existing flags (BuildCtfBases() self-guards and
		// returns early in non-CTF modes). When switching TO CTF, the flags are (re)built at round start in
		// ResetAllPlayerStats() so the spawn reaches all currently-synchronized players.
		if (serverConfig.GameMode != MpGameMode::CaptureTheFlag) {
			BuildCtfBases();
		}

		if (serverConfig.GameMode != MpGameMode::Cooperation) {
			_levelState = LevelState::Countdown3;
			_gameTimeLeft = FrameTimer::FramesPerSecond;
			SetControllableToAllPlayers(false);
			WarpAllPlayersToStart();
			ResetAllPlayerStats();
			static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(3);
			SendLevelStateToAllPlayers();
		}

		return true;
	}

	bool MpLevelHandler::SynchronizeGameMode()
	{
		if (!_isServer) {
			return false;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();

		if (_autoWeightTreasure && (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt)) {
			std::int32_t playerCount = (std::int32_t)_players.size();
			if (playerCount > 0) {
				// Take 80% of all the treasure and divide it by clamped number of players
				serverConfig.TotalTreasureCollected = (_totalTreasureCount * 8) / (10 * std::max(playerCount, 3));
				// Round to multiples of 5
				serverConfig.TotalTreasureCollected = (std::int32_t)roundf(serverConfig.TotalTreasureCollected / 5.0f) * 5;
				if (serverConfig.TotalTreasureCollected < 20) {
					serverConfig.TotalTreasureCollected = std::min(20, _totalTreasureCount);
				}
			}
		}

		std::uint8_t flags = 0;
		if (_isReforged) {
			flags |= 0x01;
		}
		if (PreferencesCache::EnableLedgeClimb) {
			flags |= 0x02;
		}
		if (serverConfig.Elimination) {
			flags |= 0x04;
		}
		if (serverConfig.EnableSpectate) {
			flags |= 0x08;
		}
		if (serverConfig.PlayerStacking) {
			flags |= 0x10;
		}
		if (serverConfig.AllowTeamSelection) {
			flags |= 0x20;
		}
		if (serverConfig.FriendlyFire) {
			flags |= 0x40;
		}
		if (serverConfig.AutoBalanceTeams) {
			flags |= 0x80;
		}

		for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
			if (peerDesc->LevelState < PeerLevelState::LevelSynchronized) {
				continue;
			}

			MemoryStream packet(27);
			packet.WriteValue<std::uint8_t>((std::uint8_t)LevelPropertyType::GameMode);
			packet.WriteValue<std::uint8_t>(flags);
			packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.GameMode);
			packet.WriteValue<std::uint8_t>(GetTeamCount());
			packet.WriteValue<std::uint8_t>(peerDesc->Team);
			packet.WriteValue<std::uint8_t>(serverConfig.AllowedPlayerTypes);
			packet.WriteVariableInt32(serverConfig.InitialPlayerHealth);
			packet.WriteVariableUint32(serverConfig.MaxGameTimeSecs);
			packet.WriteVariableUint32(serverConfig.TotalKills);
			packet.WriteVariableUint32(serverConfig.TotalLaps);
			packet.WriteVariableUint32(serverConfig.TotalTreasureCollected);
			packet.WriteValue<std::uint8_t>(serverConfig.ColorizePlayersByTeam ? 1 : 0);

			_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LevelSetProperty, packet);
		}

		// Push initial team scores so the HUD shows them from the start of the round
		SyncTeamScores();

		return true;
	}

	void MpLevelHandler::RequestChangeTeam(std::uint8_t team)
	{
		if (_isServer) {
			// Host applies the change directly
			for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
				if (!peerDesc->RemotePeer && peerDesc->Player) {
					ChangePlayerTeam(peerDesc->Player, team, false);
					break;
				}
			}
			return;
		}

		MemoryStream packet(1);
		packet.WriteValue<std::uint8_t>(team);
		_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerChangeTeamRequest, packet);
	}

	MpPlayer* MpLevelHandler::GetWeaponOwner(Actors::ActorBase* actor)
	{
		if (auto* player = runtime_cast<MpPlayer>(actor)) {
			return player;
		} else if (auto* shotBase = runtime_cast<Actors::Weapons::ShotBase>(actor)) {
			return static_cast<MpPlayer*>(shotBase->GetOwner());
		} else if (auto* tnt = runtime_cast<Actors::Weapons::TNT>(actor)) {
			return static_cast<MpPlayer*>(tnt->GetOwner());
		} else {
			return nullptr;
		}
	}

	bool MpLevelHandler::ProcessCommand(const Peer& peer, StringView line, bool isAdmin)
	{
		char infoBuffer[256];

		if (line == "/endpoints"_s) {
			if (isAdmin) {
				auto endpoints = _networkManager->GetServerEndpoints();
				for (const auto& endpoint : endpoints) {
					SendMessage(peer, UI::MessageLevel::Confirm, endpoint);
				}
				auto& serverConfig = _networkManager->GetServerConfiguration();
				StringView address; std::uint16_t port;
				if (NetworkManagerBase::TrySplitAddressAndPort(serverConfig.ServerAddressOverride, address, port)) {
					if (port == 0) {
						port = serverConfig.ServerPort;
					}
					std::size_t length = formatInto(infoBuffer, "{}:{} (Override)", address, port);
					SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
				}
				return true;
			}
		} else if (line == "/info"_s) {
			auto& serverConfig = _networkManager->GetServerConfiguration();

			std::size_t length = formatInto(infoBuffer, "Server: {}{}", serverConfig.ServerName, serverConfig.IsPrivate ? " (Private)" : "");
			SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
			length = formatInto(infoBuffer, "Current level: \"{}\" ({}{}{})",
				_levelName, NetworkManager::GameModeToString(serverConfig.GameMode),
				serverConfig.Elimination ? "/Elimination" : "", _isReforged ? "/Reforged" : "");
			SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
			length = formatInto(infoBuffer, "Players: {}/{}",
				(std::uint32_t)_networkManager->GetPeerCount(), serverConfig.MaxPlayerCount);
			SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
			if (!_players.empty()) {
				length = formatInto(infoBuffer, "Server load: {:.1f} ms ({:.1f})",
					(theApplication().GetFrameTimer().GetLastFrameDuration() * 1000.0f), theApplication().GetFrameTimer().GetAverageFps());
			} else {
				length = formatInto(infoBuffer, "Server load: - ms");
			}
			SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });

			auto uptimeSecs = (DateTime::UtcNow().ToUnixMilliseconds() / 1000) - (std::int64_t)serverConfig.StartUnixTimestamp;
			auto hours = (std::int32_t)(uptimeSecs / 3600);
			auto minutes = (std::int32_t)(uptimeSecs % 3600) / 60;
			auto seconds = (std::int32_t)(uptimeSecs % 60);
			length = formatInto(infoBuffer, "Uptime: {}:{:.2}:{:.2}", hours, minutes, seconds);
			SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });

			if (isAdmin) {
				length = formatInto(infoBuffer, "Config Path: \"{}\"", serverConfig.FilePath);
				SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
			}

			if (!serverConfig.Playlist.empty()) {
				length = formatInto(infoBuffer, "Playlist: {}/{}{}",
					(std::uint32_t)(serverConfig.PlaylistIndex + 1), (std::uint32_t)serverConfig.Playlist.size(), serverConfig.RandomizePlaylist ? " (Random)" : "");
				SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
			}
			return true;
		} else if (line == "/players"_s) {
			auto& serverConfig = _networkManager->GetServerConfiguration();
			SendMessage(peer, UI::MessageLevel::Confirm, "List of connected players:"_s);

			StringView playerName;
			char playerNameBuffer[64];
			for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
				if (!peerDesc->RemotePeer && !peerDesc->Player) {
					continue;
				}

				if (peerDesc->Player) {
					std::size_t length = formatInto(playerNameBuffer, "{}#{}", peerDesc->PlayerName, peerDesc->Player->_playerIndex);
					playerName = { playerNameBuffer, length };
				} else {
					playerName = peerDesc->PlayerName;
				}

				std::size_t length;
				if (_levelState < LevelState::Running) {
					length = formatInto(infoBuffer, "{}.\t{}\t │ {} ms\t │ P: {}\t │ I: {}\f[w:50] s\f[/w]",
						peerDesc->PositionInRound, playerName, _networkManager->GetRoundTripTimeMs(peerDesc->RemotePeer),
						peerDesc->Points, peerDesc->Player ? (std::int32_t)(peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame) : -1);
				} else if (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::Race) {
					length = formatInto(infoBuffer, "{}.\t{}\t │ {} ms\t │ P: {}\t │ K: {}\t │ D: {}\t │ I: {}\f[w:50] s\f[/w]\t│ Laps: {}/{}",
						peerDesc->PositionInRound, playerName, _networkManager->GetRoundTripTimeMs(peerDesc->RemotePeer),
						peerDesc->Points, peerDesc->Kills, peerDesc->Deaths, peerDesc->Player ? (std::int32_t)(peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame) : -1, 
						peerDesc->Laps + 1, serverConfig.TotalLaps);
				} else if (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt) {
					length = formatInto(infoBuffer, "{}.\t{}\t │ {} ms\t │ P: {}\t │ K: {}\t │ D: {}\t │ I: {}\f[w:50] s\f[/w]\t│ Treasure: {}",
						peerDesc->PositionInRound, playerName, _networkManager->GetRoundTripTimeMs(peerDesc->RemotePeer),
						peerDesc->Points, peerDesc->Kills, peerDesc->Deaths, peerDesc->Player ? (std::int32_t)(peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame) : -1,
						peerDesc->TreasureCollected);
				} else {
					length = formatInto(infoBuffer, "{}.\t{}\t │ {} ms\t │ P: {}\t │ K: {}\t │ D: {}\t │ I: {}\f[w:50] s\f[/w]",
						peerDesc->PositionInRound, playerName, _networkManager->GetRoundTripTimeMs(peerDesc->RemotePeer),
						peerDesc->Points, peerDesc->Kills, peerDesc->Deaths,
						peerDesc->Player ? (std::int32_t)(peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame) : -1);
				}
				SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
			}
			return true;
		} else if (line.hasPrefix("/set "_s)) {
			if (isAdmin) {
				auto [variableName, sep, value] = line.exceptPrefix("/set "_s).trimmedPrefix().partition(' ');
				if (variableName == "mode"_s) {
					auto gameModeString = StringUtils::lowercase(value.trimmed());

					MpGameMode gameMode;
					if (gameModeString == "battle"_s || gameModeString == "b"_s) {
						gameMode = MpGameMode::Battle;
					} else if (gameModeString == "teambattle"_s || gameModeString == "tb"_s) {
						gameMode = MpGameMode::TeamBattle;
					} else if (gameModeString == "race"_s || gameModeString == "r"_s) {
						gameMode = MpGameMode::Race;
					} else if (gameModeString == "teamrace"_s || gameModeString == "tr"_s) {
						gameMode = MpGameMode::TeamRace;
					} else if (gameModeString == "treasurehunt"_s || gameModeString == "th"_s) {
						gameMode = MpGameMode::TreasureHunt;
					} else if (gameModeString == "teamtreasurehunt"_s || gameModeString == "tth"_s) {
						gameMode = MpGameMode::TeamTreasureHunt;
					} else if (gameModeString == "capturetheflag"_s || gameModeString == "ctf"_s) {
						gameMode = MpGameMode::CaptureTheFlag;
					} else if (gameModeString == "cooperation"_s || gameModeString == "coop"_s || gameModeString == "c"_s) {
						gameMode = MpGameMode::Cooperation;
					} else {
						return false;
					}

					if (SetGameMode(gameMode)) {
						std::size_t length = formatInto(infoBuffer, "Game mode set to \f[w:80]\f[c:#707070]{}\f[/c]\f[/w]",
							NetworkManager::GameModeToString(gameMode));
						SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
						return true;
					}
				} else if (variableName == "level"_s) {
					LevelInitialization levelInit;
					PrepareNextLevelInitialization(levelInit);
					auto level = value.partition('/');
					if (value.contains('/')) {
						levelInit.LevelName = value;
					} else {
						levelInit.LevelName = "unknown/"_s + value;
					}
					if (ContentResolver::Get().LevelExists(levelInit.LevelName)) {
						LOGD("Changing level to \"{}\"", levelInit.LevelName);

						auto& serverConfig = _networkManager->GetServerConfiguration();
						serverConfig.PlaylistIndex = -1;

						levelInit.LastExitType = ExitType::Normal;
						HandleLevelChange(std::move(levelInit));
					} else {
						SendMessage(peer, UI::MessageLevel::Confirm, "Level doesn't exist");
					}
					return true;
				} else if (variableName == "welcome"_s) {
					SetWelcomeMessage(StringUtils::replaceAll(value.trimmed(), "\\n"_s, "\n"_s));
					SendMessage(peer, UI::MessageLevel::Confirm, "Lobby message changed");
					return true;
				} else if (variableName == "name"_s) {
					auto& serverConfig = _networkManager->GetServerConfiguration();

					serverConfig.ServerName = value.trimmed();

					std::size_t length;
					if (!serverConfig.ServerName.empty()) {
						length = formatInto(infoBuffer, "Server name set to \f[w:80]\f[c:#707070]{}\f[/c]\f[/w]", serverConfig.ServerName);
					} else if (!serverConfig.IsPrivate) {
						length = formatInto(infoBuffer, "Server visibility to \f[w:80]\f[c:#707070]hidden\f[/c]\f[/w]");
					}
					SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
					return true;
				} else if (variableName == "spawning"_s) {
					auto boolValue = StringUtils::lowercase(value.trimmed());
					if (boolValue == "false"_s || boolValue == "off"_s || boolValue == "0"_s) {
						_enableSpawning = false;
					} else if (boolValue == "true"_s || boolValue == "on"_s || boolValue == "1"_s) {
						_enableSpawning = true;
					} else {
						return false;
					}

					std::size_t length = formatInto(infoBuffer, "Spawning set to \f[w:80]\f[c:#707070]{}\f[/c]\f[/w]", _enableSpawning ? "Enabled"_s : "Disabled"_s);
					SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
					return true;
				} else if (variableName == "kills"_s) {
					value = value.trimmed();
					auto intValue = stou32(value.data(), value.size());
					if (intValue <= 0 || intValue > INT32_MAX) {
						SendMessage(peer, UI::MessageLevel::Confirm, "Value out of range"_s);
						return true;
					}

					auto& serverConfig = _networkManager->GetServerConfiguration();

					serverConfig.TotalKills = intValue;
					SynchronizeGameMode();
					SendMessage(peer, UI::MessageLevel::Confirm, "Value changed successfully"_s);
					return true;
				} else if (variableName == "laps"_s) {
					value = value.trimmed();
					auto intValue = stou32(value.data(), value.size());
					if (intValue <= 0 || intValue > INT32_MAX) {
						SendMessage(peer, UI::MessageLevel::Confirm, "Value out of range"_s);
						return true;
					}

					auto& serverConfig = _networkManager->GetServerConfiguration();

					serverConfig.TotalLaps = intValue;
					SynchronizeGameMode();
					SendMessage(peer, UI::MessageLevel::Confirm, "Value changed successfully"_s);
					return true;
				} else if (variableName == "treasure"_s) {
					value = value.trimmed();
					auto intValue = stou32(value.data(), value.size());
					if (intValue < 0 || intValue > INT32_MAX) {
						SendMessage(peer, UI::MessageLevel::Confirm, "Value out of range"_s);
						return true;
					}

					auto& serverConfig = _networkManager->GetServerConfiguration();

					serverConfig.TotalTreasureCollected = intValue;
					_autoWeightTreasure = (serverConfig.TotalTreasureCollected == 0);
					SynchronizeGameMode();
					SendMessage(peer, UI::MessageLevel::Confirm, "Value changed successfully"_s);
					return true;
				} else if (variableName == "teamcount"_s) {
					value = value.trimmed();
					auto intValue = stou32(value.data(), value.size());
					if (intValue < 2 || intValue > MaxTeamCount) {
						SendMessage(peer, UI::MessageLevel::Confirm, _f("Value out of range (2-{})", MaxTeamCount));
						return true;
					}

					auto& serverConfig = _networkManager->GetServerConfiguration();
					serverConfig.TeamCount = (std::uint8_t)intValue;

					// Changing the team count requires re-splitting the teams, which restarts the round
					SetGameMode(serverConfig.GameMode);
					SendMessage(peer, UI::MessageLevel::Confirm, "Value changed successfully"_s);
					return true;
				} else if (variableName == "autobalance"_s) {
					auto boolValue = StringUtils::lowercase(value.trimmed());
					auto& serverConfig = _networkManager->GetServerConfiguration();
					if (boolValue == "false"_s || boolValue == "off"_s || boolValue == "0"_s) {
						serverConfig.AutoBalanceTeams = false;
					} else if (boolValue == "true"_s || boolValue == "on"_s || boolValue == "1"_s) {
						serverConfig.AutoBalanceTeams = true;
					} else {
						return false;
					}

					SynchronizeGameMode();
					if (serverConfig.AutoBalanceTeams) {
						RebalanceTeams(false);
					}
					std::size_t length = formatInto(infoBuffer, "Auto-balance set to \f[w:80]\f[c:#707070]{}\f[/c]\f[/w]", serverConfig.AutoBalanceTeams ? "Enabled"_s : "Disabled"_s);
					SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
					return true;
				} else if (variableName == "friendlyfire"_s) {
					auto boolValue = StringUtils::lowercase(value.trimmed());
					auto& serverConfig = _networkManager->GetServerConfiguration();
					if (boolValue == "false"_s || boolValue == "off"_s || boolValue == "0"_s) {
						serverConfig.FriendlyFire = false;
					} else if (boolValue == "true"_s || boolValue == "on"_s || boolValue == "1"_s) {
						serverConfig.FriendlyFire = true;
					} else {
						return false;
					}

					SynchronizeGameMode();
					std::size_t length = formatInto(infoBuffer, "Friendly fire set to \f[w:80]\f[c:#707070]{}\f[/c]\f[/w]", serverConfig.FriendlyFire ? "Enabled"_s : "Disabled"_s);
					SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
					return true;
				} else if (variableName == "teamselect"_s) {
					auto boolValue = StringUtils::lowercase(value.trimmed());
					auto& serverConfig = _networkManager->GetServerConfiguration();
					if (boolValue == "false"_s || boolValue == "off"_s || boolValue == "0"_s) {
						serverConfig.AllowTeamSelection = false;
					} else if (boolValue == "true"_s || boolValue == "on"_s || boolValue == "1"_s) {
						serverConfig.AllowTeamSelection = true;
					} else {
						return false;
					}

					SynchronizeGameMode();
					std::size_t length = formatInto(infoBuffer, "Team selection set to \f[w:80]\f[c:#707070]{}\f[/c]\f[/w]", serverConfig.AllowTeamSelection ? "Enabled"_s : "Disabled"_s);
					SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
					return true;
				}
			}
		} else if (line.hasPrefix("/team "_s)) {
			// Any player can request to change their own team (subject to AllowTeamSelection and balancing)
			auto arg = StringUtils::lowercase(line.exceptPrefix("/team "_s).trimmed());
			std::uint8_t requestedTeam;
			if (arg == "blue"_s || arg == "0"_s) {
				requestedTeam = 0;
			} else if (arg == "red"_s || arg == "1"_s) {
				requestedTeam = 1;
			} else if (arg == "green"_s || arg == "2"_s) {
				requestedTeam = 2;
			} else if (arg == "yellow"_s || arg == "3"_s) {
				requestedTeam = 3;
			} else if (arg == "auto"_s) {
				requestedTeam = NoPreferredTeam;
			} else {
				SendMessage(peer, UI::MessageLevel::Warning, _("Usage: /team <blue|red|green|yellow|auto>"));
				return true;
			}

			if (peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				if (peerDesc != nullptr && peerDesc->Player != nullptr && ChangePlayerTeam(peerDesc->Player, requestedTeam, false)) {
					SendMessage(peer, UI::MessageLevel::Info, _f("You joined the {} team", GetTeamName(peerDesc->Team)));
				} else {
					SendMessage(peer, UI::MessageLevel::Warning, _("Cannot change team right now"));
				}
			} else {
				// Host console
				RequestChangeTeam(requestedTeam);
			}
			return true;
		} else if (line == "/balance"_s) {
			if (isAdmin) {
				auto& serverConfig = _networkManager->GetServerConfiguration();
				if (!IsTeamGameMode(serverConfig.GameMode)) {
					SendMessage(peer, UI::MessageLevel::Warning, _("Teams are only used in team game modes"));
				} else {
					RebalanceTeams(true);
					SendMessage(peer, UI::MessageLevel::Confirm, _("Teams rebalanced"));
				}
			}
			return true;
		} else if (line.hasPrefix("/setteam "_s)) {
			if (isAdmin) {
				auto [playerArg, sep, teamArg] = line.exceptPrefix("/setteam "_s).trimmedPrefix().partition(' ');
				auto& serverConfig = _networkManager->GetServerConfiguration();
				if (!IsTeamGameMode(serverConfig.GameMode)) {
					SendMessage(peer, UI::MessageLevel::Warning, _("Teams are only used in team game modes"));
					return true;
				}

				auto teamStr = StringUtils::lowercase(teamArg.trimmed());
				std::uint8_t team;
				if (teamStr == "blue"_s || teamStr == "0"_s) {
					team = 0;
				} else if (teamStr == "red"_s || teamStr == "1"_s) {
					team = 1;
				} else if (teamStr == "green"_s || teamStr == "2"_s) {
					team = 2;
				} else if (teamStr == "yellow"_s || teamStr == "3"_s) {
					team = 3;
				} else {
					SendMessage(peer, UI::MessageLevel::Warning, _("Usage: /setteam <player#> <blue|red|green|yellow>"));
					return true;
				}

				auto playerStr = playerArg.trimmed();
				std::uint32_t targetIndex = stou32(playerStr.data(), playerStr.size());
				MpPlayer* target = nullptr;
				for (auto* player : _players) {
					if (player->_playerIndex == targetIndex) {
						target = static_cast<MpPlayer*>(player);
						break;
					}
				}

				if (target != nullptr && ChangePlayerTeam(target, team, true)) {
					SendMessage(peer, UI::MessageLevel::Confirm, _f("Player {} moved to the {} team", targetIndex, GetTeamName(team)));
				} else {
					SendMessage(peer, UI::MessageLevel::Warning, _("Player not found"));
				}
			}
			return true;
		} else if (line == "/refresh"_s) {
			if (isAdmin) {
				auto& serverConfig = _networkManager->GetServerConfiguration();
				auto prevGameMode = serverConfig.GameMode;

				_networkManager->RefreshServerConfiguration();

				// Refresh all affected properties
				SetWelcomeMessage(serverConfig.WelcomeMessage);

				if (serverConfig.PlaylistIndex >= 0 && serverConfig.PlaylistIndex < serverConfig.Playlist.size()) {
					ApplyFromPlaylist();
				} else if (serverConfig.GameMode != prevGameMode) {
					SetGameMode(serverConfig.GameMode);
				}
				SendMessage(peer, UI::MessageLevel::Info, "Server configuration reloaded"_s);
			}
			return true;
		} else if (line == "/reset points"_s) {
			if (isAdmin) {
				ResetPeerPoints();
				SendMessage(peer, UI::MessageLevel::Confirm, "All points reset"_s);
			}
			return true;
		} else if (line.hasPrefix("/alert "_s)) {
			if (isAdmin) {
				StringView message = line.exceptPrefix("/alert "_s).trimmed();
				ShowAlertToAllPlayers(message);
			}
			return true;
		} else if (line == "/skip"_s) {
			if (isAdmin) {
				auto& serverConfig = _networkManager->GetServerConfiguration();
				if (_levelState == LevelState::PreGame && _players.size() >= serverConfig.MinPlayerCount) {
					_gameTimeLeft = 0.0f;
				}
			}
			return true;
		} else if (line == "/next"_s) {
			if (isAdmin) {
				auto& serverConfig = _networkManager->GetServerConfiguration();
				if (!serverConfig.Playlist.empty() && serverConfig.PlaylistIndex >= 0) {
					// Playlist is active: advance to the next entry (wraps around)
					SkipInPlaylist();
				} else {
					// Otherwise load the level definition's next level (PrepareNextLevelInitialization resolves it
					// against the current episode). The current round's outcome is intentionally not evaluated.
					LevelInitialization levelInit;
					PrepareNextLevelInitialization(levelInit);
					if (!levelInit.LevelName.empty() && ContentResolver::Get().LevelExists(levelInit.LevelName)) {
						LOGD("Skipping to next level \"{}\"", levelInit.LevelName);
						levelInit.LastExitType = ExitType::Normal;
						HandleLevelChange(std::move(levelInit));
					} else {
						SendMessage(peer, UI::MessageLevel::Confirm, "No next level is defined for this level");
					}
				}
			}
			return true;
		} else if (line.hasPrefix("/playlist"_s)) {
			if (isAdmin) {
				auto& serverConfig = _networkManager->GetServerConfiguration();
				StringView value = (line.size() > 10 ? line.exceptPrefix(10).trimmed() : StringView());
				if (!value.empty()) {
					std::uint32_t idx = stou32(value.data(), value.size());
					if (idx < serverConfig.Playlist.size()) {
						serverConfig.PlaylistIndex = idx;
						ApplyFromPlaylist();
					}
				} else {
					SkipInPlaylist();
				}
			}
			return true;
		} else if (line.hasPrefix("/ip "_s)) {
			if (isAdmin) {
				if (auto playerName = line.exceptPrefix("/ip "_s)) {
					std::int32_t playerIndex = (playerName.hasPrefix('#') ? stou32(&playerName[1], playerName.size() - 1) : -1);

					for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
						if (playerIndex >= 0 ? (peerDesc->Player && peerDesc->Player->_playerIndex == playerIndex) : (peerDesc->PlayerName == playerName)) {
							std::size_t length;
							if (peerDesc->RemotePeer) {
								length = formatInto(infoBuffer, "{} ({}) is connected from {}", peerDesc->PlayerName,
									NetworkManager::UuidToString(peerDesc->UniquePlayerID), _networkManager->AddressToString(peerDesc->RemotePeer));
							} else {
								length = formatInto(infoBuffer, "{} is connected locally", peerDesc->PlayerName);
							}
							SendMessage(peer, UI::MessageLevel::Confirm, { infoBuffer, length });
							break;
						}
					}
				}
			}
			return true;
		} else if (line.hasPrefix("/kick "_s)) {
			if (isAdmin) {
				if (auto playerName = line.exceptPrefix("/kick "_s)) {
					std::int32_t playerIndex = (playerName.hasPrefix('#') ? stou32(&playerName[1], playerName.size() - 1) : -1);

					for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
						if (peerDesc->RemotePeer) {
							if (playerIndex >= 0 ? (peerDesc->Player && peerDesc->Player->_playerIndex == playerIndex) : (peerDesc->PlayerName == playerName)) {
								_networkManager->Kick(peerDesc->RemotePeer, Reason::Kicked);
								break;
							}
						}
					}
				}
			}
			return true;
		} else if (line == "/kill"_s) {
			auto peers = _networkManager->GetPeers();
			auto it = peers->find(peer);
			if (it != peers->end()) {
				InvokeAsync([peer = it->second]() {
					if (peer->Player) {
						peer->Player->TakeDamage(INT32_MAX, 0.0f, true);
					}
				});
			}
			return true;
		} else if (line.hasPrefix("/kill "_s)) {
			if (isAdmin) {
				if (auto playerName = line.exceptPrefix("/kill "_s)) {
					std::int32_t playerIndex = (playerName.hasPrefix('#') ? stou32(&playerName[1], playerName.size() - 1) : -1);

					for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
						if (peerDesc->Player) {
							if (playerIndex >= 0 ? (peerDesc->Player->_playerIndex == playerIndex) : (peerDesc->PlayerName == playerName)) {
								// TODO: Don't count it as a death
								std::uint32_t prevDeaths = peerDesc->Deaths;
								peerDesc->Player->TakeDamage(INT32_MAX, 0.0f, true);
								peerDesc->Deaths = prevDeaths;
								break;
							}
						}
					}
				}
			}
			return true;
		} else if (line.hasPrefix("/vote "_s)) {
			if (_activePoll != VoteType::None) {
				if (isAdmin && line == "/vote cancel"_s) {
					_activePoll = VoteType::None;
					SendMessageToAll("Active poll has been cancelled");
					return true;
				} else {
					std::size_t length = formatInto(infoBuffer, "Another poll is already active, please wait {} seconds", (std::int32_t)(_activePollTimeLeft * FrameTimer::SecondsPerFrame));
					SendMessage(peer, UI::MessageLevel::Error, { infoBuffer, length });
					return true;
				}
			}

			if (auto typeStr = line.exceptPrefix("/vote "_s)) {
				VoteType type;
				if (typeStr == "restart"_s) {
					// TODO: Support also non-playlist mode
					type = VoteType::Restart;
					SendMessageToAll("Poll started to \f[c:#777777]restart playlist\f[/c], type \f[c:#587050]/yes\f[/c] to vote"_s);
				} else if (typeStr == "reset points"_s) {
					type = VoteType::ResetPoints;
					SendMessageToAll("Poll started to \f[c:#777777]reset points\f[/c], type \f[c:#587050]/yes\f[/c] to vote"_s);
				} else if (typeStr == "skip"_s) {
					// TODO: Support also non-playlist mode
					type = VoteType::Skip;
					SendMessageToAll("Poll started to \f[c:#777777]skip current level\f[/c], type \f[c:#587050]/yes\f[/c] to vote"_s);
				} else if (typeStr == "kick"_s) {
					// TODO: Allow to vote kick for a specific player
					//type = VoteType::Kick;
					//formatString(infoBuffer, "Poll started to \f[c:#777777]kick player ...\f[/c], type \f[c:#608050]/yes\f[/c] to vote", typeStr.data());
					//SendServerMessageToAll(infoBuffer);
					return true;
				} else {
					std::size_t length = formatInto(infoBuffer, "Unsupported poll type \"{}\"", typeStr);
					SendMessage(peer, UI::MessageLevel::Error, { infoBuffer, length });
					return true;
				}

				_activePoll = type;
				_activePollTimeLeft = 60.0f * FrameTimer::FramesPerSecond;

				for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
					peerDesc->VotedYes = (playerPeer == peer);
				}
				return true;
			} else {
				return false;
			}
		} else if (line == "/yes"_s) {
			if (_activePoll != VoteType::None) {
				auto peers = _networkManager->GetPeers();
				auto it = peers->find(peer);
				if (it != peers->end()) {
					if (!it->second->VotedYes) {
						it->second->VotedYes = true;
						SendMessage(peer, UI::MessageLevel::Confirm, "Successfully voted"_s);
					} else {
						SendMessage(peer, UI::MessageLevel::Confirm, "Already voted"_s);
					}
					
				}
			} else {
				SendMessage(peer, UI::MessageLevel::Error, "No poll is active"_s);
			}
			return true;
		}

		return false;
	}

	void MpLevelHandler::SendMessage(const Peer& peer, UI::MessageLevel level, StringView message)
	{
		if (!peer.IsValid()) {
			if (ContentResolver::Get().IsHeadless()) {
#if defined(DEATH_TRACE)
				switch (level) {
					default: __DEATH_TRACE(TraceLevel::Info, {}, "< │ {}", message); break;
					case UI::MessageLevel::Echo: __DEATH_TRACE(TraceLevel::Info, {}, "> │ {}", message); break;
					case UI::MessageLevel::Warning: __DEATH_TRACE(TraceLevel::Warning, {}, "< │ {}", message); break;
					case UI::MessageLevel::Error: __DEATH_TRACE(TraceLevel::Error, {}, "< │ {}", message); break;
					case UI::MessageLevel::Assert: __DEATH_TRACE(TraceLevel::Assert, {}, "< │ {}", message); break;
					case UI::MessageLevel::Fatal: __DEATH_TRACE(TraceLevel::Fatal, {}, "< │ {}", message); break;
				}
#endif
			} else {
				_console->WriteLine(UI::MessageLevel::Info, message);
			}
			return;
		}

		MemoryStream packetOut(9 + message.size());
		packetOut.WriteVariableUint32(0); // Local player ID
		packetOut.WriteValue<std::uint8_t>((std::uint8_t)level);
		packetOut.WriteVariableUint32((std::uint32_t)message.size());
		packetOut.Write(message.data(), (std::uint32_t)message.size());

		_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packetOut);
	}

	void MpLevelHandler::SendMessageToAll(StringView message, bool asChatFromServer)
	{
		String prefixedMessage = message;

		if (asChatFromServer) {
			prefixedMessage = "\f[c:#907060]Server:\f[/c] "_s + prefixedMessage;
		}

		MemoryStream packetOut(9 + message.size());
		packetOut.WriteVariableUint32(0); // Local player ID
		packetOut.WriteValue<std::uint8_t>((std::uint8_t)UI::MessageLevel::Chat);
		packetOut.WriteVariableUint32((std::uint32_t)prefixedMessage.size());
		packetOut.Write(prefixedMessage.data(), (std::uint32_t)prefixedMessage.size());

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->IsAuthenticated);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packetOut);

		InvokeAsync([this, message = std::move(prefixedMessage)]() mutable {
			_console->WriteLine(UI::MessageLevel::Info, message);
		});
	}

	void MpLevelHandler::SetPlayerSpectateMode(Actors::Player* player, SpectateMode mode)
	{
		if DEATH_UNLIKELY(!_isServer) {
			return;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		auto* mpPlayer = static_cast<MpPlayer*>(player);
		auto peerDesc = mpPlayer->GetPeerDescriptor();
		std::uint32_t playerIndex = mpPlayer->_playerIndex;

		// Remove player from active players list
		for (std::size_t i = 0; i < _players.size(); i++) {
			if (_players[i] == player) {
				_players.eraseUnordered(i);
				break;
			}
		}

		// Destroy the previous player actor
		Vector2f playerPos = player->_pos;
		player->SetState(Actors::ActorState::IsDestroyed, true);

		// Notify all clients to destroy the actor
		MemoryStream packetDestroy(4);
		packetDestroy.WriteVariableUint32(playerIndex);
		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::DestroyRemoteActor, packetDestroy);

		// Spawn new player actor according to spectate mode
		std::shared_ptr<MpPlayer> newPlayer;
		MpPlayer* ptr;

		if (peerDesc->RemotePeer) {
			newPlayer = std::make_shared<RemotePlayerOnServer>(peerDesc);
		} else {
			newPlayer = std::make_shared<LocalPlayerOnServer>(peerDesc);
		}
		ptr = newPlayer.get();

		// Apply new spectating mode
		peerDesc->IsSpectating = mode;

		if ((mode & SpectateMode::Mask) != SpectateMode::None) {
			if (serverConfig.EnableFreeCamera) {
				peerDesc->IsSpectating |= SpectateMode::FreeCamera;
			}

			// Initialize the actor as spectator
			std::uint8_t playerParams[2] = { (std::uint8_t)PlayerType::Spectate, (std::uint8_t)playerIndex };
			ptr->OnActivated(Actors::ActorActivationDetails(this,
				Vector3i((std::int32_t)playerPos.X, (std::int32_t)playerPos.Y, PlayerZ - playerIndex),
				playerParams));
			ptr->_controllableExternal = true;
		} else {
			// Initialize the actor as real player
			Vector2f spawnPosition = GetSpawnPoint(peerDesc->PreferredPlayerType, peerDesc->Team);
			// In team-coloring modes, resolve the team before activating so the spawn-time recolor decision loads the
			// sprites indexed even for default-color players (see the join spawn above); ApplyGameModeToPlayer below
			// re-resolves to the same team and refreshes afterwards.
			if (serverConfig.ColorizePlayersByTeam && IsTeamGameMode(serverConfig.GameMode)) {
				peerDesc->Team = ResolveTeam(ptr, peerDesc->PreferredTeam);
			}
			std::uint8_t playerParams[2] = { (std::uint8_t)peerDesc->PreferredPlayerType, (std::uint8_t)playerIndex };
			ptr->OnActivated(Actors::ActorActivationDetails(this,
				Vector3i((std::int32_t)spawnPosition.X, (std::int32_t)spawnPosition.Y, PlayerZ - playerIndex),
				playerParams));
			ptr->_health = (serverConfig.InitialPlayerHealth > 0
				? serverConfig.InitialPlayerHealth
				: (PlayerShouldHaveUnlimitedHealth(serverConfig.GameMode) ? INT32_MAX : 5));

			if (_levelState == LevelState::Running && serverConfig.JoinCooldownSecs > 0) {
				peerDesc->JoinCooldownFrames = serverConfig.JoinCooldownSecs * FrameTimer::FramesPerSecond;
				player->_controllableExternal = false;
			} else {
				peerDesc->JoinCooldownFrames = 0.0f;
				player->_controllableExternal = _controllableExternal;
			}
		}

		peerDesc->Player = ptr;
		_players.push_back(ptr);

		_suppressRemoting = true;
		AddActor(newPlayer);
		_suppressRemoting = false;

		// First, create new controllable player if needed, then create remote actor for other clients
		if (peerDesc->RemotePeer) {
			// Remote client
			std::uint8_t flags = 0x10; // Skip fade-in transition
			if (ptr->_controllable) {
				flags |= 0x01;
			}
			if (ptr->_controllableExternal) {
				flags |= 0x02;
			}

			MemoryStream packet2(16);
			packet2.WriteVariableUint32(playerIndex);
			packet2.WriteValue<std::uint8_t>((std::uint8_t)ptr->_playerType);
			packet2.WriteVariableInt32(ptr->_health);
			packet2.WriteValue<std::uint8_t>(flags);
			packet2.WriteValue<std::uint8_t>(peerDesc->Team);
			packet2.WriteVariableInt32((std::int32_t)ptr->_pos.X);
			packet2.WriteVariableInt32((std::int32_t)ptr->_pos.Y);
			// Forward carried-over progression to the client (flag = 0 if none). The server-side player applies
			// it in MpPlayer::OnActivatedAsync (after the base reset); clear the flag once serialized.
			packet2.WriteValue<std::uint8_t>(peerDesc->HasCarryOver ? 1 : 0);
			if (peerDesc->HasCarryOver) {
				WriteCarryOver(packet2, peerDesc->CarryOver);
			}
			peerDesc->HasCarryOver = false;

			_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateControllablePlayer, packet2);
		} else {
			// Local player
			UnassignViewport(player);
			AssignViewport(ptr);
			CommitViewports();
		}

		MemoryStream packet3;
		InitializeCreateRemoteActorPacket(packet3, playerIndex, ptr);

		_networkManager->SendTo([this, self = peerDesc->RemotePeer](const Peer& peer) {
			if (peer == self) {
				return false;
			}
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet3);

		MemoryStream packet4(11 + peerDesc->PlayerName.size());
		packet4.WriteVariableUint32(playerIndex);
		packet4.WriteValue<std::uint8_t>(0x02 | 0x04); // HasFurColor | HasTeam
		packet4.WriteVariableUint32(peerDesc->PlayerName.size());
		packet4.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());
		packet4.WriteValueAsLE<std::uint32_t>(peerDesc->FurColor);
		packet4.WriteValue<std::uint8_t>(peerDesc->Team);

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet4);

		if (peerDesc->RemotePeer) {
			MemoryStream packet5(6);
			packet5.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Spectate);
			packet5.WriteVariableUint32(playerIndex);
			packet5.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->IsSpectating);
			_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet5);
		}

		if ((mode & SpectateMode::Mask) == SpectateMode::None) {
			// Player is now active again - apply game mode effects
			ApplyGameModeToPlayer(serverConfig.GameMode, ptr);

			// The player is invulnerable for a short time after spawning
			ptr->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Blinking);
		}
	}

	void MpLevelHandler::RequestSpectateMode(bool enable)
	{
		if DEATH_UNLIKELY(_players.empty()) {
			return;
		}

		if (_isServer) {
			for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
				if (!peerDesc->RemotePeer && peerDesc->Player) {
					SetPlayerSpectateMode(peerDesc->Player, enable ? SpectateMode::Requested : SpectateMode::None);
				}
			}
		} else {
			MemoryStream packet(5);
			packet.WriteVariableUint32(_lastSpawnedActorId);
			packet.WriteValue<std::uint8_t>(enable ? 1 : 0);
			_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerSpectateRequest, packet);
		}
	}

	bool MpLevelHandler::IsSpectating()
	{
		for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
			if (!peerDesc->RemotePeer && peerDesc->Player) {
				return (peerDesc->IsSpectating != SpectateMode::None);
			}
		}
		return false;
	}

	bool MpLevelHandler::IsSpectateAvailable() const
	{
		return _networkManager->GetServerConfiguration().EnableSpectate;
	}

	void MpLevelHandler::ShowCharacterSelectLobby()
	{
		if DEATH_UNLIKELY(_inGameLobby == nullptr) {
			return;
		}

		// Open the lobby so the player can pick a (possibly different) character; SetPlayerReady() then (re)joins
		// the game with the selected type instead of treating it as the initial join.
		_changingCharacterInLobby = true;
		auto& serverConfig = _networkManager->GetServerConfiguration();
		_inGameLobby->SetAllowedPlayerTypes(serverConfig.AllowedPlayerTypes);
		std::uint8_t currentTeam = NoPreferredTeam;
		if (auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer)) {
			currentTeam = peerDesc->Team;
		}
		_inGameLobby->SetTeamInfo(currentTeam);
		_inGameLobby->Show();
	}

	bool MpLevelHandler::OnPeerDisconnected(const Peer& peer)
	{
		constexpr Vector2f OutOfBounds = Vector2f(-1000000.0f, -1000000.0f);

		if (_isServer) {
			if (auto peerDesc = _networkManager->GetPeerDescriptor(peer)) {
				peerDesc->IsAuthenticated = false;
				peerDesc->LevelState = PeerLevelState::Unknown;

				InvokeAsync([this, peerDesc]() mutable {
					_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]{}\f[/c] disconnected", peerDesc->PlayerName));
				});

				MemoryStream packet(10 + peerDesc->PlayerName.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)PeerPropertyType::Disconnected);
				packet.WriteVariableUint64(peer.GetId());
				packet.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
				packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());

				_networkManager->SendTo([otherPeer = peer](const Peer& peer) {
					return (peer != otherPeer);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PeerSetProperty, packet);

				if (MpPlayer* player = peerDesc->Player) {
					std::int32_t playerIndex = player->_playerIndex;
					Vector2f pos = player->_pos;

					// Snapshot the player's progression so it can be restored if the peer reconnects within the
					// retention window (NetworkManager keeps the descriptor by unique player ID after disconnect).
					// Call Player::PrepareLevelCarryOver() directly - RemotePlayerOnServer overrides it to return an
					// empty struct, but the server tracks the real score/lives/weapons for it.
					peerDesc->CarryOver = player->Player::PrepareLevelCarryOver();
					peerDesc->HasCarryOver = true;
					peerDesc->DisconnectedSince = TimeStamp::now();

					for (std::size_t i = 0; i < _players.size(); i++) {
						if (_players[i] == player) {
							_players.eraseUnordered(i);
							break;
						}
					}

					// Move the player out of the bounds to avoid triggering events
					player->_pos = OutOfBounds;
					player->SetState(Actors::ActorState::IsDestroyed, true);

					MemoryStream packet(4);
					packet.WriteVariableUint32(playerIndex);

					_networkManager->SendTo([otherPeer = peer](const Peer& peer) {
						return (peer != otherPeer);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::DestroyRemoteActor, packet);

					const auto& serverConfig = _networkManager->GetServerConfiguration();
					if (_levelState == LevelState::WaitingForMinPlayers) {
						std::int32_t playerCount = GetNonSpectatePlayerCount();
						_waitingForPlayerCount = (std::int32_t)serverConfig.MinPlayerCount - playerCount;
						InvokeAsync([this]() {
							SendLevelStateToAllPlayers();
						});
					} else if (_levelState == LevelState::Running && (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt)) {
						// Drop all collected treasure
						if (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt) {
							std::uint32_t treasureLost = peerDesc->TreasureCollected;
							if (treasureLost > 0) {
								peerDesc->TreasureCollected = 0;

								InvokeAsync([this, treasureLost, pos]() {
									for (std::uint32_t i = 0; i < treasureLost; i++) {
										float dir = (Random().NextBool() ? -1.0f : 1.0f);
										float force = Random().Next(10.0f, 20.0f);
										Vector3f spawnPos = Vector3f(pos.X, pos.Y, MainPlaneZ);
										std::uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] = { 0, 0x04 };
										auto actor = _eventSpawner.SpawnEvent(EventType::Gem, spawnParams, Actors::ActorState::None, spawnPos.As<std::int32_t>());
										if (actor != nullptr) {
											actor->AddExternalForce(dir * force, force);
											AddActor(actor);
										}
									}
								});
							}
						}

						if (_autoWeightTreasure) {
							SynchronizeGameMode();
						}
					}

					// A player leaving a team mode can unbalance the teams; rebalance if enabled
					if (_levelState == LevelState::Running && IsTeamGameMode(serverConfig.GameMode)) {
						InvokeAsync([this]() {
							RebalanceTeams(false);
						});
					}

					if (_activeBoss != nullptr && _nextLevelType == ExitType::None) {
						// If a boss is active and the last player left, roll everything back
						bool anyoneAlive = false;
						for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
							if (peerDesc->Player && peerDesc->Player != player && peerDesc->Player->_health > 0) {
								anyoneAlive = true;
								break;
							}
						}

						if (!anyoneAlive) {
							Vector2f checkpointPos = player->_checkpointPos;
							InvokeAsync([this, player, checkpointPos]() {
								if (_activeBoss->OnPlayerDied()) {
									_activeBoss = nullptr;
								}

								LimitCameraView(nullptr, checkpointPos, 0, 0);

								LOGI("Rolling back to checkpoint");
								for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
									if (peerDesc->Player && peerDesc->Player != player) {
										peerDesc->Player->Respawn(checkpointPos);

										if (peerDesc->RemotePeer) {
											peerDesc->LastUpdated = UINT64_MAX;
											static_cast<PlayerOnServer*>(peerDesc->Player)->_canTakeDamage = false;

											MemoryStream packet2(12);
											packet2.WriteVariableUint32(peerDesc->Player->_playerIndex);
											packet2.WriteValue<std::int32_t>((std::int32_t)(checkpointPos.X * 512.0f));
											packet2.WriteValue<std::int32_t>((std::int32_t)(checkpointPos.Y * 512.0f));
											_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet2);
										}
									}
								}

								RollbackLevelState();
								BeginPlayMusic(_musicDefaultPath);
							});
						}
					}
				}

				CalculatePositionInRound();
			}
			return true;
		}

		return false;
	}


	bool MpLevelHandler::OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
		if DEATH_UNLIKELY(_ignorePackets) {
			// IRootController is probably going to load a new level in a moment, so ignore all packets now
			return false;
		}

		if (_isServer) {
			switch ((ClientPacketType)packetType) {
				case ClientPacketType::Rpc: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();

					// TODO: remotingActor->OnPacketReceived() is also locked here, which is not good
					std::unique_lock lock(_lock);
					for (const auto& [remotingActor, remotingActorInfo] : _remotingActors) {
						if (remotingActorInfo.ActorID == actorId) {
							LOGD("[MP] ClientPacketType::Rpc [{}] - id: {}, {} bytes", peer, actorId, data.size() - packet.GetPosition());
							remotingActor->OnPacketReceived(packet);
							return true;
						}
					}

					LOGW("[MP] ClientPacketType::Rpc [{}] - id: {}, {} bytes - Actor not found", peer, actorId, data.size() - packet.GetPosition());
					return true;
				}
				case ClientPacketType::Auth: {
					if (auto peerDesc = _networkManager->GetPeerDescriptor(peer)) {
						peerDesc->LevelState = PeerLevelState::ValidatingAssets;

						InvokeAsync([this, peerDesc]() mutable {
							_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]{}\f[/c] connected", peerDesc->PlayerName));
						});

						MemoryStream packet(10 + peerDesc->PlayerName.size());
						packet.WriteValue<std::uint8_t>((std::uint8_t)PeerPropertyType::Connected);
						packet.WriteVariableUint64(peer.GetId());
						packet.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
						packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());

						_networkManager->SendTo([otherPeer = peer](const Peer& peer) {
							return (peer != otherPeer);
						}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PeerSetProperty, packet);
					}

					MemoryStream packet;
					InitializeValidateAssetsPacket(packet);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ValidateAssets, packet);
					return true;
				}
				case ClientPacketType::LevelReady: {
					MemoryStream packet(data);
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ClientPacketType::LevelReady [{}] - flags: 0x{:.2x}", peer, flags);

					if (auto peerDesc = _networkManager->GetPeerDescriptor(peer)) {
						bool enableLedgeClimb = (flags & 0x02) != 0;
						peerDesc->EnableLedgeClimb = enableLedgeClimb;
						if (peerDesc->LevelState < PeerLevelState::LevelLoaded) {
							peerDesc->LevelState = PeerLevelState::LevelLoaded;
						}

						if (peerDesc->PreferredPlayerType == PlayerType::None) {
							auto& serverConfig = _networkManager->GetServerConfiguration();

							// Show in-game lobby only to newly connected players
							std::uint8_t flags = 0x01 | 0x02 | 0x04; // Set Visibility | Show | SetWelcomeMessage
							std::uint8_t allowedCharacters = serverConfig.AllowedPlayerTypes;

							MemoryStream packet(6 + serverConfig.WelcomeMessage.size());
							packet.WriteValue<std::uint8_t>(flags);
							packet.WriteValue<std::uint8_t>(allowedCharacters);
							packet.WriteVariableUint32(serverConfig.WelcomeMessage.size());
							packet.Write(serverConfig.WelcomeMessage.data(), serverConfig.WelcomeMessage.size());

							_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ShowInGameLobby, packet);
						}
					}
					return true;
				}
				case ClientPacketType::ChatMessage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::ChatMessage [{}] - Invalid playerIndex ({})", peer, playerIndex);
						return true;
					}

					/*std::uint8_t reserved =*/ packet.ReadValue<std::uint8_t>();

					std::uint32_t lineLength = packet.ReadVariableUint32();
					if (lineLength == 0 || lineLength > 1024) {
						LOGD("[MP] ClientPacketType::ChatMessage [{}] - Length out of bounds ({})", peer, lineLength);
						return true;
					}

					String line{NoInit, lineLength};
					packet.Read(line.data(), lineLength);

					if (line.hasPrefix('/')) {
						SendMessage(peer, UI::MessageLevel::Echo, line);
						ProcessCommand(peer, line, peerDesc->IsAdmin);
						return true;
					}

					String prefixedMessage;
					if (peerDesc->IsAdmin) {
						prefixedMessage = "\f[c:#907060]"_s + peerDesc->PlayerName + ":\f[/c] "_s + line;
					} else {
						prefixedMessage = "\f[c:#709060]"_s + peerDesc->PlayerName + ":\f[/c] "_s + line;
					}

					MemoryStream packetOut(9 + prefixedMessage.size());
					packetOut.WriteVariableUint32(playerIndex);
					packetOut.WriteValue<std::uint8_t>((std::uint8_t)UI::MessageLevel::Chat);
					packetOut.WriteVariableUint32((std::uint32_t)prefixedMessage.size());
					packetOut.Write(prefixedMessage.data(), (std::uint32_t)prefixedMessage.size());

					_networkManager->SendTo([this](const Peer& peer) {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						return (peerDesc && peerDesc->LevelState != PeerLevelState::Unknown);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packetOut);

					InvokeAsync([this, line = std::move(prefixedMessage)]() mutable {
						_console->WriteLine(UI::MessageLevel::Chat, std::move(line));
					});
					return true;
				}
				case ClientPacketType::ValidateAssetsResponse: {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->LevelState != PeerLevelState::ValidatingAssets) {
						// Packet received when the peer is in different state
						LOGW("[MP] ClientPacketType::ValidateAssetsResponse [{}] - Invalid state ({})", peer, peerDesc->LevelState);
						return true;
					}

					peerDesc->LevelState = PeerLevelState::StreamingMissingAssets;

					bool success = true;
					SmallVector<RequiredAsset*> missingAssets;

					MemoryStream packet(data);
					std::uint32_t assetCount = packet.ReadVariableUint32();

					LOGD("[MP] ClientPacketType::ValidateAssetsResponse [{}] - {}/{} assets", peer, assetCount, _requiredAssets.size());

					if (assetCount == _requiredAssets.size()) {
						for (std::uint32_t i = 0; i < assetCount; i++) {
							AssetType type = (AssetType)packet.ReadValue<std::uint8_t>();
							std::uint32_t pathLength = packet.ReadVariableUint32();
							String path{NoInit, pathLength};
							packet.Read(path.data(), pathLength);
							std::int64_t size = packet.ReadVariableInt64();
							std::uint32_t crc32 = packet.ReadValue<std::uint32_t>();

							bool found = false;
							for (std::size_t j = 0; j < _requiredAssets.size(); j++) {
								if (type == _requiredAssets[j].Type && path == _requiredAssets[j].Path) {
									found = true;
									if (size != _requiredAssets[j].Size || crc32 != _requiredAssets[j].Crc32) {
										LOGD("[MP] ClientPacketType::ValidateAssetsResponse [{}] - \"{}\":{:.8x} is missing",
											peer, _requiredAssets[j].Path, _requiredAssets[j].Crc32);
										missingAssets.push_back(&_requiredAssets[j]);
									}
									break;
								}
							}

							if (!found) {
								// This asset wasn't requested, something went wrong
								success = false;
							}
						}
					} else {
						// Asset count mismatch, something went wrong
						success = false;
					}

					if (!success) {
						// Peer response is corrupted
						LOGW("[MP] ClientPacketType::ValidateAssetsResponse [{}] - Malformed packet", peer);
						_networkManager->Kick(peer, Reason::InvalidParameter);
						return true;
					}

					LOGI("[MP] ClientPacketType::ValidateAssetsResponse [{}] - {} missing assets", peer, missingAssets.size());

					if (missingAssets.empty()) {
						// All assets are already ready
						MemoryStream packet;
						InitializeLoadLevelPacket(packet);
						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LoadLevel, packet);
						return true;
					}

					const auto& serverConfig = _networkManager->GetServerConfiguration();
					if (!serverConfig.AllowAssetStreaming) {
						// Server doesn't allow downloads, kick the client instead
						_networkManager->Kick(peer, Reason::AssetStreamingNotAllowed);
						return true;
					}
					
#if defined(WITH_THREADS)
					Thread streamingThread([_this = runtime_cast<MpLevelHandler>(shared_from_this()), peer, peerDesc = std::move(peerDesc), missingAssets = std::move(missingAssets)]() {
#else
					auto* _this = this;
#endif
						LOGI("Started streaming {} assets to peer [{}]", missingAssets.size(), peer);
						TimeStamp begin = TimeStamp::now();

						for (std::uint32_t i = 0; i < (std::uint32_t)missingAssets.size(); i++) {
							if (!peerDesc->IsAuthenticated || _this->_ignorePackets) {
								// Stop streaming if peer disconnects or handler has changed
								break;
							}

							const RequiredAsset& asset = *missingAssets[i];

							MemoryStream packetBegin(14 + asset.Path.size());
							packetBegin.WriteValue<std::uint8_t>(1);	// Begin
							packetBegin.WriteValue<std::uint8_t>((std::uint8_t)asset.Type);
							packetBegin.WriteVariableUint32((std::uint32_t)asset.Path.size());
							packetBegin.Write(asset.Path.data(), (std::int64_t)asset.Path.size());
							packetBegin.WriteVariableInt64(asset.Size);
							_this->_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::StreamAsset, packetBegin);

							auto s = fs::Open(asset.FullPath, FileAccess::Read);
							if (s->IsValid()) {
								char buffer[8192];
								while (true) {
									if (!peerDesc->IsAuthenticated || _this->_ignorePackets) {
										// Stop streaming if peer disconnects or handler has changed
										break;
									}

									std::int64_t bytesRead = s->Read(buffer, sizeof(buffer));
									if (bytesRead <= 0) {
										break;
									}

									MemoryStream packetChunk(9 + bytesRead);
									packetChunk.WriteValue<std::uint8_t>(2);	// Chunk
									packetChunk.WriteVariableInt64(bytesRead);
									packetChunk.Write(buffer, bytesRead);
									_this->_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::StreamAsset, packetChunk);
								}
							}

							MemoryStream packetEnd(1);
							packetEnd.WriteValue<std::uint8_t>(3);	// End
							_this->_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::StreamAsset, packetEnd);
						}

						LOGI("Finished streaming {} assets to peer [{}] - took {:.1f} ms",
						missingAssets.size(), peer, begin.millisecondsSince());
						if (peerDesc->IsAuthenticated && !_this->_ignorePackets) {
							MemoryStream packet;
							_this->InitializeLoadLevelPacket(packet);
							_this->_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LoadLevel, packet);
						}
#if defined(WITH_THREADS)
					});
#endif

					return true;
				}
				case ClientPacketType::PlayerReady: {
					MemoryStream packet(data);
					PlayerType preferredPlayerType = (PlayerType)packet.ReadValue<std::uint8_t>();
					std::uint8_t preferredTeam = packet.ReadValue<std::uint8_t>();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->LevelState != PeerLevelState::LevelLoaded && peerDesc->LevelState != PeerLevelState::LevelSynchronized) {
						LOGW("[MP] ClientPacketType::PlayerReady [{}] - Invalid state", peer);
						return true;
					}

					if (preferredPlayerType != PlayerType::Jazz && preferredPlayerType != PlayerType::Spaz && preferredPlayerType != PlayerType::Lori) {
						LOGW("[MP] ClientPacketType::PlayerReady [{}] - Invalid preferred player type", peer);
						return true;
					}

					peerDesc->PreferredPlayerType = preferredPlayerType;
					// Honor the requested team unless it was forced by the server (admin/rebalance); ResolveTeam()
					// applies it (balanced) when the player is spawned in ApplyGameModeToPlayer()
					if (!peerDesc->TeamLocked) {
						peerDesc->PreferredTeam = preferredTeam;
					}

					LOGI("[MP] ClientPacketType::PlayerReady [{}] - type: {}, team: {}", peer, preferredPlayerType, preferredTeam);

					InvokeAsync([this, peer]() {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						if (!peerDesc || peerDesc->LevelState < PeerLevelState::LevelLoaded || peerDesc->LevelState > PeerLevelState::LevelSynchronized) {
							LOGW("[MP] ClientPacketType::PlayerReady [{}] - Invalid state", peer);
							return;
						}
						if (peerDesc->LevelState == PeerLevelState::LevelSynchronized) {
							peerDesc->LevelState = PeerLevelState::PlayerReady;
						}
					});
					return true;
				}
				case ClientPacketType::ForceResyncActors: {
					LOGD("[MP] ClientPacketType::ForceResyncActors [{}] - update: {}", peer, _lastUpdated);
					_forceResyncPending = true;
					return true;
				}
				case ClientPacketType::PlayerUpdate: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerUpdate [{}] - Invalid playerIndex ({})", peer, playerIndex);
						return true;
					}

					std::uint64_t now = packet.ReadVariableUint64();
					if DEATH_UNLIKELY(peerDesc->LastUpdated >= now) {
						return true;
					}

					peerDesc->LastUpdated = now;

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float speedX = packet.ReadValue<std::int16_t>() / 512.0f;
					float speedY = packet.ReadValue<std::int16_t>() / 512.0f;
					RemotePlayerOnServer::PlayerFlags flags = (RemotePlayerOnServer::PlayerFlags)packet.ReadVariableUint32();

					// TODO: Special move

					if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(peerDesc->Player)) {
						// Anti-cheat: reject client-reported movement that is physically impossible
						// (speedhack / teleport). Bounds are intentionally generous so latency, springs, sugar
						// rush and similar legitimate bursts never trip them; only gross violations are corrected.
						constexpr float MaxPlausibleSpeed = 32.0f;	// Per axis; normal clamp is 16, boosted states stay well under
						constexpr float MaxPlausibleStep = 600.0f;	// Max accepted position change in a single update (px)

						bool corrected = false;
						if (std::abs(speedX) > MaxPlausibleSpeed || std::abs(speedY) > MaxPlausibleSpeed) {
							LOGW("Clamped implausible speed from player {} ({:.1f}, {:.1f})", playerIndex, speedX, speedY);
							speedX = std::clamp(speedX, -MaxPlausibleSpeed, MaxPlausibleSpeed);
							speedY = std::clamp(speedY, -MaxPlausibleSpeed, MaxPlausibleSpeed);
							corrected = true;
						}

						// Skip the teleport check while warping, which legitimately moves a player far at once
						if (!remotePlayerOnServer->_justWarped) {
							float stepDistSqr = (Vector2f(posX, posY) - remotePlayerOnServer->_pos).SqrLength();
							if (stepDistSqr > MaxPlausibleStep * MaxPlausibleStep) {
								LOGW("Rejected implausible teleport from player {} ({} px in one update)",
									playerIndex, (std::int32_t)std::sqrt(stepDistSqr));
								posX = remotePlayerOnServer->_pos.X;
								posY = remotePlayerOnServer->_pos.Y;
								corrected = true;
							}
						}

						if (corrected) {
							// Snap the offending client back to the accepted authoritative state
							MemoryStream packet2(20);
							packet2.WriteVariableUint32(remotePlayerOnServer->_playerIndex);
							packet2.WriteValue<std::int32_t>((std::int32_t)(posX * 512.0f));
							packet2.WriteValue<std::int32_t>((std::int32_t)(posY * 512.0f));
							packet2.WriteValue<std::int16_t>((std::int16_t)(speedX * 512.0f));
							packet2.WriteValue<std::int16_t>((std::int16_t)(speedY * 512.0f));
							packet2.WriteValue<std::int16_t>((std::int16_t)(remotePlayerOnServer->_externalForce.X * 512.0f));
							packet2.WriteValue<std::int16_t>((std::int16_t)(remotePlayerOnServer->_externalForce.Y * 512.0f));
							_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerMoveInstantly, packet2);
						}

						bool wasIdle = (remotePlayerOnServer->Flags & (RemotePlayerOnServer::PlayerFlags::InMenu | RemotePlayerOnServer::PlayerFlags::InMenu)) != RemotePlayerOnServer::PlayerFlags::None;
						bool isIdle = (flags & (RemotePlayerOnServer::PlayerFlags::InMenu | RemotePlayerOnServer::PlayerFlags::InMenu)) != RemotePlayerOnServer::PlayerFlags::None;

						remotePlayerOnServer->SyncWithServer(Vector2f(posX, posY), Vector2f(speedX, speedY), flags);

						if (wasIdle != isIdle) {
							// Broadcast idle state to all other players
							MemoryStream packet2(6);
							packet2.WriteVariableUint32(playerIndex);
							packet2.WriteValue<std::uint8_t>(isIdle ? 0x01 : 0x00);
							packet2.WriteVariableUint32(0);

							_networkManager->SendTo([this, self = peer](const Peer& peer) {
								if (peer == self) {
									return false;
								}
								auto peerDesc = _networkManager->GetPeerDescriptor(peer);
								return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
							}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet2);
						}
					}
					return true;
				}
				case ClientPacketType::PlayerKeyPress: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						return true;
					}
					
					if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(peerDesc->Player)) {
						std::uint32_t frameCount = theApplication().GetFrameCount();
						if (remotePlayerOnServer->UpdatedFrame != frameCount) {
							remotePlayerOnServer->UpdatedFrame = frameCount;
							remotePlayerOnServer->PressedKeysLast = remotePlayerOnServer->PressedKeys;
						}
						remotePlayerOnServer->PressedKeys = packet.ReadVariableUint64();
					}

					//LOGD("Player {} pressed 0x{:.8x}, last state was 0x{:.8x}", playerIndex, it->second.PressedKeys & 0xffffffffu, prevState);
					return true;
				}
				case ClientPacketType::PlayerChangeWeaponRequest: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerChangeWeaponRequest [{}] - Invalid playerIndex ({})", peer, playerIndex);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ClientPacketType::PlayerChangeWeaponRequest [{}] - playerIndex: {}, weaponType: {}", peer, playerIndex, weaponType);

					const auto& playerAmmo = peerDesc->Player->GetWeaponAmmo();
					if (weaponType >= playerAmmo.size() || playerAmmo[weaponType] == 0) {
						LOGD("[MP] ClientPacketType::PlayerChangeWeaponRequest [{}] - playerIndex: {} - No ammo in selected weapon", peer, playerIndex);

						// Request is denied, send the current weapon back to the client
						HandlePlayerWeaponChanged(peerDesc->Player, Actors::Player::SetCurrentWeaponReason::Rollback);
						return true;
					}

					peerDesc->Player->SetCurrentWeapon((WeaponType)weaponType, Actors::Player::SetCurrentWeaponReason::User);
					return true;
				}
				case ClientPacketType::PlayerSpectateRequest: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					std::uint8_t enable = packet.ReadValue<std::uint8_t>();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerSpectateRequest [{}] - Invalid playerIndex ({})", peer, playerIndex);
						return true;
					}

					LOGD("[MP] ClientPacketType::PlayerSpectateRequest [{}] - playerIndex: {}, enable: {}", peer, playerIndex, enable);

					auto& serverConfig = _networkManager->GetServerConfiguration();
					if (!serverConfig.EnableSpectate || ((peerDesc->IsSpectating & SpectateMode::Mask) == SpectateMode::Forced && enable == 0)) {
						// Spectate mode is disabled or forced, deny the request
						return true;
					}

					LOGI("Player {} [{}] {} spectating", playerIndex, peer, enable ? "started"_s : "stopped"_s);
					SetPlayerSpectateMode(peerDesc->Player, enable ? SpectateMode::Requested : SpectateMode::None);
					return true;
				}
				case ClientPacketType::PlayerChangeCharacter: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					PlayerType playerType = (PlayerType)packet.ReadValue<std::uint8_t>();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerChangeCharacter [{}] - Invalid playerIndex ({})", peer, playerIndex);
						return true;
					}

					if (playerType != PlayerType::Jazz && playerType != PlayerType::Spaz && playerType != PlayerType::Lori) {
						LOGW("[MP] ClientPacketType::PlayerChangeCharacter [{}] - Invalid player type", peer);
						return true;
					}

					auto& serverConfig = _networkManager->GetServerConfiguration();
					if ((peerDesc->IsSpectating & SpectateMode::Mask) == SpectateMode::Forced) {
						// Forced spectators (e.g., winner, eliminated) can't rejoin by changing character
						LOGD("[MP] ClientPacketType::PlayerChangeCharacter [{}] - Forced to spectate", peer);
						return true;
					}

					std::uint8_t typeBit = (std::uint8_t)(1 << ((std::int32_t)playerType - (std::int32_t)PlayerType::Jazz));
					if ((serverConfig.AllowedPlayerTypes & typeBit) == 0) {
						LOGW("[MP] ClientPacketType::PlayerChangeCharacter [{}] - Player type {} not allowed", peer, playerType);
						return true;
					}

					LOGI("Player {} [{}] changing character to {}", playerIndex, peer, playerType);

					// Respawn the player with the chosen character (also leaves spectate mode if currently spectating)
					peerDesc->PreferredPlayerType = playerType;
					SetPlayerSpectateMode(peerDesc->Player, SpectateMode::None);
					return true;
				}
				case ClientPacketType::PlayerChangeTeamRequest: {
					MemoryStream packet(data);
					std::uint8_t requestedTeam = packet.ReadValue<std::uint8_t>();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc == nullptr || peerDesc->Player == nullptr) {
						LOGD("[MP] ClientPacketType::PlayerChangeTeamRequest [{}] - No player", peer);
						return true;
					}

					LOGI("Player {} [{}] requesting team change to {}", peerDesc->Player->_playerIndex, peer, requestedTeam);

					InvokeAsync([this, peer, requestedTeam]() {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						if (peerDesc == nullptr || peerDesc->Player == nullptr) {
							return;
						}
						if (ChangePlayerTeam(peerDesc->Player, requestedTeam, false)) {
							SendMessage(peer, UI::MessageLevel::Info, _f("You joined the {} team", GetTeamName(peerDesc->Team)));
						} else {
							SendMessage(peer, UI::MessageLevel::Warning, _("Cannot change team right now"));
						}
					});
					return true;
				}
				case ClientPacketType::PlayerAckWarped: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					std::uint64_t seqNum = packet.ReadVariableUint64();
					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float speedX = packet.ReadValue<std::int16_t>() / 512.0f;
					float speedY = packet.ReadValue<std::int16_t>() / 512.0f;

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerAckWarped [{}] - Invalid playerIndex ({})", peer, playerIndex);
						return true;
					}

					LOGD("[MP] ClientPacketType::PlayerAckWarped [{}] - playerIndex: {}, seqNum: {}, x: {}, y: {}",
						peer, playerIndex, seqNum, posX, posY);

					peerDesc->LastUpdated = seqNum;
					if (auto* mpPlayer = static_cast<RemotePlayerOnServer*>(peerDesc->Player)) {
						mpPlayer->ForceResyncWithServer(Vector2f(posX, posY), Vector2f(speedX, speedY));
						mpPlayer->_canTakeDamage = true;
						mpPlayer->_justWarped = true;
					}
					break;
				}
			}
		} else {
			switch ((ServerPacketType)packetType) {
				case ServerPacketType::Rpc: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();

					std::shared_ptr<Actors::ActorBase> actor;
					{
						std::unique_lock lock(_lock);
						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							actor = it->second;
						}
					}
					if (actor) {
						LOGD("[MP] ServerPacketType::Rpc - id: {}, {} bytes", actorId, data.size() - packet.GetPosition());
						actor->OnPacketReceived(packet);
					} else {
						LOGW("[MP] ServerPacketType::Rpc - id: {}, {} bytes - Actor not found", actorId,data.size() - packet.GetPosition());
					}
					return true;
				}
				case ServerPacketType::PeerSetProperty: {
					MemoryStream packet(data);
					PeerPropertyType type = (PeerPropertyType)packet.ReadValue<std::uint8_t>();
					DEATH_UNUSED std::uint64_t peerId = packet.ReadVariableUint64();

					switch (type) {
						case PeerPropertyType::Connected:
						case PeerPropertyType::Disconnected: {
							std::uint8_t playerNameLength = packet.ReadValue<std::uint8_t>();
							String playerName{NoInit, playerNameLength};
							packet.Read(playerName.data(), playerNameLength);

							LOGD("[MP] ServerPacketType::PeerSetProperty - type: {}, peer: 0x{:.8x}, name: \"{}\"",
								type, peerId, playerName);

							InvokeAsync([this, type, playerName = std::move(playerName)]() mutable {
								if (type == PeerPropertyType::Connected) {
									_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]{}\f[/c] connected", playerName));
								} else {
									_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]{}\f[/c] disconnected", playerName));
								}
							});
							break;
						}
						case PeerPropertyType::Roasted: {
							std::uint8_t victimNameLength = packet.ReadValue<std::uint8_t>();
							String victimName{NoInit, victimNameLength};
							packet.Read(victimName.data(), victimNameLength);

							DEATH_UNUSED std::uint64_t attackerPeerId = packet.ReadVariableUint64();

							std::uint8_t attackerNameLength = packet.ReadValue<std::uint8_t>();
							String attackerName{NoInit, attackerNameLength};
							packet.Read(attackerName.data(), attackerNameLength);

							LOGD("[MP] ServerPacketType::PeerSetProperty - type: {}, victim: 0x{:.8x}, victim-name: \"{}\", attacker: 0x{:.8x}, attacker-name: \"{}\"",
								type, peerId, victimName, attackerPeerId, attackerName);

							InvokeAsync([this, victimName = std::move(victimName), attackerName = std::move(attackerName)]() mutable {
								if (!attackerName.empty()) {
									_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]{}\f[/c] was roasted by \f[c:#d0705d]{}\f[/c]",
										victimName, attackerName));
								} else {
									_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]{}\f[/c] was roasted by environment",
										victimName));
								}
							});
							break;
						}
					}
					break;
				}
				case ServerPacketType::LoadLevel: {
					// Start to ignore all incoming packets, because they no longer belong to this handler
					_ignorePackets = true;

					LOGD("[MP] ServerPacketType::LoadLevel");
					break;
				}
				case ServerPacketType::LevelSetProperty: {
					MemoryStream packet(data);
					LevelPropertyType propertyType = (LevelPropertyType)packet.ReadValue<std::uint8_t>();
					switch (propertyType) {
						case LevelPropertyType::State: {
							LevelState state = (LevelState)packet.ReadValue<std::uint8_t>();
							std::int32_t gameTimeLeft = packet.ReadVariableInt32();

							LOGD("[MP] ServerPacketType::LevelSetProperty::State - state: {}, timeLeft: {:.1f}", state, (float)gameTimeLeft * 0.01f);

							InvokeAsync([this, state, gameTimeLeft]() {
								_levelState = state;
								_gameTimeLeft = (float)gameTimeLeft * 0.01f;

								switch (_levelState) {
									case LevelState::WaitingForMinPlayers: {
										// gameTimeLeft is reused for waitingForPlayerCount in this state
										_waitingForPlayerCount = gameTimeLeft;
										break;
									}
									case LevelState::Countdown3: {
										static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(3);
										LOGI("Starting round...");
										break;
									}
									case LevelState::Countdown2: {
										static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(2);
										break;
									}
									case LevelState::Countdown1: {
										static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(1);
										break;
									}
									case LevelState::Running: {
										const auto& serverConfig = _networkManager->GetServerConfiguration();
										if (serverConfig.GameMode != MpGameMode::Cooperation) {
											static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(0);
										}
										break;
									}
								}
							});
							break;
						}
						case LevelPropertyType::GameMode: {
							std::uint8_t flags = packet.ReadValue<std::uint8_t>();
							MpGameMode gameMode = (MpGameMode)packet.ReadValue<std::uint8_t>();
							std::uint8_t teamCount = packet.ReadValue<std::uint8_t>();
							std::uint8_t teamId = packet.ReadValue<std::uint8_t>();
							std::uint8_t allowedPlayerTypes = packet.ReadValue<std::uint8_t>();
							std::int32_t initialPlayerHealth = packet.ReadVariableInt32();
							std::uint32_t maxGameTimeSecs = packet.ReadVariableUint32();
							std::uint32_t totalKills = packet.ReadVariableUint32();
							std::uint32_t totalLaps = packet.ReadVariableUint32();
							std::uint32_t totalTreasureCollected = packet.ReadVariableUint32();
							bool colorizePlayersByTeam = (packet.ReadValue<std::uint8_t>() != 0);

							LOGD("[MP] ServerPacketType::LevelSetProperty::GameMode - mode: {}", gameMode);

							auto& serverConfig = _networkManager->GetServerConfiguration();
							serverConfig.GameMode = gameMode;
							serverConfig.ColorizePlayersByTeam = colorizePlayersByTeam;
							serverConfig.ReforgedGameplay = (flags & 0x01) != 0;
							serverConfig.Elimination = (flags & 0x04) != 0;
							serverConfig.EnableSpectate = (flags & 0x08) != 0;
							serverConfig.PlayerStacking = (flags & 0x10) != 0;
							serverConfig.AllowTeamSelection = (flags & 0x20) != 0;
							serverConfig.FriendlyFire = (flags & 0x40) != 0;
							serverConfig.AutoBalanceTeams = (flags & 0x80) != 0;
							serverConfig.TeamCount = teamCount;
							serverConfig.AllowedPlayerTypes = allowedPlayerTypes;
							serverConfig.InitialPlayerHealth = initialPlayerHealth;
							serverConfig.MaxGameTimeSecs = maxGameTimeSecs;
							serverConfig.TotalKills = totalKills;
							serverConfig.TotalLaps = totalLaps;
							serverConfig.TotalTreasureCollected = totalTreasureCollected;

							_isReforged = serverConfig.ReforgedGameplay;

							if (auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer)) {
								peerDesc->Team = teamId;
							}

							// Re-apply team colors to all visible players now that the mode / team-coloring flag is
							// (re)synced (touches the renderer, so defer to the main thread; a no-op when disabled).
							// _playerNames is the set of remote players (the local players live in _players).
							InvokeAsync([this, gameMode]() {
								// Build the game-mode rules object so this client can draw the mode-specific HUD; the
								// game-mode logic itself stays server-authoritative.
								_gameMode = CreateGameMode(gameMode);

								for (auto* player : _players) {
									player->RefreshColorPalette();
								}
								for (auto& [actorId, playerName] : _playerNames) {
									RecolorRemoteActor(actorId, playerName.FurColor, playerName.Team);
								}
							});
							break;
						}
						case LevelPropertyType::Music: {
							std::uint8_t flags = packet.ReadValue<std::uint8_t>();
							std::uint32_t pathLength = packet.ReadVariableUint32();
							String path{NoInit, pathLength};
							packet.Read(path.data(), pathLength);

							LOGD("[MP] ServerPacketType::LevelSetProperty::Music - path: \"{}\", flags: {}", path, flags);

							InvokeAsync([this, flags, path = std::move(path)]() {
								bool setDefault = (flags & 0x01) != 0;
								bool forceReload = (flags & 0x02) != 0;
								BeginPlayMusic(path, setDefault, forceReload);
							});
							break;
						}
						default: {
							LOGD("[MP] ServerPacketType::LevelSetProperty - Received unknown property {}", (std::uint32_t)propertyType);
							break;
						}
					}
					return true;
				}
				case ServerPacketType::ShowInGameLobby: {
					auto& serverConfig = _networkManager->GetServerConfiguration();
					
					MemoryStream packet(data);
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::uint8_t allowedPlayerTypes = packet.ReadValue<std::uint8_t>();

					if (flags & 0x04) {
						// Welcome message
						std::uint32_t welcomeMessageLength = packet.ReadVariableUint32();
						serverConfig.WelcomeMessage = String(NoInit, welcomeMessageLength);
						packet.Read(serverConfig.WelcomeMessage.data(), welcomeMessageLength);
					}
					if (flags & 0x08) {
						// TODO: Welcome server logo
						std::uint32_t serverLogoLength = packet.ReadVariableUint32();
						Array<std::uint8_t> serverLogo{NoInit, serverLogoLength};
						packet.Read(serverLogo.data(), serverLogoLength);
					}

					LOGD("[MP] ServerPacketType::ShowInGameLobby - flags: 0x{:.2x}, allowedPlayerTypes: 0x{:.2x}, message: \"{}\"",
						flags, allowedPlayerTypes, serverConfig.WelcomeMessage);

					if (flags & 0x01) {
						InvokeAsync([this, flags, allowedPlayerTypes]() {
							if (_inGameLobby != nullptr) {
								_changingCharacterInLobby = false; // Server-driven lobby is the initial join, not a player-initiated change
								_inGameLobby->SetAllowedPlayerTypes(allowedPlayerTypes);
								std::uint8_t currentTeam = NoPreferredTeam;
								if (auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer)) {
									currentTeam = peerDesc->Team;
								}
								_inGameLobby->SetTeamInfo(currentTeam);
								if (flags & 0x02) {
									_inGameLobby->Show();
								} else {
									_inGameLobby->Hide();
								}
							}
						});
					}
					return true;
				}
				case ServerPacketType::FadeOut: {
					MemoryStream packet(data);
					std::int32_t fadeOutDelay = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::FadeOut - delay: {}", fadeOutDelay);

					InvokeAsync([this, fadeOutDelay]() {
						if (_hud != nullptr) {
							_hud->BeginFadeOut((float)fadeOutDelay);
						}

#if defined(WITH_AUDIO)
						if (_sugarRushMusic != nullptr) {
							_sugarRushMusic->stop();
							_sugarRushMusic = nullptr;
						}
						if (_music != nullptr) {
							_music->stop();
							_music = nullptr;
						}
#endif
					});
					return true;
				}
				case ServerPacketType::PlaySfx: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					float gain = halfToFloat(packet.ReadValue<std::uint16_t>());
					float pitch = halfToFloat(packet.ReadValue<std::uint16_t>());
					std::uint32_t identifierLength = packet.ReadVariableUint32();
					String identifier = String(NoInit, identifierLength);
					packet.Read(identifier.data(), identifierLength);

					// TODO: Use only lock here
					InvokeAsync([this, actorId, gain, pitch, identifier = std::move(identifier)]() {
						if (_lastSpawnedActorId == actorId) {
							if (!_players.empty()) {
								_players[0]->PlaySfx(identifier, gain, pitch);
							}
						} else {
							std::unique_lock lock(_lock);
							auto it = _remoteActors.find(actorId);
							if (it != _remoteActors.end()) {
								// TODO: gain, pitch, ...
								it->second->PlaySfx(identifier, gain, pitch);
							}
						}
					});
					return true;
				}
				case ServerPacketType::PlayCommonSfx: {
					MemoryStream packet(data);
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					float gain = halfToFloat(packet.ReadValue<std::uint16_t>());
					float pitch = halfToFloat(packet.ReadValue<std::uint16_t>());
					std::uint32_t identifierLength = packet.ReadVariableUint32();
					String identifier(NoInit, identifierLength);
					packet.Read(identifier.data(), identifierLength);

					// TODO: Use only lock here
					InvokeAsync([this, posX, posY, gain, pitch, identifier = std::move(identifier)]() {
						std::unique_lock lock(_lock);
						PlayCommonSfx(identifier, Vector3f((float)posX, (float)posY, 0.0f), gain, pitch);
					});
					return true;
				}
				case ServerPacketType::ShowAlert: {
					MemoryStream packet(data);
					/*std::uint8_t flags =*/ packet.ReadValue<std::uint8_t>();
					std::uint32_t textLength = packet.ReadVariableUint32();
					String text = String(NoInit, textLength);
					packet.Read(text.data(), textLength);

					LOGD("[MP] ServerPacketType::ShowAlert - text: \"{}\"", text);

					_hud->ShowLevelText(text);
					return true;
				}
				case ServerPacketType::ChatMessage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					UI::MessageLevel level = (UI::MessageLevel)packet.ReadValue<std::uint8_t>();

					std::uint32_t messageLength = packet.ReadVariableUint32();
					if (messageLength == 0 || messageLength > 1024) {
						LOGD("[MP] ServerPacketType::ChatMessage - Length out of bounds ({})", messageLength);
						return true;
					}

					String message{NoInit, messageLength};
					packet.Read(message.data(), messageLength);

					if (level == UI::MessageLevel::Chat && playerIndex == _lastSpawnedActorId) {
						level = UI::MessageLevel::Echo;
					}

					InvokeAsync([this, level, message = std::move(message)]() mutable {
						_console->WriteLine(level, std::move(message));
					});
					return true;
				}
				case ServerPacketType::SyncTileMap: {
					MemoryStream packet(data);

					LOGD("[MP] ServerPacketType::SyncTileMap");

					// TODO: No lock here ???
					TileMap()->InitializeFromStream(packet);
					return true;
				}
				case ServerPacketType::SetTrigger: {
					MemoryStream packet(data);
					std::uint8_t triggerId = packet.ReadValue<std::uint8_t>();
					bool newState = (bool)packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::SetTrigger - id: {}, state: {}", triggerId, newState);

					InvokeAsync([this, triggerId, newState]() {
						TileMap()->SetTrigger(triggerId, newState);
					});
					return true;
				}
				case ServerPacketType::AdvanceTileAnimation: {
					MemoryStream packet(data);
					std::int32_t tx = packet.ReadVariableInt32();
					std::int32_t ty = packet.ReadVariableInt32();
					std::int32_t amount = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::AdvanceTileAnimation - tx: {}, ty: {}, amount: {}", tx, ty, amount);

					InvokeAsync([this, tx, ty, amount]() {
						TileMap()->AdvanceDestructibleTileAnimation(tx, ty, amount);
					});
					return true;
				}
				case ServerPacketType::CreateDebris: {
					MemoryStream packet(data);
					std::uint8_t effect = packet.ReadValue<std::uint8_t>();
					std::uint32_t actorId = packet.ReadVariableUint32();

					LOGD("[MP] ServerPacketType::CreateDebris - effect: {}, actorId: {}", effect, actorId);

					if (effect == UINT8_MAX) {
						AnimState state = (AnimState)packet.ReadVariableUint32();
						std::int32_t count = packet.ReadVariableInt32();

						InvokeAsync([this, actorId, state, count]() {
							std::unique_lock lock(_lock);
							auto it = _remoteActors.find(actorId);
							if (it != _remoteActors.end()) {
								it->second->CreateSpriteDebris(state, count);
							} else {
								LOGD("[MP] ServerPacketType::CreateDebris - actorId: {} - Actor not found", actorId);
							}
						});
					} else {
						float x = packet.ReadVariableInt32() * 0.01f;
						float y = packet.ReadVariableInt32() * 0.01f;

						InvokeAsync([this, actorId, effect, x, y]() {
							std::unique_lock lock(_lock);
							auto it = _remoteActors.find(actorId);
							if (it != _remoteActors.end()) {
								it->second->CreateParticleDebrisOnPerish((Actors::ParticleDebrisEffect)effect, Vector2f(x, y));
							} else {
								LOGD("[MP] ServerPacketType::CreateDebris - actorId: {} - Actor not found", actorId);
							}
						});
					}
					return true;
				}
				case ServerPacketType::CreateControllablePlayer: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					PlayerType playerType = (PlayerType)packet.ReadValue<std::uint8_t>();
					std::int32_t health = packet.ReadVariableInt32();
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::uint8_t teamId = packet.ReadValue<std::uint8_t>();
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					bool hasCarryOver = (packet.ReadValue<std::uint8_t>() != 0);
					PlayerCarryOver carryOver{};
					if (hasCarryOver) {
						carryOver = ReadCarryOver(packet, playerType);
					}

					LOGI("[MP] ServerPacketType::CreateControllablePlayer - playerIndex: {}, playerType: {}, health: {}, flags: 0x{:.2x}, team: {}, x: {}, y: {}",
						playerIndex, playerType, health, flags, teamId, posX, posY);

					_lastSpawnedActorId = playerIndex;

					InvokeAsync([this, playerType, health, flags, teamId, posX, posY, hasCarryOver, carryOver]() {
						auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer);
						// Stash the carried-over progression (online co-op level change / reconnect) on the descriptor
						// before activating the player, so MpPlayer::OnActivatedAsync applies it after the base reset
						// (with coroutines the base activation finishes asynchronously and would otherwise overwrite a
						// value applied here at the call site). RemotablePlayer clears the flag once it's applied.
						if (hasCarryOver) {
							peerDesc->CarryOver = carryOver;
							peerDesc->HasCarryOver = true;
						}
						// Assign the team before activating the player so the spawn-time recolor (GetEffectiveFurColor)
						// resolves the correct team color when team coloring is enabled
						peerDesc->Team = teamId;

						std::shared_ptr<Actors::Multiplayer::RemotablePlayer> player = std::make_shared<Actors::Multiplayer::RemotablePlayer>(peerDesc);
						std::uint8_t playerParams[2] = { (std::uint8_t)playerType, 0 };
						player->OnActivated(Actors::ActorActivationDetails(
							this,
							Vector3i(posX, posY, PlayerZ),
							playerParams
						));

						player->SetHealth(health);
						player->_controllable = (flags & 0x01) != 0;
						player->_controllableExternal = (flags & 0x02) != 0;

						peerDesc->LapStarted = TimeStamp::now();
						// Spawning as a real player clears any stale spectate state on the client; a spectate spawn
						// sends a separate PlayerSetProperty::Spectate right after to set it back
						if (playerType != PlayerType::Spectate) {
							peerDesc->IsSpectating = SpectateMode::None;
						}

						Actors::Multiplayer::RemotablePlayer* ptr = player.get();
						_players.push_back(ptr);
						AddActor(player);
						AssignViewport(ptr);
						// TODO: Needed to drop and reinitialize newly assigned viewport, because it's called asynchronously, not from handler initialization
						CommitViewports();

						// TODO: Fade in should be skipped sometimes
						if ((flags & 0x10) == 0) {
							_hud->BeginFadeIn(false);
						}
					});
					return true;
				}
				case ServerPacketType::CreateRemoteActor: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					std::int32_t posZ = packet.ReadVariableInt32();
					Actors::ActorState state = (Actors::ActorState)packet.ReadVariableUint32();
					std::uint32_t metadataLength = packet.ReadVariableUint32();
					String metadataPath = String(NoInit, metadataLength);
					packet.Read(metadataPath.data(), metadataLength);
					AnimState anim = (AnimState)packet.ReadVariableUint32();
					float rotation = packet.ReadValue<std::uint16_t>() * fRadAngle360 / UINT16_MAX;
					float scaleX = (float)Half{packet.ReadValue<std::uint16_t>()};
					float scaleY = (float)Half{packet.ReadValue<std::uint16_t>()};
					Actors::ActorRendererType rendererType = (Actors::ActorRendererType)packet.ReadValue<std::uint8_t>();

					//LOGD("Remote actor {} created on [{};{}] with metadata \"{}\"", actorId, posX, posY, metadataPath);
					LOGD("[MP] ServerPacketType::CreateRemoteActor - actorId: {}, metadata: \"{}\", x: {}, y: {}", actorId, metadataPath, posX, posY);

					InvokeAsync([this, actorId, flags, posX, posY, posZ, state, metadataPath = std::move(metadataPath), anim, rotation, scaleX, scaleY, rendererType]() {
						{
							std::unique_lock lock(_lock);
							if (_remoteActors.contains(actorId)) {
								LOGW("[MP] ServerPacketType::CreateRemoteActor - Actor ({}) already exists", actorId);
								return;
							}
						}

						std::shared_ptr<Actors::Multiplayer::RemoteActor> remoteActor = std::make_shared<Actors::Multiplayer::RemoteActor>();
						remoteActor->OnActivated(Actors::ActorActivationDetails(this, Vector3i(posX, posY, posZ)));

						// If this actor was already marked as a player with a custom color, apply it before assigning
						// metadata so the sprites load indexed and the palette is set. Team coloring may force a color
						// even when the player has none of their own.
						auto pnIt = _playerNames.find(actorId);
						if (pnIt != _playerNames.end()) {
							std::uint32_t furColor = ColorizeFurForTeam(pnIt->second.FurColor, pnIt->second.Team);
							if (furColor != 0) {
								remoteActor->SetPlayerColor(furColor);
							}
						}

						remoteActor->AssignMetadata(flags, state, metadataPath, anim, rotation, scaleX, scaleY, rendererType);

						{
							std::unique_lock lock(_lock);
							_remoteActors[actorId] = remoteActor;
						}
						AddActor(remoteActor);
					});
					return true;
				}
				case ServerPacketType::CreateMirroredActor: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					EventType eventType = (EventType)packet.ReadVariableUint32();
					StaticArray<Events::EventSpawner::SpawnParamsSize, std::uint8_t> eventParams(NoInit);
					packet.Read(eventParams, Events::EventSpawner::SpawnParamsSize);
					Actors::ActorState actorFlags = (Actors::ActorState)packet.ReadVariableUint32();
					std::int32_t tileX = packet.ReadVariableInt32();
					std::int32_t tileY = packet.ReadVariableInt32();
					std::int32_t posZ = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::CreateMirroredActor - actorId: {}, event: {}, x: {}, y: {}", actorId, eventType, tileX * 32 + 16, tileY * 32 + 16);

					InvokeAsync([this, actorId, eventType, eventParams = std::move(eventParams), actorFlags, tileX, tileY, posZ]() {
						{
							std::unique_lock lock(_lock);
							if (_remoteActors.contains(actorId)) {
								LOGW("[MP] ServerPacketType::CreateMirroredActor - Actor ({}) already exists", actorId);
								return;
							}
						}
						
						// TODO: Remove const_cast
						std::shared_ptr<Actors::ActorBase> actor =_eventSpawner.SpawnEvent(eventType, const_cast<std::uint8_t*>(eventParams.data()), actorFlags, tileX, tileY, posZ);
						if (actor != nullptr) {
							{
								std::unique_lock lock(_lock);
								_remoteActors[actorId] = actor;
							}
							AddActor(actor);
						} else {
							LOGD("[MP] ServerPacketType::CreateMirroredActor - actorId: {} - Failed to create", actorId);
						}
					});
					return true;
				}
				case ServerPacketType::DestroyRemoteActor: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();

					LOGD("[MP] ServerPacketType::DestroyRemoteActor - actorId: {}", actorId);

					InvokeAsync([this, actorId]() {
						std::unique_lock lock(_lock);
						if DEATH_UNLIKELY(actorId == _lastSpawnedActorId) {
							// Server requested to despawn controllable player
							if (!_players.empty()) {
								auto* player = _players[0];
								player->SetState(Actors::ActorState::IsDestroyed, true);
								_players.eraseUnordered(0);

								UnassignViewport(player);
								CommitViewports();
							}
							return;
						}

						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							// If the local player was standing on this actor, stop carrying it before it's gone
							if (!_players.empty()) {
								_players[0]->CancelCarryingObject(it->second.get());
							}
							it->second->SetState(Actors::ActorState::IsDestroyed, true);
							_remoteActors.erase(it);
							_playerNames.erase(actorId);
						} else {
							LOGW("[MP] ServerPacketType::DestroyRemoteActor - actorId: {} - Actor not found", actorId);
						}
					});
					return true;
				}
				case ServerPacketType::UpdateAllActors: {
					MemoryStream packetCompressed(data);
					DeflateStream packet(packetCompressed);
					std::uint32_t now = packet.ReadVariableUint32();
					float elapsedFrames = (float)packet.ReadVariableUint64();
					std::uint32_t actorCount = packet.ReadVariableUint32();

					bool forceResyncInvoked = (actorCount & 1) == 1;
					if DEATH_UNLIKELY(!forceResyncInvoked && _lastUpdated >= now) {
						return true;
					}

					bool forceResyncRequired = (!forceResyncInvoked && _lastUpdated + 1 < now);
					if (forceResyncRequired) {
						LOGD("[MP] ServerPacketType::UpdateAllActors - Force re-sync required ({} -> {})", _lastUpdated, now);
					} else if (forceResyncInvoked) {
						LOGD("[MP] ServerPacketType::UpdateAllActors - Force re-sync invoked ({} -> {})", _lastUpdated, now);
					}

					std::unique_lock lock(_lock);

					_lastUpdated = now;
					_elapsedFrames = lerp(_elapsedFrames, elapsedFrames + _networkManager->GetRoundTripTimeMs() * FrameTimer::FramesPerSecond * 0.002f, 0.05f);

					actorCount >>= 1;

					for (std::uint32_t i = 0; i < actorCount; i++) {
						std::uint32_t actorId = packet.ReadVariableUint32();
						std::uint8_t flags = packet.ReadValue<std::uint8_t>();

						bool positionChanged = (flags & 0x01) != 0;
						bool animationChanged = (flags & 0x02) != 0;

						float posX, posY, rotation, scaleX, scaleY;
						AnimState anim; Actors::ActorRendererType rendererType;

						if (positionChanged) {
							posX = packet.ReadValue<std::int32_t>() / 512.0f;
							posY = packet.ReadValue<std::int32_t>() / 512.0f;
						} else {
							posX = 0.0f;
							posY = 0.0f;
						}

						if (animationChanged) {
							anim = (AnimState)packet.ReadVariableUint32();
							rotation = packet.ReadValue<std::uint16_t>() * fRadAngle360 / UINT16_MAX;
							scaleX = (float)Half{packet.ReadValue<std::uint16_t>()};
							scaleY = (float)Half{packet.ReadValue<std::uint16_t>()};
							rendererType = (Actors::ActorRendererType)packet.ReadValue<std::uint8_t>();
						} else {
							anim = AnimState::Idle;
							rotation = 0.0f;
							scaleX = 0.0f;
							scaleY = 0.0f;
							rendererType = Actors::ActorRendererType::Default;
						}

						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							if (auto* remoteActor = runtime_cast<Actors::Multiplayer::RemoteActor>(it->second.get())) {
								if (positionChanged) {
									remoteActor->SyncPositionWithServer(Vector2f(posX, posY));
								}
								if (animationChanged) {
									remoteActor->SyncAnimationWithServer(anim, rotation, scaleX, scaleY, rendererType);
								}
								remoteActor->SyncMiscWithServer(flags);
							}
						}
					}

					if (forceResyncRequired) {
						_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::ForceResyncActors, {});
					}
					return true;
				}
				case ServerPacketType::ChangeRemoteActorMetadata: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::uint32_t metadataLength = packet.ReadVariableUint32();
					String metadataPath = String(NoInit, metadataLength);
					packet.Read(metadataPath.data(), metadataLength);

					LOGD("[MP] ServerPacketType::ChangeRemoteActorMetadata - id: {}, metadata: \"{}\"", actorId, metadataPath);

					InvokeAsync([this, actorId, metadataPath = std::move(metadataPath)]() mutable {
						std::unique_lock lock(_lock);
						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							if (auto* remoteActor = runtime_cast<Actors::Multiplayer::RemoteActor>(it->second.get())) {
								remoteActor->ChangeMetadata(metadataPath);
							}
						}
					});
					return true;
					break;
				}
				case ServerPacketType::MarkRemoteActorAsPlayer: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::uint32_t playerNameLength = packet.ReadVariableUint32();
					String playerName = String(NoInit, playerNameLength);
					packet.Read(playerName.data(), playerNameLength);

					// Per-player recolor is only present when the HasFurColor flag is set (the idle-state broadcast
					// reuses this packet type with no color); the team is present when the HasTeam flag is set
					bool hasFurColor = (flags & 0x02) != 0;
					std::uint32_t furColor = (hasFurColor ? packet.ReadValueAsLE<std::uint32_t>() : 0);
					bool hasTeam = (flags & 0x04) != 0;
					std::uint8_t team = (hasTeam ? packet.ReadValue<std::uint8_t>() : 0);

					LOGD("[MP] ServerPacketType::MarkRemoteActorAsPlayer - actorId: {}, flags: 0x{:.2x}, name: \"{}\"", actorId, flags, playerName);

					InvokeAsync([this, actorId, flags, hasFurColor, furColor, hasTeam, team, playerName = std::move(playerName)]() mutable {
						if (actorId == _lastSpawnedActorId) {
							if (!playerName.empty()) {
								// Player name is optional, so only set it if it's not empty
								auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer);
								peerDesc->PlayerName = std::move(playerName);
							}
							if (hasTeam) {
								if (auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer)) {
									peerDesc->Team = team;
								}
							}
						} else {
							auto it = _playerNames.try_emplace(actorId, PlayerName{});
							if (!playerName.empty()) {
								// Player name is optional, so only set it if it's not empty
								it.first->second.Name = std::move(playerName);
							}
							it.first->second.Flags = flags;
							if (hasTeam) {
								it.first->second.Team = team;
							}
							if (hasFurColor) {
								it.first->second.FurColor = furColor;
							}

							if (hasFurColor || hasTeam) {
								// (Re)apply the effective color if the actor already exists (otherwise CreateRemoteActor
								// picks it up from _playerNames). Team coloring can force a color even when the player
								// has none of their own, so this runs whenever the color or team is known.
								RecolorRemoteActor(actorId, it.first->second.FurColor, it.first->second.Team);
							}
						}
					});
					return true;
				}
				case ServerPacketType::UpdatePositionsInRound: {
					MemoryStream packet(data);
					std::uint32_t count = packet.ReadVariableUint32();
					_positionsInRound.resize_for_overwrite(count);
					for (std::uint32_t i = 0; i < count; i++) {
						std::uint32_t playerIdx = packet.ReadVariableUint32();
						std::uint32_t positionInRound = packet.ReadVariableUint32();
						std::uint32_t pointsInRound = packet.ReadVariableUint32();
						_positionsInRound[i] = { playerIdx, positionInRound, pointsInRound };
					}
					return true;
				}
				case ServerPacketType::SyncRaceCheckpoints: {
					MemoryStream packet(data);
					_raceBoundsMin.X = packet.ReadVariableInt32();
					_raceBoundsMin.Y = packet.ReadVariableInt32();
					_raceBoundsMax.X = packet.ReadVariableInt32();
					_raceBoundsMax.Y = packet.ReadVariableInt32();
					std::uint32_t count = packet.ReadVariableUint32();
					_orderedRaceCheckpoints.resize_for_overwrite(count);
					for (std::uint32_t i = 0; i < count; i++) {
						std::int32_t x = packet.ReadVariableInt32();
						std::int32_t y = packet.ReadVariableInt32();
						std::uint16_t order = (std::uint16_t)packet.ReadVariableUint32();
						std::uint8_t group = packet.ReadValue<std::uint8_t>();
						_orderedRaceCheckpoints[i] = { Vector2i(x, y), order, group };
					}
					std::uint32_t markerCount = packet.ReadVariableUint32();
					_raceStartMarkers.resize_for_overwrite(markerCount);
					for (std::uint32_t i = 0; i < markerCount; i++) {
						std::int32_t x = packet.ReadVariableInt32();
						std::int32_t y = packet.ReadVariableInt32();
						_raceStartMarkers[i] = Vector2i(x, y);
					}
					return true;
				}
				case ServerPacketType::SyncTeamScores: {
					MemoryStream packet(data);
					std::uint8_t teamCount = packet.ReadValue<std::uint8_t>();
					_teamScores.resize_for_overwrite(teamCount);
					for (std::uint8_t i = 0; i < teamCount; i++) {
						_teamScores[i] = packet.ReadVariableUint32();
					}
					std::uint8_t isCtf = packet.ReadValue<std::uint8_t>();
					if (isCtf != 0) {
						// Update state/positions in place, preserving the client-local visual actor pointers
						if ((std::uint8_t)_ctfFlagStates.size() != teamCount) {
							// Team count changed - drop any stale local actors and resize
							for (auto& info : _ctfFlagStates) {
								if (info.FlagActor != nullptr) info.FlagActor->SetState(Actors::ActorState::IsDestroyed, true);
								if (info.BaseActor != nullptr) info.BaseActor->SetState(Actors::ActorState::IsDestroyed, true);
							}
							_ctfFlagStates.clear();
							_ctfFlagStates.resize(teamCount);
						}
						for (std::uint8_t i = 0; i < teamCount; i++) {
							_ctfFlagStates[i].State = packet.ReadValue<std::uint8_t>();
							_ctfFlagStates[i].BasePos.X = (float)packet.ReadVariableInt32();
							_ctfFlagStates[i].BasePos.Y = (float)packet.ReadVariableInt32();
							_ctfFlagStates[i].DropPos.X = (float)packet.ReadVariableInt32();
							_ctfFlagStates[i].DropPos.Y = (float)packet.ReadVariableInt32();
							_ctfFlagStates[i].CarrierActorId = packet.ReadVariableUint32();
						}
					} else {
						for (auto& info : _ctfFlagStates) {
							if (info.FlagActor != nullptr) info.FlagActor->SetState(Actors::ActorState::IsDestroyed, true);
							if (info.BaseActor != nullptr) info.BaseActor->SetState(Actors::ActorState::IsDestroyed, true);
						}
						_ctfFlagStates.clear();
					}
					return true;
				}
				case ServerPacketType::SyncScoreboard: {
					MemoryStream packet(data);
					std::uint32_t count = packet.ReadVariableUint32();
					_scoreboard.clear();
					for (std::uint32_t i = 0; i < count; i++) {
						std::uint32_t actorId = packet.ReadVariableUint32();
						PlayerScore score;
						score.Team = packet.ReadValue<std::uint8_t>();
						score.Kills = packet.ReadVariableUint32();
						score.Deaths = packet.ReadVariableUint32();
						score.Points = packet.ReadVariableUint32();
						score.Extra = packet.ReadVariableUint32();
						score.PingMs = packet.ReadVariableInt32();
						std::uint32_t nameLength = packet.ReadVariableUint32();
						score.Name = String(NoInit, nameLength);
						packet.Read(score.Name.data(), nameLength);
						score.IsLocal = (actorId == _lastSpawnedActorId);
						_scoreboard.push_back(std::move(score));
					}

					nCine::sort(_scoreboard.begin(), _scoreboard.end(), [](const PlayerScore& a, const PlayerScore& b) {
						return (a.Points != b.Points ? a.Points > b.Points : a.Kills > b.Kills);
					});
					return true;
				}
				case ServerPacketType::PlayerSetProperty: {
					MemoryStream packet(data);
					PlayerPropertyType propertyType = (PlayerPropertyType)packet.ReadValue<std::uint8_t>();
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					// Team is tracked for every player (not just the local one) so the HUD/nametags can color them
					if (propertyType == PlayerPropertyType::Team) {
						std::uint8_t team = packet.ReadValue<std::uint8_t>();
						InvokeAsync([this, playerIndex, team]() {
							if (playerIndex == _lastSpawnedActorId) {
								if (auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer)) {
									peerDesc->Team = team;
								}
								// Recolor the local player: with team coloring on, GetEffectiveFurColor now resolves
								// the new team (no-op otherwise)
								if (!_players.empty()) {
									_players[0]->RefreshColorPalette();
								}
							} else {
								auto it = _playerNames.try_emplace(playerIndex, PlayerName{});
								it.first->second.Team = team;
								// Recolor the matching remote actor for its new team
								RecolorRemoteActor(playerIndex, it.first->second.FurColor, team);
							}
						});
						return true;
					}

					// Shield drives a visible decoration for every player, so it is applied to the local player or
					// to the matching RemoteActor; the server broadcasts it to all peers, not just the owner.
					if (propertyType == PlayerPropertyType::Shield) {
						ShieldType shieldType = (ShieldType)packet.ReadValue<std::uint8_t>();
						std::int32_t timeLeft = packet.ReadVariableInt32();

						LOGD("[MP] ServerPacketType::PlayerSetProperty::Shield - playerIndex: {}, shieldType: {}, timeLeft: {}", playerIndex, shieldType, timeLeft);

						InvokeAsync([this, playerIndex, shieldType, timeLeft]() {
							if (playerIndex == _lastSpawnedActorId) {
								if (!_players.empty()) {
									_players[0]->SetShield(shieldType, float(timeLeft));
								}
							} else {
								std::unique_lock lock(_lock);
								auto it = _remoteActors.find(playerIndex);
								if (it != _remoteActors.end()) {
									if (auto* remoteActor = runtime_cast<Actors::Multiplayer::RemoteActor>(it->second.get())) {
										remoteActor->SetShield(shieldType, float(timeLeft));
									}
								}
							}
						});
						return true;
					}

					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerSetProperty - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					switch (propertyType) {
						case PlayerPropertyType::PlayerType: {
							PlayerType type = (PlayerType)packet.ReadValue<std::uint8_t>();
							InvokeAsync([this, type]() {
								if (!_players.empty()) {
									_players[0]->MorphTo(type);
								}
							});
							break;
						}
						case PlayerPropertyType::Lives: {
							std::int32_t lives = packet.ReadVariableInt32();
							InvokeAsync([this, lives]() {
								if (!_players.empty()) {
									_players[0]->_lives = lives;
								}
							});
							break;
						}
						case PlayerPropertyType::Health: {
							std::int32_t health = packet.ReadVariableInt32();
							InvokeAsync([this, health]() {
								if (!_players.empty()) {
									_players[0]->SetHealth(health);
								}
							});
							break;
						}
						case PlayerPropertyType::Controllable: {
							std::uint8_t enable = packet.ReadValue<std::uint8_t>();
							InvokeAsync([this, enable]() {
								if (!_players.empty()) {
									_players[0]->_controllableExternal = (enable != 0);
								}
							});
							break;
						}
						case PlayerPropertyType::Invulnerable: {
							std::int32_t timeLeft = packet.ReadVariableInt32();
							Actors::Player::InvulnerableType type = (Actors::Player::InvulnerableType)packet.ReadValue<std::uint8_t>();
							InvokeAsync([this, timeLeft, type]() {
								if (!_players.empty()) {
									_players[0]->SetInvulnerability(float(timeLeft), type);
								}
							});
							break;
						}
						case PlayerPropertyType::Modifier: {
							Actors::Player::Modifier modifier = (Actors::Player::Modifier)packet.ReadValue<std::uint8_t>();
							std::uint32_t decorActorId = packet.ReadVariableUint32();
							InvokeAsync([this, modifier, decorActorId]() {
								std::unique_lock lock(_lock);
								if (!_players.empty()) {
									auto it = _remoteActors.find(decorActorId);
									_players[0]->SetModifier(modifier, it != _remoteActors.end() ? it->second : nullptr);
								}
							});
							break;
						}
						case PlayerPropertyType::Dizzy: {
							std::int32_t timeLeft = packet.ReadVariableInt32();
							InvokeAsync([this, timeLeft]() {
								if (!_players.empty()) {
									_players[0]->SetDizzy(float(timeLeft));
								}
							});
							break;
						}
						case PlayerPropertyType::BeingStoodOn: {
							bool beingStoodOn = (packet.ReadValue<std::uint8_t>() != 0);
							InvokeAsync([this, beingStoodOn]() {
								if (!_players.empty()) {
									// Our own player is predicted locally, so it can't detect a remote player standing on
									// it - the server tells us, and PushSolidObjects leaves this flag alone on a client
									_players[0]->_beingStoodOn = beingStoodOn;
								}
							});
							break;
						}
						case PlayerPropertyType::Freeze: {
							std::int32_t timeLeft = packet.ReadVariableInt32();
							InvokeAsync([this, timeLeft]() {
								if (!_players.empty()) {
									_players[0]->Freeze(float(timeLeft));
								}
							});
							break;
						}
						case PlayerPropertyType::LimitCameraView: {
							std::int32_t left = packet.ReadVariableInt32();
							std::int32_t width = packet.ReadVariableInt32();
							std::int32_t playerPosX = packet.ReadVariableInt32();
							std::int32_t playerPosY = packet.ReadVariableInt32();

							LOGD("[MP] ServerPacketType::PlayerSetProperty::LimitCameraView - left: {}, width: {}, playerPos: [{}, {}]", left, width, playerPosX, playerPosY);

							InvokeAsync([this, left, width, playerPosX, playerPosY]() {
								LimitCameraView(nullptr, Vector2f(playerPosX, playerPosY), left, width);
							});
							break;
						}
						case PlayerPropertyType::OverrideCameraView: {
							float x = packet.ReadVariableInt32() * 0.01f;
							float y = packet.ReadVariableInt32() * 0.01f;
							bool topLeft = packet.ReadValue<std::uint8_t>() != 0;

							LOGD("[MP] ServerPacketType::PlayerSetProperty::OverrideCameraView - x: {}, y: {}, topLeft: {}", x, y, topLeft);

							InvokeAsync([this, x, y, topLeft]() {
								if (!_players.empty()) {
									OverrideCameraView(_players[0], x, y, topLeft);
								}
							});
							break;
						}
						case PlayerPropertyType::ShakeCameraView: {
							float duration = packet.ReadVariableInt32() * 0.01f;

							LOGD("[MP] ServerPacketType::PlayerSetProperty::ShakeCameraView - duration: {}", duration);

							InvokeAsync([this, duration]() {
								if (!_players.empty()) {
									ShakeCameraView(_players[0], duration);
								}
							});
							break;
						}
						case PlayerPropertyType::Spectate: {
							SpectateMode spectateMode = (SpectateMode)packet.ReadValue<std::uint8_t>();

							LOGD("[MP] ServerPacketType::PlayerSetProperty::Spectate - mode: 0x{:.2x}", spectateMode);

							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								player->GetPeerDescriptor()->IsSpectating = spectateMode;
							}
							break;
						}
						case PlayerPropertyType::WeaponAmmo: {
							std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();
							std::uint16_t weaponAmmo = packet.ReadValue<std::uint16_t>();
							InvokeAsync([this, weaponType, weaponAmmo]() {
								if (!_players.empty() && weaponType < arraySize(_players[0]->_weaponAmmo)) {
									_players[0]->_weaponAmmo[weaponType] = weaponAmmo;
								}
							});
							break;
						}
						case PlayerPropertyType::WeaponUpgrades: {
							std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();
							std::uint8_t weaponUpgrades = packet.ReadValue<std::uint8_t>();
							InvokeAsync([this, weaponType, weaponUpgrades]() {
								if (!_players.empty() && weaponType < arraySize(_players[0]->_weaponUpgrades)) {
									_players[0]->_weaponUpgrades[weaponType] = weaponUpgrades;
								}
							});
							break;
						}
						case PlayerPropertyType::Coins: {
							std::int32_t newCount = packet.ReadVariableInt32();
							InvokeAsync([this, newCount]() {
								if (!_players.empty()) {
									_players[0]->_coins = newCount;
									_hud->ShowCoins(newCount);
								}
							});
							break;
						}
						case PlayerPropertyType::Gems: {
							std::uint8_t gemType = packet.ReadValue<std::uint8_t>();
							std::int32_t newCount = packet.ReadVariableInt32();
							InvokeAsync([this, gemType, newCount]() {
								if (!_players.empty() && gemType < arraySize(_players[0]->_gems)) {
									_players[0]->_gems[gemType] = newCount;
									_hud->ShowGems(gemType, newCount);
								}
							});
							break;
						}
						case PlayerPropertyType::Score: {
							std::int32_t score = packet.ReadVariableInt32();
							InvokeAsync([this, score]() {
								if (!_players.empty()) {
									_players[0]->_score = score;
								}
							});
							break;
						}
						case PlayerPropertyType::Points: {
							std::uint32_t points = packet.ReadVariableUint32();
							std::uint32_t totalPoints = packet.ReadVariableUint32();

							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								player->GetPeerDescriptor()->Points = points;
							}

							auto& serverConfig = _networkManager->GetServerConfiguration();
							serverConfig.TotalPlayerPoints = totalPoints;
							break;
						}
						/*case PlayerPropertyType::PositionInRound: {
							std::uint32_t positionInRound = packet.ReadVariableUint32();
							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								player->GetPeerDescriptor()->PositionInRound = positionInRound;
							}
							break;
						}*/
						case PlayerPropertyType::Deaths: {
							std::uint32_t deaths = packet.ReadVariableUint32();
							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								player->GetPeerDescriptor()->Deaths = deaths;
							}
							break;
						}
						case PlayerPropertyType::Kills: {
							std::uint32_t kills = packet.ReadVariableUint32();
							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								player->GetPeerDescriptor()->Kills = kills;
							}
							break;
						}
						case PlayerPropertyType::Laps: {
							std::uint32_t currentLaps = packet.ReadVariableUint32();
							//std::uint32_t totalLaps = packet.ReadVariableUint32();
							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								auto peerDesc = player->GetPeerDescriptor();
								peerDesc->Laps = currentLaps;
								peerDesc->LapStarted = TimeStamp::now();
							}
							break;
						}
						case PlayerPropertyType::TreasureCollected: {
							std::uint32_t treasureCollected = packet.ReadVariableUint32();
							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								player->GetPeerDescriptor()->TreasureCollected = treasureCollected;
							}
							break;
						}
						default: {
							LOGD("[MP] ServerPacketType::PlayerSetProperty - Received unknown property {}", propertyType);
							break;
						}
					}
					return true;
				}
				case ServerPacketType::PlayerResetProperties: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerSetProperty - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					if (!_players.empty()) {
						auto* player = static_cast<RemotablePlayer*>(_players[0]);
						auto peerDesc = player->GetPeerDescriptor();
						peerDesc->PositionInRound = 0;
						peerDesc->Deaths = 0;
						peerDesc->Kills = 0;
						peerDesc->Laps = 0;
						peerDesc->LapStarted = TimeStamp::now();
						peerDesc->TreasureCollected = 0;
						peerDesc->DeathElapsedFrames = FLT_MAX;

						player->_coins = 0;
						std::memset(player->_gems, 0, sizeof(player->_gems));
						std::memset(player->_weaponAmmo, 0, sizeof(player->_weaponAmmo));
						std::memset(player->_weaponAmmoCheckpoint, 0, sizeof(player->_weaponAmmoCheckpoint));

						player->_coinsCheckpoint = 0;
						std::memset(player->_gemsCheckpoint, 0, sizeof(player->_gemsCheckpoint));
						std::memset(player->_weaponUpgrades, 0, sizeof(player->_weaponUpgrades));
						std::memset(player->_weaponUpgradesCheckpoint, 0, sizeof(player->_weaponUpgradesCheckpoint));

						player->_weaponAmmo[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
						player->_weaponAmmoCheckpoint[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
						player->_currentWeapon = WeaponType::Blaster;
					}
					return true;
				}
				case ServerPacketType::PlayerRespawn: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRespawn - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					LOGD("[MP] ServerPacketType::PlayerRespawn - playerIndex: {}, x: {}, y: {}", playerIndex, posX, posY);

					InvokeAsync([this, posX, posY]() {
						if (!_players.empty()) {
							auto* player = _players[0];
							if (!player->Respawn(Vector2f(posX, posY))) {
								return;
							}

							Clock& c = nCine::clock();
							std::uint64_t now = c.now() * 1000 / c.frequency();

							MemoryStream packetAck(24);
							packetAck.WriteVariableUint32(_lastSpawnedActorId);
							packetAck.WriteVariableUint64(now);
							packetAck.WriteValue<std::int32_t>((std::int32_t)(player->_pos.X * 512.0f));
							packetAck.WriteValue<std::int32_t>((std::int32_t)(player->_pos.Y * 512.0f));
							packetAck.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
							packetAck.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
							_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerAckWarped, packetAck);
						}
					});
					return true;
				}
				case ServerPacketType::PlayerMoveInstantly: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float speedX = packet.ReadValue<std::int16_t>() / 512.0f;
					float speedY = packet.ReadValue<std::int16_t>() / 512.0f;
					float externalForceX = packet.ReadValue<std::int16_t>() / 512.0f;
					float externalForceY = packet.ReadValue<std::int16_t>() / 512.0f;

					LOGD("[MP] ServerPacketType::PlayerMoveInstantly - playerIndex: {}, x: {}, y: {}, sx: {}, sy: {}",
						playerIndex, posX, posY, speedX, speedY);

					InvokeAsync([this, posX, posY, speedX, speedY, externalForceX, externalForceY]() {
						if (!_players.empty()) {
							auto* player = static_cast<RemotablePlayer*>(_players[0]);
							player->MoveRemotely(Vector2f(posX, posY), Vector2f(speedX, speedY), Vector2f(externalForceX, externalForceY));
						}
					});
					return true;
				}
				case ServerPacketType::PlayerAckWarped: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					std::uint64_t seqNum = packet.ReadVariableUint64();

					LOGD("[MP] ServerPacketType::PlayerAckWarped - playerIndex: {}, seqNum: {}", playerIndex, seqNum);

					if (_lastSpawnedActorId == playerIndex && _seqNumWarped == seqNum) {
						_seqNumWarped = 0;
					}
					return true;
				}
				case ServerPacketType::PlayerEmitWeaponFlare: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerEmitWeaponFlare - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					LOGD("[MP] ServerPacketType::PlayerEmitWeaponFlare - playerIndex: {}", playerIndex);

					InvokeAsync([this]() {
						if (!_players.empty()) {
							_players[0]->EmitWeaponFlare();
						}
					});
					return true;
				}
				case ServerPacketType::PlayerChangeWeapon: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerChangeWeapon - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					WeaponType weaponType = (WeaponType)packet.ReadValue<std::uint8_t>();
					Actors::Player::SetCurrentWeaponReason reason = (Actors::Player::SetCurrentWeaponReason)packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::PlayerChangeWeapon - playerIndex: {}, weaponType: {}, reason: {}", playerIndex, weaponType, reason);

					if (!_players.empty()) {
						auto* remotablePlayer = static_cast<Actors::Multiplayer::RemotablePlayer*>(_players[0]);

						if (reason == Actors::Player::SetCurrentWeaponReason::AddAmmo && !PreferencesCache::SwitchToNewWeapon) {
							HandlePlayerWeaponChanged(remotablePlayer, Actors::Player::SetCurrentWeaponReason::Rollback);
							return true;
						}
						
						remotablePlayer->ChangingWeaponFromServer = true;
						static_cast<Actors::Player*>(remotablePlayer)->SetCurrentWeapon(weaponType, reason);
						remotablePlayer->ChangingWeaponFromServer = false;
					}
					return true;
				}
				case ServerPacketType::PlayerTakeDamage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerTakeDamage - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::int32_t health = packet.ReadVariableInt32();
					float pushForce = packet.ReadValue<std::int16_t>() / 512.0f;

					LOGD("[MP] ServerPacketType::PlayerTakeDamage - playerIndex: {}, health: {}, pushForce: {}", playerIndex, health, pushForce);

					InvokeAsync([this, health, pushForce]() {
						if (!_players.empty()) {
							_players[0]->TakeDamage(health == 0 ? INT32_MAX : (_players[0]->_health - health), pushForce, true);
						}
					});
					return true;
				}
				case ServerPacketType::PlayerPush: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerPush - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					float pushSpeedX = packet.ReadValue<std::int32_t>() / 512.0f;
					InvokeAsync([this, pushSpeedX]() {
						if (!_players.empty()) {
							// TODO: Remove timeMult and make push speed consistent regardless of time multiplier
							float timeMult = theApplication().GetTimeMult();
							_players[0]->OnPushSolidObject(timeMult, pushSpeedX);
						}
					});
					return true;
				}
				case ServerPacketType::PlayerActivateSpring: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerActivateSpring - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float forceX = packet.ReadValue<std::int16_t>() / 512.0f;
					float forceY = packet.ReadValue<std::int16_t>() / 512.0f;
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					InvokeAsync([this, posX, posY, forceX, forceY, flags]() {
						if (!_players.empty()) {
							bool removeSpecialMove = false;
							_players[0]->OnHitSpring(Vector2f(posX, posY), Vector2f(forceX, forceY), (flags & 0x01) == 0x01, (flags & 0x02) == 0x02, removeSpecialMove);
							if (removeSpecialMove) {
								_players[0]->_controllable = true;
								_players[0]->EndDamagingMove();
							}
						}
					});
					return true;
				}
				case ServerPacketType::PlayerWarpIn: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerWarpIn - Received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
						return true;
					}

					ExitType exitType = (ExitType)packet.ReadValue<std::uint8_t>();
					LOGD("[MP] ServerPacketType::PlayerWarpIn - playerIndex: {}, exitType: 0x{:.2x}", playerIndex, exitType);

					InvokeAsync([this, exitType]() {
						if (!_players.empty()) {
							auto* player = static_cast<RemotablePlayer*>(_players[0]);
							player->WarpIn(exitType);
						}
					});
					return true;
				}
			}
		}

		return false;
	}

	String MpLevelHandler::GetAssetFullPath(AssetType type, StringView path, StaticArrayView<Uuid::Size, Uuid::Type> remoteServerId, bool forWrite)
	{
		const auto& resolver = ContentResolver::Get();
		auto pathNormalized = fs::ToNativeSeparators(path);

		char uuidStr[33]; std::size_t uuidStrLength;
		if (remoteServerId) {
			uuidStrLength = formatInto(uuidStr, "{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}",
				remoteServerId[0], remoteServerId[1], remoteServerId[2], remoteServerId[3], remoteServerId[4], remoteServerId[5], remoteServerId[6], remoteServerId[7], remoteServerId[8], remoteServerId[9], remoteServerId[10], remoteServerId[11], remoteServerId[12], remoteServerId[13], remoteServerId[14], remoteServerId[15]);
		}

		switch (type) {
			case AssetType::Level: {
				String fullPath;
				if (remoteServerId) {
					fullPath = fs::CombinePath({ resolver.GetCachePath(), "Downloads"_s,
						{ uuidStr, uuidStrLength }, fs::ToNativeSeparators(String(path + ".j2l"_s)) });
				}
				if (!forWrite && !fs::IsReadableFile(fullPath)) {
					fullPath = fs::CombinePath({ resolver.GetContentPath(), "Episodes"_s, String(pathNormalized + ".j2l"_s) });
					if (!fs::IsReadableFile(fullPath)) {
						fullPath = fs::CombinePath({ resolver.GetCachePath(), "Episodes"_s, String(pathNormalized + ".j2l"_s) });
						if (!fs::IsReadableFile(fullPath)) {
							fullPath = {};
						}
					}
				}
				return fullPath;
			}
			case AssetType::TileSet: {
				String fullPath;
				if (remoteServerId) {
					fullPath = fs::CombinePath({ resolver.GetCachePath(), "Downloads"_s,
						{ uuidStr, uuidStrLength }, fs::ToNativeSeparators(String(path + ".j2t"_s)) });
				}
				if (!forWrite && !fs::IsReadableFile(fullPath)) {
					fullPath = fs::CombinePath({ resolver.GetContentPath(), "Tilesets"_s, String(path + ".j2t"_s) });
					if (!fs::IsReadableFile(fullPath)) {
						fullPath = fs::CombinePath({ resolver.GetCachePath(), "Tilesets"_s, String(path + ".j2t"_s) });
						if (!fs::IsReadableFile(fullPath)) {
							fullPath = {};
						}
					}
				}
				return fullPath;
			}
			case AssetType::Music: {
				String fullPath;
				if (remoteServerId) {
					fullPath = fs::CombinePath({ resolver.GetCachePath(), "Downloads"_s,
						{ uuidStr, uuidStrLength }, fs::ToNativeSeparators(path) });
				}
				if (!forWrite && !fs::IsReadableFile(fullPath)) {
					fullPath = fs::CombinePath({ resolver.GetContentPath(), "Music"_s, path });
					if (!fs::IsReadableFile(fullPath)) {
						// "Source" directory must be case in-sensitive
						fullPath = fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), path));
						if (!fs::IsReadableFile(fullPath)) {
							fullPath = {};
						}
					}
				}
				return fullPath;
			}
			default: {
				return {};
			}
		}
	}

	void MpLevelHandler::AttachComponents(LevelDescriptor&& descriptor)
	{
		LevelHandler::AttachComponents(std::move(descriptor));

		// Reset race minimap geometry for both server and client (the client repopulates it from a server packet,
		// or leaves it empty so no minimap is shown when the new level has no track)
		_orderedRaceCheckpoints.clear();
		_raceStartMarkers.clear();
		_raceCheckpoints.clear();
		_raceCheckpointsOrdered = false;
		_raceBoundsMin = Vector2i(0, 0);
		_raceBoundsMax = Vector2i(0, 0);

		if (_isServer) {
			// Cache all possible multiplayer spawn points (if it's not coop level)
			_multiplayerSpawnPoints.clear();
			_totalTreasureCount = 0;

			_eventMap->ForEachEvent([this](Events::EventMap::EventTile& e, std::int32_t x, std::int32_t y) {
				if (e.Event == EventType::LevelStartMultiplayer) {
					_multiplayerSpawnPoints.emplace_back(Vector2f(x * Tiles::TileSet::DefaultTileSize, y * Tiles::TileSet::DefaultTileSize - 8), e.EventParams[0]);
				} else if (e.Event == EventType::Gem) {
					// GemStomp event is not counted, because it's difficult to find
					std::uint8_t gemType = (std::uint8_t)(e.EventParams[0] & 0x03);
					std::int32_t count = GetTreasureWeight(gemType);
					_totalTreasureCount += count;
				} else if (e.Event == EventType::GemGiant) {
					// Giant gem randowly spawns between 5 and 12 red gems, so use 8 as average
					_totalTreasureCount += 8 * GetTreasureWeight(0);
				} else if (e.Event == EventType::GemRing) {
					// Gems in the ring are always red
					std::int32_t count = (e.EventParams[0] > 0 ? e.EventParams[0] : 8) * GetTreasureWeight(0);
					_totalTreasureCount += count;
				} else if (e.Event == EventType::CrateGem || e.Event == EventType::BarrelGem) {
					std::int32_t count = e.EventParams[0] * GetTreasureWeight(0) +
										 e.EventParams[1] * GetTreasureWeight(1) +
										 e.EventParams[2] * GetTreasureWeight(2) +
										 e.EventParams[3] * GetTreasureWeight(3);
					_totalTreasureCount += count;
				}
				return true;
			});

			const auto& serverConfig = _networkManager->GetServerConfiguration();

			if (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::TeamRace) {
				BuildRaceCheckpoints();
			}
			// Capture The Flag bases/flags are (re)built at round start in ResetAllPlayerStats(), not here: at level
			// load no client is connected yet, so spawning now would only reach late joiners via the per-peer sync

			if (serverConfig.GameMode == MpGameMode::Cooperation) {
				_eventMap->ForEachEvent([this](Events::EventMap::EventTile& e, std::int32_t x, std::int32_t y) {
					if (e.Event == EventType::OneUp) {
						// Replace all 1ups with max carrots
						// TODO: Reset it back to 1ups when starting a different mode with limited lives
						e.Event = EventType::Carrot;
						e.EventParams[0] = 1;
					} else if (e.Event == EventType::AirboardGenerator) {
						// Shorten delay time of all airboard generators
						// TODO: Reset it back when starting a different mode
						if (e.EventParams[0] > 4) {
							e.EventParams[0] = 4;
						}
					}
					return true;
				});
			}
		} else {
			// Change InstantDeathPit to FallForever, because player health is managed by the server
			if (_eventMap->GetPitType() == PitType::InstantDeathPit) {
				_eventMap->SetPitType(PitType::FallForever);
			}

			_eventMap->ForEachEvent([this](Events::EventMap::EventTile& e, std::int32_t x, std::int32_t y) {
				switch (e.Event) {
					case EventType::WarpOrigin:
					case EventType::ModifierHurt:
					case EventType::ModifierDeath:
					case EventType::ModifierLimitCameraView:
					case EventType::AreaEndOfLevel:
					case EventType::AreaCallback:
					case EventType::AreaActivateBoss:
					case EventType::AreaFlyOff:
					case EventType::AreaRevertMorph:
					case EventType::AreaMorphToFrog:
					case EventType::AreaNoFire:
					case EventType::TriggerZone:
					case EventType::RollingRockTrigger:
						// These events are handled on server-side only
						e.Event = EventType::Empty;
						break;
				}
				return true;
			});
		}
	}

	std::unique_ptr<UI::HUD> MpLevelHandler::CreateHUD()
	{
		return std::make_unique<UI::Multiplayer::MpHUD>(this);
	}

	void MpLevelHandler::SpawnPlayers(const LevelInitialization& levelInit)
	{
		if (!_isServer) {
			// Player spawning is delayed/controlled by server
			_hud = CreateHUD();
			return;
		}

		const auto& serverConfig = _networkManager->GetServerConfiguration();

		for (std::int32_t i = 0; i < std::int32_t(arraySize(levelInit.PlayerCarryOvers)); i++) {
			if (levelInit.PlayerCarryOvers[i].Type == PlayerType::None) {
				continue;
			}

			Vector2f spawnPosition = GetSpawnPoint(levelInit.PlayerCarryOvers[i].Type);

			// Each local player gets its own peer descriptor so teams, scores, kills and laps are tracked separately.
			// In online sessions there is only the single host player (index 0 == LocalPeer); local splitscreen
			// registers one local descriptor per player index.
			auto peerDesc = _networkManager->AddLocalPlayer(i);
			std::shared_ptr<Actors::Multiplayer::LocalPlayerOnServer> player = std::make_shared<Actors::Multiplayer::LocalPlayerOnServer>(peerDesc);
			std::uint8_t playerParams[2] = { (std::uint8_t)levelInit.PlayerCarryOvers[i].Type, (std::uint8_t)i };
			player->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i((std::int32_t)spawnPosition.X + (i * 30), (std::int32_t)spawnPosition.Y - (i * 30), PlayerZ - i),
				playerParams
			));
			player->_controllableExternal = _controllableExternal;
			player->_health = (serverConfig.InitialPlayerHealth > 0
				? serverConfig.InitialPlayerHealth
				: (PlayerShouldHaveUnlimitedHealth(serverConfig.GameMode) ? INT32_MAX : 5));

			peerDesc->PreferredPlayerType = levelInit.PlayerCarryOvers[i].Type;
			peerDesc->LevelState = PeerLevelState::PlayerSpawned;
			peerDesc->LapsElapsedFrames = _elapsedFrames;
			peerDesc->LapStarted = TimeStamp::now();
			peerDesc->PlayerName = PreferencesCache::GetEffectivePlayerName();
			// Local players use the host's configured color so remote clients see it (sent via MarkRemoteActorAsPlayer)
			peerDesc->FurColor = PreferencesCache::PlayerFurColor;

			Actors::Multiplayer::LocalPlayerOnServer* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);
			AssignViewport(ptr);

			ptr->ReceiveLevelCarryOver(levelInit.LastExitType, levelInit.PlayerCarryOvers[i]);

			// The player is invulnerable for a short time after spawning
			ptr->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Blinking);
		}

		ApplyGameModeToAllPlayers(serverConfig.GameMode);

		_hud = CreateHUD();
		_hud->BeginFadeIn((levelInit.LastExitType & ExitType::FastTransition) == ExitType::FastTransition);
	}

	std::shared_ptr<Actors::Player> MpLevelHandler::CreateResumablePlayer(std::int32_t index)
	{
		// Restoring a resumable (local cooperative) session: give each player its own local peer descriptor and create
		// it as a LocalPlayerOnServer, mirroring SpawnPlayers(). The player's state is restored afterwards by the base
		// from the stream (Player::InitializeFromStream activates the actor at its checkpoint).
		auto peerDesc = _networkManager->AddLocalPlayer(index);
		peerDesc->LevelState = PeerLevelState::PlayerSpawned;
		peerDesc->LapsElapsedFrames = _elapsedFrames;
		peerDesc->LapStarted = TimeStamp::now();
		peerDesc->PlayerName = PreferencesCache::GetEffectivePlayerName();
		peerDesc->FurColor = PreferencesCache::PlayerFurColor;
		return std::make_shared<Actors::Multiplayer::LocalPlayerOnServer>(peerDesc);
	}

	void MpLevelHandler::PrepareNextLevelInitialization(LevelInitialization& levelInit)
	{
		LevelHandler::PrepareNextLevelInitialization(levelInit);

		// Online co-op: capture each remote player's state into its (persistent) peer descriptor so weapons,
		// lives, score and gems carry over to the next level - the host already carries over via levelInit.
		// The snapshot is applied on (re)spawn in PlayerOnServer::OnActivatedAsync.
		if (_isServer) {
			for (auto* player : _players) {
				if (auto* mpPlayer = runtime_cast<Actors::Multiplayer::MpPlayer>(player)) {
					auto peerDesc = mpPlayer->GetPeerDescriptor();
					if (peerDesc != nullptr && peerDesc->RemotePeer) {
						// Call the base implementation explicitly: RemotePlayerOnServer::PrepareLevelCarryOver() is
						// overridden to return an empty struct (so remotes stay out of levelInit's local-spawn list),
						// but the server does track the real score/lives/weapons/gems for it, so capture those here.
						peerDesc->CarryOver = mpPlayer->Player::PrepareLevelCarryOver();
						peerDesc->HasCarryOver = true;
					}
				}
			}
		}
	}

	bool MpLevelHandler::IsCheatingAllowed()
	{
		if (!_isServer) {
			return false;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		return (PreferencesCache::AllowCheats && serverConfig.GameMode == MpGameMode::Cooperation);
	}

	void MpLevelHandler::BeforeActorDestroyed(Actors::ActorBase* actor)
	{
		if (!_isServer) {
			return;
		}

		std::uint32_t actorId;
		{
			std::unique_lock lock(_lock);
			auto it = _remotingActors.find(actor);
			if (it == _remotingActors.end()) {
				return;
			}

			actorId = it->second.ActorID;
			_remotingActors.erase(it);
			_remoteActors.erase(actorId);
		}

		MemoryStream packet(4);
		packet.WriteVariableUint32(actorId);

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::DestroyRemoteActor, packet);
	}

	void MpLevelHandler::ProcessEvents(float timeMult)
	{
		// Process events only by server
		if DEATH_LIKELY(_isServer) {
			LevelHandler::ProcessEvents(timeMult);
		}
	}

	void MpLevelHandler::PauseGame()
	{
		LevelHandler::PauseGame();

		BroadcastLocalPlayerIdle(true);
	}

	void MpLevelHandler::ResumeGame()
	{
		LevelHandler::ResumeGame();

		BroadcastLocalPlayerIdle(false);
	}

	void MpLevelHandler::ShowConsole()
	{
		LevelHandler::ShowConsole();

		BroadcastLocalPlayerIdle(true);
	}

	void MpLevelHandler::HideConsole()
	{
		LevelHandler::HideConsole();

		BroadcastLocalPlayerIdle(false);
	}

	void MpLevelHandler::HandlePlayerLevelChanging(Actors::Player* player, ExitType exitType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(5);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)exitType);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerWarpIn, packet);
			}
		}
	}

	bool MpLevelHandler::HandlePlayerPush(Actors::Player* player, float pushSpeedX)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(8);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::int32_t>((std::int32_t)(pushSpeedX * 512.0f));
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerPush, packet);
			}
		}

		return true;
	}

	bool MpLevelHandler::HandlePlayerSpring(Actors::Player* player, Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				std::uint8_t flags = 0;
				if (keepSpeedX) {
					flags |= 0x01;
				}
				if (keepSpeedY) {
					flags |= 0x02;
				}

				MemoryStream packet(17);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 512.0f));
				packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(force.X * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(force.Y * 512.0f));
				packet.WriteValue<std::uint8_t>(flags);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerActivateSpring, packet);
			}
		}

		return true;
	}

	void MpLevelHandler::HandlePlayerBeforeWarp(Actors::Player* player, Vector2f pos, WarpFlags flags)
	{
		if DEATH_LIKELY(_isServer) {
			if ((flags & (WarpFlags::Fast | WarpFlags::SkipWarpIn)) != WarpFlags::Default) {
				// Nothing to do, sending PlayerMoveInstantly packet is enough
				return;
			}

			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(5);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>(0xFF);	// Only temporary, no level changing
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerWarpIn, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetModifier(Actors::Player* player, Actors::Player::Modifier modifier, const std::shared_ptr<Actors::ActorBase>& decor)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				std::uint32_t actorId;
				{
					std::unique_lock lock(_lock);
					auto it = _remotingActors.find(decor.get());
					if (it != _remotingActors.end()) {
						actorId = it->second.ActorID;
					} else {
						actorId = UINT32_MAX;
					}
				}

				MemoryStream packet(10);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Modifier);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)modifier);
				packet.WriteVariableUint32(actorId);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerFreeze(Actors::Player* player, float timeLeft)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Freeze);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)timeLeft);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetInvulnerability(Actors::Player* player, float timeLeft, Actors::Player::InvulnerableType type)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(10);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Invulnerable);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)timeLeft);
				packet.WriteValue<std::uint8_t>((std::uint8_t)type);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetScore(Actors::Player* player, std::int32_t value)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Score);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)value);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetHealth(Actors::Player* player, std::int32_t count)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Health);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)count);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetLives(Actors::Player* player, std::int32_t count)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Lives);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)count);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerTakeDamage(Actors::Player* player, std::int32_t amount, float pushForce)
	{
		// TODO: Only called by PlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto& serverConfig = _networkManager->GetServerConfiguration();
			auto* mpPlayer = static_cast<PlayerOnServer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(10);
				packet.WriteVariableUint32(player->_playerIndex);
				packet.WriteVariableInt32(player->_health);
				packet.WriteValue<std::int16_t>((std::int16_t)(pushForce * 512.0f));
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerTakeDamage, packet);
			}

			if (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt) {
				// TODO: Drop number of gems accorting to the gun strength (usually 3 times)
				std::uint32_t treasureLost = std::min(peerDesc->TreasureCollected, 3u);
				if (treasureLost > 0) {
					// If the player is dead, drop half of the collected treasure instead
					if (mpPlayer->_health <= 0 && treasureLost < peerDesc->TreasureCollected / 2) {
						treasureLost = peerDesc->TreasureCollected / 2;
					}

					peerDesc->TreasureCollected -= treasureLost;

					if (peerDesc->RemotePeer) {
						MemoryStream packet2(9);
						packet2.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::TreasureCollected);
						packet2.WriteVariableUint32(mpPlayer->_playerIndex);
						packet2.WriteVariableUint32(peerDesc->TreasureCollected);
						_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet2);
					}

					Vector2f pos = mpPlayer->_pos;
					for (std::uint32_t i = 0; i < treasureLost; i++) {
						float dir = (Random().NextBool() ? -1.0f : 1.0f);
						float force = Random().NextFloat(10.0f, 20.0f);
						Vector3f spawnPos = Vector3f(pos.X, pos.Y, MainPlaneZ);
						std::uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] = { 0, 0x04 };
						auto actor = _eventSpawner.SpawnEvent(EventType::Gem, spawnParams, Actors::ActorState::None, spawnPos.As<std::int32_t>());
						if (actor != nullptr) {
							actor->AddExternalForce(dir * force, force);
							AddActor(actor);
						}
					}
				}
			}

			if (_levelState == LevelState::Running && player->_health <= 0) {
				peerDesc->Deaths++;

				if (peerDesc->RemotePeer) {
					MemoryStream packet3(9);
					packet3.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Deaths);
					packet3.WriteVariableUint32(mpPlayer->_playerIndex);
					packet3.WriteVariableUint32(peerDesc->Deaths);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet3);
				}

				if (auto* attacker = GetWeaponOwner(mpPlayer->_lastAttacker.get())) {
					auto attackerPeerDesc = attacker->GetPeerDescriptor();
					attackerPeerDesc->Kills++;

					if (attackerPeerDesc->RemotePeer) {
						MemoryStream packet4(9);
						packet4.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Kills);
						packet4.WriteVariableUint32(attacker->_playerIndex);
						packet4.WriteVariableUint32(attackerPeerDesc->Kills);
						_networkManager->SendTo(attackerPeerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet4);
					}

					_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]{}\f[/c] was roasted by \f[c:#d0705d]{}\f[/c]",
						peerDesc->PlayerName, attackerPeerDesc->PlayerName));

					MemoryStream packet5(19 + peerDesc->PlayerName.size() + attackerPeerDesc->PlayerName.size());
					packet5.WriteValue<std::uint8_t>((std::uint8_t)PeerPropertyType::Roasted);
					packet5.WriteVariableUint64(peerDesc->RemotePeer.GetId());
					packet5.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
					packet5.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());
					packet5.WriteVariableUint64(attackerPeerDesc->RemotePeer.GetId());
					packet5.WriteValue<std::uint8_t>((std::uint8_t)attackerPeerDesc->PlayerName.size());
					packet5.Write(attackerPeerDesc->PlayerName.data(), (std::uint32_t)attackerPeerDesc->PlayerName.size());

					_networkManager->SendTo([this](const Peer& peer) {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						return (peerDesc && peerDesc->IsAuthenticated);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PeerSetProperty, packet5);
				} else {
					_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]{}\f[/c] was roasted by environment",
						peerDesc->PlayerName));

					MemoryStream packet6(19 + peerDesc->PlayerName.size());
					packet6.WriteValue<std::uint8_t>((std::uint8_t)PeerPropertyType::Roasted);
					packet6.WriteVariableUint64(peerDesc->RemotePeer.GetId());
					packet6.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
					packet6.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());
					packet6.WriteVariableUint64(0);
					packet6.WriteValue<std::uint8_t>(0);

					_networkManager->SendTo([this](const Peer& peer) {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						return (peerDesc && peerDesc->IsAuthenticated);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PeerSetProperty, packet6);
				}
			}
		}
	}

	void MpLevelHandler::HandlePlayerBumped(Actors::Player* player)
	{
		// TODO: Only called by PlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<PlayerOnServer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			// The owning client simulates its own physics, so resync the post-bump position and velocity;
			// reuses the existing PlayerMoveInstantly packet.
			if (peerDesc->RemotePeer) {
				MemoryStream packet(20);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_pos.X * 512.0f));
				packet.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_pos.Y * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_speed.X * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_speed.Y * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_externalForce.X * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_externalForce.Y * 512.0f));
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerMoveInstantly, packet);
			}
		}
	}

	bool MpLevelHandler::IsPlayerStackingEnabled() const
	{
		return _networkManager->GetServerConfiguration().PlayerStacking;
	}

	Actors::ActorBase* MpLevelHandler::FindPlayerToStandOn(Actors::Player* player, float timeMult)
	{
		// Minimum horizontal overlap required to count as "on top of" the other player (not brushing its side)
		constexpr float MinHorizontalOverlap = 4.0f;
		// How close the feet must be to the other player's head to count as standing/landing on it
		constexpr float StandThreshold = 6.0f;

		auto& serverConfig = _networkManager->GetServerConfiguration();
		if (!serverConfig.PlayerStacking) {
			return nullptr;
		}

		// Only grab onto another player while falling or already resting, never while moving up (jumping off)
		if (player->_speed.Y < -0.1f) {
			return nullptr;
		}

		const AABBf& self = player->AABBInner;
		float feet = self.B;
		float nextFeet = feet + player->_speed.Y * timeMult;
		// Locate the other player's head from its position plus OUR collision head-offset (players are the same
		// size), not its AABB top: a client's RemoteActor uses the sprite box (taller than the collision box),
		// which would report the head ~16px too high and leave us floating.
		float headOffset = self.T - player->_pos.Y;

		// Treat the other player as a one-way platform: stand on it if our feet are at (or about to cross) its head
		// this frame, with enough horizontal overlap that we're really on top of it (not just brushing its side).
		auto isStandingOn = [&](Actors::ActorBase* actor) -> bool {
			const AABBf& o = actor->AABBInner;
			if (self.R - o.L < MinHorizontalOverlap || o.R - self.L < MinHorizontalOverlap) {
				return false;
			}
			float top = actor->_pos.Y + headOffset;
			return (feet <= top + StandThreshold && nextFeet >= top - StandThreshold);
		};

		if (_isServer) {
			for (auto* other : _players) {
				if (other == player) {
					continue;
				}
				auto* mpOther = static_cast<MpPlayer*>(other);
				if (mpOther->GetPeerDescriptor()->IsSpectating != SpectateMode::None) {
					continue;
				}
				if (isStandingOn(other)) {
					return other;
				}
			}
		} else {
			std::unique_lock lock(_lock);
			for (auto& [actorId, remoteActor] : _remoteActors) {
				if (_playerNames.find(actorId) == _playerNames.end()) {
					continue;
				}
				if (isStandingOn(remoteActor.get())) {
					return remoteActor.get();
				}
			}
		}
		return nullptr;
	}

	void MpLevelHandler::HandlePlayerRefreshAmmo(Actors::Player* player, WeaponType weaponType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(8);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::WeaponAmmo);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)weaponType);
				packet.WriteValue<std::uint16_t>((std::uint16_t)mpPlayer->_weaponAmmo[(std::uint8_t)weaponType]);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerRefreshWeaponUpgrades(Actors::Player* player, WeaponType weaponType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(7);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::WeaponUpgrades);
				packet.WriteVariableUint32(player->_playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)weaponType);
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_weaponUpgrades[(std::uint8_t)weaponType]);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerMorphTo(Actors::Player* player, PlayerType type)
	{
		// TODO: Only called by PlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			String metadataPath = fs::FromNativeSeparators(mpPlayer->_metadata->Path);

			MemoryStream packet(9 + metadataPath.size());
			packet.WriteVariableUint32(mpPlayer->_playerIndex);
			packet.WriteValue<std::uint8_t>(0); // Flags (Reserved)
			packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
			packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
			_networkManager->SendTo([otherPeer = peerDesc->RemotePeer](const Peer& peer) {
				return (peer != otherPeer);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChangeRemoteActorMetadata, packet);

			if (peerDesc->RemotePeer) {
				MemoryStream packet2(6);
				packet2.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::PlayerType);
				packet2.WriteVariableUint32(mpPlayer->_playerIndex);
				packet2.WriteValue<std::uint8_t>((std::uint8_t)type);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet2);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetDizzy(Actors::Player* player, float timeLeft)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Dizzy);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)timeLeft);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetBeingStoodOn(Actors::Player* player, bool beingStoodOn)
	{
		// The owning client predicts its own player and can't see a remote player standing on it, so the server (which
		// simulates everyone) tells it when to show/hide the cosmetic lift animation. Other peers already see it via
		// the normal remoting-actor animation sync.
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(6);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::BeingStoodOn);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>(beingStoodOn ? 1 : 0);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetShield(Actors::Player* player, ShieldType shieldType, float timeLeft)
	{
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);

			// Broadcast to every synchronized peer, not just the owning one: the owner applies it to its local
			// player while the other peers apply it to this player's RemoteActor, so they render the shield
			// decoration around the remote player too.
			MemoryStream packet(10);
			packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Shield);
			packet.WriteVariableUint32(mpPlayer->_playerIndex);
			packet.WriteValue<std::uint8_t>((std::uint8_t)shieldType);
			packet.WriteVariableInt32((std::int32_t)timeLeft);
			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
		}
	}

	void MpLevelHandler::HandlePlayerEmitWeaponFlare(Actors::Player* player)
	{
		// TODO: Only called by RemotePlayerOnServer
		if DEATH_LIKELY(_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(4);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerEmitWeaponFlare, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerWeaponChanged(Actors::Player* player, Actors::Player::SetCurrentWeaponReason reason)
	{
		if (_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(6);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)mpPlayer->_currentWeapon);
				packet.WriteValue<std::uint8_t>((std::uint8_t)reason);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerChangeWeapon, packet);
			}
		} else {
			auto* remotablePlayer = static_cast<Actors::Multiplayer::RemotablePlayer*>(player);
			if (!remotablePlayer->ChangingWeaponFromServer) {
				MemoryStream packet(5);
				packet.WriteVariableUint32(_lastSpawnedActorId);
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
				_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerChangeWeaponRequest, packet);
			}
		}
	}

	void MpLevelHandler::InitializeRequiredAssets()
	{
		_requiredAssets.clear();

		auto levelFullPath = GetAssetFullPath(AssetType::Level, _levelName);
		auto s = fs::Open(levelFullPath, FileAccess::Read);
		if (s->IsValid()) {
			_requiredAssets.emplace_back(AssetType::Level, _levelName, levelFullPath, s->GetSize(), nCine::crc32(*s));
		}

		auto usedTileSetPaths = _tileMap->GetUsedTileSetPaths();
		for (const auto& tileSetPath : usedTileSetPaths) {
			auto tileSetFullPath = GetAssetFullPath(AssetType::TileSet, tileSetPath);
			auto s = fs::Open(tileSetFullPath, FileAccess::Read);
			if (s->IsValid()) {
				_requiredAssets.emplace_back(AssetType::TileSet, tileSetPath, tileSetFullPath, s->GetSize(), nCine::crc32(*s));
			}
		}

		if (!_musicDefaultPath.empty()) {
			auto musicFullPath = GetAssetFullPath(AssetType::Music, _musicDefaultPath);
			auto s = fs::Open(musicFullPath, FileAccess::Read);
			if (s->IsValid()) {
				_requiredAssets.emplace_back(AssetType::Music, _musicDefaultPath, musicFullPath, s->GetSize(), nCine::crc32(*s));
			}
		}
	}

	void MpLevelHandler::SynchronizePeers(float timeMult)
	{
		for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
			if (peerDesc->TeamSwitchCooldown > 0.0f) {
				peerDesc->TeamSwitchCooldown -= timeMult;
			}

			if (peerDesc->LevelState == PeerLevelState::LevelLoaded) {
				if DEATH_LIKELY(peerDesc != nullptr && peerDesc->PreferredPlayerType != PlayerType::None) {
					peerDesc->LevelState = PeerLevelState::PlayerReady;
				} else {
					peerDesc->LevelState = PeerLevelState::LevelSynchronized;
				}

				LOGI("Syncing peer [{}]", peer);

				// Sync the game mode (incl. the team-coloring flag) to this peer BEFORE the remote actors below and
				// before its own player spawns next tick (PlayerReady branch). The client decides at spawn time whether
				// to load player sprites indexed (recolorable) based on the effective fur color, which depends on this
				// flag - if it arrived only at round start (after spawn), default-color players would load non-indexed
				// and could never be team-recolored. Reliable-ordered channel guarantees it lands first.
				SynchronizeGameMode();

				// TODO: Send positions only to the newly connected player
				CalculatePositionInRound(true);

				// Synchronize level state
				{
					MemoryStream packet(6);
					packet.WriteValue<std::uint8_t>((std::uint8_t)LevelPropertyType::State);
					packet.WriteValue<std::uint8_t>((std::uint8_t)_levelState);
					packet.WriteVariableInt32(_levelState == LevelState::WaitingForMinPlayers
						? _waitingForPlayerCount : (std::int32_t)(_gameTimeLeft * 100.0f));

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LevelSetProperty, packet);
				}

				// Synchronize tilemap
				{
					// TODO: Use deflate compression here?
					MemoryStream packet(40 * 1024);
					_tileMap->SerializeResumableToStream(packet);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncTileMap, packet);
				}

				// Synchronize music
				if (_musicCurrentPath != _musicDefaultPath) {
					MemoryStream packet(6 + _musicCurrentPath.size());
					packet.WriteValue<std::uint8_t>((std::uint8_t)LevelPropertyType::Music);
					packet.WriteValue<std::uint8_t>(0);
					packet.WriteVariableUint32((std::uint32_t)_musicCurrentPath.size());
					packet.Write(_musicCurrentPath.data(), (std::uint32_t)_musicCurrentPath.size());

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LevelSetProperty, packet);
				}

				// Synchronize race checkpoints for the minimap (only present in Race/TeamRace, and only if the
				// server allows the minimap - the ranking uses them server-side regardless)
				if (_networkManager->GetServerConfiguration().AllowMinimap && !_orderedRaceCheckpoints.empty()) {
					MemoryStream packet(24 + _orderedRaceCheckpoints.size() * 9 + _raceStartMarkers.size() * 8);
					packet.WriteVariableInt32(_raceBoundsMin.X);
					packet.WriteVariableInt32(_raceBoundsMin.Y);
					packet.WriteVariableInt32(_raceBoundsMax.X);
					packet.WriteVariableInt32(_raceBoundsMax.Y);
					packet.WriteVariableUint32((std::uint32_t)_orderedRaceCheckpoints.size());
					for (const auto& cp : _orderedRaceCheckpoints) {
						packet.WriteVariableInt32(cp.Tile.X);
						packet.WriteVariableInt32(cp.Tile.Y);
						packet.WriteVariableUint32(cp.Order);
						packet.WriteValue<std::uint8_t>(cp.Group);
					}
					packet.WriteVariableUint32((std::uint32_t)_raceStartMarkers.size());
					for (const auto& m : _raceStartMarkers) {
						packet.WriteVariableInt32(m.X);
						packet.WriteVariableInt32(m.Y);
					}

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncRaceCheckpoints, packet);
				}

				// Synchronize actors
				for (Actors::Player* otherPlayer : _players) {
					auto* mpOtherPlayer = static_cast<MpPlayer*>(otherPlayer);
					auto otherPeerDesc = mpOtherPlayer->GetPeerDescriptor();

					String metadataPath = fs::FromNativeSeparators(mpOtherPlayer->_metadata->Path);

					MemoryStream packet;
					InitializeCreateRemoteActorPacket(packet, mpOtherPlayer->_playerIndex, mpOtherPlayer);

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
					
					MemoryStream packet2(11 + otherPeerDesc->PlayerName.size());
					packet2.WriteVariableUint32(mpOtherPlayer->_playerIndex);
					packet2.WriteValue<std::uint8_t>(0x02 | 0x04); // HasFurColor | HasTeam
					packet2.WriteVariableUint32(otherPeerDesc->PlayerName.size());
					packet2.Write(otherPeerDesc->PlayerName.data(), (std::uint32_t)otherPeerDesc->PlayerName.size());
					packet2.WriteValueAsLE<std::uint32_t>(otherPeerDesc->FurColor);
					packet2.WriteValue<std::uint8_t>(otherPeerDesc->Team);

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet2);

					// If this player currently has an active shield, sync it so the joining peer renders the
					// decoration right away (shields are otherwise only broadcast when they change, which this
					// late joiner missed). The CreateRemoteActor above is sent first, so the actor already exists.
					if (mpOtherPlayer->_activeShield != ShieldType::None && mpOtherPlayer->_activeShieldTime > 0.0f) {
						MemoryStream packet3(10);
						packet3.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Shield);
						packet3.WriteVariableUint32(mpOtherPlayer->_playerIndex);
						packet3.WriteValue<std::uint8_t>((std::uint8_t)mpOtherPlayer->_activeShield);
						packet3.WriteVariableInt32((std::int32_t)mpOtherPlayer->_activeShieldTime);
						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet3);
					}
				}

				// TODO: Does this need to be locked?
				std::unique_lock lock(_lock);
				for (const auto& [remotingActor, remotingActorInfo] : _remotingActors) {
					if DEATH_UNLIKELY(ActorShouldBeMirrored(remotingActor)) {
						Vector2i originTile = remotingActor->_originTile;
						const auto& eventTile = _eventMap->GetEventTile(originTile.X, originTile.Y);
						if (eventTile.Event != EventType::Empty) {
							MemoryStream packet(24 + Events::EventSpawner::SpawnParamsSize);
							packet.WriteVariableUint32(remotingActorInfo.ActorID);
							packet.WriteVariableUint32((std::uint32_t)eventTile.Event);
							packet.Write(eventTile.EventParams, Events::EventSpawner::SpawnParamsSize);
							packet.WriteVariableUint32((std::uint32_t)eventTile.EventFlags);
							packet.WriteVariableInt32((std::int32_t)originTile.X);
							packet.WriteVariableInt32((std::int32_t)originTile.Y);
							packet.WriteVariableInt32((std::int32_t)remotingActor->_renderer.layer());

							_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateMirroredActor, packet);
						}
					} else {
						MemoryStream packet;
						InitializeCreateRemoteActorPacket(packet, remotingActorInfo.ActorID, remotingActor);

						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
					}
				}
			} else if (peerDesc->LevelState == PeerLevelState::PlayerReady) {
				auto& serverConfig = _networkManager->GetServerConfiguration();
				bool canSpawn = (_enableSpawning && _activeBoss == nullptr);
				
				// Check if joining mid-round is allowed
				if (_levelState == LevelState::Running && !serverConfig.AllowJoinDuringRound) {
					canSpawn = false;
				}

				peerDesc->LevelState = PeerLevelState::PlayerSpawned;

				if DEATH_LIKELY(canSpawn) {
					Vector2f spawnPosition = (serverConfig.GameMode == MpGameMode::Cooperation && _lastCheckpointPos != Vector2f::Zero
						? _lastCheckpointPos : GetSpawnPoint(peerDesc->PreferredPlayerType, peerDesc->Team));

					// TODO: Send ambient light (_lastCheckpointLight)

					std::uint8_t playerIndex = FindFreePlayerId();
					LOGI("Spawning player {} [{}]", playerIndex, peer);

					std::shared_ptr<Actors::Multiplayer::RemotePlayerOnServer> player = std::make_shared<Actors::Multiplayer::RemotePlayerOnServer>(peerDesc);
					// In team-coloring modes, resolve the team BEFORE activating so the spawn-time recolor decision
					// (GetEffectiveFurColor in OnActivatedAsync) loads the sprites indexed and applies the team color
					// even for players with no custom color of their own. ApplyGameModeToPlayer below re-resolves to the
					// same team and refreshes once the sprites are loaded.
					if (serverConfig.ColorizePlayersByTeam && IsTeamGameMode(serverConfig.GameMode)) {
						peerDesc->Team = ResolveTeam(player.get(), peerDesc->PreferredTeam);
					}
					std::uint8_t playerParams[2] = { (std::uint8_t)peerDesc->PreferredPlayerType, (std::uint8_t)playerIndex };
					player->OnActivated(Actors::ActorActivationDetails(
						this,
						Vector3i((std::int32_t)spawnPosition.X, (std::int32_t)spawnPosition.Y, PlayerZ - playerIndex),
						playerParams
					));
					player->_health = (serverConfig.InitialPlayerHealth > 0
						? serverConfig.InitialPlayerHealth
						: (PlayerShouldHaveUnlimitedHealth(serverConfig.GameMode) ? INT32_MAX : 5));

					// Spawning as a real player clears any stale spectate state (e.g., carried over from a previous
					// round or a reconnect), otherwise the player would be treated as spectating despite being active
					peerDesc->IsSpectating = SpectateMode::None;

					// Apply join cooldown if joining mid-round
					if (_levelState == LevelState::Running && serverConfig.JoinCooldownSecs > 0) {
						peerDesc->JoinCooldownFrames = serverConfig.JoinCooldownSecs * FrameTimer::FramesPerSecond;
						player->_controllableExternal = false;
					} else {
						peerDesc->JoinCooldownFrames = 0.0f;
						player->_controllableExternal = _controllableExternal;
					}

					peerDesc->LapsElapsedFrames = _elapsedFrames;
					peerDesc->LapStarted = TimeStamp::now();

					Actors::Multiplayer::RemotePlayerOnServer* ptr = player.get();
					_players.push_back(ptr);

					_suppressRemoting = true;
					AddActor(player);
					_suppressRemoting = false;

					ApplyGameModeToPlayer(serverConfig.GameMode, ptr);

					if (_autoWeightTreasure && (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt)) {
						SynchronizeGameMode();
					}

					// Spawn the player also on the remote side
					{
						std::uint8_t flags = 0;
						if (player->_controllable) {
							flags |= 0x01;
						}
						if (player->_controllableExternal) {
							flags |= 0x02;
						}

						MemoryStream packet(16);
						packet.WriteVariableUint32(playerIndex);
						packet.WriteValue<std::uint8_t>((std::uint8_t)player->_playerType);
						packet.WriteVariableInt32(player->_health);
						packet.WriteValue<std::uint8_t>(flags);
						packet.WriteValue<std::uint8_t>(peerDesc->Team);
						packet.WriteVariableInt32((std::int32_t)player->_pos.X);
						packet.WriteVariableInt32((std::int32_t)player->_pos.Y);
						// Forward carried-over progression (co-op level change / reconnect) to the client so it doesn't
						// start fresh; for a normal join there is none (flag = 0). The server-side player applies it in
						// MpPlayer::OnActivatedAsync (after the base reset); clear the flag once it has been serialized
						// so a later in-level respawn doesn't re-send/re-apply it.
						packet.WriteValue<std::uint8_t>(peerDesc->HasCarryOver ? 1 : 0);
						if (peerDesc->HasCarryOver) {
							WriteCarryOver(packet, peerDesc->CarryOver);
						}
						peerDesc->HasCarryOver = false;

						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateControllablePlayer, packet);
					}

					// The player is invulnerable for a short time after spawning
					ptr->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Blinking);

					if (_limitCameraLeft != 0 || _limitCameraWidth != 0) {
						Vector2f otherPlayerPos;
						for (auto* otherPlayer : _players) {
							if (otherPlayer != ptr) {
								otherPlayerPos = otherPlayer->_pos;
								break;
							}
						}

						MemoryStream packet(21);
						packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::LimitCameraView);
						packet.WriteVariableUint32(playerIndex);
						packet.WriteVariableInt32(_limitCameraLeft);
						packet.WriteVariableInt32(_limitCameraWidth);
						packet.WriteVariableInt32((std::int32_t)otherPlayerPos.X);
						packet.WriteVariableInt32((std::int32_t)otherPlayerPos.Y);
						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
					}

					// Create the player also on all other clients
					{
						MemoryStream packet;
						InitializeCreateRemoteActorPacket(packet, playerIndex, player.get());

						_networkManager->SendTo([this, self = peer](const Peer& peer) {
							if (peer == self) {
								return false;
							}
							auto peerDesc = _networkManager->GetPeerDescriptor(peer);
							return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
						}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
					}

					{
						MemoryStream packet(11 + peerDesc->PlayerName.size());
						packet.WriteVariableUint32(playerIndex);
						packet.WriteValue<std::uint8_t>(0x02 | 0x04); // HasFurColor | HasTeam
						packet.WriteVariableUint32(peerDesc->PlayerName.size());
						packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());
						packet.WriteValueAsLE<std::uint32_t>(peerDesc->FurColor);
						packet.WriteValue<std::uint8_t>(peerDesc->Team);

						_networkManager->SendTo([this](const Peer& peer) {
							auto peerDesc = _networkManager->GetPeerDescriptor(peer);
							return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
						}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet);
					}

					if DEATH_UNLIKELY(_levelState == LevelState::WaitingForMinPlayers) {
						_waitingForPlayerCount = (std::int32_t)serverConfig.MinPlayerCount - (std::int32_t)_players.size();
						SendLevelStateToAllPlayers();
					}
				} else {
					// Cannot spawn as normal player, spawn as spectate player instead
					Vector2f spawnPosition = GetSpawnPoint(peerDesc->PreferredPlayerType, peerDesc->Team);

					std::uint8_t playerIndex = FindFreePlayerId();
					LOGI("Spawning player {} [{}] as spectator", playerIndex, peer);

					std::shared_ptr<Actors::Multiplayer::RemotePlayerOnServer> player = std::make_shared<Actors::Multiplayer::RemotePlayerOnServer>(peerDesc);
					std::uint8_t playerParams[2] = { (std::uint8_t)PlayerType::Spectate, (std::uint8_t)playerIndex };
					player->OnActivated(Actors::ActorActivationDetails(
						this,
						Vector3i((std::int32_t)spawnPosition.X, (std::int32_t)spawnPosition.Y, PlayerZ - playerIndex),
						playerParams
					));
					player->_controllableExternal = true;
					player->_health = (serverConfig.InitialPlayerHealth > 0
						? serverConfig.InitialPlayerHealth
						: (PlayerShouldHaveUnlimitedHealth(serverConfig.GameMode) ? INT32_MAX : 5));

					// Set spectate mode
					peerDesc->IsSpectating = SpectateMode::Forced;
					if (serverConfig.EnableFreeCamera) {
						peerDesc->IsSpectating |= SpectateMode::FreeCamera;
					}

					Actors::Multiplayer::RemotePlayerOnServer* ptr = player.get();
					_players.push_back(ptr);

					_suppressRemoting = true;
					AddActor(player);
					_suppressRemoting = false;

					peerDesc->Player = ptr;

					// Spawn the spectate player on the remote side
					{
						std::uint8_t flags = 0;
						if (player->_controllable) {
							flags |= 0x01;
						}
						if (player->_controllableExternal) {
							flags |= 0x02;
						}

						MemoryStream packet(16);
						packet.WriteVariableUint32(playerIndex);
						packet.WriteValue<std::uint8_t>((std::uint8_t)player->_playerType);
						packet.WriteVariableInt32(player->_health);
						packet.WriteValue<std::uint8_t>(flags);
						packet.WriteValue<std::uint8_t>(peerDesc->Team);
						packet.WriteVariableInt32((std::int32_t)player->_pos.X);
						packet.WriteVariableInt32((std::int32_t)player->_pos.Y);
						// Forward carried-over progression (co-op level change / reconnect) to the client so it doesn't
						// start fresh; for a normal join there is none (flag = 0). The server-side player applies it in
						// MpPlayer::OnActivatedAsync (after the base reset); clear the flag once it has been serialized
						// so a later in-level respawn doesn't re-send/re-apply it.
						packet.WriteValue<std::uint8_t>(peerDesc->HasCarryOver ? 1 : 0);
						if (peerDesc->HasCarryOver) {
							WriteCarryOver(packet, peerDesc->CarryOver);
						}
						peerDesc->HasCarryOver = false;

						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateControllablePlayer, packet);
					}

					// Notify client about spectate mode
					{
						MemoryStream packet(6);
						packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Spectate);
						packet.WriteVariableUint32(playerIndex);
						packet.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->IsSpectating);
						_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
					}
				}
			} else if (peerDesc->LevelState == PeerLevelState::PlayerSpawned) {
				if DEATH_UNLIKELY(peerDesc->Player && peerDesc->JoinCooldownFrames > 0.0f) {
					peerDesc->JoinCooldownFrames -= timeMult;
					if (peerDesc->JoinCooldownFrames <= 0.0f) {
						// Join cooldown expired, make the player controllable
						peerDesc->Player->_controllableExternal = true;

						if (peerDesc->RemotePeer) {
							MemoryStream packet(6);
							packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Controllable);
							packet.WriteVariableUint32(peerDesc->Player->_playerIndex);
							packet.WriteValue<std::uint8_t>(1);
							_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
						}
					}
				}
			}
		}
	}

	std::uint32_t MpLevelHandler::FindFreeActorId()
	{
		for (std::uint32_t i = UINT8_MAX + 1; i < UINT32_MAX - 1; i++) {
			if (!_remoteActors.contains(i)) {
				return i;
			}
		}

		return UINT32_MAX;
	}

	std::uint8_t MpLevelHandler::FindFreePlayerId()
	{
		// Reserve ID 0 for the local player
		std::size_t count = _players.size();
		for (std::uint8_t i = 1; i < UINT8_MAX - 1; i++) {
			bool found = false;
			for (std::size_t j = 0; j < count; j++) {
				if (_players[j]->_playerIndex == i) {
					found = true;
					break;
				}
			}

			if (!found) {
				return i;
			}
		}

		return UINT8_MAX;
	}

	std::int32_t MpLevelHandler::GetNonSpectatePlayerCount()
	{
		std::int32_t count = 0;
		for (auto* player : _players) {
			if (player->_playerType != PlayerType::Spectate) {
				count++;
			}
		}
		return count;
	}

	bool MpLevelHandler::IsLocalPlayer(Actors::ActorBase* actor)
	{
		return (runtime_cast<Actors::Multiplayer::LocalPlayerOnServer>(actor) ||
				runtime_cast<Actors::Multiplayer::RemotablePlayer>(actor));
	}

	void MpLevelHandler::ApplyGameModeToAllPlayers(MpGameMode gameMode)
	{
		// Keep the active game-mode rules object in sync with the selected mode. This is the single point through
		// which the game mode is (re)applied, so it is also where the IGameMode is (re)built. It stays null for modes
		// not yet migrated to IGameMode, in which case callers fall back to the legacy switch-based logic.
		_gameMode = CreateGameMode(gameMode);

		if (gameMode == MpGameMode::Cooperation) {
			// Everyone in a single team
			for (auto* player : _players) {
				static_cast<MpPlayer*>(player)->GetPeerDescriptor()->Team = 0;
			}
			return;
		}
		if (!IsTeamGameMode(gameMode)) {
			// Each player is in their own team (free-for-all)
			for (auto* player : _players) {
				std::int32_t playerIdx = player->_playerIndex;
				DEATH_DEBUG_ASSERT(0 <= playerIdx && playerIdx < UINT8_MAX);
				static_cast<MpPlayer*>(player)->GetPeerDescriptor()->Team = (std::uint8_t)playerIdx;
			}
			return;
		}

		// Team mode: (re)distribute players into balanced teams, honoring per-player preferences where they
		// keep the teams balanced. Players whose team was forced (TeamLocked) keep it. Unassigned players are
		// marked out of range so the incremental balancing in ResolveTeam() doesn't count them prematurely.
		std::uint8_t teamCount = GetTeamCount();
		SmallVector<MpPlayer*, 0> toAssign;
		for (auto* player : _players) {
			if (player->_playerType == PlayerType::Spectate) {
				continue;
			}
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();
			if (peerDesc->TeamLocked && peerDesc->Team < teamCount) {
				continue;
			}
			peerDesc->Team = NoPreferredTeam;
			toAssign.push_back(mpPlayer);
		}

		Random().Shuffle(arrayView(toAssign));

		for (auto* mpPlayer : toAssign) {
			auto peerDesc = mpPlayer->GetPeerDescriptor();
			peerDesc->Team = ResolveTeam(mpPlayer, peerDesc->PreferredTeam);
		}

		// Broadcast every player's (re)assigned team so all clients update their team colors, nametags and
		// scoreboard - SynchronizeGameMode only tells each peer its own team, not the others'. This also recolors
		// the host's own view (a no-op when team coloring is off or on a headless server).
		for (auto* player : _players) {
			BroadcastPlayerTeam(static_cast<MpPlayer*>(player));
		}
	}

	void MpLevelHandler::ApplyGameModeToPlayer(MpGameMode gameMode, Actors::Player* player)
	{
		auto* mpPlayer = static_cast<MpPlayer*>(player);
		auto peerDesc = mpPlayer->GetPeerDescriptor();
		peerDesc->Team = ResolveTeam(mpPlayer, peerDesc->PreferredTeam);
		// Recolor for the assigned team (no-op when team coloring is off or on a headless server)
		player->RefreshColorPalette();
	}

	std::uint8_t MpLevelHandler::GetTeamCount() const
	{
		auto& serverConfig = _networkManager->GetServerConfiguration();
		std::uint8_t count = serverConfig.TeamCount;
		if (count < 2) {
			count = 2;
		} else if (count > MaxTeamCount) {
			count = MaxTeamCount;
		}
		return count;
	}

	std::uint8_t MpLevelHandler::FindSmallestTeam(MpPlayer* exclude)
	{
		std::uint8_t teamCount = GetTeamCount();
		std::int32_t counts[MaxTeamCount] = {};
		for (auto* player : _players) {
			if (player == exclude || player->_playerType == PlayerType::Spectate) {
				continue;
			}
			std::uint8_t team = static_cast<MpPlayer*>(player)->GetPeerDescriptor()->Team;
			if (team < teamCount) {
				counts[team]++;
			}
		}

		// Find the smallest team(s); break ties randomly so repeated joins don't always stack the same team
		std::int32_t minCount = INT32_MAX;
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (counts[team] < minCount) {
				minCount = counts[team];
			}
		}
		SmallVector<std::uint8_t, MaxTeamCount> smallest;
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (counts[team] == minCount) {
				smallest.push_back(team);
			}
		}
		return smallest[Random().Next(0, smallest.size())];
	}

	std::uint8_t MpLevelHandler::ResolveTeam(MpPlayer* player, std::uint8_t requested)
	{
		auto& serverConfig = _networkManager->GetServerConfiguration();
		if (serverConfig.GameMode == MpGameMode::Cooperation) {
			// Everyone in a single team
			return 0;
		}
		if (!IsTeamGameMode(serverConfig.GameMode)) {
			// Each player is in their own team (free-for-all)
			std::int32_t playerIdx = player->_playerIndex;
			return (std::uint8_t)(playerIdx >= 0 && playerIdx < UINT8_MAX ? playerIdx : 0);
		}

		std::uint8_t teamCount = GetTeamCount();
		std::uint8_t smallest = FindSmallestTeam(player);
		if (requested >= teamCount) {
			// No (valid) preference - balance the player into the smallest team
			return smallest;
		}
		if (!serverConfig.AutoBalanceTeams) {
			// Balancing is disabled, honor the requested team unconditionally
			return requested;
		}

		// Honor the requested team only if joining it doesn't break the size limit
		std::int32_t counts[MaxTeamCount] = {};
		for (auto* other : _players) {
			if (other == player || other->_playerType == PlayerType::Spectate) {
				continue;
			}
			std::uint8_t team = static_cast<MpPlayer*>(other)->GetPeerDescriptor()->Team;
			if (team < teamCount) {
				counts[team]++;
			}
		}
		std::int32_t minCount = INT32_MAX;
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (counts[team] < minCount) {
				minCount = counts[team];
			}
		}
		if (counts[requested] - minCount < (std::int32_t)serverConfig.MaxTeamSizeDiff) {
			return requested;
		}
		return smallest;
	}

	bool MpLevelHandler::ChangePlayerTeam(MpPlayer* player, std::uint8_t requestedTeam, bool fromAdmin)
	{
		if (!_isServer) {
			return false;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		if (!IsTeamGameMode(serverConfig.GameMode)) {
			return false;
		}

		auto peerDesc = player->GetPeerDescriptor();
		std::uint8_t teamCount = GetTeamCount();
		std::uint8_t newTeam;

		if (fromAdmin) {
			if (requestedTeam >= teamCount) {
				return false;
			}
			peerDesc->PreferredTeam = requestedTeam;
			peerDesc->TeamLocked = true;
			newTeam = requestedTeam;
		} else {
			if (!serverConfig.AllowTeamSelection || peerDesc->TeamLocked) {
				return false;
			}
			if (peerDesc->TeamSwitchCooldown > 0.0f) {
				return false;
			}
			if (requestedTeam != NoPreferredTeam && requestedTeam >= teamCount) {
				return false;
			}
			peerDesc->PreferredTeam = requestedTeam;
			newTeam = ResolveTeam(player, requestedTeam);
			// If the player explicitly requested a specific (full) team but balancing moved them elsewhere, reject
			if (requestedTeam != NoPreferredTeam && newTeam != requestedTeam) {
				return false;
			}
		}

		if (newTeam == peerDesc->Team) {
			// Already on the requested team, nothing to do (still counts as success)
			return true;
		}

		peerDesc->Team = newTeam;
		peerDesc->TeamSwitchCooldown = TeamSwitchCooldownFrames;

		BroadcastPlayerTeam(player);

		// Relocate the player to a friendly spawn point so a mid-round switch doesn't leave them behind enemy lines
		if (_levelState == LevelState::Running) {
			Vector2f spawnPos = GetSpawnPoint(peerDesc->PreferredPlayerType, newTeam);
			player->_checkpointPos = spawnPos;
			player->Respawn(spawnPos);

			if (peerDesc->RemotePeer) {
				peerDesc->LastUpdated = UINT64_MAX;
				static_cast<PlayerOnServer*>(player)->_canTakeDamage = false;

				MemoryStream packet(12);
				packet.WriteVariableUint32(player->_playerIndex);
				packet.WriteValue<std::int32_t>((std::int32_t)(spawnPos.X * 512.0f));
				packet.WriteValue<std::int32_t>((std::int32_t)(spawnPos.Y * 512.0f));
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet);
			}
		}

		return true;
	}

	void MpLevelHandler::RebalanceTeams(bool force)
	{
		if (!_isServer) {
			return;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		if (!IsTeamGameMode(serverConfig.GameMode)) {
			return;
		}
		if (!serverConfig.AutoBalanceTeams && !force) {
			return;
		}

		std::uint8_t teamCount = GetTeamCount();
		bool anyChanged = false;

		// Each iteration moves one player from the largest team to the smallest; this always reduces the spread,
		// so the loop terminates. The safety counter is just a hard backstop.
		for (std::int32_t safety = 0; safety < 64; safety++) {
			std::int32_t counts[MaxTeamCount] = {};
			for (auto* player : _players) {
				if (player->_playerType == PlayerType::Spectate) {
					continue;
				}
				std::uint8_t team = static_cast<MpPlayer*>(player)->GetPeerDescriptor()->Team;
				if (team < teamCount) {
					counts[team]++;
				}
			}

			std::uint8_t largestTeam = 0, smallestTeam = 0;
			for (std::uint8_t team = 1; team < teamCount; team++) {
				if (counts[team] > counts[largestTeam]) {
					largestTeam = team;
				}
				if (counts[team] < counts[smallestTeam]) {
					smallestTeam = team;
				}
			}

			if (counts[largestTeam] - counts[smallestTeam] <= (std::int32_t)serverConfig.MaxTeamSizeDiff) {
				break;
			}

			// Move the lowest-scoring eligible player from the largest team to keep the disruption minimal.
			// Locked players are never auto-moved.
			MpPlayer* victim = nullptr;
			std::uint32_t victimScore = UINT32_MAX;
			for (auto* player : _players) {
				if (player->_playerType == PlayerType::Spectate) {
					continue;
				}
				auto* mpPlayer = static_cast<MpPlayer*>(player);
				auto peerDesc = mpPlayer->GetPeerDescriptor();
				if (peerDesc->Team != largestTeam || peerDesc->TeamLocked) {
					continue;
				}
				std::uint32_t score = peerDesc->Kills + peerDesc->Laps + peerDesc->TreasureCollected;
				if (score < victimScore) {
					victimScore = score;
					victim = mpPlayer;
				}
			}

			if (victim == nullptr) {
				// No eligible player to move (e.g., all locked); give up to avoid an infinite loop
				break;
			}

			auto peerDesc = victim->GetPeerDescriptor();
			peerDesc->Team = smallestTeam;
			peerDesc->TeamSwitchCooldown = TeamSwitchCooldownFrames;
			BroadcastPlayerTeam(victim);

			if (_levelState == LevelState::Running) {
				Vector2f spawnPos = GetSpawnPoint(peerDesc->PreferredPlayerType, smallestTeam);
				victim->_checkpointPos = spawnPos;
				victim->Respawn(spawnPos);

				if (peerDesc->RemotePeer) {
					peerDesc->LastUpdated = UINT64_MAX;
					static_cast<PlayerOnServer*>(victim)->_canTakeDamage = false;

					MemoryStream packet(12);
					packet.WriteVariableUint32(victim->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(spawnPos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(spawnPos.Y * 512.0f));
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet);
				}
			}

			anyChanged = true;
		}

		if (anyChanged) {
			ShowAlertToAllPlayers(_("\n\nTeams have been rebalanced"));
		}
	}

	void MpLevelHandler::BroadcastPlayerTeam(MpPlayer* player)
	{
		if (!_isServer) {
			return;
		}

		auto peerDesc = player->GetPeerDescriptor();

		// Recolor the player for the host's own view (team coloring resolves the new team via GetEffectiveFurColor;
		// no-op when disabled or on a headless server)
		player->RefreshColorPalette();

		MemoryStream packet(8);
		packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Team);
		packet.WriteVariableUint32(player->_playerIndex);
		packet.WriteValue<std::uint8_t>(peerDesc->Team);

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
	}

	std::uint32_t MpLevelHandler::ColorizeFurForTeam(std::uint32_t furColor, std::uint8_t team) const
	{
		// In team modes with team coloring enabled, force the primary fur color section to the team color. Kept
		// consistent on server and clients - both have the synced ColorizePlayersByTeam flag and team ids.
		const auto& serverConfig = _networkManager->GetServerConfiguration();
		if (serverConfig.ColorizePlayersByTeam && IsTeamGameMode(serverConfig.GameMode)) {
			return ApplyTeamFurColor(furColor, team);
		}
		return furColor;
	}

	void MpLevelHandler::RecolorRemoteActor(std::uint32_t actorId, std::uint32_t furColor, std::uint8_t team)
	{
		// (Re)applies the effective (team-aware) fur color to a remote player actor if it currently exists. Looks the
		// actor up under the lock, then recolors after releasing it - SetPlayerColor reloads metadata (file I/O),
		// which must not run while holding the spinlock.
		std::uint32_t effectiveColor = ColorizeFurForTeam(furColor, team);
		std::shared_ptr<Actors::ActorBase> actor;
		{
			std::unique_lock lock(_lock);
			auto it = _remoteActors.find(actorId);
			if (it != _remoteActors.end()) {
				actor = it->second;
			}
		}
		if (auto* remoteActor = runtime_cast<Actors::Multiplayer::RemoteActor>(actor.get())) {
			remoteActor->SetPlayerColor(effectiveColor);
		}
	}

	std::uint32_t MpLevelHandler::GetTeamScore(std::uint8_t team)
	{
		auto& serverConfig = _networkManager->GetServerConfiguration();
		if (serverConfig.GameMode == MpGameMode::CaptureTheFlag) {
			return (team < MaxTeamCount ? _ctfCaptures[team] : 0);
		}

		std::uint32_t total = 0;
		for (auto* player : _players) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();
			if (peerDesc->Team != team || peerDesc->IsSpectating != SpectateMode::None) {
				continue;
			}
			switch (serverConfig.GameMode) {
				case MpGameMode::TeamRace: total += peerDesc->Laps; break;
				case MpGameMode::TeamTreasureHunt: total += peerDesc->TreasureCollected; break;
				default: total += peerDesc->Kills; break;
			}
		}
		return total;
	}

	void MpLevelHandler::SyncTeamScores()
	{
		if DEATH_UNLIKELY(!_isServer) {
			return;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		if (!IsTeamGameMode(serverConfig.GameMode)) {
			return;
		}

		std::uint8_t teamCount = GetTeamCount();
		_teamScores.resize_for_overwrite(teamCount);

		bool isCtf = (serverConfig.GameMode == MpGameMode::CaptureTheFlag);

		MemoryStream packet(2 + teamCount * 24);
		packet.WriteValue<std::uint8_t>(teamCount);
		for (std::uint8_t team = 0; team < teamCount; team++) {
			std::uint32_t score = GetTeamScore(team);
			_teamScores[team] = score;
			packet.WriteVariableUint32(score);
		}
		// In Capture The Flag also send each team's flag state (home/taken/dropped), its base and drop positions and
		// the carrier id, so clients can render the flag/base as local actors driven by this state (rather than relying
		// on actor remoting). Mirrored into _ctfFlagStates locally too (unused on the host, which uses its own actors).
		packet.WriteValue<std::uint8_t>(isCtf ? 1 : 0);
		if (isCtf) {
			// Keep the local mirror in sync for the HUD (the host reads _ctfFlagStates[].State the same as clients).
			// The host doesn't use the FlagActor/BaseActor pointers here - it renders its own _ctfFlags actors.
			// resize() (not resize_for_overwrite) because CtfClientFlag holds shared_ptr members that must be inited.
			if ((std::uint8_t)_ctfFlagStates.size() != teamCount) {
				_ctfFlagStates.resize(teamCount);
			}
			for (std::uint8_t team = 0; team < teamCount; team++) {
				CtfFlag* flag = nullptr;
				for (auto& f : _ctfFlags) {
					if (f.Team == team) {
						flag = &f;
						break;
					}
				}

				std::uint8_t state = (flag != nullptr ? (std::uint8_t)flag->State : (std::uint8_t)CtfFlagState::AtBase);
				Vector2f basePos = (flag != nullptr ? flag->BasePos : Vector2f::Zero);
				Vector2f dropPos = (flag != nullptr ? flag->DropPos : Vector2f::Zero);
				std::uint32_t carrierId = (flag != nullptr && flag->State == CtfFlagState::Carried ? flag->CarrierPlayerIndex : 0);

				_ctfFlagStates[team].State = state;
				_ctfFlagStates[team].BasePos = basePos;
				_ctfFlagStates[team].DropPos = dropPos;
				_ctfFlagStates[team].CarrierActorId = carrierId;

				packet.WriteValue<std::uint8_t>(state);
				packet.WriteVariableInt32((std::int32_t)basePos.X);
				packet.WriteVariableInt32((std::int32_t)basePos.Y);
				packet.WriteVariableInt32((std::int32_t)dropPos.X);
				packet.WriteVariableInt32((std::int32_t)dropPos.Y);
				packet.WriteVariableUint32(carrierId);
			}
		}

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncTeamScores, packet);
	}

	void MpLevelHandler::BuildCtfBases()
	{
		// Remove any flag/base actors from a previous build / game mode before rebuilding
		for (auto& flag : _ctfFlags) {
			if (flag.Actor != nullptr) {
				flag.Actor->SetState(Actors::ActorState::IsDestroyed, true);
			}
			if (flag.BaseActor != nullptr) {
				flag.BaseActor->SetState(Actors::ActorState::IsDestroyed, true);
			}
		}
		_ctfFlags.clear();
		std::memset(_ctfCaptures, 0, sizeof(_ctfCaptures));

		auto& serverConfig = _networkManager->GetServerConfiguration();
		if (serverConfig.GameMode != MpGameMode::CaptureTheFlag) {
			return;
		}

		std::uint8_t teamCount = GetTeamCount();

		// Collect authored CTF base positions from the level (JJ2 CtfBase events carry the team in param 0)
		Vector2f basePos[MaxTeamCount];
		bool baseFound[MaxTeamCount] = {};
		_eventMap->ForEachEvent([&](Events::EventMap::EventTile& e, std::int32_t x, std::int32_t y) {
			if (e.Event == EventType::CtfBase) {
				std::uint8_t team = e.EventParams[0];
				if (team < teamCount && !baseFound[team]) {
					baseFound[team] = true;
					basePos[team] = Vector2f(x * Tiles::TileSet::DefaultTileSize, y * Tiles::TileSet::DefaultTileSize - 8.0f);
				}
			}
			return true;
		});

		for (std::uint8_t team = 0; team < teamCount; team++) {
			// Fall back to the team's multiplayer spawn point if the level has no authored base for this team
			Vector2f pos = (baseFound[team] ? basePos[team] : GetSpawnPoint(PlayerType::Jazz, team));
			// The base/flag sprites are anchored one tile above the ground, so shift them down a tile to sit on it
			pos.Y += Tiles::TileSet::DefaultTileSize;

			CtfFlag flag = {};
			flag.Team = team;
			flag.BasePos = pos;
			flag.DropPos = pos;
			flag.State = CtfFlagState::AtBase;
			flag.CarrierPlayerIndex = UINT32_MAX;

			std::uint8_t actorParams[1] = { team };

			// The flag/base are visualized locally on each side (host here, clients via UpdateCtfClient) driven by
			// synced state, so they are NOT remoted as actors - that proved unreliable (id reuse showed the wrong
			// sprite). Suppress remoting so these stay server-local for the host's own view.
			_suppressRemoting = true;

			// Static home-base structure (drawn behind the flag)
			auto baseActor = std::make_shared<Actors::Multiplayer::CtfBase>();
			baseActor->OnActivated(Actors::ActorActivationDetails(this,
				Vector3i((std::int32_t)pos.X, (std::int32_t)pos.Y, MainPlaneZ - 40), actorParams));
			flag.BaseActor = baseActor;
			AddActor(baseActor);

			// Carryable flag (drawn in front of the base; the server moves it to follow its carrier)
			auto flagActor = std::make_shared<Actors::Multiplayer::Flag>();
			flagActor->OnActivated(Actors::ActorActivationDetails(this,
				Vector3i((std::int32_t)pos.X, (std::int32_t)pos.Y, MainPlaneZ - 30), actorParams));
			flag.Actor = flagActor;
			AddActor(flagActor);

			_suppressRemoting = false;

			_ctfFlags.push_back(std::move(flag));
		}

		// Push the new flag/base positions and state to clients right away so they can place their local visuals
		SyncTeamScores();
	}

	void MpLevelHandler::UpdateCtf(float timeMult)
	{
		if DEATH_UNLIKELY(!_isServer || _ctfFlags.empty() || _levelState != LevelState::Running) {
			return;
		}

		bool stateChanged = false;

		// Resolve the carrier actor for each carried flag and make the flag follow it. If the carrier is gone
		// (disconnected, spectating, or dead) the flag is dropped where it last was.
		for (auto& flag : _ctfFlags) {
			if (flag.State != CtfFlagState::Carried) {
				continue;
			}

			MpPlayer* carrier = nullptr;
			for (auto* player : _players) {
				auto* mpPlayer = static_cast<MpPlayer*>(player);
				if (mpPlayer->_playerIndex == flag.CarrierPlayerIndex &&
					mpPlayer->GetPeerDescriptor()->IsSpectating == SpectateMode::None && mpPlayer->_health > 0) {
					carrier = mpPlayer;
					break;
				}
			}

			if (carrier != nullptr) {
				flag.DropPos = carrier->_pos;	// Drop point stays at the carrier's feet, not the banner offset
				if (flag.Actor != nullptr) {
					flag.Actor->MoveInstantly(carrier->_pos - Vector2f(0.0f, 20.0f), Actors::MoveType::Absolute | Actors::MoveType::Force);
				}
			} else {
				flag.State = CtfFlagState::Dropped;
				flag.CarrierPlayerIndex = UINT32_MAX;
				if (flag.Actor != nullptr) {
					flag.Actor->MoveInstantly(flag.DropPos, Actors::MoveType::Absolute | Actors::MoveType::Force);
				}
				stateChanged = true;
			}
		}

		// Pickups (enemy flag) and returns (own dropped flag) on touch
		for (auto* player : _players) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();
			if (peerDesc->IsSpectating != SpectateMode::None || mpPlayer->_health <= 0) {
				continue;
			}

			std::uint8_t playerTeam = peerDesc->Team;
			for (auto& flag : _ctfFlags) {
				if (flag.State == CtfFlagState::Carried) {
					continue;
				}

				Vector2f flagPos = (flag.State == CtfFlagState::AtBase ? flag.BasePos : flag.DropPos);
				if ((mpPlayer->_pos - flagPos).Length() > CtfTouchRadius) {
					continue;
				}

				if (flag.Team != playerTeam) {
					// Enemy flag taken
					flag.State = CtfFlagState::Carried;
					flag.CarrierPlayerIndex = mpPlayer->_playerIndex;
					ShowAlertToAllPlayers(_f("\n\n{} took the {} flag!", peerDesc->PlayerName, GetTeamName(flag.Team)));
					stateChanged = true;
				} else if (flag.State == CtfFlagState::Dropped) {
					// Own flag returned to base
					flag.State = CtfFlagState::AtBase;
					if (flag.Actor != nullptr) {
						flag.Actor->MoveInstantly(flag.BasePos, Actors::MoveType::Absolute | Actors::MoveType::Force);
					}
					ShowAlertToAllPlayers(_f("\n\nThe {} flag was returned", GetTeamName(flag.Team)));
					stateChanged = true;
				}
			}
		}

		// Captures: a player carrying an enemy flag reaches their own base while their own flag is home
		for (auto& flag : _ctfFlags) {
			if (flag.State != CtfFlagState::Carried) {
				continue;
			}

			MpPlayer* carrier = nullptr;
			for (auto* player : _players) {
				auto* mpPlayer = static_cast<MpPlayer*>(player);
				if (mpPlayer->_playerIndex == flag.CarrierPlayerIndex) {
					carrier = mpPlayer;
					break;
				}
			}
			if (carrier == nullptr) {
				continue;
			}

			std::uint8_t carrierTeam = carrier->GetPeerDescriptor()->Team;
			CtfFlag* ownFlag = nullptr;
			for (auto& f : _ctfFlags) {
				if (f.Team == carrierTeam) {
					ownFlag = &f;
					break;
				}
			}
			if (ownFlag == nullptr || ownFlag->State != CtfFlagState::AtBase) {
				continue;
			}

			if ((carrier->_pos - ownFlag->BasePos).Length() <= CtfTouchRadius) {
				if (carrierTeam < MaxTeamCount) {
					_ctfCaptures[carrierTeam]++;
				}

				// Return the captured flag to its base
				flag.State = CtfFlagState::AtBase;
				flag.CarrierPlayerIndex = UINT32_MAX;
				if (flag.Actor != nullptr) {
					flag.Actor->MoveInstantly(flag.BasePos, Actors::MoveType::Absolute | Actors::MoveType::Force);
				}

				ShowAlertToAllPlayers(_f("\n\n{} team captured the {} flag!", GetTeamName(carrierTeam), GetTeamName(flag.Team)));
				stateChanged = true;
				CheckGameEnds();
			}
		}

		if (stateChanged) {
			SyncTeamScores();
		}
	}

	void MpLevelHandler::UpdateCtfClient()
	{
		// Client-only: the flag/base are rendered as client-local actors (not remoted), created on demand and
		// positioned each frame from the synced state. The carried flag follows its carrier's already-smoothed/
		// predicted position, so it never lags or jitters relative to the player.
		if DEATH_UNLIKELY(_isServer || _ctfFlagStates.empty()) {
			return;
		}

		for (std::uint8_t team = 0; team < (std::uint8_t)_ctfFlagStates.size(); team++) {
			auto& info = _ctfFlagStates[team];

			// The server sends zeroed positions until it has actually built the flags (at round start); don't
			// create the local visuals until a real base position is known
			if (info.BasePos == Vector2f::Zero) {
				continue;
			}

			// Lazily create the local base + flag actors for this team
			if (info.BaseActor == nullptr) {
				auto baseActor = std::make_shared<Actors::Multiplayer::CtfBase>();
				std::uint8_t params[1] = { team };
				baseActor->OnActivated(Actors::ActorActivationDetails(this,
					Vector3i((std::int32_t)info.BasePos.X, (std::int32_t)info.BasePos.Y, MainPlaneZ - 40), params));
				info.BaseActor = baseActor;
				AddActor(baseActor);
			}
			if (info.FlagActor == nullptr) {
				auto flagActor = std::make_shared<Actors::Multiplayer::Flag>();
				std::uint8_t params[1] = { team };
				flagActor->OnActivated(Actors::ActorActivationDetails(this,
					Vector3i((std::int32_t)info.BasePos.X, (std::int32_t)info.BasePos.Y, MainPlaneZ - 30), params));
				info.FlagActor = flagActor;
				AddActor(flagActor);
			}

			// Keep the base on its home position
			info.BaseActor->MoveInstantly(info.BasePos, Actors::MoveType::Absolute | Actors::MoveType::Force);

			// Position the flag according to its state
			Vector2f flagPos;
			if (info.State == (std::uint8_t)CtfFlagState::Carried) {
				bool found = false;
				if (info.CarrierActorId == _lastSpawnedActorId) {
					if (!_players.empty()) {
						flagPos = _players[0]->GetPos();
						found = true;
					}
				} else {
					std::shared_ptr<Actors::ActorBase> carrierActor;
					{
						std::unique_lock lock(_lock);
						auto it = _remoteActors.find(info.CarrierActorId);
						if (it != _remoteActors.end()) {
							carrierActor = it->second;
						}
					}
					if (carrierActor != nullptr) {
						flagPos = carrierActor->GetPos();
						found = true;
					}
				}
				if (!found) {
					flagPos = info.DropPos;
				} else {
					flagPos.Y -= 20.0f;	// Sit above the carrier's head like a banner
				}
			} else if (info.State == (std::uint8_t)CtfFlagState::Dropped) {
				flagPos = info.DropPos;
			} else {
				flagPos = info.BasePos;
			}

			info.FlagActor->MoveInstantly(flagPos, Actors::MoveType::Absolute | Actors::MoveType::Force);
		}
	}

	void MpLevelHandler::DropCtfFlag(Actors::Player* player)
	{
		if DEATH_UNLIKELY(!_isServer || _ctfFlags.empty()) {
			return;
		}

		std::uint32_t playerIndex = static_cast<MpPlayer*>(player)->_playerIndex;
		for (auto& flag : _ctfFlags) {
			if (flag.State == CtfFlagState::Carried && flag.CarrierPlayerIndex == playerIndex) {
				flag.State = CtfFlagState::Dropped;
				flag.CarrierPlayerIndex = UINT32_MAX;
				flag.DropPos = player->_pos;
				if (flag.Actor != nullptr) {
					flag.Actor->MoveInstantly(player->_pos, Actors::MoveType::Absolute | Actors::MoveType::Force);
				}
				ShowAlertToAllPlayers(_f("\n\nThe {} flag was dropped", GetTeamName(flag.Team)));
				SyncTeamScores();
			}
		}
	}

	void MpLevelHandler::BuildScoreboard()
	{
		if (!_isServer) {
			return;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		MpGameMode gameMode = serverConfig.GameMode;

		_scoreboard.clear();

		auto getExtra = [gameMode](const auto& peerDesc) -> std::uint32_t {
			switch (gameMode) {
				case MpGameMode::Race:
				case MpGameMode::TeamRace: return peerDesc->Laps;
				case MpGameMode::TreasureHunt:
				case MpGameMode::TeamTreasureHunt: return peerDesc->TreasureCollected;
				default: return 0;
			}
		};

		auto peers = _networkManager->GetPeers();
		std::uint32_t count = 0;
		for (auto& [peer, peerDesc] : *peers) {
			if (peerDesc->Player != nullptr) {
				count++;
			}
		}

		MemoryStream packet(8 + count * 24);
		packet.WriteVariableUint32(count);
		for (auto& [peer, peerDesc] : *peers) {
			if (peerDesc->Player == nullptr) {
				continue;
			}

			std::uint32_t extra = getExtra(peerDesc);
			std::int32_t ping = (peerDesc->RemotePeer ? _networkManager->GetRoundTripTimeMs(peerDesc->RemotePeer) : -1);

			PlayerScore score;
			score.Name = peerDesc->PlayerName;
			score.Team = peerDesc->Team;
			score.Kills = peerDesc->Kills;
			score.Deaths = peerDesc->Deaths;
			score.Points = peerDesc->Points;
			score.Extra = extra;
			score.PingMs = ping;
			score.IsLocal = !peerDesc->RemotePeer;
			_scoreboard.push_back(std::move(score));

			packet.WriteVariableUint32(peerDesc->Player->_playerIndex);
			packet.WriteValue<std::uint8_t>(peerDesc->Team);
			packet.WriteVariableUint32(peerDesc->Kills);
			packet.WriteVariableUint32(peerDesc->Deaths);
			packet.WriteVariableUint32(peerDesc->Points);
			packet.WriteVariableUint32(extra);
			packet.WriteVariableInt32(ping);
			packet.WriteVariableUint32((std::uint32_t)peerDesc->PlayerName.size());
			packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());
		}

		peers.unlock();

		// Highest score first; sorted locally so the host renders the same order as clients (which sort on receipt)
		nCine::sort(_scoreboard.begin(), _scoreboard.end(), [](const PlayerScore& a, const PlayerScore& b) {
			return (a.Points != b.Points ? a.Points > b.Points : a.Kills > b.Kills);
		});

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncScoreboard, packet);
	}

	void MpLevelHandler::ShowAlertToAllPlayers(StringView text)
	{
		if (text.empty()) {
			return;
		}

		ShowLevelText(text);

		std::uint8_t flags = 0;

		MemoryStream packet(5 + text.size());
		packet.WriteValue<std::uint8_t>(flags);
		packet.WriteVariableUint32((std::uint32_t)text.size());
		packet.Write(text.data(), (std::uint32_t)text.size());

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState != PeerLevelState::Unknown);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ShowAlert, packet);
	}

	void MpLevelHandler::SetControllableToAllPlayers(bool enable)
	{
		_controllableExternal = enable;

		for (auto* player : _players) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			mpPlayer->_controllableExternal = enable;

			if (peerDesc->RemotePeer) {
				MemoryStream packet(6);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Controllable);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>(enable ? 1 : 0);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::SendLevelStateToAllPlayers()
	{
		MemoryStream packet(6);
		packet.WriteValue<std::uint8_t>((std::uint8_t)LevelPropertyType::State);
		packet.WriteValue<std::uint8_t>((std::uint8_t)_levelState);
		packet.WriteVariableInt32(_levelState == LevelState::WaitingForMinPlayers
			? _waitingForPlayerCount : (std::int32_t)(_gameTimeLeft * 100.0f));

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelLoaded);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LevelSetProperty, packet);
	}

	void MpLevelHandler::ResetAllPlayerStats()
	{
		auto& serverConfig = _networkManager->GetServerConfiguration();

		// A new round invalidates any retained reconnect state, so reconnecting players reset like everyone else
		_networkManager->ClearDisconnectedPeerStates();

		for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
			peerDesc->PositionInRound = 0;
			peerDesc->Deaths = 0;
			peerDesc->Kills = 0;
			peerDesc->Laps = 0;
			peerDesc->TreasureCollected = 0;
			peerDesc->DeathElapsedFrames = FLT_MAX;

			if (peerDesc->Player) {
				peerDesc->Player->_coins = 0;
				std::memset(peerDesc->Player->_gems, 0, sizeof(peerDesc->Player->_gems));
				std::memset(peerDesc->Player->_weaponAmmo, 0, sizeof(peerDesc->Player->_weaponAmmo));
				std::memset(peerDesc->Player->_weaponAmmoCheckpoint, 0, sizeof(peerDesc->Player->_weaponAmmoCheckpoint));

				peerDesc->Player->_coinsCheckpoint = 0;
				std::memset(peerDesc->Player->_gemsCheckpoint, 0, sizeof(peerDesc->Player->_gemsCheckpoint));
				std::memset(peerDesc->Player->_weaponUpgrades, 0, sizeof(peerDesc->Player->_weaponUpgrades));
				std::memset(peerDesc->Player->_weaponUpgradesCheckpoint, 0, sizeof(peerDesc->Player->_weaponUpgradesCheckpoint));

				peerDesc->Player->_weaponAmmo[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
				peerDesc->Player->_weaponAmmoCheckpoint[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
				peerDesc->Player->_currentWeapon = WeaponType::Blaster;
				peerDesc->Player->_health = (serverConfig.InitialPlayerHealth > 0
					? serverConfig.InitialPlayerHealth
					: (PlayerShouldHaveUnlimitedHealth(serverConfig.GameMode) ? INT32_MAX : 5));

				if (peerDesc->RemotePeer) {
					// TODO: Send it also to peers without assigned player
					MemoryStream packet1(4);
					packet1.WriteVariableUint32(peerDesc->Player->_playerIndex);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerResetProperties, packet1);

					MemoryStream packet2(9);
					packet2.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Health);
					packet2.WriteVariableUint32(peerDesc->Player->_playerIndex);
					packet2.WriteVariableInt32(peerDesc->Player->_health);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet2);
				}
			}
		}

		// Revive forced spectators (previous winner, eliminated, or someone who joined mid-round) so they play the
		// new round. Players who chose to spectate (SpectateMode::Requested) keep spectating. This is collected first
		// because SetPlayerSpectateMode() mutates the active player list.
		SmallVector<Actors::Player*, 0> toRevive;
		for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
			if (peerDesc->Player != nullptr && (peerDesc->IsSpectating & SpectateMode::Mask) == SpectateMode::Forced) {
				toRevive.push_back(peerDesc->Player);
			}
		}
		for (auto* player : toRevive) {
			SetPlayerSpectateMode(player, SpectateMode::None);
		}

		// (Re)build the flags at round start. They are rebuilt here (rather than only at level load) so the spawn is
		// broadcast to all currently-synchronized players - at level-load time no client is connected yet, so the
		// load-time spawn would only reach late joiners via the per-peer sync.
		if (_networkManager->GetServerConfiguration().GameMode == MpGameMode::CaptureTheFlag) {
			BuildCtfBases();
		}
	}

	Vector2f MpLevelHandler::GetSpawnPoint(PlayerType playerType, std::uint8_t team)
	{
		if (!_multiplayerSpawnPoints.empty()) {
			// In team modes, prefer spawn points tagged for this team; fall back to any spawn point if the level
			// doesn't provide team-tagged spawns (or none match the requested team)
			auto& serverConfig = _networkManager->GetServerConfiguration();
			if (IsTeamGameMode(serverConfig.GameMode)) {
				SmallVector<std::uint32_t, 0> candidates;
				for (std::uint32_t i = 0; i < _multiplayerSpawnPoints.size(); i++) {
					if (_multiplayerSpawnPoints[i].Team == team) {
						candidates.push_back(i);
					}
				}
				if (!candidates.empty()) {
					return _multiplayerSpawnPoints[candidates[Random().Next(0, candidates.size())]].Pos;
				}
			}
			return _multiplayerSpawnPoints[Random().Next(0, _multiplayerSpawnPoints.size())].Pos;
		} else {
			Vector2f spawnPosition = EventMap()->GetSpawnPosition(playerType);
			if (spawnPosition.X >= 0.0f && spawnPosition.Y >= 0.0f) {
				return spawnPosition;
			}
			return EventMap()->GetSpawnPosition(PlayerType::Jazz);
		}
	}


	void MpLevelHandler::BuildRaceCheckpoints()
	{
		// (Re)builds the race checkpoint geometry from the current level. Called on the server at level load (if in a
		// race mode) and whenever the game mode is switched to a race mode at runtime. Safe to call repeatedly.
		_raceCheckpoints.clear();
		_orderedRaceCheckpoints.clear();
		_raceStartMarkers.clear();
		_raceCheckpointsOrdered = false;
		_raceBoundsMin = Vector2i(0, 0);
		_raceBoundsMax = Vector2i(0, 0);

		_eventMap->ForEachEvent([this](Events::EventMap::EventTile& e, std::int32_t x, std::int32_t y) {
			if (e.Event == EventType::WarpOrigin) {
				if (e.EventParams[2] != 0) {
					_raceCheckpoints.emplace_back(Vector2i(x, y));
					_raceStartMarkers.emplace_back(Vector2i(x, y));
				}
			} else if (e.Event == EventType::AreaEndOfLevel) {
				_raceCheckpoints.emplace_back(Vector2i(x, y));
			} else if (e.Event == EventType::RaceCheckpoint) {
				_orderedRaceCheckpoints.push_back({ Vector2i(x, y), (std::uint16_t)e.EventParams[0], e.EventParams[1] });
				_raceCheckpoints.emplace_back(Vector2i(x, y));
			}
			return true;
		});

		ConsolidateRaceCheckpoints();

		// The ordered checkpoints are always built: they drive both the minimap AND the race position ranking
		// (how far each player is along the track). Whether the minimap is actually shown/synced is controlled
		// separately by ServerConfiguration::AllowMinimap at the display/sync sites.
		if (!_orderedRaceCheckpoints.empty()) {
			// Merge waypoints that share the same (Order, Group) to their unweighted center (JJ2+ rule),
			// then order the polyline by (Group, Order) so the minimap can connect subsequent numbers.
			ConsolidateOrderedRaceCheckpoints();
		} else {
			// Original levels have no waypoints placed; trace the walkable track and place them automatically
			GenerateRaceCheckpointsFromGeometry();
		}
	}

	void MpLevelHandler::ConsolidateRaceCheckpoints()
	{
		static const Vector2i Directions[] = {
			{0, 1}, {1, 0}, {0, -1}, {-1, 0}
		};

		HashMap<Vector2i, std::int32_t, TileCoordHash> tiles;
		HashMap<Vector2i, std::int32_t, TileCoordHash> visited;
		SmallVector<Vector2i, 64> group;
		std::queue<Vector2i> q;

		tiles.reserve(_raceCheckpoints.size());
		for (const auto& tile : _raceCheckpoints) {
			tiles.emplace(tile, 1);
		}

		_raceCheckpoints.clear();

		for (const auto& [tile, priority] : tiles) {
			if (!visited.try_emplace(tile, 1).second) {
				continue;
			}

			q.push(tile);
			group.clear();

			while (!q.empty()) {
				Vector2i current = q.front(); q.pop();
				group.push_back(current);

				for (const auto& dir : Directions) {
					Vector2i neighbor = Vector2i(current.X + dir.X, current.Y + dir.Y);
					if (tiles.contains(neighbor) && !visited.contains(neighbor)) {
						q.push(neighbor);
						visited.emplace(neighbor, 1);
					}
				}
			}

			if (group.size() == 1) {
				_raceCheckpoints.push_back(group[0]);
			} else {
				// Calculate center
				std::int32_t sumX = 0, sumY = 0;
				for (const auto& coord : group) {
					sumX += coord.X;
					sumY += coord.Y;
				}
				_raceCheckpoints.emplace_back(sumX / group.size(), sumY / group.size());
			}
		}
	}

	void MpLevelHandler::ConsolidateOrderedRaceCheckpoints()
	{
		if (_orderedRaceCheckpoints.empty()) {
			return;
		}

		// Order the polyline by (Group, Order) so the minimap can connect subsequent TextID numbers within each group
		nCine::sort(_orderedRaceCheckpoints.begin(), _orderedRaceCheckpoints.end(), [](const RaceCheckpoint& a, const RaceCheckpoint& b) {
			return (a.Group != b.Group ? a.Group < b.Group : a.Order < b.Order);
		});

		// Merge waypoints that share the same (Order, Group) into their unweighted center (JJ2+ rule)
		SmallVector<RaceCheckpoint, 0> merged;
		std::int32_t count = (std::int32_t)_orderedRaceCheckpoints.size();
		for (std::int32_t i = 0; i < count;) {
			std::int32_t j = i;
			std::int64_t sumX = 0, sumY = 0;
			while (j < count && _orderedRaceCheckpoints[j].Order == _orderedRaceCheckpoints[i].Order
							 && _orderedRaceCheckpoints[j].Group == _orderedRaceCheckpoints[i].Group) {
				sumX += _orderedRaceCheckpoints[j].Tile.X;
				sumY += _orderedRaceCheckpoints[j].Tile.Y;
				j++;
			}
			std::int32_t n = j - i;
			merged.push_back({ Vector2i((std::int32_t)(sumX / n), (std::int32_t)(sumY / n)), _orderedRaceCheckpoints[i].Order, _orderedRaceCheckpoints[i].Group });
			i = j;
		}

		_orderedRaceCheckpoints = std::move(merged);
		_raceCheckpointsOrdered = true;

		// Minimap extent covers all waypoints and start markers
		Vector2i boundsMin(INT32_MAX, INT32_MAX), boundsMax(INT32_MIN, INT32_MIN);
		auto includeBounds = [&boundsMin, &boundsMax](Vector2i t) {
			if (t.X < boundsMin.X) { boundsMin.X = t.X; }
			if (t.Y < boundsMin.Y) { boundsMin.Y = t.Y; }
			if (t.X > boundsMax.X) { boundsMax.X = t.X; }
			if (t.Y > boundsMax.Y) { boundsMax.Y = t.Y; }
		};
		for (const auto& cp : _orderedRaceCheckpoints) {
			includeBounds(cp.Tile);
		}
		for (const auto& m : _raceStartMarkers) {
			includeBounds(m);
		}
		_raceBoundsMin = boundsMin;
		_raceBoundsMax = boundsMax;

		LOGI("Race minimap: {} authored waypoints, bounds [{}, {}]-[{}, {}]",
			(std::int32_t)_orderedRaceCheckpoints.size(), boundsMin.X, boundsMin.Y, boundsMax.X, boundsMax.Y);
	}

	void MpLevelHandler::GenerateRaceCheckpointsFromGeometry()
	{
		// Heuristic auto-placement for original race levels that don't carry JJ2+ waypoint Text events.
		// We trace the actual walkable route the player would take: from a spawn point, out to the farthest
		// reachable point (the far side of the loop), and back to the "Set Lap" warp via the opposite arm of
		// the track. Checkpoints are then sampled evenly along that route, so the minimap reflects the real
		// level geometry and starts at the spawn / ends at the Set Lap warp.
		Vector2i gridSize = _tileMap->GetSize();
		const std::int32_t W = gridSize.X, H = gridSize.Y;
		if (W <= 0 || H <= 0) {
			return;
		}

		Vector2f spawnPos;
		if (!_multiplayerSpawnPoints.empty()) {
			// Multiple spawn points are usually clustered around the start line; aggregate the ones near each
			// other (within ~20 tiles of the first) into a single start point
			const float clusterRange = 20.0f * Tiles::TileSet::DefaultTileSize;
			Vector2f base = _multiplayerSpawnPoints[0].Pos;
			Vector2f sum(0.0f, 0.0f);
			std::int32_t clustered = 0;
			for (const auto& sp : _multiplayerSpawnPoints) {
				if ((sp.Pos - base).Length() <= clusterRange) {
					sum += sp.Pos;
					clustered++;
				}
			}
			spawnPos = (clustered > 0 ? sum / (float)clustered : base);
			LOGI("Auto-placing race checkpoints: aggregated {} of {} spawn point(s)", clustered, (std::int32_t)_multiplayerSpawnPoints.size());
		} else {
			spawnPos = _eventMap->GetSpawnPosition(PlayerType::Jazz);
		}
		if (spawnPos.X < 0.0f || spawnPos.Y < 0.0f) {
			LOGW("Cannot auto-place race checkpoints: no valid spawn position");
			return;
		}

		// Movement model: the player can stand on solid ground or special surfaces (bridges/platforms), walk, fall,
		// jump a limited height/gap (without passing through solid tiles), ascend through float-up/vine/pole areas
		// and launch high off springs - but cannot fly. This keeps the traced route on the real player path.
		const std::int32_t totalTiles = W * H;

		// A tile is passable if it's empty, destructible (player breaks it) or a one-way platform (passable from
		// below). Trigger-controlled tiles are treated as PASSABLE (their open state): a trigger crate toggles such
		// tiles - typically solid-by-default tiles that become invisible once triggered to open the way forward - so
		// assuming they're open lets the route pass through the path the crate reveals.
		auto isFree = [this](std::int32_t tx, std::int32_t ty) -> bool {
			if (_tileMap->IsTileTrigger(tx, ty)) {
				return true;
			}
			return _tileMap->IsTileEmpty(tx, ty) || _tileMap->IsTileDestructible(tx, ty) || _tileMap->IsTileOneWay(tx, ty);
		};
		auto isOneWay = [this, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && _tileMap->IsTileOneWay(tx, ty));
		};

		// Scan events for movement aids: springs (launch), lifts (ascend through float-up/vine/pole/hook) and
		// surfaces (bridges/moving platforms the player stands on over a gap)
		std::unique_ptr<std::uint8_t[]> springMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> springBoost = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> liftMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> surfaceMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::uint8_t[]> tubeMap = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		// Warps that teleport the player to another section (so the route can continue from the destination). The
		// "Set Lap" warp (EventParams[2] != 0) is the lap finish and is handled separately, so it's excluded here.
		// Warp targets live in a separate EventMap list (not the event layout), so they're resolved via GetWarpTarget().
		SmallVector<Pair<Vector2i, std::uint32_t>, 0> warpOrigins;
		// Level-exit events (end-of-level area / sign): the finish when there's no "Set Lap" warp, mirroring how
		// Race mode itself falls back to level exits for lap completion
		SmallVector<Vector2i, 0> exitMarkers;
		_eventMap->ForEachEvent([this, &springMap, &springBoost, &liftMap, &surfaceMap, &tubeMap, &warpOrigins, &exitMarkers, &isFree, W, H](Events::EventMap::EventTile& e, std::int32_t x, std::int32_t y) {
			if (x < 0 || y < 0 || x >= W || y >= H) {
				return true;
			}
			switch (e.Event) {
				case EventType::Spring: {
					// Orientation: 0 = vertical up, 2 = vertical down (ceiling), 4/5 = horizontal. Mark 1 for
					// "launch up", 2 for "launch sideways"; down springs don't help the player ascend, so ignore them.
					std::uint8_t orientation = e.EventParams[1];
					if (orientation == 4 || orientation == 5) {
						springMap[x + y * W] = 2;
					} else if (orientation == 0) {
						springMap[x + y * W] = 1;
						// Per-type lift height: red 9, green 15, blue 19 tiles (+1 for the landing)
						std::uint8_t type = e.EventParams[0];
						springBoost[x + y * W] = (std::uint8_t)((type == 0 ? 9 : type == 2 ? 19 : 15) + 1);
					}
					break;
				}
				case EventType::AreaFloatUp:
				case EventType::ModifierVine:
				case EventType::SwingingVine:
				case EventType::ModifierHook:
					// Gentle lift: float-up area / vine / hook - the player climbs and does a normal jump off
					liftMap[x + y * W] = 1;
					break;
				case EventType::PinballBumper:
				case EventType::PinballPaddle:
					// Pinball bumpers/paddles fling the player upward; model them as a moderate vertical spring
					springMap[x + y * W] = 1;
					springBoost[x + y * W] = 12;
					break;
				case EventType::Crate:
				case EventType::Barrel: {
					// A crate/barrel may contain a spring (CRATE_SPRING etc.) - the player breaks it and rides the
					// spring, so treat the tile as that spring. Params[0..1] = contained event type; the contained
					// event's own params follow at [3..], so [3] = spring type, [4] = orientation (as in Spring).
					std::uint16_t contained = (std::uint16_t)(e.EventParams[0] | (e.EventParams[1] << 8));
					if (contained == (std::uint16_t)EventType::Spring) {
						std::uint8_t orientation = e.EventParams[4];
						if (orientation == 4 || orientation == 5) {
							springMap[x + y * W] = 2;
						} else if (orientation == 0) {
							springMap[x + y * W] = 1;
							std::uint8_t type = e.EventParams[3];
							springBoost[x + y * W] = (std::uint8_t)((type == 0 ? 9 : type == 2 ? 19 : 15) + 1);
						}
					}
					break;
				}
				case EventType::ModifierVPole:
				case EventType::Pole:
					// Spinning pole: flings the player upward with amplified momentum (chains higher and higher),
					// often arranged in an offset zigzag - mark as a strong launcher with a long jump-off reach
					liftMap[x + y * W] = 2;
					break;
				case EventType::ModifierTube:
					// Tubes transport the player through (often narrow/diagonal) passages regardless of geometry
					tubeMap[x + y * W] = 1;
					break;
				case EventType::WarpOrigin:
					if (e.EventParams[2] == 0) {
						warpOrigins.push_back(pair(Vector2i(x, y), (std::uint32_t)e.EventParams[0]));
					}
					break;
				case EventType::AreaEndOfLevel:
				case EventType::SignEOL:
					// Skip secret exits (encoded as ExitType::Special) - they lead to a secret level, not the finish
					if (e.EventParams[0] != (std::uint8_t)ExitType::Special) {
						exitMarkers.push_back(Vector2i(x, y));
					}
					break;
				case EventType::Bridge: {
					// A bridge is a walkable surface extending to the right of the event tile (one extra tile past
					// each end for the actor's ~half-tile overhang). Mark only its EMPTY tiles: the span may overlap
					// solid anchor tiles, which ground the player on their own - flagging those as "bridge surface"
					// would wrongly suppress the downward jump on solid ground (see the jump loop below).
					std::int32_t widthTiles = ((e.EventParams[0] | (e.EventParams[1] << 8)) * 16) / Tiles::TileSet::DefaultTileSize;
					for (std::int32_t i = -1; i <= widthTiles + 1; i++) {
						if (x + i >= 0 && x + i < W && _tileMap->IsTileEmpty(x + i, y)) {
							surfaceMap[(x + i) + y * W] = 1;
						}
					}
					break;
				}
				case EventType::MovingPlatform: {
					// A moving platform's anchor (the event tile) usually sits inside a wall; the platform sweeps a
					// circle of radius (length * 12px) around it. Mark the free tiles within that radius as a lift
					// zone (+ surface) so the tracer can board the platform anywhere along its path and ride up.
					std::int32_t radius = (e.EventParams[3] * 12 + Tiles::TileSet::DefaultTileSize - 1) / Tiles::TileSet::DefaultTileSize;
					if (radius < 1) {
						radius = 1;
					}
					for (std::int32_t dy = -radius; dy <= radius; dy++) {
						for (std::int32_t dx = -radius; dx <= radius; dx++) {
							if (dx * dx + dy * dy > radius * radius) {
								continue;
							}
							std::int32_t nx = x + dx, ny = y + dy;
							if (nx >= 0 && ny >= 0 && nx < W && ny < H && isFree(nx, ny)) {
								liftMap[nx + ny * W] = 1;
								surfaceMap[nx + ny * W] = 1;
							}
						}
					}
					break;
				}
				default:
					break;
			}
			return true;
		});
		auto springAt = [&springMap, W, H](std::int32_t tx, std::int32_t ty) -> std::uint8_t {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H ? springMap[tx + ty * W] : 0);
		};
		auto springBoostAt = [&springBoost, W, H](std::int32_t tx, std::int32_t ty) -> std::int32_t {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H ? springBoost[tx + ty * W] : 0);
		};
		auto isLift = [&liftMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && liftMap[tx + ty * W] != 0);
		};
		auto isPole = [&liftMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && liftMap[tx + ty * W] == 2);
		};
		auto onSurface = [&surfaceMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && surfaceMap[tx + ty * W] != 0);
		};
		auto isTube = [&tubeMap, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && tubeMap[tx + ty * W] != 0);
		};

		// A tile the player can occupy - a single free tile is enough (the player can crawl through 1-tile gaps);
		// tube tiles count too, even if their terrain is solid, since the tube transports the player through them.
		// Lift tiles (vine/pole/hook/float-up) are occupiable even when their tile graphic has a solid mask - the
		// player grabs and climbs them, so they must not read as a ceiling that blocks the upward boost.
		auto occupiable = [&isFree, &isTube, &isLift, W, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (tx >= 0 && ty >= 0 && tx < W && ty < H && (isFree(tx, ty) || isTube(tx, ty) || isLift(tx, ty)));
		};
		// The player stands here if there's a solid tile/level floor, a one-way platform or a bridge/platform below,
		// or a spring at its feet/below (springs are actors on otherwise-empty tiles, but the player rests on them)
		auto hasGround = [&isFree, &isOneWay, &onSurface, &springAt, H](std::int32_t tx, std::int32_t ty) -> bool {
			return (ty + 1 >= H) || !isFree(tx, ty + 1) || isOneWay(tx, ty + 1) || onSurface(tx, ty) || onSurface(tx, ty + 1)
				|| springAt(tx, ty) != 0 || springAt(tx, ty + 1) != 0;
		};
		// Snaps a tile to the nearest occupiable tile within a small radius (or {-1,-1} if none found)
		auto findSeed = [&occupiable](Vector2i t) -> Vector2i {
			if (occupiable(t.X, t.Y)) {
				return t;
			}
			for (std::int32_t r = 1; r <= 6; r++) {
				for (std::int32_t dy = -r; dy <= r; dy++) {
					for (std::int32_t dx = -r; dx <= r; dx++) {
						if (occupiable(t.X + dx, t.Y + dy)) {
							return Vector2i(t.X + dx, t.Y + dy);
						}
					}
				}
			}
			return Vector2i(-1, -1);
		};

		Vector2i spawnTile = findSeed(Vector2i((std::int32_t)(spawnPos.X / Tiles::TileSet::DefaultTileSize), (std::int32_t)(spawnPos.Y / Tiles::TileSet::DefaultTileSize)));
		if (spawnTile.X < 0) {
			LOGW("Cannot auto-place race checkpoints: spawn area is not walkable");
			return;
		}

		// Resolve each warp origin to a standable destination tile (matched by warp ID), so the BFS can teleport.
		// GetWarpTarget() returns the destination in world (pixel) coordinates, or (-1,-1) if the ID is unknown.
		constexpr std::int32_t WarpTS = (std::int32_t)Tiles::TileSet::DefaultTileSize;
		HashMap<Vector2i, Vector2i, TileCoordHash> warpJump;
		for (const auto& origin : warpOrigins) {
			Vector2f targetWorld = _eventMap->GetWarpTarget(origin.second());
			if (targetWorld.X >= 0.0f && targetWorld.Y >= 0.0f) {
				Vector2i dest = findSeed(Vector2i((std::int32_t)(targetWorld.X / WarpTS), (std::int32_t)(targetWorld.Y / WarpTS)));
				if (dest.X >= 0) {
					warpJump[origin.first()] = dest;
				}
			}
		}

		// Jump / boost envelope (in tiles)
		constexpr std::int32_t JumpHeight = 7;		// How high the player can jump straight up
		constexpr std::int32_t JumpReachX = 10;		// Horizontal reach of a (running) jump - clears fairly wide gaps
		constexpr std::int32_t JumpDropY = 6;		// How far below the player can land when jumping across a gap
		constexpr std::int32_t BoostHeight = 64;	// Max tiles a spring/float-up can carry the player up a clear column
		constexpr std::int32_t BoostReachX = 3;		// Sideways reach when stepping off a vertical boost
		constexpr std::int32_t HSpringReachX = 16;	// Horizontal reach of a running jump off a spring (clears wide pits)
		constexpr std::int32_t PoleReachY = 8;		// How far a spinning pole carries the player to the next pole (kept modest so it doesn't vault whole sections)

		// Finds a clear arc for a jump from (sx,sy) to (sx+dx,sy+dy) - rise in the start column, travel across at
		// the apex, then descend to the landing - without passing through solid tiles, trying higher apexes up to
		// maxUp to clear taller obstacles. Returns the apex row, or INT32_MAX if no clear arc exists.
		auto jumpApex = [&isFree, &isOneWay](std::int32_t sx, std::int32_t sy, std::int32_t dx, std::int32_t dy, std::int32_t maxUp) -> std::int32_t {
			std::int32_t ex = sx + dx, ey = sy + dy;
			std::int32_t x0 = (sx < ex ? sx : ex), x1 = (sx < ex ? ex : sx);
			std::int32_t apexStart = (sy < ey ? sy : ey);
			for (std::int32_t apexY = apexStart; apexY >= sy - maxUp; apexY--) {
				bool ok = true;
				for (std::int32_t yy = sy - 1; yy >= apexY; yy--) {
					if (!isFree(sx, yy)) { ok = false; break; }
				}
				if (!ok) {
					break; // ceiling above the start: a higher apex is impossible too
				}
				for (std::int32_t xx = x0; xx <= x1; xx++) {
					if (!isFree(xx, apexY)) { ok = false; break; }
				}
				if (!ok) {
					continue; // wall at this height; try a higher apex
				}
				// Descending toward the landing: a one-way platform is solid from above, so the arc can't drop
				// down through it (the rise phase above may still pass up through one-way platforms)
				for (std::int32_t yy = apexY + 1; yy <= ey; yy++) {
					if (!isFree(ex, yy) || isOneWay(ex, yy)) { ok = false; break; }
				}
				if (ok) {
					return apexY;
				}
			}
			return INT32_MAX;
		};
		auto jumpClear = [&jumpApex](std::int32_t sx, std::int32_t sy, std::int32_t dx, std::int32_t dy, std::int32_t maxUp) -> bool {
			return jumpApex(sx, sy, dx, dy, maxUp) != INT32_MAX;
		};

		// Gravity-aware breadth-first search recording predecessors, so a route can be reconstructed.
		// 'blocked' optionally forbids tiles (used to force the second pass around the other arm of the loop).
		// 'useWarps' toggles warp teleport edges: enabled for reachability, disabled when tracing a route so it
		// follows the geometry (e.g., climbs poles) rather than teleporting past it.
		auto runBfs = [&](Vector2i start, const std::uint8_t* blocked, std::int32_t* parent, std::int32_t* dist, Vector2i& farTile, std::int32_t& visitedCount, bool useWarps) {
			std::queue<Vector2i> q;
			std::int32_t startIdx = start.X + start.Y * W;
			dist[startIdx] = 0;
			farTile = start;
			std::int32_t farDist = 0;
			visitedCount = 0;
			q.push(start);

			auto tryAdd = [&](std::int32_t tx, std::int32_t ty, std::int32_t fromDist, std::int32_t fromIdx) {
				if (!occupiable(tx, ty)) {
					return;
				}
				std::int32_t ni = tx + ty * W;
				if (dist[ni] >= 0 || (blocked != nullptr && blocked[ni] != 0)) {
					return;
				}
				dist[ni] = fromDist + 1;
				parent[ni] = fromIdx;
				q.push(Vector2i(tx, ty));
			};

			// Adds a tile without the occupiable check (the caller verified it can be entered, e.g., a slope tile
			// reached diagonally through its empty corner)
			auto tryAddRaw = [&](std::int32_t tx, std::int32_t ty, std::int32_t fromDist, std::int32_t fromIdx) {
				if (tx < 0 || ty < 0 || tx >= W || ty >= H) {
					return;
				}
				std::int32_t ni = tx + ty * W;
				if (dist[ni] >= 0 || (blocked != nullptr && blocked[ni] != 0)) {
					return;
				}
				dist[ni] = fromDist + 1;
				parent[ni] = fromIdx;
				q.push(Vector2i(tx, ty));
			};

			while (!q.empty()) {
				Vector2i c = q.front(); q.pop();
				visitedCount++;
				std::int32_t ci = c.X + c.Y * W;
				std::int32_t cd = dist[ci];

				// A node sitting inside a partially-solid tile (a slope or a thin solid band, reached via the diagonal
				// step) rests on that tile's solid part - treat it as grounded so the tracer doesn't fall straight
				// through it (fixes the player dropping through a middle-solid tile after a tube ends). Destructible
				// tiles are excluded: the player breaks through them (e.g., buttstomps down), so they must not read as
				// standable ground - !isFree(c) is true only for genuine solid terrain (slopes/bands), not breakables.
				bool grounded = hasGround(c.X, c.Y) || (_tileMap->IsTilePartiallySolid(c.X, c.Y) && !isFree(c.X, c.Y));
				bool lift = isLift(c.X, c.Y);
				bool tube = isTube(c.X, c.Y);
				// A spinning pole flings the player in the direction of entry momentum (see Player::NextPoleStage):
				// up if they rose into it, down if they descended into it (or entered from the side, where gravity
				// dominates). Approximate the entry direction from how the BFS arrived, so a pole on a downward
				// section doesn't launch the route up and over it.
				bool poleHere = isPole(c.X, c.Y);
				bool poleUp = poleHere && (parent[ci] >= 0 && (parent[ci] / W) > c.Y);
				bool poleDown = poleHere && !poleUp;

				// Only consider real footing (not transient air tiles above a spring/jump) as the farthest point
				if (cd > farDist && (grounded || lift || tube)) {
					farDist = cd;
					farTile = c;
				}

				if (tube) {
					// Inside a tube the player is transported freely through the connected passage (any direction,
					// ignoring gravity and tight geometry)
					tryAdd(c.X - 1, c.Y, cd, ci);
					tryAdd(c.X + 1, c.Y, cd, ci);
					tryAdd(c.X, c.Y - 1, cd, ci);
					tryAdd(c.X, c.Y + 1, cd, ci);
				}

				// A warp teleports the player to its destination; continue tracing the route from there
				if (useWarps) {
					auto warp = warpJump.find(c);
					if (warp != warpJump.end()) {
						tryAddRaw(warp->second.X, warp->second.Y, cd, ci);
					}
				}

				std::uint8_t springHere = (springAt(c.X, c.Y) != 0 ? springAt(c.X, c.Y) : springAt(c.X, c.Y + 1));
				// Springs and vines/hooks/float-up always carry upward; a pole only when the player rose into it
				bool boostUp = springHere == 1 || (lift && !poleHere) || poleUp;

				if (boostUp) {
					// Vertical springs launch the player a fixed height (per spring type); float-up/vine/pole areas
					// carry the player up the whole clear column. Ride upward (bounded by that cap or a ceiling) and
					// step off onto any ledge within reach along the way.
					// Poles only carry a modest distance (they don't lift the player the whole shaft like a float-up
					// area or vine), so cap them low; springs use their per-type height, vines/float-up the full column
					std::int32_t cap = poleHere ? PoleReachY : (lift ? BoostHeight : springBoostAt(c.X, c.Y));
					if (cap == 0) {
						cap = springBoostAt(c.X, c.Y + 1);
					}
					if (cap <= 0 || cap > BoostHeight) {
						cap = BoostHeight;
					}
					for (std::int32_t k = 1; k <= cap; k++) {
						std::int32_t ty = c.Y - k;
						if (!occupiable(c.X, ty)) {
							break; // ceiling
						}
						tryAdd(c.X, ty, cd, ci);
						for (std::int32_t dir = -1; dir <= 1; dir += 2) {
							for (std::int32_t s = 1; s <= BoostReachX; s++) {
								std::int32_t sx = c.X + dir * s;
								if (!occupiable(sx, ty)) {
									break; // wall blocks stepping further sideways at this height
								}
								if (hasGround(sx, ty) || isLift(sx, ty)) {
									tryAdd(sx, ty, cd, ci);
								}
							}
						}
					}
				}

				if (poleDown && !isOneWay(c.X, c.Y + 1)) {
					// Flung downward: descend the pole's column (then keeps falling below it via the airborne logic);
					// never drop down through a one-way platform (solid from above)
					tryAdd(c.X, c.Y + 1, cd, ci);
				}

				if (lift) {
					// On a vine/pole the player can also move sideways
					tryAdd(c.X - 1, c.Y, cd, ci);
					tryAdd(c.X + 1, c.Y, cd, ci);
				}

				// A jump-off arc: from a spring (long arc, height by type), or off a vine/pole (a normal jump - the
				// player can climb then leap to a forward ledge). jumpClear picks a feasible apex under any ceiling,
				// so the player arcs forward instead of going straight up into it.
				if ((grounded && (springHere == 1 || springHere == 2)) || (lift && !poleDown)) {
					std::int32_t sMaxUp;
					std::int32_t maxDx;
					if (springHere == 1) {
						std::int32_t sc = springBoostAt(c.X, c.Y);
						if (sc == 0) {
							sc = springBoostAt(c.X, c.Y + 1);
						}
						sMaxUp = (sc > 0 ? sc : JumpHeight);
						maxDx = HSpringReachX;
					} else if (springHere == 2) {
						sMaxUp = JumpHeight;
						maxDx = HSpringReachX;
					} else if (poleHere) {
						// Spinning pole (entered ascending): carries the player a modest distance up to the next
						// pole/ledge - kept conservative so it doesn't vault over whole sections
						sMaxUp = PoleReachY;
						maxDx = JumpReachX;
					} else {
						// Vine/hook/float-up: a normal jump off it
						sMaxUp = JumpHeight;
						maxDx = JumpReachX;
					}
					for (std::int32_t dx = -maxDx; dx <= maxDx; dx++) {
						if (dx == 0) {
							continue; // straight up is already covered by the column boost
						}
						// The spring/climb provides the height and running provides the horizontal distance
						// independently, so a wide gap can still end on a higher ledge; jumpClear validates the arc.
						for (std::int32_t dy = -sMaxUp; dy <= JumpDropY; dy++) {
							std::int32_t tx = c.X + dx, ty = c.Y + dy;
							if ((hasGround(tx, ty) || isLift(tx, ty)) && occupiable(tx, ty) && jumpClear(c.X, c.Y, dx, dy, sMaxUp)) {
								tryAdd(tx, ty, cd, ci);
							}
						}
					}
				}

				if (!grounded && !lift && !tube) {
					// Airborne: fall, drifting horizontally as it descends - but never INTO a one-way platform (those
					// are solid from above; the player may only pass UP through them), and never upward
					if (!isOneWay(c.X, c.Y + 1)) {
						tryAdd(c.X, c.Y + 1, cd, ci);
					}
					if (!isOneWay(c.X - 1, c.Y + 1)) {
						tryAdd(c.X - 1, c.Y + 1, cd, ci);
					}
					if (!isOneWay(c.X + 1, c.Y + 1)) {
						tryAdd(c.X + 1, c.Y + 1, cd, ci);
					}
					if (isFree(c.X - 1, c.Y + 1) && !isOneWay(c.X - 2, c.Y + 1)) {
						tryAdd(c.X - 2, c.Y + 1, cd, ci);
					}
					if (isFree(c.X + 1, c.Y + 1) && !isOneWay(c.X + 2, c.Y + 1)) {
						tryAdd(c.X + 2, c.Y + 1, cd, ci);
					}
				} else if (grounded) {
					// On a bridge/platform surface the player is held up over empty space and must WALK across it -
					// they can't descend off it (down a slope at its end, or by jumping/falling through it). Once on
					// solid ground past the bridge, descending is allowed again.
					bool onBridgeSurface = onSurface(c.X, c.Y) || onSurface(c.X, c.Y + 1);

					// Walk to either side (stepping off a ledge then falls via gravity)
					tryAdd(c.X - 1, c.Y, cd, ci);
					tryAdd(c.X + 1, c.Y, cd, ci);

					// Step one tile diagonally to follow a slope or squeeze through a tight 1-tile diagonal corridor.
					// Only when the straight-sideways tile is blocked (otherwise walk/jump/fall already handle it -
					// and this stops the tracer from dropping diagonally off an open ledge or bridge). 45-degree
					// slope tiles read as "solid" on the grid, so a partially-solid tile is enterable - but ONLY when
					// it's a genuine triangular slope: the corner facing the player is clear AND the diagonally
					// opposite corner is solid. A thin diagonal line or a middle-solid band has both corners clear,
					// so this check refuses to step into (and through) it.
					for (std::int32_t dir = -1; dir <= 1; dir += 2) {
						if (occupiable(c.X + dir, c.Y)) {
							continue;
						}
						std::int32_t cornerX = (dir < 0 ? 1 : -1); // destination corner facing the player
						std::int32_t ux = c.X + dir, uy = c.Y - 1;
						bool upSlope = !occupiable(ux, uy) && _tileMap->IsTileCornerEmpty(ux, uy, cornerX, 1)
							&& !_tileMap->IsTileCornerEmpty(ux, uy, -cornerX, -1);
						if ((occupiable(ux, uy) && (hasGround(ux, uy) || isLift(ux, uy))) || upSlope) {
							tryAddRaw(ux, uy, cd, ci);
						}
						// Descend diagonally ONLY into a genuine slope tile. Dropping into an open tile past a solid
						// side would clip the solid corner - that's what made the tracer fall off the end of a bridge
						// past its solid anchor instead of stepping up onto it (the diagonal-up branch above handles
						// that). A normal walk + gravity covers stepping down onto a lower ledge with an open side.
						std::int32_t dxt = c.X + dir, dyt = c.Y + 1;
						bool downSlope = !occupiable(dxt, dyt) && _tileMap->IsTileCornerEmpty(dxt, dyt, cornerX, -1)
							&& !_tileMap->IsTileCornerEmpty(dxt, dyt, -cornerX, 1);
						if (downSlope && !isOneWay(dxt, dyt) && !onBridgeSurface) {
							tryAddRaw(dxt, dyt, cd, ci);
						}
					}

					// Jump onto reachable ledges, surfaces or movement aids within the envelope, only when the arc
					// is clear of solids. When standing on a bridge/platform surface (which holds the player up over
					// empty space), DON'T allow a downward jump: jumpClear sees the empty space below the surface as
					// "clear" and would otherwise let the tracer drop straight down THROUGH the bridge it's crossing.
					// The player must walk across; once on solid ground past it they can descend normally.
					std::int32_t maxDrop = (onBridgeSurface ? 0 : JumpDropY);
					for (std::int32_t dx = -JumpReachX; dx <= JumpReachX; dx++) {
						std::int32_t adx = (dx < 0 ? -dx : dx);
						std::int32_t up = JumpHeight - adx;
						if (up < 0) {
							up = 0;
						}
						for (std::int32_t dy = -up; dy <= maxDrop; dy++) {
							if (dx == 0 && dy == 0) {
								continue;
							}
							std::int32_t tx = c.X + dx, ty = c.Y + dy;
							if ((hasGround(tx, ty) || isLift(tx, ty)) && occupiable(tx, ty) && jumpClear(c.X, c.Y, dx, dy, JumpHeight)) {
								tryAdd(tx, ty, cd, ci);
							}
						}
					}
				}
			}
		};

		auto reconstruct = [&](const std::int32_t* parent, std::int32_t fromIdx, std::int32_t toIdx, SmallVector<Vector2i, 0>& out) {
			SmallVector<Vector2i, 0> rev;
			std::int32_t cur = toIdx;
			while (cur >= 0) {
				rev.push_back(Vector2i(cur % W, cur / W));
				if (cur == fromIdx) {
					break;
				}
				cur = parent[cur];
			}
			for (std::int32_t i = (std::int32_t)rev.size() - 1; i >= 0; i--) {
				out.push_back(rev[i]);
			}
		};

		// First pass: spawn -> everything; find the farthest reachable tile (far side of the loop)
		std::unique_ptr<std::int32_t[]> parent = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int32_t[]> dist = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 0; i < totalTiles; i++) { parent[i] = -1; dist[i] = -1; }

		Vector2i farTile;
		std::int32_t regionSize = 0;
		runBfs(spawnTile, nullptr, parent.get(), dist.get(), farTile, regionSize, true);

		if (regionSize < 32) {
			LOGW("Cannot auto-place race checkpoints: walkable region is too small ({} tiles)", regionSize);
			return;
		}
		if (regionSize > (totalTiles * 3) / 5) {
			LOGW("Cannot auto-place race checkpoints: level looks like an open arena, not a track");
			return;
		}

		const std::int32_t spawnIdx = spawnTile.X + spawnTile.Y * W;
		std::int32_t farIdx = farTile.X + farTile.Y * W;

		// The finish is the "Set Lap" warp, or - if the level has none - a level-exit event (end-of-level area or
		// EOL sign), mirroring how Race mode itself falls back to level exits for lap completion. A full lap goes
		// spawn -> (out to the far side) -> finish; routing via the far tile forces the trace around the whole loop
		// even when the start line sits right next to the finish (so there's no short path to block). Among the
		// candidates, take the one reachable from spawn and farthest along the track, so the route spans the level.
		auto pickFarthestReachable = [&](const SmallVector<Vector2i, 0>& markers) -> Vector2i {
			Vector2i best(-1, -1);
			std::int32_t bestDist = -1;
			for (const auto& m : markers) {
				Vector2i t = findSeed(m);
				if (t.X >= 0) {
					std::int32_t d = dist[t.X + t.Y * W];
					if (d > bestDist) { bestDist = d; best = t; }
				}
			}
			return best;
		};
		Vector2i finishTile = pickFarthestReachable(_raceStartMarkers);
		const char* finishSource = "warp";
		if (finishTile.X < 0) {
			finishTile = pickFarthestReachable(exitMarkers);
			finishSource = (finishTile.X >= 0 ? "exit" : "far");
		}
		Vector2i target = farTile;
		if (finishTile.X >= 0 && dist[finishTile.X + finishTile.Y * W] >= 0) {
			target = finishTile;
			// If the finish itself lies far from the spawn (a point-to-point race, not a loop back to the start),
			// make it the turnaround too, so the route runs straight to it instead of overshooting to the farthest
			// tile and doubling back past the finish.
			std::int32_t finDist = dist[finishTile.X + finishTile.Y * W];
			if (dist[farIdx] > 0 && finDist * 2 >= dist[farIdx]) {
				farTile = finishTile;
				farIdx = finishTile.X + finishTile.Y * W;
			}
		}
		const std::int32_t targetIdx = target.X + target.Y * W;

		std::int32_t springUp = 0, springSide = 0, liftCount = 0, surfaceCount = 0, poleCount = 0;
		std::int32_t springUpReached = 0, liftReached = 0, poleReached = 0;
		for (std::int32_t i = 0; i < totalTiles; i++) {
			if (springMap[i] == 1) { springUp++; if (dist[i] >= 0) { springUpReached++; } }
			else if (springMap[i] == 2) { springSide++; }
			if (liftMap[i] != 0) { liftCount++; if (dist[i] >= 0) { liftReached++; } }
			if (liftMap[i] == 2) { poleCount++; if (dist[i] >= 0) { poleReached++; } }
			if (surfaceMap[i] != 0) { surfaceCount++; }
		}
		LOGI("Race geometry: {} vertical springs ({} reached) + {} horizontal, {} lift ({} reached, of which {} poles {} reached), {} surface, {} warp(s), {} exit(s); finish [{}, {}] via {} reachable={} (dist {})",
			springUp, springUpReached, springSide, liftCount, liftReached, poleCount, poleReached, surfaceCount, (std::int32_t)warpJump.size(), (std::int32_t)exitMarkers.size(),
			finishTile.X, finishTile.Y, finishSource,
			(finishTile.X >= 0 && dist[finishTile.X + finishTile.Y * W] >= 0) ? 1 : 0,
			(finishTile.X >= 0 ? dist[finishTile.X + finishTile.Y * W] : -1));
		// Each resolved warp and whether the BFS actually reached its origin (so it could teleport) - helps diagnose
		// warps the tracer stops at instead of following
		for (const auto& wj : warpJump) {
			std::int32_t oi = wj.first.X + wj.first.Y * W;
			LOGI("Race warp: origin [{}, {}] (reached={}) -> dest [{}, {}] (reached={})",
				wj.first.X, wj.first.Y, (dist[oi] >= 0) ? 1 : 0,
				wj.second.X, wj.second.Y, (dist[wj.second.X + wj.second.Y * W] >= 0) ? 1 : 0);
		}

		// First arm: spawn -> far. Prefer a warp-free route so the line follows the geometry (e.g., climbs the poles
		// or vines) instead of teleporting past it via a warp shortcut; fall back to the warp-enabled route only
		// when the far tile can't be reached without warps (e.g., a section only accessible by a warp).
		std::unique_ptr<std::int32_t[]> parentNW = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int32_t[]> distNW = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 0; i < totalTiles; i++) { parentNW[i] = -1; distNW[i] = -1; }
		Vector2i farNW;
		std::int32_t regionNW = 0;
		runBfs(spawnTile, nullptr, parentNW.get(), distNW.get(), farNW, regionNW, false);
		bool routeNoWarp = (distNW[farIdx] >= 0);

		SmallVector<Vector2i, 0> pathOut;
		reconstruct(routeNoWarp ? parentNW.get() : parent.get(), spawnIdx, farIdx, pathOut);

		// Second arm: far -> finish, taking the opposite side by blocking the first arm. If blocking disconnects
		// the finish (e.g., wide corridors), retry without blocking so the route still reaches the end.
		std::unique_ptr<std::uint8_t[]> blocked = std::make_unique<std::uint8_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 1; i + 1 < (std::int32_t)pathOut.size(); i++) {
			blocked[pathOut[i].X + pathOut[i].Y * W] = 1;
		}

		std::unique_ptr<std::int32_t[]> parent2 = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		std::unique_ptr<std::int32_t[]> dist2 = std::make_unique<std::int32_t[]>((std::size_t)totalTiles);
		for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }

		Vector2i far2;
		std::int32_t visited2 = 0;
		runBfs(farTile, blocked.get(), parent2.get(), dist2.get(), far2, visited2, !routeNoWarp);

		if (dist2[targetIdx] < 0) {
			// Blocking the first arm cut off the finish; retry unblocked so we still reach it
			for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }
			runBfs(farTile, nullptr, parent2.get(), dist2.get(), far2, visited2, !routeNoWarp);
		}
		if (dist2[targetIdx] < 0 && routeNoWarp) {
			// Still unreachable without warps - allow warps for the return arm so the route completes
			for (std::int32_t i = 0; i < totalTiles; i++) { parent2[i] = -1; dist2[i] = -1; }
			runBfs(farTile, nullptr, parent2.get(), dist2.get(), far2, visited2, true);
		}

		SmallVector<Vector2i, 0> pathBack;
		if (dist2[targetIdx] >= 0) {
			reconstruct(parent2.get(), farIdx, targetIdx, pathBack);
		}

		// Assemble the route spawn -> far -> finish. But if the far tile overshoots just past the finish (e.g., a
		// catch-spring sits a couple of tiles beyond the finish warp), end the route at the finish instead of
		// looping out to far and back.
		SmallVector<Vector2i, 0> route;
		std::int32_t trimAt = -1;
		if (target.X == finishTile.X && target.Y == finishTile.Y) {
			for (std::int32_t i = (std::int32_t)pathOut.size() / 2; i < (std::int32_t)pathOut.size(); i++) {
				std::int32_t ddx = pathOut[i].X - finishTile.X, ddy = pathOut[i].Y - finishTile.Y;
				if (std::max(ddx < 0 ? -ddx : ddx, ddy < 0 ? -ddy : ddy) <= 2) {
					trimAt = i;
					break;
				}
			}
		}
		if (trimAt >= 0) {
			for (std::int32_t i = 0; i <= trimAt; i++) {
				route.push_back(pathOut[i]);
			}
			Vector2i last = route[route.size() - 1];
			if (last.X != finishTile.X || last.Y != finishTile.Y) {
				route.push_back(finishTile);
			}
		} else {
			for (std::int32_t i = 0; i < (std::int32_t)pathOut.size(); i++) {
				route.push_back(pathOut[i]);
			}
			for (std::int32_t i = 1; i < (std::int32_t)pathBack.size(); i++) {
				route.push_back(pathBack[i]);
			}
		}

		if (route.size() < 2) {
			LOGW("Cannot auto-place race checkpoints: could not trace a route through the level");
			return;
		}

		// Expand jumps (non-adjacent steps) into up-over-down arcs so the minimap draws the player going up and
		// over an obstacle instead of a straight line cutting through it. A parallel group id is bumped at each
		// warp edge (origin -> teleport target) so the minimap doesn't draw a line straight across the level.
		SmallVector<Vector2i, 0> routeArc;
		SmallVector<std::uint8_t, 0> routeArcGroup;
		std::uint8_t curGroup = 0;
		routeArc.push_back(route[0]);
		routeArcGroup.push_back(curGroup);
		for (std::int32_t i = 1; i < (std::int32_t)route.size(); i++) {
			Vector2i a = route[i - 1], b = route[i];
			auto w = warpJump.find(a);
			if (w != warpJump.end() && w->second.X == b.X && w->second.Y == b.Y) {
				if (curGroup < 255) {
					curGroup++; // teleport: break the line here
				}
			} else {
				std::int32_t ddx = b.X - a.X, ddy = b.Y - a.Y;
				std::int32_t adx = (ddx < 0 ? -ddx : ddx), ady = (ddy < 0 ? -ddy : ddy);
				if (adx > 1 || ady > 1) {
					std::int32_t apexY = jumpApex(a.X, a.Y, ddx, ddy, BoostHeight);
					std::int32_t topY = (a.Y < b.Y ? a.Y : b.Y);
					if (apexY != INT32_MAX && apexY < topY) {
						routeArc.push_back(Vector2i(a.X, apexY));
						routeArcGroup.push_back(curGroup);
						routeArc.push_back(Vector2i(b.X, apexY));
						routeArcGroup.push_back(curGroup);
					}
				}
			}
			routeArc.push_back(b);
			routeArcGroup.push_back(curGroup);
		}

		// Minimap extent = the route's bounding box (padded for track width), plus start markers
		Vector2i boundsMin(W, H), boundsMax(-1, -1);
		auto includeBounds = [&boundsMin, &boundsMax](Vector2i t) {
			if (t.X < boundsMin.X) { boundsMin.X = t.X; }
			if (t.Y < boundsMin.Y) { boundsMin.Y = t.Y; }
			if (t.X > boundsMax.X) { boundsMax.X = t.X; }
			if (t.Y > boundsMax.Y) { boundsMax.Y = t.Y; }
		};
		for (std::int32_t i = 0; i < (std::int32_t)routeArc.size(); i++) {
			includeBounds(routeArc[i]);
		}
		for (const auto& m : _raceStartMarkers) {
			includeBounds(m);
		}
		boundsMin.X = std::max(0, boundsMin.X - 2);
		boundsMin.Y = std::max(0, boundsMin.Y - 2);
		boundsMax.X = std::min(W - 1, boundsMax.X + 2);
		boundsMax.Y = std::min(H - 1, boundsMax.Y + 2);
		_raceBoundsMin = boundsMin;
		_raceBoundsMax = boundsMax;

		// Checkpoints = the route's corners (direction changes), so straight runs stay sparse while bends and jump
		// arcs get the detail they need; decimated uniformly if there are too many. The group id is carried so the
		// minimap breaks the polyline at teleports, and a group change at a corner is always kept as a corner.
		SmallVector<Vector2i, 0> corners;
		SmallVector<std::uint8_t, 0> cornerGroup;
		for (std::int32_t i = 0; i < (std::int32_t)routeArc.size(); i++) {
			if (i == 0 || i == (std::int32_t)routeArc.size() - 1) {
				corners.push_back(routeArc[i]);
				cornerGroup.push_back(routeArcGroup[i]);
				continue;
			}
			Vector2i p = routeArc[i - 1], c2 = routeArc[i], n = routeArc[i + 1];
			std::int32_t d1x = (c2.X > p.X) - (c2.X < p.X), d1y = (c2.Y > p.Y) - (c2.Y < p.Y);
			std::int32_t d2x = (n.X > c2.X) - (n.X < c2.X), d2y = (n.Y > c2.Y) - (n.Y < c2.Y);
			if (d1x != d2x || d1y != d2y || routeArcGroup[i] != routeArcGroup[i - 1] || routeArcGroup[i] != routeArcGroup[i + 1]) {
				corners.push_back(routeArc[i]);
				cornerGroup.push_back(routeArcGroup[i]);
			}
		}

		LOGI("Auto-placing race checkpoints: spawn [{}, {}], far [{}, {}], target [{}, {}], region {} tiles, warpFreeRoute={}, route {}, arc {}, corners {}, bounds [{}, {}]-[{}, {}]",
			spawnTile.X, spawnTile.Y, farTile.X, farTile.Y, target.X, target.Y, regionSize, routeNoWarp ? 1 : 0,
			(std::int32_t)route.size(), (std::int32_t)routeArc.size(), (std::int32_t)corners.size(),
			boundsMin.X, boundsMin.Y, boundsMax.X, boundsMax.Y);

		const std::int32_t cornerCount = (std::int32_t)corners.size();
		constexpr std::int32_t MaxCheckpoints = 100;
		_orderedRaceCheckpoints.clear();
		if (cornerCount <= MaxCheckpoints) {
			for (std::int32_t i = 0; i < cornerCount; i++) {
				_orderedRaceCheckpoints.push_back({ corners[i], (std::uint16_t)i, cornerGroup[i] });
			}
		} else {
			for (std::int32_t k = 0; k < MaxCheckpoints; k++) {
				std::int32_t idx = (std::int32_t)((std::int64_t)k * (cornerCount - 1) / (MaxCheckpoints - 1));
				_orderedRaceCheckpoints.push_back({ corners[idx], (std::uint16_t)k, cornerGroup[idx] });
			}
		}

		if (_orderedRaceCheckpoints.size() < 2) {
			_orderedRaceCheckpoints.clear();
			LOGW("Cannot auto-place race checkpoints: could not derive a track from level geometry");
			return;
		}

		// The route is directional (spawn -> finish), so it can be trusted for progress-based ranking
		_raceCheckpointsOrdered = true;
		LOGI("Auto-placed {} race checkpoints (finish tile [{}, {}])",
			(std::int32_t)_orderedRaceCheckpoints.size(), finishTile.X, finishTile.Y);
	}

	void MpLevelHandler::WarpAllPlayersToStart()
	{
		// TODO: Reset ambient lighting
		for (auto* player : _players) {
			Vector2f spawnPosition = GetSpawnPoint(player->_playerTypeOriginal, static_cast<MpPlayer*>(player)->GetPeerDescriptor()->Team);
			player->SetModifier(Actors::Player::Modifier::None);
			player->SetShield(ShieldType::None, 0.0f);
			player->SetDizzy(0.0f);
			player->WarpToPosition(spawnPosition, WarpFlags::Default);

			if (auto mpPlayer = runtime_cast<PlayerOnServer>(player)) {
				mpPlayer->_canTakeDamage = true;
			}
		}
	}

	void MpLevelHandler::RollbackLevelState()
	{
		_eventMap->RollbackToCheckpoint();
		_tileMap->RollbackToCheckpoint();

		// Synchronize tilemap
		{
			// TODO: Use deflate compression here?
			MemoryStream packet(40 * 1024);
			_tileMap->SerializeResumableToStream(packet);
			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncTileMap, packet);
		}

		for (auto& actor : _actors) {
			// Despawn all actors that were created after the last checkpoint
			if (actor->_spawnFrames > _checkpointFrames && !actor->GetState(Actors::ActorState::PreserveOnRollback)) {
				if ((actor->_state & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::None) {
					Vector2i originTile = actor->_originTile;
					if ((actor->_state & Actors::ActorState::IsFromGenerator) == Actors::ActorState::IsFromGenerator) {
						_eventMap->ResetGenerator(originTile.X, originTile.Y);
					}

					_eventMap->Deactivate(originTile.X, originTile.Y);
				}

				actor->_state |= Actors::ActorState::IsDestroyed;
			}
		}
	}

	void MpLevelHandler::CalculatePositionInRound(bool forceSend)
	{
		SmallVector<Pair<MpPlayer*, std::uint32_t>, 128> sortedPlayers;
		SmallVector<Pair<MpPlayer*, std::uint32_t>, 128> sortedDeadPlayers;
		auto& serverConfig = _networkManager->GetServerConfiguration();

		if (serverConfig.GameMode == MpGameMode::Unknown || serverConfig.GameMode == MpGameMode::Cooperation) {
			return;
		}

		for (auto* player : _players) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->IsSpectating != SpectateMode::None) {
				continue;
			}

			std::uint32_t roundPoints = 0;
			switch (serverConfig.GameMode) {
				case MpGameMode::Battle: {
					roundPoints = peerDesc->Kills;
					break;
				}
				case MpGameMode::Race: {
					// 1 hour penalty for every unfinished lap
					//roundPoints = (std::uint32_t)(peerDesc->LapsElapsedFrames + (serverConfig.TotalLaps - peerDesc->Laps) * 3600.0f * FrameTimer::FramesPerSecond);

					Vector2f pos = mpPlayer->_pos;
					const std::uint32_t lapPenalty = (serverConfig.TotalLaps - peerDesc->Laps) * (UINT32_MAX / 100);

					if (_raceCheckpointsOrdered && _orderedRaceCheckpoints.size() >= 2) {
						// Rank by how far along the track the player is: project the player's position onto the ordered
						// checkpoint polyline and measure the arc length travelled. Players closer to the finish (more
						// distance travelled, then more laps) rank higher.
						constexpr float TS = (float)Tiles::TileSet::DefaultTileSize;
						std::int32_t n = (std::int32_t)_orderedRaceCheckpoints.size();
						float bestDistSq = FLT_MAX;
						float bestProgress = 0.0f;
						float cumulative = 0.0f;
						for (std::int32_t k = 0; k + 1 < n; k++) {
							// A group change marks a teleport: advance progress a little but don't project onto the
							// (level-spanning) warp segment, so the gap doesn't distort the ranking
							if (_orderedRaceCheckpoints[k].Group != _orderedRaceCheckpoints[k + 1].Group) {
								cumulative += 1.0f;
								continue;
							}
							Vector2f a(_orderedRaceCheckpoints[k].Tile.X * TS, _orderedRaceCheckpoints[k].Tile.Y * TS);
							Vector2f b(_orderedRaceCheckpoints[k + 1].Tile.X * TS, _orderedRaceCheckpoints[k + 1].Tile.Y * TS);
							float abx = b.X - a.X, aby = b.Y - a.Y;
							float segLenSq = abx * abx + aby * aby;
							float segLen = std::sqrt(segLenSq);
							float t = 0.0f;
							if (segLenSq > 0.0001f) {
								t = ((pos.X - a.X) * abx + (pos.Y - a.Y) * aby) / segLenSq;
								t = (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
							}
							float dx = pos.X - (a.X + abx * t), dy = pos.Y - (a.Y + aby * t);
							float distSq = dx * dx + dy * dy;
							if (distSq < bestDistSq) {
								bestDistSq = distSq;
								bestProgress = cumulative + t * segLen;
							}
							cumulative += segLen;
						}
						float remaining = cumulative - bestProgress;
						roundPoints = (std::uint32_t)(remaining < 0.0f ? 0.0f : remaining) + lapPenalty;
					} else {
						float nearestCheckpoint = FLT_MAX;
						for (auto checkpointPos : _raceCheckpoints) {
							float length = (pos - Vector2f(checkpointPos.X * Tiles::TileSet::DefaultTileSize, checkpointPos.Y * Tiles::TileSet::DefaultTileSize)).Length();
							if (nearestCheckpoint > length) {
								nearestCheckpoint = length;
							}
						}

						roundPoints = nearestCheckpoint + lapPenalty;
					}
					break;
				}
				case MpGameMode::TreasureHunt: {
					roundPoints = peerDesc->TreasureCollected;
					break;
				}
			}

			bool isDead = (serverConfig.Elimination && peerDesc->Deaths >= serverConfig.TotalKills);
			if (isDead) {
				sortedDeadPlayers.push_back(pair(mpPlayer, roundPoints));
			} else {
				sortedPlayers.push_back(pair(mpPlayer, roundPoints));
			}
		}

		bool lessWins = (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::TeamRace);
		if (lessWins) {
			const auto comparator = [](const auto& a, const auto& b) -> bool {
				return a.second() < b.second();
			};
			nCine::sort(sortedPlayers.begin(), sortedPlayers.end(), comparator);
			nCine::sort(sortedDeadPlayers.begin(), sortedDeadPlayers.end(), comparator);
		} else {
			const auto comparator = [](const auto& a, const auto& b) -> bool {
				return a.second() > b.second();
			};
			nCine::sort(sortedPlayers.begin(), sortedPlayers.end(), comparator);
			nCine::sort(sortedDeadPlayers.begin(), sortedDeadPlayers.end(), comparator);
		}

		bool positionsChanged = false;

		std::uint32_t currentPos = 1;
		std::uint32_t prevPos = 0;
		for (std::int32_t i = 0; i < sortedPlayers.size(); i++) {
			std::uint32_t pos, points = sortedPlayers[i].second();
			if (sortedPlayers[i].second() == 0) {
				pos = 0;	// Don't assign valid position if player has no points
			} else if (i > 0 && points == sortedPlayers[i - 1].second()) {
				pos = prevPos;
			} else {
				pos = currentPos;
				prevPos = currentPos;
			}
			
			currentPos++;

			auto peerDesc = sortedPlayers[i].first()->GetPeerDescriptor();
			if (peerDesc->PointsInRound != points || peerDesc->PositionInRound != pos) {
				peerDesc->PointsInRound = points;
				peerDesc->PositionInRound = pos;
				positionsChanged = true;

				/*if (peerDesc->RemotePeer) {
					MemoryStream packet(9);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::PositionInRound);
					packet.WriteVariableUint32(sortedPlayers[i].first()->_playerIndex);
					packet.WriteVariableUint32(peerDesc->PositionInRound);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
				}*/
			}
		}

		for (std::int32_t i = 0; i < sortedDeadPlayers.size(); i++) {
			std::uint32_t pos, points = sortedDeadPlayers[i].second();
			if (points == 0) {
				pos = 0;	// Don't assign valid position if player has no points
			} else if (i > 0 && points == sortedDeadPlayers[i - 1].second()) {
				pos = prevPos;
			} else {
				pos = currentPos;
				prevPos = currentPos;
			}

			currentPos++;

			auto peerDesc = sortedDeadPlayers[i].first()->GetPeerDescriptor();
			if (peerDesc->PointsInRound != points || peerDesc->PositionInRound != pos) {
				peerDesc->PointsInRound = points;
				peerDesc->PositionInRound = pos;
				positionsChanged = true;

				/*if (peerDesc->RemotePeer) {
					MemoryStream packet(9);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::PositionInRound);
					packet.WriteVariableUint32(sortedDeadPlayers[i].first()->_playerIndex);
					packet.WriteVariableUint32(peerDesc->PositionInRound);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
				}*/
			}
		}

		if (positionsChanged || forceSend) {
			MemoryStream packet(4 + (sortedPlayers.size() + sortedDeadPlayers.size()) * 12);
			packet.WriteVariableUint32(sortedPlayers.size() + sortedDeadPlayers.size());
			for (std::int32_t i = 0; i < sortedPlayers.size(); i++) {
				auto peerDesc = sortedPlayers[i].first()->GetPeerDescriptor();
				packet.WriteVariableUint32(sortedPlayers[i].first()->_playerIndex);
				packet.WriteVariableUint32(peerDesc->PositionInRound);
				packet.WriteVariableUint32(peerDesc->PointsInRound);
			}
			for (std::int32_t i = 0; i < sortedDeadPlayers.size(); i++) {
				auto peerDesc = sortedDeadPlayers[i].first()->GetPeerDescriptor();
				packet.WriteVariableUint32(sortedDeadPlayers[i].first()->_playerIndex);
				packet.WriteVariableUint32(peerDesc->PositionInRound);
				packet.WriteVariableUint32(peerDesc->PointsInRound);
			}
			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState != PeerLevelState::Unknown);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::UpdatePositionsInRound, packet);
		}
	}

	void MpLevelHandler::CheckGameEnds()
	{
		// Win-condition evaluation is server-authoritative (it ends the round and broadcasts the result); clients only
		// receive the outcome, so this must never run on a client even though they now host a game-mode object for the HUD.
		if DEATH_UNLIKELY(!_isServer) {
			return;
		}

		if DEATH_UNLIKELY(_levelState != LevelState::Running) {
			return;
		}

		CalculatePositionInRound();

		auto& serverConfig = _networkManager->GetServerConfiguration();

		// Keep clients' team scores up to date for the HUD
		SyncTeamScores();

		// Modes that own their round logic decide their win condition; the rest fall back to the legacy checks below
		if (_gameMode != nullptr && _gameMode->OwnsRoundLogic()) {
			GameEndResult result = _gameMode->CheckGameEnds(*this);
			if (result.ShouldEnd) {
				if (result.WithTeam) {
					EndGameWithTeam(result.WinnerTeam);
				} else {
					EndGame(static_cast<MpPlayer*>(result.Winner));
				}
			}
			return;
		}

		if (serverConfig.GameMode != MpGameMode::Cooperation && serverConfig.Elimination) {
			if (IsTeamGameMode(serverConfig.GameMode)) {
				// Team elimination: a team is out when all its non-spectating members are out; last team wins
				std::uint8_t teamCount = GetTeamCount();
				bool teamAlive[MaxTeamCount] = {};
				for (auto* player : _players) {
					auto* mpPlayer = static_cast<MpPlayer*>(player);
					auto peerDesc = mpPlayer->GetPeerDescriptor();
					if (peerDesc->IsSpectating != SpectateMode::None || peerDesc->Team >= teamCount) {
						continue;
					}
					if (peerDesc->Deaths < serverConfig.TotalKills) {
						teamAlive[peerDesc->Team] = true;
					}
				}

				std::int32_t aliveTeams = 0;
				std::uint8_t lastAliveTeam = 0;
				for (std::uint8_t team = 0; team < teamCount; team++) {
					if (teamAlive[team]) {
						aliveTeams++;
						lastAliveTeam = team;
					}
				}

				if (aliveTeams <= 1) {
					EndGameWithTeam(lastAliveTeam);
					return;
				}
			} else {
				std::int32_t eliminationCount = 0;
				MpPlayer* eliminationWinner = nullptr;

				for (auto* player : _players) {
					auto* mpPlayer = static_cast<MpPlayer*>(player);
					auto peerDesc = mpPlayer->GetPeerDescriptor();

					if (peerDesc->IsSpectating != SpectateMode::None) {
						continue;
					}

					if (peerDesc->Deaths < serverConfig.TotalKills) {
						eliminationCount++;
					} else {
						eliminationWinner = mpPlayer;
					}
				}

				if (eliminationCount >= _players.size() - 1) {
					EndGame(eliminationWinner);
					return;
				}
			}
		}

		switch (serverConfig.GameMode) {
			case MpGameMode::Battle: {
				if (!serverConfig.Elimination) {
					for (auto* player : _players) {
						auto* mpPlayer = static_cast<MpPlayer*>(player);
						auto peerDesc = mpPlayer->GetPeerDescriptor();

						if (peerDesc->IsSpectating != SpectateMode::None) {
							continue;
						}

						if (peerDesc->Kills >= serverConfig.TotalKills) {
							EndGame(mpPlayer);
							break;
						}
					}
				}
				break;
			}

			case MpGameMode::TeamBattle: {
				if (!serverConfig.Elimination) {
					std::uint8_t teamCount = GetTeamCount();
					for (std::uint8_t team = 0; team < teamCount; team++) {
						if (GetTeamScore(team) >= serverConfig.TotalKills) {
							EndGameWithTeam(team);
							break;
						}
					}
				}
				break;
			}

			case MpGameMode::CaptureTheFlag: {
				// First team to the required number of captures wins (capture target reuses TotalKills)
				std::uint8_t teamCount = GetTeamCount();
				for (std::uint8_t team = 0; team < teamCount; team++) {
					if (GetTeamScore(team) >= serverConfig.TotalKills) {
						EndGameWithTeam(team);
						break;
					}
				}
				break;
			}

			case MpGameMode::TeamRace: {
				// A team wins when all of its non-spectating members have completed the required laps
				std::uint8_t teamCount = GetTeamCount();
				for (std::uint8_t team = 0; team < teamCount; team++) {
					bool anyMember = false, allFinished = true;
					for (auto* player : _players) {
						auto* mpPlayer = static_cast<MpPlayer*>(player);
						auto peerDesc = mpPlayer->GetPeerDescriptor();
						if (peerDesc->IsSpectating != SpectateMode::None || peerDesc->Team != team) {
							continue;
						}
						anyMember = true;
						if (peerDesc->Laps < serverConfig.TotalLaps) {
							allFinished = false;
							break;
						}
					}
					if (anyMember && allFinished) {
						EndGameWithTeam(team);
						break;
					}
				}
				break;
			}

			case MpGameMode::Race: {
				for (auto* player : _players) {
					auto* mpPlayer = static_cast<MpPlayer*>(player);
					auto peerDesc = mpPlayer->GetPeerDescriptor();

					if (peerDesc->IsSpectating != SpectateMode::None) {
						continue;
					}

					if (peerDesc->Laps >= serverConfig.TotalLaps) {
						if (serverConfig.OvertimeSecs > 0) {
							BeginOvertime(mpPlayer);
						} else {
							EndGame(mpPlayer);
							break;
						}
					}
				}

				// Check if all players finished or overtime expired
				if (_overtimeStarted) {
					if (_overtimeFinishers >= GetNonSpectatePlayerCount() || _overtimeTimeLeft <= 0.0f) {
						// Find winner (player with best position)
						MpPlayer* winner = nullptr;
						std::uint32_t bestPosition = UINT32_MAX;
						for (auto* player : _players) {
							auto* mpPlayer = static_cast<MpPlayer*>(player);
							auto peerDesc = mpPlayer->GetPeerDescriptor();
							if (peerDesc->PositionInRound > 0 && peerDesc->PositionInRound < bestPosition) {
								bestPosition = peerDesc->PositionInRound;
								winner = mpPlayer;
							}
						}
						EndGame(winner);
					}
				}
				break;
			}
			
			// Player has to stand on EndOfLevel event with required treasure amount
			/*case MpGameMode::TreasureHunt:
			case MpGameMode::TeamTreasureHunt: {
				for (auto* player : _players) {
					auto* mpPlayer = static_cast<MpPlayer*>(player);
					auto peerDesc = mpPlayer->GetPeerDescriptor();

					if (peerDesc->TreasureCollected >= serverConfig.TotalTreasureCollected) {
						EndGame(mpPlayer);
						break;
					}
				}
				break;
			}*/
		}
	}

	void MpLevelHandler::BeginOvertime(MpPlayer* winner)
	{
		if (!_overtimeStarted) {
			auto& serverConfig = _networkManager->GetServerConfiguration();
			_overtimeTimeLeft = serverConfig.OvertimeSecs * FrameTimer::FramesPerSecond;
			_overtimeStarted = true;
			_overtimeFinishers = 0;

			LOGI("Overtime started - {} seconds", serverConfig.OvertimeSecs);
			// TODO: Localize on client
			ShowAlertToAllPlayers(_f("\n\nOvertime! {} seconds remaining", serverConfig.OvertimeSecs));
		}

		_overtimeFinishers++;
		SetPlayerSpectateMode(winner, SpectateMode::Forced);
	}

	void MpLevelHandler::EndGame(MpPlayer* winner)
	{
		_levelState = LevelState::Ending;
		_gameTimeLeft = EndingDuration;

		SetControllableToAllPlayers(false);
		SendLevelStateToAllPlayers();

		// Fade out
		{
			float fadeOutDelay = EndingDuration - 2 * FrameTimer::FramesPerSecond;

			_hud->BeginFadeOut(fadeOutDelay);

			MemoryStream packet(4);
			packet.WriteVariableInt32((std::int32_t)fadeOutDelay);

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelLoaded);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::FadeOut, packet);
		}

		if (winner != nullptr) {
			auto peerDesc = winner->GetPeerDescriptor();
			ShowAlertToAllPlayers(_f("\n\nWinner is {}", peerDesc->PlayerName));
			LOGW("Winner is {}", peerDesc->PlayerName);
		}

		for (auto* player : _players) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->PositionInRound > 0 && peerDesc->PositionInRound - 1 < arraySize(PointsPerPosition)) {
				peerDesc->Points += PointsPerPosition[peerDesc->PositionInRound - 1];

				if (peerDesc->RemotePeer) {
					auto& serverConfig = _networkManager->GetServerConfiguration();

					MemoryStream packet(13);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Points);
					packet.WriteVariableUint32(mpPlayer->_playerIndex);
					packet.WriteVariableUint32(peerDesc->Points);
					packet.WriteVariableUint32(serverConfig.TotalPlayerPoints);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
				}
			}
		}
	}

	void MpLevelHandler::EndGameWithTeam(std::uint8_t team)
	{
		_levelState = LevelState::Ending;
		_gameTimeLeft = EndingDuration;

		SetControllableToAllPlayers(false);
		SendLevelStateToAllPlayers();

		// Fade out
		{
			float fadeOutDelay = EndingDuration - 2 * FrameTimer::FramesPerSecond;

			_hud->BeginFadeOut(fadeOutDelay);

			MemoryStream packet(4);
			packet.WriteVariableInt32((std::int32_t)fadeOutDelay);

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelLoaded);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::FadeOut, packet);
		}

		ShowAlertToAllPlayers(_f("\n\n{} team wins!", GetTeamName(team)));
		LOGW("Team {} wins", team);

		auto& serverConfig = _networkManager->GetServerConfiguration();

		// Award championship points to the members of the winning team
		for (auto* player : _players) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();
			if (peerDesc->Team != team || peerDesc->IsSpectating != SpectateMode::None) {
				continue;
			}

			peerDesc->Points += PointsPerPosition[0];

			if (peerDesc->RemotePeer) {
				MemoryStream packet(13);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Points);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableUint32(peerDesc->Points);
				packet.WriteVariableUint32(serverConfig.TotalPlayerPoints);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::EndGameOnTimeOut()
	{
		auto& serverConfig = _networkManager->GetServerConfiguration();

		if (IsTeamGameMode(serverConfig.GameMode)) {
			// Winner is the team with the highest aggregate score; a tie ends in a draw
			std::uint8_t teamCount = GetTeamCount();
			std::uint8_t bestTeam = 0;
			std::uint32_t bestScore = 0;
			bool tied = false;
			for (std::uint8_t team = 0; team < teamCount; team++) {
				std::uint32_t score = GetTeamScore(team);
				if (score > bestScore) {
					bestScore = score;
					bestTeam = team;
					tied = false;
				} else if (score == bestScore) {
					tied = true;
				}
			}

			if (tied || bestScore == 0) {
				ShowAlertToAllPlayers(_("\n\nThe round ended in a draw"));
				EndGame(static_cast<MpPlayer*>(nullptr));
			} else {
				EndGameWithTeam(bestTeam);
			}
			return;
		}

		MpPlayer* winner = nullptr;
		switch (serverConfig.GameMode) {
			case MpGameMode::Battle: {
				std::uint32_t mostKills = 0;
				for (auto* player : _players) {
					auto* mpPlayer = static_cast<MpPlayer*>(player);
					auto peerDesc = mpPlayer->GetPeerDescriptor();

					if (mostKills < peerDesc->Kills) {
						mostKills = peerDesc->Kills;
						winner = mpPlayer;
					}
				}
				break;
			}

			case MpGameMode::Race: {
				std::uint32_t mostLaps = 0;
				for (auto* player : _players) {
					auto* mpPlayer = static_cast<MpPlayer*>(player);
					auto peerDesc = mpPlayer->GetPeerDescriptor();

					if (mostLaps < peerDesc->Laps) {
						mostLaps = peerDesc->Laps;
						winner = mpPlayer;
					}
				}
				break;
			}

			case MpGameMode::TreasureHunt: {
				std::uint32_t mostTreasureCollected = 0;
				for (auto* player : _players) {
					auto* mpPlayer = static_cast<MpPlayer*>(player);
					auto peerDesc = mpPlayer->GetPeerDescriptor();

					if (mostTreasureCollected < peerDesc->TreasureCollected) {
						mostTreasureCollected = peerDesc->TreasureCollected;
						winner = mpPlayer;
					}
				}
				break;
			}

			default: break;
		}

		EndGame(winner);
	}

	bool MpLevelHandler::ApplyFromPlaylist()
	{
		if (!_isServer) {
			return false;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		if (serverConfig.PlaylistIndex >= serverConfig.Playlist.size()) {
			return false;
		}

		auto& playlistEntry = serverConfig.Playlist[serverConfig.PlaylistIndex];

		// Override properties
		serverConfig.ReforgedGameplay = playlistEntry.ReforgedGameplay;
		serverConfig.TeamCount = playlistEntry.TeamCount;
		serverConfig.AutoBalanceTeams = playlistEntry.AutoBalanceTeams;
		serverConfig.AllowTeamSelection = playlistEntry.AllowTeamSelection;
		serverConfig.FriendlyFire = playlistEntry.FriendlyFire;
		serverConfig.Elimination = playlistEntry.Elimination;
		serverConfig.InitialPlayerHealth = playlistEntry.InitialPlayerHealth;
		serverConfig.MaxGameTimeSecs = playlistEntry.MaxGameTimeSecs;
		serverConfig.PreGameSecs = playlistEntry.PreGameSecs;
		serverConfig.TotalKills = playlistEntry.TotalKills;
		serverConfig.TotalLaps = playlistEntry.TotalLaps;
		serverConfig.TotalTreasureCollected = playlistEntry.TotalTreasureCollected;
		serverConfig.PlayerStacking = playlistEntry.PlayerStacking;
		serverConfig.AllowMinimap = playlistEntry.AllowMinimap;
		serverConfig.ColorizePlayersByTeam = playlistEntry.ColorizePlayersByTeam;

		_autoWeightTreasure = (serverConfig.TotalTreasureCollected == 0);

		if (_levelName == playlistEntry.LevelName) {
			// Level is the same, just change game mode
			SetGameMode(playlistEntry.GameMode);
		} else {
			// Level is different, new level handler needs to be created
			LevelInitialization levelInit;
			PrepareNextLevelInitialization(levelInit);
			auto level = playlistEntry.LevelName.partition('/');
			if (playlistEntry.LevelName.contains('/')) {
				levelInit.LevelName = playlistEntry.LevelName;
			} else {
				levelInit.LevelName = "unknown/"_s + playlistEntry.LevelName;
			}
			if (!ContentResolver::Get().LevelExists(levelInit.LevelName)) {
				return false;
			}

			serverConfig.GameMode = playlistEntry.GameMode;
			levelInit.IsReforged = serverConfig.ReforgedGameplay;
			levelInit.LastExitType = ExitType::Normal;
			HandleLevelChange(std::move(levelInit));
		}

		return true;
	}

	void MpLevelHandler::RestartPlaylist()
	{
		auto& serverConfig = _networkManager->GetServerConfiguration();

		if (serverConfig.Playlist.size() > 1) {
			serverConfig.PlaylistIndex = 0;
			if (serverConfig.RandomizePlaylist) {
				Random().Shuffle<PlaylistEntry>(serverConfig.Playlist);
			}
			ApplyFromPlaylist();
		}
	}

	void MpLevelHandler::SkipInPlaylist()
	{
		auto& serverConfig = _networkManager->GetServerConfiguration();

		if (serverConfig.Playlist.size() > 1) {
			serverConfig.PlaylistIndex++;
			if (serverConfig.PlaylistIndex >= serverConfig.Playlist.size()) {
				serverConfig.PlaylistIndex = 0;
				if (serverConfig.RandomizePlaylist) {
					Random().Shuffle<PlaylistEntry>(serverConfig.Playlist);
				}
			}
			ApplyFromPlaylist();
		}
	}

	void MpLevelHandler::ResetPeerPoints()
	{
		for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
			peerDesc->Points = 0;

			if (peerDesc->RemotePeer) {
				auto& serverConfig = _networkManager->GetServerConfiguration();

				MemoryStream packet(13);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Points);
				packet.WriteVariableUint32(peerDesc->Player->_playerIndex);
				packet.WriteVariableUint32(peerDesc->Points);
				packet.WriteVariableUint32(serverConfig.TotalPlayerPoints);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::SetWelcomeMessage(StringView message)
	{
		if (!_isServer) {
			return;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		serverConfig.WelcomeMessage = message;

		std::uint8_t flags = 0x04; // SetLobbyMessage

		MemoryStream packet(6 + serverConfig.WelcomeMessage.size());
		packet.WriteValue<std::uint8_t>(flags);
		packet.WriteValue<std::uint8_t>(0x00);
		packet.WriteVariableUint32(serverConfig.WelcomeMessage.size());
		packet.Write(serverConfig.WelcomeMessage.data(), serverConfig.WelcomeMessage.size());

		_networkManager->SendTo([this](const Peer& peer) {
			auto peerDesc = _networkManager->GetPeerDescriptor(peer);
			return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ShowInGameLobby, packet);
	}

	void MpLevelHandler::SetPlayerReady(PlayerType playerType, std::uint8_t team)
	{
		bool changingCharacter = _changingCharacterInLobby;
		_changingCharacterInLobby = false;

		if (_inGameLobby != nullptr) {
			_inGameLobby->Hide();
		}

		if (_isServer) {
			// Host re-picks a character mid-game: respawn the local player with the selected type directly.
			// (The initial host spawn doesn't use the lobby, so this only applies to a deliberate change.)
			if (changingCharacter) {
				for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
					if (!peerDesc->RemotePeer && peerDesc->Player) {
						peerDesc->PreferredPlayerType = playerType;
						if (team != NoPreferredTeam) {
							ChangePlayerTeam(peerDesc->Player, team, false);
						}
						SetPlayerSpectateMode(peerDesc->Player, SpectateMode::None);
						break;
					}
				}
			}
			return;
		}

		if (changingCharacter) {
			// Already in the game (real player or spectator): ask the server to respawn us as the chosen character
			MemoryStream packet(6);
			packet.WriteVariableUint32(_lastSpawnedActorId);
			packet.WriteValue<std::uint8_t>((std::uint8_t)playerType);
			_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerChangeCharacter, packet);

			// The team is changed via a separate request (the player is already spawned)
			if (team != NoPreferredTeam) {
				RequestChangeTeam(team);
			}
			return;
		}

		MemoryStream packet(2);
		packet.WriteValue<std::uint8_t>((std::uint8_t)playerType);
		packet.WriteValue<std::uint8_t>(team);

		_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerReady, packet);
	}

	void MpLevelHandler::BroadcastLocalPlayerIdle(bool isIdle)
	{
		if (!_isServer) {
			return;
		}

		// Broadcast idle state to all other players
		for (auto* player : _players) {
			if (!IsLocalPlayer(player)) {
				continue;
			}

			MemoryStream packet(6);
			packet.WriteVariableUint32(player->_playerIndex);
			packet.WriteValue<std::uint8_t>(isIdle ? 0x01 : 0x00);
			packet.WriteVariableUint32(0);

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet);
		}
	}

	void MpLevelHandler::EndActivePoll()
	{
		std::int32_t total, votedYes = 0;
		{
			auto peers = _networkManager->GetPeers();
			total = (std::int32_t)peers->size();
			votedYes = 0;
			for (auto& [playerPeer, peerDesc] : *peers) {
				if (peerDesc->VotedYes) {
					votedYes++;
				}
			}
		}

		if (votedYes >= total * 2 / 3 && total >= 3) {
			SendMessageToAll("Poll accepted, the majority of players voted yes");

			switch (_activePoll) {
				case VoteType::Restart: {
					RestartPlaylist();
					break;
				}
				case VoteType::ResetPoints: {
					ResetPeerPoints();
					break;
				}
				case VoteType::Skip: {
					SkipInPlaylist();
					break;
				}
				case VoteType::Kick: {
					// TODO
					break;
				}
			}
		} else {
			SendMessageToAll("Not enough votes to pass the poll");
		}

		_activePoll = VoteType::None;
	}

	bool MpLevelHandler::ActorShouldBeMirrored(Actors::ActorBase* actor)
	{
		// If actor has no animation, it's probably some special object (usually lights and ambient sounds)
		if (actor->_currentAnimation == nullptr && actor->_currentTransition == nullptr) {
			return true;
		}

		// List of objects that needs to be recreated on client-side instead of remoting
		return (runtime_cast<Actors::Environment::SteamNote>(actor) || runtime_cast<Actors::Solid::Pole>(actor) ||
				runtime_cast<Actors::Environment::SwingingVine>(actor) || runtime_cast<Actors::Solid::Bridge>(actor) ||
				runtime_cast<Actors::Solid::MovingPlatform>(actor) || runtime_cast<Actors::Solid::PinballBumper>(actor) ||
				runtime_cast<Actors::Solid::PinballPaddle>(actor) || runtime_cast<Actors::Solid::SpikeBall>(actor));
	}

	std::int32_t MpLevelHandler::GetTreasureWeight(std::uint8_t gemType)
	{
		switch (gemType) {
			default:
			case 0: // Red (+1)
				return 1;
			case 1: // Green (+5)
				return 5;
			case 2: // Blue (+10)
				return 10;
			case 3: // Purple
				return 1;
		}
	}

	bool MpLevelHandler::PlayerShouldHaveUnlimitedHealth(MpGameMode gameMode)
	{
		return (gameMode == MpGameMode::Race || gameMode == MpGameMode::TeamRace ||
				gameMode == MpGameMode::TreasureHunt || gameMode == MpGameMode::TeamTreasureHunt);
	}

	void MpLevelHandler::InitializeValidateAssetsPacket(MemoryStream& packet)
	{
		packet.ReserveCapacity(4 + _requiredAssets.size() * 64);
		packet.WriteVariableUint32((std::uint32_t)_requiredAssets.size());

		for (std::uint32_t i = 0; i < (std::uint32_t)_requiredAssets.size(); i++) {
			const auto& asset = _requiredAssets[i];
			packet.WriteValue<std::uint8_t>((std::uint8_t)asset.Type);
			packet.WriteVariableUint32((std::uint32_t)asset.Path.size());
			packet.Write(asset.Path.data(), (std::int64_t)asset.Path.size());
		}
	}

	void MpLevelHandler::InitializeLoadLevelPacket(MemoryStream& packet)
	{
		auto& serverConfig = _networkManager->GetServerConfiguration();

		std::uint32_t flags = 0;
		if (_isReforged) {
			flags |= 0x01;
		}
		if (PreferencesCache::EnableLedgeClimb) {
			flags |= 0x02;
		}
		if (serverConfig.Elimination) {
			flags |= 0x04;
		}
		if (serverConfig.EnableSpectate) {
			flags |= 0x08;
		}
		if (serverConfig.PlayerStacking) {
			flags |= 0x10;
		}

		packet.ReserveCapacity(29 + _levelName.size());
		packet.WriteVariableUint32(flags);
		packet.WriteValue<std::uint8_t>((std::uint8_t)_levelState);
		packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.GameMode);
		packet.WriteValue<std::uint8_t>((std::uint8_t)/*levelInit.LastExitType*/0);	// TODO: LastExitType
		packet.WriteVariableUint32(_levelName.size());
		packet.Write(_levelName.data(), _levelName.size());
		packet.WriteVariableInt32(serverConfig.InitialPlayerHealth);
		packet.WriteVariableUint32(serverConfig.MaxGameTimeSecs);
		packet.WriteVariableUint32(serverConfig.TotalKills);
		packet.WriteVariableUint32(serverConfig.TotalLaps);
		packet.WriteVariableUint32(serverConfig.TotalTreasureCollected);
		packet.WriteValue<std::uint8_t>(serverConfig.AllowedPlayerTypes);
	}

	void MpLevelHandler::InitializeCreateRemoteActorPacket(MemoryStream& packet, std::uint32_t actorId, const Actors::ActorBase* actor)
	{
		String metadataPath = fs::FromNativeSeparators(actor->_metadata->Path);

		std::uint8_t flags = 0;
		if (actor->_renderer.isDrawEnabled()) {
			flags |= 0x04;
		}
		if (actor->_renderer.AnimPaused) {
			flags |= 0x08;
		}
		if (actor->_renderer.isFlippedX()) {
			flags |= 0x10;
		}
		if (actor->_renderer.isFlippedY()) {
			flags |= 0x20;
		}

		packet.ReserveCapacity(36 + metadataPath.size());

		packet.WriteVariableUint32(actorId);
		packet.WriteValue<std::uint8_t>(flags);
		packet.WriteVariableInt32((std::int32_t)actor->_pos.X);
		packet.WriteVariableInt32((std::int32_t)actor->_pos.Y);
		packet.WriteVariableInt32((std::int32_t)actor->_renderer.layer());
		packet.WriteVariableUint32((std::uint32_t)actor->_state);
		packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
		packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
		packet.WriteVariableUint32((std::uint32_t)(actor->_currentTransition != nullptr ? actor->_currentTransition->State : actor->_currentAnimation->State));

		float rotation = actor->_renderer.rotation();
		if (rotation < 0.0f) rotation += fRadAngle360;
		packet.WriteValue<std::uint16_t>((std::uint16_t)(rotation * UINT16_MAX / fRadAngle360));
		Vector2f scale = actor->_renderer.scale();
		packet.WriteValue<std::uint16_t>((std::uint16_t)Half{scale.X});
		packet.WriteValue<std::uint16_t>((std::uint16_t)Half{scale.Y});
		packet.WriteValue<std::uint8_t>((std::uint8_t)actor->_renderer.GetRendererType());
	}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
	void MpLevelHandler::ShowDebugWindow()
	{
		const float appWidth = theApplication().GetWidth();

		//const ImVec2 windowPos = ImVec2(appWidth * 0.5f, ImGui::GetIO().DisplaySize.y - Margin);
		//const ImVec2 windowPosPivot = ImVec2(0.5f, 1.0f);
		//ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver, windowPosPivot);
		ImGui::Begin("Multiplayer Debug", nullptr);

		ImGui::PlotLines("Update Packet Size", _updatePacketSize, PlotValueCount, _plotIndex, nullptr, 0.0f, _updatePacketMaxSize, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(600.0f);
		ImGui::Text("%.0f bytes", _updatePacketSize[_plotIndex]);

		ImGui::PlotLines("Update Packet Size Compressed", _compressedUpdatePacketSize, PlotValueCount, _plotIndex, nullptr, 0.0f, _updatePacketMaxSize, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(600.0f);
		ImGui::Text("%.0f bytes", _compressedUpdatePacketSize[_plotIndex]);

		ImGui::Separator();

		ImGui::PlotLines("Actors", _actorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(600.0f);
		ImGui::Text("%.0f", _actorsCount[_plotIndex]);

		ImGui::PlotLines("Remote Actors", _remoteActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(600.0f);
		ImGui::Text("%.0f", _remoteActorsCount[_plotIndex]);

		ImGui::PlotLines("Mirrored Actors", _mirroredActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(600.0f);
		ImGui::Text("%.0f", _mirroredActorsCount[_plotIndex]);

		ImGui::PlotLines("Remoting Actors", _remotingActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(600.0f);
		ImGui::Text("%.0f", _remotingActorsCount[_plotIndex]);

		ImGui::Text("Last spawned ID: %u", _lastSpawnedActorId);

		ImGui::SeparatorText("Peers");

		ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoPadOuterX;
		if (ImGui::BeginTable("peers", 9, flags, ImVec2(0.0f, 0.0f))) {
			ImGui::TableSetupColumn("Peer");
			ImGui::TableSetupColumn("Player Index");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Ping");
			ImGui::TableSetupColumn("Last Updated");
			ImGui::TableSetupColumn("X");
			ImGui::TableSetupColumn("Y");
			ImGui::TableSetupColumn("Flags");
			ImGui::TableSetupColumn("Pressed");
			ImGui::TableHeadersRow();
			
			for (auto& [peer, desc] : *_networkManager->GetPeers()) {
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("0x%08x", peer.GetId());

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", desc->Player != nullptr ? desc->Player->GetPlayerIndex() : -1);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", _networkManager->GetRoundTripTimeMs(desc->RemotePeer));

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", desc->LastUpdated);

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", desc->RemotePeer);

				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.2f", desc->Player != nullptr ? desc->Player->GetPos().X : -1.0f);

				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%.2f", desc->Player != nullptr ? desc->Player->GetPos().Y : -1.0f);

				if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(desc->Player)) {
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("0x%02x", remotePlayerOnServer->Flags);

					ImGui::TableSetColumnIndex(8);
					ImGui::Text("0x%04x", remotePlayerOnServer->PressedKeys);
				}
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}
#endif
}

#endif