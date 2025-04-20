#include "MpLevelHandler.h"

#if defined(WITH_MULTIPLAYER)

#include "PacketTypes.h"
#include "../PreferencesCache.h"
#include "../UI/InGameConsole.h"
#include "../UI/Multiplayer/MpHUD.h"
#include "../UI/Multiplayer/MpInGameCanvasLayer.h"
#include "../UI/Multiplayer/MpInGameLobby.h"
#include "../../Main.h"

#if defined(WITH_ANGELSCRIPT)
#	include "../Scripting/LevelScriptLoader.h"
#endif

#include "../../nCine/Application.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Primitives/Half.h"

#include "../Actors/Player.h"
#include "../Actors/Multiplayer/LocalPlayerOnServer.h"
#include "../Actors/Multiplayer/RemotablePlayer.h"
#include "../Actors/Multiplayer/RemotePlayerOnServer.h"
#include "../Actors/Multiplayer/RemoteActor.h"

#include "../Actors/Environment/AirboardGenerator.h"
#include "../Actors/Environment/SteamNote.h"
#include "../Actors/Environment/SwingingVine.h"
#include "../Actors/Solid/Bridge.h"
#include "../Actors/Solid/MovingPlatform.h"
#include "../Actors/Solid/PinballBumper.h"
#include "../Actors/Solid/PinballPaddle.h"
#include "../Actors/Solid/SpikeBall.h"
#include "../Actors/Weapons/ElectroShot.h"
#include "../Actors/Weapons/ShotBase.h"
#include "../Actors/Weapons/TNT.h"

