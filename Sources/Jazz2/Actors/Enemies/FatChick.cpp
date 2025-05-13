#include "FatChick.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	FatChick::FatChick()
		: _isAttacking(false), _stuck(false)
	{
	}

	void FatChick::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/FatChick"_s);
	}

	Task<bool> FatChick::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(3);
		_scoreValue = 300;

		async_await RequestMetadataAsync("Enemy/FatChick"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;

		PlaceOnGround();

		async_return true;
	}

	void FatChick::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		for (auto& player : _levelHandler->GetPlayers()) {
			Vector2f targetPos = player->GetPos();
			float length = (_pos - targetPos).Length();
			if (length > 20.0f && length < 60.0f) {
				SetFacingLeft(_pos.X > targetPos.X);
				_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
				break;
			}
		}

		if (GetState(ActorState::CanJump)) {
			if (!CanMoveToPosition(_speed.X * 4, 0)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}

		if (!_isAttacking) {
			bool foundPlayer = false;
			_levelHandler->GetCollidingPlayers(AABBInner + Vector2f(_speed.X * 28.0f, 0.0f), [&foundPlayer](Actors::ActorBase* actor) {
				foundPlayer = true;
				return false;
			});
			if (foundPlayer) {
				Attack();
			}
		}
	}

	void FatChick::OnUpdateHitbox()
	{
		UpdateHitbox(20, 24);
	}

	bool FatChick::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	void FatChick::Attack()
	{
		// TODO: Play sound in the middle of transition
		// TODO: Apply force in the middle of transition
		PlaySfx("Attack"_s, 0.8f, 0.6f);

		SetTransition(AnimState::TransitionAttack, false, [this]() {
			_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
			_isAttacking = false;
		});
		_speed.X = 0.0f;
		_isAttacking = true;
	}
}