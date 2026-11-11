#include "ShieldWaterShot.h"
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
	ShieldWaterShot::ShieldWaterShot()
		: _fired(0)
	{
	}

	Task<bool> ShieldWaterShot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_await ShotBase::OnActivatedAsync(details);

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/ShieldWater"_s);

		_strength = 1;

		SetAnimation(AnimState::Idle);

		// The bubble lasts as long as its animation
		std::int32_t frameCount = (_currentAnimation != nullptr ? _currentAnimation->FrameCount : 0);
		_timeLeft = (frameCount > 0 ? float(frameCount) * TicksPerAnimFrame : 35.0f) * TicksToFrames;

		_renderer.SetBlendingPreset(DrawableNode::BlendingPreset::Additive);

		async_return true;
	}

	void ShieldWaterShot::OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft)
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
		SetAcceleration(Vector2f(_speed.X < 0.0f ? -1.0f : 1.0f, 0.0f), 0.125f * TickAccelScale, 0.0f);
		SetCarriedSpeed(speed.X, true);

		// A bubble starts bobbing in a random direction
		float bobSin, bobCos;
		sincosApprox(Random().NextFloat(0.0f, fRadAngle360), bobSin, bobCos);
		_speed.Y += bobSin * TickSpeedScale;

		_renderer.SetAlphaF(0.7f);
		_renderer.SetDrawEnabled(false);

		PlaySfx("Fire"_s);
	}

	void ShieldWaterShot::OnUpdate(float timeMult)
	{
		_age += timeMult;
		// The bubble rises, and after a while a bubble near the player turns back
		_speed.Y -= Buoyancy * TickAccelScale * timeMult;
		if (_age > TurnBackTicks * TicksToFrames && !_turnedBack) {
			auto* owner = _owner.get();
			if (owner != nullptr && (owner->GetPos() - _pos).Length() < TurnBackDistance) {
				_turnedBack = true;
				_speed.X = -_speed.X * 0.5f;
				_speed.Y = -_speed.Y;
				_accelerationDir.X = -_accelerationDir.X;
			}
		}

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
		if (_fired >= HiddenUpdates) {
			RevealPastMuzzle(_gunspotPos);
		}
	}

	void ShieldWaterShot::OnUpdateHitbox()
	{
		AABBInner = AABBf(_pos.X - 3, _pos.Y - 2, _pos.X + 3, _pos.Y + 4);
	}

	bool ShieldWaterShot::OnPerish(ActorBase* collider)
	{
		if (_timeLeft > 0.0f) {
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::Small);
		}

		return ShotBase::OnPerish(collider);
	}

	void ShieldWaterShot::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}