#include <float.h>

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
			_levelState(LevelState::InitialUpdatePending), _enableSpawning(true), _lastSpawnedActorId(-1), _waitingForPlayerCount(0),
			_seqNum(0), _seqNumWarped(0), _suppressRemoting(false), _ignorePackets(false), _enableLedgeClimb(enableLedgeClimb),
			_controllableExternal(true)
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

		if (_isServer) {
			// Reserve first 255 indices for players
			_lastSpawnedActorId = UINT8_MAX;

			for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
				peerDesc->LevelState = PeerLevelState::Unknown;
				peerDesc->LastUpdated = 0;
				if (peerDesc->RemotePeer) {
					peerDesc->Player = nullptr;
				}
			}

			auto& serverConfig = _networkManager->GetServerConfiguration();

			std::uint32_t flags = 0;
			if (PreferencesCache::EnableReforgedGameplay) {
				flags |= 0x01;
			}
			if (PreferencesCache::EnableLedgeClimb) {
				flags |= 0x02;
			}
			if (serverConfig.IsElimination) {
				flags |= 0x04;
			}

			MemoryStream packet(28 + _levelName.size());
			packet.WriteVariableUint32(flags);
			packet.WriteValue<std::uint8_t>((std::uint8_t)_levelState);
			packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.GameMode);
			packet.WriteValue<std::uint8_t>((std::uint8_t)levelInit.LastExitType);
			packet.WriteVariableUint32(_levelName.size());
			packet.Write(_levelName.data(), _levelName.size());
			packet.WriteVariableUint32(serverConfig.InitialPlayerHealth);
			packet.WriteVariableUint32(serverConfig.MaxGameTimeSecs);
			packet.WriteVariableUint32(serverConfig.TotalKills);
			packet.WriteVariableUint32(serverConfig.TotalLaps);
			packet.WriteVariableUint32(serverConfig.TotalTreasureCollected);

			_networkManager->SendTo([this](const Peer& peer) {
				auto peerDesc = _networkManager->GetPeerDescriptor(peer);
				return (peerDesc && peerDesc->IsAuthenticated);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LoadLevel, packet);
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

	bool MpLevelHandler::IsPausable() const
	{
		return false;
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
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(player)) {
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

		// Update last pressed keys only if it wasn't done this frame yet (because of PlayerKeyPress packet)
		for (auto* player : _players) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(player)) {
				if (remotePlayerOnServer->UpdatedFrame != frameCount) {
					remotePlayerOnServer->UpdatedFrame = frameCount;
					remotePlayerOnServer->PressedKeysLast = remotePlayerOnServer->PressedKeys;
				}
			}
		}

		_updateTimeLeft -= timeMult;
		_gameTimeLeft -= timeMult;

		if (_updateTimeLeft < 0.0f) {
			_updateTimeLeft = FrameTimer::FramesPerSecond / UpdatesPerSecond;

			switch (_levelState) {
				case LevelState::InitialUpdatePending: {
					LOGD("[MP] Level \"%s\" is ready", _levelName.data());

					if (_isServer) {
						auto& serverConfig = _networkManager->GetServerConfiguration();
						if (serverConfig.GameMode == MpGameMode::Cooperation) {
							// Skip pre-game and countdown in cooperation
							_levelState = LevelState::Running;
						} else {
							_levelState = LevelState::PreGame;
							_gameTimeLeft = serverConfig.PreGameSecs * FrameTimer::FramesPerSecond;
							ShowAlertToAllPlayers(_("\n\nThe game will begin shortly!"));
						}
						SendLevelStateToAllPlayers();
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
						auto& serverConfig = _networkManager->GetServerConfiguration();
						// TODO: Check all players are ready
						if (_players.size() >= serverConfig.MinPlayerCount) {
							_levelState = LevelState::Countdown3;
							_gameTimeLeft = FrameTimer::FramesPerSecond;
							SetControllableToAllPlayers(false);
							WarpAllPlayersToStart();
							ResetAllPlayerStats();
							static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(3);
							// TODO: Respawn all events and tilemap
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
							ResetAllPlayerStats();
							static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(3);
							SendLevelStateToAllPlayers();
							// TODO: Respawn all events and tilemap
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
						auto& serverConfig = _networkManager->GetServerConfiguration();
						_gameTimeLeft = serverConfig.MaxGameTimeSecs * FrameTimer::FramesPerSecond;

						SetControllableToAllPlayers(true);
						static_cast<UI::Multiplayer::MpHUD*>(_hud.get())->ShowCountdown(0);
						SendLevelStateToAllPlayers();

						for (auto* player : _players) {
							auto* mpPlayer = static_cast<MpPlayer*>(player);
							auto peerDesc = mpPlayer->GetPeerDescriptor();
							mpPlayer->GetPeerDescriptor()->LapStarted = TimeStamp::now();

							// The player is invulnerable for a short time after starting a round
							player->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Transient);

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
						auto& serverConfig = _networkManager->GetServerConfiguration();
						if (serverConfig.GameMode != MpGameMode::Cooperation && serverConfig.MaxGameTimeSecs > 0 && _gameTimeLeft <= 0.0f) {
							EndGameOnTimeOut();
						}
					}
					break;
				}
				case LevelState::Ending: {
					if (_isServer && _gameTimeLeft <= 0.0f) {
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
					: !runtime_cast<Actors::Multiplayer::RemoteActor*>(actor.second));

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

					MemoryStream packet(4 + actorCount * 24);
					packet.WriteVariableUint32(actorCount);

					for (Actors::Player* player : _players) {
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
						packet.WriteValue<std::uint8_t>((std::uint8_t)rendererType);
					}

					// TODO: Does this need to be locked?
					{
						std::unique_lock lock(_lock);
						for (auto& [remotingActor, remotingActorInfo] : _remotingActors) {
							packet.WriteVariableUint32(remotingActorInfo.ActorID);

							std::int32_t newPosX = (std::int32_t)(remotingActor->_pos.X * 512.0f);
							std::int32_t newPosY = (std::int32_t)(remotingActor->_pos.Y * 512.0f);
							bool positionChanged = (newPosX != remotingActorInfo.LastPosX || newPosY != remotingActorInfo.LastPosY);

							std::uint32_t newAnimation = (std::uint32_t)(remotingActor->_currentTransition != nullptr ? remotingActor->_currentTransition->State : (remotingActor->_currentAnimation != nullptr ? remotingActor->_currentAnimation->State : AnimState::Idle));
							float rotation = remotingActor->_renderer.rotation();
							if (rotation < 0.0f) rotation += fRadAngle360;
							std::uint16_t newRotation = (std::uint16_t)(rotation * UINT16_MAX / fRadAngle360);
							Vector2f newScale = remotingActor->_renderer.scale();
							std::uint16_t newScaleX = (std::uint16_t)Half { newScale.X };
							std::uint16_t newScaleY = (std::uint16_t)Half { newScale.Y };
							std::uint8_t newRendererType = (std::uint8_t)remotingActor->_renderer.GetRendererType();
							bool animationChanged = (newAnimation != remotingActorInfo.LastAnimation || newRotation != remotingActorInfo.LastRotation ||
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
					}, NetworkChannel::UnreliableUpdates, (std::uint8_t)ServerPacketType::UpdateAllActors, packetCompressed);

					SynchronizePeers();
				} else {
#if defined(DEATH_DEBUG)
					_debugAverageUpdatePacketSize = 0;
#endif
				}
			} else {
				if (!_players.empty()) {
					_seqNum++;

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

						formatString(actorIdString, arraySize(actorIdString), "%i [%0.1f] %04x", player->_playerIndex, it->second.DeviationTime, it->second.Flags);
					} else {
						formatString(actorIdString, arraySize(actorIdString), "%i", player->_playerIndex);
					}

					auto aabbMin = WorldPosToScreenSpace({ player->AABB.L, player->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}

				for (const auto& [actor, actorId] : _remotingActors) {
					char actorIdString[16];
					formatString(actorIdString, arraySize(actorIdString), "%u", actorId);

					auto aabbMin = WorldPosToScreenSpace({ actor->AABB.L, actor->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}
			} else {
				for (Actors::Player* player : _players) {
					char actorIdString[16];
					formatString(actorIdString, arraySize(actorIdString), "%i", player->_playerIndex);

					auto aabbMin = WorldPosToScreenSpace({ player->AABB.L, player->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}

				for (const auto& [actorId, actor] : _remoteActors) {
					char actorIdString[16];
					formatString(actorIdString, arraySize(actorIdString), "%u", actorId);

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
			packet.WriteValue<std::uint8_t>((std::uint8_t)UI::MessageLevel::Info);
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

	void MpLevelHandler::OnKeyPressed(const KeyboardEvent& event)
	{
		LevelHandler::OnKeyPressed(event);
	}

	void MpLevelHandler::OnKeyReleased(const KeyboardEvent& event)
	{
		LevelHandler::OnKeyReleased(event);
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
			// TODO: Player weapon SFX doesn't work
			std::uint32_t actorId; bool excludeSelf;
			if (auto* player = runtime_cast<Actors::Player*>(self)) {
				actorId = player->_playerIndex;
				excludeSelf = (!identifier.hasPrefix("EndOfLevel"_s) && !identifier.hasPrefix("Pickup"_s) && !identifier.hasPrefix("Weapon"_s));

				if (sourceRelative) {
					// Remote players don't have local viewport, so SFX cannot be relative to them
					if (auto* remotePlayer = runtime_cast<RemotePlayerOnServer*>(self)) {
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

	bool MpLevelHandler::IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider)
	{
		return LevelHandler::IsPositionEmpty(self, aabb, params, collider);
	}

	void MpLevelHandler::FindCollisionActorsByAABB(const Actors::ActorBase* self, const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback)
	{
		LevelHandler::FindCollisionActorsByAABB(self, aabb, std::move(callback));
	}

	void MpLevelHandler::FindCollisionActorsByRadius(float x, float y, float radius, Function<bool(Actors::ActorBase*)>&& callback)
	{
		LevelHandler::FindCollisionActorsByRadius(x, y, radius, std::move(callback));
	}

	void MpLevelHandler::GetCollidingPlayers(const AABBf& aabb, Function<bool(Actors::ActorBase*)>&& callback)
	{
		LevelHandler::GetCollidingPlayers(aabb, std::move(callback));
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
				auto peerDesc = mpPlayer->GetPeerDescriptor();
				peerDesc->Laps++;
				auto now = TimeStamp::now();
				peerDesc->LapsElapsedFrames += (now - peerDesc->LapStarted).secondsSince() * FrameTimer::FramesPerSecond;
				peerDesc->LapStarted = now;

				CheckGameEnds();

				Vector2f spawnPosition = GetSpawnPoint(mpPlayer->_playerTypeOriginal);
				mpPlayer->WarpToPosition(spawnPosition, WarpFlags::Default);
			}
			return;
		} else if (serverConfig.GameMode != MpGameMode::Cooperation) {
			// Ignore end of the level in all game modes except cooperation
			return;
		}

		LOGD("[MP] Changing level to \"%s\" (0x%02x)", nextLevel.data(), (std::uint32_t)exitType);

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

			bool canRespawn = (_enableSpawning && (_levelState != LevelState::Running || !serverConfig.IsElimination || peerDesc->Deaths < serverConfig.TotalKills));

			if (canRespawn && serverConfig.GameMode != MpGameMode::Cooperation) {
				mpPlayer->_checkpointPos = GetSpawnPoint(peerDesc->PreferredPlayerType);

				// The player is invulnerable for a short time after respawning
				mpPlayer->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Transient);
			}

			if (_levelState != LevelState::Running) {
				if (peerDesc->RemotePeer) {
					MemoryStream packet2(12);
					packet2.WriteVariableUint32(mpPlayer->_playerIndex);
					packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.X * 512.0f));
					packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.Y * 512.0f));
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet2);
				}
				return canRespawn;
			}

			peerDesc->Deaths++;
			if (serverConfig.IsElimination && peerDesc->Deaths >= serverConfig.TotalKills) {
				peerDesc->DeathElapsedFrames = _elapsedFrames;
			}

			if (peerDesc->RemotePeer) {
				MemoryStream packet1(9);
				packet1.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Deaths);
				packet1.WriteVariableUint32(mpPlayer->_playerIndex);
				packet1.WriteVariableUint32(peerDesc->Deaths);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet1);

				if (canRespawn) {
					MemoryStream packet2(12);
					packet2.WriteVariableUint32(mpPlayer->_playerIndex);
					packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.X * 512.0f));
					packet2.WriteValue<std::int32_t>((std::int32_t)(mpPlayer->_checkpointPos.Y * 512.0f));
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet2);
				}
			}

			if (auto* attacker = GetWeaponOwner(mpPlayer->_lastAttacker.get())) {
				auto attackerPeerDesc = attacker->GetPeerDescriptor();
				attackerPeerDesc->Kills++;

				if(attackerPeerDesc->RemotePeer) {
					MemoryStream packet3(9);
					packet3.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Kills);
					packet3.WriteVariableUint32(attacker->_playerIndex);
					packet3.WriteVariableUint32(attackerPeerDesc->Kills);
					_networkManager->SendTo(attackerPeerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet3);
				}

				_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]%s\f[/c] was roasted by \f[c:#d0705d]%s\f[/c]",
					peerDesc->PlayerName.data(), attackerPeerDesc->PlayerName.data()));

				MemoryStream packet(19 + peerDesc->PlayerName.size() + attackerPeerDesc->PlayerName.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)PeerPropertyType::Roasted);
				packet.WriteVariableUint64((std::uint64_t)peerDesc->RemotePeer._enet);
				packet.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
				packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());
				packet.WriteVariableUint64((std::uint64_t)attackerPeerDesc->RemotePeer._enet);
				packet.WriteValue<std::uint8_t>((std::uint8_t)attackerPeerDesc->PlayerName.size());
				packet.Write(attackerPeerDesc->PlayerName.data(), (std::uint32_t)attackerPeerDesc->PlayerName.size());

				_networkManager->SendTo([this](const Peer& peer) {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					return (peerDesc && peerDesc->IsAuthenticated);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PeerSetProperty, packet);
			} else {
				_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]%s\f[/c] was roasted by environment",
					peerDesc->PlayerName.data()));

				MemoryStream packet(19 + peerDesc->PlayerName.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)PeerPropertyType::Roasted);
				packet.WriteVariableUint64((std::uint64_t)peerDesc->RemotePeer._enet);
				packet.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
				packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());
				packet.WriteVariableUint64(0);
				packet.WriteValue<std::uint8_t>(0);

				_networkManager->SendTo([this](const Peer& peer) {
					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					return (peerDesc && peerDesc->IsAuthenticated);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PeerSetProperty, packet);
			}

			CheckGameEnds();

			return canRespawn;
		} else {
			auto* mpPlayer = static_cast<RemotablePlayer*>(player);
			if (mpPlayer->RespawnPending) {
				mpPlayer->RespawnPending = false;
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
			if ((flags & WarpFlags::Fast) == WarpFlags::Fast) {
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

	void MpLevelHandler::HandlePlayerTakeDamage(Actors::Player* player, std::int32_t amount, float pushForce)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(10);
				packet.WriteVariableUint32(player->_playerIndex);
				packet.WriteVariableInt32(player->_health);
				packet.WriteValue<std::int16_t>((std::int16_t)(pushForce * 512.0f));
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerTakeDamage, packet);
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
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
				packet.WriteValue<std::uint16_t>((std::uint16_t)player->_weaponAmmo[(std::uint8_t)player->_currentWeapon]);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerSetDizzyTime(Actors::Player* player, float timeLeft)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::DizzyTime);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
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
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_weaponUpgrades[(std::uint8_t)player->_currentWeapon]);
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

	void MpLevelHandler::HandlePlayerWeaponChanged(Actors::Player* player)
	{
		if (_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(5);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)mpPlayer->_currentWeapon);
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
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if ((flags & WarpFlags::IncrementLaps) == WarpFlags::IncrementLaps && _levelState == LevelState::Running) {
				peerDesc->Laps++;
				auto now = TimeStamp::now();
				peerDesc->LapsElapsedFrames += (now - peerDesc->LapStarted).secondsSince() * FrameTimer::FramesPerSecond;
				peerDesc->LapStarted = now;

				CheckGameEnds();
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
		}
	}

	void MpLevelHandler::HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount)
	{
		LevelHandler::HandlePlayerCoins(player, prevCount, newCount);

		if (_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->RemotePeer) {
				MemoryStream packet(9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Coins);
				packet.WriteVariableUint32(mpPlayer->_playerIndex);
				packet.WriteVariableInt32(newCount);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
			}
		}
	}

	void MpLevelHandler::HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount)
	{
		LevelHandler::HandlePlayerGems(player, gemType, prevCount, newCount);

		if (_isServer) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (newCount > prevCount && _levelState == LevelState::Running) {
				std::int32_t weightedCount;
				switch (gemType) {
					default:
					case 0: // Red (+1)
						weightedCount = 1;
						break;
					case 1: // Green (+5)
						weightedCount = 5;
						break;
					case 2: // Blue (+10)
						weightedCount = 10;
						break;
					case 3: // Purple
						weightedCount = 1;
						break;
				}

				peerDesc->TreasureCollected += (newCount - prevCount) * weightedCount;
			}

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

			CheckGameEnds();
		}
	}

	void MpLevelHandler::SetCheckpoint(Actors::Player* player, Vector2f pos)
	{
		LevelHandler::SetCheckpoint(player, pos);
	}

	void MpLevelHandler::RollbackToCheckpoint(Actors::Player* player)
	{
		LevelHandler::RollbackToCheckpoint(player);
	}

	void MpLevelHandler::HandleActivateSugarRush(Actors::Player* player)
	{
		LevelHandler::HandleActivateSugarRush(player);
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

	bool MpLevelHandler::PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads)
	{
		// TODO: Remove this override
		return LevelHandler::PlayerActionPressed(player, action, includeGamepads);
	}

	bool MpLevelHandler::PlayerActionPressed(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad)
	{
		if (_isServer) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(player)) {
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

	bool MpLevelHandler::PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads)
	{
		// TODO: Remove this override
		return LevelHandler::PlayerActionHit(player, action, includeGamepads);
	}

	bool MpLevelHandler::PlayerActionHit(Actors::Player* player, PlayerAction action, bool includeGamepads, bool& isGamepad)
	{
		if (_isServer) {
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(player)) {
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
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(player)) {
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
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(player)) {
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
			if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(player)) {
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

	void MpLevelHandler::AttachComponents(LevelDescriptor&& descriptor)
	{
		LevelHandler::AttachComponents(std::move(descriptor));

		if (_isServer) {
			// Cache all possible multiplayer spawn points (if it's not coop level)
			_multiplayerSpawnPoints.clear();
			_eventMap->ForEachEvent(EventType::LevelStartMultiplayer, [this](const Events::EventMap::EventTile& event, std::int32_t x, std::int32_t y) {
				_multiplayerSpawnPoints.emplace_back(Vector2f(x * Tiles::TileSet::DefaultTileSize, y * Tiles::TileSet::DefaultTileSize - 8), event.EventParams[0]);
				return true;
			});
		} else {
			Vector2i size = _eventMap->GetSize();
			for (std::int32_t y = 0; y < size.Y; y++) {
				for (std::int32_t x = 0; x < size.X; x++) {
					std::uint8_t* eventParams;
					switch (_eventMap->GetEventByPosition(x, y, &eventParams)) {
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
							_eventMap->StoreTileEvent(x, y, EventType::Empty);
							break;
					}
				}
			}	
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

			peerDesc->LevelState = PeerLevelState::PlayerSpawned;
			peerDesc->LapStarted = TimeStamp::now();
			peerDesc->PlayerName = PreferencesCache::GetEffectivePlayerName();

			Actors::Multiplayer::LocalPlayerOnServer* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);
			AssignViewport(ptr);

			ptr->ReceiveLevelCarryOver(levelInit.LastExitType, levelInit.PlayerCarryOvers[i]);

			// The player is invulnerable for a short time after spawning
			ptr->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Transient);
		}

		ApplyGameModeToAllPlayers(serverConfig.GameMode);
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

		auto& serverConfig = _networkManager->GetServerConfiguration();
		serverConfig.GameMode = value;

		ApplyGameModeToAllPlayers(serverConfig.GameMode);

		std::uint8_t flags = 0;
		if (serverConfig.IsElimination) {
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
			packet.WriteVariableUint32(serverConfig.InitialPlayerHealth);
			packet.WriteVariableUint32(serverConfig.MaxGameTimeSecs);
			packet.WriteVariableUint32(serverConfig.TotalKills);
			packet.WriteVariableUint32(serverConfig.TotalLaps);
			packet.WriteVariableUint32(serverConfig.TotalTreasureCollected);

			_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LevelSetProperty, packet);
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

	MpPlayer* MpLevelHandler::GetWeaponOwner(Actors::ActorBase* actor)
	{
		if (auto* player = runtime_cast<MpPlayer*>(actor)) {
			return player;
		} else if (auto* shotBase = runtime_cast<Actors::Weapons::ShotBase*>(actor)) {
			return static_cast<MpPlayer*>(shotBase->GetOwner());
		} else if (auto* tnt = runtime_cast<Actors::Weapons::TNT*>(actor)) {
			return static_cast<MpPlayer*>(tnt->GetOwner());
		} else {
			return nullptr;
		}
	}

	bool MpLevelHandler::ProcessCommand(const Peer& peer, StringView line, bool isAdmin)
	{
		if (line == "/endpoints"_s) {
			if (isAdmin) {
				auto endpoints = _networkManager->GetServerEndpoints();
				for (const auto& endpoint : endpoints) {
					SendMessage(peer, UI::MessageLevel::Info, endpoint);
				}
				auto& serverConfig = _networkManager->GetServerConfiguration();
				StringView address; std::uint16_t port;
				if (NetworkManagerBase::TrySplitAddressAndPort(serverConfig.ServerAddressOverride, address, port)) {
					if (port == 0) {
						port = serverConfig.ServerPort;
					}
					char infoBuffer[128];
					formatString(infoBuffer, sizeof(infoBuffer), "%s:%u (Override)", String::nullTerminatedView(address).data(), port);
					SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
				}
				return true;
			}
		} else if (line == "/info"_s) {
			auto& serverConfig = _networkManager->GetServerConfiguration();

			char infoBuffer[128];
			formatString(infoBuffer, sizeof(infoBuffer), "Server: %s%s", serverConfig.ServerName.data(), serverConfig.IsPrivate ? " (Private)" : "");
			SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
			formatString(infoBuffer, sizeof(infoBuffer), "Current level: \"%s\" (%s%s)",
				_levelName.data(), NetworkManager::GameModeToString(serverConfig.GameMode).data(), serverConfig.IsElimination ? "/Elimination" : "");
			SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
			formatString(infoBuffer, sizeof(infoBuffer), "Players: %u/%u",
				(std::uint32_t)_networkManager->GetPeerCount(), serverConfig.MaxPlayerCount);
			SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
			if (!_players.empty()) {
				formatString(infoBuffer, sizeof(infoBuffer), "Server load: %.1f ms (%.1f)",
					(theApplication().GetFrameTimer().GetLastFrameDuration() * 1000.0f), theApplication().GetFrameTimer().GetAverageFps());
			} else {
				formatString(infoBuffer, sizeof(infoBuffer), "Server load: - ms");
			}
			SendMessage(peer, UI::MessageLevel::Info, infoBuffer);

			auto uptimeSecs = (DateTime::Now().ToUnixMilliseconds() / 1000) - (std::int64_t)serverConfig.StartUnixTimestamp;
			auto hours = (std::int32_t)(uptimeSecs / 3600);
			auto minutes = (std::int32_t)(uptimeSecs % 3600) / 60;
			auto seconds = (std::int32_t)(uptimeSecs % 60);
			formatString(infoBuffer, sizeof(infoBuffer), "Uptime: %d:%02d:%02d", hours, minutes, seconds);
			SendMessage(peer, UI::MessageLevel::Info, infoBuffer);

			if (isAdmin) {
				formatString(infoBuffer, sizeof(infoBuffer), "Config Path: \"%s\"", serverConfig.FilePath.data());
				SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
			}

			if (!serverConfig.Playlist.empty()) {
				formatString(infoBuffer, sizeof(infoBuffer), "Playlist: %u/%u%s",
					(std::uint32_t)(serverConfig.PlaylistIndex + 1), (std::uint32_t)serverConfig.Playlist.size(), serverConfig.RandomizePlaylist ? " (Random)" : "");
				SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
			}
			return true;
		} else if (line.hasPrefix("/kick "_s)) {
			if (isAdmin) {
				// TODO: Implement /kick
			}
		} else if (line == "/players"_s) {
			auto& serverConfig = _networkManager->GetServerConfiguration();
			SendMessage(peer, UI::MessageLevel::Info, "List of connected players:"_s);

			char infoBuffer[128];
			for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
				if (!peerDesc->RemotePeer && !peerDesc->Player) {
					continue;
				}

				if (serverConfig.GameMode == MpGameMode::Race || serverConfig.GameMode == MpGameMode::Race) {
					formatString(infoBuffer, sizeof(infoBuffer), "%u. %s - Points: %u - Kills: %u - Deaths: %u - Laps: %u/%u",
						peerDesc->PositionInRound, peerDesc->PlayerName.data(), peerDesc->Points, peerDesc->Kills, peerDesc->Deaths, peerDesc->Laps + 1, serverConfig.TotalLaps);
				} else if (serverConfig.GameMode == MpGameMode::TreasureHunt || serverConfig.GameMode == MpGameMode::TeamTreasureHunt) {
					formatString(infoBuffer, sizeof(infoBuffer), "%u. %s - Points: %u - Kills: %u - Deaths: %u - Treasure: %u",
						peerDesc->PositionInRound, peerDesc->PlayerName.data(), peerDesc->Points, peerDesc->Kills, peerDesc->Deaths, peerDesc->TreasureCollected);
				} else {
					formatString(infoBuffer, sizeof(infoBuffer), "%u. %s - Points: %u - Kills: %u - Deaths: %u",
						peerDesc->PositionInRound, peerDesc->PlayerName.data(), peerDesc->Points, peerDesc->Kills, peerDesc->Deaths);
				}
				SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
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
						char infoBuffer[128];
						formatString(infoBuffer, sizeof(infoBuffer), "Game mode set to \f[w:80]\f[c:#707070]%s\f[/c]\f[/w]",
							NetworkManager::GameModeToString(gameMode).data());
						SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
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
						LOGD("[MP] Changing level to \"%s\"", levelInit.LevelName.data());

						auto& serverConfig = _networkManager->GetServerConfiguration();
						serverConfig.PlaylistIndex = -1;

						levelInit.LastExitType = ExitType::Normal;
						HandleLevelChange(std::move(levelInit));
					} else {
						LOGD("[MP] Level \"%s\" doesn't exist", levelInit.LevelName.data());
					}
				} else if (variableName == "welcome"_s) {
					auto& serverConfig = _networkManager->GetServerConfiguration();
					SetWelcomeMessage(StringUtils::replaceAll(value.trimmed(), "\\n"_s, "\n"_s));
					SendMessage(peer, UI::MessageLevel::Info, "Lobby message changed");
					return true;
				} else if (variableName == "name"_s) {
					auto& serverConfig = _networkManager->GetServerConfiguration();

					serverConfig.ServerName = value.trimmed();

					char infoBuffer[128];
					if (!serverConfig.ServerName.empty()) {
						formatString(infoBuffer, sizeof(infoBuffer), "Server name set to \f[w:80]\f[c:#707070]%s\f[/c]\f[/w]", serverConfig.ServerName.data());
					} else if (!serverConfig.IsPrivate) {
						formatString(infoBuffer, sizeof(infoBuffer), "Server visibility to \f[w:80]\f[c:#707070]hidden\f[/c]\f[/w]");
					}
					SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
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

					char infoBuffer[128];
					formatString(infoBuffer, sizeof(infoBuffer), "Spawning set to \f[w:80]\f[c:#707070]%s\f[/c]\f[/w]", _enableSpawning ? "Enabled" : "Disabled");
					SendMessage(peer, UI::MessageLevel::Info, infoBuffer);
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
				if (serverConfig.GameMode != prevGameMode) {
					SetGameMode(serverConfig.GameMode);
				}
				SendMessage(peer, UI::MessageLevel::Info, "Server configuration reloaded"_s);
				return true;
			}
		} else if (line == "/reset points"_s) {
			if (isAdmin) {
				for (auto& [playerPeer, peerDesc] : *_networkManager->GetPeers()) {
					peerDesc->Points = 0;

					if (peerDesc->RemotePeer) {
						MemoryStream packet(9);
						packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Points);
						packet.WriteVariableUint32(peerDesc->Player->_playerIndex);
						packet.WriteVariableUint32(peerDesc->Points);
						_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
					}
				}
				SendMessage(peer, UI::MessageLevel::Info, "All points reset"_s);
				return true;
			}
		} else if (line.hasPrefix("/alert "_s)) {
			if (isAdmin) {
				StringView message = line.exceptPrefix("/alert "_s).trimmed();
				ShowAlertToAllPlayers(message);
				return true;
			}
		} else if (line == "/skip"_s) {
			if (isAdmin) {
				auto& serverConfig = _networkManager->GetServerConfiguration();
				if (_levelState == LevelState::PreGame && _players.size() >= serverConfig.MinPlayerCount) {
					_gameTimeLeft = 0.0f;
				}
				return true;
			}
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
				return true;
			}
		}

		return false;
	}

	void MpLevelHandler::SendMessage(const Peer& peer, UI::MessageLevel level, StringView message)
	{
		if (!peer.IsValid()) {
			if (ContentResolver::Get().IsHeadless()) {
				switch (level) {
					default: DEATH_TRACE(TraceLevel::Info, {}, "[<] %s", message.data()); break;
					case UI::MessageLevel::Echo: DEATH_TRACE(TraceLevel::Info, {}, "[>] %s", message.data()); break;
					case UI::MessageLevel::Warning: DEATH_TRACE(TraceLevel::Warning, {}, "[<] %s", message.data()); break;
					case UI::MessageLevel::Error: DEATH_TRACE(TraceLevel::Error, {}, "[<] %s", message.data()); break;
					case UI::MessageLevel::Assert: DEATH_TRACE(TraceLevel::Assert, {}, "[<] %s", message.data()); break;
					case UI::MessageLevel::Fatal: DEATH_TRACE(TraceLevel::Fatal, {}, "[<] %s", message.data()); break;
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

	bool MpLevelHandler::OnPeerDisconnected(const Peer& peer)
	{
		if (_isServer) {
			if (auto peerDesc = _networkManager->GetPeerDescriptor(peer)) {
				_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]%s\f[/c] disconnected", peerDesc->PlayerName.data()));

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

					for (std::size_t i = 0; i < _players.size(); i++) {
						if (_players[i] == player) {
							_players.eraseUnordered(i);
							break;
						}
					}

					player->SetState(Actors::ActorState::IsDestroyed, true);

					MemoryStream packet(4);
					packet.WriteVariableUint32(playerIndex);

					_networkManager->SendTo([otherPeer = peer](const Peer& peer) {
						return (peer != otherPeer);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::DestroyRemoteActor, packet);

					if (_levelState == LevelState::WaitingForMinPlayers) {
						auto& serverConfig = _networkManager->GetServerConfiguration();
						_waitingForPlayerCount = (std::int32_t)serverConfig.MinPlayerCount - (std::int32_t)_players.size();
						_root->InvokeAsync([this]() {
							SendLevelStateToAllPlayers();
						}, NCINE_CURRENT_FUNCTION);
					}
				}
			}
			return true;
		}

		return false;
	}

	bool MpLevelHandler::OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t packetType, ArrayView<const std::uint8_t> data)
	{
		if (_ignorePackets) {
			// IRootController is probably going to load a new level in a moment, so ignore all packets now
			return false;
		}

		if (_isServer) {
			switch ((ClientPacketType)packetType) {
				case ClientPacketType::Auth: {
					auto& serverConfig = _networkManager->GetServerConfiguration();

					if (auto peerDesc = _networkManager->GetPeerDescriptor(peer)) {
						_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]%s\f[/c] connected", peerDesc->PlayerName.data()));

						MemoryStream packet(10 + peerDesc->PlayerName.size());
						packet.WriteValue<std::uint8_t>((std::uint8_t)PeerPropertyType::Connected);
						packet.WriteVariableUint64((std::uint64_t)peer._enet);
						packet.WriteValue<std::uint8_t>((std::uint8_t)peerDesc->PlayerName.size());
						packet.Write(peerDesc->PlayerName.data(), (std::uint32_t)peerDesc->PlayerName.size());

						_networkManager->SendTo([otherPeer = peer](const Peer& peer) {
							return (peer != otherPeer);
						}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PeerSetProperty, packet);
					}

					std::uint32_t flags = 0;
					if (PreferencesCache::EnableReforgedGameplay) {
						flags |= 0x01;
					}
					if (PreferencesCache::EnableLedgeClimb) {
						flags |= 0x02;
					}
					if (serverConfig.IsElimination) {
						flags |= 0x04;
					}

					MemoryStream packet(28 + _levelName.size());
					packet.WriteVariableUint32(flags);
					packet.WriteValue<std::uint8_t>((std::uint8_t)_levelState);
					packet.WriteValue<std::uint8_t>((std::uint8_t)serverConfig.GameMode);
					packet.WriteValue<std::uint8_t>((std::uint8_t)ExitType::None);
					packet.WriteVariableUint32(_levelName.size());
					packet.Write(_levelName.data(), _levelName.size());
					packet.WriteVariableUint32(serverConfig.InitialPlayerHealth);
					packet.WriteVariableUint32(serverConfig.MaxGameTimeSecs);
					packet.WriteVariableUint32(serverConfig.TotalKills);
					packet.WriteVariableUint32(serverConfig.TotalLaps);
					packet.WriteVariableUint32(serverConfig.TotalTreasureCollected);

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LoadLevel, packet);
					return true;
				}
				case ClientPacketType::LevelReady: {
					MemoryStream packet(data);
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ClientPacketType::LevelReady - peer: 0x%p, flags: 0x%02x, ", peer._enet, flags);

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
						LOGD("[MP] ClientPacketType::ChatMessage - invalid playerIndex (%u)", playerIndex);
						return true;
					}

					std::uint8_t reserved = packet.ReadValue<std::uint8_t>();

					std::uint32_t lineLength = packet.ReadVariableUint32();
					if (lineLength == 0 || lineLength > 1024) {
						LOGD("[MP] ClientPacketType::ChatMessage - length out of bounds (%u)", lineLength);
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
					packetOut.WriteValue<std::uint8_t>((std::uint8_t)UI::MessageLevel::Info);
					packetOut.WriteVariableUint32((std::uint32_t)prefixedMessage.size());
					packetOut.Write(prefixedMessage.data(), (std::uint32_t)prefixedMessage.size());

					_networkManager->SendTo([this](const Peer& peer) {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						return (peerDesc && peerDesc->LevelState != PeerLevelState::Unknown);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packetOut);

					_root->InvokeAsync([this, line = std::move(prefixedMessage)]() mutable {
						_console->WriteLine(UI::MessageLevel::Info, std::move(line));
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ClientPacketType::PlayerReady: {
					MemoryStream packet(data);
					PlayerType preferredPlayerType = (PlayerType)packet.ReadValue<std::uint8_t>();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->LevelState != PeerLevelState::LevelLoaded && peerDesc->LevelState != PeerLevelState::LevelSynchronized) {
						LOGW("[MP] ClientPacketType::PlayerReady - invalid state");
						return true;
					}

					if (preferredPlayerType != PlayerType::Jazz && preferredPlayerType != PlayerType::Spaz && preferredPlayerType != PlayerType::Lori) {
						LOGW("[MP] ClientPacketType::PlayerReady - invalid preferred player type");
						return true;
					}

					peerDesc->PreferredPlayerType = preferredPlayerType;

					LOGI("[MP] ClientPacketType::PlayerReady - type: %x, peer: 0x%p", preferredPlayerType, peer._enet);

					_root->InvokeAsync([this, peer]() {
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						if (!peerDesc || peerDesc->LevelState < PeerLevelState::LevelLoaded || peerDesc->LevelState > PeerLevelState::LevelSynchronized) {
							LOGW("[MP] ClientPacketType::PlayerReady - invalid state");
							return;
						}
						if (peerDesc->LevelState == PeerLevelState::LevelSynchronized) {
							peerDesc->LevelState = PeerLevelState::PlayerReady;
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ClientPacketType::PlayerUpdate: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerUpdate - invalid playerIndex (%u)", playerIndex);
						return true;
					}

					std::uint64_t now = packet.ReadVariableUint64();
					if (peerDesc->LastUpdated >= now) {
						return true;
					}

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
							LOGD("Acknowledged player %i warp for sequence #%i", playerIndex, seqNumWarped);
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
							LOGD("Granted permission to player %i to warp asynchronously", playerIndex);
							it2->second.WarpTimeLeft = 0.0f;
						} else if (!justWarped) {
							LOGD("Granted permission to player %i to warp before client", playerIndex);
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
							LOGW("Player %i position mismatch by %i pixels (speed: %0.2f)", playerIndex, (std::int32_t)sqrt(posDiffSqr), sqrt(speedSqr));

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

					peerDesc->LastUpdated = now;

					if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(peerDesc->Player)) {
						remotePlayerOnServer->SyncWithServer(Vector2f(posX, posY), Vector2f(speedX, speedY),
							(flags & RemotePlayerOnServer::PlayerFlags::IsVisible) != RemotePlayerOnServer::PlayerFlags::None,
							(flags & RemotePlayerOnServer::PlayerFlags::IsFacingLeft) != RemotePlayerOnServer::PlayerFlags::None,
							(flags & RemotePlayerOnServer::PlayerFlags::IsActivelyPushing) != RemotePlayerOnServer::PlayerFlags::None);
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
					
					if (auto* remotePlayerOnServer = runtime_cast<RemotePlayerOnServer*>(peerDesc->Player)) {
						std::uint32_t frameCount = theApplication().GetFrameCount();
						if (remotePlayerOnServer->UpdatedFrame != frameCount) {
							remotePlayerOnServer->UpdatedFrame = frameCount;
							remotePlayerOnServer->PressedKeysLast = remotePlayerOnServer->PressedKeys;
						}
						remotePlayerOnServer->PressedKeys = packet.ReadVariableUint64();
					}

					//LOGD("Player %i pressed 0x%08x, last state was 0x%08x", playerIndex, it->second.PressedKeys & 0xffffffffu, prevState);
					return true;
				}
				case ClientPacketType::PlayerChangeWeaponRequest: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto peerDesc = _networkManager->GetPeerDescriptor(peer);
					if (peerDesc->Player == nullptr || peerDesc->Player->_playerIndex != playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerUpdate - invalid playerIndex (%u)", playerIndex);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ClientPacketType::PlayerChangeWeaponRequest - playerIndex: %u, weaponType: %u", playerIndex, weaponType);

					const auto& playerAmmo = peerDesc->Player->GetWeaponAmmo();
					if (weaponType >= playerAmmo.size() || playerAmmo[weaponType] == 0) {
						LOGD("[MP] ClientPacketType::PlayerChangeWeaponRequest - playerIndex: %u, no ammo in selected weapon", playerIndex);

						// Request is denied, send the current weapon back to the client
						HandlePlayerWeaponChanged(peerDesc->Player);
						return true;
					}

					peerDesc->Player->SetCurrentWeapon((WeaponType)weaponType);
					return true;
				}
			}
		} else {
			switch ((ServerPacketType)packetType) {
				case ServerPacketType::PeerSetProperty: {
					MemoryStream packet(data);
					PeerPropertyType type = (PeerPropertyType)packet.ReadValue<std::uint8_t>();
					std::uint64_t peerId = packet.ReadVariableUint64();

					switch (type) {
						case PeerPropertyType::Connected:
						case PeerPropertyType::Disconnected: {
							std::uint8_t playerNameLength = packet.ReadValue<std::uint8_t>();
							String playerName{NoInit, playerNameLength};
							packet.Read(playerName.data(), playerNameLength);

							LOGD("[MP] ServerPacketType::PeerSetProperty - type: %u, peer: 0x%016X, name: \"%s\"", type, peerId, playerName.data());

							if (type == PeerPropertyType::Connected) {
								_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]%s\f[/c] connected", playerName.data()));
							} else {
								_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]%s\f[/c] disconnected", playerName.data()));
							}
							break;
						}
						case PeerPropertyType::Roasted: {
							std::uint8_t victimNameLength = packet.ReadValue<std::uint8_t>();
							String victimName{NoInit, victimNameLength};
							packet.Read(victimName.data(), victimNameLength);

							std::uint64_t attackerPeerId = packet.ReadVariableUint64();

							std::uint8_t attackerNameLength = packet.ReadValue<std::uint8_t>();
							String attackerName{NoInit, attackerNameLength};
							packet.Read(attackerName.data(), attackerNameLength);

							LOGD("[MP] ServerPacketType::PeerSetProperty - type: %u, victim: 0x%016X, victim-name: \"%s\", attacker: 0x%016X, attacker-name: \"%s\"",
								type, peerId, victimName.data(), attackerPeerId, attackerName.data());

							if (!attackerName.empty()) {
								_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]%s\f[/c] was roasted by \f[c:#d0705d]%s\f[/c]",
									victimName.data(), attackerName.data()));
							} else {
								_console->WriteLine(UI::MessageLevel::Info, _f("\f[c:#d0705d]%s\f[/c] was roasted by environment",
									victimName.data()));
							}
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

							LOGD("[MP] ServerPacketType::LevelSetProperty[State] - state: %u, time: %f", (std::uint32_t)state, (float)gameTimeLeft * 0.01f);

							_root->InvokeAsync([this, state, gameTimeLeft]() {
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
							}, NCINE_CURRENT_FUNCTION);
							break;
						}
						case LevelPropertyType::GameMode: {
							std::uint8_t flags = packet.ReadValue<std::uint8_t>();
							MpGameMode gameMode = (MpGameMode)packet.ReadValue<std::uint8_t>();
							std::uint8_t teamId = packet.ReadValue<std::uint8_t>();
							std::uint32_t initialPlayerHealth = packet.ReadVariableUint32();
							std::uint32_t maxGameTimeSecs = packet.ReadVariableUint32();
							std::uint32_t totalKills = packet.ReadVariableUint32();
							std::uint32_t totalLaps = packet.ReadVariableUint32();
							std::uint32_t totalTreasureCollected = packet.ReadVariableUint32();

							LOGD("[MP] ServerPacketType::LevelSetProperty[GameMode] - mode: %u", (std::uint32_t)gameMode);

							auto& serverConfig = _networkManager->GetServerConfiguration();
							serverConfig.GameMode = gameMode;
							serverConfig.IsElimination = (flags & 0x04) != 0;
							serverConfig.InitialPlayerHealth = initialPlayerHealth;
							serverConfig.MaxGameTimeSecs = maxGameTimeSecs;
							serverConfig.TotalKills = totalKills;
							serverConfig.TotalLaps = totalLaps;
							serverConfig.TotalTreasureCollected = totalTreasureCollected;
							break;
						}
						default: {
							LOGD("[MP] ServerPacketType::LevelSetProperty - received unknown property %u", (std::uint32_t)propertyType);
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

					LOGD("[MP] ServerPacketType::ShowInGameLobby - flags: 0x%02x, allowedPlayerTypes: 0x%02x, message: \"%s\"",
						flags, allowedPlayerTypes, serverConfig.WelcomeMessage.data());

					if (flags & 0x01) {
						_root->InvokeAsync([this, flags, allowedPlayerTypes]() {
							_inGameLobby->SetAllowedPlayerTypes(allowedPlayerTypes);
							if (flags & 0x02) {
								_inGameLobby->Show();
							} else {
								_inGameLobby->Hide();
							}
						}, NCINE_CURRENT_FUNCTION);
					}
					return true;
				}
				case ServerPacketType::FadeOut: {
					MemoryStream packet(data);
					std::int32_t fadeOutDelay = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::FadeOut - delay: %i", fadeOutDelay);

					_root->InvokeAsync([this, fadeOutDelay]() {
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
					}, NCINE_CURRENT_FUNCTION);
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
					_root->InvokeAsync([this, actorId, gain, pitch, identifier = std::move(identifier)]() {
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
					}, NCINE_CURRENT_FUNCTION);
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
					_root->InvokeAsync([this, posX, posY, gain, pitch, identifier = std::move(identifier)]() {
						std::unique_lock lock(_lock);
						PlayCommonSfx(identifier, Vector3f((float)posX, (float)posY, 0.0f), gain, pitch);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::ShowAlert: {
					MemoryStream packet(data);
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::uint32_t textLength = packet.ReadVariableUint32();
					String text = String(NoInit, textLength);
					packet.Read(text.data(), textLength);

					LOGD("[MP] ServerPacketType::ShowAlert - text: \"%s\"", text.data());

					_hud->ShowLevelText(text);
					return true;
				}
				case ServerPacketType::ChatMessage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					UI::MessageLevel level = (UI::MessageLevel)packet.ReadValue<std::uint8_t>();

					std::uint32_t messageLength = packet.ReadVariableUint32();
					if (messageLength == 0 || messageLength > 1024) {
						LOGD("[MP] ServerPacketType::ChatMessage - length out of bounds (%u)", messageLength);
						return true;
					}

					String message{NoInit, messageLength};
					packet.Read(message.data(), messageLength);

					if (level == UI::MessageLevel::Info && playerIndex == _lastSpawnedActorId) {
						level = UI::MessageLevel::Echo;
					}

					_root->InvokeAsync([this, level, message = std::move(message)]() mutable {
						_console->WriteLine(level, std::move(message));
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::CreateControllablePlayer: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					PlayerType playerType = (PlayerType)packet.ReadValue<std::uint8_t>();
					std::uint8_t health = packet.ReadValue<std::uint8_t>();
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::uint8_t teamId = packet.ReadValue<std::uint8_t>();
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();

					LOGI("[MP] ServerPacketType::CreateControllablePlayer - playerIndex: %u, playerType: %u, health: %u, flags: %u, team: %u, x: %i, y: %i",
						playerIndex, (std::uint32_t)playerType, health, flags, teamId, posX, posY);

					_lastSpawnedActorId = playerIndex;

					_root->InvokeAsync([this, playerType, health, flags, teamId, posX, posY]() {
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
						// TODO: Needed to initialize newly assigned viewport, because it called asynchronously, not from handler initialization
						Vector2i res = theApplication().GetResolution();
						OnInitializeViewport(res.X, res.Y);
					}, NCINE_CURRENT_FUNCTION);
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

					//LOGD("Remote actor %u created on [%i;%i] with metadata \"%s\"", actorId, posX, posY, metadataPath.data());
					LOGD("[MP] ServerPacketType::CreateRemoteActor - actorId: %u, metadata: \"%s\", x: %i, y: %i", actorId, metadataPath.data(), posX, posY);

					_root->InvokeAsync([this, actorId, flags, posX, posY, posZ, state, metadataPath = std::move(metadataPath), anim, rotation, scaleX, scaleY, rendererType]() {
						{
							std::unique_lock lock(_lock);
							if (_remoteActors.contains(actorId)) {
								LOGW("[MP] ServerPacketType::CreateRemoteActor - actor (%u) already exists", actorId);
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
					}, NCINE_CURRENT_FUNCTION);
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

					LOGD("[MP] ServerPacketType::CreateMirroredActor - actorId: %u, event: %u, x: %i, y: %i", actorId, (std::uint32_t)eventType, tileX * 32 + 16, tileY * 32 + 16);

					_root->InvokeAsync([this, actorId, eventType, eventParams = std::move(eventParams), actorFlags, tileX, tileY, posZ]() {
						{
							std::unique_lock lock(_lock);
							if (_remoteActors.contains(actorId)) {
								LOGW("[MP] ServerPacketType::CreateMirroredActor - actor (%u) already exists", actorId);
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
							LOGD("[MP] ServerPacketType::CreateMirroredActor - CANNOT CREATE - actorId: %u", actorId);
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::DestroyRemoteActor: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();

					LOGD("[MP] ServerPacketType::DestroyRemoteActor - actorId: %u", actorId);

					_root->InvokeAsync([this, actorId]() {
						std::unique_lock lock(_lock);
						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							it->second->SetState(Actors::ActorState::IsDestroyed, true);
							_remoteActors.erase(it);
							_playerNames.erase(actorId);
						} else {
							LOGW("[MP] ServerPacketType::DestroyRemoteActor - NOT FOUND - actorId: %u", actorId);
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::UpdateAllActors: {
					std::unique_lock lock(_lock);

					MemoryStream packetCompressed(data);
					DeflateStream packet(packetCompressed);
					std::uint32_t actorCount = packet.ReadVariableUint32();
					for (std::uint32_t i = 0; i < actorCount; i++) {
						std::uint32_t index = packet.ReadVariableUint32();
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

						auto it = _remoteActors.find(index);
						if (it != _remoteActors.end()) {
							if (auto* remoteActor = runtime_cast<Actors::Multiplayer::RemoteActor*>(it->second)) {
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
					return true;
				}
				case ServerPacketType::MarkRemoteActorAsPlayer: {
					MemoryStream packet(data);
					std::uint32_t actorId = packet.ReadVariableUint32();
					std::uint32_t playerNameLength = packet.ReadVariableUint32();
					String playerName = String(NoInit, playerNameLength);
					packet.Read(playerName.data(), playerNameLength);

					LOGD("[MP] ServerPacketType::MarkRemoteActorAsPlayer - id: %u, name: %s", actorId, playerName.data());

					_root->InvokeAsync([this, actorId, playerName = std::move(playerName)]() mutable {
						_playerNames[actorId] = std::move(playerName);
					}, NCINE_CURRENT_FUNCTION);
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

					LOGD("[MP] ServerPacketType::SetTrigger - id: %u, state: %u", triggerId, newState);

					_root->InvokeAsync([this, triggerId, newState]() {
						TileMap()->SetTrigger(triggerId, newState);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::AdvanceTileAnimation: {
					MemoryStream packet(data);
					std::int32_t tx = packet.ReadVariableInt32();
					std::int32_t ty = packet.ReadVariableInt32();
					std::int32_t amount = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::AdvanceTileAnimation - tx: %i, ty: %i, amount: %i", tx, ty, amount);

					_root->InvokeAsync([this, tx, ty, amount]() {
						TileMap()->AdvanceDestructibleTileAnimation(tx, ty, amount);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerSetProperty: {
					MemoryStream packet(data);
					PlayerPropertyType propertyType = (PlayerPropertyType)packet.ReadValue<std::uint8_t>();
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerSetProperty - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					switch (propertyType) {
						case PlayerPropertyType::Lives: {
							// TODO
							std::int32_t lives = packet.ReadVariableInt32();
							if (!_players.empty()) {
								_players[0]->_lives = lives;
							}
							break;
						}
						case PlayerPropertyType::Health: {
							// TODO
							std::int32_t health = packet.ReadVariableInt32();
							if (!_players.empty()) {
								_players[0]->SetHealth(health);
							}
							break;
						}
						case PlayerPropertyType::Controllable: {
							std::uint8_t enable = packet.ReadValue<std::uint8_t>();
							if (!_players.empty()) {
								_players[0]->_controllableExternal = (enable != 0);
							}
							break;
						}
						case PlayerPropertyType::Invulnerable: {
							// TODO
							break;
						}
						case PlayerPropertyType::Modifier: {
							Actors::Player::Modifier modifier = (Actors::Player::Modifier)packet.ReadValue<std::uint8_t>();
							std::uint32_t decorActorId = packet.ReadVariableUint32();

							_root->InvokeAsync([this, modifier, decorActorId]() {
								std::unique_lock lock(_lock);
								if (!_players.empty()) {
									auto it = _remoteActors.find(decorActorId);
									_players[0]->SetModifier(modifier, it != _remoteActors.end() ? it->second : nullptr);
								}
							}, NCINE_CURRENT_FUNCTION);
							break;
						}
						case PlayerPropertyType::DizzyTime: {
							std::int32_t dizzyTime = packet.ReadVariableInt32();
							if (!_players.empty()) {
								_players[0]->SetDizzyTime(float(dizzyTime));
							}
							break;
						}
						case PlayerPropertyType::WeaponAmmo: {
							std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();
							std::uint16_t weaponAmmo = packet.ReadValue<std::uint16_t>();
							if (!_players.empty()) {
								_players[0]->_weaponAmmo[weaponType] = weaponAmmo;
							}
							break;
						}
						case PlayerPropertyType::WeaponUpgrades: {
							std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();
							std::uint8_t weaponUpgrades = packet.ReadValue<std::uint8_t>();
							if (!_players.empty()) {
								_players[0]->_weaponUpgrades[weaponType] = weaponUpgrades;
							}
							break;
						}
						case PlayerPropertyType::Coins: {
							std::int32_t newCount = packet.ReadVariableInt32();
							if (!_players.empty()) {
								_players[0]->_coins = newCount;
								_hud->ShowCoins(newCount);
							}
							break;
						}
						case PlayerPropertyType::Gems: {
							std::uint8_t gemType = packet.ReadValue<std::uint8_t>();
							std::int32_t newCount = packet.ReadVariableInt32();
							if (!_players.empty() && gemType < arraySize(_players[0]->_gems)) {
								_players[0]->_gems[gemType] = newCount;
								_hud->ShowGems(gemType, newCount);
							}
							break;
						}
						case PlayerPropertyType::Points: {
							std::uint32_t points = packet.ReadVariableUint32();
							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								player->GetPeerDescriptor()->Points = points;
							}
							break;
						}
						case PlayerPropertyType::PositionInRound: {
							std::uint32_t positionInRound = packet.ReadVariableUint32();
							if (!_players.empty()) {
								auto* player = static_cast<RemotablePlayer*>(_players[0]);
								player->GetPeerDescriptor()->PositionInRound = positionInRound;
							}
							break;
						}
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
							LOGD("[MP] ServerPacketType::PlayerSetProperty - received unknown property %u", (std::uint32_t)propertyType);
							break;
						}
					}
					return true;
				}
				case ServerPacketType::PlayerResetProperties: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerSetProperty - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
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
						LOGD("[MP] ServerPacketType::PlayerRespawn - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					LOGD("[MP] ServerPacketType::PlayerRespawn - playerIndex: %u, x: %f, y: %f", playerIndex, posX, posY);

					_root->InvokeAsync([this, posX, posY]() {
						if (!_players.empty()) {
							_players[0]->Respawn(Vector2f(posX, posY));
						}
					}, NCINE_CURRENT_FUNCTION);
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

					LOGD("[MP] ServerPacketType::PlayerMoveInstantly - playerIndex: %u, x: %f, y: %f, sx: %f, sy: %f",
						playerIndex, posX, posY, speedX, speedY);

					_root->InvokeAsync([this, posX, posY, speedX, speedY]() {
						if (!_players.empty()) {
							auto* player = static_cast<RemotablePlayer*>(_players[0]);
							player->MoveRemotely(Vector2f(posX, posY), Vector2f(speedX, speedY));
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerAckWarped: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					std::uint64_t seqNum = packet.ReadVariableUint64();

					LOGD("[MP] ServerPacketType::PlayerAckWarped - playerIndex: %u, seqNum: %llu", playerIndex, seqNum);

					if (_lastSpawnedActorId == playerIndex && _seqNumWarped == seqNum) {
						_seqNumWarped = 0;
					}
					return true;
				}
				case ServerPacketType::PlayerEmitWeaponFlare: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerEmitWeaponFlare - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					LOGD("[MP] ServerPacketType::PlayerEmitWeaponFlare - playerIndex: %u", playerIndex);

					_root->InvokeAsync([this]() {
						if (!_players.empty()) {
							_players[0]->EmitWeaponFlare();
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerChangeWeapon: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerChangeWeapon - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::PlayerChangeWeapon - playerIndex: %u, weaponType: %u", playerIndex, weaponType);

					if (!_players.empty()) {
						auto* remotablePlayer = static_cast<Actors::Multiplayer::RemotablePlayer*>(_players[0]);
						remotablePlayer->ChangingWeaponFromServer = true;
						static_cast<Actors::Player*>(remotablePlayer)->SetCurrentWeapon((WeaponType)weaponType);
						remotablePlayer->ChangingWeaponFromServer = false;
					}
					return true;
				}
				case ServerPacketType::PlayerTakeDamage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerTakeDamage - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::int32_t health = packet.ReadVariableInt32();
					float pushForce = packet.ReadValue<std::int16_t>() / 512.0f;

					LOGD("[MP] ServerPacketType::PlayerTakeDamage - playerIndex: %u, health: %i, pushForce: %f", playerIndex, health, pushForce);

					_root->InvokeAsync([this, health, pushForce]() {
						if (!_players.empty()) {
							_players[0]->TakeDamage(health == 0 ? INT32_MAX : (_players[0]->_health - health), pushForce);
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerActivateSpring: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerActivateSpring - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float forceX = packet.ReadValue<std::int16_t>() / 512.0f;
					float forceY = packet.ReadValue<std::int16_t>() / 512.0f;
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					_root->InvokeAsync([this, posX, posY, forceX, forceY, flags]() {
						if (!_players.empty()) {
							bool removeSpecialMove = false;
							_players[0]->OnHitSpring(Vector2f(posX, posY), Vector2f(forceX, forceY), (flags & 0x01) == 0x01, (flags & 0x02) == 0x02, removeSpecialMove);
							if (removeSpecialMove) {
								_players[0]->_controllable = true;
								_players[0]->EndDamagingMove();
							}
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ServerPacketType::PlayerWarpIn: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerWarpIn - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					ExitType exitType = (ExitType)packet.ReadValue<std::uint8_t>();
					LOGD("[MP] ServerPacketType::PlayerWarpIn - playerIndex: %u, exitType: 0x%02x", playerIndex, (std::uint32_t)exitType);

					_root->InvokeAsync([this, exitType]() {
						if (!_players.empty()) {
							auto* player = static_cast<RemotablePlayer*>(_players[0]);
							player->WarpIn(exitType);
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
			}
		}

		return false;
	}

	void MpLevelHandler::LimitCameraView(Actors::Player* player, std::int32_t left, std::int32_t width)
	{
		// TODO: This should probably be client local
		LevelHandler::LimitCameraView(player, left, width);
	}

	void MpLevelHandler::ShakeCameraView(Actors::Player* player, float duration)
	{
		// TODO: This should probably be client local
		LevelHandler::ShakeCameraView(player, duration);
	}
	
	void MpLevelHandler::ShakeCameraViewNear(Vector2f pos, float duration)
	{
		// TODO: This should probably be client local
		LevelHandler::ShakeCameraViewNear(pos, duration);
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
		// TODO: This should probably be client local
		return LevelHandler::BeginPlayMusic(path, setDefault, forceReload);
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

	void MpLevelHandler::SynchronizePeers()
	{
		for (auto& [peer, peerDesc] : *_networkManager->GetPeers()) {
			if (peerDesc->LevelState == PeerLevelState::LevelLoaded) {
				if (peerDesc != nullptr && peerDesc->PreferredPlayerType != PlayerType::None) {
					peerDesc->LevelState = PeerLevelState::PlayerReady;
				} else {
					peerDesc->LevelState = PeerLevelState::LevelSynchronized;
				}

				LOGI("[MP] Syncing peer (0x%p)", peer._enet);

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
					MemoryStream packet(20 * 1024);
					_tileMap->SerializeResumableToStream(packet);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncTileMap, packet);
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
					if (ActorShouldBeMirrored(remotingActor)) {
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
				peerDesc->LevelState = PeerLevelState::PlayerSpawned;

				const auto& serverConfig = _networkManager->GetServerConfiguration();
				Vector2f spawnPosition = GetSpawnPoint(peerDesc->PreferredPlayerType);

				std::uint8_t playerIndex = FindFreePlayerId();
				LOGI("[MP] Spawning player %u (0x%p)", playerIndex, peer._enet);

				std::shared_ptr<Actors::Multiplayer::RemotePlayerOnServer> player = std::make_shared<Actors::Multiplayer::RemotePlayerOnServer>(peerDesc);
				std::uint8_t playerParams[2] = { (std::uint8_t)peerDesc->PreferredPlayerType, (std::uint8_t)playerIndex };
				player->OnActivated(Actors::ActorActivationDetails(
					this,
					Vector3i((std::int32_t)spawnPosition.X, (std::int32_t)spawnPosition.Y, PlayerZ - playerIndex),
					playerParams
				));
				player->_controllableExternal = _controllableExternal;
				peerDesc->LapStarted = TimeStamp::now();

				Actors::Multiplayer::RemotePlayerOnServer* ptr = player.get();
				_players.push_back(ptr);

				_suppressRemoting = true;
				AddActor(player);
				_suppressRemoting = false;

				// The player is invulnerable for a short time after spawning
				ptr->SetInvulnerability(serverConfig.SpawnInvulnerableSecs * FrameTimer::FramesPerSecond, Actors::Player::InvulnerableType::Transient);

				ApplyGameModeToPlayer(serverConfig.GameMode, ptr);

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
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_health);
					packet.WriteValue<std::uint8_t>(flags);
					packet.WriteValue<std::uint8_t>(peerDesc->Team);
					packet.WriteVariableInt32((std::int32_t)player->_pos.X);
					packet.WriteVariableInt32((std::int32_t)player->_pos.Y);

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateControllablePlayer, packet);
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

					_networkManager->SendTo([this, self = peer](const Peer& peer) {
						if (peer == self) {
							return false;
						}
						auto peerDesc = _networkManager->GetPeerDescriptor(peer);
						return (peerDesc && peerDesc->LevelState >= PeerLevelState::LevelSynchronized);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet);
				}

				if (_levelState == LevelState::WaitingForMinPlayers) {
					_waitingForPlayerCount = (std::int32_t)serverConfig.MinPlayerCount - (std::int32_t)_players.size();
					SendLevelStateToAllPlayers();
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
		return (runtime_cast<Actors::Multiplayer::LocalPlayerOnServer*>(actor) ||
				runtime_cast<Actors::Multiplayer::RemotablePlayer*>(actor));
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

		for (auto* player : _players) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			peerDesc->PositionInRound = 0;
			peerDesc->Deaths = 0;
			peerDesc->Kills = 0;
			peerDesc->Laps = 0;
			peerDesc->TreasureCollected = 0;
			peerDesc->DeathElapsedFrames = FLT_MAX;

			mpPlayer->_coins = 0;
			std::memset(mpPlayer->_gems, 0, sizeof(mpPlayer->_gems));
			std::memset(mpPlayer->_weaponAmmo, 0, sizeof(mpPlayer->_weaponAmmo));
			std::memset(mpPlayer->_weaponAmmoCheckpoint, 0, sizeof(mpPlayer->_weaponAmmoCheckpoint));

			mpPlayer->_coinsCheckpoint = 0;
			std::memset(mpPlayer->_gemsCheckpoint, 0, sizeof(mpPlayer->_gemsCheckpoint));
			std::memset(mpPlayer->_weaponUpgrades, 0, sizeof(mpPlayer->_weaponUpgrades));
			std::memset(mpPlayer->_weaponUpgradesCheckpoint, 0, sizeof(mpPlayer->_weaponUpgradesCheckpoint));

			mpPlayer->_weaponAmmo[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
			mpPlayer->_weaponAmmoCheckpoint[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
			mpPlayer->_currentWeapon = WeaponType::Blaster;
			mpPlayer->_health = serverConfig.InitialPlayerHealth;

			if (peerDesc->RemotePeer) {
				MemoryStream packet1(4);
				packet1.WriteVariableUint32(mpPlayer->_playerIndex);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerResetProperties, packet1);

				MemoryStream packet2(9);
				packet2.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Health);
				packet2.WriteVariableUint32(mpPlayer->_playerIndex);
				packet2.WriteVariableInt32(mpPlayer->_health);
				_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet2);
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

	void MpLevelHandler::WarpAllPlayersToStart()
	{
		// TODO: Reset ambient lighting
		for (auto* player : _players) {
			Vector2f spawnPosition = GetSpawnPoint(player->_playerTypeOriginal);
			player->WarpToPosition(spawnPosition, WarpFlags::Default);
		}
	}

	void MpLevelHandler::CalculatePositionInRound()
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
				case MpGameMode::Battle:
					roundPoints = peerDesc->Kills;
					break;
				case MpGameMode::Race:
					roundPoints = (peerDesc->Laps > 0 ? (std::uint32_t)(peerDesc->LapsElapsedFrames / peerDesc->Laps) : 0);
					break;
				case MpGameMode::TreasureHunt:
					roundPoints = peerDesc->TreasureCollected;
					break;
			}

			bool isDead = (serverConfig.IsElimination && peerDesc->Deaths >= serverConfig.TotalKills);
			if (isDead) {
				sortedDeadPlayers.push_back(pair(mpPlayer, roundPoints));
			} else {
				sortedPlayers.push_back(pair(mpPlayer, roundPoints));
			}
		}

		const auto comparator = [this](const Pair<MpPlayer*, std::uint32_t>& a, const Pair<MpPlayer*, std::uint32_t>& b) -> bool {
			return a.second() > b.second();
		};

		nCine::sort(sortedPlayers.begin(), sortedPlayers.end(), comparator);
		nCine::sort(sortedDeadPlayers.begin(), sortedDeadPlayers.end(), comparator);

		std::uint32_t currentPos = 1;
		std::uint32_t prevPos = 0;
		for (std::int32_t i = 0; i < sortedPlayers.size(); i++) {
			std::uint32_t pos;
			if (i > 0 && sortedPlayers[i].second() == sortedPlayers[i - 1].second()) {
				pos = prevPos;
			} else {
				pos = currentPos;
				prevPos = currentPos;
			}
			
			currentPos++;

			auto peerDesc = sortedPlayers[i].first()->GetPeerDescriptor();
			if (peerDesc->PositionInRound != pos) {
				peerDesc->PositionInRound = pos;

				if (peerDesc->RemotePeer) {
					MemoryStream packet(9);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::PositionInRound);
					packet.WriteVariableUint32(sortedPlayers[i].first()->_playerIndex);
					packet.WriteVariableUint32(peerDesc->PositionInRound);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
				}
			}
		}

		for (std::int32_t i = 0; i < sortedDeadPlayers.size(); i++) {
			std::uint32_t pos;
			if (i > 0 && sortedDeadPlayers[i].second() == sortedDeadPlayers[i - 1].second()) {
				pos = prevPos;
			} else {
				pos = currentPos;
				prevPos = currentPos;
			}

			currentPos++;

			auto peerDesc = sortedDeadPlayers[i].first()->GetPeerDescriptor();
			if (peerDesc->PositionInRound != pos) {
				peerDesc->PositionInRound = pos;

				if (peerDesc->RemotePeer) {
					MemoryStream packet(9);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::PositionInRound);
					packet.WriteVariableUint32(sortedDeadPlayers[i].first()->_playerIndex);
					packet.WriteVariableUint32(peerDesc->PositionInRound);
					_networkManager->SendTo(peerDesc->RemotePeer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerSetProperty, packet);
				}
			}
		}
	}

	void MpLevelHandler::CheckGameEnds()
	{
		if (_levelState != LevelState::Running) {
			return;
		}

		CalculatePositionInRound();

		auto& serverConfig = _networkManager->GetServerConfiguration();

		if (serverConfig.GameMode != MpGameMode::Cooperation && serverConfig.IsElimination) {
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
				if (!serverConfig.IsElimination) {
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
			
			case MpGameMode::TreasureHunt:
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
			}
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

		if (winner) {
			auto peerDesc = winner->GetPeerDescriptor();
			ShowAlertToAllPlayers(_f("\n\nWinner is %s", peerDesc->PlayerName.data()));
		}

		for (auto* player : _players) {
			auto* mpPlayer = static_cast<MpPlayer*>(player);
			auto peerDesc = mpPlayer->GetPeerDescriptor();

			if (peerDesc->PositionInRound > 0 && peerDesc->PositionInRound - 1 < arraySize(PointsPerPosition)) {
				peerDesc->Points += PointsPerPosition[peerDesc->PositionInRound - 1];

				if (peerDesc->RemotePeer) {
					MemoryStream packet(9);
					packet.WriteValue<std::uint8_t>((std::uint8_t)PlayerPropertyType::Points);
					packet.WriteVariableUint32(mpPlayer->_playerIndex);
					packet.WriteVariableUint32(peerDesc->Points);
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
		serverConfig.IsElimination = playlistEntry.IsElimination;
		serverConfig.InitialPlayerHealth = playlistEntry.InitialPlayerHealth;
		serverConfig.MaxGameTimeSecs = playlistEntry.MaxGameTimeSecs;
		serverConfig.PreGameSecs = playlistEntry.PreGameSecs;
		serverConfig.TotalKills = playlistEntry.TotalKills;
		serverConfig.TotalLaps = playlistEntry.TotalLaps;
		serverConfig.TotalTreasureCollected = playlistEntry.TotalTreasureCollected;

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
			levelInit.LastExitType = ExitType::Normal;
			HandleLevelChange(std::move(levelInit));
		}

		return true;
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

		MemoryStream packet(1);
		packet.WriteValue<std::uint8_t>((std::uint8_t)playerType);
		// TODO: Selected team
		packet.WriteValue<std::uint8_t>(0);

		_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::PlayerReady, packet);
	}

	bool MpLevelHandler::ActorShouldBeMirrored(Actors::ActorBase* actor)
	{
		// If actor has no animation, it's probably some special object (usually lights and ambient sounds)
		if (actor->_currentAnimation == nullptr && actor->_currentTransition == nullptr) {
			return true;
		}

		// List of objects that needs to be recreated on client-side instead of remoting
		return (runtime_cast<Actors::Environment::AirboardGenerator*>(actor) || runtime_cast<Actors::Environment::SteamNote*>(actor) ||
				runtime_cast<Actors::Environment::SwingingVine*>(actor) || runtime_cast<Actors::Solid::Bridge*>(actor) ||
				runtime_cast<Actors::Solid::MovingPlatform*>(actor) || runtime_cast<Actors::Solid::PinballBumper*>(actor) ||
				runtime_cast<Actors::Solid::PinballPaddle*>(actor) || runtime_cast<Actors::Solid::SpikeBall*>(actor));
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
					LOGW("Deviation of player %i was high for too long (deviation: %0.2fpx, speed: %0.2f)", player->_playerIndex, sqrt(devSqr), sqrt(speedSqr));
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
				LOGW("Deviation of player %i is high (alpha: %0.1f, deviation: %0.2fpx, speed: %0.2f)", player->_playerIndex, alpha, sqrt(devSqr), sqrt(speedSqr));
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
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f bytes", _updatePacketSize[_plotIndex]);

		ImGui::PlotLines("Update Packet Size Compressed", _compressedUpdatePacketSize, PlotValueCount, _plotIndex, nullptr, 0.0f, _updatePacketMaxSize, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f bytes", _compressedUpdatePacketSize[_plotIndex]);

		ImGui::Separator();

		ImGui::PlotLines("Actors", _actorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f", _actorsCount[_plotIndex]);

		ImGui::PlotLines("Remote Actors", _remoteActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f", _remoteActorsCount[_plotIndex]);

		ImGui::PlotLines("Mirrored Actors", _mirroredActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f", _mirroredActorsCount[_plotIndex]);

		ImGui::PlotLines("Remoting Actors", _remotingActorsCount, PlotValueCount, _plotIndex, nullptr, 0.0f, _actorsMaxCount, ImVec2(appWidth * 0.2f, 40.0f));
		ImGui::SameLine(360.0f);
		ImGui::Text("%.0f", _remotingActorsCount[_plotIndex]);

		ImGui::Text("Last spawned ID: %u", _lastSpawnedActorId);

		ImGui::SeparatorText("Peers");

		ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoPadOuterX;
		if (ImGui::BeginTable("peers", 6, flags, ImVec2(0.0f, 0.0f))) {
			ImGui::TableSetupColumn("Peer");
			ImGui::TableSetupColumn("Player Index");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Last Updated");
			ImGui::TableSetupColumn("X");
			ImGui::TableSetupColumn("Y");
			ImGui::TableHeadersRow();
			
			for (auto& [peer, desc] : _peerDesc) {
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("0x%p", peer._enet);

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", desc.Player != nullptr ? desc.Player->GetPlayerIndex() : -1);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("0x%x", desc.State);

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", desc.LastUpdated);

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%.2f", desc.Player != nullptr ? desc.Player->GetPos().X : -1.0f);

				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%.2f", desc.Player != nullptr ? desc.Player->GetPos().Y : -1.0f);
			}
			ImGui::EndTable();
		}

		ImGui::SeparatorText("Player States");

		if (ImGui::BeginTable("playerStates", 4, flags, ImVec2(0.0f, 0.0f))) {
			ImGui::TableSetupColumn("Player Index");
			ImGui::TableSetupColumn("Flags");
			ImGui::TableSetupColumn("Pressed");
			ImGui::TableSetupColumn("Pressed (Last)");
			ImGui::TableHeadersRow();

			for (auto& [playerIdx, state] : _playerStates) {
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", playerIdx);

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("0x%08x", state.Flags);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("0x%08x", state.PressedKeys);

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("0x%08x", state.PressedKeysLast);
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}
#endif
}

#endif