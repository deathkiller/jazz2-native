#include "ShotBase.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../SolidObjectBase.h"
#include "../Enemies/EnemyBase.h"

#include "../../../nCine/Base/Random.h"
#include "../../../nCine/Application.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	ShotBase::ShotBase()
		:
		_timeLeft(0),
		_upgrades(0),
		_strength(0),
		_lastRicochet(nullptr)
	{
	}

	Task<bool> ShotBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CanBeFrozen, false);

		async_return true;
	}

	Player* ShotBase::GetOwner()
	{
		return dynamic_cast<Player*>(_owner.get());
	}

	WeaponType ShotBase::GetWeaponType()
	{
		return WeaponType::Unknown;
	}

	void ShotBase::TriggerRicochet(ActorBase* other)
	{
		TimeStamp now = TimeStamp::now();

		if (other == nullptr) {
			if ((now - _lastRicochetTime).seconds() > 1.0f) {
				_lastRicochet = nullptr;
				_lastRicochetTime = now;
				OnRicochet();
			}
		} else {
			if (_lastRicochet != other) {
				_lastRicochet = other;
				_lastRicochetTime = now;
				OnRicochet();
			} else if ((now - _lastRicochetTime).seconds() < 1.0f) {
				DecreaseHealth(INT32_MAX);
			}
		}
	}

	void ShotBase::OnUpdate(float timeMult)
	{
		_timeLeft -= timeMult;
		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		}
	}

	bool ShotBase::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto enemyBase = dynamic_cast<Enemies::EnemyBase*>(other.get())) {
			if (enemyBase->CanCollideWithAmmo) {
				DecreaseHealth(INT32_MAX);
			}
		}

		return false;
	}

	void ShotBase::OnRicochet()
	{
		MoveInstantly(Vector2f(_speed.X * -0.4f, _speed.Y * -0.4f), MoveType::Relative | MoveType::Force);

		_speed.Y = _speed.Y * -0.9f + Random().NextFloat(-2.0f, 2.0f);
		_speed.X = _speed.X * -0.9f + Random().NextFloat(-2.0f, 2.0f);
	}

	void ShotBase::TryMovement(float timeMult, TileCollisionParams& params)
	{
		float accelY = (_internalForceY + _externalForce.Y) * timeMult;

		_speed.X = std::clamp(_speed.X, -16.0f, 16.0f);
		_speed.Y = std::clamp(_speed.Y + accelY, -16.0f, 16.0f);

		float effectiveSpeedX = _speed.X + _externalForce.X * timeMult;
		float effectiveSpeedY = _speed.Y + 0.5f * accelY;
		effectiveSpeedX *= timeMult;
		effectiveSpeedY *= timeMult;

		if (!MoveInstantly(Vector2f(effectiveSpeedX, effectiveSpeedY), MoveType::Relative, params)) {
			OnHitWall(timeMult);
		}
	}
}