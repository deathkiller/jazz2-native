#include "EnemyBase.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../../Tiles/TileMap.h"
#include "../Weapons/ShotBase.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	EnemyBase::EnemyBase()
		:
		CanCollideWithAmmo(true),
		_canHurtPlayer(true),
		_scoreValue(0),
		_lastHitDir(LastHitDirection::None)
	{
	}

	Task<bool> EnemyBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// TODO: change these types to uint8_t
		PlayerType playerType = (PlayerType)details.Params[0];
		SetFacingLeft(details.Params[2] != 0);

		switch (playerType) {
			default:
			case PlayerType::Jazz:
				co_await RequestMetadataAsync("Interactive/PlayerJazz");
				break;
			case PlayerType::Spaz:
				co_await RequestMetadataAsync("Interactive/PlayerSpaz");
				break;
			case PlayerType::Lori:
				co_await RequestMetadataAsync("Interactive/PlayerLori");
				break;
		}

		SetAnimation("Corpse");

		CollisionFlags = CollisionFlags::ForceDisableCollisions;

		co_return true;
	}

	void EnemyBase::SetHealthByDifficulty(int health)
	{
		switch (_levelHandler->Difficulty()) {
			case GameDifficulty::Easy: health = (int)std::round(health * 0.6f); break;
			case GameDifficulty::Hard: health = (int)std::round(health * 1.4f); break;
		}

		_health = std::max(health, 1);
		_maxHealth = _health;
	}

	bool EnemyBase::CanMoveToPosition(float x, float y)
	{
		int direction = (IsFacingLeft() ? -1 : 1);
		AABBf aabbA = AABBInner + Vector2f(x, y - 3);
		AABBf aabbB = AABBInner + Vector2f(x, y + 3);
		if (!_levelHandler->IsPositionEmpty(this, aabbA, true) && !_levelHandler->IsPositionEmpty(this, aabbB, true)) {
			return false;
		}

		AABBf aabbDir = AABBInner + Vector2f(x + direction * (AABBInner.R - AABBInner.L) * 0.5f, y + 12);

		uint8_t* eventParams;
		auto events = _levelHandler->EventMap();
		return ((events == nullptr || events->GetEventByPosition(_pos.X + x, _pos.Y + y, &eventParams) != EventType::AreaStopEnemy) && !_levelHandler->IsPositionEmpty(this, aabbDir, true));
	}

	void EnemyBase::TryGenerateRandomDrop()
	{
		// TODO
		/*EventType drop = MathF.Rnd.OneOfWeighted(
			new KeyValuePair<EventType, float>(EventType.Empty, 10),
			new KeyValuePair<EventType, float>(EventType.Carrot, 2),
			new KeyValuePair<EventType, float>(EventType.FastFire, 2),
			new KeyValuePair<EventType, float>(EventType.Gem, 6)
		);

		if (drop != EventType.Empty) {
			ActorBase actor = levelHandler.EventSpawner.SpawnEvent(drop, new ushort[8], ActorInstantiationFlags.None, Transform.Pos);
			levelHandler.AddActor(actor);
		}*/
	}

	bool EnemyBase::OnHandleCollision(ActorBase* other)
	{
		if (!GetState(ActorFlags::IsInvulnerable)) {
			if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other)) {
				Vector2f ammoSpeed = shotBase->GetSpeed();
				if (std::abs(ammoSpeed.X) > 0.2f) {
					_lastHitDir = (ammoSpeed.X > 0.0f ? LastHitDirection::Right : LastHitDirection::Left);
				} else {
					_lastHitDir = (ammoSpeed.Y > 0.0f ? LastHitDirection::Down : LastHitDirection::Up);
				}
				DecreaseHealth(shotBase->GetStrength(), shotBase);
				// Collision must also be processed by the shot
				//return true;
			} /*else if (auto shotTnt = dynamic_cast<Weapons::ShotTNT*>(other)) {
				DecreaseHealth(5, shotTnt);
				return true;
			}*/
		}

		return false;
	}

	bool EnemyBase::OnPerish(ActorBase* collider)
	{
		if (auto player = dynamic_cast<Player*>(collider)) {
			player->AddScore(_scoreValue);
		} else if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(collider)) {
			auto owner = shotBase->GetOwner();
			if (owner != nullptr) {
				owner->AddScore(_scoreValue);
			}
		} /*else if(auto shotTnt = dynamic_cast<Weapons::ShotTNT*>(collider)) {
			auto owner = shotTnt->GetOwner();
			if (owner != nullptr) {
				owner->AddScore(_scoreValue);
			}
		}*/

		return ActorBase::OnPerish(collider);
	}

	void EnemyBase::CreateDeathDebris(ActorBase* collider)
	{
		auto tilemap = _levelHandler->TileMap();
		if (tilemap == nullptr) {
			return;
		}

		GraphicResource* res = (_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation);
		Texture* texture = res->Base->TextureDiffuse.get();
		float x = _pos.X - res->Base->Hotspot.X;
		float y = _pos.Y - res->Base->Hotspot.Y;

		// TODO
		/*if (auto toasterShot = dynamic_cast<ToasterShot*>(collider)) {
			constexpr int DebrisSizeX = 5;
			constexpr int DebrisSizeY = 3;

			int currentFrame = _renderer.CurrentFrame;

			for (int fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSizeX + 1) {
				for (int fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSizeY + 1) {
					float currentSizeX = DebrisSizeX * random().NextFloat(0.8f, 1.1f);
					float currentSizeY = DebrisSizeY * random().NextFloat(0.8f, 1.1f);
					levelHandler.TileMap.CreateDebris(new DestructibleDebris {
						Pos = new Vector3(x + (IsFacingLeft ? res.Base.FrameDimensions.X - fx : fx), y + fy, pos.Z),
						Size = new Vector2(currentSizeX, currentSizeY),
						Speed = new Vector2(((fx - res.Base.FrameDimensions.X / 2) + MathF.Rnd.NextFloat(-2f, 2f)) * (IsFacingLeft ? -1f : 1f) * MathF.Rnd.NextFloat(0.5f, 2f) / res.Base.FrameDimensions.X,
							 MathF.Rnd.NextFloat(0f, 0.2f)),
						Acceleration = new Vector2(0f, 0.06f),

						Scale = 1f,
						Alpha = 1f,
						AlphaSpeed = -0.002f,

						Time = 320f,

						Material = material,
						MaterialOffset = new Rect(
							 (((float)(currentFrame % res.Base.FrameConfiguration.X) / res.Base.FrameConfiguration.X) + ((float)fx / texture.ContentWidth)) * texture.UVRatio.X,
							 (((float)(currentFrame / res.Base.FrameConfiguration.X) / res.Base.FrameConfiguration.Y) + ((float)fy / texture.ContentHeight)) * texture.UVRatio.Y,
							 (currentSizeX * texture.UVRatio.X / texture.ContentWidth),
							 (currentSizeY * texture.UVRatio.Y / texture.ContentHeight)
						 ),

						CollisionAction = DebrisCollisionAction.Bounce
					});
				}
			}
		} else*/ if (_pos.Y > _levelHandler->WaterLevel()) {
			constexpr int DebrisSize = 3;

			Vector2i texSize = res->Base->TextureDiffuse->size();

			for (int fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
				for (int fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
					float currentSize = DebrisSize * nCine::Random().NextFloat(0.2f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris = { };
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + nCine::Random().NextFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * nCine::Random().NextFloat(1.0f, 3.0f) / res->Base->FrameDimensions.X,
						 ((fy - res->Base->FrameDimensions.Y / 2) + nCine::Random().NextFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * nCine::Random().NextFloat(1.0f, 3.0f) / res->Base->FrameDimensions.Y);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.0f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.004f;

					debris.Time = 340.0f;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = res->Base->TextureDiffuse.get();
					debris.CollisionAction = Tiles::TileMap::DebrisCollisionAction::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
		} else {
			Vector2f force;
			switch (_lastHitDir) {
				case LastHitDirection::Left: force = Vector2f(-1.4f, 0.0f); break;
				case LastHitDirection::Right: force = Vector2f(1.4f, 0.0f); break;
				case LastHitDirection::Up: force = Vector2f(0.0f, -1.4f); break;
				case LastHitDirection::Down: force = Vector2f(0.0f, 1.4f); break;
				default: force = Vector2f::Zero; break;
			}

			tilemap->CreateParticleDebris(res, Vector3f(_pos.X, _pos.Y, (float)_renderer.layer()), force, _renderer.CurrentFrame, IsFacingLeft());
		}
	}
}