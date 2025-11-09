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

	// TODO: levelState is unused, it needs to be set after LevelState::InitialUpdatePending is processed
	MpLevelHandler::MpLevelHandler(IRootController* root, NetworkManager* networkManager, MpLevelHandler::LevelState levelState, bool enableLedgeClimb)
		: LevelHandler(root), _networkManager(networkManager), _updateTimeLeft(1.0f), _gameTimeLeft(0.0f),
			_levelState(LevelState::InitialUpdatePending), _forceResyncPending(true), _enableSpawning(true), _lastSpawnedActorId(-1), _waitingForPlayerCount(0),
			_lastUpdated(0), _seqNumWarped(0), _suppressRemoting(false), _ignorePackets(false), _enableLedgeClimb(enableLedgeClimb),
			_controllableExternal(true), _autoWeightTreasure(false), _activePoll(VoteType::None), _activePollTimeLeft(0.0f), _recalcPositionInRoundTime(0.0f),
			_limitCameraLeft(0), _limitCameraWidth(0), _totalTreasureCount(0)
#if defined(DEATH_DEBUG)
			, _debugAverageUpdatePacketSize(0)
#endif
#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
			, _plotIndex(0), _actorsMaxCount(0.0f), _actorsCount{}, _remoteActorsCount{}, _remotingActorsCount{},
			_mirroredActorsCount{}, _updatePacketMaxSize(0.0f), _updatePacketSize{}, _compressedUpdatePacketSize{}
