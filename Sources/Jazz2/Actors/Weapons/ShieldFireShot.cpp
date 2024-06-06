#include "ShieldFireShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	ShieldFireShot::ShieldFireShot()
		: _fired(0)
	{
	}

	Task<bool> ShieldFireShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/ShieldFire"_s);

		_timeLeft = 30;
		_strength = 1;

		SetAnimation(AnimState::Idle);

		_renderer.setBlendingPreset(DrawableNode::BlendingPreset::ADDITIVE);

		async_return true;
	}

	void ShieldFireShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		constexpr float baseSpeed = 8.0f;
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setAlphaF(0.8f);
		_renderer.setDrawEnabled(false);

		PlaySfx("Fire"_s, 0.8f);
	}

	void ShieldFireShot::OnUpdate(float timeMult)
	{
		int n = (timeMult > 0.9f ? 2 : 1);
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::Blaster, _strength };
		for (int i = 0; i < n && params.WeaponStrength > 0; i++) {
			TryMovement(timeMult / n, params);
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		ShotBase::OnUpdate(timeMult);

		_fired++;
		if (_fired == 2) {
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			_renderer.setDrawEnabled(true);
		}
	}

	void ShieldFireShot::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 3, _pos.Y - 2, _pos.X + 3, _pos.Y + 4);
	}

	void ShieldFireShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 0.8f;
			light.Brightness = 0.6f;
			light.RadiusNear = 0.0f;
			light.RadiusFar = 50.0f;
		}
	}

	bool ShieldFireShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::SmokeGray);

		return ShotBase::OnPerish(collider);
	}

	void ShieldFireShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}