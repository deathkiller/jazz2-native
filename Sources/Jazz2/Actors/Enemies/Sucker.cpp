#include "Sucker.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Actors::Enemies
{
	Sucker::Sucker()
		: _cycle(0), _cycleTimer(0.0f), _stuck(false), _fallStage(FallStage::None)
	{
	}

	void Sucker::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Sucker"_s);
	}

	Task<bool> Sucker::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Direction parentLastHitDir = (Direction)details.Params[0];

		SetHealthByDifficulty(1);
		_scoreValue = 100;
		_maxHealth = 4;

		async_await RequestMetadataAsync("Enemy/Sucker"_s);

		if (parentLastHitDir != Direction::None) {
			SetFacingLeft(parentLastHitDir == Direction::Left);

			_health = 1;

			// Shots pass through it until it lands
			CanCollideWithShots = false;
			_fallStage = FallStage::Deflating;

			SetState(ActorState::ApplyGravitation, false);
			SetAnimation(AnimState::Walk);
			SetTransition((AnimState)1073741824, false, [this]() {
				_fallStage = FallStage::Falling;
				SetAnimation(AnimState::Fall);
				SetState(ActorState::ApplyGravitation, true);
			});
			// Measured in JJ2+: it deflates for 21 ticks, pushed away at 3 px a tick to begin with
			SetAnimationTicks(DeflateTicks);
			if (parentLastHitDir == Direction::Left || parentLastHitDir == Direction::Right) {
				_speed.X = (parentLastHitDir == Direction::Left ? -DeflateStartSpeed : DeflateStartSpeed);
			}
			PlaySfx("Deflate"_s);
		} else {
			SetAnimation(AnimState::Walk);

			_health = 4;
			PlaceOnGround();
		}

		async_return true;
	}

	void Sucker::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_fallStage == FallStage::Deflating) {
			// It speeds up sideways in the direction of the shot while sinking slowly
			float dir = (_speed.X < 0.0f ? -1.0f : 1.0f);
			_speed.X = std::clamp(_speed.X + dir * DeflateAcceleration * timeMult, -DeflateMaxSpeed, DeflateMaxSpeed);
			_speed.Y = DeflateSinkSpeed;
			return;
		} else if (_fallStage == FallStage::Falling) {
			_speed.X *= std::pow(FallSpeedKeptPerTick, timeMult * TicksPerFrame);
			return;
		}

		if (_currentTransition == nullptr && std::abs(_speed.X) > 0 && GetState(ActorState::CanJump)) {
			if (!CanMoveToPosition(_speed.X * 4, 0)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					_speed.X *= -1;
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}

		if (_currentTransition == nullptr && _frozenTimeLeft <= 0.0f) {
			if (_cycleTimer < 0.0f) {
				_cycle++;
				if (_cycle == 12) {
					_cycle = 0;
				}

				if (_cycle == 0) {
					PlaySfx("Walk1"_s, 0.2f);
				} else if (_cycle == 6) {
					PlaySfx("Walk2"_s, 0.2f);
				} else if (_cycle == 2 || _cycle == 7) {
					PlaySfx("Walk3"_s, 0.2f);
				}

				if ((_cycle >= 4 && _cycle < 7) || _cycle >= 9) {
					_speed.X = 0.6f * (IsFacingLeft() ? -1 : 1);
				} else {
					_speed.X = 0;
				}

				_cycleTimer = 5.0f;
			} else {
				_cycleTimer -= timeMult;
			}
		}
	}

	void Sucker::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteVariableInt32(_cycle);
		dest.WriteValueAsLE<float>(_cycleTimer);

		std::uint8_t flags = 0;
		if (_stuck) flags |= 0x01;
		if (_currentTransition != nullptr) flags |= 0x02;
		dest.WriteValue<std::uint8_t>(flags);
	}

	void Sucker::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_cycle = src.ReadVariableInt32();
		_cycleTimer = src.ReadValueAsLE<float>();

		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_stuck = ((flags & 0x01) != 0);
		_fallStage = FallStage::None;
		CanCollideWithShots = true;
		if ((flags & 0x02) != 0) {
			// Deflating was interrupted, finish it as its transition callback would
			_speed.X = 0;
			SetAnimation(AnimState::Walk);
			SetState(ActorState::ApplyGravitation, true);
		}
	}

	void Sucker::OnHitFloor(float timeMult)
	{
		EnemyBase::OnHitFloor(timeMult);

		if (_fallStage == FallStage::Falling) {
			_fallStage = FallStage::None;
			_speed.X = 0.0f;
			CanCollideWithShots = true;
			SetAnimation(AnimState::Walk);
		}
	}

	void Sucker::OnHitWall(float timeMult)
	{
		EnemyBase::OnHitWall(timeMult);

		// At a wall it stops flying sideways and drops
		if (_fallStage == FallStage::Deflating) {
			_fallStage = FallStage::Falling;
			_speed.X = 0.0f;
			SetFacingLeft(!IsFacingLeft());
			SetState(ActorState::ApplyGravitation, true);
		}
	}

	float Sucker::GetGravityModifier(float baseGravity, bool isRising) const
	{
		return (_fallStage == FallStage::Falling ? ReplaceLevelGravity(FallGravity, baseGravity) : baseGravity);
	}

	bool Sucker::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}