#include "MultiLevelHandler.h"

#if defined(WITH_MULTIPLAYER)

#include "PacketTypes.h"
#include "../PreferencesCache.h"
#include "../UI/ControlScheme.h"
#include "../UI/HUD.h"
#include "../../Common.h"

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
#include "../Actors/RemoteActor.h"
#include "../Actors/SolidObjectBase.h"
#include "../Actors/Enemies/Bosses/BossBase.h"

#include <float.h>

#include <Utf8.h>
#include <Containers/StaticArray.h>
#include <IO/MemoryStream.h>

using namespace nCine;

namespace Jazz2::Multiplayer
{
	MultiLevelHandler::MultiLevelHandler(IRootController* root, NetworkManager* networkManager)
		: LevelHandler(root), _networkManager(networkManager), _updateTimeLeft(1.0f), _initialUpdateSent(false),
			_lastSpawnedActorId(-1), _seqNum(0), _seqNumWarped(0), _suppressRemoting(false), _ignorePackets(false)
	{
		_isServer = (networkManager->GetState() == NetworkState::Listening);
	}

	MultiLevelHandler::~MultiLevelHandler()
	{
	}

	bool MultiLevelHandler::Initialize(const LevelInitialization& levelInit)
	{
		_suppressRemoting = true;
		bool initialized = LevelHandler::Initialize(levelInit);
		_suppressRemoting = false;
		if (!initialized) {
			return false;
		}

		for (std::int32_t i = 0; i < countof(levelInit.PlayerCarryOvers); i++) {
			if (levelInit.PlayerCarryOvers[i].Type != PlayerType::None) {
				_lastSpawnedActorId = i;
			}
		}

		if (_isServer) {
			std::uint8_t flags = 0;
			if (PreferencesCache::EnableReforged) {
				flags |= 0x01;
			}
			MemoryStream packet(10 + _episodeName.size() + _levelFileName.size());
			packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::LoadLevel);
			packet.WriteValue<std::uint8_t>(flags);
			packet.WriteVariableUint32(_episodeName.size());
			packet.Write(_episodeName.data(), _episodeName.size());
			packet.WriteVariableUint32(_levelFileName.size());
			packet.Write(_levelFileName.data(), _levelFileName.size());
			// TODO: Send it to only authenticated peers
			_networkManager->SendToAll(NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
		}

		auto& resolver = ContentResolver::Get();
		resolver.PreloadMetadataAsync("Interactive/PlayerJazz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerSpaz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerLori"_s);
		return true;
	}

	float MultiLevelHandler::GetAmbientLight() const
	{
		return LevelHandler::GetAmbientLight();
	}

	void MultiLevelHandler::SetAmbientLight(float value)
	{
		LevelHandler::SetAmbientLight(value);
	}

	void MultiLevelHandler::OnBeginFrame()
	{
		LevelHandler::OnBeginFrame();

		if ((_pressedActions & 0xffffffffu) != ((_pressedActions >> 32) & 0xffffffffu)) {
			MemoryStream packet(9);
			packet.WriteValue<std::uint8_t>((std::uint8_t)ClientPacketType::PlayerKeyPress);
			packet.WriteVariableUint32(_lastSpawnedActorId);
			packet.WriteVariableUint32((std::uint32_t)(_pressedActions & 0xffffffffu));
			_networkManager->SendToPeer(nullptr, NetworkChannel::UnreliableUpdates, packet.GetBuffer(), packet.GetSize());
		}
	}

	void MultiLevelHandler::OnEndFrame()
	{
		LevelHandler::OnEndFrame();

		float timeMult = theApplication().timeMult();

		_updateTimeLeft -= timeMult;
		if (_updateTimeLeft < 0.0f) {
			_updateTimeLeft = 4.0f;

			if (!_initialUpdateSent) {
				_initialUpdateSent = true;

				if (_isServer) {
					// TODO
				} else {
					MemoryStream packet(5);
					packet.WriteValue<std::uint8_t>((std::uint8_t)ClientPacketType::LevelReady);

					_networkManager->SendToPeer(nullptr, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
				}
			}

			if (_isServer) {
				std::uint32_t actorCount = (std::uint32_t)(_players.size() + _remotingActors.size());

				MemoryStream packet(5 + actorCount * 9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::UpdateAllActors);
				packet.WriteVariableUint32(actorCount);

				for (Actors::Player* player : _players) {
					Vector2f pos;
					auto it = _playerStates.find(player->_playerIndex);
					if (it != _playerStates.end()) {
						// Remote players
						UpdatePlayerLocalPos(player, it->second, timeMult);
						pos = player->_pos;

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
					}

					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 512.0f));
					packet.WriteVariableUint32((std::uint32_t)(player->_currentTransition != nullptr ? player->_currentTransition->State : player->_currentAnimation->State));

					std::uint8_t flags = 0;
					if (player->IsFacingLeft()) {
						flags |= 0x01;
					}
					if (player->_renderer.isDrawEnabled()) {
						flags |= 0x02;
					}
					packet.WriteValue<std::uint8_t>(flags);
				}

				for (const auto& [remotingActor, remotingActorId] : _remotingActors) {
					packet.WriteVariableUint32(remotingActorId);
					packet.WriteValue<std::int32_t>((std::int32_t)(remotingActor->_pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(remotingActor->_pos.Y * 512.0f));
					packet.WriteVariableUint32((std::uint32_t)(remotingActor->_currentTransition != nullptr ? remotingActor->_currentTransition->State : (remotingActor->_currentAnimation != nullptr ? remotingActor->_currentAnimation->State : AnimState::Idle)));

					std::uint8_t flags = 0;
					if (remotingActor->IsFacingLeft()) {
						flags |= 0x01;
					}
					if (remotingActor->_renderer.isDrawEnabled()) {
						flags |= 0x02;
					}
					packet.WriteValue<std::uint8_t>(flags);
				}

				_networkManager->SendToAll(NetworkChannel::UnreliableUpdates, packet.GetBuffer(), packet.GetSize());

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
					packet.WriteValue<std::uint8_t>((std::uint8_t)ClientPacketType::PlayerUpdate);
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

					_networkManager->SendToPeer(nullptr, NetworkChannel::UnreliableUpdates, packet.GetBuffer(), packet.GetSize());
				}
			}
		}

#if defined(DEATH_DEBUG) && defined(WITH_IMGUI)
		if (PreferencesCache::ShowPerformanceMetrics) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();

