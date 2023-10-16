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
	MultiLevelHandler::MultiLevelHandler(IRootController* root, NetworkManager* networkManager, const LevelInitialization& levelInit)
		: LevelHandler(root, levelInit), _networkManager(networkManager), _updateTimeLeft(1.0f), _initialUpdateSent(false), _justWarped(false)
	{
		_isServer = (networkManager->GetState() == NetworkState::Listening);

		_lastPlayerIndex = -1;
		for (int32_t i = 0; i < countof(levelInit.PlayerCarryOvers); i++) {
			if (levelInit.PlayerCarryOvers[i].Type != PlayerType::None) {
				_lastPlayerIndex = i;
			}
		}
		_lastPlayerIndex++;

		auto& resolver = ContentResolver::Get();
		resolver.PreloadMetadataAsync("Interactive/PlayerJazz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerSpaz"_s);
		resolver.PreloadMetadataAsync("Interactive/PlayerLori"_s);
	}

	MultiLevelHandler::~MultiLevelHandler()
	{
	}

	float MultiLevelHandler::GetAmbientLight() const
	{
		return LevelHandler::GetAmbientLight();
	}

	void MultiLevelHandler::SetAmbientLight(float value)
	{
		LevelHandler::SetAmbientLight(value);
	}

	void MultiLevelHandler::OnLevelLoaded(const StringView& fullPath, const StringView& name, const StringView& nextLevel, const StringView& secretLevel, std::unique_ptr<Tiles::TileMap>& tileMap, std::unique_ptr<Events::EventMap>& eventMap, const StringView& musicPath, const Vector4f& ambientColor, WeatherType weatherType, uint8_t weatherIntensity, uint16_t waterLevel, SmallVectorImpl<String>& levelTexts)
	{
		// TODO
		//LevelHandler::OnLevelLoaded(fullPath, name, nextLevel,secretLevel, tileMap, eventMap, musicPath, ambientColor, weatherType, weatherIntensity, waterLevel, levelTexts);
	}

	void MultiLevelHandler::OnBeginFrame()
	{
		LevelHandler::OnBeginFrame();
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
				std::uint32_t actorCount = (std::uint32_t)_players.size();

				MemoryStream packet(5 + actorCount * 9);
				packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::UpdateAllActors);
				packet.WriteVariableUint32(_players.size());

				for (Actors::Player* player : _players) {
					Vector2f pos;
					auto it = _playerStates.find(player->_playerIndex);
					if (it != _playerStates.end()) {
						// Remote players
						UpdatePlayerLocalPos(player, it->second);
						pos = it->second.StateBuffer[it->second.StateBufferPos].Pos;

						if (it->second.WarpTimeLeft > 0.0f) {
							it->second.WarpTimeLeft -= timeMult;
							if (it->second.WarpTimeLeft <= 0.0f) {
								LOGW("Player warped without permission (possible cheating attempt)");
							}
						}
					} else {
						// Local players
						pos = player->_pos;
					}

					packet.WriteVariableUint32(player->_playerIndex);
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.X * 256.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(pos.Y * 256.0f));
					packet.WriteVariableUint32((std::uint32_t)(player->_currentTransitionState != AnimState::Idle ? player->_currentTransitionState : player->_currentAnimationState));

					std::uint8_t flags = 0;
					if (player->IsFacingLeft()) {
						flags |= 0x01;
					}
					if (player->_renderer.isDrawEnabled()) {
						flags |= 0x02;
					}
					packet.WriteValue<std::uint8_t>(flags);
				}

				_networkManager->SendToAll(NetworkChannel::UnreliableUpdates, packet.GetBuffer(), packet.GetSize());
			} else {
				if (!_players.empty()) {
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
					if (_justWarped) {
						flags |= PlayerFlags::JustWarped;
						_justWarped = false;
					}

					if (PlayerActionPressed(0, PlayerActions::Up)) {
						flags |= PlayerFlags::PressedUp;
					}
					if (PlayerActionPressed(0, PlayerActions::Down)) {
						flags |= PlayerFlags::PressedDown;
					}
					if (PlayerActionPressed(0, PlayerActions::Left)) {
						flags |= PlayerFlags::PressedLeft;
					}
					if (PlayerActionPressed(0, PlayerActions::Right)) {
						flags |= PlayerFlags::PressedRight;
					}
					if (PlayerActionPressed(0, PlayerActions::Jump)) {
						flags |= PlayerFlags::PressedJump;
					}
					if (PlayerActionPressed(0, PlayerActions::Run)) {
						flags |= PlayerFlags::PressedRun;
					}

					MemoryStream packet(20);
					packet.WriteValue<std::uint8_t>((std::uint8_t)ClientPacketType::PlayerUpdate);
					packet.WriteVariableUint32(_lastPlayerIndex);
					packet.WriteVariableUint64(now);
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.X * 256.0f));
					packet.WriteValue<std::int32_t>((std::int32_t)(player->_pos.Y * 256.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.X * 256.0f));
					packet.WriteValue<std::int16_t>((std::int16_t)(player->_speed.Y * 256.0f));
					packet.WriteVariableUint32((std::uint32_t)flags);

					_networkManager->SendToPeer(nullptr, NetworkChannel::UnreliableUpdates, packet.GetBuffer(), packet.GetSize());
				}
			}
		}
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
	}

	std::shared_ptr<AudioBufferPlayer> MultiLevelHandler::PlaySfx(AudioBuffer* buffer, const Vector3f& pos, bool sourceRelative, float gain, float pitch)
	{
		return LevelHandler::PlaySfx(buffer, pos, sourceRelative, gain, pitch);
	}

	std::shared_ptr<AudioBufferPlayer> MultiLevelHandler::PlayCommonSfx(const StringView& identifier, const Vector3f& pos, float gain, float pitch)
	{
		return LevelHandler::PlayCommonSfx(identifier, pos, gain, pitch);
	}

	void MultiLevelHandler::WarpCameraToTarget(const std::shared_ptr<Actors::ActorBase>& actor, bool fast)
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
		LevelHandler::BeginLevelChange(exitType, nextLevel);
	}

	void MultiLevelHandler::HandleGameOver()
	{
		LevelHandler::HandleGameOver();
	}

	bool MultiLevelHandler::HandlePlayerDied(const std::shared_ptr<Actors::ActorBase>& player)
	{
		return LevelHandler::HandlePlayerDied(player);
	}

	void MultiLevelHandler::HandlePlayerWarped(const std::shared_ptr<Actors::ActorBase>& player, const Vector2f& prevPos, bool fast)
	{
		LevelHandler::HandlePlayerWarped(player, prevPos, fast);

		if (_isServer) {
			auto it = _playerStates.find(std::static_pointer_cast<Actors::Player>(player)->_playerIndex);
			if (it != _playerStates.end()) {
				it->second.Flags |= PlayerFlags::JustWarped;
			}
		} else {
			_justWarped = true;
		}
	}

	void MultiLevelHandler::SetCheckpoint(Vector2f pos)
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
				PlayerFlags mask;
				switch (action) {
					case PlayerActions::Up: mask = PlayerFlags::PressedUp; break;
					case PlayerActions::Down: mask = PlayerFlags::PressedDown; break;
					case PlayerActions::Left: mask = PlayerFlags::PressedLeft; break;
					case PlayerActions::Right: mask = PlayerFlags::PressedRight; break;
					case PlayerActions::Jump: mask = PlayerFlags::PressedJump; break;
					case PlayerActions::Run: mask = PlayerFlags::PressedRun; break;
					default: return false;
				}
				return (it->second.Flags & mask) == mask;
			}
		}

		return LevelHandler::PlayerActionPressed(index, action, includeGamepads);
	}

	bool MultiLevelHandler::PlayerActionPressed(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				PlayerFlags mask;
				switch (action) {
					case PlayerActions::Up: mask = PlayerFlags::PressedUp; break;
					case PlayerActions::Down: mask = PlayerFlags::PressedDown; break;
					case PlayerActions::Left: mask = PlayerFlags::PressedLeft; break;
					case PlayerActions::Right: mask = PlayerFlags::PressedRight; break;
					case PlayerActions::Jump: mask = PlayerFlags::PressedJump; break;
					case PlayerActions::Run: mask = PlayerFlags::PressedRun; break;
					default: return false;
				}
				isGamepad = false;
				return (it->second.Flags & mask) == mask;
			}
		}

		return LevelHandler::PlayerActionPressed(index, action, includeGamepads, isGamepad);
	}

	bool MultiLevelHandler::PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				PlayerFlags mask;
				switch (action) {
					case PlayerActions::Up: mask = PlayerFlags::PressedUp; break;
					case PlayerActions::Down: mask = PlayerFlags::PressedDown; break;
					case PlayerActions::Left: mask = PlayerFlags::PressedLeft; break;
					case PlayerActions::Right: mask = PlayerFlags::PressedRight; break;
					case PlayerActions::Jump: mask = PlayerFlags::PressedJump; break;
					case PlayerActions::Run: mask = PlayerFlags::PressedRun; break;
					default: return false;
				}
				return (it->second.Flags & mask) == mask;
			}
		}

		return LevelHandler::PlayerActionHit(index, action, includeGamepads);
	}

	bool MultiLevelHandler::PlayerActionHit(int32_t index, PlayerActions action, bool includeGamepads, bool& isGamepad)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				PlayerFlags mask;
				switch (action) {
					case PlayerActions::Up: mask = PlayerFlags::PressedUp; break;
					case PlayerActions::Down: mask = PlayerFlags::PressedDown; break;
					case PlayerActions::Left: mask = PlayerFlags::PressedLeft; break;
					case PlayerActions::Right: mask = PlayerFlags::PressedRight; break;
					case PlayerActions::Jump: mask = PlayerFlags::PressedJump; break;
					case PlayerActions::Run: mask = PlayerFlags::PressedRun; break;
					default: return false;
				}
				isGamepad = false;
				return (it->second.Flags & mask) == mask;
			}
		}

		return LevelHandler::PlayerActionHit(index, action, includeGamepads, isGamepad);
	}

	float MultiLevelHandler::PlayerHorizontalMovement(int32_t index)
	{
		if (index > 0) {
			auto it = _playerStates.find(index);
			if (it != _playerStates.end()) {
				if ((it->second.Flags & PlayerFlags::PressedLeft) != PlayerFlags::None) {
					return -1.0f;
				} else if ((it->second.Flags & PlayerFlags::PressedRight) != PlayerFlags::None) {
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
				if ((it->second.Flags & PlayerFlags::PressedUp) != PlayerFlags::None) {
					return -1.0f;
				} else if ((it->second.Flags & PlayerFlags::PressedDown) != PlayerFlags::None) {
					return 1.0f;
				} else {
					return 0.0f;
				}
			}
		}

		return LevelHandler::PlayerVerticalMovement(index);
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

	bool MultiLevelHandler::OnPacketReceived(const Peer& peer, std::uint8_t channelId, std::uint8_t* data, std::size_t dataLength)
	{
		if (_isServer) {
			auto packetType = (ClientPacketType)data[0];
			switch (packetType) {
				case ClientPacketType::LevelReady: {
					// TODO
					Vector2 spawnPosition = EventMap()->GetSpawnPosition(PlayerType::Jazz);
					if (spawnPosition.X < 0.0f && spawnPosition.Y < 0.0f) {
						return true;
					}

					++_lastPlayerIndex;
					std::int32_t playerIndex = _lastPlayerIndex;

					std::shared_ptr<Actors::Player> player = std::make_shared<Actors::Player>();
					std::uint8_t playerParams[2] = { (std::uint8_t)PlayerType::Spaz, (std::uint8_t)playerIndex };
					player->OnActivated(Actors::ActorActivationDetails(
						this,
						Vector3i(spawnPosition.X, spawnPosition.Y, PlayerZ - playerIndex),
						playerParams
					));

					Actors::Player* ptr = player.get();
					_players.push_back(ptr);

					_peerStates[peer] = PeerState(ptr);
					_playerStates[playerIndex] = PlayerState(player->_pos);

					AddActor(player);

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

					// TODO: Create all remote players
					String metadataPath = "Interactive/PlayerJazz"_s;

					for (Actors::Player* otherPlayer : _players) {
						if (otherPlayer == ptr) {
							continue;
						}

						MemoryStream packet(20 + metadataPath.size());
						packet.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::CreateRemoteActor);
						packet.WriteVariableUint32(otherPlayer->_playerIndex);
						packet.WriteVariableInt32((std::int32_t)otherPlayer->_pos.X);
						packet.WriteVariableInt32((std::int32_t)otherPlayer->_pos.Y);
						packet.WriteVariableInt32((std::int32_t)otherPlayer->_renderer.layer());
						packet.WriteVariableUint32((std::uint32_t)metadataPath.size());
						packet.Write(metadataPath.data(), (std::uint32_t)metadataPath.size());

						// TODO: If it fail, it will release the packet which is wrong
						_networkManager->SendToPeer(peer, NetworkChannel::Main, packet.GetBuffer(), packet.GetSize());
					}
					return true;
				}
				case ClientPacketType::PlayerUpdate: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();

					auto it = _peerStates.find(peer);
					auto it2 = _playerStates.find(playerIndex);
					if (it == _peerStates.end() || it2 == _playerStates.end()) {
						return true;
					}

					auto player = it->second.Player;
					if (playerIndex != player->_playerIndex) {
						LOGW("PlayerUpdate packet received with wrong player index %i instead of %i", playerIndex, player->_playerIndex);
						return true;
					}

					std::uint64_t now = packet.ReadVariableUint64();
					float posX = packet.ReadValue<std::int32_t>() / 256.0f;
					float posY = packet.ReadValue<std::int32_t>() / 256.0f;
					float speedX = packet.ReadValue<std::int16_t>() / 256.0f;
					float speedY = packet.ReadValue<std::int16_t>() / 256.0f;
					PlayerFlags flags = (PlayerFlags)packet.ReadVariableUint32();
		
					if ((it2->second.Flags & PlayerFlags::JustWarped) == PlayerFlags::JustWarped) {
						if (it2->second.WarpTimeLeft > 0.0f) {
							LOGD("Granted permission to player %i to warp asynchronously", playerIndex);
							it2->second.WarpTimeLeft = 0.0f;
						}
					} else if ((flags & PlayerFlags::JustWarped) == PlayerFlags::JustWarped) {
						LOGD("Player warped remotely (local permission pending)");
						it2->second.WarpTimeLeft = 90.0f;
					} else if (it2->second.WarpTimeLeft <= 0.0f) {
						if (it->second.LastUpdated >= now) {
							return true;
						}

						float posDist = (Vector2f(posX, posY) - player->_pos).SqrLength();
						if (posDist > 92 * 92) {
							LOGW("Player %i position mismatch by %i pixels", playerIndex, (std::int32_t)sqrt(posDist));

							posX = player->_pos.X;
							posY = player->_pos.Y;

							MemoryStream packet2(13);
							packet2.WriteValue<std::uint8_t>((std::uint8_t)ServerPacketType::PlayerMoveInstantly);
							packet2.WriteVariableUint32(player->_playerIndex);
							packet2.WriteValue<std::int32_t>((std::int32_t)(posX * 256.0f));
							packet2.WriteValue<std::int32_t>((std::int32_t)(posY * 256.0f));
							packet2.WriteValue<std::int32_t>((std::int32_t)(player->_speed.X * 256.0f));
							packet2.WriteValue<std::int32_t>((std::int32_t)(player->_speed.Y * 256.0f));

							_networkManager->SendToPeer(peer, NetworkChannel::Main, packet2.GetBuffer(), packet2.GetSize());
						}
					}

					player->_speed = Vector2f(speedX, speedY);
					player->SetFacingLeft((flags & PlayerFlags::IsFacingLeft) != PlayerFlags::None);
					player->_renderer.setDrawEnabled((flags & PlayerFlags::IsVisible) != PlayerFlags::None);
					player->_isActivelyPushing = (flags & PlayerFlags::IsActivelyPushing) != PlayerFlags::None;
					// TODO: Special move

					it->second.LastUpdated = now;

					OnRemotePlayerPosReceived(it2->second, Vector2f(posX, posY), flags);
					return true;
				}
			}

		} else {
			auto packetType = (ServerPacketType)data[0];
			switch (packetType) {
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

					_lastPlayerIndex = playerIndex;
					break;
				}
				case ServerPacketType::CreateRemoteActor: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t index = packet.ReadVariableUint32();
					std::int32_t posX = packet.ReadVariableInt32();
					std::int32_t posY = packet.ReadVariableInt32();
					std::int32_t posZ = packet.ReadVariableInt32();
					std::uint32_t metadataLength = packet.ReadVariableUint32();
					String metadataPath = String(NoInit, metadataLength);
					packet.Read(metadataPath.data(), metadataLength);

					std::shared_ptr<Actors::RemoteActor> remoteActor = std::make_shared<Actors::RemoteActor>();
					remoteActor->OnActivated(Actors::ActorActivationDetails(this, Vector3i(posX, posY, posZ)));

					_remoteActors[index] = remoteActor;
					AddActor(std::static_pointer_cast<Actors::ActorBase>(remoteActor));
					break;
				}
				case ServerPacketType::UpdateAllActors: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t actorCount = packet.ReadVariableUint32();
					for (std::uint32_t i = 0; i < actorCount; i++) {
						std::uint32_t index = packet.ReadVariableUint32();
						auto it = _remoteActors.find(index);
						if (it != _remoteActors.end()) {
							float posX = packet.ReadValue<std::int32_t>() / 256.0f;
							float posY = packet.ReadValue<std::int32_t>() / 256.0f;
							std::uint32_t anim = packet.ReadVariableUint32();
							std::uint8_t flags = packet.ReadValue<std::uint8_t>();
							it->second->SyncWithServer(Vector2f(posX, posY), (AnimState)anim, (flags & 0x02) != 0, (flags & 0x01) != 0);
						}
					}
					break;
				}
				case ServerPacketType::AdvanceTileAnimation: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::int32_t tx = packet.ReadVariableInt32();
					std::int32_t ty = packet.ReadVariableInt32();
					std::int32_t amount = packet.ReadVariableInt32();
					TileMap()->AdvanceDestructibleTileAnimation(tx, ty, amount);
					break;
				}
				case ServerPacketType::PlayerMoveInstantly: {
					MemoryStream packet(data + 1, dataLength - 1);
					std::uint32_t playerIndex = packet.ReadVariableUint32();
					if (_lastPlayerIndex != playerIndex) {
						break;
					}

					float posX = packet.ReadValue<std::int32_t>() / 256.0f;
					float posY = packet.ReadValue<std::int32_t>() / 256.0f;
					float speedX = packet.ReadValue<std::int32_t>() / 256.0f;
					float speedY = packet.ReadValue<std::int32_t>() / 256.0f;

					_players[0]->_speed = Vector2f(speedX, speedY);
					_players[0]->MoveInstantly(Vector2f(posX, posY), Actors::MoveType::Absolute);
					break;
				}
				case ServerPacketType::PlaySfx: {
					// TODO
					break;
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

	void MultiLevelHandler::UpdatePlayerLocalPos(Actors::Player* player, const PlayerState& playerState)
	{
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
			std::int64_t timeRange = (playerState.StateBuffer[nextIdx].Time - playerState.StateBuffer[prevIdx].Time);
			if (timeRange > 0) {
				float lerp = (float)(renderTime - playerState.StateBuffer[prevIdx].Time) / timeRange;
				pos = playerState.StateBuffer[prevIdx].Pos + (playerState.StateBuffer[nextIdx].Pos - playerState.StateBuffer[prevIdx].Pos) * lerp;
			} else {
				pos = playerState.StateBuffer[nextIdx].Pos;
			}

			// TODO
			//player->MoveInstantly(pos, Actors::MoveType::Absolute);
			player->MoveInstantly(Vector2f(lerp(player->_pos.X, pos.X, 0.1f), lerp(player->_pos.Y, pos.Y, 0.1f)), Actors::MoveType::Absolute);
		}
	}

	void MultiLevelHandler::OnRemotePlayerPosReceived(PlayerState& playerState, const Vector2f& pos, PlayerFlags flags)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();

		if ((flags & PlayerFlags::JustWarped) != PlayerFlags::JustWarped) {
			// Player is still visible, enable interpolation
			playerState.StateBuffer[playerState.StateBufferPos].Time = now;
			playerState.StateBuffer[playerState.StateBufferPos].Pos = pos;
		} else {
			// Player just warped, reset state buffer to disable interpolation
			std::int32_t stateBufferPrevPos = playerState.StateBufferPos - 1;
			if (stateBufferPrevPos < 0) {
				stateBufferPrevPos += countof(playerState.StateBuffer);
			}

			std::int64_t renderTime = now - ServerDelay;

			playerState.StateBuffer[stateBufferPrevPos].Time = renderTime;
			playerState.StateBuffer[stateBufferPrevPos].Pos = pos;
			playerState.StateBuffer[playerState.StateBufferPos].Time = renderTime;
			playerState.StateBuffer[playerState.StateBufferPos].Pos = pos;
		}

		playerState.StateBufferPos++;
		if (playerState.StateBufferPos >= countof(playerState.StateBuffer)) {
			playerState.StateBufferPos = 0;
		}

		playerState.Flags = (flags & ~PlayerFlags::JustWarped);
	}

	MultiLevelHandler::PlayerState::PlayerState(const Vector2f& pos)
		: StateBufferPos(0), Flags(PlayerFlags::None), WarpTimeLeft(0.0f)
	{
		Clock& c = nCine::clock();
		std::uint64_t now = c.now() * 1000 / c.frequency();
		for (std::int32_t i = 0; i < countof(StateBuffer); i++) {
			StateBuffer[i].Time = now - countof(StateBuffer) + i;
			StateBuffer[i].Pos = Vector2f(pos.X, pos.Y);
		}
	}
}

#endif