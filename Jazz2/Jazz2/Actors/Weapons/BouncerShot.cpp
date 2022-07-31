#include "BouncerShot.h"
#include "../../LevelInitialization.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

namespace Jazz2::Actors::Weapons
{
	BouncerShot::BouncerShot()
		:
		_fired(false),
		_hitLimit(0.0f),
		_targetSpeedX(0.0f)
	{
	}

	Task<bool> BouncerShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_strength = 1;

		co_await RequestMetadataAsync("Weapon/Bouncer"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x1) != 0) {
			_timeLeft = 130;
			state |= (AnimState)1;
			PlaySfx("FireUpgraded"_s, 1.0f, 0.5f);
		} else {
			_timeLeft = 90;
			PlaySfx("Fire"_s, 1.0f, 0.5f);
		}

		SetAnimation(state);

		co_return true;
	}

	void BouncerShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		const float baseSpeed = 10.0f;
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - std::cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + std::cosf(angleRel) * baseSpeed;
		}
		_speed.Y = std::sinf(angleRel) * baseSpeed;

		_elasticity = 0.9f;

		_renderer.setDrawEnabled(false);
	}

	void BouncerShot::OnUpdate(float timeMult)
	{
		TryStandardMovement(timeMult);
		OnUpdateHitbox();
		CheckCollisions(timeMult);

		ShotBase::OnUpdate(timeMult);

		if (_hitLimit > 0.0f) {
			_hitLimit -= timeMult;
		}

		if ((_upgrades & 0x1) != 0 && _targetSpeedX != 0.0f) {
			if (_speed.X != _targetSpeedX) {
				float step = timeMult * 0.2f;
				if (std::abs(_speed.X - _targetSpeedX) < step) {
					_speed.X = _targetSpeedX;
				} else {
					_speed.X += step * ((_targetSpeedX < _speed.X) ? -1 : 1);
				}
			}
		}

		if (!_fired) {
			_fired = true;
			MoveInstantly(_gunspotPos, MoveType::Absolute, true);
			_renderer.setDrawEnabled(true);
		}
	}

	void BouncerShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.8f;
		light.Brightness = 0.2f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 12.0f;
	}

	bool BouncerShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer()), Explosion::Type::SmallDark);

		return ShotBase::OnPerish(collider);
	}

	void BouncerShot::OnHitWall()
	{
		if (_hitLimit > 3.0f) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		_hitLimit += 2.0f;
		PlaySfx("Bounce"_s, 0.5f);
	}

	void BouncerShot::OnHitFloor()
	{
		if (_hitLimit > 3.0f) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		_hitLimit += 2.0f;
		PlaySfx("Bounce"_s, 0.5f);
	}

	void BouncerShot::OnHitCeiling()
	{
		if (_hitLimit > 3.0f) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		_hitLimit += 2.0f;
		PlaySfx("Bounce"_s, 0.5f);
	}

	bool BouncerShot::OnHandleCollision(ActorBase* other)
	{
		// TODO
		/*switch (other) {
			case Queen queen :
				if (queen.IsInvulnerable) {
					OnRicochet();
				} else {
					base.OnHandleCollision(other);
				}
				break;

			default:
				base.OnHandleCollision(other);
				break;
		}*/

		return ShotBase::OnHandleCollision(other);
	}

	void BouncerShot::OnRicochet()
	{
		_speed.X = -_speed.X;
	}
}