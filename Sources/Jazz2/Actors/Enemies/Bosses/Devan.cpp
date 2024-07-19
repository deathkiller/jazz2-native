#include "Devan.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Bosses
{
	Devan::Devan()
		:
		_state(StateWaiting),
		_stateTime(0.0f),
		_endText(0),
		_shots(0),
		_isDemon(false),
		_isDead(false)
	{
	}

	void Devan::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/Devan"_s);
	}

	Task<bool> Devan::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];

		_scoreValue = 10000;

		SetState(ActorState::CollideWithOtherActors, false);

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation(AnimState::Idle);

		_renderer.setDrawEnabled(false);

		async_return true;
	}

	bool Devan::OnActivatedBoss()
	{
		SetHealthByDifficulty(140 * 2);

		_state = StateWarpingIn;
		_stateTime = 120.0f;
		_attackTime = 90.0f;
		_anglePhase = 0.0f;
		_shots = 0;
		_isDemon = false;
		_isDead = false;

		SetState(ActorState::CollideWithOtherActors, true);

		return true;
	}

	void Devan::OnUpdate(float timeMult)
	{
		if (_isDemon) {
			OnUpdateHitbox();
			HandleBlinking(timeMult);

			MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);
		} else {
			BossBase::OnUpdate(timeMult);
		}

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		switch (_state) {
			case StateWarpingIn: {
				if (_stateTime <= 0.0f) {
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
						SetFacingLeft(targetPos.X < _pos.X);
					}

					_renderer.setDrawEnabled(true);

					_state = StateTransition;
					SetTransition(AnimState::TransitionWarpIn, false, [this]() {
						_state = StateIdling;
						_stateTime = 80.0f;

						SetState(ActorState::IsInvulnerable, false);
						_canHurtPlayer = true;
					});
				}

				break;
			}

			case StateIdling: {
				if (_stateTime <= 0.0f) {
					FollowNearestPlayer(StateRunning1, Random().NextFloat(60.0f, 120.0f));
				}

				break;
			}

			case StateRunning1: {
				if (_stateTime <= 0.0f) {
					if (_health < _maxHealth / 2) {
						_isDemon = true;
						_speed.X = 0.0f;
						_speed.Y = 0.0f;

						SetState(ActorState::IsInvulnerable, true);
						SetState(ActorState::CanBeFrozen, false);

						_state = StateTransition;
						// DEMON_FLY
						SetAnimation((AnimState)669);
						// DEMON_TRANSFORM_START
						SetTransition((AnimState)670, false, [this]() {
							SetState(ActorState::ApplyGravitation | ActorState::IsInvulnerable, false);
							_state = StateDemonFlying;

							_lastPos = _pos;
							_targetPos = _lastPos + Vector2f(0.0f, -200.0f);
						});
					} else {
						if (Random().NextFloat() < 0.5f) {
							FollowNearestPlayer(StateRunning1, Random().NextFloat(60, 120));
						} else {
							FollowNearestPlayer(StateRunning2, Random().NextFloat(10, 30));
						}
					}

				} else {
					if (!CanMoveToPosition(_speed.X, 0)) {
						SetFacingLeft(!IsFacingLeft());
						_speed.X = (IsFacingLeft() ? -4.0f : 4.0f);
					}
				}
				break;
			}

			case StateRunning2: {
				if (_stateTime <= 0.0f) {
					_speed.X = 0.0f;

					_state = StateTransition;
					SetTransition(AnimState::TransitionRunToIdle, false, [this]() {
						SetTransition((AnimState)15, false, [this]() {
							_shots = Random().Next(1, 8);
							Shoot();
						});
					});
				}
				break;
			}

			case StateDemonFlying: {
				if (_attackTime <= 0.0f) {
					_state = StateDemonSpewingFireball;
				} else {
					_attackTime -= timeMult;
					FollowNearestPlayerDemon(timeMult);
				}
				break;
			}

			case StateDemonSpewingFireball: {
				_state = StateTransition;
				SetTransition((AnimState)673, false, [this]() {
					PlaySfx("SpitFireball"_s);

					std::shared_ptr<Fireball> fireball = std::make_shared<Fireball>();
					uint8_t fireballParams[1] = { (uint8_t)(IsFacingLeft() ? 1 : 0) };
					fireball->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -26 : 26), (std::int32_t)_pos.Y - 14, _renderer.layer() + 2),
						fireballParams
					));
					_levelHandler->AddActor(fireball);

					SetTransition((AnimState)674, false, [this]() {
						_state = StateDemonFlying;

						_attackTime = Random().NextFloat(100.0f, 240.0f);
					});
				});
				break;
			}

			case StateFalling: {
				if (GetState(ActorState::CanJump)) {
					_state = StateTransition;
					// DISORIENTED_START
					SetTransition((AnimState)666, false, [this]() {
						// DISORIENTED
						SetTransition((AnimState)667, false, [this]() {
							// DISORIENTED
							SetTransition((AnimState)667, false, [this]() {
								// DISORIENTED_WARP_OUT
								SetTransition((AnimState)6670, false, [this]() {
									BossBase::OnPerish(nullptr);
								});
							});
						});
					});
				}
				break;
			}
		}

		_stateTime -= timeMult;
	}

	bool Devan::OnPerish(ActorBase* collider)
	{
		if (_isDead) {
			return false;
		}

		StringView text = _levelHandler->GetLevelText(_endText);
		_levelHandler->ShowLevelText(text);

		_isDead = true;

		_speed.X = 0.0f;
		_speed.Y = 0.0f;

		SetState(ActorState::ApplyGravitation, false);

		_state = StateTransition;
		SetTransition((AnimState)671, false, [this]() {
			SetState(ActorState::ApplyGravitation, true);

			_isDemon = false;
			_state = StateFalling;
			SetAnimation(AnimState::Freefall);
		});

		return false;
	}

	void Devan::FollowNearestPlayer(int newState, float time)
	{
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
			_state = newState;
			_stateTime = time;

			SetFacingLeft(targetPos.X < _pos.X);

			_speed.X = (IsFacingLeft() ? -4.0f : 4.0f);

			//PlaySound("RUN");
			SetAnimation(AnimState::Run);
		}
	}

	void Devan::FollowNearestPlayerDemon(float timeMult)
	{
		bool found = false;
		Vector2f foundPos = Vector2f(FLT_MAX, FLT_MAX);

		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).Length() < (_pos - foundPos).Length()) {
				foundPos = newPos;
				found = true;
			}
		}

		if (found) {
			_targetPos = foundPos;
			_targetPos.Y -= 70.0f;

			_anglePhase += timeMult * 0.04f;

			Vector2f speed = ((_targetPos - _lastPos) / 70.0f + _lastSpeed * 1.4f) / 2.4f;
			_lastPos.X += speed.X;
			_lastPos.Y += speed.Y;
			_lastSpeed = speed;

			bool willFaceLeft = (speed.X < 0.0f);
			if (IsFacingLeft() != willFaceLeft) {
				SetTransition(AnimState::TransitionTurn, false, [this, willFaceLeft]() {
					SetFacingLeft(willFaceLeft);
				});
			}

			MoveInstantly(_lastPos + Vector2f(0.0f, sinf(_anglePhase) * 30.0f), MoveType::Absolute | MoveType::Force);
		}
	}

	void Devan::Shoot()
	{
		PlaySfx("Shoot"_s);

		SetTransition((AnimState)16, false, [this]() {
			std::shared_ptr<Bullet> bullet = std::make_shared<Bullet>();
			uint8_t fireballParams[1] = { (uint8_t)(IsFacingLeft() ? 1 : 0) };
			bullet->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -24 : 24), (std::int32_t)_pos.Y + 2, _renderer.layer() + 2),
				fireballParams
			));
			_levelHandler->AddActor(bullet);

			_shots--;

			SetTransition((AnimState)17, false, [this]() {
				if (_shots > 0) {
					Shoot();
				} else {
					Run();
				}
			});
		});
	}

	void Devan::Run()
	{
		SetTransition((AnimState)18, false, [this]() {
			FollowNearestPlayer(StateRunning1, Random().NextFloat(60.0f, 150.0f));
		});
	}

	Task<bool> Devan::Bullet::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);
		_speed.X = (IsFacingLeft() ? -8.0f : 8.0f);

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithAmmo = false;

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation((AnimState)668);

		async_return true;
	}

	void Devan::Bullet::OnUpdateHitbox()
	{
		UpdateHitbox(6, 6);
	}

	void Devan::Bullet::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.8f;
		light.Brightness = 0.8f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 28.0f;
	}

	bool Devan::Bullet::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::Small);

		return EnemyBase::OnPerish(collider);
	}

	void Devan::Bullet::OnHitFloor(float timeMult)
	{
		PlaySfx("WallPoof"_s);
		DecreaseHealth(INT32_MAX);
	}

	void Devan::Bullet::OnHitWall(float timeMult)
	{
		PlaySfx("WallPoof"_s);
		DecreaseHealth(INT32_MAX);
	}

	void Devan::Bullet::OnHitCeiling(float timeMult)
	{
		PlaySfx("WallPoof"_s);
		DecreaseHealth(INT32_MAX);
	}

	Task<bool> Devan::Fireball::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);
		_speed.X = (IsFacingLeft() ? -5.0f : 5.0f);
		_speed.Y = 5.0f;
		_timeLeft = 50.0f;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation((AnimState)675);

		async_return true;
	}

	void Devan::Fireball::OnUpdateHitbox()
	{
		UpdateHitbox(6, 6);
	}

	void Devan::Fireball::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.85f;
		light.Brightness = 0.4f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 30.0f;
	}

	bool Devan::Fireball::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::SmallDark);

		PlaySfx("Flap"_s);

		return EnemyBase::OnPerish(collider);
	}

	void Devan::Fireball::OnHitFloor(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Devan::Fireball::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Devan::Fireball::OnHitCeiling(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}