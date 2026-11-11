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

		_timeLeft = 46.0f * TicksToFrames;
		_strength = 3;

		SetAnimation(AnimState::Idle);

		_renderer.SetBlendingPreset(DrawableNode::BlendingPreset::Additive);

		async_return true;
	}

	void ShieldFireShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		constexpr float baseSpeed = 2.0f * TickSpeedScale;
		float sinAngle, cosAngle;
		sincosApprox(angleRel, sinAngle, cosAngle);
		_speed.X = (isFacingLeft ? -cosAngle : cosAngle) * baseSpeed;
		_speed.Y = sinAngle * baseSpeed;
		SetAcceleration(_speed.Normalized(), 0.125f * TickAccelScale, 10.0f * TickSpeedScale);
		SetCarriedSpeed(speed.X, true);

		_renderer.SetRotation(angle);
		_renderer.SetAlphaF(0.8f);
		_renderer.SetDrawEnabled(false);

		PlaySfx("Fire"_s, 0.8f);
	}

	void ShieldFireShot::OnUpdate(float timeMult)
	{
		std::int32_t n = GetMovementSubstepCount(timeMult);
		TileCollisionParams params = { TileDestructType::Weapon, false, WeaponType::Blaster, _strength };
		for (std::int32_t i = 0; i < n && params.WeaponStrength > 0; i++) {
			TryMovement(timeMult / n, params);
		}
		if (params.TilesDestroyed > 0) {
			if (auto* player = runtime_cast<Player>(_owner.get())) {
				player->AddScore(params.TilesDestroyed * 50);
			}
		}
		if (params.WeaponStrength <= 0) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		ShotBase::OnUpdate(timeMult);

		if (_fired < 255) {
			_fired++;
		}
		if (_fired >= 2) {
			RevealPastMuzzle(_gunspotPos);
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
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::SmokeGray);

		return ShotBase::OnPerish(collider);
	}

	void ShieldFireShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}