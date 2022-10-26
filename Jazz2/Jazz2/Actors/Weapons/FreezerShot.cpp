#include "FreezerShot.h"
#include "../../LevelInitialization.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Solid/TriggerCrate.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

namespace Jazz2::Actors::Weapons
{
	FreezerShot::FreezerShot()
		:
		_fired(false)
	{
	}

	Task<bool> FreezerShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);
		_strength = 0;

		async_await RequestMetadataAsync("Weapon/Freezer"_s);

		AnimState state = AnimState::Idle;
		if ((_upgrades & 0x01) != 0) {
			_timeLeft = 38;
			state |= (AnimState)1;
			PlaySfx("FireUpgraded"_s);
		} else {
			_timeLeft = 44;
			PlaySfx("Fire"_s);
		}

		SetAnimation(state);

		async_return true;
	}

	void FreezerShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		float baseSpeed = ((_upgrades & 0x01) != 0 ? 8.0f : 6.0f);
		if (isFacingLeft) {
			_speed.X = std::min(0.0f, speed.X) - cosf(angleRel) * baseSpeed;
		} else {
			_speed.X = std::max(0.0f, speed.X) + cosf(angleRel) * baseSpeed;
		}
		_speed.Y = sinf(angleRel) * baseSpeed;

		_renderer.setRotation(angle);
		_renderer.setDrawEnabled(false);
	}

	void FreezerShot::OnUpdate(float timeMult)
	{
		int n = (timeMult > 0.9f ? 2 : 1);
		TileCollisionParams params = { TileDestructType::Weapon, _speed.Y >= 0.0f, WeaponType::Freezer, _strength };
		for (int i = 0; i < n; i++) {
			TryMovement(timeMult / n, params);
		}

		ShotBase::OnUpdate(timeMult);

		// TODO: Add particles

		if (_timeLeft <= 0.0f) {
			PlaySfx("WallPoof"_s);
		}

		if (!_fired) {
			_fired = true;
			MoveInstantly(_gunspotPos, MoveType::Absolute | MoveType::Force);
			_renderer.setDrawEnabled(true);
		}
	}

	void FreezerShot::OnUpdateHitbox()
	{
		// TODO: This is a quick fix for player cannot freeze springs
		AABBInner = AABBf(
			_pos.X - 4,
			_pos.Y - 2,
			_pos.X + 4,
			_pos.Y + 6
		);
	}

	void FreezerShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.8f;
		light.Brightness = 0.2f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 20.0f;
	}

	bool FreezerShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::SmokeWhite);

		return ShotBase::OnPerish(collider);
	}

	void FreezerShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);

		PlaySfx("WallPoof"_s);
	}
}