#include "ShieldLightningShot.h"
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
	ShieldLightningShot::ShieldLightningShot()
		:
		_fired(0)
	{
	}

	ShieldLightningShot::~ShieldLightningShot()
	{
		if (_noise != nullptr) {
			_noise->stop();
			_noise = nullptr;
		}
	}

	Task<bool> ShieldLightningShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/ShieldLightning"_s);

		_timeLeft = 30;
		_strength = 2;

		SetAnimation(AnimState::Idle);

		_renderer.setBlendingPreset(DrawableNode::BlendingPreset::ADDITIVE);

		async_return true;
	}

	void ShieldLightningShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		constexpr float baseSpeed = 7.0f;
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);

		_noise = PlaySfx("Fire"_s, 0.8f, 1.2f);
	}

	void ShieldLightningShot::OnUpdate(float timeMult)
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

		if (_noise != nullptr) {
			_noise->setPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
		}
	}

	void ShieldLightningShot::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 3, _pos.Y - 2, _pos.X + 3, _pos.Y + 4);
	}

	void ShieldLightningShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 0.2f;
			light.Brightness = 0.2f;
			light.RadiusNear = 5.0f;
			light.RadiusFar = 40.0f;
		}
	}

	bool ShieldLightningShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::Small);

		return ShotBase::OnPerish(collider);
	}

	void ShieldLightningShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}