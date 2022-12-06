#include "Helmut.h"
#include "../../LevelInitialization.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Helmut::Helmut()
		:
		_idling(false),
		_stateTime(0.0f),
		_stuck(false),
		_turnCooldown(0.0f)
	{
	}

	void Helmut::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Helmut"_s);
	}

	Task<bool> Helmut::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(1);
		_scoreValue = 100;

		async_await RequestMetadataAsync("Enemy/Helmut"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;

		PlaceOnGround();

		async_return true;
	}

	void Helmut::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_idling) {
			if (_stateTime <= 0.0f) {
				_idling = false;
				SetFacingLeft(!IsFacingLeft());
				SetAnimation(AnimState::Walk);
				_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;

				_stateTime = Random().NextFloat(280.0f, 360.0f);
			}
		} else {
			if (_stateTime <= 0.0f) {
				_speed.X = 0;
				_idling = true;
				SetAnimation(AnimState::Idle);

				_stateTime = Random().NextFloat(70.0f, 190.0f);
			} else if (GetState(ActorState::CanJump)) {
				if (!CanMoveToPosition(_speed.X * 4.0f, 0.0f)) {
					if (_stuck) {
						MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
					} else if (_turnCooldown <= 0.0f) {
						SetFacingLeft(!IsFacingLeft());
						_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
						_turnCooldown = 90.0f;
						_stuck = true;
					} else {
						_speed.X = 0;
						_idling = true;
						SetAnimation(AnimState::Idle);

						_stateTime = Random().NextFloat(90.0f, 190.0f) - _turnCooldown;
						_turnCooldown = 0.0f;
					}
				} else {
					_stuck = false;
				}
			}
		}

		_stateTime -= timeMult;
		_turnCooldown -= timeMult;
	}

	void Helmut::OnUpdateHitbox()
	{
		UpdateHitbox(28, 26);
	}

	bool Helmut::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}