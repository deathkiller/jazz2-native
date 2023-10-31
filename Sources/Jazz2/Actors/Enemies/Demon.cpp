#include "Demon.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Demon::Demon()
		:
		_attackTime(80.0f),
		_attacking(false),
		_stuck(false),
		_turnCooldown(0.0f)
	{
	}

	void Demon::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Demon"_s);
	}

	Task<bool> Demon::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(2);
		_scoreValue = 100;

		SetState(ActorState::CollideWithTilesetReduced, true);

		async_await RequestMetadataAsync("Enemy/Demon"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		PlaceOnGround();

		async_return true;
	}

	void Demon::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_currentTransition == nullptr) {
			if (_attacking) {
				if (_attackTime <= 0.0f) {
					_attacking = false;
					_attackTime = Random().NextFloat(60.0f, 90.0f);

					_speed.X = 0.0f;

					SetAnimation(AnimState::Idle);
					SetTransition((AnimState)1073741826, false);
				} else if (GetState(ActorState::CanJump)) {
					if (!CanMoveToPosition(_speed.X * 4.0f, 0.0f)) {
						if (_stuck) {
							MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
						} else if (_turnCooldown <= 0.0f) {
							SetFacingLeft(!IsFacingLeft());
							_speed.X = (IsFacingLeft() ? -1.8f : 1.8f);
							_turnCooldown = 90.0f;
							_stuck = true;
						} else {
							_attacking = false;
							_attackTime = 90.0f - _turnCooldown;
							_turnCooldown = 0.0f;

							_speed.X = 0.0f;

							SetAnimation(AnimState::Idle);
							SetTransition((AnimState)1073741826, false);
						}
					} else {
						_stuck = false;
					}

					_attackTime -= timeMult;
				}
			} else {
				if (_attackTime <= 0.0f) {
					for (auto& player : _levelHandler->GetPlayers()) {
						Vector2f newPos = player->GetPos();
						if ((newPos - _pos).Length() <= 300.0f) {
							_attacking = true;
							_attackTime = Random().NextFloat(130.0f, 180.0f);

							SetFacingLeft(newPos.X < _pos.X);
							_speed.X = (IsFacingLeft() ? -1.8f : 1.8f);

							SetAnimation((AnimState)1073741824);
							SetTransition((AnimState)1073741825, false);
							break;
						}
					}
				} else {
					_attackTime -= timeMult;
				}
			}
		}

		_turnCooldown -= timeMult;
	}

	void Demon::OnUpdateHitbox()
	{
		UpdateHitbox(28, 26);
	}

	bool Demon::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}