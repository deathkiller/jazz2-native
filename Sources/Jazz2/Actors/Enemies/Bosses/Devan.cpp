#include "Devan.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"
#include "../../Weapons/ShotBase.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Bosses
{
	static constexpr AnimState ShootStart = (AnimState)10;
	static constexpr AnimState ShootInProgress = (AnimState)11;
	static constexpr AnimState ShootEnd = (AnimState)12;
	static constexpr AnimState ShootEnd2 = (AnimState)13;
	static constexpr AnimState CrouchStart = (AnimState)14;			// TODO: Unused
	static constexpr AnimState CrouchInProgress = (AnimState)15;	// TODO: Unused
	static constexpr AnimState CrouchEnd = (AnimState)16;			// TODO: Unused
	static constexpr AnimState JumpStart = (AnimState)17;			// TODO: Unused
	static constexpr AnimState JumpInProgress = (AnimState)18;		// TODO: Unused
	static constexpr AnimState JumpEnd = (AnimState)19;				// TODO: Unused
	static constexpr AnimState JumpEnd2 = (AnimState)20;			// TODO: Unused
	static constexpr AnimState DisarmedStart = (AnimState)22;
	static constexpr AnimState DisarmedGunDecor = (AnimState)23;
	static constexpr AnimState DisorientedStart = (AnimState)24;
	static constexpr AnimState Disoriented = (AnimState)25;
	static constexpr AnimState DisorientedWarpOut = (AnimState)26;
	static constexpr AnimState DevanBullet = (AnimState)27;

	static constexpr AnimState DemonFly = (AnimState)30;
	static constexpr AnimState DemonTransformStart = (AnimState)31;
	static constexpr AnimState DemonTransformEnd = (AnimState)32;
	static constexpr AnimState DemonTurn = (AnimState)33;
	static constexpr AnimState DemonSpewFireball = (AnimState)34;
	static constexpr AnimState DemonSpewFireballEnd = (AnimState)35;
	static constexpr AnimState DemonFireball = (AnimState)36;

	Devan::Devan()
		: _state(State::Waiting), _stateTime(0.0f), _crouchCooldown(60.0f), _endText(0), _shots(0), _isDemon(false),
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

		_state = State::WarpingIn;
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
			case State::WarpingIn: {
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

					_state = State::Transition;
					SetTransition(AnimState::TransitionWarpIn, false, [this]() {
						_state = State::Idling;
						_stateTime = 80.0f;

						SetState(ActorState::IsInvulnerable, false);
						_canHurtPlayer = true;
					});
				}

				break;
			}

			case State::Idling: {
				if (_stateTime <= 0.0f) {
					FollowNearestPlayer(State::Running1, Random().NextFloat(60.0f, 120.0f));
				}

				break;
			}

			case State::Running1: {
				if (_crouchCooldown <= 0.0 && ShouldCrouch()) {
					Crouch();
				} else if (_health < _maxHealth / 2) {
					_isDemon = true;
					_speed.X = 0.0f;
					_speed.Y = 0.0f;

					SetState(ActorState::IsInvulnerable, true);
					SetState(ActorState::CanBeFrozen, false);

					std::shared_ptr<DisarmedGun> gun = std::make_shared<DisarmedGun>();
					std::uint8_t gunParams[1] = { (std::uint8_t)(IsFacingLeft() ? 1 : 0) };
					gun->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? 12 : -12), (std::int32_t)_pos.Y - 8, _renderer.layer() + 40),
						gunParams
					));
					_levelHandler->AddActor(gun);

					_state = State::Transition;
					SetTransition(DisarmedStart, false, [this]() {
						SetAnimation(DemonFly);
						SetTransition(DemonTransformStart, false, [this]() {
							SetState(ActorState::ApplyGravitation | ActorState::IsInvulnerable, false);
							_state = State::DemonFlying;

							_lastPos = _pos;
							_targetPos = _lastPos + Vector2f(0.0f, -200.0f);
						});
					});
				} else if (_stateTime <= 0.0f) {
					if (Random().NextFloat() < 0.5f) {
						FollowNearestPlayer(State::Running1, Random().NextFloat(60, 120));
					} else {
						FollowNearestPlayer(State::Running2, Random().NextFloat(10, 30));
					}
				} else {
					if (!CanMoveToPosition(_speed.X, 0)) {
						SetFacingLeft(!IsFacingLeft());
						_speed.X = (IsFacingLeft() ? -4.0f : 4.0f);
					}
				}
				break;
			}

			case State::Running2: {
				if (_stateTime <= 0.0f) {
					_speed.X = 0.0f;

					_state = State::Transition;
					SetTransition(AnimState::TransitionRunToIdle, false, [this]() {
						SetTransition(ShootStart, false, [this]() {
							_shots = Random().Next(1, 8);
							Shoot();
						});
					});
				}
				break;
			}

			case State::Crouch: {
				if (_stateTime <= 0.0f) {
					_state = State::Transition;

					switch (_levelHandler->Difficulty()) {
						case GameDifficulty::Easy: _crouchCooldown = Random().NextFloat(360.0f, 600.0f); break;
						default:
						case GameDifficulty::Normal: _crouchCooldown = Random().NextFloat(180.0f, 360.0f); break;
						case GameDifficulty::Hard: _crouchCooldown = Random().NextFloat(100.0f, 240.0f); break;
					}

					SetState(ActorState::SkipPerPixelCollisions, false);
					SetTransition(CrouchEnd, false, [this]() {
						FollowNearestPlayer(State::Running1, Random().NextFloat(60.0f, 150.0f));
					});
				}
				break;
			}

			case State::DemonFlying: {
				if (_attackTime <= 0.0f) {
					_state = State::DemonSpewingFireball;
				} else {
					_attackTime -= timeMult;
					FollowNearestPlayerDemon(timeMult);
				}
				break;
			}

			case State::DemonSpewingFireball: {
				_state = State::Transition;
				SetTransition(DemonSpewFireball, false, [this]() {
					PlaySfx("SpitFireball"_s);

					std::shared_ptr<Fireball> fireball = std::make_shared<Fireball>();
					std::uint8_t fireballParams[1] = { (std::uint8_t)(IsFacingLeft() ? 1 : 0) };
					fireball->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -26 : 26), (std::int32_t)_pos.Y - 14, _renderer.layer() + 2),
						fireballParams
					));
					_levelHandler->AddActor(fireball);

					SetTransition(DemonSpewFireballEnd, false, [this]() {
						_state = State::DemonFlying;

						_attackTime = Random().NextFloat(100.0f, 240.0f);
					});
				});
				break;
			}

			case State::Falling: {
				if (GetState(ActorState::CanJump)) {
					_state = State::Transition;
					SetTransition(DisorientedStart, false, [this]() {
						SetTransition(Disoriented, false, [this]() {
							SetTransition(Disoriented, false, [this]() {
								SetTransition(DisorientedWarpOut, false, [this]() {
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
		_crouchCooldown -= timeMult;
	}

	void Devan::OnUpdateHitbox()
	{
		BossBase::OnUpdateHitbox();

		if (_state == State::Crouch) {
			// Smaller hitbox when Devan is crouching
			AABBInner.T = AABBInner.B - 16.0f;
		}
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

		_state = State::Transition;
		SetTransition(DemonTransformEnd, false, [this]() {
			SetState(ActorState::ApplyGravitation, true);

			_isDemon = false;
			_state = State::Falling;
			SetAnimation(AnimState::Freefall);
		});

		return false;
	}

	void Devan::FollowNearestPlayer(State newState, float time)
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
			bool willFaceLeft = (foundPos.X < _pos.X);

			float xOffset, yOffset, speedMult;
			if (_levelHandler->Difficulty() == GameDifficulty::Easy) {
				xOffset = 100.0f;
				yOffset = 70.0f;
				speedMult = 0.01f;
			} else {
				xOffset = 70.0f;
				yOffset = 30.0f;
				speedMult = 0.005f;
			}

			_targetPos = foundPos;
			_targetPos.X += (willFaceLeft ? xOffset : -xOffset);
			_targetPos.Y -= yOffset;

			Vector2f speed = ((_targetPos - _lastPos) * speedMult + _lastSpeed * 1.4f) / 2.4f;
			_lastPos.X += speed.X;
			_lastPos.Y += speed.Y;
			_lastSpeed = speed;

			if (IsFacingLeft() != willFaceLeft) {
				SetFacingLeft(willFaceLeft);
				SetTransition(DemonTurn, false);
			}
		}

		_anglePhase += timeMult * 0.02f;

		MoveInstantly(_lastPos + Vector2f(0.0f, sinf(_anglePhase) * 22.0f), MoveType::Absolute | MoveType::Force);
	}

	void Devan::Shoot()
	{
		PlaySfx("Shoot"_s);

		SetTransition(ShootInProgress, false, [this]() {
			std::shared_ptr<Bullet> bullet = std::make_shared<Bullet>();
			std::uint8_t fireballParams[1] = { (std::uint8_t)(IsFacingLeft() ? 1 : 0) };
			bullet->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -24 : 24), (std::int32_t)_pos.Y + 2, _renderer.layer() + 2),
				fireballParams
			));
			_levelHandler->AddActor(bullet);

			_shots--;

			SetTransition(ShootEnd, false, [this]() {
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
		SetTransition(ShootEnd2, false, [this]() {
			FollowNearestPlayer(State::Running1, Random().NextFloat(60.0f, 150.0f));
		});
	}

	void Devan::Crouch()
	{
		_speed.X = 0.0f;
		_state = State::Crouch;

		switch (_levelHandler->Difficulty()) {
			case GameDifficulty::Easy: _stateTime = Random().NextFloat(100.0f, 240.0f); break;
			default:
			case GameDifficulty::Normal: _stateTime = Random().NextFloat(60.0f, 120.0f); break;
			case GameDifficulty::Hard: _stateTime = Random().NextFloat(30.0f, 80.0f); break;
		}

		SetState(ActorState::SkipPerPixelCollisions, true);

		SetAnimation(CrouchInProgress);
		SetTransition(CrouchStart, false);
	}

	bool Devan::ShouldCrouch() const
	{
		constexpr float Distance = 64.0f;

		bool shouldCrouch = false;
		AABBf crouchAabb = AABB;
		crouchAabb.L -= Distance;
		crouchAabb.R += Distance;

		_levelHandler->FindCollisionActorsByAABB(this, crouchAabb, [this, &shouldCrouch](ActorBase* actor) {
			if (auto* shot = runtime_cast<Weapons::ShotBase*>(actor)) {
				float xSpeed = shot->GetSpeed().X;
				float x = shot->GetPos().X;
				float xSelf = _pos.X;
				// Check if the shot is moving towards the boss
				if (std::abs(xSpeed) > 0.0f && std::signbit(xSelf - x) == std::signbit(xSpeed)) {
					shouldCrouch = true;
					return false;
				}
			}
			return true;
		});

		return shouldCrouch;
	}

	Devan::DisarmedGun::DisarmedGun()
	{
	}

	Task<bool> Devan::DisarmedGun::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);
		_speed.X = (IsFacingLeft() ? 3.5f : -3.5f);
		_speed.Y = -2.0f;

		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithOtherActors | ActorState::CollideWithSolidObjects | ActorState::CollideWithTileset, false);

		_health = INT32_MAX;
		_timeLeft = 10.0f * FrameTimer::FramesPerSecond;

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation(DisarmedGunDecor);

		async_return true;
	}

	void Devan::DisarmedGun::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		_timeLeft -= timeMult;
		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		}
	}

	void Devan::DisarmedGun::OnUpdateHitbox()
	{
	}

	Devan::Bullet::Bullet()
	{
	}

	Task<bool> Devan::Bullet::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);
		_speed.X = (IsFacingLeft() ? -8.0f : 8.0f);

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithShots = false;

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation(DevanBullet);

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
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::Small);

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

	Devan::Fireball::Fireball()
	{
	}

	Task<bool> Devan::Fireball::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);
		_speed.X = (IsFacingLeft() ? -5.0f : 5.0f);
		_speed.Y = 3.5f;
		_timeLeft = 50.0f;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation(DemonFireball);

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
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::SmallDark);

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