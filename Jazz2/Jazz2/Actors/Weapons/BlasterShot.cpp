#include "BlasterShot.h"
#include "../../LevelInitialization.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Solid/TriggerCrate.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

namespace Jazz2::Actors::Weapons
{
	BlasterShot::BlasterShot()
		:
		_fired(false)
	{
	}

	Task<bool> BlasterShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];

		CollisionFlags = (CollisionFlags & ~CollisionFlags::ApplyGravitation) | CollisionFlags::SkipPerPixelCollisions;

		co_await RequestMetadataAsync("Weapon/Blaster"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x1) != 0) {
			_timeLeft = 28;
			state |= (AnimState)1;
			_strength = 2;
		} else {
			_timeLeft = 25;
			_strength = 1;
		}

		SetAnimation(state);

		_renderer.setBlendingPreset(DrawableNode::BlendingPreset::ADDITIVE);

		co_return true;
	}

	void BlasterShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
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

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);
	}

	void BlasterShot::OnUpdate(float timeMult)
	{
		float halfTimeMult = timeMult * 0.5f;

		for (int i = 0; i < 2; i++) {
			TryMovement(halfTimeMult);
			OnUpdateHitbox();
			CheckCollisions(halfTimeMult);
		}

		ShotBase::OnUpdate(timeMult);

		if (_timeLeft <= 0.0f) {
			PlaySfx("WallPoof"_s);
		}

		if (!_fired) {
			_fired = true;
			MoveInstantly(_gunspotPos, MoveType::Absolute, true);
			_renderer.setDrawEnabled(true);
		}
	}

	void BlasterShot::OnUpdateHitbox()
	{
		AABBInner = AABBf(
			_pos.X - 8,
			_pos.Y - 8,
			_pos.X + 8,
			_pos.Y + 8
		);
	}

	void BlasterShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.8f;
		light.Brightness = 0.6f;
		light.RadiusNear = 5.0f;
		light.RadiusFar = 20.0f;
	}

	bool BlasterShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer()), Explosion::Type::Small);

		return ShotBase::OnPerish(collider);
	}

	void BlasterShot::OnHitWall()
	{
		DecreaseHealth(INT32_MAX);

		PlaySfx("WallPoof"_s);
	}

	bool BlasterShot::OnHandleCollision(ActorBase* other)
	{
		// TODO
		/*switch (other) {
			case Queen queen :
				if (queen.IsInvulnerable) {
					if (lastRicochet != other) {
						lastRicochet = other;
						OnRicochet();
					}
				} else {
					base.OnHandleCollision(other);
				}
				break;

			default:
				base.OnHandleCollision(other);
				break;
		}*/

		if (auto triggerCrate = dynamic_cast<Solid::TriggerCrate*>(other)) {
			if (_lastRicochet != other) {
				_lastRicochet = other;
				OnRicochet();
			}
			return true;
		}

		return ShotBase::OnHandleCollision(other);
	}

	void BlasterShot::OnRicochet()
	{
		ShotBase::OnRicochet();

		_renderer.setRotation(std::atan2(_speed.Y, _speed.X));

		PlaySfx("Ricochet"_s);
	}
}