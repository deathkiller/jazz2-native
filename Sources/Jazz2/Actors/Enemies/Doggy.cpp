#include "Doggy.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Weapons/ShotBase.h"
#include "../Weapons/FreezerShot.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Doggy::Doggy()
		:
		_attackSpeed(0.0f),
		_attackTime(0.0f),
		_noiseCooldown(120.0f),
		_stuck(false)
	{
	}

	void Doggy::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Enemy/Doggy"_s);
				break;
			case 1: // TSF Cat
				PreloadMetadataAsync("Enemy/Cat"_s);
				break;
		}
	}

	Task<bool> Doggy::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];

		SetHealthByDifficulty(3);
		_scoreValue = 200;

		SetState(ActorState::CollideWithTilesetReduced, true);

		switch (theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Enemy/Doggy"_s);
				_attackSpeed = 3.2f;
				break;
			case 1: // TSF Cat
				async_await RequestMetadataAsync("Enemy/Cat"_s);
				_attackSpeed = 3.8f;
				break;
		}
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f);

		PlaceOnGround();

		async_return true;
	}

	void Doggy::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_attackTime <= 0.0f) {
			_speed.X = (IsFacingLeft() ? -1 : 1);
			SetAnimation(AnimState::Walk);

			if (_noiseCooldown <= 0.0f) {
				_noiseCooldown = Random().NextFloat(100, 300);
				PlaySfx("Noise"_s, 0.4f);
			} else {
				_noiseCooldown -= timeMult;
			}
		} else {
			_attackTime -= timeMult;

			if (_noiseCooldown <= 0.0f) {
				_noiseCooldown = Random().NextFloat(25, 40);
				PlaySfx("Woof"_s);
			} else {
				_noiseCooldown -= timeMult;
			}
		}

		if (GetState(ActorState::CanJump)) {
			if (!CanMoveToPosition(_speed.X * 4.0f, 0.0f)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * (_attackTime <= 0.0f ? 1.0f : _attackSpeed);
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}
	}

	void Doggy::OnUpdateHitbox()
	{
		UpdateHitbox(50, 30);
	}

	bool Doggy::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase*>(other)) {
			DecreaseHealth(shotBase->GetStrength(), shotBase);

			if (_health <= 0.0f) {
				return true;
			}

			HandleFrozenStateChange(shotBase);

			if (!runtime_cast<Weapons::FreezerShot*>(shotBase)) {
				if (_attackTime <= 0.0f) {
					PlaySfx("Attack");
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * _attackSpeed;
					SetAnimation(AnimState::TransitionAttack);
				}

				_attackTime = 200.0f;
				_noiseCooldown = 45.0f;
			}
		}

		return false;
	}

	bool Doggy::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}