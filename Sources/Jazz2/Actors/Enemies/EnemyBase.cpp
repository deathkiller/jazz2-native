#include "EnemyBase.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../../Tiles/TileMap.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/ShieldFireShot.h"
#include "../Weapons/ToasterShot.h"
#include "../Weapons/Thunderbolt.h"
#include "../Weapons/TNT.h"
#include "../Explosion.h"
#include "../Player.h"
#include "../Solid/Pole.h"
#include "../Solid/PushableBox.h"

#include "../../../nCine/Base/Random.h"

#include <numeric>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Enemies
{
	EnemyBase::EnemyBase()
		: CanCollideWithShots(true), _canHurtPlayer(true), _scoreValue(0), _blinkingTimeout(0.0f)
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
			if (auto* player = runtime_cast<Player>(collider)) {
				player->AddScore(_scoreValue);
			} else if (auto* shotBase = runtime_cast<Weapons::ShotBase>(collider)) {
				auto* owner = shotBase->GetOwner();
				if (owner != nullptr) {
					owner->AddScore(_scoreValue);
				}
			} else if (auto* tnt = runtime_cast<Weapons::TNT>(collider)) {
				auto* owner = tnt->GetOwner();
				if (owner != nullptr) {
					owner->AddScore(_scoreValue);
				}
			}

			_scoreValue = 0;
		}
	}

	void EnemyBase::SetHealthByDifficulty(int health)
	{
		switch (_levelHandler->GetDifficulty()) {
			case GameDifficulty::Easy: health = (std::int32_t)std::round(health * 0.6f); break;
			case GameDifficulty::Hard: health = (std::int32_t)std::round(health * 1.4f); break;
		}

		_health = std::max(health, 1);
		_maxHealth = _health;
	}

	void EnemyBase::PlaceOnGround()
	{
		if (!GetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation)) {
			return;
		}

		OnUpdateHitbox();

		TileCollisionParams params = { TileDestructType::None, _speed.Y >= 0.0f };
		if (_levelHandler->IsPositionEmpty(this, AABBInner, params)) {
			// Apply instant gravitation
			std::int32_t i = 10;
			while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative, params)) {
				// Try it multiple times
			}
		} else {
			// Enemy is probably stuck inside a tile, move up
			std::int32_t i = 1;
			while (i++ <= 6 && !MoveInstantly(Vector2f(0.0f, i * -4.0f), MoveType::Relative, params)) {
				// Try it multiple times
			}
		}
	}

	bool EnemyBase::CanMoveToPosition(float x, float y)
	{
		uint8_t* eventParams;
		auto events = _levelHandler->EventMap();
		if (events != nullptr && events->GetEventByPosition(_pos.X + x, _pos.Y + y, &eventParams) == EventType::AreaStopEnemy) {
			return false;
		}

		ActorState prevState = GetState();
		SetState(ActorState::CollideWithTilesetReduced, false);
		SetState(ActorState::SkipPerPixelCollisions, true);

		bool success;
		TileCollisionParams params = { TileDestructType::None, true };
		AABBf aabbAbove = AABBf(x < 0.0f ? AABBInner.L - 8.0f + x : AABBInner.R, AABBInner.T, x < 0.0f ? AABBInner.L : AABBInner.R + 8.0f + x, AABBInner.B - 2.0f);
		if (_levelHandler->IsPositionEmpty(this, aabbAbove, params)) {
			AABBf aabbBelow = AABBf(x < 0.0f ? AABBInner.L - 8.0f + x : AABBInner.R + 2.0f, AABBInner.B, x < 0.0f ? AABBInner.L - 2.0f : AABBInner.R + 8.0f + x, AABBInner.B + 8.0f);
			success = !_levelHandler->IsPositionEmpty(this, aabbBelow, params);
		} else {
			success = false;
		}
		
		SetState(prevState);

		return success;
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

		// TODO: This cannot be constexpr because of Android
		int combinedChance = std::accumulate(std::begin(DropChanges), std::end(DropChanges), 0, [](const int& sum, const auto& item) {
			return sum + item.Chance;
		});

		int drop = Random().Next(0, combinedChance);
		for (auto& item : DropChanges) {
			if (drop < item.Chance) {
				if (item.Event != EventType::Empty) {
					uint8_t eventParams[16] { };
					std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(item.Event, eventParams, ActorState::None, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer()));
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
		if (!GetState(ActorState::IsInvulnerable)) {
			if (auto* shotBase = runtime_cast<Weapons::ShotBase>(other.get())) {
				if (shotBase->GetStrength() > 0) {
					DecreaseHealth(shotBase->GetStrength(), shotBase);
				}
				// Collision must also be processed by the shot
				//return true;
			} else if (auto* tnt = runtime_cast<Weapons::TNT>(other.get())) {
				DecreaseHealth(5, tnt);
				return true;
			} else if (auto* pole = runtime_cast<Solid::Pole>(other.get())) {
				if (_levelHandler->IsReforged()) {
					bool hit;
					switch (pole->GetFallDirection()) {
						case Solid::Pole::FallDirection::Left: hit = (_pos.X < pole->GetPos().X); break;
						case Solid::Pole::FallDirection::Right: hit = (_pos.X > pole->GetPos().X); break;
						default: hit = false; break;
					}
					if (hit) {
						DecreaseHealth(10, pole);
						return true;
					}
				}
			} else if (auto* pushableBox = runtime_cast<Solid::PushableBox>(other.get())) {
				if (_levelHandler->IsReforged() && pushableBox->GetSpeed().Y > 0.0f && pushableBox->AABBInner.B < _pos.Y) {
					DecreaseHealth(10, pushableBox);
					return true;
				}
			}
		}

		return ActorBase::OnHandleCollision(std::move(other));
	}

	bool EnemyBase::CanCauseDamage(ActorBase* collider)
	{
		return !IsInvulnerable() && CanCollideWithShots;
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

	void EnemyBase::StartBlinking()
	{
		if (_frozenTimeLeft > 0.0f) {
			return;
		}

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