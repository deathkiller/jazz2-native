#include "RollingRock.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Environment
{
	RollingRock::RollingRock()
		:
		_delayLeft(40.0f),
		_triggered(false),
		_soundCooldown(0.0f)
	{
	}

	Task<bool> RollingRock::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_renderer.setLayer(_renderer.layer() + 10);

		_id = details.Params[0];
		_triggerSpeedX = (float)*(int8_t*)&details.Params[1];
		_triggerSpeedY = (float)*(int8_t*)&details.Params[2];

		SetState(ActorState::CollideWithSolidObjects | ActorState::CollideWithSolidObjectsBelow | ActorState::IsSolidObject | ActorState::IsInvulnerable, true);
		SetState(ActorState::ApplyGravitation, false);
		_elasticity = 0.4f;
		_canHurtPlayer = false;

		async_await RequestMetadataAsync("Object/RollingRock"_s);
		SetAnimation(AnimState::Idle);

		_renderer.setRotation(_pos.X + _pos.Y);

		async_return true;
	}

	void RollingRock::OnUpdate(float timeMult)
	{
		UpdateFrozenState(timeMult);

		if (_soundCooldown > 0.0f) {
			_soundCooldown -= timeMult;
		}

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (GetState(ActorState::ApplyGravitation)) {
			// Rock is triggered
			float currentGravity = _levelHandler->Gravity;

			_speed.X = std::clamp(_speed.X + _externalForce.X * timeMult, -3.0f, 3.0f) * powf(0.6f, timeMult);

			bool up = IsPositionEmpty(_pos.X, _pos.Y - 33.0f);
			bool down = IsPositionEmpty(_pos.X - 4.0f, _pos.Y + 36.0f) && IsPositionEmpty(_pos.X + 4.0f, _pos.Y + 36.0f);
			bool left = IsPositionEmpty(_pos.X - 36.0f, _pos.Y) && IsPositionEmpty(_pos.X - 19.0f, _pos.Y - 19.0f) && IsPositionEmpty(_pos.X - 4.0f, _pos.Y + 33.0f);
			bool right = IsPositionEmpty(_pos.X + 36.0f, _pos.Y) && IsPositionEmpty(_pos.X + 19.0f, _pos.Y + 19.0f) && IsPositionEmpty(_pos.X + 4.0f, _pos.Y + 33.0f);

			if (down) {
				if (_pos.Y > _levelHandler->WaterLevel()) {
					_speed.Y += currentGravity * 0.25f * timeMult;
				} else {
					_speed.Y += currentGravity * timeMult;
				}
			}

			//int hits = 0;
			if ((_speed.X < 0.0f && !left) || (_speed.X > 0.0f && !right)) {
				// Bounce on X
				if (_soundCooldown <= 0.0f && std::abs(_speed.X) > 2.0f) {
					_soundCooldown = 140.0f;
					PlaySfx("Hit"_s, 0.6f, 0.4f);
				}

				_speed.X = _speed.X * -0.5f;
				_externalForce.X = _externalForce.X * -0.5f;
				//hits |= 0x1;
			}
			if ((_speed.Y < 0.0f && !up) || (_speed.Y > 0.0f && !down)) {
				// Bounce on Y
				if (_soundCooldown <= 0.0f && std::abs(_speed.Y) > 2.0f) {
					_soundCooldown = 140.0f;
					PlaySfx("Hit"_s, 0.6f, 0.4f);
				}

				_speed.Y = _speed.Y * -0.5f;
				//hits |= 0x2;
			}

			MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

			if (std::abs(_speed.X) < 0.2f && !down) {
				_speed = Vector2f::Zero;
				_externalForce = Vector2f::Zero;
				SetState(ActorState::ApplyGravitation, false);
				_canHurtPlayer = false;
			}

			_renderer.setRotation(_renderer.rotation() + _speed.X * 0.02f * timeMult);
		}

		// Trigger
		if (_triggered && _delayLeft > 0.0f) {
			_delayLeft -= timeMult;

			if (_delayLeft <= 0.0f) {
				SetState(ActorState::ApplyGravitation, true);
				_canHurtPlayer = true;

				_speed.X = _triggerSpeedX;
				_speed.Y = _triggerSpeedY;
				_externalForce.X = (_triggerSpeedX < 0.0f ? -4.0f : 4.0f);

				if (_soundCooldown <= 0.0f) {
					_soundCooldown = 140.0f;
					PlaySfx("Hit"_s, 0.6f, 0.4f);
				}
			}
		}
	}

	void RollingRock::OnUpdateHitbox()
	{
		UpdateHitbox(50, 50);
	}

	bool RollingRock::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* rollingRock = runtime_cast<RollingRock*>(other)) {
			float dx = (rollingRock->_pos.X - _pos.X);
			float dy = (rollingRock->_pos.Y - _pos.Y);
			float distance = Vector2f(dx, dy).Length();
			if (distance < 74.0f) {
				float relSpeeds = (std::abs(_speed.X) + std::abs(rollingRock->_speed.X)) * 0.5f;

				if (dx > 0.0f) {
					_speed.X = -relSpeeds;
					_externalForce.X = -std::abs(_externalForce.X);
				} else {
					_speed.X = relSpeeds;
					_externalForce.X = std::abs(_externalForce.X);
				}
				if (dx < 0.0f) {
					rollingRock->_speed.X = -relSpeeds;
					rollingRock->_externalForce.X = -std::abs(rollingRock->_externalForce.X);
				} else {
					rollingRock->_speed.X = relSpeeds;
					rollingRock->_externalForce.X = std::abs(rollingRock->_externalForce.X);
				}

				if (relSpeeds > 1.0f) {
					_triggered = true;
				}

				SetState(ActorState::CanBeFrozen, true);
			}
			return true;
		} else if (auto* player = runtime_cast<Player*>(other)) {
			if (_triggered) {
				float dx = (player->GetPos().X - _pos.X);
				float dy = (player->GetPos().Y - _pos.Y);
				float distance = Vector2f(dx, dy).Length();

				if (distance < 50.0f || dy > 0.0f) {
					float relSpeeds = (std::abs(_speed.X) + std::abs(player->GetSpeed().X)) * 0.5f;
					if (relSpeeds < 1.0f) {
						relSpeeds = (60.0f - distance) * 0.2f;
					}

					if (dx > 0.0f) {
						_speed.X = -2.0f;
						_externalForce.X = -std::abs(_externalForce.X);
					} else {
						_speed.X = 2.0f;
						_externalForce.X = std::abs(_externalForce.X);
					}
					if (dy > 0.0f) {
						_externalForce.Y -= 2.0f;
					}
					SetState(ActorState::ApplyGravitation, true);
				}
			}
		}

		return EnemyBase::OnHandleCollision(other);
	}

	void RollingRock::OnTriggeredEvent(EventType eventType, uint8_t* eventParams)
	{
		if (!_triggered && eventType == EventType::RollingRockTrigger && eventParams[0] == _id) {
			_triggered = true;

			_levelHandler->ShakeCameraView(60.0f);

			// TODO: Shake amplitude
			/*float distanceToPlayer;
			if (distanceToPlayer < 400.0f) {
				float amplitude = std::min((400.0f - distanceToPlayer) / (256.0f * 8.0f), 40.0f);
				_levelHandler->ShakeCameraView(amplitude);
			}*/
		}
	}

	bool RollingRock::IsPositionEmpty(float x, float y)
	{
		TileCollisionParams params = { TileDestructType::None, _speed.Y >= 0.0f };
		return _levelHandler->IsPositionEmpty(this, AABBf(x - 1.0f, y - 1.0f, x + 1.0f, y + 1.0f), params);
	}
}