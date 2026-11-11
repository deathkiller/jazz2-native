#include "Crab.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Crab::Crab()
		: _noiseCooldown(80.0f), _stepCooldown(8.0f), _canJumpPrev(false), _stuck(false)
	{
	}

	void Crab::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Crab"_s);
	}

	Task<bool> Crab::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(3);
		_scoreValue = 300;

		async_await RequestMetadataAsync("Enemy/Crab"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
		_canJumpPrev = GetState(ActorState::CanJump);
		_verticalSpeedLimit = FallSpeedLimit;

		PlaceOnGround();
		// Only one that really falls plays the landing
		_canJumpPrev = true;

		async_return true;
	}

	void Crab::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (GetState(ActorState::CanJump)) {
			if (!_canJumpPrev) {
				_canJumpPrev = true;
				_speed.X = 0.0f;
				SetAnimation(AnimState::Walk);
				// It walks on only after the landing, which takes 7 ticks a frame in the original
				SetTransition(AnimState::TransitionFallToIdle, false, [this]() {
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
				});
				if (_currentTransition != nullptr && _currentTransition->FrameCount > 0) {
					SetAnimationTicks(_currentTransition->FrameCount * LandingFrameTicks);
				}
			}

			if (std::abs(_speed.X) > 0.0f && !CanMoveToPosition(_speed.X * 4, 0)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
					_stuck = true;
				}
			} else {
				_stuck = false;
			}

			if (_noiseCooldown <= 0.0f) {
				_noiseCooldown = Random().NextFloat(60, 160);
				PlaySfx("Noise"_s, 0.3f);
			} else {
				_noiseCooldown -= timeMult;
			}

			if (_stepCooldown <= 0.0f) {
				_stepCooldown = Random().NextFloat(7, 10);
				PlaySfx("Step"_s, 0.08f);
			} else {
				_stepCooldown -= timeMult;
			}
		} else {
			if (_canJumpPrev) {
				_canJumpPrev = false;
				SetAnimation(AnimState::Fall);
			}
			// The original loses a 32nd of the sideways speed each tick of a fall
			_speed.X *= std::pow(31.0f / 32.0f, timeMult * TicksPerFrame);
		}
	}

	float Crab::GetGravityModifier(float baseGravity, bool isRising) const
	{
		return ReplaceLevelGravity(FallGravity, baseGravity);
	}

	void Crab::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		std::uint8_t flags = 0;
		if (_canJumpPrev) flags |= 0x01;
		if (_stuck) flags |= 0x02;
		dest.WriteValue<std::uint8_t>(flags);
	}

	void Crab::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_canJumpPrev = ((flags & 0x01) != 0);
		_stuck = ((flags & 0x02) != 0);
		// A landing in progress is not restored, so it walks on right away
		if (_canJumpPrev && _speed.X == 0.0f) {
			_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
		}
	}

	void Crab::OnUpdateHitbox()
	{
		UpdateHitbox(26, 20);
	}

	bool Crab::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 2), Explosion::Type::Large);

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}