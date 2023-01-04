#include "Crab.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Crab::Crab()
		:
		_noiseCooldown(80.0f),
		_stepCooldown(8.0f),
		_canJumpPrev(false),
		_stuck(false)
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

		PlaceOnGround();

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
				SetAnimation(AnimState::Walk);
				SetTransition(AnimState::TransitionFallToIdle, false);
			}

			if (!CanMoveToPosition(_speed.X * 4, 0)) {
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
		}
	}

	void Crab::OnUpdateHitbox()
	{
		UpdateHitbox(26, 20);
	}

	bool Crab::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2), Explosion::Type::Large);

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}