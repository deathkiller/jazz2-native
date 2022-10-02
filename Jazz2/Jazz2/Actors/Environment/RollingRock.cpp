#include "RollingRock.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	RollingRock::RollingRock()
		:
		_delayLeft(40.0f),
		_triggered(false),
		_speedUpDirection(SpeedUpDirection::None),
		_speedUpDirectionCooldown(0.0f),
		_soundCooldown(0.0f)
	{
	}

	Task<bool> RollingRock::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_renderer.setLayer(_renderer.layer() + 10);

		_id = details.Params[0];
		_triggerSpeedX = (float)*(int8_t*)&details.Params[1];
		_triggerSpeedY = (float)*(int8_t*)&details.Params[2];

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::ApplyGravitation, false);
		_elasticity = 0.4f;
		_canHurtPlayer = false;

		co_await RequestMetadataAsync("Object/RollingRock"_s);
		SetAnimation(AnimState::Idle);

		co_return true;
	}

	void RollingRock::OnUpdate(float timeMult)
	{
		// Movement
		if (GetState(ActorState::ApplyGravitation)) {
			float currentGravity = _levelHandler->Gravity;
			float accelY = (_internalForceY + _externalForce.Y) * timeMult;

			_speed.X = std::clamp(_speed.X, -16.0f, 16.0f);
			_speed.Y = std::clamp(_speed.Y + accelY, -16.0f, 16.0f);

			float effectiveSpeedX = (_speed.X + _externalForce.X) * timeMult * timeMult;
			float effectiveSpeedY = (_speed.Y + 0.5f * accelY) * timeMult;

			bool success = false;

			float maxYDiff = std::max(3.0f, std::abs(effectiveSpeedX) + 2.5f);
			float yDiff;
			for (yDiff = maxYDiff + effectiveSpeedY; yDiff >= -maxYDiff + effectiveSpeedY; yDiff -= CollisionCheckStep) {
				if (MoveInstantly(Vector2f(effectiveSpeedX, yDiff), MoveType::Relative)) {
					success = true;
					if (yDiff <= maxYDiff + effectiveSpeedY * 0.8f) {
						_speed.X += yDiff * (_speed.X < 0.0f ? -0.02f : 0.02f);
					}
					break;
				}
			}

			// Also try to move horizontally as far as possible
			float xDiff = std::abs(effectiveSpeedX);
			float maxXDiff = -xDiff;
			if (!success) {
				int sign = (effectiveSpeedX > 0.0f ? 1 : -1);
				for (; xDiff >= maxXDiff; xDiff -= CollisionCheckStep) {
					if (MoveInstantly(Vector2f(xDiff * sign, 0.0f), MoveType::Relative)) {
						break;
					}
				}

				// If no angle worked in the previous step, the actor is facing a wall
				if (xDiff > CollisionCheckStep || (xDiff > 0.0f && _elasticity > 0.0f)) {
					_speed.X = -(_elasticity * _speed.X);
					_speedUpDirection = SpeedUpDirection::None;
					_speedUpDirectionCooldown = 60.0f;

					if (_soundCooldown <= 0.0f) {
						_soundCooldown = 140.0f;
						PlaySfx("Hit"_s, 0.6f, 0.4f);
					}
				}
			}

			if (yDiff > 0.0f) {
				yDiff = std::min(8.0f, yDiff * yDiff);
			}

			_speed.X += (_speed.X < 0.0f ? -1.0f : 1.0f) * (yDiff * 0.02f - 0.02f) * timeMult;

			if (_speed.Y > 0.0f && yDiff <= 0.0f) {
				_speed.Y = 0.0f;

				if (_soundCooldown <= 0.0f) {
					_soundCooldown = 140.0f;
					PlaySfx("Hit"_s, 0.6f, 0.4f);
				}
			}

			if (std::abs(_speed.X) < 0.4f) {
				if (_speedUpDirectionCooldown > 0.0f) {
					_speedUpDirection = SpeedUpDirection::None;
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
				} else if (_speedUpDirection == SpeedUpDirection::Left) {
					_speed.X = -0.6f;
					_speedUpDirection = SpeedUpDirection::None;
					_speedUpDirectionCooldown = 60.0f;
				} else if (_speedUpDirection == SpeedUpDirection::Right) {
					_speed.X = 0.6f;
					_speedUpDirection = SpeedUpDirection::None;
					_speedUpDirectionCooldown = 60.0f;
				}
			} else if (yDiff < -1.0f) {
				if (_speed.X > 0.0f && _speedUpDirection != SpeedUpDirection::Left) {
					_speedUpDirection = SpeedUpDirection::Left;
				} else if (_speed.X < 0.0f && _speedUpDirection != SpeedUpDirection::Right) {
					_speedUpDirection = SpeedUpDirection::Right;
				}
			} else {
				_speedUpDirectionCooldown -= timeMult;
			}

			// Reduce all forces if they are present
			if (std::abs(_externalForce.X) > 0.0f) {
				if (_externalForce.X > 0.0f) {
					_externalForce.X = std::max(_externalForce.X - _friction * timeMult, 0.0f);
				} else {
					_externalForce.X = std::min(_externalForce.X + _friction * timeMult, 0.0f);
				}
			}
			_externalForce.Y = std::min(_externalForce.Y + currentGravity * 0.33f * timeMult, 0.0f);
			_internalForceY = std::min(_internalForceY + currentGravity * 0.33f * timeMult, 0.0f);

			_renderer.setRotation(_renderer.rotation() + effectiveSpeedX * 0.02f * timeMult);

			if (std::abs(_speed.X) <= 0.4f * timeMult && std::abs(_speed.Y) <= 0.4f * timeMult) {
				SetState(ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);
				_canHurtPlayer = false;
			}

			if (_soundCooldown > 0.0f) {
				_soundCooldown -= timeMult;
			}
		}

		// Trigger
		if (_triggered && _delayLeft > 0.0f) {
			_delayLeft -= timeMult;

			if (_delayLeft <= 0.0f) {
				SetState(ActorState::ApplyGravitation, true);
				_canHurtPlayer = true;

				_externalForce.X = _triggerSpeedX * 0.5f;
				_externalForce.Y = _triggerSpeedY * -0.5f;

				if (_soundCooldown <= 0.0f) {
					_soundCooldown = 140.0f;
					PlaySfx("Hit"_s, 0.6f, 0.4f);
				}
			}
		}
	}

	void RollingRock::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	void RollingRock::OnTriggeredEvent(EventType eventType, uint8_t* eventParams)
	{
		if (!_triggered && eventType == EventType::RollingRockTrigger && eventParams[0] == _id) {
			_triggered = true;
			_levelHandler->ShakeCameraView(60.0f);
		}
	}
}