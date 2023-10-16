#include "Robot.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Bosses
{
	Robot::Robot()
		:
		_state(StateWaiting),
		_stateTime(0.0f),
		_shots(0)
	{
	}

	void Robot::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/Robot"_s);
	}

	Task<bool> Robot::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_scoreValue = 2000;
		_originPos = Vector2f(_pos.X, _pos.Y - 300.0f);

		async_await RequestMetadataAsync("Boss/Robot"_s);
		SetAnimation(AnimState::Idle);

		SetFacingLeft(true);

		_renderer.setDrawEnabled(false);

		async_return true;
	}

	void Robot::Activate()
	{
		SetHealthByDifficulty(100);

		_renderer.setDrawEnabled(true);
		_state = StateCopter;

		MoveInstantly(_originPos, MoveType::Absolute);

		SetAnimation((AnimState)1073741828);
	}

	void Robot::Deactivate()
	{
		_renderer.setDrawEnabled(false);
		_state = StateWaiting;
	}

	void Robot::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		switch (_state) {
			case StateCopter: {
				if (GetState(ActorState::CanJump)) {
					_speed.Y = 0.0f;

					_state = StateTransition;
					SetTransition((AnimState)1073741829, false, [this]() {
						FollowNearestPlayer(StateRunning1, Random().NextFloat(20, 40));
					});
				} else {
					_speed.Y -= (_levelHandler->IsReforged() ? 0.27f : 0.21f) * timeMult;
				}
				break;
			}
			case StateRunning1: {
				if (_stateTime <= 0.0f) {
					FollowNearestPlayer(
						Random().NextFloat() < 0.65f ? StateRunning1 : StateRunning2,
						Random().NextFloat(10, 30));
				}
				break;
			}

			case StateRunning2: {
				if (_stateTime <= 0.0f) {
					_speed.X = 0.0f;

					_state = StateTransition;
					PlaySfx("AttackStart"_s);
					SetAnimation(AnimState::Idle);
					SetTransition((AnimState)1073741824, false, [this]() {
						_shots = Random().Next(1, 4);
						Shoot();
					});
				}
				break;
			}

			case StatePreparingToRun: {
				if (_stateTime <= 0.0f) {
					FollowNearestPlayer(StateRunning1, Random().NextFloat(10, 30));
				}
				break;
			}
		}

		_stateTime -= timeMult;
	}

	void Robot::OnHealthChanged(ActorBase* collider)
	{
		EnemyBase::OnHealthChanged(collider);

		constexpr StringView Shrapnels[] = {
			"Shrapnel1"_s, "Shrapnel2"_s, "Shrapnel3"_s,
			"Shrapnel4"_s, "Shrapnel5"_s, "Shrapnel6"_s,
			"Shrapnel7"_s, "Shrapnel8"_s, "Shrapnel9"_s
		};
		int n = Random().Next(1, 4);
		for (int i = 0; i < n; i++) {
			CreateSpriteDebris(Shrapnels[Random().Fast(0, countof(Shrapnels))], 1);
		}

		PlaySfx("Shrapnel"_s);
	}

	bool Robot::OnPerish(ActorBase* collider)
	{
		CreateParticleDebris();

		constexpr StringView Shrapnels[] = {
			"Shrapnel1"_s, "Shrapnel2"_s, "Shrapnel3"_s,
			"Shrapnel4"_s, "Shrapnel5"_s, "Shrapnel6"_s,
			"Shrapnel7"_s, "Shrapnel8"_s, "Shrapnel9"_s
		};
		for (int i = 0; i < 8; i++) {
			CreateSpriteDebris(Shrapnels[Random().Fast(0, countof(Shrapnels))], 1);
		}

		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		return EnemyBase::OnPerish(collider);
	}

	void Robot::FollowNearestPlayer(int newState, float time)
	{
		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto& players = _levelHandler->GetPlayers();
		for (auto& player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).SqrLength() < (_pos - targetPos).SqrLength()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			_state = newState;
			_stateTime = time;

			SetFacingLeft(targetPos.X < _pos.X);

			float mult = Random().NextFloat(0.6f, 0.9f);
			_speed.X = (IsFacingLeft() ? -3.0f : 3.0f) * mult;
			_renderer.AnimDuration = _currentAnimation->AnimDuration / mult;

			PlaySfx("Run"_s);
			SetAnimation(AnimState::Run);
		}
	}

	void Robot::Shoot()
	{
		if (_state == StateWaiting) {
			return;
		}

		std::shared_ptr<SpikeBall> spikeBall = std::make_shared<SpikeBall>();
		uint8_t spikeBallParams[1] = { (uint8_t)(IsFacingLeft() ? 1 : 0) };
		spikeBall->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y - 32, _renderer.layer() + 2),
			spikeBallParams
		));
		_levelHandler->AddActor(spikeBall);

		_shots--;

		PlaySfx("Attack"_s);
		SetTransition((AnimState)1073741825, false, [this]() {
			if (_shots > 0) {
				PlaySfx("AttackShutter"_s);
				Shoot();
			} else {
				Run();
			}
		});
	}

	void Robot::Run()
	{
		if (_state == StateWaiting) {
			return;
		}

		PlaySfx("AttackEnd"_s);
		SetTransition((AnimState)1073741826, false, [this]() {
			_state = StatePreparingToRun;
			_stateTime = 10.0f;
		});
	}

	Task<bool> Robot::SpikeBall::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetFacingLeft(details.Params[0] != 0);

		SetState(ActorState::IsInvulnerable | ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen, false);
		CanCollideWithAmmo = false;

		_health = INT32_MAX;
		_speed.X = (IsFacingLeft() ? -8.0f : 8.0f);

		async_await RequestMetadataAsync("Boss/Robot"_s);
		SetAnimation((AnimState)1073741827);

		async_return true;
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
		Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + _speed.X), (int)(_pos.Y + _speed.Y), _renderer.layer() + 2), Explosion::Type::SmallDark);

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