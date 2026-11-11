#include "Devan.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"
#include "../../Weapons/ShotBase.h"

#include <cmath>

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
		: _state(State::Waiting), _stateTime(0.0f), _endText(0), _shots(0), _isDemon(false),
			_isDead(false), _evalAcc(0.0f), _jumpTicks(0.0f)
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
		_originPos = _pos;

		SetState(ActorState::CollideWithOtherActors, false);

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation(AnimState::Idle);

		_renderer.SetDrawEnabled(false);

		async_return true;
	}

	bool Devan::OnActivatedBoss()
	{
		// Both forms together, the first form ends when half of it is gone
		SetHealthByDifficulty(TotalHealth);

		_state = State::WarpingIn;
		_stateTime = StartDelay;
		_attackTime = DemonFlightTime;
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

		// It stays inside the level like a player, a jump that ignores tiles could carry it out
		Recti levelBounds = _levelHandler->GetLevelBounds();
		float boundedX = std::clamp(_pos.X, float(levelBounds.X), float(levelBounds.X + levelBounds.W));
		float boundedY = std::max(_pos.Y, float(levelBounds.Y));
		if (boundedX != _pos.X || boundedY != _pos.Y) {
			if (boundedY != _pos.Y) {
				_speed.Y = std::max(_speed.Y, 0.0f);
			}
			// The edge of the level turns it around like a wall
			if (boundedX != _pos.X && _state == State::Running1) {
				SetFacingLeft(boundedX < _pos.X);
				_speed.X = (IsFacingLeft() ? -RunSpeed : RunSpeed);
			}
			MoveInstantly(Vector2f(boundedX, boundedY), MoveType::Absolute | MoveType::Force);
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
						if (player->GetHealth() <= 0) {
							continue;
						}

						Vector2f newPos = player->GetPos();
						if ((_pos - newPos).Length() < (_pos - targetPos).Length()) {
							targetPos = newPos;
							found = true;
						}
					}

					if (found) {
						SetFacingLeft(targetPos.X < _pos.X);
					}

					_renderer.SetDrawEnabled(true);

					_state = State::Transition;
					SetTransition(AnimState::TransitionWarpIn, false, [this]() {
						_state = State::Idling;
						_stateTime = IdleTime;

						SetState(ActorState::IsInvulnerable, false);
						_canHurtPlayer = true;
					});
				}

				break;
			}

			case State::Idling: {
				if (_stateTime <= 0.0f) {
					StartRunning();
				}

				break;
			}

			case State::Running1: {
				float ticks = timeMult * TicksPerFrame;

				if (_health <= _maxHealth / 2) {
					_isDemon = true;
					_speed.X = 0.0f;
					_speed.Y = 0.0f;

					SetState(ActorState::IsInvulnerable, false);
					SetState(ActorState::CanBeFrozen, false);

					std::shared_ptr<DisarmedGun> gun = std::make_shared<DisarmedGun>();
					std::uint8_t gunParams[1] = { (std::uint8_t)(IsFacingLeft() ? 1 : 0) };
					gun->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? 12 : -12), (std::int32_t)_pos.Y - 8, _renderer.GetLayer() + 40),
						gunParams
					));
					_levelHandler->AddActor(gun);

					_state = State::Transition;
					SetTransition(DisarmedStart, false, [this]() {
						SetAnimation(DemonFly);
						SetTransition(DemonTransformStart, false, [this]() {
							SetState(ActorState::ApplyGravitation, false);
							_state = State::DemonFlying;
							_attackTime = DemonFlightTime;
							_demonTicks = 0.0f;
							_hoverSet = false;

							_lastPos = _pos;
						});
					});
					break;
				}

				// Falling off a ledge, it keeps its heading and decides nothing until it lands
				if (!GetState(ActorState::CanJump)) {
					_fellOffLedge = true;
					break;
				}
				if (_fellOffLedge) {
					_fellOffLedge = false;
					Land();
					break;
				}

				// Only a wall turns it around, the original runs off a ledge and falls
				AABBf ahead = AABBInner;
				float reach = (_speed.X < 0.0f ? -WallProbeDistance : WallProbeDistance);
				ahead.L += reach;
				ahead.R += reach;
				ahead.B -= WallProbeLift;
				Tiles::TileCollisionParams probe = { Tiles::TileDestructType::None, true };
				if (!_levelHandler->IsPositionEmpty(this, ahead, probe)) {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = (IsFacingLeft() ? -RunSpeed : RunSpeed);
				}

				_evalAcc += ticks;
				if (_evalAcc >= EvaluationTicks) {
					_evalAcc = std::fmod(_evalAcc, EvaluationTicks);
					Evaluate();
				}
				break;
			}

			case State::Jumping: {
				_jumpTicks += timeMult * TicksPerFrame;
				if (_jumpTicks < JumpRunTicks) {
					// It stands still for a moment first, as measured in the original
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
				} else if (!_jumpLaunched) {
					// The original rises through whatever is above and only lands on the way down
					_jumpLaunched = true;
					_speed.X = (IsFacingLeft() ? -JumpSpeedX : JumpSpeedX);
					_speed.Y = JumpSpeedY;
					SetState(ActorState::CanJump, false);
					SetState(ActorState::CollideWithTileset, false);
				} else if (_speed.Y >= 0.0f && !GetState(ActorState::CollideWithTileset)) {
					SetState(ActorState::CollideWithTileset, true);
				} else if (GetState(ActorState::CanJump) && _speed.Y >= 0.0f) {
					Land();
				}
				break;
			}

			case State::Crouch: {
				if (_stateTime <= 0.0f) {
					_state = State::Transition;

					SetState(ActorState::SkipPerPixelCollisions, false);
					SetTransition(CrouchEnd, false, [this]() {
						StartRunning();
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
					std::uint8_t fireballParams[2] = { (std::uint8_t)(IsFacingLeft() ? 1 : 0), (std::uint8_t)GetDifficultyIndex() };
					fireball->OnActivated(ActorActivationDetails(
						_levelHandler,
						Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -26 : 26), (std::int32_t)_pos.Y - 14, _renderer.GetLayer() + 2),
						fireballParams
					));
					_levelHandler->AddActor(fireball);

					SetTransition(DemonSpewFireballEnd, false, [this]() {
						_state = State::DemonFlying;

						_attackTime = DemonFlightTime;
						_demonTicks = 0.0f;
						_hoverSet = false;
					});
				});
				break;
			}

			case State::Falling: {
				if (GetState(ActorState::CanJump)) {
					_state = State::Transition;
					SetTransition(DisorientedStart, false, [this]() {
						_state = State::Disoriented;
						_stateTime = DisorientedTicks / TicksPerFrame;
						SetAnimation(Disoriented);
					});
				}
				break;
			}

			case State::Disoriented: {
				if (_stateTime <= 0.0f) {
					_state = State::Transition;
					SetTransition(DisorientedWarpOut, false, [this]() {
						BossBase::OnPerish(nullptr);
					});
				}
				break;
			}
		}

		_stateTime -= timeMult;
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
			if (player->GetHealth() <= 0) {
				continue;
			}

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
		float ticks = timeMult * TicksPerFrame;

		// The side and the height to hover at are picked when a flight starts
		if (!_hoverSet) {
			_hoverSet = true;
			_demonTicks = 0.0f;
			Player* player = FindNearestPlayer(DemonNoticeDistanceSqr);
			if (player == nullptr) {
				auto players = _levelHandler->GetPlayers();
				if (!players.empty()) {
					player = players[0];
				}
			}
			if (player != nullptr) {
				_demonTarget = player->shared_from_this();
				_hoverX = (_pos.X < player->GetPos().X ? -HoverOffsetX : HoverOffsetX);
				SetFacingLeft(_pos.X >= player->GetPos().X);
			} else {
				_demonTarget.reset();
			}
			_hoverY = -(float)Random().Next(0, 16) * 4.0f;
		}

		_demonTicks += ticks;

		if (auto target = _demonTarget.lock()) {
			float remaining = std::max<float>(129.0f - _demonTicks, 1.0f);
			Vector2f wanted;
			wanted.X = std::clamp<float>((target->GetPos().X + _hoverX - _lastPos.X) / remaining, -3.0f, 3.0f);
			wanted.Y = std::clamp<float>((target->GetPos().Y + _hoverY - _lastPos.Y) / remaining, -2.0f, 2.0f);

			// The speed per original tick is a low-pass of the wanted speed
			float blend = 1.0f - std::pow(0.25f, ticks);
			_demonVel += (wanted - _demonVel) * blend;
			_lastPos += _demonVel * TicksPerFrame * timeMult;

			float dx = _pos.X - target->GetPos().X;
			bool willFaceLeft = IsFacingLeft();
			if (dx < -TurnDistance) {
				willFaceLeft = false;
			} else if (dx > TurnDistance) {
				willFaceLeft = true;
			}
			if (willFaceLeft != IsFacingLeft()) {
				SetFacingLeft(willFaceLeft);
				SetTransition(DemonTurn, false);
			}
		}

		MoveInstantly(_lastPos, MoveType::Absolute | MoveType::Force);
	}

	void Devan::Shoot()
	{
		PlaySfx("Shoot"_s);

		SetTransition(ShootInProgress, false, [this]() {
			std::shared_ptr<Bullet> bullet = std::make_shared<Bullet>();
			std::uint8_t bulletParams[2] = { (std::uint8_t)(IsFacingLeft() ? 1 : 0), (std::uint8_t)GetDifficultyIndex() };
			bullet->OnActivated(ActorActivationDetails(
				_levelHandler,
				Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -24 : 24), (std::int32_t)_pos.Y + 2, _renderer.GetLayer() + 2),
				bulletParams
			));
			_levelHandler->AddActor(bullet);

			_shots++;

			SetTransition(ShootEnd, false, [this]() {
				// It keeps firing while the player stays in front of it and level with it, a shot every 8 ticks
				bool again = false;
				if (_shots < MaxShots) {
					Player* player = FindNearestPlayer(ShootDistanceSqr * 1.5f);
					if (player != nullptr) {
						bool isFacing = ((player->GetPos().X < _pos.X) == IsFacingLeft());
						again = (isFacing && std::abs(player->GetPos().Y - _pos.Y) < LevelBand);
					}
				}

				if (again) {
					Shoot();
				} else {
					Run();
				}
			});
			SetAnimationTicks(ShotRecoverTicks);
		});
		SetAnimationTicks(ShotAimTicks);
	}

	void Devan::Run()
	{
		SetTransition(ShootEnd2, false, [this]() {
			StartRunning();
		});
		SetAnimationTicks(ShootEndTicks);
	}

	void Devan::Crouch()
	{
		_speed.X = 0.0f;
		_state = State::Crouch;
		_stateTime = CrouchTicks / TicksPerFrame;

		SetState(ActorState::SkipPerPixelCollisions, true);

		SetAnimation(CrouchInProgress);
		SetTransition(CrouchStart, false);
	}

	void Devan::StartRunning()
	{
		_state = State::Running1;
		_evalAcc = 0.0f;

		Player* player = FindNearestPlayer(NoticeDistanceSqr);
		if (player != nullptr) {
			SetFacingLeft(player->GetPos().X < _pos.X);
		}

		_speed.X = (IsFacingLeft() ? -RunSpeed : RunSpeed);
		SetAnimation(AnimState::Run);
	}

	void Devan::Evaluate()
	{
		Player* player = FindNearestPlayer(NoticeDistanceSqr);
		if (player == nullptr) {
			return;
		}

		Vector2f diff = player->GetPos() - _pos;
		float distanceSqr = diff.SqrLength();
		bool isFacing = ((diff.X < 0.0f) == IsFacingLeft());

		if (!isFacing) {
			return;
		}

		if (std::abs(diff.Y) < LevelBand) {
			if (distanceSqr < ShootDistanceSqr && Random().Next(0, 8) < 4) {
				_speed.X = 0.0f;
				_shots = 0;
				_state = State::Transition;
				// The lengths of the shooting animations are measured in JJ2+
				SetTransition(AnimState::TransitionRunToIdle, false, [this]() {
					SetTransition(ShootStart, false, [this]() {
						Shoot();
					});
					SetAnimationTicks(ShootStartTicks);
				});
				SetAnimationTicks(RunStopTicks);
			} else if (distanceSqr > CrouchDistanceSqr && player->HasFiredWithin(RecentFireFrames)) {
				Crouch();
			}
		} else if (_pos.Y > _originPos.Y - 32.0f) {
			Jump();
		}
	}

	float Devan::GetGravityModifier(float baseGravity, bool isRising) const
	{
		// It rises and falls at the rate measured in the original, in a jump and off a ledge alike
		return (!_isDemon ? ReplaceLevelGravity(JumpGravity, baseGravity) : baseGravity);
	}

	void Devan::Land()
	{
		// It stands for a moment after a landing before it runs again, as measured in the original
		_speed.X = 0.0f;
		_state = State::Idling;
		_stateTime = LandingTicks / TicksPerFrame;
		SetAnimation(AnimState::Idle);
	}

	void Devan::Jump()
	{
		_state = State::Jumping;
		_jumpTicks = 0.0f;
		_jumpLaunched = false;
		_speed.X = 0.0f;
		_speed.Y = 0.0f;
		SetAnimation(AnimState::Jump);
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
		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * (float)(details.Params[1] + 4) * TicksPerFrame;
		_timeLeft = 70.0f / TicksPerFrame;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithShots = false;

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation(DevanBullet);

		async_return true;
	}

	void Devan::Bullet::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
		}
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
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::Small);

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
		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * (float)(details.Params[1] + 2) * TicksPerFrame;
		_speed.Y = (float)(details.Params[1] + 2) * TicksPerFrame;
		_timeLeft = 35.0f / TicksPerFrame;

		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/Devan"_s);
		SetAnimation(DemonFireball);

		async_return true;
	}

	void Devan::Fireball::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
		}
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
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::SmallDark);

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