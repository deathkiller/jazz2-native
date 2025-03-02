#include "MpLevelHandler.h"

#if defined(WITH_MULTIPLAYER)

#include "PacketTypes.h"
#include "../PreferencesCache.h"
#include "../Input/ControlScheme.h"
#include "../UI/HUD.h"
#include "../UI/DiscordRpcClient.h"
#include "../UI/InGameConsole.h"
#include "../UI/Multiplayer/MpInGameCanvasLayer.h"
#include "../UI/Multiplayer/MpInGameLobby.h"
#include "../../Main.h"

#if defined(WITH_ANGELSCRIPT)
#	include "../Scripting/LevelScriptLoader.h"
#endif

#include "../../nCine/MainApplication.h"
#include "../../nCine/IAppEventHandler.h"
#include "../../nCine/ServiceLocator.h"
#include "../../nCine/Input/IInputEventHandler.h"
#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Sprite.h"
#include "../../nCine/Graphics/Texture.h"
#include "../../nCine/Graphics/Viewport.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Audio/AudioReaderMpt.h"
#include "../../nCine/Base/Random.h"

#include "../Actors/Player.h"
#include "../Actors/Multiplayer/LocalPlayerOnServer.h"
#include "../Actors/Multiplayer/RemotablePlayer.h"
#include "../Actors/Multiplayer/RemotePlayerOnServer.h"
#include "../Actors/Multiplayer/RemoteActor.h"
#include "../Actors/SolidObjectBase.h"
#include "../Actors/Enemies/Bosses/BossBase.h"

#include "../Actors/Environment/AirboardGenerator.h"
#include "../Actors/Environment/SteamNote.h"
#include "../Actors/Environment/SwingingVine.h"
#include "../Actors/Solid/Bridge.h"
#include "../Actors/Solid/MovingPlatform.h"
#include "../Actors/Solid/PinballBumper.h"
#include "../Actors/Solid/PinballPaddle.h"
#include "../Actors/Solid/SpikeBall.h"

#include <float.h>

#include <Utf8.h>
#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::IO::Compression;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	MpLevelHandler::MpLevelHandler(IRootController* root, NetworkManager* networkManager, bool enableLedgeClimb)
		: LevelHandler(root), _networkManager(networkManager), _updateTimeLeft(1.0f),
			_initialUpdateSent(false), _enableSpawning(true), _lastSpawnedActorId(-1), _seqNum(0), _seqNumWarped(0), _suppressRemoting(false),
			_ignorePackets(false), _enableLedgeClimb(enableLedgeClimb)
#if defined(DEATH_DEBUG)
			, _debugAverageUpdatePacketSize(0)
#endif
#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
			, _plotIndex(0), _actorsMaxCount(0.0f), _actorsCount{}, _remoteActorsCount{}, _remotingActorsCount{},
			_mirroredActorsCount{}, _updatePacketMaxSize(0.0f), _updatePacketSize{}, _compressedUpdatePacketSize{}
