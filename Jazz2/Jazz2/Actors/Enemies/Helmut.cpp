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
		_stuck(false)
	{
	}

	void Helmut::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Helmut"_s);
	}

	Task<bool> Helmut::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_pos.Y -= 6.0f;

		SetHealthByDifficulty(1);
		_scoreValue = 100;

		co_await RequestMetadataAsync("Enemy/Helmut"_s);
		SetAnimation(AnimState::Walk);

		SetFacingLeft(Random().NextBool());
		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;

		co_return true;
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
				_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;

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
					} else {
						SetFacingLeft(!IsFacingLeft());
						_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
						_stuck = true;
					}
				} else {
					_stuck = false;
				}
			}
		}

		_stateTime -= timeMult;
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