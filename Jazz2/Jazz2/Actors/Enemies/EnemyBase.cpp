#include "EnemyBase.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../../Tiles/TileMap.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/ToasterShot.h"
#include "../Weapons/Thunderbolt.h"
#include "../Weapons/TNT.h"
#include "../Player.h"
#include "../Solid/PushableBox.h"

#include "../../../nCine/Base/Random.h"

#include <numeric>

namespace Jazz2::Actors::Enemies
{
	EnemyBase::EnemyBase()
		:
		CanCollideWithAmmo(true),
		_canHurtPlayer(true),
		_scoreValue(0),
		_lastHitDir(LastHitDirection::None),
		_blinkingTimeout(0.0f)
	{
	}

	void EnemyBase::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		HandleBlinking(timeMult);
	}

	void EnemyBase::AddScoreToCollider(ActorBase* collider)
	{
		if (_scoreValue > 0) {
			if (auto player = dynamic_cast<Player*>(collider)) {
				player->AddScore(_scoreValue);
			} else if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(collider)) {
				auto owner = shotBase->GetOwner();
				if (owner != nullptr) {
					owner->AddScore(_scoreValue);
				}
			} else if (auto tnt = dynamic_cast<Weapons::TNT*>(collider)) {
				auto owner = tnt->GetOwner();
				if (owner != nullptr) {
					owner->AddScore(_scoreValue);
				}
			}

			_scoreValue = 0;
		}
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
		TileCollisionParams params = { TileDestructType::None, true };
		if (!_levelHandler->IsPositionEmpty(this, aabbA, params) && !_levelHandler->IsPositionEmpty(this, aabbB, params)) {
			return false;
		}

		AABBf aabbDir = AABBInner + Vector2f(x + direction * (AABBInner.R - AABBInner.L) * 0.5f, y + 12.0f);

		uint8_t* eventParams;
		auto events = _levelHandler->EventMap();
		return ((events == nullptr || events->GetEventByPosition(_pos.X + x, _pos.Y + y, &eventParams) != EventType::AreaStopEnemy) && !_levelHandler->IsPositionEmpty(this, aabbDir, params));
	}

	void EnemyBase::TryGenerateRandomDrop()
	{
		constexpr struct {
			EventType Event;
			int Chance;
		} DropChanges[] = {
			{ EventType::Empty, 10 },
			{ EventType::Carrot, 2 },
			{ EventType::FastFire, 2 },
			{ EventType::Gem, 6 }
		};

		constexpr int combinedChance = std::accumulate(DropChanges, DropChanges + _countof(DropChanges), 0, [](const int& sum, const auto& item) {
			return sum + item.Chance;
		});

		int drop = Random().Next(0, combinedChance);
		for (auto& item : DropChanges) {
			if (drop < item.Chance) {
				if (item.Event != EventType::Empty) {
					uint8_t eventParams[16] { };
					std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(item.Event, eventParams, ActorFlags::None, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer()));
					if (actor != nullptr) {
						_levelHandler->AddActor(actor);
					}
				}
				break;
			}

			drop -= item.Chance;
		}
	}

	bool EnemyBase::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (!GetState(ActorFlags::IsInvulnerable)) {
			if (auto shotBase = dynamic_cast<Weapons::ShotBase*>(other.get())) {
				if (shotBase->GetStrength() > 0) {
					Vector2f shotSpeed;
					if (auto thunderbolt = dynamic_cast<Weapons::Thunderbolt*>(shotBase)) {
						shotSpeed = _pos - shotBase->GetPos();
					} else {
						shotSpeed = shotBase->GetSpeed();
					}

					if (std::abs(shotSpeed.X) > 0.2f) {
						_lastHitDir = (shotSpeed.X > 0.0f ? LastHitDirection::Right : LastHitDirection::Left);
					} else {
						_lastHitDir = (shotSpeed.Y > 0.0f ? LastHitDirection::Down : LastHitDirection::Up);
					}
					DecreaseHealth(shotBase->GetStrength(), shotBase);
				}
				// Collision must also be processed by the shot
				//return true;
			} else if (auto tnt = dynamic_cast<Weapons::TNT*>(other.get())) {
				DecreaseHealth(5, tnt);
				return true;
			} else if (auto pushableBox = dynamic_cast<Solid::PushableBox*>(other.get())) {
				if (pushableBox->GetSpeed().Y > 0.0f && pushableBox->AABBInner.B < _pos.Y) {
					_lastHitDir = LastHitDirection::Up;
					DecreaseHealth(10, pushableBox);
					return true;
				}
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	void EnemyBase::OnHealthChanged(ActorBase* collider)
	{
		StartBlinking();
	}

	bool EnemyBase::OnPerish(ActorBase* collider)
	{
		AddScoreToCollider(collider);

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

		if (auto toasterShot = dynamic_cast<Weapons::ToasterShot*>(collider)) {
			constexpr int DebrisSize = 3;

			Vector2i texSize = res->Base->TextureDiffuse->size();

			for (int fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
				for (int fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
					float currentSize = DebrisSize * Random().FastFloat(0.8f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris = { };
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(0.5f, 2.0f) / res->Base->FrameDimensions.X,
						 Random().FastFloat(0.0f, 0.2f));
					debris.Acceleration = Vector2(0.0f, 0.06f);

					debris.Scale = 1.0f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.001f;

					debris.Time = 320.0f;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = res->Base->TextureDiffuse.get();
					debris.CollisionAction = Tiles::TileMap::DebrisCollisionAction::Bounce;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}
		
		if (auto thunderbolt = dynamic_cast<Weapons::Thunderbolt*>(collider)) {
			constexpr int DebrisSize = 3;

			Vector2i texSize = res->Base->TextureDiffuse->size();

			for (int fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
				for (int fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
					float currentSize = DebrisSize * Random().FastFloat(0.4f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris = { };
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 4.0f) / res->Base->FrameDimensions.X,
						 ((fy - res->Base->FrameDimensions.Y / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 4.0f) / res->Base->FrameDimensions.Y);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.0f;
					debris.ScaleSpeed = -0.004f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.004f;

					debris.Time = Random().FastFloat(10.0f, 50.0f);

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = res->Base->TextureDiffuse.get();
					debris.CollisionAction = Tiles::TileMap::DebrisCollisionAction::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (_pos.Y > _levelHandler->WaterLevel()) {
			constexpr int DebrisSize = 3;

			Vector2i texSize = res->Base->TextureDiffuse->size();

			for (int fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
				for (int fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
					float currentSize = DebrisSize * Random().FastFloat(0.2f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris = { };
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(1.0f, 3.0f) / res->Base->FrameDimensions.X,
						 ((fy - res->Base->FrameDimensions.Y / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(1.0f, 3.0f) / res->Base->FrameDimensions.Y);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.0f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.004f;

					debris.Time = Random().FastFloat(300.0f, 340.0f);;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = res->Base->TextureDiffuse.get();
					debris.CollisionAction = Tiles::TileMap::DebrisCollisionAction::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

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

	void EnemyBase::StartBlinking()
	{
		if (_blinkingTimeout <= 0.0f) {
			_renderer.Initialize(ActorRendererType::WhiteMask);
		}

		_blinkingTimeout = 6.0f;
	}

	void EnemyBase::HandleBlinking(float timeMult)
	{
		if (_blinkingTimeout > 0.0f) {
			_blinkingTimeout -= timeMult;
			if (_blinkingTimeout <= 0.0f) {
				_renderer.Initialize(ActorRendererType::Default);
			}
		}
	}
}