#endif
	{
		_isServer = (networkManager->GetState() == NetworkState::Listening);

		if (_isServer) {
			// TODO: Lobby message
			_lobbyMessage = "Welcome to the testing server!";
		}
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

			std::uint8_t flags = 0;
			if (PreferencesCache::EnableReforgedGameplay) {
				flags |= 0x01;
			}
			if (PreferencesCache::EnableLedgeClimb) {
				flags |= 0x02;
			}

			MemoryStream packet(10 + _episodeName.size() + _levelFileName.size());
			packet.WriteValue<std::uint8_t>(flags);
			packet.WriteValue<std::uint8_t>((std::uint8_t)_networkManager->GameMode);
			packet.WriteVariableUint32(_episodeName.size());
			packet.Write(_episodeName.data(), _episodeName.size());
			packet.WriteVariableUint32(_levelFileName.size());
			packet.Write(_levelFileName.data(), _levelFileName.size());

			_networkManager->SendTo([this](const Peer& peer) {
				auto* globalPeerDesc = _networkManager->GetPeerDescriptor(peer);
				return (globalPeerDesc != nullptr && globalPeerDesc->IsAuthenticated);
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
		return (_networkManager->GameMode == MpGameMode::Cooperation ? 180.0f : 80.0f);
	}

	float MpLevelHandler::GetDefaultAmbientLight() const
	{
		// TODO: Remove this override
		return LevelHandler::GetDefaultAmbientLight();
	}

	void MpLevelHandler::SetAmbientLight(Actors::Player* player, float value)
	{
		if (_isServer && player != nullptr) {
			auto it = _playerStates.find(player->_playerIndex);
			if (it != _playerStates.end()) {
				// TODO: Send it to remote peer
				return;
			}
		}

		LevelHandler::SetAmbientLight(player, value);
	}

	void MpLevelHandler::OnBeginFrame()
	{
		LevelHandler::OnBeginFrame();

		if (!_isServer) {
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
		for (auto& [playerIndex, playerState] : _playerStates) {
			if (playerState.UpdatedFrame != frameCount) {
				playerState.UpdatedFrame = frameCount;
				playerState.PressedKeysLast = playerState.PressedKeys;
			}
		}

		_updateTimeLeft -= timeMult;
		if (_updateTimeLeft < 0.0f) {
			_updateTimeLeft = FrameTimer::FramesPerSecond / UpdatesPerSecond;

			if (!_initialUpdateSent) {
				_initialUpdateSent = true;

				LOGD("[MP] Level \"%s/%s\" is ready", _episodeName.data(), _levelFileName.data());

				if (!_isServer) {
					std::uint8_t flags = 0;
					if (PreferencesCache::EnableLedgeClimb) {
						flags |= 0x02;
					}

					MemoryStream packet(1);
					packet.WriteValue<std::uint8_t>(flags);
					_networkManager->SendTo(AllPeers, NetworkChannel::Main, (std::uint8_t)ClientPacketType::LevelReady, packet);
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
				std::uint32_t actorCount = (std::uint32_t)(_players.size() + _remotingActors.size());

				MemoryStream packet(5 + actorCount * 19);
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
					if (player->IsFacingLeft()) {
						flags |= 0x10;
					}
					packet.WriteValue<std::uint8_t>(flags);

					packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 512.0f));
					packet.WriteVariableUint32((std::uint32_t)(player->_currentTransition != nullptr ? player->_currentTransition->State : player->_currentAnimation->State));

					float rotation = player->_renderer.rotation();
					if (rotation < 0.0f) rotation += fRadAngle360;
					packet.WriteValue<std::uint16_t>((std::uint16_t)(rotation * UINT16_MAX / fRadAngle360));
					Actors::ActorRendererType rendererType = player->_renderer.GetRendererType();
					packet.WriteValue<std::uint8_t>((std::uint8_t)rendererType);
				}

				for (auto& [remotingActor, remotingActorInfo] : _remotingActors) {
					packet.WriteVariableUint32(remotingActorInfo.ActorID);

					std::int32_t newPosX = (std::int32_t)(remotingActor->_pos.X * 512.0f);
					std::int32_t newPosY = (std::int32_t)(remotingActor->_pos.Y * 512.0f);
					bool positionChanged = (newPosX != remotingActorInfo.LastPosX || newPosY != remotingActorInfo.LastPosY);
					
					std::uint32_t newAnimation = (std::uint32_t)(remotingActor->_currentTransition != nullptr ? remotingActor->_currentTransition->State : (remotingActor->_currentAnimation != nullptr ? remotingActor->_currentAnimation->State : AnimState::Idle));
					float rotation = remotingActor->_renderer.rotation();
					if (rotation < 0.0f) rotation += fRadAngle360;
					std::uint16_t newRotation = (std::uint16_t)(rotation * UINT16_MAX / fRadAngle360);
					std::uint8_t newRendererType = (std::uint8_t)remotingActor->_renderer.GetRendererType();
					bool animationChanged = (newAnimation != remotingActorInfo.LastAnimation || newRotation != remotingActorInfo.LastRotation || newRendererType != remotingActorInfo.LastRendererType);

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
					if (remotingActor->IsFacingLeft()) {
						flags |= 0x10;
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
						packet.WriteValue<std::uint8_t>(newRendererType);

						remotingActorInfo.LastAnimation = newAnimation;
						remotingActorInfo.LastRotation = newRotation;
						remotingActorInfo.LastRendererType = newRendererType;
					}
				}

				MemoryStream packetCompressed(1024);
				{
					DeflateWriter dw(packetCompressed);
					dw.Write(packet.GetBuffer(), packet.GetSize());
				}

#if defined(DEATH_DEBUG)
				_debugAverageUpdatePacketSize = lerp(_debugAverageUpdatePacketSize, (std::int32_t)packet.GetSize(), 0.2f * timeMult);
#endif
#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
				_updatePacketSize[_plotIndex] = packet.GetSize();
				_updatePacketMaxSize = std::max(_updatePacketMaxSize, _updatePacketSize[_plotIndex]);
				_compressedUpdatePacketSize[_plotIndex] = packetCompressed.GetSize();
#endif

				_networkManager->SendTo([this](const Peer& peer) {
					auto it = _peerDesc.find(peer);
					return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
				}, NetworkChannel::UnreliableUpdates, (std::uint8_t)ServerPacketType::UpdateAllActors, packetCompressed);

				SynchronizePeers();
			} else {
				if (!_players.empty()) {
					_seqNum++;

					Clock& c = nCine::clock();
					std::uint64_t now = c.now() * 1000 / c.frequency();
					auto player = _players[0];

					PlayerFlags flags = (PlayerFlags)player->_currentSpecialMove;
					if (player->IsFacingLeft()) {
						flags |= PlayerFlags::IsFacingLeft;
					}
					if (player->_renderer.isDrawEnabled()) {
						flags |= PlayerFlags::IsVisible;
					}
					if (player->_isActivelyPushing) {
						flags |= PlayerFlags::IsActivelyPushing;
					}
					if (_seqNumWarped != 0) {
						flags |= PlayerFlags::JustWarped;
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
				if (line.hasPrefix("/ban "_s)) {
					// TODO: Implement /ban
				} else if (line == "/info"_s) {
					char infoBuffer[128];
					formatString(infoBuffer, sizeof(infoBuffer), "Current level: %s/%s (\f[w:80]\f[c:#707070]%s\f[/c]\f[/w])", _episodeName.data(), _levelFileName.data(), GameModeToString(_networkManager->GameMode).data());
					_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
					formatString(infoBuffer, sizeof(infoBuffer), "Players: \f[w:80]\f[c:#707070]%zu\f[/c]\f[/w]/%zu", _peerDesc.size() + 1, NetworkManagerBase::MaxPeerCount);
					_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
					formatString(infoBuffer, sizeof(infoBuffer), "Server load: %i ms", (std::int32_t)(theApplication().GetFrameTimer().GetLastFrameDuration() * 1000.0f));
					_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
					return true;
				} else if (line.hasPrefix("/kick "_s)) {
					// TODO: Implement /kick
				} else if (line.hasPrefix("/set "_s)) {
					auto [variableName, sep, value] = line.exceptPrefix("/set "_s).trimmedPrefix().partition(' ');
					if (variableName == "mode"_s) {
						auto gameModeString = StringUtils::lowercase(value.trimmed());

						MpGameMode gameMode;
						if (gameModeString == "battle"_s || gameModeString == "b"_s) {
							gameMode = MpGameMode::Battle;
						} else if (gameModeString == "teambattle"_s || gameModeString == "tb"_s) {
							gameMode = MpGameMode::TeamBattle;
						} else if (gameModeString == "capturetheflag"_s || gameModeString == "ctf"_s) {
							gameMode = MpGameMode::CaptureTheFlag;
						} else if (gameModeString == "race"_s || gameModeString == "r"_s) {
							gameMode = MpGameMode::Race;
						} else if (gameModeString == "teamrace"_s || gameModeString == "tr"_s) {
							gameMode = MpGameMode::TeamRace;
						} else if (gameModeString == "treasurehunt"_s || gameModeString == "th"_s) {
							gameMode = MpGameMode::TreasureHunt;
						} else if (gameModeString == "cooperation"_s || gameModeString == "coop"_s || gameModeString == "c"_s) {
							gameMode = MpGameMode::Cooperation;
						} else {
							return false;
						}

						if (SetGameMode(gameMode)) {
							char infoBuffer[128];
							formatString(infoBuffer, sizeof(infoBuffer), "Game mode set to \f[w:80]\f[c:#707070]%s\f[/c]\f[/w]", GameModeToString(_networkManager->GameMode).data());
							_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
							return true;
						}
					} else if (variableName == "level"_s) {
						// TODO: Implement /set level
					} else if (variableName == "lobby"_s) {
						SetLobbyMessage(StringUtils::replaceAll(value.trimmed(), "\\n"_s, "\n"_s));
						_console->WriteLine(UI::MessageLevel::Info, "Lobby message changed");
						return true;
					} else if (variableName == "name"_s) {
						auto name = value.trimmed();
						_root->SetServerName(name);

						name = _root->GetServerName();
						char infoBuffer[128];
						if (!name.empty()) {
							formatString(infoBuffer, sizeof(infoBuffer), "Server name set to \f[w:80]\f[c:#707070]%s\f[/c]\f[/w]", String::nullTerminatedView(name).data());
						} else {
							formatString(infoBuffer, sizeof(infoBuffer), "Server visibility to \f[w:80]\f[c:#707070]hidden\f[/c]\f[/w]");
						}
						_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
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
						_console->WriteLine(UI::MessageLevel::Info, infoBuffer);
						return true;
					}
				} else if (line.hasPrefix("/alert "_s)) {
					StringView message = line.exceptPrefix("/alert "_s).trimmed();
					if (!message.empty()) {
						MemoryStream packet(4 + message.size());
						packet.WriteVariableUint32((std::uint32_t)message.size());
						packet.Write(message.data(), (std::uint32_t)message.size());

						_networkManager->SendTo([this](const Peer& peer) {
							auto it = _peerDesc.find(peer);
							return (it != _peerDesc.end() && it->second.State != LevelPeerState::Unknown);
						}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ShowAlert, packet);
					}
				}

				return false;
			}

			// Chat message
			MemoryStream packet(9 + line.size());
			packet.WriteVariableUint32(0); // TODO: Player index
			packet.WriteValue<std::uint8_t>(0); // Reserved
			packet.WriteVariableUint32((std::uint32_t)line.size());
			packet.Write(line.data(), (std::uint32_t)line.size());

			_networkManager->SendTo([this](const Peer& peer) {
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State != LevelPeerState::Unknown);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packet);
		} else {
			if (line.hasPrefix('/')) {
				// Command are allowed only on server
				return false;
			}

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
			std::uint32_t actorId = FindFreeActorId();
			Actors::ActorBase* actorPtr = actor.get();
			_remotingActors[actorPtr] = { actorId };

			// Store only used IDs on server-side
			_remoteActors[actorId] = nullptr;

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
						auto it = _peerDesc.find(peer);
						return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateMirroredActor, packet);
				}
			} else {
				const auto& metadataPath = actorPtr->_metadata->Path;

				MemoryStream packet(28 + metadataPath.size());
				packet.WriteVariableUint32(actorId);
				packet.WriteVariableInt32((std::int32_t)actorPtr->_pos.X);
				packet.WriteVariableInt32((std::int32_t)actorPtr->_pos.Y);
				packet.WriteVariableInt32((std::int32_t)actorPtr->_renderer.layer());
				packet.WriteVariableUint32((std::uint32_t)actorPtr->_state);
				packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
				packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
				packet.WriteVariableUint32((std::uint32_t)(actorPtr->_currentTransition != nullptr ? actorPtr->_currentTransition->State : actorPtr->_currentAnimation->State));
				
				_networkManager->SendTo([this](const Peer& peer) {
					auto it = _peerDesc.find(peer);
					return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
			}
		}
	}

	std::shared_ptr<AudioBufferPlayer> MpLevelHandler::PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch)
	{
		if (_isServer) {
			std::uint32_t actorId;
			auto it = _remotingActors.find(self);
			if (auto* player = runtime_cast<Actors::Player*>(self)) {
				actorId = player->_playerIndex;
			} else if (it != _remotingActors.end()) {
				actorId = it->second.ActorID;
			} else {
				actorId = UINT32_MAX;
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

				_networkManager->SendTo([this, self](const Peer& peer) {
					auto it = _peerDesc.find(peer);
					return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized && it->second.Player != self);
				}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlaySfx, packet);
			}
		}

		return LevelHandler::PlaySfx(self, identifier, buffer, pos, sourceRelative, gain, pitch);
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
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
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

	void MpLevelHandler::BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, uint8_t* eventParams)
	{
		LevelHandler::BroadcastTriggeredEvent(initiator, eventType, eventParams);
	}

	void MpLevelHandler::BeginLevelChange(Actors::ActorBase* initiator, ExitType exitType, StringView nextLevel)
	{
		if (!_isServer || _nextLevelType != ExitType::None) {
			// Level can be changed only by server
			return;
		}

		LOGD("[MP] Changing level to \"%s\" (0x%02x)", nextLevel.data(), (std::uint32_t)exitType);

		LevelHandler::BeginLevelChange(initiator, exitType, nextLevel);

		if ((exitType & ExitType::FastTransition) != ExitType::FastTransition) {
			float fadeOutDelay = _nextLevelTime - 40.0f;

			MemoryStream packet(4);
			packet.WriteVariableInt32((std::int32_t)fadeOutDelay);

			_networkManager->SendTo([this](const Peer& peer) {
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelLoaded);
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

	bool MpLevelHandler::HandlePlayerDied(Actors::Player* player, Actors::ActorBase* collider)
	{
#if defined(WITH_ANGELSCRIPT)
		if (_scripts != nullptr) {
			_scripts->OnPlayerDied(player, collider);
		}
#endif

		// TODO
		//return LevelHandler::HandlePlayerDied(player, collider);

		if (_isServer && _enableSpawning) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(12);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_checkpointPos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_checkpointPos.Y * 512.0f));
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRespawn, packet);
					break;
				}
			}
			return true;
		} else {
			return false;
		}
	}

	void MpLevelHandler::HandlePlayerLevelChanging(Actors::Player* player, ExitType exitType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(5);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)exitType);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerWarpIn, packet);
					break;
				}
			}
		}
	}

	bool MpLevelHandler::HandlePlayerSpring(Actors::Player* player, Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					std::uint8_t flags = 0;
					if (keepSpeedX) {
						flags |= 0x01;
					}
					if (keepSpeedY) {
						flags |= 0x02;
					}

					MemoryStream packet(17);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(force.X * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(force.Y * 512.0f));
					packet.WriteValue<std::uint8_t>(flags);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerActivateSpring, packet);
					break;
				}
			}
		}

		return true;
	}

	void MpLevelHandler::HandlePlayerBeforeWarp(Actors::Player* player, Vector2f pos, WarpFlags flags)
	{
		if (!_isServer) {
			return;
		}

		if (_networkManager->GameMode == MpGameMode::Race && (flags & WarpFlags::IncrementLaps) == WarpFlags::IncrementLaps) {
			// TODO: Increment laps
		}

		if ((flags & WarpFlags::Fast) == WarpFlags::Fast) {
			// Nothing to do, sending PlayerMoveInstantly packet is enough
			return;
		}

		for (const auto& [peer, peerDesc] : _peerDesc) {
			if (peerDesc.Player == player) {
				MemoryStream packet(5);
				packet.WriteVariableUint32(player->_playerIndex);
				packet.WriteValue<std::uint8_t>(0xFF);	// Only temporary, no level changing
				_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerWarpIn, packet);
				break;
			}
		}
	}

	void MpLevelHandler::HandlePlayerTakeDamage(Actors::Player* player, std::int32_t amount, float pushForce)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(10);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteVariableInt32(player->_health);
					packet.WriteValue<std::int16_t>((std::int16_t)(pushForce * 512.0f));
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerTakeDamage, packet);
					break;
				}
			}
		}
	}

	void MpLevelHandler::HandlePlayerRefreshAmmo(Actors::Player* player, WeaponType weaponType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(7);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
					packet.WriteValue<std::uint16_t>((std::uint16_t)player->_weaponAmmo[(std::uint8_t)player->_currentWeapon]);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRefreshAmmo, packet);
					break;
				}
			}
		}
	}

	void MpLevelHandler::HandlePlayerRefreshWeaponUpgrades(Actors::Player* player, WeaponType weaponType)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(6);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_weaponUpgrades[(std::uint8_t)player->_currentWeapon]);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRefreshWeaponUpgrades, packet);
					break;
				}
			}
		}
	}

	void MpLevelHandler::HandlePlayerEmitWeaponFlare(Actors::Player* player)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(4);
					packet.WriteVariableUint32(player->_playerIndex);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerEmitWeaponFlare, packet);
					break;
				}
			}
		}
	}

	void MpLevelHandler::HandlePlayerWeaponChanged(Actors::Player* player)
	{
		// TODO: Only called by RemotePlayerOnServer
		if (_isServer) {
			for (const auto & [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(5);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_currentWeapon);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerChangeWeapon, packet);
					break;
				}
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
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(16);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.Y * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 512.0f));
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerMoveInstantly, packet);
					break;
				}
			}
		}
	}

	void MpLevelHandler::HandlePlayerCoins(Actors::Player* player, std::int32_t prevCount, std::int32_t newCount)
	{
		LevelHandler::HandlePlayerCoins(player, prevCount, newCount);

		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(8);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteVariableInt32(newCount);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRefreshCoins, packet);
					break;
				}
			}
		}
	}

	void MpLevelHandler::HandlePlayerGems(Actors::Player* player, std::uint8_t gemType, std::int32_t prevCount, std::int32_t newCount)
	{
		LevelHandler::HandlePlayerGems(player, gemType, prevCount, newCount);

		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				if (peerDesc.Player == player) {
					MemoryStream packet(9);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::uint8_t>(gemType);
					packet.WriteVariableInt32(newCount);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::PlayerRefreshGems, packet);
					break;
				}
			}
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

			MemoryStream packet(8 + textLength);
			packet.WriteVariableUint32(textId);
			packet.WriteVariableUint32(textLength);
			packet.Write(value.data(), textLength);

			_networkManager->SendTo([this](const Peer& peer) {
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelLoaded);
			}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::OverrideLevelText, packet);
		}
	}

	bool MpLevelHandler::PlayerActionPressed(std::int32_t index, PlayerAction action, bool includeGamepads)
	{
		// TODO: Remove this override
		return LevelHandler::PlayerActionPressed(index, action, includeGamepads);
	}

	bool MpLevelHandler::PlayerActionPressed(std::int32_t index, PlayerAction action, bool includeGamepads, bool& isGamepad)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (std::int32_t)action)) != 0) {
					isGamepad = (it->second.PressedKeys & (1ull << (32 + (std::int32_t)action))) != 0;
					return true;
				}

				isGamepad = false;
				return false;
			}
		}

		return LevelHandler::PlayerActionPressed(index, action, includeGamepads, isGamepad);
	}

	bool MpLevelHandler::PlayerActionHit(std::int32_t index, PlayerAction action, bool includeGamepads)
	{
		// TODO: Remove this override
		return LevelHandler::PlayerActionHit(index, action, includeGamepads);
	}

	bool MpLevelHandler::PlayerActionHit(std::int32_t index, PlayerAction action, bool includeGamepads, bool& isGamepad)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (std::int32_t)action)) != 0 && (it->second.PressedKeysLast & (1ull << (std::int32_t)action)) == 0) {
					isGamepad = (it->second.PressedKeys & (1ull << (32 + (std::int32_t)action))) != 0;
					return true;
				}

				isGamepad = false;
				return false;
			}
		}

		return LevelHandler::PlayerActionHit(index, action, includeGamepads, isGamepad);
	}

	float MpLevelHandler::PlayerHorizontalMovement(std::int32_t index)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (std::int32_t)PlayerAction::Left)) != 0) {
					return -1.0f;
				} else if ((it->second.PressedKeys & (1ull << (std::int32_t)PlayerAction::Right)) != 0) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerHorizontalMovement(index);
	}

	float MpLevelHandler::PlayerVerticalMovement(std::int32_t index)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (std::int32_t)PlayerAction::Up)) != 0) {
					return -1.0f;
				} else if ((it->second.PressedKeys & (1ull << (std::int32_t)PlayerAction::Down)) != 0) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerVerticalMovement(index);
	}

	void MpLevelHandler::PlayerExecuteRumble(std::int32_t index, StringView rumbleEffect)
	{
		if (index > 0) {
			// Ignore remote players
			return;
		}

		return LevelHandler::PlayerExecuteRumble(index, rumbleEffect);
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
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
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

	void MpLevelHandler::SpawnPlayers(const LevelInitialization& levelInit)
	{
		if (!_isServer) {
			// Player spawning is delayed/controlled by server
			return;
		}

		for (std::int32_t i = 0; i < std::int32_t(arraySize(levelInit.PlayerCarryOvers)); i++) {
			if (levelInit.PlayerCarryOvers[i].Type == PlayerType::None) {
				continue;
			}

			Vector2f spawnPosition;
			if (!_multiplayerSpawnPoints.empty()) {
				// TODO: Select spawn according to team
				spawnPosition = _multiplayerSpawnPoints[Random().Next(0, _multiplayerSpawnPoints.size())].Pos;
			} else {
				spawnPosition = _eventMap->GetSpawnPosition(levelInit.PlayerCarryOvers[i].Type);
				if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
					spawnPosition = _eventMap->GetSpawnPosition(PlayerType::Jazz);
					if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
						continue;
					}
				}
			}

			std::shared_ptr<Actors::Multiplayer::LocalPlayerOnServer> player = std::make_shared<Actors::Multiplayer::LocalPlayerOnServer>();
			std::uint8_t playerParams[2] = { (std::uint8_t)levelInit.PlayerCarryOvers[i].Type, (std::uint8_t)i };
			player->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i((std::int32_t)spawnPosition.X + (i * 30), (std::int32_t)spawnPosition.Y - (i * 30), PlayerZ - i),
				playerParams
			));

			Actors::Multiplayer::LocalPlayerOnServer* ptr = player.get();
			_players.push_back(ptr);
			AddActor(player);
			AssignViewport(ptr);

			ptr->ReceiveLevelCarryOver(levelInit.LastExitType, levelInit.PlayerCarryOvers[i]);
		}

		ApplyGameModeToAllPlayers(_networkManager->GameMode);
	}

	MpGameMode MpLevelHandler::GetGameMode() const
	{
		return _networkManager->GameMode;
	}

	bool MpLevelHandler::SetGameMode(MpGameMode value)
	{
		if (!_isServer) {
			return false;
		}

		_networkManager->GameMode = value;

		ApplyGameModeToAllPlayers(_networkManager->GameMode);

		// TODO: Send new teamId to each player
		// TODO: Reset level and broadcast it to players
		std::uint8_t packet[1] = { (std::uint8_t)_networkManager->GameMode };
		_networkManager->SendTo([this](const Peer& peer) {
			auto it = _peerDesc.find(peer);
			return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChangeGameMode, packet);

		return true;
	}

	bool MpLevelHandler::OnPeerDisconnected(const Peer& peer)
	{
		if (_isServer) {
			auto it = _peerDesc.find(peer);
			if (it != _peerDesc.end()) {
				Actors::Player* player = it->second.Player;
				_peerDesc.erase(it);

				if (player != nullptr) {
					std::int32_t playerIndex = player->_playerIndex;

					for (std::size_t i = 0; i < _players.size(); i++) {
						if (_players[i] == player) {
							_players.eraseUnordered(i);
							break;
						}
					}

					it->second.Player->SetState(Actors::ActorState::IsDestroyed, true);
					_playerStates.erase(playerIndex);

					MemoryStream packet(4);
					packet.WriteVariableUint32(playerIndex);

					_networkManager->SendTo([this, otherPeer = peer](const Peer& peer) {
						return (peer != otherPeer);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::DestroyRemoteActor, packet);
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
					std::uint8_t flags = 0;
					if (PreferencesCache::EnableReforgedGameplay) {
						flags |= 0x01;
					}
					if (PreferencesCache::EnableLedgeClimb) {
						flags |= 0x02;
					}

					MemoryStream packet(10 + _episodeName.size() + _levelFileName.size());
					packet.WriteValue<std::uint8_t>(flags);
					packet.WriteValue<std::uint8_t>((std::uint8_t)_networkManager->GameMode);
					packet.WriteVariableUint32(_episodeName.size());
					packet.Write(_episodeName.data(), _episodeName.size());
					packet.WriteVariableUint32(_levelFileName.size());
					packet.Write(_levelFileName.data(), _levelFileName.size());

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::LoadLevel, packet);
					return true;
				}
				case ClientPacketType::LevelReady: {
					MemoryStream packet(data);
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ClientPacketType::LevelReady - peer: 0x%p, flags: 0x%02x, ", peer._enet, flags);

					bool enableLedgeClimb = (flags & 0x02) != 0;
					_peerDesc[peer] = LevelPeerDesc(nullptr, LevelPeerState::LevelLoaded, enableLedgeClimb);

					auto* globalPeerDesc = _networkManager->GetPeerDescriptor(peer);
					if (globalPeerDesc == nullptr || globalPeerDesc->PreferredPlayerType == PlayerType::None) {
						// Show in-game lobby only to newly connected players
						std::uint8_t flags = 0x01 | 0x02 | 0x04; // Set Visibility | Show | SetLobbyMessage
						// TODO: Allowed characters
						std::uint8_t allowedCharacters = 0x01 | 0x02 | 0x04; // Jazz | Spaz | Lori

						MemoryStream packet(6 + _lobbyMessage.size());
						packet.WriteValue<std::uint8_t>(flags);
						packet.WriteValue<std::uint8_t>(allowedCharacters);
						packet.WriteVariableUint32(_lobbyMessage.size());
						packet.Write(_lobbyMessage.data(), _lobbyMessage.size());

						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ShowInGameLobby, packet);
					}
					return true;
				}
				case ClientPacketType::ChatMessage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _peerDesc.find(peer);
					auto it2 = _playerStates.find(playerIndex);
					if (it == _peerDesc.end() || it2 == _playerStates.end()) {
						LOGD("[MP] ClientPacketType::ChatMessage - invalid playerIndex (%u)", playerIndex);
						return true;
					}

					auto player = it->second.Player;
					if (playerIndex != player->_playerIndex) {
						LOGD("[MP] ClientPacketType::ChatMessage - received playerIndex %u instead of %i", playerIndex, player->_playerIndex);
						return true;
					}

					std::uint8_t reserved = packet.ReadValue<std::uint8_t>();

					std::uint32_t messageLength = packet.ReadVariableUint32();
					if (messageLength == 0 && messageLength > 1024) {
						LOGD("[MP] ClientPacketType::ChatMessage - length out of bounds (%u)", messageLength);
						return true;
					}

					String message{NoInit, messageLength};
					packet.Read(message.data(), messageLength);

					MemoryStream packetOut(9 + message.size());
					packetOut.WriteVariableUint32(playerIndex);
					packetOut.WriteValue<std::uint8_t>(0); // Reserved
					packetOut.WriteVariableUint32((std::uint32_t)message.size());
					packetOut.Write(message.data(), (std::uint32_t)message.size());

					_networkManager->SendTo([this, otherPeer = peer](const Peer& peer) {
						auto it = _peerDesc.find(peer);
						return (it != _peerDesc.end() && it->second.State != LevelPeerState::Unknown && peer != otherPeer);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ChatMessage, packetOut);

					_root->InvokeAsync([this, peer, message = std::move(message)]() {
						_console->WriteLine(UI::MessageLevel::Info, message);
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ClientPacketType::PlayerReady: {
					MemoryStream packet(data);
					PlayerType preferredPlayerType = (PlayerType)packet.ReadValue<std::uint8_t>();
					std::uint8_t playerNameLength = packet.ReadValue<std::uint8_t>();

					// TODO: Sanitize (\n,\r,\t) and strip formatting (\f) from player name
					if (playerNameLength == 0 || playerNameLength > MaxPlayerNameLength) {
						LOGD("[MP] ClientPacketType::PlayerReady - player name length out of bounds (%u)", playerNameLength);
						return true;
					}

					String playerName{NoInit, playerNameLength};
					packet.Read(playerName.data(), playerNameLength);

					auto it = _peerDesc.find(peer);
					if (it == _peerDesc.end() || (it->second.State != LevelPeerState::LevelLoaded && it->second.State != LevelPeerState::LevelSynchronized)) {
						LOGD("[MP] ClientPacketType::PlayerReady - invalid state");
						return true;
					}

					// TODO
					if (preferredPlayerType != PlayerType::Jazz && preferredPlayerType != PlayerType::Spaz && preferredPlayerType != PlayerType::Lori) {
						LOGD("[MP] ClientPacketType::PlayerReady - invalid preferred player type");
						return true;
					}

					// Allow to set player name only once
					auto* globalPeerDesc = _networkManager->GetPeerDescriptor(peer);	
					if (globalPeerDesc->PreferredPlayerType == PlayerType::None) {
						globalPeerDesc->PlayerName = Death::move(playerName);
					}
					globalPeerDesc->PreferredPlayerType = preferredPlayerType;

					_root->InvokeAsync([this, peer]() {
						auto it = _peerDesc.find(peer);
						if (it == _peerDesc.end() || (it->second.State != LevelPeerState::LevelLoaded && it->second.State != LevelPeerState::LevelSynchronized)) {
							LOGD("[MP] ClientPacketType::PlayerReady - invalid state");
							return;
						}
						if (it->second.State == LevelPeerState::LevelSynchronized) {
							it->second.State = LevelPeerState::PlayerReady;
						}
					}, NCINE_CURRENT_FUNCTION);
					return true;
				}
				case ClientPacketType::PlayerUpdate: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _peerDesc.find(peer);
					auto it2 = _playerStates.find(playerIndex);
					if (it == _peerDesc.end() || it2 == _playerStates.end()) {
						LOGD("[MP] ClientPacketType::PlayerUpdate - invalid playerIndex (%u)", playerIndex);
						return true;
					}

					auto player = it->second.Player;
					if (playerIndex != player->_playerIndex) {
						LOGD("[MP] ClientPacketType::PlayerUpdate - received playerIndex %u instead of %i", playerIndex, player->_playerIndex);
						return true;
					}

					std::uint64_t now = packet.ReadVariableUint64();
					if (it->second.LastUpdated >= now) {
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float speedX = packet.ReadValue<std::int16_t>() / 512.0f;
					float speedY = packet.ReadValue<std::int16_t>() / 512.0f;
					PlayerFlags flags = (PlayerFlags)packet.ReadVariableUint32();

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

					it->second.LastUpdated = now;

					player->SyncWithServer(Vector2f(posX, posY), Vector2f(speedX, speedY),
						(flags & PlayerFlags::IsVisible) != PlayerFlags::None,
						(flags & PlayerFlags::IsFacingLeft) != PlayerFlags::None, 
						(flags & PlayerFlags::IsActivelyPushing) != PlayerFlags::None);
					return true;
				}
				case ClientPacketType::PlayerKeyPress: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _playerStates.find(playerIndex);
					if (it == _playerStates.end()) {
						return true;
					}
					
					std::uint32_t frameCount = theApplication().GetFrameCount();
					if (it->second.UpdatedFrame != frameCount) {
						it->second.UpdatedFrame = frameCount;
						it->second.PressedKeysLast = it->second.PressedKeys;
					}
					it->second.PressedKeys = packet.ReadVariableUint64();

					//LOGD("Player %i pressed 0x%08x, last state was 0x%08x", playerIndex, it->second.PressedKeys & 0xffffffffu, prevState);
					return true;
				}
			}
		} else {
			switch ((ServerPacketType)packetType) {
				case ServerPacketType::LoadLevel: {
					// Start to ignore all incoming packets, because they no longer belong to this handler
					_ignorePackets = true;

					LOGD("[MP] ServerPacketType::LoadLevel");
					break;
				}
				case ServerPacketType::ShowInGameLobby: {
					MemoryStream packet(data);
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::uint8_t allowedPlayerTypes = packet.ReadValue<std::uint8_t>();

					if (flags & 0x04) {
						std::uint32_t lobbyMessageLength = packet.ReadVariableUint32();
						_lobbyMessage = String(NoInit, lobbyMessageLength);
						packet.Read(_lobbyMessage.data(), lobbyMessageLength);
					}

					LOGD("[MP] ServerPacketType::ShowInGameLobby - flags: 0x%02x, allowedPlayerTypes: 0x%02x, message: \"%s\"", flags, allowedPlayerTypes, _lobbyMessage.data());

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
				case ServerPacketType::ChangeGameMode: {
					MemoryStream packet(data);
					MpGameMode gameMode = (MpGameMode)packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::ChangeGameMode - mode: %u", (std::uint32_t)gameMode);

					_networkManager->GameMode = gameMode;
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
						std::unique_lock lock(_lock);
						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							// TODO: gain, pitch, ...
							it->second->PlaySfx(identifier, gain, pitch);
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
					std::uint32_t messageLength = packet.ReadVariableUint32();
					String message = String(NoInit, messageLength);
					packet.Read(message.data(), messageLength);

					LOGD("[MP] ServerPacketType::ShowAlert - text: \"%s\"", message.data());

					_hud->ShowLevelText(message);
					return true;
				}
				case ServerPacketType::ChatMessage: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					std::uint8_t reserved = packet.ReadValue<std::uint8_t>();

					std::uint32_t messageLength = packet.ReadVariableUint32();
					if (messageLength == 0 && messageLength > 1024) {
						LOGD("[MP] ServerPacketType::ChatMessage - length out of bounds (%u)", messageLength);
						return true;
					}

					String message{NoInit, messageLength};
					packet.Read(message.data(), messageLength);

					_root->InvokeAsync([this, message = std::move(message)]() mutable {
						_console->WriteLine(UI::MessageLevel::Info, std::move(message));
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

					LOGD("[MP] ServerPacketType::CreateControllablePlayer - playerIndex: %u, playerType: %u, health: %u, flags: %u, team: %u, x: %i, y: %i",
						playerIndex, (std::uint32_t)playerType, health, flags, teamId, posX, posY);

					_lastSpawnedActorId = playerIndex;

					_root->InvokeAsync([this, playerType, health, teamId, posX, posY]() {
						std::shared_ptr<Actors::Multiplayer::RemotablePlayer> player = std::make_shared<Actors::Multiplayer::RemotablePlayer>();
						std::uint8_t playerParams[2] = { (std::uint8_t)playerType, 0 };
						player->OnActivated(Actors::ActorActivationDetails(
							this,
							Vector3i(posX, posY, PlayerZ),
							playerParams
						));
						player->SetTeamId(teamId);
						player->SetHealth(health);

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
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					std::int32_t posZ = packet.ReadVariableInt32();
					Actors::ActorState state = (Actors::ActorState)packet.ReadVariableUint32();
					std::uint32_t metadataLength = packet.ReadVariableUint32();
					String metadataPath = String(NoInit, metadataLength);
					packet.Read(metadataPath.data(), metadataLength);
					std::uint32_t anim = packet.ReadVariableUint32();

					//LOGD("Remote actor %u created on [%i;%i] with metadata \"%s\"", actorId, posX, posY, metadataPath.data());
					LOGD("[MP] ServerPacketType::CreateRemoteActor - actorId: %u, metadata: \"%s\", x: %i, y: %i", actorId, metadataPath.data(), posX, posY);

					_root->InvokeAsync([this, actorId, posX, posY, posZ, state, metadataPath = std::move(metadataPath), anim]() {
						std::shared_ptr<Actors::Multiplayer::RemoteActor> remoteActor = std::make_shared<Actors::Multiplayer::RemoteActor>();
						remoteActor->OnActivated(Actors::ActorActivationDetails(this, Vector3i(posX, posY, posZ)));
						remoteActor->AssignMetadata(metadataPath, (AnimState)anim, state);

						{
							std::unique_lock lock(_lock);
							_remoteActors[actorId] = remoteActor;
						}
						AddActor(std::static_pointer_cast<Actors::ActorBase>(remoteActor));
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

					//LOGD("Mirrored actor %u created on [%i;%i] with event %u", actorId, tileX * 32 + 16, tileY * 32 + 16, (std::uint32_t)eventType);
					LOGD("[MP] ServerPacketType::CreateMirroredActor - actorId: %u, event: %u, x: %i, y: %i", actorId, (std::uint32_t)eventType, tileX * 32 + 16, tileY * 32 + 16);

					_root->InvokeAsync([this, actorId, eventType, eventParams = std::move(eventParams), actorFlags, tileX, tileY, posZ]() {
						// TODO: Remove const_cast
						std::shared_ptr<Actors::ActorBase> actor =_eventSpawner.SpawnEvent(eventType, const_cast<std::uint8_t*>(eventParams.data()), actorFlags, tileX, tileY, ILevelHandler::SpritePlaneZ);
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

					//LOGD("Remote actor %u destroyed", actorId);
					LOGD("[MP] ServerPacketType::DestroyRemoteActor - actorId: %u", actorId);

					_root->InvokeAsync([this, actorId]() {
						std::unique_lock lock(_lock);
						auto it = _remoteActors.find(actorId);
						if (it != _remoteActors.end()) {
							it->second->SetState(Actors::ActorState::IsDestroyed, true);
							_remoteActors.erase(it);
							_playerNames.erase(actorId);
						} else {
							LOGD("[MP] ServerPacketType::DestroyRemoteActor - NOT FOUND - actorId: %u", actorId);
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

						float posX, posY, rotation;
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
							rendererType = (Actors::ActorRendererType)packet.ReadValue<std::uint8_t>();
						} else {
							anim = AnimState::Idle;
							rotation = 0.0f;
							rendererType = Actors::ActorRendererType::Default;
						}

						auto it = _remoteActors.find(index);
						if (it != _remoteActors.end()) {
							if (auto* remoteActor = runtime_cast<Actors::Multiplayer::RemoteActor*>(it->second)) {
								if (positionChanged) {
									remoteActor->SyncPositionWithServer(Vector2f(posX, posY));
								}
								if (animationChanged) {
									remoteActor->SyncAnimationWithServer(anim, rotation, rendererType);
								}
								remoteActor->SyncMiscWithServer((flags & 0x04) != 0, (flags & 0x08) != 0, (flags & 0x10) != 0);
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

					_root->InvokeAsync([this, actorId, playerName = std::move(playerName)]() {
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
				case ServerPacketType::PlayerRespawn: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRespawn - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					LOGD("[MP] ServerPacketType::PlayerRespawn - playerIndex: %u", playerIndex);

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					_players[0]->Respawn(Vector2f(posX, posY));
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
						static_cast<Actors::Multiplayer::RemotablePlayer*>(_players[0])->MoveRemotely(Vector2f(posX, posY), Vector2f(speedX, speedY));
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

					_players[0]->EmitWeaponFlare();
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

					_players[0]->SetCurrentWeapon((WeaponType)weaponType);
					return true;
				}
				case ServerPacketType::PlayerRefreshAmmo: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRefreshAmmo - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();
					std::uint16_t weaponAmmo = packet.ReadValue<std::uint16_t>();

					LOGD("[MP] ServerPacketType::PlayerRefreshAmmo - playerIndex: %u, weaponType: %u, weaponAmmo: %u", playerIndex, weaponType, weaponAmmo);

					_players[0]->_weaponAmmo[weaponType] = weaponAmmo;
					return true;
				}
				case ServerPacketType::PlayerRefreshWeaponUpgrades: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRefreshWeaponUpgrades - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::uint8_t weaponType = packet.ReadValue<std::uint8_t>();
					std::uint8_t weaponUpgrades = packet.ReadValue<std::uint8_t>();

					LOGD("[MP] ServerPacketType::PlayerRefreshWeaponUpgrades - playerIndex: %u, weaponType: %u, weaponUpgrades: %u", playerIndex, weaponType, weaponUpgrades);

					_players[0]->_weaponUpgrades[weaponType] = weaponUpgrades;
					return true;
				}
				case ServerPacketType::PlayerRefreshCoins: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRefreshCoins - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::int32_t newCount = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::PlayerRefreshCoins - playerIndex: %u, newCount: %i", playerIndex, newCount);

					_players[0]->_coins = newCount;
					_hud->ShowCoins(newCount);
					return true;
				}
				case ServerPacketType::PlayerRefreshGems: {
					MemoryStream packet(data);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						LOGD("[MP] ServerPacketType::PlayerRefreshGems - received playerIndex %u instead of %u", playerIndex, _lastSpawnedActorId);
						return true;
					}

					std::uint8_t gemType = packet.ReadValue<std::uint8_t>();
					std::int32_t newCount = packet.ReadVariableInt32();

					LOGD("[MP] ServerPacketType::PlayerRefreshGems - playerIndex: %u, gemType: %u, newCount: %i", playerIndex, gemType, newCount);

					if (gemType < arraySize(_players[0]->_gems)) {
						_players[0]->_gems[gemType] = newCount;
						_hud->ShowGems(gemType, newCount);
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
						_players[0]->TakeDamage(_players[0]->_health - health, pushForce);
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
						bool removeSpecialMove = false;
						_players[0]->OnHitSpring(Vector2f(posX, posY), Vector2f(forceX, forceY), (flags & 0x01) == 0x01, (flags & 0x02) == 0x02, removeSpecialMove);
						if (removeSpecialMove) {
							_players[0]->_controllable = true;
							_players[0]->EndDamagingMove();
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
						static_cast<Actors::Multiplayer::RemotablePlayer*>(_players[0])->WarpIn(exitType);
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
				auto it = _peerDesc.find(peer);
				return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
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

		auto it = _remotingActors.find(actor);
		if (it == _remotingActors.end()) {
			return;
		}

		std::uint32_t actorId = it->second.ActorID;

		MemoryStream packet(4);
		packet.WriteVariableUint32(actorId);

		_networkManager->SendTo([this](const Peer& peer) {
			auto it = _peerDesc.find(peer);
			return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
		}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::DestroyRemoteActor, packet);

		_remotingActors.erase(it);
		_remoteActors.erase(actorId);
	}

	void MpLevelHandler::ProcessEvents(float timeMult)
	{
		// Process events only by server
		if (_isServer) {
			LevelHandler::ProcessEvents(timeMult);
		}
	}

	void MpLevelHandler::PrepareNextLevelInitialization(LevelInitialization& levelInit)
	{
		LevelHandler::PrepareNextLevelInitialization(levelInit);

		// Initialize only local players
		for (std::int32_t i = 1; i < std::int32_t(arraySize(levelInit.PlayerCarryOvers)); i++) {
			levelInit.PlayerCarryOvers[i].Type = PlayerType::None;
		}
	}

	void MpLevelHandler::SynchronizePeers()
	{
		for (auto& [peer, peerDesc] : _peerDesc) {
			if (peerDesc.State == LevelPeerState::LevelLoaded) {
				auto* globalPeerDesc = _networkManager->GetPeerDescriptor(peer);
				if (globalPeerDesc != nullptr && globalPeerDesc->PreferredPlayerType != PlayerType::None) {
					peerDesc.State = LevelPeerState::PlayerReady;
				} else {
					peerDesc.State = LevelPeerState::LevelSynchronized;
				}

				LOGD("[MP] Syncing peer (0x%p)", peer._enet);

				// Synchronize tilemap
				{
					// TODO: Use deflate compression here?
					MemoryStream packet(20 * 1024);
					_tileMap->SerializeResumableToStream(packet);
					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::SyncTileMap, packet);
				}

				// Synchronize actors
				for (Actors::Player* otherPlayer : _players) {
					const auto& metadataPath = otherPlayer->_metadata->Path;

					MemoryStream packet(28 + metadataPath.size());
					packet.WriteVariableUint32(otherPlayer->_playerIndex);
					packet.WriteVariableInt32((std::int32_t)otherPlayer->_pos.X);
					packet.WriteVariableInt32((std::int32_t)otherPlayer->_pos.Y);
					packet.WriteVariableInt32((std::int32_t)otherPlayer->_renderer.layer());
					packet.WriteVariableUint32((std::uint32_t)otherPlayer->_state);
					packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
					packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
					packet.WriteVariableUint32((std::uint32_t)(otherPlayer->_currentTransition != nullptr ? otherPlayer->_currentTransition->State : otherPlayer->_currentAnimation->State));

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
					
					// TODO: Send player name
					if (otherPlayer->_playerIndex == 0) {
						// TODO: Server player name
						String playerName = "Server"_s;

						MemoryStream packet(5 + playerName.size());
						packet.WriteVariableUint32(otherPlayer->_playerIndex);
						packet.WriteValue<std::uint8_t>((std::uint8_t)playerName.size());
						packet.Write(playerName.data(), (std::uint32_t)playerName.size());

						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet);
					} else {
						for (const auto& [otherPeer, otherPeerDesc] : _peerDesc) {
							if (otherPeerDesc.Player == otherPlayer) {
								auto* otherGlobalPeerDesc = _networkManager->GetPeerDescriptor(otherPeer);

								MemoryStream packet(5 + otherGlobalPeerDesc->PlayerName.size());
								packet.WriteVariableUint32(otherPlayer->_playerIndex);
								packet.WriteValue<std::uint8_t>((std::uint8_t)otherGlobalPeerDesc->PlayerName.size());
								packet.Write(otherGlobalPeerDesc->PlayerName.data(), (std::uint32_t)otherGlobalPeerDesc->PlayerName.size());

								_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet);
								break;
							}
						}
					}
				}

				for (const auto& [remotingActor, remotingActorInfo] : _remotingActors) {
					if (ActorShouldBeMirrored(remotingActor)) {
						Vector2i originTile = remotingActor->_originTile;
						const auto& eventTile = _eventMap->GetEventTile(originTile.X, originTile.Y);
						if (eventTile.Event != EventType::Empty) {
							for (const auto& [peer, peerDesc] : _peerDesc) {
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
						}
					} else {
						const auto& metadataPath = remotingActor->_metadata->Path;

						MemoryStream packet(28 + metadataPath.size());
						packet.WriteVariableUint32(remotingActorInfo.ActorID);
						packet.WriteVariableInt32((std::int32_t)remotingActor->_pos.X);
						packet.WriteVariableInt32((std::int32_t)remotingActor->_pos.Y);
						packet.WriteVariableInt32((std::int32_t)remotingActor->_renderer.layer());
						packet.WriteVariableUint32((std::uint32_t)remotingActor->_state);
						packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
						packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
						packet.WriteVariableUint32((std::uint32_t)(remotingActor->_currentTransition != nullptr ? remotingActor->_currentTransition->State : remotingActor->_currentAnimation->State));

						_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
					}
				}
			} else if (peerDesc.State == LevelPeerState::PlayerReady) {
				peerDesc.State = LevelPeerState::PlayerSpawned;

				Vector2f spawnPosition;
				if (!_multiplayerSpawnPoints.empty()) {
					// TODO: Select spawn according to team
					spawnPosition = _multiplayerSpawnPoints[Random().Next(0, _multiplayerSpawnPoints.size())].Pos;
				} else {
					// TODO: Spawn on the last checkpoint
					spawnPosition = EventMap()->GetSpawnPosition(PlayerType::Jazz);
					if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
						continue;
					}
				}

				std::uint8_t playerIndex = FindFreePlayerId();
				LOGD("[MP] Spawning player %u (0x%p)", playerIndex, peer._enet);

				auto* globalPeerDesc = _networkManager->GetPeerDescriptor(peer);

				std::shared_ptr<Actors::Multiplayer::RemotePlayerOnServer> player = std::make_shared<Actors::Multiplayer::RemotePlayerOnServer>(peerDesc.EnableLedgeClimb);
				std::uint8_t playerParams[2] = { (std::uint8_t)globalPeerDesc->PreferredPlayerType, (std::uint8_t)playerIndex };
				player->OnActivated(Actors::ActorActivationDetails(
					this,
					Vector3i((std::int32_t)spawnPosition.X, (std::int32_t)spawnPosition.Y, PlayerZ - playerIndex),
					playerParams
				));

				Actors::Multiplayer::RemotePlayerOnServer* ptr = player.get();
				_players.push_back(ptr);
				peerDesc.Player = ptr;
				_playerStates[playerIndex] = PlayerState(player->_pos, player->_speed);

				_suppressRemoting = true;
				AddActor(player);
				_suppressRemoting = false;

				ApplyGameModeToPlayer(_networkManager->GameMode, ptr);

				// Spawn the player also on the remote side
				{
					std::uint8_t flags = 0;
					if (player->_controllable) {
						flags |= 0x01;
					}

					MemoryStream packet(16);
					packet.WriteVariableUint32(playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_playerType);
					packet.WriteValue<std::uint8_t>((std::uint8_t)player->_health);
					packet.WriteValue<std::uint8_t>(flags);
					packet.WriteValue<std::uint8_t>(player->GetTeamId());
					packet.WriteVariableInt32((std::int32_t)player->_pos.X);
					packet.WriteVariableInt32((std::int32_t)player->_pos.Y);

					_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateControllablePlayer, packet);
				}

				// Create the player also on all other clients
				{
					const auto& metadataPath = player->_metadata->Path;

					MemoryStream packet(28 + metadataPath.size());
					packet.WriteVariableUint32(playerIndex);
					packet.WriteVariableInt32((std::int32_t)player->_pos.X);
					packet.WriteVariableInt32((std::int32_t)player->_pos.Y);
					packet.WriteVariableInt32((std::int32_t)player->_renderer.layer());
					packet.WriteVariableUint32((std::uint32_t)player->_state);
					packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
					packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
					packet.WriteVariableUint32((std::uint32_t)(player->_currentTransition != nullptr ? player->_currentTransition->State : player->_currentAnimation->State));

					_networkManager->SendTo([this, self = peer](const Peer& peer) {
						if (peer == self) {
							return false;
						}
						auto it = _peerDesc.find(peer);
						return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::CreateRemoteActor, packet);
				}

				// TODO: Send player name
				{
					MemoryStream packet(5 + globalPeerDesc->PlayerName.size());
					packet.WriteVariableUint32(playerIndex);
					packet.WriteValue<std::uint8_t>((std::uint8_t)globalPeerDesc->PlayerName.size());
					packet.Write(globalPeerDesc->PlayerName.data(), (std::uint32_t)globalPeerDesc->PlayerName.size());

					_networkManager->SendTo([this, self = peer](const Peer& peer) {
						if (peer == self) {
							return false;
						}
						auto it = _peerDesc.find(peer);
						return (it != _peerDesc.end() && it->second.State >= LevelPeerState::LevelSynchronized);
					}, NetworkChannel::Main, (std::uint8_t)ServerPacketType::MarkRemoteActorAsPlayer, packet);
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
		std::size_t count = _players.size();
		for (std::uint8_t i = 0; i < UINT8_MAX - 1; i++) {
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
		if (gameMode == MpGameMode::Cooperation) {
			// Everyone in single team
			for (auto* player : _players) {
				static_cast<Actors::Multiplayer::PlayerOnServer*>(player)->SetTeamId(0);
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
				std::uint8_t teamId = (teamIds[i] < splitIdx ? 0 : 1);
				static_cast<Actors::Multiplayer::PlayerOnServer*>(_players[i])->SetTeamId(teamId);
			}
		} else {
			// Each player is in their own team
			std::int32_t playerCount = (std::int32_t)_players.size();
			for (std::int32_t i = 0; i < playerCount; i++) {
				std::int32_t playerIdx = _players[i]->_playerIndex;
				DEATH_DEBUG_ASSERT(0 <= playerIdx && playerIdx < UINT8_MAX);
				static_cast<Actors::Multiplayer::PlayerOnServer*>(_players[i])->SetTeamId((std::uint8_t)playerIdx);
			}
		}
	}

	void MpLevelHandler::ApplyGameModeToPlayer(MpGameMode gameMode, Actors::Player* player)
	{
		if (gameMode == MpGameMode::Cooperation) {
			// Everyone in single team
			static_cast<Actors::Multiplayer::PlayerOnServer*>(player)->SetTeamId(0);
		} else if (gameMode == MpGameMode::TeamBattle ||
				   gameMode == MpGameMode::CaptureTheFlag ||
				   gameMode == MpGameMode::TeamRace) {
			// Create two teams
			std::int32_t playerCountsInTeam[2] = {};
			std::int32_t playerCount = (std::int32_t)_players.size();
			for (std::int32_t i = 0; i < playerCount; i++) {
				if (_players[i] != player) {
					std::uint8_t teamId = static_cast<Actors::Multiplayer::PlayerOnServer*>(_players[i])->GetTeamId();
					if (teamId < arraySize(playerCountsInTeam)) {
						playerCountsInTeam[teamId]++;
					}
				}
			}
			std::uint8_t teamId = (playerCountsInTeam[0] < playerCountsInTeam[1] ? 0
				: (playerCountsInTeam[0] > playerCountsInTeam[1] ? 1
					: Random().Next(0, 2)));

			static_cast<Actors::Multiplayer::PlayerOnServer*>(player)->SetTeamId(teamId);
		} else {
			// Each player is in their own team
			std::int32_t playerIdx = player->_playerIndex;
			DEATH_DEBUG_ASSERT(0 <= playerIdx && playerIdx < UINT8_MAX);
			static_cast<Actors::Multiplayer::PlayerOnServer*>(player)->SetTeamId((std::uint8_t)playerIdx);
		}
	}

	void MpLevelHandler::SetLobbyMessage(StringView message)
	{
		if (!_isServer) {
			return;
		}

		_lobbyMessage = message;

		for (auto& [peer, peerDesc] : _peerDesc) {
			if (peerDesc.State >= LevelPeerState::LevelLoaded && peerDesc.State >= LevelPeerState::LevelSynchronized) {
				std::uint8_t flags = 0x04; // SetLobbyMessage

				MemoryStream packet(6 + _lobbyMessage.size());
				packet.WriteValue<std::uint8_t>(flags);
				packet.WriteValue<std::uint8_t>(0x00);
				packet.WriteVariableUint32(_lobbyMessage.size());
				packet.Write(_lobbyMessage.data(), _lobbyMessage.size());

				_networkManager->SendTo(peer, NetworkChannel::Main, (std::uint8_t)ServerPacketType::ShowInGameLobby, packet);
			}
		}
	}

	void MpLevelHandler::SetPlayerReady(PlayerType playerType)
	{
		if (_isServer) {
			return;
		}

		_inGameLobby->Hide();

		MemoryStream packet(72);
		packet.WriteValue<std::uint8_t>((std::uint8_t)playerType);

		// TODO
		String playerName;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (PreferencesCache::EnableDiscordIntegration && UI::DiscordRpcClient::Get().IsSupported()) {
			playerName = UI::DiscordRpcClient::Get().GetUserDisplayName();
		}
#endif
		if (playerName.empty()) {
			char buffer[64];
			formatString(buffer, sizeof(buffer), "%x", Random().Next());
			playerName = buffer;
		}
		if (playerName.size() > MaxPlayerNameLength) {
			playerName = playerName.prefix(MaxPlayerNameLength);
		}
		packet.WriteValue<std::uint8_t>((std::uint8_t)playerName.size());
		packet.Write(playerName.data(), (std::uint32_t)playerName.size());

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

	StringView MpLevelHandler::GameModeToString(MpGameMode mode)
	{
		switch (mode) {
			case MpGameMode::Battle: return _("Battle");
			case MpGameMode::TeamBattle: return _("Team Battle");
			case MpGameMode::CaptureTheFlag: return _("Capture The Flag");
			case MpGameMode::Race: return _("Race");
			case MpGameMode::TeamRace: return _("Team Race");
			case MpGameMode::TreasureHunt: return _("Treasure Hunt");
			case MpGameMode::Cooperation: return _("Cooperation");
			default: return _("Unknown");
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

	MpLevelHandler::PlayerState::PlayerState()
	{
	}

	MpLevelHandler::PlayerState::PlayerState(Vector2f pos, Vector2f speed)
		: Flags(PlayerFlags::None), PressedKeys(0), PressedKeysLast(0), UpdatedFrame(0)/*, WarpSeqNum(0), WarpTimeLeft(0.0f)*/
	{
	}
}

#endif