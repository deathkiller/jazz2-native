#include "Sucker.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Sucker::Sucker()
		:
		_cycle(0),
		_cycleTimer(0.0f),
		_stuck(false)
	{
	}

	void Sucker::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Sucker"_s);
	}

	Task<bool> Sucker::OnActivatedAsync(const ActorActivationDetails& details)
	{
		LastHitDirection parentLastHitDir = (LastHitDirection)details.Params[0];

		SetHealthByDifficulty(1);
		_scoreValue = 100;
		_maxHealth = 4;

		async_await RequestMetadataAsync("Enemy/Sucker"_s);

		if (parentLastHitDir != LastHitDirection::None) {
			SetFacingLeft(parentLastHitDir == LastHitDirection::Left);

			_health = 1;

			SetState(ActorState::ApplyGravitation, false);
			SetAnimation(AnimState::Walk);
			SetTransition((AnimState)1073741824, false, [this]() {
				_speed.X = 0;
				SetAnimation(AnimState::Walk);
				SetState(ActorState::ApplyGravitation, true);
			});
			if (parentLastHitDir == LastHitDirection::Left || parentLastHitDir == LastHitDirection::Right) {
				_speed.X = 3 * (parentLastHitDir == LastHitDirection::Left ? -1 : 1);
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

	bool Sucker::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}