			if (_isServer) {
				for (Actors::Player* player : _players) {
					char actorIdString[32];

					Vector2f pos;
					auto it = _playerStates.find(player->_playerIndex);
					if (it != _playerStates.end()) {
						for (std::size_t i = 0; i < countof(it->second.StateBuffer); i++) {
							auto posFrom = WorldPosToScreenSpace(it->second.StateBuffer[i].Pos);
							auto posTo = WorldPosToScreenSpace(it->second.StateBuffer[i].Pos + it->second.StateBuffer[i].Speed);

							drawList->AddLine(posFrom, posTo, ImColor(255, 200, 120, 220), 2.0f);
						}

						std::int32_t prevIdx = it->second.StateBufferPos - 1;
						while (prevIdx < 0) {
							prevIdx += countof(it->second.StateBuffer);
						}

						auto posPrev = WorldPosToScreenSpace(it->second.StateBuffer[prevIdx].Pos);
						auto posLocal = WorldPosToScreenSpace(player->_pos);
						drawList->AddLine(posPrev, posLocal, ImColor(255, 60, 60, 220), 2.0f);

						formatString(actorIdString, countof(actorIdString), "%i [%0.1f] %04x", player->_playerIndex, it->second.DeviationTime, it->second.Flags);
					} else {
						formatString(actorIdString, countof(actorIdString), "%i", player->_playerIndex);
					}

					auto aabbMin = WorldPosToScreenSpace({ player->AABB.L, player->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}

				for (const auto& [actor, actorId] : _remotingActors) {
					char actorIdString[16];
					formatString(actorIdString, countof(actorIdString), "%u", actorId);

					auto aabbMin = WorldPosToScreenSpace({ actor->AABB.L, actor->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}
			} else {
				for (Actors::Player* player : _players) {
					char actorIdString[16];
					formatString(actorIdString, countof(actorIdString), "%i", player->_playerIndex);

					auto aabbMin = WorldPosToScreenSpace({ player->AABB.L, player->AABB.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}

				for (const auto& [actorId, actor] : _remoteActors) {
					char actorIdString[16];
					formatString(actorIdString, countof(actorIdString), "%u", actorId);

					auto aabbMin = WorldPosToScreenSpace({ actor->AABBInner.L, actor->AABBInner.T });
					aabbMin.x += 4.0f;
					aabbMin.y += 3.0f;
					drawList->AddText(aabbMin, ImColor(255, 255, 255), actorIdString);
				}
			}
		}
#endif
	}

	void MultiLevelHandler::OnInitializeViewport(int32_t width, int32_t height)
	{
		LevelHandler::OnInitializeViewport(width, height);
	}

	void MultiLevelHandler::OnKeyPressed(const KeyboardEvent& event)
	{
		LevelHandler::OnKeyPressed(event);
	}

	void MultiLevelHandler::OnKeyReleased(const KeyboardEvent& event)
	{
		LevelHandler::OnKeyReleased(event);
	}

	void MultiLevelHandler::OnTouchEvent(const TouchEvent& event)
	{
		LevelHandler::OnTouchEvent(event);
	}

	void MultiLevelHandler::AddActor(std::shared_ptr<Actors::ActorBase> actor)
	{
		LevelHandler::AddActor(actor);

		if (!_suppressRemoting && _isServer && (actor->_currentAnimation != nullptr || actor->_currentTransition != nullptr)) {
			++_lastSpawnedActorId;
			std::uint32_t actorId = _lastSpawnedActorId;
			_remotingActors[actor.get()] = actorId;

			const auto& metadataPath = actor->_metadata->Path;

			for (const auto& [peer, peerDesc] : _peerDesc) {
				MemoryStream packet(24 + metadataPath.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::CreateRemoteActor);
				packet.WriteVariableUint32(actorId);
				packet.WriteVariableInt32((std::int32_t)actor->_pos.X);
				packet.WriteVariableInt32((std::int32_t)actor->_pos.Y);
				packet.WriteVariableInt32((std::int32_t)actor->_renderer.layer());
				packet.WriteVariableUint32((std::uint32_t)actor->_state);
				packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
				packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
				packet.WriteVariableUint32((std::uint32_t)(actor->_currentTransition != nullptr ? actor->_currentTransition->State : actor->_currentAnimation->State));

				// TODO: If it fail, it will release the packet which is wrong
				_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
			}
		}
	}

	std::shared_ptr<AudioBufferPlayer> MultiLevelHandler::PlaySfx(Actors::ActorBase* self, const StringView& identifier, AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch)
	{
		if (_isServer) {
			std::uint32_t actorId;
			auto it = _remotingActors.find(self);
			if (auto* player = dynamic_cast<Actors::Player*>(self)) {
				actorId = player->_playerIndex;
			} else if (it != _remotingActors.end()) {
				actorId = it->second;
			} else {
				actorId = UINT32_MAX;
			}

			if (actorId != UINT32_MAX) {
				for (const auto& [peer, peerDesc] : _peerDesc) {
					if (self == peerDesc.Player) {
						continue;
					}

					MemoryStream packet(9 + identifier.size());
					packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::PlaySfx);
					packet.WriteVariableUint32(actorId);
					// TODO: sourceRelative
					// TODO: looping
					// TODO: gain
					// TODO: pitch
					packet.WriteVariableUint32((std::uint32_t)identifier.size());
					packet.Write(identifier.data(), (std::uint32_t)identifier.size());

					// TODO: If it fails, it will release the packet which is wrong
					_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
				}
			}
		}

		return LevelHandler::PlaySfx(self, identifier, buffer, pos, sourceRelative, gain, pitch);
	}

	std::shared_ptr<AudioBufferPlayer> MultiLevelHandler::PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain, float pitch)
	{
		if (_isServer) {
			for (const auto& [peer, peerDesc] : _peerDesc) {
				MemoryStream packet(10 + identifier.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::PlayCommonSfx);
				packet.WriteVariableInt32((std::int32_t)pos.X);
				packet.WriteVariableInt32((std::int32_t)pos.Y);
				// TODO: looping
				// TODO: gain
				// TODO: pitch
				packet.WriteVariableUint32((std::uint32_t)identifier.size());
				packet.Write(identifier.data(), (std::uint32_t)identifier.size());

				// TODO: If it fails, it will release the packet which is wrong
				_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
			}
		}

		return LevelHandler::PlayCommonSfx(identifier, pos, gain, pitch);
	}

	void MultiLevelHandler::WarpCameraToTarget(Actors::ActorBase* actor, bool fast)
	{
		LevelHandler::WarpCameraToTarget(actor, fast);
	}

	bool MultiLevelHandler::IsPositionEmpty(Actors::ActorBase* self, const AABBf& aabb, TileCollisionParams& params, Actors::ActorBase** collider)
	{
		return LevelHandler::IsPositionEmpty(self, aabb, params, collider);
	}

	void MultiLevelHandler::FindCollisionActorsByAABB(Actors::ActorBase* self, const AABBf& aabb, const std::function<bool(Actors::ActorBase*)>& callback)
	{
		LevelHandler::FindCollisionActorsByAABB(self, aabb, callback);
	}

	void MultiLevelHandler::FindCollisionActorsByRadius(float x, float y, float radius, const std::function<bool(Actors::ActorBase*)>& callback)
	{
		LevelHandler::FindCollisionActorsByRadius(x, y, radius, callback);
	}

	void MultiLevelHandler::GetCollidingPlayers(const AABBf& aabb, const std::function<bool(Actors::ActorBase*)> callback)
	{
		LevelHandler::GetCollidingPlayers(aabb, callback);
	}

	void MultiLevelHandler::BroadcastTriggeredEvent(Actors::ActorBase* initiator, EventType eventType, uint8_t* eventParams)
	{
		LevelHandler::BroadcastTriggeredEvent(initiator, eventType, eventParams);
	}

	void MultiLevelHandler::BeginLevelChange(ExitType exitType, const StringView& nextLevel)
	{
		if (!_isServer) {
			// Level can be changed only by server
			return;
		}

		LevelHandler::BeginLevelChange(exitType, nextLevel);
	}

	void MultiLevelHandler::HandleGameOver()
	{
		LevelHandler::HandleGameOver();
	}

	bool MultiLevelHandler::HandlePlayerDied(Actors::Player* player)
	{
		return LevelHandler::HandlePlayerDied(player);
	}

	bool MultiLevelHandler::HandlePlayerFireWeapon(Actors::Player* player, WeaponType& weaponType, std::uint16_t& ammoDecrease)
	{
		if (!_isServer) {
			// TODO
			return false;
		}

		return true;
	}

	bool MultiLevelHandler::HandlePlayerSpring(Actors::Player* player, const Vector2f& pos, const Vector2f& force, bool keepSpeedX, bool keepSpeedY)
	{
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

					MemoryStream packet(18);
					packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::PlayerActivateSpring);
					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 512.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(force.X * 512.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(force.Y * 512.0f));
					packet.WriteValue<std::uint8_t>(flags);
					_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
					break;
				}
			}
		}

		return LevelHandler::HandlePlayerSpring(player, pos, force, keepSpeedX, keepSpeedY);
	}

	void MultiLevelHandler::HandlePlayerWarped(Actors::Player* player, const Vector2f& prevPos, bool fast)
	{
		LevelHandler::HandlePlayerWarped(player, prevPos, fast);

		if (_isServer) {
			auto it = _playerStates.find(player->_playerIndex);
			if (it != _playerStates.end()) {
				it->second.Flags |= PlayerFlags::JustWarped;
			}
		} else {
			_seqNumWarped = _seqNum;
		}
	}

	void MultiLevelHandler::SetCheckpoint(const Vector2f& pos)
	{
		LevelHandler::SetCheckpoint(pos);
	}

	void MultiLevelHandler::RollbackToCheckpoint()
	{
		LevelHandler::RollbackToCheckpoint();
	}

	void MultiLevelHandler::ActivateSugarRush()
	{
		LevelHandler::ActivateSugarRush();
	}

	void MultiLevelHandler::ShowLevelText(const StringView& text)
	{
		LevelHandler::ShowLevelText(text);
	}

	void MultiLevelHandler::ShowCoins(int32_t count)
	{
		LevelHandler::ShowCoins(count);
	}

	void MultiLevelHandler::ShowGems(int32_t count)
	{
		LevelHandler::ShowGems(count);
	}

	StringView MultiLevelHandler::GetLevelText(uint32_t textId, int32_t index, uint32_t delimiter)
	{
		return LevelHandler::GetLevelText(textId, index, delimiter);
	}

	void MultiLevelHandler::OverrideLevelText(uint32_t textId, const StringView& value)
	{
		LevelHandler::OverrideLevelText(textId, value);

		if (_isServer) {
			std::uint32_t textLength = (std::uint32_t)value.size();

			MemoryStream packet(9 + textLength);
			packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::OverrideLevelText);
			packet.WriteVariableUint32(textId);
			packet.WriteVariableUint32(textLength);
			packet.Write(value.data(), textLength);

			_networkManager->SendToPeer(nullptr, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
		}
	}

	bool MultiLevelHandler::PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				return (it->second.PressedKeys & (1ull << (int32_t)action)) != 0;
			}
		}

		return LevelHandler::PlayerActionPressed(index, action, includeGamepads);
	}

	bool MultiLevelHandler::PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				isGamepad = false;
				return (it->second.PressedKeys & (1ull << (int32_t)action)) != 0;
			}
		}

		return LevelHandler::PlayerActionPressed(index, action, includeGamepads, isGamepad);
	}

	bool MultiLevelHandler::PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				// TODO
				return (it->second.PressedKeys & (1ull << (int32_t)action)) != 0;
			}
		}

		return LevelHandler::PlayerActionHit(index, action, includeGamepads);
	}

	bool MultiLevelHandler::PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				// TODO
				isGamepad = false;
				return (it->second.PressedKeys & (1ull << (int32_t)action)) != 0;
			}
		}

		return LevelHandler::PlayerActionHit(index, action, includeGamepads, isGamepad);
	}

	float MultiLevelHandler::PlayerHorizontalMovement(int32_t index)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (int32_t)PlayerActions::Left)) != 0) {
					return -1.0f;
				} else if ((it->second.PressedKeys & (1ull << (int32_t)PlayerActions::Right)) != 0) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerHorizontalMovement(index);
	}

	float MultiLevelHandler::PlayerVerticalMovement(int32_t index)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.PressedKeys & (1ull << (int32_t)PlayerActions::Up)) != 0) {
					return -1.0f;
				} else if ((it->second.PressedKeys & (1ull << (int32_t)PlayerActions::Down)) != 0) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerVerticalMovement(index);
	}

	bool MultiLevelHandler::SerializeResumableToStream(Stream& dest)
	{
		return false;
	}

	void MultiLevelHandler::OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount)
	{
		if (_isServer) {
			MemoryStream packet(13);
			packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::AdvanceTileAnimation);
			packet.WriteVariableInt32(tx);
			packet.WriteVariableInt32(ty);
			packet.WriteVariableInt32(amount);

			_networkManager->SendToAll(NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
		}
	}

	void MultiLevelHandler::AttachComponents(LevelDescriptor&& descriptor)
	{
		LevelHandler::AttachComponents(std::move(descriptor));

		if (!_isServer) {
			Vector2i size = _eventMap->GetSize();
			for (std::int32_t y = 0; y < size.Y; y++) {
				for (std::int32_t x = 0; x < size.X; x++) {
					std::uint8_t* eventParams;
					switch (_eventMap->GetEventByPosition(x, y, &eventParams)) {
						case EventType::WarpOrigin:
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

	bool MultiLevelHandler::OnPeerDisconnected(const Peer& peer)
	{
		if (_isServer) {
			auto it = _peerDesc.find(peer);
			if (it != _peerDesc.end()) {
				Actors::Player* player = it->second.Player;
				std::int32_t playerIndex = player->_playerIndex;

				for (std::size_t i = 0; i < _players.size(); i++) {
					if (_players[i] == player) {
						_players.erase(_players.begin() + i);
						break;
					}
				}

				it->second.Player->SetState(Actors::ActorState::IsDestroyed, true);
				_peerDesc.erase(it);

				_playerStates.erase(playerIndex);

				for (auto& [otherPeer, otherPeerDesc] : _peerDesc) {
					if (otherPeer == peer) {
						continue;
					}

					MemoryStream packet(5);
					packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::DestroyRemoteActor);
					packet.WriteVariableUint32(playerIndex);

					// TODO: If it fails, it will release the packet which is wrong
					_networkManager->SendToPeer(otherPeer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
				}
			}
			return true;
		}

		return false;
	}

	bool MultiLevelHandler::OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength)
	{
		if (_ignorePackets) {
			return false;
		}

		if (_isServer) {
			auto packetType = (ClientPacketType)data[0];
			switch (packetType) {
				case ClientPacketType::Auth: {
					std::uint8_t flags = 0;
					if (PreferencesCache::EnableReforged) {
						flags |= 0x01;
					}

					MemoryStream packet(10 + _episodeName.size() + _levelFileName.size());
					packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::LoadLevel);
					packet.WriteValue<std::uint8_t>(flags);
					packet.WriteVariableUint32(_episodeName.size());
					packet.Write(_episodeName.data(), _episodeName.size());
					packet.WriteVariableUint32(_levelFileName.size());
					packet.Write(_levelFileName.data(), _levelFileName.size());

					_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
					return true;
				}
				case ClientPacketType::LevelReady: {
					_peerDesc[peer] = PeerDesc(nullptr, PeerState::LevelLoaded);
					return true;
				}
				case ClientPacketType::PlayerUpdate: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _peerDesc.find(peer);
					auto it2 = _playerStates.find(playerIndex);
					if (it == _peerDesc.end() || it2 == _playerStates.end()) {
						return true;
					}

					auto player = it->second.Player;
					if (playerIndex != player->_playerIndex) {
						LOGW("PlayerUpdate packet received with wrong player index %i instead of %i", playerIndex, player->_playerIndex);
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

					bool justWarped = (flags & PlayerFlags::JustWarped) == PlayerFlags::JustWarped;
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

							_networkManager->SendToPeer(peer, NetworkChannel::Main, packet2.GetBuffer(), packet2.GetSize());
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

							_networkManager->SendToPeer(peer, NetworkChannel::Main, packet2.GetBuffer(), packet2.GetSize());
						}
					}

					player->SetFacingLeft((flags & PlayerFlags::IsFacingLeft) != PlayerFlags::None);
					player->_renderer.setDrawEnabled((flags & PlayerFlags::IsVisible) != PlayerFlags::None);
					player->_isActivelyPushing = (flags & PlayerFlags::IsActivelyPushing) != PlayerFlags::None;
					// TODO: Special move

					it->second.LastUpdated = now;

					OnRemotePlayerPosReceived(it2->second, Vector2f(posX, posY), Vector2f(speedX, speedY), flags);
					return true;
				}
				case ClientPacketType::PlayerKeyPress: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _playerStates.find(playerIndex);
					if (it == _playerStates.end()) {
						return true;
					}

					it->second.PressedKeys = packet.ReadVariableUint32();
					return true;
				}
				case ClientPacketType::PlayerFireWeapon: {
					// TODO
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _peerDesc.find(peer);
					auto it2 = _playerStates.find(playerIndex);
					if (it == _peerDesc.end() || it2 == _playerStates.end()) {
						return true;
					}

					auto player = it->second.Player;
					if (playerIndex != player->_playerIndex) {
						LOGW("PlayerFireWeapon packet received with wrong player index %i instead of %i", playerIndex, player->_playerIndex);
						return true;
					}

					WeaponType weaponType = (WeaponType)packet.ReadValue<std::uint8_t>();
					
					LOGW("[TODO] Player %i fires weapon %i", playerIndex, (std::int32_t)weaponType);
					return true;
				}
			}
		} else {
			auto packetType = (ServerPacketType)data[0];
			switch (packetType) {
				case ServerPacketType::LoadLevel: {
					// Start to ignore all incoming packets, because they no longer belong to this handler
					_ignorePackets = true;
					break;
				}
				case ServerPacketType::PlaySfx: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t actorId = packet.ReadVariableUint32();
					std::uint32_t identifierLength = packet.ReadVariableUint32();
					String identifier = String(NoInit, identifierLength);
					packet.Read(identifier.data(), identifierLength);
					
					auto it = _remoteActors.find(actorId);
					if (it != _remoteActors.end()) {
						// TODO: gain, pitch, ...
						it->second->PlaySfx(identifier);
					}
					break;
				}
				case ServerPacketType::PlayCommonSfx: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					std::uint32_t identifierLength = packet.ReadVariableUint32();
					String identifier = String(NoInit, identifierLength);
					packet.Read(identifier.data(), identifierLength);
					// TODO: gain, pitch, ...
					PlayCommonSfx(identifier, Vector3f(posX, posY, 0.0f));
					break;
				}
				case ServerPacketType::ShowMessage: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t messageLength = packet.ReadVariableUint32();
					String message = String(NoInit, messageLength);
					packet.Read(message.data(), messageLength);
					_hud->ShowLevelText(message);
					break;
				}
				case ServerPacketType::CreateControllablePlayer: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					PlayerType playerType = (PlayerType)packet.ReadValue<std::uint8_t>();
					std::uint8_t health = packet.ReadValue<std::uint8_t>();
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();

					std::shared_ptr<Actors::Player> player = std::make_shared<Actors::Player>();
					uint8_t playerParams[2] = { (uint8_t)playerType, 0 };
					player->OnActivated(Actors::ActorActivationDetails(
						this,
						Vector3i(posX, posY, PlayerZ),
						playerParams
					));
					player->SetHealth(health);

					Actors::Player* ptr = player.get();
					_players.push_back(ptr);
					AddActor(player);

					_lastSpawnedActorId = playerIndex;
					return true;
				}
				case ServerPacketType::CreateRemoteActor: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t actorId = packet.ReadVariableUint32();
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					std::int32_t posZ = packet.ReadVariableInt32();
					Actors::ActorState state = (Actors::ActorState)packet.ReadVariableUint32();
					std::uint32_t metadataLength = packet.ReadVariableUint32();
					String metadataPath = String(NoInit, metadataLength);
					packet.Read(metadataPath.data(), metadataLength);
					std::uint32_t anim = packet.ReadVariableUint32();

					std::shared_ptr<Actors::RemoteActor> remoteActor = std::make_shared<Actors::RemoteActor>();
					remoteActor->OnActivated(Actors::ActorActivationDetails(this, Vector3i(posX, posY, posZ)));
					remoteActor->AssignMetadata(metadataPath, (AnimState)anim, state);

					_remoteActors[actorId] = remoteActor;
					AddActor(std::static_pointer_cast<Actors::ActorBase>(remoteActor));

					LOGD("Remote actor %u created on [%i;%i] with metadata \"%s\"", actorId, posX, posY, metadataPath.data());
					return true;
				}
				case ServerPacketType::DestroyRemoteActor: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t actorId = packet.ReadVariableUint32();

					auto it = _remoteActors.find(actorId);
					if (it != _remoteActors.end()) {
						it->second->SetState(Actors::ActorState::IsDestroyed, true);
						_remoteActors.erase(it);
					}

					LOGD("Remote actor %u destroyed", actorId);
					return true;
				}
				case ServerPacketType::UpdateAllActors: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t actorCount = packet.ReadVariableUint32();
					for (std::uint32_t i = 0; i < actorCount; i++) {
						std::uint32_t index = packet.ReadVariableUint32();
						float posX = packet.ReadValue<std::int32_t>() / 512.0f;
						float posY = packet.ReadValue<std::int32_t>() / 512.0f;
						std::uint32_t anim = packet.ReadVariableUint32();
						std::uint8_t flags = packet.ReadValue<std::uint8_t>();

						auto it = _remoteActors.find(index);
						if (it != _remoteActors.end()) {
							it->second->SyncWithServer(Vector2f(posX, posY), (AnimState)anim, (flags & 0x02) != 0, (flags & 0x01) != 0);
						}
					}
					return true;
				}
				case ServerPacketType::SetTrigger: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint8_t triggerId = packet.ReadValue<std::uint8_t>();
					bool newState = (bool)packet.ReadValue<std::uint8_t>();
					TileMap()->SetTrigger(triggerId, newState);
					return true;
				}
				case ServerPacketType::AdvanceTileAnimation: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::int32_t tx = packet.ReadVariableInt32();
					std::int32_t ty = packet.ReadVariableInt32();
					std::int32_t amount = packet.ReadVariableInt32();
					TileMap()->AdvanceDestructibleTileAnimation(tx, ty, amount);
					return true;
				}
				case ServerPacketType::PlayerMoveInstantly: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float speedX = packet.ReadValue<std::int16_t>() / 512.0f;
					float speedY = packet.ReadValue<std::int16_t>() / 512.0f;

					_players[0]->_speed = Vector2f(speedX, speedY);
					_players[0]->MoveInstantly(Vector2f(posX, posY), Actors::MoveType::Absolute);
					return true;
				}
				case ServerPacketType::PlayerAckWarped: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					std::uint64_t seqNum = packet.ReadVariableUint64();
					if (_lastSpawnedActorId == playerIndex && _seqNumWarped == seqNum) {
						_seqNumWarped = 0;
					}
					return true;
				}
				case ServerPacketType::PlayerActivateSpring: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastSpawnedActorId != playerIndex) {
						return true;
					}

					float posX = packet.ReadValue<std::int32_t>() / 512.0f;
					float posY = packet.ReadValue<std::int32_t>() / 512.0f;
					float forceX = packet.ReadValue<std::int16_t>() / 512.0f;
					float forceY = packet.ReadValue<std::int16_t>() / 512.0f;
					std::uint8_t flags = packet.ReadValue<std::uint8_t>();
					bool removeSpecialMove = false;
					_players[0]->OnHitSpring(Vector2f(posX, posY), Vector2f(forceX, forceY), (flags & 0x01) == 0x01, (flags & 0x02) == 0x02, removeSpecialMove);
					if (removeSpecialMove) {
						_players[0]->_controllable = true;
						_players[0]->EndDamagingMove();
					}
					return true;
				}
			}
		}

		return false;
	}

	void MultiLevelHandler::LimitCameraView(int left, int width)
	{
		// TODO: This should probably be client local
		LevelHandler::LimitCameraView(left, width);
	}

	void MultiLevelHandler::ShakeCameraView(float duration)
	{
		// TODO: This should probably be client local
		LevelHandler::ShakeCameraView(duration);
	}

	void MultiLevelHandler::SetTrigger(std::uint8_t triggerId, bool newState)
	{
		LevelHandler::SetTrigger(triggerId, newState);

		if (_isServer) {
			MemoryStream packet(3);
			packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::SetTrigger);
			packet.WriteValue<std::uint8_t>(triggerId);
			packet.WriteValue<std::uint8_t>(newState);

			_networkManager->SendToAll(NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
		}
	}

	void MultiLevelHandler::SetWeather(WeatherType type, uint8_t intensity)
	{
		// TODO: This should probably be client local
		LevelHandler::SetWeather(type, intensity);
	}

	bool MultiLevelHandler::BeginPlayMusic(const StringView& path, bool setDefault, bool forceReload)
	{
		// TODO: This should probably be client local
		return LevelHandler::BeginPlayMusic(path, setDefault, forceReload);
	}

	void MultiLevelHandler::BeforeActorDestroyed(Actors::ActorBase* actor)
	{
		auto it = _remotingActors.find(actor);
		if (it == _remotingActors.end()) {
			return;
		}

		for (auto& [peer, peerDesc] : _peerDesc) {
			MemoryStream packet(5);
			packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::DestroyRemoteActor);
			packet.WriteVariableUint32(it->second);

			// TODO: If it fails, it will release the packet which is wrong
			_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
		}

		_remotingActors.erase(it);
	}

	void MultiLevelHandler::ProcessEvents(float timeMult)
	{
		// Process events only by server
		if (_isServer) {
			LevelHandler::ProcessEvents(timeMult);
		}
	}

	void MultiLevelHandler::PrepareNextLevelInitialization(LevelInitialization& levelInit)
	{
		LevelHandler::PrepareNextLevelInitialization(levelInit);

		// Initialize only local players
		for (std::int32_t i = 1; i < countof(levelInit.PlayerCarryOvers); i++) {
			levelInit.PlayerCarryOvers[i].Type = PlayerType::None;
		}
	}

	void MultiLevelHandler::SynchronizePeers()
	{
		for (auto& [peer, peerDesc] : _peerDesc) {
			if (peerDesc.State != PeerState::LevelLoaded) {
				continue;
			}

			// TODO
			Vector2 spawnPosition = EventMap()->GetSpawnPosition(PlayerType::Jazz);
			if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
				continue;
			}

			++_lastSpawnedActorId;
			std::int32_t playerIndex = _lastSpawnedActorId;

			std::shared_ptr<Actors::Player> player = std::make_shared<Actors::Player>();
			std::uint8_t playerParams[2] = { (std::uint8_t)PlayerType::Spaz, (std::uint8_t)playerIndex };
			player->OnActivated(Actors::ActorActivationDetails(
				this,
				Vector3i(spawnPosition.X, spawnPosition.Y, PlayerZ - playerIndex),
				playerParams
			));

			Actors::Player* ptr = player.get();
			_players.push_back(ptr);

			peerDesc.Player = ptr;
			peerDesc.State = PeerState::LevelSynchronized;

			_playerStates[playerIndex] = PlayerState(player->_pos, player->_speed);

			_suppressRemoting = true;
			AddActor(player);
			_suppressRemoting = false;

			// Spawn the player also on the remote side
			{
				std::uint8_t flags = 0;
				if (player->_controllable) {
					flags |= 0x01;
				}

				MemoryStream packet(16);
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::CreateControllablePlayer);
				packet.WriteVariableUint32(playerIndex);
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_playerType);
				packet.WriteValue<std::uint8_t>((std::uint8_t)player->_health);
				packet.WriteValue<std::uint8_t>(flags);
				packet.WriteVariableInt32((std::int32_t)player->_pos.X);
				packet.WriteVariableInt32((std::int32_t)player->_pos.Y);

				_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
			}

			for (Actors::Player* otherPlayer : _players) {
				if (otherPlayer == ptr) {
					continue;
				}

				const auto& metadataPath = otherPlayer->_metadata->Path;

				MemoryStream packet(24 + metadataPath.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::CreateRemoteActor);
				packet.WriteVariableUint32(otherPlayer->_playerIndex);
				packet.WriteVariableInt32((std::int32_t)otherPlayer->_pos.X);
				packet.WriteVariableInt32((std::int32_t)otherPlayer->_pos.Y);
				packet.WriteVariableInt32((std::int32_t)otherPlayer->_renderer.layer());
				packet.WriteVariableUint32((std::uint32_t)otherPlayer->_state);
				packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
				packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
				packet.WriteVariableUint32((std::uint32_t)(otherPlayer->_currentTransition != nullptr ? otherPlayer->_currentTransition->State : otherPlayer->_currentAnimation->State));

				// TODO: If it fail, it will release the packet which is wrong
				_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
			}

			for (const auto& [remotingActor, remotingActorId] : _remotingActors) {
				const auto& metadataPath = remotingActor->_metadata->Path;

				MemoryStream packet(24 + metadataPath.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::CreateRemoteActor);
				packet.WriteVariableUint32(remotingActorId);
				packet.WriteVariableInt32((std::int32_t)remotingActor->_pos.X);
				packet.WriteVariableInt32((std::int32_t)remotingActor->_pos.Y);
				packet.WriteVariableInt32((std::int32_t)remotingActor->_renderer.layer());
				packet.WriteVariableUint32((std::uint32_t)remotingActor->_state);
				packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
				packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
				packet.WriteVariableUint32((std::uint32_t)(remotingActor->_currentTransition != nullptr ? remotingActor->_currentTransition->State : remotingActor->_currentAnimation->State));

				// TODO: If it fail, it will release the packet which is wrong
				_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
			}

			for (const auto& [otherPeer, otherPeerDesc] : _peerDesc) {
				if (otherPeer == peer || otherPeerDesc.State != PeerState::LevelSynchronized) {
					continue;
				}

				const auto& metadataPath = player->_metadata->Path;

				MemoryStream packet(24 + metadataPath.size());
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::CreateRemoteActor);
				packet.WriteVariableUint32(playerIndex);
				packet.WriteVariableInt32((std::int32_t)player->_pos.X);
				packet.WriteVariableInt32((std::int32_t)player->_pos.Y);
				packet.WriteVariableInt32((std::int32_t)player->_renderer.layer());
				packet.WriteVariableUint32((std::uint32_t)player->_state);
				packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
				packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());
				packet.WriteVariableUint32((std::uint32_t)(player->_currentTransition != nullptr ? player->_currentTransition->State : player->_currentAnimation->State));

				// TODO: If it fails, it will release the packet which is wrong
				_networkManager->SendToPeer(otherPeer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
			}
		}
	}

	void MultiLevelHandler::UpdatePlayerLocalPos(Actors::Player* player, PlayerState& playerState, float timeMult)
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
			nextIdx += countof(playerState.StateBuffer);
		}

		if (renderTime <= playerState.StateBuffer[nextIdx].Time) {
			std::int32_t prevIdx;
			while (true) {
				prevIdx = nextIdx - 1;
				if (prevIdx < 0) {
					prevIdx += countof(playerState.StateBuffer);
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
	}

	void MultiLevelHandler::OnRemotePlayerPosReceived(PlayerState& playerState, const Vector2f& pos, const Vector2f speed, PlayerFlags flags)
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
				stateBufferPrevPos += countof(playerState.StateBuffer);
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
		if (playerState.StateBufferPos >= countof(playerState.StateBuffer)) {
			playerState.StateBufferPos = 0;
		}

		playerState.Flags = (flags & ~PlayerFlags::JustWarped);
	}

	MultiLevelHandler::PlayerState::PlayerState()
	{
	}

	MultiLevelHandler::PlayerState::PlayerState(const Vector2f& pos, const Vector2f& speed)
		: StateBufferPos(0), Flags(PlayerFlags::None), PressedKeys(0), WarpSeqNum(0), WarpTimeLeft(0.0f), DeviationTime(0.0f)
	{
		Clock& c = nCine::clock();
		std::uint64_t now = c.now() * 1000 / c.frequency();
		for (std::int32_t i = 0; i < countof(StateBuffer); i++) {
			StateBuffer[i].Time = now - countof(StateBuffer) + i;
			StateBuffer[i].Pos = pos;
			StateBuffer[i].Pos = speed;
		}
	}
}

#endif