#include "ShotBase.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../SolidObjectBase.h"
#include "../Enemies/EnemyBase.h"

#include "../../../nCine/Base/Random.h"
#include "../../../nCine/Application.h"

namespace Jazz2::Actors::Weapons
{
	ShotBase::ShotBase()
		:
		_owner(nullptr),
		_timeLeft(0),
		_firedUp(false),
		_upgrades(0),
		_strength(0),
		_lastRicochet(nullptr),
		_lastRicochetFrame(0)
	{
	}

	Task<bool> ShotBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		CollisionFlags = CollisionFlags::CollideWithTileset | CollisionFlags::CollideWithOtherActors | CollisionFlags::ApplyGravitation;
		SetState(ActorFlags::CanBeFrozen, false);

		co_return true;
	}

	Player* ShotBase::GetOwner()
	{
		return dynamic_cast<Player*>(_owner.get());
	}

	WeaponType ShotBase::GetWeaponType()
	{
		return WeaponType::Unknown;
	}

	void ShotBase::OnUpdate(float timeMult)
	{
		_timeLeft -= timeMult;
		if (_timeLeft <= 0) {
			DecreaseHealth(INT32_MAX);
		}
	}

	bool ShotBase::OnHandleCollision(ActorBase* other)
	{
		if (auto enemyBase = dynamic_cast<Enemies::EnemyBase*>(other)) {
			if (enemyBase->CanCollideWithAmmo) {
				DecreaseHealth(INT32_MAX);
			}
		} else if (auto solidObjectBase = dynamic_cast<SolidObjectBase*>(other)) {
			DecreaseHealth(INT32_MAX);
		} /*else if (other is TriggerCrate || other is BarrelContainer || other is PowerUpWeaponMonitor) {
			if (_lastRicochet != other) {
				_lastRicochet = other;
				_lastRicochetFrame = theApplication().numFrames();
				OnRicochet();
			} else if (_lastRicochetFrame + 2 >= theApplication().numFrames()) {
				DecreaseHealth(INT32_MAX);
			}
		}*/

		return false;
	}

	void ShotBase::OnRicochet()
	{
		MoveInstantly(Vector2f(-_speed.X, -_speed.Y), MoveType::Relative, true);

		_speed.Y = _speed.Y * -0.9f + (nCine::Random().Next() % 100 - 50) * 0.1f;
		_speed.X = _speed.X * -0.9f + (nCine::Random().Next() % 100 - 50) * 0.1f;
	}

	void ShotBase::CheckCollisions(float timeMult)
	{
		if (_health <= 0) {
			return;
		}

		auto tiles = _levelHandler->TileMap();
		if (tiles != nullptr) {
			AABBf adjustedAABB = AABBInner + Vector2f(_speed.X * timeMult, _speed.Y * timeMult);
			if (tiles->CheckWeaponDestructible(adjustedAABB, GetWeaponType(), _strength) > 0) {
				if (GetWeaponType() != WeaponType::Freezer) {
					if (auto player = dynamic_cast<Player*>(_owner.get())) {
						player->AddScore(50);
					}
				}

				DecreaseHealth(1);
			} else if (!tiles->IsTileEmpty(AABBInner, false)) {
				auto events = _levelHandler->EventMap();
				bool handled = false;
				if (events != nullptr) {
					uint8_t* eventParams;
					switch (events->GetEventByPosition(_pos.X + _speed.X * timeMult, _pos.Y + _speed.Y * timeMult, &eventParams)) {
						case EventType::ModifierRicochet:
							if (_lastRicochetFrame + 2 < theApplication().numFrames()) {
								_lastRicochet = nullptr;
								_lastRicochetFrame = theApplication().numFrames();
								OnRicochet();
								handled = true;
							}
							break;
					}
				}

				if (!handled) {
					OnHitWall();
				}
			}
		}
	}

	void ShotBase::TryMovement(float timeMult)
	{
		_speed.X = std::clamp(_speed.X, -16.0f, 16.0f);
		_speed.Y = std::clamp(_speed.Y - (_internalForceY + _externalForce.Y) * timeMult, -16.0f, 16.0f);

		float effectiveSpeedX, effectiveSpeedY;
		effectiveSpeedX = _speed.X + _externalForce.X * timeMult;
		effectiveSpeedY = _speed.Y;
		effectiveSpeedX *= timeMult;
		effectiveSpeedY *= timeMult;

		MoveInstantly(Vector2f(effectiveSpeedX, effectiveSpeedY), MoveType::Relative, true);
	}
}