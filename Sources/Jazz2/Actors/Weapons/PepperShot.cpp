#include "PepperShot.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Explosion.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	PepperShot::PepperShot()
		: _fired(0)
	{
	}

	Task<bool> PepperShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];
		_strength = 1;

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Pepper"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x01) != 0) {
			_timeLeft = Random().NextFloat(32.0f, 40.0f);
			state |= (AnimState)1;
		} else {
			_timeLeft = Random().NextFloat(26.0f, 36.0f);
		}

		SetAnimation(state);
		PlaySfx("Fire"_s);

		_renderer.setBlendingPreset(DrawableNode::BlendingPreset::ADDITIVE);

		async_return true;
	}

	void PepperShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		float baseSpeed = ((_upgrades & 0x1) != 0 ? Random().NextFloat(5.0f, 7.2f) : Random().NextFloat(3.0f, 7.0f));
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);
	}

	void PepperShot::OnUpdate(float timeMult)
	{
		int n = (timeMult > 0.9f ? 2 : 1);
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::Pepper, _strength };
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
	
	void PepperShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2 && (_upgrades & 0x1) != 0) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 1.0f;
			light.Brightness = 1.0f;
			light.RadiusNear = 4.0f;
			light.RadiusFar = 16.0f;
		}
	}

	bool PepperShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::Pepper);

		return ShotBase::OnPerish(collider);
	}

	void PepperShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void PepperShot::OnRicochet()
	{
		ShotBase::OnRicochet();

		_renderer.setRotation(atan2f(_speed.Y, _speed.X));
	}
}