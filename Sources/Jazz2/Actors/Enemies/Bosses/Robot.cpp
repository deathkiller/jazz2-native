#include "Robot.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"

#include "../../../../nCine/Base/Random.h"

#include <cmath>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Bosses
{
	Robot::Robot()
		: _state(StateWaiting), _ticks(0.0f), _gaitPhase(0.0f), _gaitBase(FastGaitSpeed), _tickAcc(0.0f), _counter(0), _shots(0),
			_attackPhase(0), _ballThrown(false), _ballSpeed(0.0f), _deathAcc(0.0f)
	{
	}

	void Robot::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/Robot"_s);
	}

	Task<bool> Robot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_scoreValue = 2000;
		// It comes down from where the level puts it, as measured in JJ2+
		_originPos = _pos;

		async_await RequestMetadataAsync("Boss/Robot"_s);
		SetAnimation(AnimState::Idle);

		SetFacingLeft(true);

		_renderer.SetDrawEnabled(false);
		// It waits in place until the fight starts
		SetState(ActorState::ApplyGravitation, false);

		async_return true;
	}

	void Robot::Activate()
	{
		if (_levelHandler->IsReforged()) {
			SetHealthByDifficulty(100);
		} else {
			_health = 100;
			_maxHealth = 100;
		}

		_renderer.SetDrawEnabled(true);
		_state = StateCopter;
		_counter = 0;
		_tickAcc = 0.0f;

		// It descends at a constant speed without gravity
		SetState(ActorState::ApplyGravitation, false);
		MoveInstantly(_originPos, MoveType::Absolute);

		SetAnimation((AnimState)1073741828);
		SetAnimationTicks(CopterLoopTicks);
	}

	void Robot::Deactivate()
	{
		_renderer.SetDrawEnabled(false);
		_state = StateWaiting;
		SetState(ActorState::ApplyGravitation, true);
	}

	bool Robot::IsDeathFinished() const
	{
		return (_state == StateDead);
	}

	void Robot::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		float ticks = timeMult * TicksPerFrame;

		switch (_state) {
			case StateCopter: {
				if (HasGroundBelow()) {
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					_state = StateLanding;
					_ticks = 0.0f;
					// The landing ends with its animation, the timer is only a fallback
					SetTransition((AnimState)1073741829, false, [this]() {
						if (_state == StateLanding) {
							SetState(ActorState::ApplyGravitation, true);
							_gaitBase = FastGaitSpeed;
							StartRunning();
						}
					});
					SetAnimationTicks(LandingTicks);
				} else {
					_speed.Y = DropSpeed;
				}
				break;
			}
			case StateLanding: {
				_ticks += ticks;
				if (_ticks >= LandingTicks * 2.0f) {
					SetState(ActorState::ApplyGravitation, true);
					_gaitBase = FastGaitSpeed;
					StartRunning();
				}
				break;
			}
			case StateRunning: {
				_gaitPhase = std::fmod(_gaitPhase + ticks, 16.0f);
				if (!CanMoveToPosition(_speed.X, 0)) {
					SetFacingLeft(!IsFacingLeft());
				}
				float speed = (std::sin(fPi * std::floor(_gaitPhase) / 16.0f) + _gaitBase) * TicksPerFrame;
				_speed.X = (IsFacingLeft() ? -speed : speed);
			}
			// fallthrough
			case StateIdle: {
				// The decisions are made once per original tick with a coin flip, so they run on whole ticks
				_tickAcc += ticks;
				while (_tickAcc >= 1.0f && (_state == StateRunning || _state == StateIdle)) {
					_tickAcc -= 1.0f;
					LogicTick();
				}
				break;
			}
			case StateAttacking: UpdateAttack(timeMult); break;
			case StateDying: UpdateDying(timeMult); break;
		}
	}

	void Robot::OnHealthChanged(ActorBase* collider)
	{
		EnemyBase::OnHealthChanged(collider);

		std::int32_t n;
		if (_health < 10) {
			n = 4 + Random().Next(0, 7);
		} else if (_health < 20) {
			n = 4 + Random().Next(0, 3);
		} else {
			n = 2 + Random().Next(0, 3);
		}
		for (std::int32_t i = 0; i < n; i++) {
			CreateSpriteDebris((AnimState)Random().Fast(100, 109), 1);
		}

		PlaySfx("Shrapnel"_s);
	}

	bool Robot::OnPerish(ActorBase* collider)
	{
		if (_state == StateDying || _state == StateDead) {
			return false;
		}

		AddScoreToCollider(collider);

		_state = StateDying;
		_ticks = 0.0f;
		_deathAcc = 0.0f;
		_speed.X = 0.0f;
		_speed.Y = 0.0f;
		_canHurtPlayer = false;
		CanCollideWithShots = false;
		SetState(ActorState::IsInvulnerable, true);
		ForceCancelTransition();
		SetAnimation(AnimState::Idle);
		return false;
	}

	bool Robot::HasGroundBelow() const
	{
		TileCollisionParams params = { TileDestructType::None, true };
		// A short column below the feet, the hitbox is not exactly where the sprite ends
		AABBf feet = AABBf(AABBInner.L + 2.0f, AABBInner.B - 2.0f, AABBInner.R - 2.0f, AABBInner.B + 1.0f);
		return !_levelHandler->IsPositionEmpty(const_cast<Robot*>(this), feet, params);
	}

	void Robot::StartRunning()
	{
		_state = StateRunning;
		_counter = 0;
		PlaySfx("Run"_s);
		SetAnimation(AnimState::Run);
		SetAnimationTicks(RunLoopTicks);
	}

	void Robot::StartIdle()
	{
		_state = StateIdle;
		_counter = 0;
		_speed.X = 0.0f;
		SetAnimation(AnimState::Idle);
	}

	void Robot::LogicTick()
	{
		_counter++;
		if (_counter <= DecisionDelayTicks) {
			return;
		}

		if (Random().Next(0, 2) != 0) {
			_counter = 0;
			return;
		}

		float distanceSqr;
		Player* player = FindNearestPlayer(NoticeDistanceSqr, &distanceSqr);
		if (player == nullptr) {
			return;
		}

		SetFacingLeft(player->GetPos().X < _pos.X);

		if (distanceSqr < AttackDistanceSqr) {
			StartAttack(distanceSqr);
		} else if (_state == StateIdle) {
			_gaitBase = (Random().Next(0, 2) == 0 ? SlowGaitSpeed : FastGaitSpeed);
			StartRunning();
		}
	}

	void Robot::StartAttack(float distanceSqr)
	{
		_state = StateAttacking;
		_attackPhase = 0;
		_ticks = 0.0f;
		_shots = Random().Next(0, 4) + 1;
		// The speed of the ball is the squared distance in pixels divided by 2048 per tick
		_ballSpeed = distanceSqr / 2048.0f;
		_speed.X = 0.0f;

		PlaySfx("AttackStart"_s);
		SetAnimation(AnimState::Idle);
		// Each clip runs its exact length in ticks and starts the next one when it ends, so no idle frame shows between them
		SetTransition((AnimState)1073741824, false, [this]() {
			if (_state == StateAttacking && _attackPhase == 0) {
				StartShot();
			}
		});
		SetAnimationTicks(AttackStartTicks);
	}

	void Robot::StartShot()
	{
		_attackPhase = 1;
		_ballThrown = false;
		_ticks = 0.0f;
		PlaySfx("Attack"_s);
		SetTransition((AnimState)1073741825, false, [this]() {
			if (_state == StateAttacking && _attackPhase == 1) {
				EndShot();
			}
		});
		SetAnimationTicks(ShotTicks);
	}

	void Robot::EndShot()
	{
		ThrowSpikeBall();

		// The shots stop when they are used up or the player got behind it
		bool playerBehind = false;
		Player* player = FindNearestPlayer(NoticeDistanceSqr, nullptr);
		if (player != nullptr) {
			playerBehind = ((player->GetPos().X < _pos.X) != IsFacingLeft());
		}

		if (_shots < 1 || playerBehind) {
			_attackPhase = 2;
			_ticks = 0.0f;
			PlaySfx("AttackEnd"_s);
			SetTransition((AnimState)1073741826, false, [this]() {
				if (_state == StateAttacking && _attackPhase == 2) {
					StartIdle();
				}
			});
			SetAnimationTicks(AttackEndTicks);
		} else {
			StartShot();
		}
	}

	void Robot::ThrowSpikeBall()
	{
		// Once per shot
		if (_ballThrown) {
			return;
		}
		_ballThrown = true;
		_shots--;

		std::shared_ptr<SpikeBall> spikeBall = std::make_shared<SpikeBall>();
		uint8_t spikeBallParams[1] = { (uint8_t)(IsFacingLeft() ? 1 : 0) };
		spikeBall->SetLaunchSpeed(_ballSpeed);
		spikeBall->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X + (IsFacingLeft() ? -16 : 16), (std::int32_t)_pos.Y - 27, _renderer.GetLayer() + 2),
			spikeBallParams
		));
		_levelHandler->AddActor(spikeBall);
	}

	void Robot::UpdateAttack(float timeMult)
	{
		_ticks += timeMult * TicksPerFrame;

		// The clips end each step, the timers only catch a clip that didn't run (e.g. missing animation)
		switch (_attackPhase) {
			case 0: {
				if (_ticks >= AttackStartTicks * 2.0f) {
					ForceCancelTransition();
					StartShot();
				}
				break;
			}
			case 1: {
				if (_ticks >= ShotThrowTick) {
					ThrowSpikeBall();
				}
				if (_ticks >= ShotTicks * 2.0f) {
					ForceCancelTransition();
					EndShot();
				}
				break;
			}
			case 2: {
				if (_ticks >= AttackEndTicks * 2.0f) {
					ForceCancelTransition();
					StartIdle();
				}
				break;
			}
		}
	}

	void Robot::UpdateDying(float timeMult)
	{
		float ticks = timeMult * TicksPerFrame;
		_ticks += ticks;
		_deathAcc += ticks;

		while (_deathAcc >= 1.0f) {
			_deathAcc -= 1.0f;
			std::int32_t r = Random().Next(0, 16);
			if (r == 0 || r == 2 || r == 5 || r == 7) {
				Vector3i pos = Vector3i((std::int32_t)(_pos.X + Random().NextFloat(-32.0f, 32.0f)), (std::int32_t)(_pos.Y + Random().NextFloat(-32.0f, 32.0f)), _renderer.GetLayer() + 2);
				Explosion::Create(_levelHandler, pos, Explosion::Type::Small);
				if (r == 0) {
					CreateSpriteDebris((AnimState)Random().Fast(100, 109), 1);
				}
			}
		}

		if (_ticks >= DyingTicks) {
			_state = StateDead;

			// The original just removes it here, it bursts into pieces instead of vanishing
			CreateParticleDebrisOnPerish(ParticleDebrisEffect::Standard, Vector2f::Zero);
			// The remote control keeps this actor alive for a while, so it has to hide itself
			_renderer.SetDrawEnabled(false);
			for (std::int32_t i = 0; i < 8; i++) {
				CreateSpriteDebris((AnimState)Random().Fast(100, 109), 1);
			}
			_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

			EnemyBase::OnPerish(nullptr);
		}
	}

	Task<bool> Robot::SpikeBall::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);

		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithShots = false;

		_health = INT32_MAX;
		_speed.X = (IsFacingLeft() ? -_launchSpeed : _launchSpeed) * TicksPerFrame;
		_speed.Y = 0.0f;

		async_await RequestMetadataAsync("Boss/Robot"_s);
		SetAnimation((AnimState)1073741827);

		async_return true;
	}

	void Robot::SpikeBall::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		_speed.Y += Gravity * timeMult;

		if (_timeLeft <= 0.0f) {
			DecreaseHealth(INT32_MAX);
		} else {
			_timeLeft -= timeMult;
		}
	}

	void Robot::SpikeBall::OnUpdateHitbox()
	{
		UpdateHitbox(8, 8);
	}

	void Robot::SpikeBall::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 0.1f;
		light.Brightness = 0.8f;
		light.RadiusNear = 0.0f;
		light.RadiusFar = 22.0f;
	}

	bool Robot::SpikeBall::OnPerish(ActorBase* collider)
	{
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + _speed.X), (std::int32_t)(_pos.Y + _speed.Y), _renderer.GetLayer() + 2), Explosion::Type::SmallDark);

		return EnemyBase::OnPerish(collider);
	}

	void Robot::SpikeBall::OnHitFloor(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Robot::SpikeBall::OnHitWall(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}

	void Robot::SpikeBall::OnHitCeiling(float timeMult)
	{
		DecreaseHealth(INT32_MAX);
	}
}
