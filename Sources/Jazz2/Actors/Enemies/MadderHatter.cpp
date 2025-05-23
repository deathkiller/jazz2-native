﻿#include "MadderHatter.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	MadderHatter::MadderHatter()
		:
		_attackTime(0.0f),
		_stuck(false)
	{
	}

	void MadderHatter::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/MadderHatter"_s);
	}

	Task<bool> MadderHatter::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(3);
		_scoreValue = 200;

		SetState(ActorState::CollideWithTilesetReduced, true);

		async_await RequestMetadataAsync("Enemy/MadderHatter"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -DefaultSpeed : DefaultSpeed);

		PlaceOnGround();

		async_return true;
	}

	void MadderHatter::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_currentTransition == nullptr) {
			if (_attackTime <= 0.0f) {
				auto players = _levelHandler->GetPlayers();
				for (auto* player : players) {
					if (player->GetHealth() <= 0) {
						continue;
					}

					Vector2f newPos = player->GetPos();
					if ((newPos - _pos).Length() <= 200.0f) {
						SetFacingLeft(newPos.X < _pos.X);
						_speed.X = 0.0f;

						SetAnimation((AnimState)1073741824);
						SetTransition((AnimState)1073741824, false, [this]() {
							PlaySfx("Spit"_s);

							std::shared_ptr<BulletSpit> bulletSpit = std::make_shared<BulletSpit>();
							uint8_t bulletSpitParams[1];
							bulletSpitParams[0] = (IsFacingLeft() ? 1 : 0);
							bulletSpit->OnActivated(ActorActivationDetails(
								_levelHandler,
								Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -42 : 42), (std::int32_t)_pos.Y - 6, _renderer.layer() + 2),
								bulletSpitParams
							));
							_levelHandler->AddActor(bulletSpit);

							SetAnimation(AnimState::Walk);
							SetTransition((AnimState)1073741825, false, [this]() {
								_attackTime = Random().NextFloat(120.0f, 160.0f);
								_speed.X = (IsFacingLeft() ? -DefaultSpeed : DefaultSpeed);
							});
						});
						break;
					}
				}
			} else {
				_attackTime -= timeMult;
			}

			if (GetState(ActorState::CanJump)) {
				if (!CanMoveToPosition(_speed.X * 4, 0)) {
					if (_stuck) {
						MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
					} else {
						SetFacingLeft(!IsFacingLeft());
						_speed.X = (IsFacingLeft() ? -DefaultSpeed : DefaultSpeed);
						_stuck = true;
					}
				} else {
					_stuck = false;
				}
			}
		}
	}

	void MadderHatter::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	bool MadderHatter::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		if (_frozenTimeLeft <= 0.0f) {
			CreateSpriteDebris((AnimState)2, 1); // Cup
			CreateSpriteDebris((AnimState)3, 1); // Hat
		}

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	Task<bool> MadderHatter::BulletSpit::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen, false);
		CanCollideWithShots = false;

		SetFacingLeft(details.Params[0] != 0);
		_speed.X = (IsFacingLeft() ? -6.0f : 6.0f);

		if (_levelHandler->IsReforged()) {
			_timeLeft = 60;
			_externalForce.Y = -0.4f;
		} else {
			_timeLeft = 30;
			SetState(ActorState::ApplyGravitation, false);
		}

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Enemy/MadderHatter"_s);
		SetAnimation((AnimState)1073741826);

		async_return true;
	}

	void MadderHatter::BulletSpit::OnUpdate(float timeMult)
	{
		_timeLeft -= timeMult;
		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
			return;
		}

		EnemyBase::OnUpdate(timeMult);

		float angle = atan2f(_speed.Y, _speed.X);
		if (IsFacingLeft()) {
			angle -= fRadAngle180;
		}

		_renderer.setRotation(angle);
	}

	void MadderHatter::BulletSpit::OnUpdateHitbox()
	{
		UpdateHitbox(8, 8);
	}

	bool MadderHatter::BulletSpit::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		return false;
	}

	bool MadderHatter::BulletSpit::OnPerish(ActorBase* collider)
	{
		if (!_levelHandler->IsReforged()) {
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::Small);
		}

		return EnemyBase::OnPerish(collider);
	}

	void MadderHatter::BulletSpit::OnHitFloor(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void MadderHatter::BulletSpit::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void MadderHatter::BulletSpit::OnHitCeiling(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}