#include "BlasterShot.h"
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
	BlasterShot::BlasterShot()
		: _fired(0)
	{
	}

	Task<bool> BlasterShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/Blaster"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x01) != 0) {
			_timeLeft = 25;
			state |= (AnimState)1;
			_strength = 2;
		} else {
			_timeLeft = 22;
			_strength = 1;
		}

		SetAnimation(state);

		_renderer.setBlendingPreset(DrawableNode::BlendingPreset::ADDITIVE);

		async_return true;
	}

	void BlasterShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		constexpr float baseSpeed = 12.0f;
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);
	}

	void BlasterShot::OnUpdate(float timeMult)
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

		if (_timeLeft <= 0.0f) {
			PlaySfx("WallPoof"_s);
		}

		_fired++;
		if (_fired == 2) {
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			_renderer.setDrawEnabled(true);
		}
	}

	void BlasterShot::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 3, _pos.Y - 2, _pos.X + 3, _pos.Y + 4);
	}

	void BlasterShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 0.8f;
			light.Brightness = 0.6f;
			light.RadiusNear = 5.0f;
			light.RadiusFar = 20.0f;
		}
	}

	bool BlasterShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::Small);

		return ShotBase::OnPerish(collider);
	}

	void BlasterShot::OnHitWall(float timeMult)
	{
		auto events = _levelHandler->EventMap();
		if (events != nullptr) {
			uint8_t* eventParams;
			switch (events->GetEventByPosition(_pos.X + _speed.X, _pos.Y + _speed.Y, &eventParams)) {
				case EventType::ModifierRicochet:
					TriggerRicochet(nullptr);
					return;
			}
		}

		DecreaseHealth(INT32_MAX);

		PlaySfx("WallPoof"_s);
	}

	void BlasterShot::OnRicochet()
	{
		ShotBase::OnRicochet();

		_renderer.setRotation(atan2f(_speed.Y, _speed.X));

		PlaySfx("Ricochet"_s);
	}
}