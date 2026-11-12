#include "LabRat.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	LabRat::LabRat()
		: _attackTime(0.0f), _landingTime(0.0f), _isAttacking(false), _canAttack(true), _isWaiting(false)
	{
	}

	void LabRat::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/LabRat"_s);
	}

	Task<bool> LabRat::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(1);
		_scoreValue = 200;

		SetState(ActorState::CollideWithTilesetReduced, true);

		async_await RequestMetadataAsync("Enemy/LabRat"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;

		_attackTime = Random().NextFloat(300.0f, 400.0f);

		PlaceOnGround();

		async_return true;
	}

	void LabRat::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_isAttacking) {
			if (_landingTime > 0.0f) {
				// Measured in JJ2+: it sits for a moment where it landed, then walks on the same way
				_landingTime -= timeMult;
				if (_landingTime <= 0.0f) {
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
					_isAttacking = false;
					_canAttack = false;
					_attackTime = 180.0f;
				}
			} else if (GetState(ActorState::CanJump) && _speed.Y >= 0.0f) {
				_speed.X = 0.0f;
				_landingTime = LandingTicks / TicksPerFrame;
			}
		}

		Walking(timeMult);
	}

	float LabRat::GetGravityModifier(float baseGravity, bool isRising) const
	{
		return (_isAttacking ? ReplaceLevelGravity(LeapGravity, baseGravity) : baseGravity);
	}

	void LabRat::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		std::uint8_t flags = 0;
		if (_isAttacking) flags |= 0x01;
		if (_canAttack) flags |= 0x02;
		if (_isWaiting) flags |= 0x08;
		dest.WriteValue<std::uint8_t>(flags);
		dest.WriteValueAsLE<float>(0.0f);	// Unused
		dest.WriteValueAsLE<float>(_attackTime);
		dest.WriteValueAsLE<float>(0.0f);	// Unused
	}

	void LabRat::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t flags = src.ReadValue<std::uint8_t>();
		_isAttacking = ((flags & 0x01) != 0);
		_canAttack = ((flags & 0x02) != 0);
		_isWaiting = ((flags & 0x08) != 0);
		if (_isWaiting) {
			SetAnimation(AnimState::Idle);
		}
		src.ReadValueAsLE<float>();	// Unused
		_attackTime = src.ReadValueAsLE<float>();
		src.ReadValueAsLE<float>();	// Unused

		if ((flags & 0x04) != 0) {
			// It no longer idles, an idling one from an older state walks on
			SetAnimation(AnimState::Walk);
			_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
		}

		if (_isAttacking) {
			// The leap was interrupted, finish it as its landing would
			_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
			_isAttacking = false;
			_canAttack = false;
			_attackTime = 180.0f;
		}
	}

	void LabRat::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	bool LabRat::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	void LabRat::Walking(float timeMult)
	{
		if (!_isAttacking && GetState(ActorState::CanJump)) {
			// Measured in JJ2+: it turns at every edge and never stops to idle
			float step = (IsFacingLeft() ? -4.0f : 4.0f) * DefaultSpeed;
			if (!CanMoveToPosition(step, 0.0f)) {
				// Blocked both ways it stops instead of turning back and forth, the original also stops against a wall
				if (CanMoveToPosition(-step, 0.0f)) {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = -step / 4.0f;
					SetWaiting(false);
				} else {
					SetWaiting(true);
				}
			} else if (_isWaiting) {
				_speed.X = step / 4.0f;
				SetWaiting(false);
			}
		}

		if (_isWaiting) {
			_speed.X = 0.0f;
			return;
		}

		if (!_isAttacking) {

			if (_canAttack) {
				if (std::abs(_speed.Y) < std::numeric_limits<float>::epsilon()) {
					AABBf aabb = AABBInner;
					// Jumps at a player above it too, as measured in JJ2+
					aabb.T -= 64;
					if (IsFacingLeft()) {
						aabb.L -= 128;
					} else {
						aabb.R += 128;
					}

					bool playerFound = false;
					_levelHandler->GetCollidingPlayers(aabb, [&playerFound](ActorBase*) {
						playerFound = true;
						return false;
					});

					if (playerFound) {
						Attack();
					}
				}
			} else {
				if (_attackTime <= 0.0f) {
					_canAttack = true;
					_attackTime = 180.0f;
				} else {
					_attackTime -= timeMult;
				}
			}

			if (Random().NextFloat() < 0.004f * timeMult) {
				PlaySfx("Noise"_s, 0.2f);
			}
		}
	}

	void LabRat::SetWaiting(bool value)
	{
		if (_isWaiting != value) {
			_isWaiting = value;
			// It stands while it can't go either way
			SetAnimation(value ? AnimState::Idle : AnimState::Walk);
		}
	}

	void LabRat::Attack()
	{
		SetTransition(AnimState::TransitionAttack, false);
		if (_currentTransition != nullptr && _currentTransition->FrameCount > 0) {
			SetAnimationTicks(_currentTransition->FrameCount * LeapFrameTicks);
		}

		// Measured in JJ2+: it leaps at 3 px/tick until it lands, only a wall stops it sooner
		_speed.X = (IsFacingLeft() ? -LeapSpeed : LeapSpeed);
		MoveInstantly(Vector2f(0.0f, -1.0f), MoveType::Relative);
		_speed.Y = -LeapSpeed;
		_internalForceY = 0.0f;
		_landingTime = 0.0f;
		_isAttacking = true;
		SetState(ActorState::CanJump, false);

		PlaySfx("Attack"_s);
	}
}