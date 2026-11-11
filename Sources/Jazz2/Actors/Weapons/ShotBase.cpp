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
		: _lastRicochet(nullptr), _timeLeft(0), _carriedSpeedX(0.0f), _acceleration(0.0f), _accelerationDir(Vector2f::Zero),
			_speedCap(0.0f), _timeRate(1.0f), _strength(0), _ricochetCount(0), _ricochetLimit(0), _upgrades(0), _carriedDecays(false),
			_frozenDuration(0.0f)
	{
	}

	float ShotBase::AdvanceCarriedSpeed(float timeMult)
	{
		float distance = _carriedSpeedX * timeMult;
		if (_carriedDecays && _carriedSpeedX != 0.0f) {
			float decay = CarriedSpeedDecay * TickAccelScale * timeMult;
			_carriedSpeedX = (std::abs(_carriedSpeedX) > decay ? _carriedSpeedX - std::copysign(decay, _carriedSpeedX) : 0.0f);
		}
		return distance;
	}

	void ShotBase::RevealPastMuzzle(Vector2f gunspotPos)
	{
		if (_renderer.IsDrawEnabled()) {
			return;
		}

		bool pastMuzzle;
		if (std::abs(_speed.X) >= std::abs(_speed.Y)) {
			pastMuzzle = (_speed.X < 0.0f ? _pos.X <= gunspotPos.X : _pos.X >= gunspotPos.X);
		} else {
			pastMuzzle = (_speed.Y < 0.0f ? _pos.Y <= gunspotPos.Y : _pos.Y >= gunspotPos.Y);
		}
		if (pastMuzzle) {
			_renderer.SetDrawEnabled(true);
		}
	}

	void ShotBase::ShortenUpShotLifetime(float sinAngle)
	{
		if (std::abs(sinAngle) > 0.99f) {
			_timeRate = 1.5f;
		}
	}

	void ShotBase::SetCarriedSpeed(float playerSpeedX, bool decays)
	{
		_carriedSpeedX = std::clamp(playerSpeedX, -MaxCarriedSpeed * TickSpeedScale, MaxCarriedSpeed * TickSpeedScale);
		_carriedDecays = decays;
	}

	void ShotBase::SetAcceleration(Vector2f direction, float acceleration, float speedCap)
	{
		_accelerationDir = direction;
		_acceleration = acceleration;
		_speedCap = speedCap;
	}

	Task<bool> ShotBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CanBeFrozen, false);

		async_return true;
	}

	Player* ShotBase::GetOwner()
	{
		return runtime_cast<Player>(_owner.get());
	}

	WeaponType ShotBase::GetWeaponType()
	{
		return WeaponType::Unknown;
	}

	WeaponType ShotBase::GetSourceWeapon()
	{
		return GetWeaponType();
	}

	bool ShotBase::ThawsFrozen() const
	{
		return false;
	}

	void ShotBase::TriggerRicochet(ActorBase* other)
	{
		TimeStamp now = TimeStamp::Now();

		if (other == nullptr) {
			if ((_ricochetLimit <= 0 || _ricochetCount < _ricochetLimit) && (now - _lastRicochetTime).GetSeconds() > 1.0f) {
				if (_ricochetCount < 255) {
					_ricochetCount++;
				}
				_lastRicochet = nullptr;
				_lastRicochetTime = now;
				OnRicochet();
			}
		} else {
			if (_lastRicochet != other) {
				_lastRicochet = other;
				_lastRicochetTime = now;
				OnRicochet();
			} else if ((now - _lastRicochetTime).GetSeconds() < 1.0f) {
				DecreaseHealth(INT32_MAX);
			}
		}
	}

	void ShotBase::OnUpdate(float timeMult)
	{
		_timeLeft -= timeMult * _timeRate;
		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		}
	}

	bool ShotBase::OnHandleCollision(ActorBase* other)
	{
		if (auto* enemyBase = runtime_cast<Enemies::EnemyBase>(other)) {
			if (enemyBase->CanCollideWithShots) {
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

	void ShotBase::OnRicochetExact()
	{
		MoveInstantly(Vector2f(-(_speed.X + _carriedSpeedX) * TicksToFrames, 0.0f), MoveType::Relative | MoveType::Force);

		constexpr float maxSpeedY = 8.0f * TickSpeedScale;
		_speed.X = -_speed.X;
		_accelerationDir.X = -_accelerationDir.X;
		_speed.Y = std::clamp(-_speed.Y + Random().NextFloat(-RicochetRandomSpeedY, RicochetRandomSpeedY) * TickSpeedScale, -maxSpeedY, maxSpeedY);
	}

	void ShotBase::TryMovement(float timeMult, TileCollisionParams& params)
	{
		float accelY = (_internalForceY + _externalForce.Y) * timeMult;

		_speed.X = std::clamp(_speed.X, -16.0f, 16.0f);
		_speed.Y = std::clamp(_speed.Y + accelY, -16.0f, 16.0f);

		float effectiveSpeedX = _speed.X + _carriedSpeedX + _externalForce.X * timeMult;
		float effectiveSpeedY = _speed.Y + 0.5f * accelY;
		if (_acceleration > 0.0f) {
			// Average speed over the step, the speed itself follows after the move
			effectiveSpeedX += _accelerationDir.X * _acceleration * 0.5f * timeMult;
			effectiveSpeedY += _accelerationDir.Y * _acceleration * 0.5f * timeMult;
		}
		effectiveSpeedX *= timeMult;
		effectiveSpeedY *= timeMult;

		if (_acceleration > 0.0f) {
			_speed += _accelerationDir * (_acceleration * timeMult);
			if (_speedCap > 0.0f) {
				float length = _speed.Length();
				if (length > _speedCap) {
					_speed *= _speedCap / length;
				}
			}
		}
		if (_carriedDecays && _carriedSpeedX != 0.0f) {
			float decay = CarriedSpeedDecay * TickAccelScale * timeMult;
			_carriedSpeedX = (std::abs(_carriedSpeedX) > decay ? _carriedSpeedX - std::copysign(decay, _carriedSpeedX) : 0.0f);
		}

		if (!MoveInstantly(Vector2f(effectiveSpeedX, effectiveSpeedY), MoveType::Relative, params)) {
			OnHitWall(timeMult);
		}
	}

	std::int32_t ShotBase::GetMovementSubstepCount(float timeMult) const
	{
		// What decides whether a step can pass through thin geometry is its *length*, so that is what is
		// bounded here - the same way and against the same limit as ActorBase::TryStandardMovement().
		//
		// Deriving the count from `timeMult` alone was a proxy for this. It did keep the length independent of
		// the frame rate, but left it proportional to the shot's speed, so the two halves of the problem were
		// each still wrong in one direction: a fast shot tunnelled through a one-tile wall at *any* frame rate,
		// and a slow one paid the fast one's cost anyway (five sub-steps at `MaxTimeMult` where one is already
		// tunnel-proof, times every shot on screen).
		float distance = std::max(std::abs(_speed.X + _carriedSpeedX + _externalForce.X * timeMult),
			std::abs(_speed.Y + _externalForce.Y * timeMult)) * timeMult;
		if (distance <= MaxMovementStep) {
			return 1;
		}

		return std::min<std::int32_t>((std::int32_t)(distance / MaxMovementStep) + 1, MaxMovementSubsteps);
	}
}