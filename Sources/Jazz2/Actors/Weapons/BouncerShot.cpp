#include "BouncerShot.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/CommonConstants.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Weapons
{
	BouncerShot::BouncerShot()
		: _fired(0), _bounces(0)
	{
		// It bounces back off a slope like the original, it doesn't ride up it
		_climbsSlopesInAir = false;
	}

	float BouncerShot::GetGravityModifier(float baseGravity, bool isRising) const
	{
		return Gravity * TickAccelScale * (_pos.Y > _levelHandler->GetWaterLevel() ? 0.25f : 1.0f);
	}

	Task<bool> BouncerShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		_upgrades = details.Params[0];

		async_await RequestMetadataAsync("Weapon/Bouncer"_s);

		AnimState state = AnimState::Idle;
		_timeLeft = 105.0f * TicksToFrames;
		if ((_upgrades & 0x1) != 0) {
			_strength = 2;
			state |= (AnimState)1;
			_elasticity = 9.0f / 8.0f;
			_horizontalSpeedLimit = _verticalSpeedLimit = 6.0f * TickSpeedScale;
			PlaySfx("FireUpgraded"_s, 1.0f, 0.5f);
		} else {
			_strength = 1;
			_elasticity = 7.0f / 8.0f;
			_horizontalSpeedLimit = 8.0f * TickSpeedScale;
			_verticalSpeedLimit = 4.0f * TickSpeedScale;
			PlaySfx("Fire"_s, 1.0f, 0.5f);
		}

		SetAnimation(state);

		async_return true;
	}

	void BouncerShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
	{
		_owner = owner;
		SetFacingLeft(isFacingLeft);

		_gunspotPos = gunspotPos;

		float angleRel = angle * (isFacingLeft ? -1 : 1);

		constexpr float baseSpeed = 5.0f * TickSpeedScale;
		float sinAngle, cosAngle;
		sincosApprox(angleRel, sinAngle, cosAngle);
		_speed.X = (isFacingLeft ? -cosAngle : cosAngle) * baseSpeed;
		_speed.Y = sinAngle * baseSpeed + std::abs(cosAngle) * TickSpeedScale;
		SetCarriedSpeed(std::abs(sinAngle) > 0.99f ? 0.0f : speed.X, true);
		ShortenUpShotLifetime(sinAngle);

		_acceleration = Acceleration * TickAccelScale;
		_accelerationDir = Vector2f(isFacingLeft ? -cosAngle : cosAngle, sinAngle);
		_internalForceY = ConstantFall * TickAccelScale;

		_renderer.SetDrawEnabled(false);
	}

	void BouncerShot::OnUpdate(float timeMult)
	{
		TileCollisionParams params = { TileDestructType::Weapon, _speed.Y >= 0.0f, WeaponType::Bouncer, _strength };
		_speed += _accelerationDir * (_acceleration * timeMult);
		float carriedDistance = AdvanceCarriedSpeed(timeMult);
		if (carriedDistance != 0.0f) {
			MoveInstantly(Vector2f(carriedDistance, 0.0f), MoveType::Relative, params);
		}
		TryStandardMovement(timeMult, params);
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

	void BouncerShot::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_fired >= 2) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = 0.8f;
			light.Brightness = 0.2f;
			light.RadiusNear = 0.0f;
			light.RadiusFar = 12.0f;
		}
	}

	bool BouncerShot::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::SmallDark);

		return ShotBase::OnPerish(collider);
	}

	void BouncerShot::OnBounce()
	{
		if (_bounces < 255) {
			_bounces++;
		}
		if (_bounces > ((_upgrades & 0x1) != 0 ? MaxBouncesPoweredUp : MaxBounces)) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		PlaySfx("Bounce"_s, 0.5f);
	}

	void BouncerShot::OnHitWall(float timeMult)
	{
		// The original always flips the push after a wall, a powered-up shot only one time in four
		if ((_upgrades & 0x1) == 0 || Random().Next() % 4 == 0) {
			_accelerationDir.X = -_accelerationDir.X;
		}
		OnBounce();
	}

	void BouncerShot::OnHitFloor(float timeMult)
	{
		OnBounce();
	}

	void BouncerShot::OnHitCeiling(float timeMult)
	{
		// A powered-up shot leaves a ceiling up to twice as fast
		if ((_upgrades & 0x1) != 0) {
			_speed.Y *= (9.0f + float(Random().Next() % 8)) / 9.0f;
		}
		OnBounce();
	}

	void BouncerShot::OnRicochet()
	{
		_speed.X = -_speed.X;
	}
}