#endif
	{
		_isServer = (networkManager->GetState() == NetworkState::Listening);
	}

	MpLevelHandler::~MpLevelHandler()
	{
	}

	bool MpLevelHandler::Initialize(const LevelInitialization& levelInit)
	{
		DEATH_DEBUG_ASSERT(!levelInit.IsLocalSession);

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

			for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
				peerDesc->LevelState = PeerLevelState::ValidatingAssets;
				peerDesc->LastUpdated = 0;
				if (peerDesc->RemotePeer) {
					peerDesc->Player = nullptr;
				}
			}
		}

		auto& resolver = ContentResolver::Get();
		resolver.PreloadMetadataAsync("Interactive/PlayerJazz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerSpaz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerLori"_s);

		_inGameCanvasLayer = std::make_unique<UI::Multiplayer::MpInGameCanvasLayer>(this);
		_inGameCanvasLayer->setParent(_rootNode.get());
		_inGameLobby = std::make_unique<UI::Multiplayer::MpInGameLobby>(this);

		return true;
	}

	bool MpLevelHandler::IsLocalSession() const
	{
		return false;
	}

	bool MpLevelHandler::IsServer() const
	{
		return _isServer;
	}

	bool MpLevelHandler::IsPausable() const
	{
		return false;
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

			if (_activePoll != VoteType::None) {
				_activePollTimeLeft -= timeMult;
				if (_activePollTimeLeft <= 0.0f) {
					EndActivePoll();
				}
			}
		}

		_updateTimeLeft -= timeMult;
		_gameTimeLeft -= timeMult;

		if (_updateTimeLeft < 0.0f) {
			_updateTimeLeft = FrameTimer::FramesPerSecond / UpdatesPerSecond;

			switch (_levelState) {
				case LevelState::InitialUpdatePending: {
					LOGD("[MP] Level \"{}\" is ready", _levelName);

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
						if (_players.size() >= serverConfig.MinPlayerCount) {
							_levelState = LevelState::Countdown3;
							_gameTimeLeft = FrameTimer::FramesPerSecond;
							SetControllableToAllPlayers(false);
							WarpAllPlayersToStart();
							SynchronizeGameMode();
							ResetAllPlayerStats();
							static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(3);
							LOGI("[MP] Starting round...");
						} else {
							_levelState = LevelState::WaitingForMinPlayers;
							_waitingForPlayerCount = (std::int32_t)serverConfig.MinPlayerCount - (std::int32_t)_players.size();
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
							LOGI("[MP] Starting round...");
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
					std::uint32_t actorCount = (std::uint32_t)(_players.size() + _remotingActors.size());

					MemoryStream packet(8 + actorCount * 24);
					packet.WriteVariableUint32(_lastUpdated);
					packet.WriteVariableUint64((std::uint64_t)_elapsedFrames);
					packet.WriteVariableUint32((actorCount << 1) | (_forceResyncPending ? 1 : 0));

					for (Actors::Player* player : _players) {
						auto* mpPlayer = static_cast<PlayerOnServer*>(player);

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

					SynchronizePeers();
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

		if (_inGameLobby->IsVisible() && _pauseMenu == nullptr && !_console->IsVisible()) {
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
		if (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::TeamRace) {
			auto* mpPlayer = static_cast<MpPlayer*>(initiator);
			if (mpPlayer->_currentTransition == nullptr ||
				(mpPlayer->_currentTransition->State != AnimState::TransitionWarpIn && mpPlayer->_currentTransition->State != AnimState::TransitionWarpOut &&
					mpPlayer->_currentTransition->State != AnimState::TransitionWarpInFreefall && mpPlayer->_currentTransition->State != AnimState::TransitionWarpOutFreefall)) {
				Vector2f spawnPosition = GetSpawnPoint(mpPlayer->_playerTypeOriginal);
				mpPlayer->WarpToPosition(spawnPosition, WarpFlags::IncrementLaps);
			}
			return;
		} else if (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt) {
			// Player has to stand on EndOfLevel event with required treasure amount
			auto* mpPlayer = static_cast<MpPlayer*>(initiator);
			auto peerDesc = mpPlayer->GetPeerDescriptor();
			if (peerDesc->TreasureCollected >= serverConfig.TotalTreasureCollected) {
				peerDesc->TreasureCollected++;	// The escaped player should have the most points
				CalculatePositionInRound();
				EndGame(mpPlayer);
			}
			return;
		} else if (serverConfig.GameMode != MpGameMode::Cooperation) {
			// Ignore end of the level in all game modes except cooperation
			return;
		}

		if (nextLevel.empty()) {
			nextLevel = _defaultNextLevel;
		}

		LOGD("[MP] Changing level to \"{}\" (0x{:.2x})", nextLevel, exitType);

		if ((nextLevel == ":end"_s || nextLevel == ":credits"_s) && !serverConfig.Playlist.empty()) {
			serverConfig.PlaylistIndex++;
			if (serverConfig.PlaylistIndex >= serverConfig.Playlist.size()) {
				serverConfig.PlaylistIndex = 0;
				if (serverConfig.RandomizePlaylist) {
					Random().Shuffle<PlaylistEntry>(serverConfig.Playlist);
				}
			}
			// TODO: Implement transition
			if (ApplyFromPlaylist()) {
				return;
			}
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
		levelInit.IsLocalSession = false;
		LevelHandler::HandleLevelChange(std::move(levelInit));
	}

	void MpLevelHandler::HandleGameOver(Actors::Player* player)
	{
		// TODO
		//LevelHandler::HandleGameOver(player);
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

				if (serverConfig.GameMode != MpGameMode::Cooperation) {
					mpPlayer->_checkpointPos = GetSpawnPoint(peerDesc->PreferredPlayerType);

					// The player is invulnerable for a short time after respawning
					mpPlayer->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Blinking);
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

	void MpLevelHandler::HandlePlayerLevelChanging(Actors::Player* player, ExitType exitType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
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

	bool MpLevelHandler::HandlePlayerSpring(Actors::Player* player, Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
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
		if (_isServer) {
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
		if (_isServer) {
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
		if (_isServer) {
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
		if (_isServer) {
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
		if (_isServer) {
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
		if (_isServer) {
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
		if (_isServer) {
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
		if (_isServer) {
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
					packet5.WriteVariableUint64((std::uint64_t)peerDesc->RemotePeer._enet);
					packet5.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
					packet5.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());
					packet5.WriteVariableUint64((std::uint64_t)attackerPeerDesc->RemotePeer._enet);
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
					packet6.WriteVariableUint64((std::uint64_t)peerDesc->RemotePeer._enet);
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

	void MpLevelHandler::HandlePlayerRefreshAmmo(Actors::Player* player, WeaponType weaponType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
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

	void MpLevelHandler::HandlePlayerMorphTo(Actors::Player* player, PlayerType type)
	{
		// TODO: Only called by PlayerOnServer
		if (_isServer) {
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
		if (_isServer) {
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

	void MpLevelHandler::HandlePlayerSetShield(Actors::Player* player, ShieldType shieldType, float timeLeft)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Shield);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)shieldType);
				packet.WriteVariableInt32((std::int32_t)timeLeft);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerRefreshWeaponUpgrades(Actors::Player* player, WeaponType weaponType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
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

	void MpLevelHandler::HandlePlayerEmitWeaponFlare(Actors::Player* player)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
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

				MemoryStream packet(16);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_pos.X * 512.0f));
				packet.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_pos.Y * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_speed.X * 512.0f));
				packet.WriteValue<std::int16_t>((std::int16_t)(mpPlayer->_speed.Y * 512.0f));
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

	void MpLevelHandler::HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount)
	{
		//LevelHandler::HandlePlayerCoins(player, prevCount, newCount);

		if (_isServer) {
			// Coins are shared in cooperation, add it also to all other local players
			auto& serverConfig = _networkManager->GetServerConfiguration();
			if (serverConfig.GameMode == MpGameMode::Cooperation) {
				if (prevCount < newCount) {
					std::int32_t increment = (newCount - prevCount);
					for (auto current : _players) {
						if (current != player) {
							current->AddCoinsInternal(increment);
						}
					}
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
		// Online multiplayer sessions cannot be resumed
		return false;
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

	void MpLevelHandler::AttachComponents(LevelDescriptor&& descriptor)
	{
		LevelHandler::AttachComponents(std::move(descriptor));

		if (_isServer) {
			// Cache all possible multiplayer spawn points (if it's not coop level) and race checkpoints
			_multiplayerSpawnPoints.clear();
			_raceCheckpoints.clear();
			_totalTreasureCount = 0;

			_eventMap->ForEachEvent([this](Events::EventMap::EventTile& e, std::int32_t x, std::int32_t y) {
				if (e.Event == EventType::LevelStartMultiplayer) {
					_multiplayerSpawnPoints.emplace_back(Vector2f(x * Tiles::TileSet::DefaultTileSize, y * Tiles::TileSet::DefaultTileSize - 8), e.EventParams[0]);
				} else if (e.Event == EventType::WarpOrigin) {
					if (e.EventParams[2] != 0) {
						_raceCheckpoints.emplace_back(Vector2i(x, y));
					}
				} else if (e.Event == EventType::AreaEndOfLevel) {
					_raceCheckpoints.emplace_back(Vector2i(x, y));
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

			ConsolidateRaceCheckpoints();

			const auto& serverConfig = _networkManager->GetServerConfiguration();
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

			auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer);
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

			peerDesc->LevelState = PeerLevelState::PlayerSpawned;
			peerDesc->LapsElapsedFrames = _elapsedFrames;
			peerDesc->LapStarted = TimeStamp::now();
			peerDesc->PlayerName = PreferencesCache::GetEffectivePlayerName();

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

	bool MpLevelHandler::IsCheatingAllowed()
	{
		if (!_isServer) {
			return false;
		}

		auto& serverConfig = _networkManager->GetServerConfiguration();
		return (PreferencesCache::AllowCheats && serverConfig.GameMode == MpGameMode::Cooperation);
	}

	MpGameMode MpLevelHandler::GetGameMode() const
	{
		const auto& serverConfig = _networkManager->GetServerConfiguration();
		return serverConfig.GameMode;
	}

	bool MpLevelHandler::SetGameMode(MpGameMode value)
	{
		if (!_isServer) {
			return false;
		}

		LOGI("[MP] Game mode set to {}", NetworkManager::GameModeToString(value));

		auto& serverConfig = _networkManager->GetServerConfiguration();
		serverConfig.GameMode = value;

		ApplyGameModeToAllPlayers(serverConfig.GameMode);
		SynchronizeGameMode();

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

		for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
			if (peerDesc->LevelState < PeerLevelState::LevelSynchronized) {
				continue;
			}

			MemoryStream packet(24);
			packet.WriteValue<std::uint8_t>((std::uint8_t)LevelPropertyType::GameMode);
			packet.WriteValue<std::uint8_t>(flags);
			packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.GameMode);
			packet.WriteValue<std::uint8_t>(peerDesc->Team);
			packet.WriteVariableInt32(serverConfig.InitialPlayerHealth);
			packet.WriteVariableUint32(serverConfig.MaxGameTimeSecs);
			packet.WriteVariableUint32(serverConfig.TotalKills);
			packet.WriteVariableUint32(serverConfig.TotalLaps);
			packet.WriteVariableUint32(serverConfig.TotalTreasureCollected);

			_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LevelSetProperty, packet);
		}

		return true;
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
						peerDesc->PositionInRound, playerName, peerDesc->RemotePeer ? peerDesc->RemotePeer._enet->roundTripTime : 0,
						peerDesc->Points, peerDesc->Player ? (std::int32_t)(peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame) : -1);
				} else if (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::Race) {
					length = formatInto(infoBuffer, "{}.\t{}\t │ {} ms\t │ P: {}\t │ K: {}\t │ D: {}\t │ I: {}\f[w:50] s\f[/w]\t│ Laps: {}/{}",
						peerDesc->PositionInRound, playerName, peerDesc->RemotePeer ? peerDesc->RemotePeer._enet->roundTripTime : 0,
						peerDesc->Points, peerDesc->Kills, peerDesc->Deaths, peerDesc->Player ? (std::int32_t)(peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame) : -1, 
						peerDesc->Laps + 1, serverConfig.TotalLaps);
				} else if (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt) {
					length = formatInto(infoBuffer, "{}.\t{}\t │ {} ms\t │ P: {}\t │ K: {}\t │ D: {}\t │ I: {}\f[w:50] s\f[/w]\t│ Treasure: {}",
						peerDesc->PositionInRound, playerName, peerDesc->RemotePeer ? peerDesc->RemotePeer._enet->roundTripTime : 0,
						peerDesc->Points, peerDesc->Kills, peerDesc->Deaths, peerDesc->Player ? (std::int32_t)(peerDesc->IdleElapsedFrames * FrameTimer::SecondsPerFrame) : -1,
						peerDesc->TreasureCollected);
				} else {
					length = formatInto(infoBuffer, "{}.\t{}\t │ {} ms\t │ P: {}\t │ K: {}\t │ D: {}\t │ I: {}\f[w:50] s\f[/w]",
						peerDesc->PositionInRound, playerName, peerDesc->RemotePeer ? peerDesc->RemotePeer._enet->roundTripTime : 0,
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
						LOGD("[MP] Changing level to \"{}\"", levelInit.LevelName);

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
				}
			}
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
									NetworkManager::UuidToString(peerDesc->UniquePlayerID), NetworkManagerBase::AddressToString(peerDesc->RemotePeer));
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
				switch (level) {
					default: __DEATH_TRACE(TraceLevel::Info, {}, "< │ {}", message); break;
					case UI::MessageLevel::Echo: __DEATH_TRACE(TraceLevel::Info, {}, "> │ {}", message); break;
					case UI::MessageLevel::Warning: __DEATH_TRACE(TraceLevel::Warning, {}, "< │ {}", message); break;
					case UI::MessageLevel::Error: __DEATH_TRACE(TraceLevel::Error, {}, "< │ {}", message); break;
					case UI::MessageLevel::Assert: __DEATH_TRACE(TraceLevel::Assert, {}, "< │ {}", message); break;
					case UI::MessageLevel::Fatal: __DEATH_TRACE(TraceLevel::Fatal, {}, "< │ {}", message); break;
				}
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
				packet.WriteVariableUint64((std::uint64_t)peer._enet);
				packet.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
				packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());

				_networkManager->SendTo([otherPeer = peer](const Peer& peer) {
					return (peer != otherPeer);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PeerSetProperty, packet);

				if (MpPlayer* player = peerDesc->Player) {
					std::int32_t playerIndex = player->_playerIndex;
					Vector2f pos = player->_pos;

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
						_waitingForPlayerCount = (std::int32_t)serverConfig.MinPlayerCount - (std::int32_t)_players.size();
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

					// TODO: remotingActor-> OnPacketReceived() is also locked here, which is not good
					std::unique_lock lock(_lock);
					for (const auto& [remotingActor, remotingActorInfo] : _remotingActors) {
						if (remotingActorInfo.ActorID == actorId) {
							LOGD("[MP] ClientPacketType::Rpc [{:.8x}] - id: {}, {} bytes", std::uint64_t(peer._enet), actorId, data.size() - packet.GetPosition());
							remotingActor->OnPacketReceived(packet);
							return true;
						}
					}

					LOGW("[MP] ClientPacketType::Rpc [{:.8x}] - id: {}, {} bytes - ACTOR NOT FOUND", std::uint64_t(peer._enet), actorId, data.size() - packet.GetPosition());
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
						packet.WriteVariableUint64((std::uint64_t)peer._enet);
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

					LOGD("[MP] ClientPacketType::LevelReady [{:.8x}] - flags: 0x{:.2x}", std::uint64_t(peer._enet), flags);

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
						LOGD("[MP] ClientPacketType::ChatMessage [{:.8x}] - invalid playerIndex ({})", std::uint64_t(peer._enet), playerIndex);
						return true;
					}

					/*std::uint8_t reserved =*/ packet.ReadValue<std::uint8_t>();

					std::uint32_t lineLength = packet.ReadVariableUint32();
					if (lineLength == 0 || lineLength > 1024) {
						LOGD("[MP] ClientPacketType::ChatMessage [{:.8x}] - length out of bounds ({})", std::uint64_t(peer._enet), lineLength);
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
						LOGW("[MP] ClientPacketType::ValidateAssetsResponse [{:.8x}] - invalid state ({})", std::uint64_t(peer._enet), peerDesc->LevelState);
						return true;
					}

					peerDesc->LevelState = PeerLevelState::StreamingMissingAssets;

					bool success = true;
					SmallVector<RequiredAsset*> missingAssets;

					MemoryStream packet(data);
					std::uint32_t assetCount = packet.ReadVariableUint32();

					LOGD("[MP] ClientPacketType::ValidateAssetsResponse [{:.8x}] - {}/{} assets", std::uint64_t(peer._enet), assetCount, _requiredAssets.size());

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
										LOGD("[MP] ClientPacketType::ValidateAssetsResponse [{:.8x}] - \"{}\":{:.8x} is missing",
											std::uint64_t(peer._enet), _requiredAssets[j].Path, _requiredAssets[j].Crc32);
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
						LOGW("[MP] ClientPacketType::ValidateAssetsResponse [{:.8x}] - malformed packet", std::uint64_t(peer._enet));
						_networkManager->Kick(peer, Reason::InvalidParameter);
						return true;
					}

					LOGI("[MP] ClientPacketType::ValidateAssetsResponse [{:.8x}] - {} missing assets", std::uint64_t(peer._enet), missingAssets.size());

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
					
					Thread streamingThread([_this = runtime_cast<MpLevelHandler>(shared_from_this()), peer, peerDesc = std::move(peerDesc), missingAssets = std::move(missingAssets)]() {
						LOGI("[MP] Started streaming {} assets to peer [{:.8x}]", missingAssets.size(), std::uint64_t(peer._enet));
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

						LOGI("[MP] Finished streaming {} assets to peer [{:.8x}] - took {:.1f} ms",
							missingAssets.size(), std::uint64_t(peer._enet), begin.millisecondsSince());

						if (peerDesc->IsAuthenticated && !_this->_ignorePackets) {
							MemoryStream packet;
							_this->InitializeLoadLevelPacket(packet);
							_this->_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LoadLevel, packet);
						}
					});

					return true;
				}
				case ClientPacketType::PlayerReady: {
					MemoryStream packet(data);
					PlayerType preferredPlayerType = (PlayerType)packet.ReadValue<std::uint8_t>();
					std::uint8_t preferredTeam = packet.ReadValue<std::uint8_t>();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->LevelState != PeerLevelState::LevelLoaded && peerDesc->LevelState != PeerLevelState::LevelSynchronized) {
						LOGW("[MP] ClientPacketType::PlayerReady [{:.8x}] - invalid state", std::uint64_t(peer._enet));
						return true;
					}

					if (preferredPlayerType != PlayerType::Jazz && preferredPlayerType != PlayerType::Spaz && preferredPlayerType != PlayerType::Lori) {
						LOGW("[MP] ClientPacketType::PlayerReady [{:.8x}] - invalid preferred player type", std::uint64_t(peer._enet));
						return true;
					}

					peerDesc->PreferredPlayerType = preferredPlayerType;

					LOGI("[MP] ClientPacketType::PlayerReady [{:.8x}] - type: {}, team: {}", std::uint64_t(peer._enet), preferredPlayerType, preferredTeam);

					InvokeAsync([this, peer]() {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						if (!peerDesc || peerDesc->LevelState < PeerLevelState::LevelLoaded || peerDesc->LevelState > PeerLevelState::LevelSynchronized) {
							LOGW("[MP] ClientPacketType::PlayerReady [{:.8x}] - invalid state", std::uint64_t(peer._enet));
							return;
						}
						if (peerDesc->LevelState == PeerLevelState::LevelSynchronized) {
							peerDesc->LevelState = PeerLevelState::PlayerReady;
						}
					});
					return true;
				}
				case ClientPacketType::ForceResyncActors: {
					LOGD("[MP] ClientPacketType::ForceResyncActors [{:.8x}] - update: {}", std::uint64_t(peer._enet), _lastUpdated);
					_forceResyncPending = true;
					return true;
				}
				case ClientPacketType::PlayerUpdate: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerUpdate [{:.8x}] - invalid playerIndex ({})", std::uint64_t(peer._enet), playerIndex);
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

					/*bool justWarped = (flags & PlayerFlags::JustWarped) == PlayerFlags::JustWarped;
					if (justWarped) {
						std::uint64_t seqNumWarped = packet.ReadVariableUint64();
						if (seqNumWarped == it2->second.WarpSeqNum) {
							justWarped = false;
						} else {
							LOGD("Acknowledged player {} warp for sequence #{}", playerIndex, seqNumWarped);
							it2->second.WarpSeqNum = seqNumWarped;

							MemoryStream packet2(13);
							packet2.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::PlayerAckWarped);
							packet2.WriteVariableUint32(playerIndex);
							packet2.WriteVariableUint64(seqNumWarped);

							_networkManager->SendTo(peer, NetworkChannel::Main, packet2);
						}
					}
		
					if ((it2->second.Flags & PlayerFlags::JustWarped) == PlayerFlags::JustWarped) {
						// Player already warped locally, mark the request as handled
						if (it2->second.WarpTimeLeft > 0.0f) {
							LOGD("Granted permission to player {} to warp asynchronously", playerIndex);
							it2->second.WarpTimeLeft = 0.0f;
						} else if (!justWarped) {
							LOGD("Granted permission to player {} to warp before client", playerIndex);
							it2->second.WarpTimeLeft = -90.0f;
						}
					} else if (justWarped) {
						// Player warped remotely but not locally, keep some time to resync with local state
						if (it2->second.WarpTimeLeft < 0.0f) {
							// Server is faster than client (probably due to higher latency)
							LOGD("Player already warped locally");
							it2->second.WarpTimeLeft = 0.0f;
						} else {
							// Client is faster than server
							LOGD("Player warped remotely (local permission pending)");
							it2->second.WarpTimeLeft = 90.0f;
						}
					} else if (it2->second.WarpTimeLeft <= 0.0f) {
						constexpr float MaxDeviation = 256.0f;

						float posDiffSqr = (Vector2f(posX, posY) - player->_pos).SqrLength();
						float speedSqr = std::max(player->_speed.SqrLength(), Vector2f(speedX, speedY).SqrLength());
						if (posDiffSqr > speedSqr + (MaxDeviation * MaxDeviation)) {
							LOGW("Player {} position mismatch by {} pixels (speed: {:.2f})", playerIndex, (std::int32_t)sqrt(posDiffSqr), sqrt(speedSqr));

							posX = player->_pos.X;
							posY = player->_pos.Y;

							MemoryStream packet2(13);
							packet2.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::PlayerMoveInstantly);
							packet2.WriteVariableUint32(player->_playerIndex);
							packet2.WriteValue<std::int32_t>((std::int32_t)(posX * 512.0f));
							packet2.WriteValue<std::int32_t>((std::int32_t)(posY * 512.0f));
							packet2.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
							packet2.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
							_networkManager->SendTo(peer, NetworkChannel::Main, packet2);
						}
					}*/

					// TODO: Special move

					if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(peerDesc->Player)) {
						remotePlayerOnServer->SyncWithServer(Vector2f(posX, posY), Vector2f(speedX, speedY), flags);
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
						LOGD("[MP] ClientPacketType::PlayerChangeWeaponRequest [{:.8x}] - invalid playerIndex ({})", std::uint64_t(peer._enet), playerIndex);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ClientPacketType::PlayerChangeWeaponRequest [{:.8x}] - playerIndex: {}, weaponType: {}", std::uint64_t(peer._enet), playerIndex, weaponType);

					const auto& playerAmmo = peerDesc->Player->GetWeaponAmmo();
					if (weaponType >= playerAmmo.size() || playerAmmo[weaponType] == 0) {
						LOGD("[MP] ClientPacketType::PlayerChangeWeaponRequest [{:.8x}] - playerIndex: {}, no ammo in selected weapon", std::uint64_t(peer._enet), playerIndex);

						// Request is denied, send the current weapon back to the client
						HandlePlayerWeaponChanged(peerDesc->Player, Actors::Player::SetCurrentWeaponReason::Rollback);
						return true;
					}

					peerDesc->Player->SetCurrentWeapon((WeaponType)weaponType, Actors::Player::SetCurrentWeaponReason::User);
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
						LOGD("[MP] ClientPacketType::PlayerAckWarped [{:.8x}] - invalid playerIndex ({})", std::uint64_t(peer._enet), playerIndex);
						return true;
					}

					LOGD("[MP] ClientPacketType::PlayerAckWarped [{:.8x}] - playerIndex: {}, seqNum: {}, x: {}, y: {}",
						std::uint64_t(peer._enet), playerIndex, seqNum, posX, posY);

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
						LOGW("[MP] ServerPacketType::Rpc - id: {}, {} bytes - ACTOR NOT FOUND", actorId,data.size() - packet.GetPosition());
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
							std::uint8_t teamId = packet.ReadValue<std::uint8_t>();
							std::int32_t initialPlayerHealth = packet.ReadVariableInt32();
							std::uint32_t maxGameTimeSecs = packet.ReadVariableUint32();
							std::uint32_t totalKills = packet.ReadVariableUint32();
							std::uint32_t totalLaps = packet.ReadVariableUint32();
							std::uint32_t totalTreasureCollected = packet.ReadVariableUint32();

							LOGD("[MP] ServerPacketType::LevelSetProperty::GameMode - mode: {}", gameMode);

							auto& serverConfig = _networkManager->GetServerConfiguration();
							serverConfig.GameMode = gameMode;
							serverConfig.ReforgedGameplay = (flags & 0x01) != 0;
							serverConfig.Elimination = (flags & 0x04) != 0;
							serverConfig.InitialPlayerHealth = initialPlayerHealth;
							serverConfig.MaxGameTimeSecs = maxGameTimeSecs;
							serverConfig.TotalKills = totalKills;
							serverConfig.TotalLaps = totalLaps;
							serverConfig.TotalTreasureCollected = totalTreasureCollected;

							_isReforged = serverConfig.ReforgedGameplay;

							if (auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer)) {
								peerDesc->Team = teamId;
							}
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
							LOGD("[MP] ServerPacketType::LevelSetProperty - received unknown property {}", (std::uint32_t)propertyType);
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
							_inGameLobby->SetAllowedPlayerTypes(allowedPlayerTypes);
							if (flags & 0x02) {
								_inGameLobby->Show();
							} else {
								_inGameLobby->Hide();
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
						LOGD("[MP] ServerPacketType::ChatMessage - length out of bounds ({})", messageLength);
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
								LOGW("[MP] ServerPacketType::CreateDebris - NOT FOUND - actorId: {}", actorId);
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
								LOGW("[MP] ServerPacketType::CreateDebris - NOT FOUND - actorId: {}", actorId);
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

					LOGI("[MP] ServerPacketType::CreateControllablePlayer - playerIndex: {}, playerType: {}, health: {}, flags: {}, team: {}, x: {}, y: {}",
						playerIndex, playerType, health, flags, teamId, posX, posY);

					_lastSpawnedActorId = playerIndex;

					InvokeAsync([this, playerType, health, flags, teamId, posX, posY]() {
						auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer);
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

						peerDesc->Team = teamId;
						peerDesc->LapStarted = TimeStamp::now();

						Actors::Multiplayer::RemotablePlayer* ptr = player.get();
						_players.push_back(ptr);
						AddActor(player);
						AssignViewport(ptr);
						// TODO: Needed to drop and reinitialize newly assigned viewport, because it's called asynchronously, not from handler initialization
						Viewport::GetChain().clear();
						Vector2i res = theApplication().GetResolution();
						OnInitializeViewport(res.X, res.Y);

						// TODO: Fade in should be skipped sometimes
						_hud->BeginFadeIn(false);
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
								LOGW("[MP] ServerPacketType::CreateRemoteActor - actor ({}) already exists", actorId);
								return;
							}
						}

						std::shared_ptr<Actors::Multiplayer::RemoteActor> remoteActor = std::make_shared<Actors::Multiplayer::RemoteActor>();
						remoteActor->OnActivated(Actors::ActorActivationDetails(this, Vector3i(posX, posY, posZ)));
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
								LOGW("[MP] ServerPacketType::CreateMirroredActor - actor ({}) already exists", actorId);
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
							LOGD("[MP] ServerPacketType::CreateMirroredActor - CANNOT CREATE - actorId: {}", actorId);
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
						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							it->second->SetState(Actors::ActorState::IsDestroyed, true);
							_remoteActors.erase(it);
							_playerNames.erase(actorId);
						} else {
							LOGW("[MP] ServerPacketType::DestroyRemoteActor - NOT FOUND - actorId: {}", actorId);
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
						LOGD("[MP] ServerPacketType::UpdateAllActors - FORCE RESYNC REQUIRED ({} -> {})", _lastUpdated, now);
					} else if (forceResyncInvoked) {
						LOGD("[MP] ServerPacketType::UpdateAllActors - FORCE RESYNC INVOKED ({} -> {})", _lastUpdated, now);
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
								remoteActor->RequestMetadata(metadataPath);
							}
						}
					});
					return true;
					break;
				}
				case ServerPacketType::MarkRemoteActorAsPlayer: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					std::uint32_t playerNameLength = packet.ReadVariableUint32();
					String playerName = String(NoInit, playerNameLength);
					packet.Read(playerName.data(), playerNameLength);

					LOGD("[MP] ServerPacketType::MarkRemoteActorAsPlayer - id: {}, name: \"{}\"", actorId, playerName);

					InvokeAsync([this, actorId, playerName = std::move(playerName)]() mutable {
						if (actorId == _lastSpawnedActorId) {
							auto peerDesc = _networkManager->GetPeerDescriptor(LocalPeer);
							peerDesc->PlayerName = std::move(playerName);
						} else {
							_playerNames[actorId] = std::move(playerName);
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
				case ServerPacketType::PlayerSetProperty: {
					MemoryStream packet(data);
					PlayerPropertyType propertyType = (PlayerPropertyType)packet.ReadValue<std::uint8_t>();
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerSetProperty - received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
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
						case PlayerPropertyType::Freeze: {
							std::int32_t timeLeft = packet.ReadVariableInt32();
							InvokeAsync([this, timeLeft]() {
								if (!_players.empty()) {
									_players[0]->Freeze(float(timeLeft));
								}
							});
							break;
						}
						case PlayerPropertyType::Shield: {
							ShieldType shieldType = (ShieldType)packet.ReadValue<std::uint8_t>();
							std::int32_t timeLeft = packet.ReadVariableInt32();

							LOGD("[MP] ServerPacketType::PlayerSetProperty::Shield - shieldType: {}, timeLeft: {}", shieldType, timeLeft);

							InvokeAsync([this, shieldType, timeLeft]() {
								if (!_players.empty()) {
									_players[0]->SetShield(shieldType, float(timeLeft));
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
							LOGD("[MP] ServerPacketType::PlayerSetProperty - received unknown property {}", propertyType);
							break;
						}
					}
					return true;
				}
				case ServerPacketType::PlayerResetProperties: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerSetProperty - received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
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
						LOGD("[MP] ServerPacketType::PlayerRespawn - received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
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

					LOGD("[MP] ServerPacketType::PlayerMoveInstantly - playerIndex: {}, x: {}, y: {}, sx: {}, sy: {}",
						playerIndex, posX, posY, speedX, speedY);

					InvokeAsync([this, posX, posY, speedX, speedY]() {
						if (!_players.empty()) {
							auto* player = static_cast<RemotablePlayer*>(_players[0]);
							player->MoveRemotely(Vector2f(posX, posY), Vector2f(speedX, speedY));
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
						LOGD("[MP] ServerPacketType::PlayerEmitWeaponFlare - received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
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
						LOGD("[MP] ServerPacketType::PlayerChangeWeapon - received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
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
						LOGD("[MP] ServerPacketType::PlayerTakeDamage - received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
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
				case ServerPacketType::PlayerActivateSpring: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerActivateSpring - received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
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
						LOGD("[MP] ServerPacketType::PlayerWarpIn - received playerIndex {} instead of {}", playerIndex, _lastSpawnedActorId);
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
		if (_isServer) {
			LevelHandler::ProcessEvents(timeMult);
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

	void MpLevelHandler::SynchronizePeers()
	{
		for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
			if (peerDesc->LevelState == PeerLevelState::LevelLoaded) {
				if DEATH_LIKELY(peerDesc != nullptr && peerDesc->PreferredPlayerType != PlayerType::None) {
					peerDesc->LevelState = PeerLevelState::PlayerReady;
				} else {
					peerDesc->LevelState = PeerLevelState::LevelSynchronized;
				}

				LOGI("[MP] Syncing peer [{:.8x}]", std::uint64_t(peer._enet));

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

				// Synchronize actors
				for (Actors::Player* otherPlayer : _players) {
					auto* mpOtherPlayer = static_cast<MpPlayer*>(otherPlayer);
					auto otherPeerDesc = mpOtherPlayer->GetPeerDescriptor();

					String metadataPath = fs::FromNativeSeparators(mpOtherPlayer->_metadata->Path);

					MemoryStream packet;
					InitializeCreateRemoteActorPacket(packet, mpOtherPlayer->_playerIndex, mpOtherPlayer);

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
					
					MemoryStream packet2(5 + otherPeerDesc->PlayerName.size());
					packet2.WriteVariableUint32(mpOtherPlayer->_playerIndex);
					packet2.WriteValue<std::uint8_t>((std::uint8_t)otherPeerDesc->PlayerName.size());
					packet2.Write(otherPeerDesc->PlayerName.data(), (std::uint32_t)otherPeerDesc->PlayerName.size());

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet2);
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
				if (_enableSpawning && _activeBoss == nullptr) {
					peerDesc->LevelState = PeerLevelState::PlayerSpawned;

					const auto& serverConfig = _networkManager->GetServerConfiguration();
					Vector2f spawnPosition = (serverConfig.GameMode == MpGameMode::Cooperation && _lastCheckpointPos != Vector2f::Zero
						? _lastCheckpointPos : GetSpawnPoint(peerDesc->PreferredPlayerType));

					// TODO: Send ambient light (_lastCheckpointLight)

					std::uint8_t playerIndex = FindFreePlayerId();
					LOGI("[MP] Spawning player {} [{:.8x}]", playerIndex, std::uint64_t(peer._enet));

					std::shared_ptr<Actors::Multiplayer::RemotePlayerOnServer> player = std::make_shared<Actors::Multiplayer::RemotePlayerOnServer>(peerDesc);
					std::uint8_t playerParams[2] = { (std::uint8_t)peerDesc->PreferredPlayerType, (std::uint8_t)playerIndex };
					player->OnActivated(Actors::ActorActivationDetails(
						this,
						Vector3i((std::int32_t)spawnPosition.X, (std::int32_t)spawnPosition.Y, PlayerZ - playerIndex),
						playerParams
					));
					player->_controllableExternal = _controllableExternal;
					player->_health = (serverConfig.InitialPlayerHealth > 0
						? serverConfig.InitialPlayerHealth
						: (PlayerShouldHaveUnlimitedHealth(serverConfig.GameMode) ? INT32_MAX : 5));

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
						MemoryStream packet(5 + peerDesc->PlayerName.size());
						packet.WriteVariableUint32(playerIndex);
						packet.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
						packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());

						_networkManager->SendTo([this](const Peer& peer) {
							auto peerDesc = _networkManager->GetPeerDescriptor(peer);
							return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
						}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet);
					}

					if DEATH_UNLIKELY(_levelState == LevelState::WaitingForMinPlayers) {
						_waitingForPlayerCount = (std::int32_t)serverConfig.MinPlayerCount - (std::int32_t)_players.size();
						SendLevelStateToAllPlayers();
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

	bool MpLevelHandler::IsLocalPlayer(Actors::ActorBase* actor)
	{
		return (runtime_cast<Actors::Multiplayer::LocalPlayerOnServer>(actor) ||
				runtime_cast<Actors::Multiplayer::RemotablePlayer>(actor));
	}

	void MpLevelHandler::ApplyGameModeToAllPlayers(MpGameMode gameMode)
	{
		if (_levelState <= LevelState::Countdown1 || gameMode == MpGameMode::Cooperation) {
			// Everyone in single team
			for (auto* player : _players) {
				auto* mpPlayer = static_cast<MpPlayer*>(player);
				mpPlayer->GetPeerDescriptor()->Team = 0;
			}
		} else if (gameMode == MpGameMode::TeamBattle ||
				   gameMode == MpGameMode::CaptureTheFlag ||
				   gameMode == MpGameMode::TeamRace) {
			// Create two teams
			std::int32_t playerCount = (std::int32_t)_players.size();
			std::int32_t splitIdx = (playerCount + 1) / 2;
			SmallVector<std::int32_t, 0> teamIds(playerCount);
			for (std::int32_t i = 0; i < playerCount; i++) {
				teamIds.push_back(i);
			}
			Random().Shuffle(arrayView(teamIds));

			for (std::int32_t i = 0; i < playerCount; i++) {
				auto* mpPlayer = static_cast<MpPlayer*>(_players[i]);
				mpPlayer->GetPeerDescriptor()->Team = (teamIds[i] < splitIdx ? 0 : 1);
			}
		} else {
			// Each player is in their own team
			std::int32_t playerCount = (std::int32_t)_players.size();
			for (std::int32_t i = 0; i < playerCount; i++) {
				std::int32_t playerIdx = _players[i]->_playerIndex;
				DEATH_DEBUG_ASSERT(0 <= playerIdx && playerIdx < UINT8_MAX);
				auto* mpPlayer = static_cast<MpPlayer*>(_players[i]);
				mpPlayer->GetPeerDescriptor()->Team = (std::uint8_t)playerIdx;
			}
		}
	}

	void MpLevelHandler::ApplyGameModeToPlayer(MpGameMode gameMode, Actors::Player* player)
	{
		auto* mpPlayer = static_cast<MpPlayer*>(player);

		if (gameMode == MpGameMode::Cooperation) {
			// Everyone in single team
			mpPlayer->GetPeerDescriptor()->Team = 0;
		} else if (gameMode == MpGameMode::TeamBattle ||
				   gameMode == MpGameMode::TeamRace || 
				   gameMode == MpGameMode::TeamTreasureHunt ||
				   gameMode == MpGameMode::CaptureTheFlag) {
			// Create two teams
			std::int32_t playerCountsInTeam[2] = {};
			std::int32_t playerCount = (std::int32_t)_players.size();
			for (std::int32_t i = 0; i < playerCount; i++) {
				if (_players[i] != player) {
					auto* otherPlayer = static_cast<MpPlayer*>(_players[i]);

					std::uint8_t teamId = otherPlayer->GetPeerDescriptor()->Team;
					if (teamId < arraySize(playerCountsInTeam)) {
						playerCountsInTeam[teamId]++;
					}
				}
			}
			std::uint8_t teamId = (playerCountsInTeam[0] < playerCountsInTeam[1] ? 0
				: (playerCountsInTeam[0] > playerCountsInTeam[1] ? 1
					: Random().Next(0, 2)));

			mpPlayer->GetPeerDescriptor()->Team = teamId;
		} else {
			// Each player is in their own team
			std::int32_t playerIdx = mpPlayer->_playerIndex;
			DEATH_DEBUG_ASSERT(0 <= playerIdx && playerIdx < UINT8_MAX);
			mpPlayer->GetPeerDescriptor()->Team = (std::uint8_t)playerIdx;
		}
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
	}

	Vector2f MpLevelHandler::GetSpawnPoint(PlayerType playerType)
	{
		if (!_multiplayerSpawnPoints.empty()) {
			// TODO: Select spawn according to team
			return _multiplayerSpawnPoints[Random().Next(0, _multiplayerSpawnPoints.size())].Pos;
		} else {
			Vector2f spawnPosition = EventMap()->GetSpawnPosition(playerType);
			if (spawnPosition.X >= 0.0f && spawnPosition.Y >= 0.0f) {
				return spawnPosition;
			}
			return EventMap()->GetSpawnPosition(PlayerType::Jazz);
		}
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

	void MpLevelHandler::WarpAllPlayersToStart()
	{
		// TODO: Reset ambient lighting
		for (auto* player : _players) {
			Vector2f spawnPosition = GetSpawnPoint(player->_playerTypeOriginal);
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
					float nearestCheckpoint = FLT_MAX;
					for (auto checkpointPos : _raceCheckpoints) {
						float length = (pos - Vector2f(checkpointPos.X * Tiles::TileSet::DefaultTileSize, checkpointPos.Y * Tiles::TileSet::DefaultTileSize)).Length();
						if (nearestCheckpoint > length) {
							nearestCheckpoint = length;
						}
					}

					roundPoints = nearestCheckpoint + (serverConfig.TotalLaps - peerDesc->Laps) * (UINT32_MAX / 100);
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
		if DEATH_UNLIKELY(_levelState != LevelState::Running) {
			return;
		}

		CalculatePositionInRound();

		auto& serverConfig = _networkManager->GetServerConfiguration();

		if (serverConfig.GameMode != MpGameMode::Cooperation && serverConfig.Elimination) {
			std::int32_t eliminationCount = 0;
			MpPlayer* eliminationWinner = nullptr;

			for (auto* player : _players) {
				auto* mpPlayer = static_cast<MpPlayer*>(player);
				auto peerDesc = mpPlayer->GetPeerDescriptor();

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
		
		switch (serverConfig.GameMode) {
			case MpGameMode::Battle:
			case MpGameMode::TeamBattle: {
				if (!serverConfig.Elimination) {
					for (auto* player : _players) {
						auto* mpPlayer = static_cast<MpPlayer*>(player);
						auto peerDesc = mpPlayer->GetPeerDescriptor();

						if (peerDesc->Kills >= serverConfig.TotalKills) {
							EndGame(mpPlayer);
							break;
						}
					}
				}
				break;
			}

			case MpGameMode::Race:
			case MpGameMode::TeamRace: {
				for (auto* player : _players) {
					auto* mpPlayer = static_cast<MpPlayer*>(player);
					auto peerDesc = mpPlayer->GetPeerDescriptor();

					if (peerDesc->Laps >= serverConfig.TotalLaps) {
						EndGame(mpPlayer);
						break;
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
			LOGI("[MP] Winner is {}", peerDesc->PlayerName);
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

	void MpLevelHandler::EndGameOnTimeOut()
	{
		MpPlayer* winner = nullptr;

		auto& serverConfig = _networkManager->GetServerConfiguration();
		switch (serverConfig.GameMode) {
			case MpGameMode::Battle:
			case MpGameMode::TeamBattle: {
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

			case MpGameMode::Race:
			case MpGameMode::TeamRace: {
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
			
			case MpGameMode::TreasureHunt:
			case MpGameMode::TeamTreasureHunt: {
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
		serverConfig.Elimination = playlistEntry.Elimination;
		serverConfig.InitialPlayerHealth = playlistEntry.InitialPlayerHealth;
		serverConfig.MaxGameTimeSecs = playlistEntry.MaxGameTimeSecs;
		serverConfig.PreGameSecs = playlistEntry.PreGameSecs;
		serverConfig.TotalKills = playlistEntry.TotalKills;
		serverConfig.TotalLaps = playlistEntry.TotalLaps;
		serverConfig.TotalTreasureCollected = playlistEntry.TotalTreasureCollected;

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

	void MpLevelHandler::SetPlayerReady(PlayerType playerType)
	{
		if (_isServer) {
			return;
		}

		_inGameLobby->Hide();

		MemoryStream packet(2);
		packet.WriteValue<std::uint8_t>((std::uint8_t)playerType);
		// TODO: Preferred team
		packet.WriteValue<std::uint8_t>(0);

		_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerReady, packet);
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

		packet.ReserveCapacity(28 + _levelName.size());
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

	/*void MpLevelHandler::UpdatePlayerLocalPos(Actors::Player* player, PlayerState& playerState, float timeMult)
	{
		if (playerState.WarpTimeLeft > 0.0f || !player->_controllable || !player->GetState(Actors::ActorState::CollideWithTileset)) {
			// Don't interpolate if warping is in progress or if collisions with tileset are disabled (when climbing or in tube)
			return;
		}

		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();
		std::int64_t renderTime = now - ServerDelay;

		std::int32_t nextIdx = playerState.StateBufferPos - 1;
		if (nextIdx < 0) {
			nextIdx += arraySize(playerState.StateBuffer);
		}

		if (renderTime <= playerState.StateBuffer[nextIdx].Time) {
			std::int32_t prevIdx;
			while (true) {
				prevIdx = nextIdx - 1;
				if (prevIdx < 0) {
					prevIdx += arraySize(playerState.StateBuffer);
				}

				if (prevIdx == playerState.StateBufferPos || playerState.StateBuffer[prevIdx].Time <= renderTime) {
					break;
				}

				nextIdx = prevIdx;
			}

			Vector2f pos;
			Vector2f speed;
			std::int64_t timeRange = (playerState.StateBuffer[nextIdx].Time - playerState.StateBuffer[prevIdx].Time);
			if (timeRange > 0) {
				float lerp = (float)(renderTime - playerState.StateBuffer[prevIdx].Time) / timeRange;
				pos = playerState.StateBuffer[prevIdx].Pos + (playerState.StateBuffer[nextIdx].Pos - playerState.StateBuffer[prevIdx].Pos) * lerp;
				speed = playerState.StateBuffer[prevIdx].Speed + (playerState.StateBuffer[nextIdx].Speed - playerState.StateBuffer[prevIdx].Speed) * lerp;
			} else {
				pos = playerState.StateBuffer[nextIdx].Pos;
				speed = playerState.StateBuffer[nextIdx].Speed;
			}

			constexpr float BaseDeviation = 2.0f;
			constexpr float DeviationTimeMax = 90.0f;
			constexpr float DeviationTimeCutoff = 40.0f;

			float devSqr = (player->_pos - pos).SqrLength();
			float speedSqr = std::max(player->_speed.SqrLength(), speed.SqrLength());
			if (devSqr > speedSqr + (BaseDeviation * BaseDeviation)) {
				playerState.DeviationTime += timeMult;
				if (playerState.DeviationTime > DeviationTimeMax) {
					LOGW("Deviation of player {} was high for too long (deviation: {:.2f}px, speed: {:.2f})", player->_playerIndex, sqrt(devSqr), sqrt(speedSqr));
					playerState.DeviationTime = 0.0f;
					player->MoveInstantly(pos, Actors::MoveType::Absolute);
					return;
				}
			} else {
				playerState.DeviationTime = 0.0f;
			}

			float alpha = std::clamp(playerState.DeviationTime - DeviationTimeCutoff, 0.0f, DeviationTimeMax - DeviationTimeCutoff) / (DeviationTimeMax - DeviationTimeCutoff);
			if (alpha > 0.01f) {
				player->MoveInstantly(Vector2f(lerp(player->_pos.X, pos.X, alpha), lerp(player->_pos.Y, pos.Y, alpha)), Actors::MoveType::Absolute);
				player->_speed = Vector2f(lerp(player->_speed.X, speed.X, alpha), lerp(player->_speed.Y, speed.Y, alpha));
				LOGW("Deviation of player {} is high (alpha: {:.1f}, deviation: {:.2f}px, speed: {:.2f})", player->_playerIndex, alpha, sqrt(devSqr), sqrt(speedSqr));
			}
		}
	}*/

	/*void MpLevelHandler::OnRemotePlayerPosReceived(PlayerState& playerState, Vector2f pos, const Vector2f speed, PlayerFlags flags)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();

		if ((flags & PlayerFlags::JustWarped) != PlayerFlags::JustWarped) {
			// Player is still visible, enable interpolation
			playerState.StateBuffer[playerState.StateBufferPos].Time = now;
			playerState.StateBuffer[playerState.StateBufferPos].Pos = pos;
			playerState.StateBuffer[playerState.StateBufferPos].Speed = speed;
		} else {
			// Player just warped, reset state buffer to disable interpolation
			std::int32_t stateBufferPrevPos = playerState.StateBufferPos - 1;
			if (stateBufferPrevPos < 0) {
				stateBufferPrevPos += arraySize(playerState.StateBuffer);
			}

			std::int64_t renderTime = now - ServerDelay;

			playerState.StateBuffer[stateBufferPrevPos].Time = renderTime;
			playerState.StateBuffer[stateBufferPrevPos].Pos = pos;
			playerState.StateBuffer[stateBufferPrevPos].Speed = speed;
			playerState.StateBuffer[playerState.StateBufferPos].Time = renderTime;
			playerState.StateBuffer[playerState.StateBufferPos].Pos = pos;
			playerState.StateBuffer[playerState.StateBufferPos].Speed = speed;
		}

		playerState.StateBufferPos++;
		if (playerState.StateBufferPos >= std::int32_t(arraySize(playerState.StateBuffer))) {
			playerState.StateBufferPos = 0;
		}

		playerState.Flags = (flags & ~PlayerFlags::JustWarped);
	}*/

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
		if (ImGui::BeginTable("peers", 8, flags, ImVec2(0.0f, 0.0f))) {
			ImGui::TableSetupColumn("Peer");
			ImGui::TableSetupColumn("Player Index");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Last Updated");
			ImGui::TableSetupColumn("X");
			ImGui::TableSetupColumn("Y");
			ImGui::TableSetupColumn("Flags");
			ImGui::TableSetupColumn("Pressed");
			ImGui::TableHeadersRow();
			
			for (auto& [peer, desc] : *_networkManager->GetPeers()) {
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("0x%08x", peer._enet);

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", desc->Player != nullptr ? desc->Player->GetPlayerIndex() : -1);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("0x%x", desc->LevelState);

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", desc->LastUpdated);

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.2f", desc->Player != nullptr ? desc->Player->GetPos().X : -1.0f);

				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.2f", desc->Player != nullptr ? desc->Player->GetPos().Y : -1.0f);

				if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer>(desc->Player)) {
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("0x%02x", remotePlayerOnServer->Flags);

					ImGui::TableSetColumnIndex(7);
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