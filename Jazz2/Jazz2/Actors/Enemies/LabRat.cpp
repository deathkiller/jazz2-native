#include "LabRat.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	LabRat::LabRat()
		:
		_isAttacking(false),
		_canAttack(true),
		_idling(false),
		_canIdle(false),
		_stateTime(0.0f),
		_attackTime(0.0f)
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

		CollisionFlags |= CollisionFlags::CollideWithTilesetReduced;

		co_await RequestMetadataAsync("Enemy/LabRat"_s);

		SetAnimation(AnimState::Walk);

		SetFacingLeft(Random().NextBool());
		_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;

		_stateTime = Random().NextFloat(180.0f, 300.0f);
		_attackTime = Random().NextFloat(300.0f, 400.0f);

		co_return true;
	}

	void LabRat::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0) {
			return;
		}

		if (_idling) {
			Idle(timeMult);
		} else {
			Walking(timeMult);
		}

		_stateTime -= timeMult;
	}

	void LabRat::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	bool LabRat::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	void LabRat::Idle(float timeMult)
	{
		if (_stateTime <= 0.0f) {
			_idling = false;
			SetAnimation(AnimState::Walk);
			_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
			_speed.Y = 0.0f;

			_stateTime = Random().NextFloat(420, 540);
		} else {
			_stateTime -= timeMult;

			if (Random().NextFloat() < 0.008f * timeMult) {
				PlaySfx("Idle"_s, 0.2f);
			}
		}
	}

	void LabRat::Walking(float timeMult)
	{
		if (!_isAttacking) {
			if (GetState(ActorFlags::CanJump) && !CanMoveToPosition(_speed.X * 4, 0)) {
				SetFacingLeft(!IsFacingLeft());
				_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
			}

			if (_canAttack) {
				if (std::abs(_speed.Y) < std::numeric_limits<float>::epsilon()) {
					AABBf aabb = AABBInner;
					if (IsFacingLeft()) {
						aabb.L -= 128;
					} else {
						aabb.R += 128;
					}

					bool playerFound = false;
					_levelHandler->GetCollidingPlayers(aabb, [&playerFound](ActorBase*) -> bool {
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

			if (_canIdle) {
				if (_stateTime <= 0.0f) {
					_speed.X = 0;
					_idling = true;
					SetAnimation(AnimState::Idle);
					_canIdle = false;

					_stateTime = Random().NextFloat(260, 320);
				}
			} else {
				if (_stateTime <= 0.0f) {
					_canIdle = true;
					_stateTime = Random().NextFloat(60, 120);
				}
			}
		} else {
			_internalForceY += 0.08f * timeMult;
		}
	}

	void LabRat::Attack()
	{
		SetTransition(AnimState::TransitionAttack, false, [this]() {
				_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
				_isAttacking = false;
				_canAttack = false;

				_attackTime = 180;
			});

		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * 2.0f;
		MoveInstantly(Vector2f(0.0f, -1.0f), MoveType::Relative);
		_speed.Y = -1;
		_internalForceY = 0.5f;
		_isAttacking = true;
		SetState(ActorFlags::CanJump, false);

		PlaySfx("Attack"_s);
	}
}