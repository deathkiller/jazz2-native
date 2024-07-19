#include "Monkey.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	Monkey::Monkey()
		:
		_isWalking(false),
		_stuck(false)
	{
	}

	void Monkey::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Monkey"_s);
	}

	Task<bool> Monkey::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_isWalking = (details.Params[0] != 0);

		SetHealthByDifficulty(3);
		_scoreValue = 200;

		async_await RequestMetadataAsync("Enemy/Monkey"_s);

		if (_isWalking) {
			SetFacingLeft(Random().NextBool());
			SetAnimation(AnimState::Walk);

			_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;

			PlaceOnGround();
		} else {
			SetAnimation(AnimState::Jump);
		}

		async_return true;
	}

	void Monkey::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (!_isWalking || _frozenTimeLeft > 0.0f) {
			return;
		}

		if (GetState(ActorState::CanJump) && std::abs(_speed.X) > 0) {
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
	}

	void Monkey::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	void Monkey::OnAnimationFinished()
	{
		EnemyBase::OnAnimationFinished();

		if (_currentTransition == nullptr) {
			bool found = false;
			Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

			auto players = _levelHandler->GetPlayers();
			for (auto* player : players) {
				Vector2f newPos = player->GetPos();
				if ((_pos - newPos).Length() < (_pos - targetPos).Length()) {
					targetPos = newPos;
					found = true;
				}
			}

			if (found) {
				Vector2f diff = (targetPos - _pos);
				if (diff.Length() < 280.0f) {
					if (_isWalking) {
						_speed.X = 0.0f;

						SetTransition((AnimState)1073741825, false, [this, targetPos]() {
							SetFacingLeft(targetPos.X < _pos.X);

							SetTransition((AnimState)1073741826, false, [this]() {
								std::shared_ptr<Banana> banana = std::make_shared<Banana>();
								uint8_t bananaParams[1];
								bananaParams[0] = (IsFacingLeft() ? 1 : 0);
								banana->OnActivated(ActorActivationDetails(
									_levelHandler,
									Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -8 : 8), (std::int32_t)_pos.Y - 8, _renderer.layer() + 2),
									bananaParams
								));
								_levelHandler->AddActor(banana);

								SetTransition((AnimState)1073741827, false, [this]() {

									SetTransition((AnimState)1073741824, false, [this]() {
										SetFacingLeft(Random().NextBool());
										_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;

									});
								});
							});
						});
					} else {
						SetFacingLeft(targetPos.X < _pos.X);

						SetTransition((AnimState)1073741826, false, [this]() {
							std::shared_ptr<Banana> banana = std::make_shared<Banana>();
							uint8_t bananaParams[1];
							bananaParams[0] = (IsFacingLeft() ? 1 : 0);
							banana->OnActivated(ActorActivationDetails(
								_levelHandler,
								Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -42 : 42), (std::int32_t)_pos.Y - 8, _renderer.layer() + 2),
								bananaParams
							));
							_levelHandler->AddActor(banana);

							SetTransition((AnimState)1073741827, false);
						});
					}
				}
			}
		}
	}

	bool Monkey::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	Task<bool> Monkey::Banana::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_health = 2;
		SetFacingLeft(details.Params[0] != 0);

		_speed.X = (IsFacingLeft() ? -8.0f : 8.0f);
		_speed.Y = -3.0f;

		async_await RequestMetadataAsync("Enemy/Monkey"_s);
		SetAnimation((AnimState)1073741828);

		_soundThrow = PlaySfx("BananaThrow"_s);

		async_return true;
	}

	void Monkey::Banana::OnUpdateHitbox()
	{
		UpdateHitbox(4, 4);
	}

	bool Monkey::Banana::OnPerish(ActorBase* collider)
	{
		_soundThrow = nullptr;

		_speed.X = 0.0f;
		_speed.Y = 0.0f;

		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		SetTransition((AnimState)1073741829, false, [this, collider]() {
			EnemyBase::OnPerish(collider);
		});

		PlaySfx("BananaSplat"_s, 0.6f);

		return false;
	}

	void Monkey::Banana::OnHitFloor(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Monkey::Banana::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Monkey::Banana::OnHitCeiling(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}