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

	void ShotBase::TriggerRicochet(ActorBase* other)
	{
		if (other == nullptr) {
			if (_lastRicochetFrame + 2 < theApplication().numFrames()) {
				_lastRicochet = nullptr;
				_lastRicochetFrame = theApplication().numFrames();
				OnRicochet();
			}
		} else {
			if (_lastRicochet != other) {
				_lastRicochet = other;
				_lastRicochetFrame = theApplication().numFrames();
				OnRicochet();
			} else if (_lastRicochetFrame + 2 >= theApplication().numFrames()) {
				DecreaseHealth(INT32_MAX);
			}
		}
	}

	void ShotBase::OnUpdate(float timeMult)
	{
		_timeLeft -= timeMult;
		if (_timeLeft <= 0) {
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

		_speed.Y = _speed.Y * -0.9f + (nCine::Random().Next() % 100 - 50) * 0.1f;
		_speed.X = _speed.X * -0.9f + (nCine::Random().Next() % 100 - 50) * 0.1f;
	}

	void ShotBase::TryMovement(float timeMult, TileCollisionParams& params)
	{
		float accelY = (_internalForceY + _externalForce.Y) * timeMult;

		_speed.X = std::clamp(_speed.X, -16.0f, 16.0f);
		_speed.Y = std::clamp(_speed.Y - accelY, -16.0f, 16.0f);

		float effectiveSpeedX = _speed.X + _externalForce.X * timeMult;
		float effectiveSpeedY = _speed.Y - 0.5f * accelY;
		effectiveSpeedX *= timeMult;
		effectiveSpeedY *= timeMult;

		if (!MoveInstantly(Vector2f(effectiveSpeedX, effectiveSpeedY), MoveType::Relative, params)) {
			OnHitWall();
		}
	}
}