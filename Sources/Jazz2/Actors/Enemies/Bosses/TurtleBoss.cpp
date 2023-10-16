#include "TurtleBoss.h"
#include "../../../ILevelHandler.h"
#include "../../Player.h"
#include "../../Explosion.h"
#include "../TurtleShell.h"

#include "../../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Bosses
{
	TurtleBoss::TurtleBoss()
		:
		_state(StateWaiting),
		_stateTime(0.0f),
		_endText(0),
		_maceTime(0.0f)
	{
	}

	TurtleBoss::~TurtleBoss()
	{
		if (_mace != nullptr) {
			_mace->DecreaseHealth(INT32_MAX);
			_mace = nullptr;
		}
	}

	void TurtleBoss::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/TurtleBoss"_s);
		PreloadMetadataAsync("Boss/TurtleBossShell"_s);
	}

	Task<bool> TurtleBoss::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];
		_originPos = _pos;
		_scoreValue = 5000;

		async_await RequestMetadataAsync("Boss/TurtleBoss"_s);
		SetAnimation(AnimState::Idle);

		SetFacingLeft(true);

		async_return true;
	}

	bool TurtleBoss::OnActivatedBoss()
	{
		SetHealthByDifficulty(100);
		MoveInstantly(_originPos, MoveType::Absolute | MoveType::Force);
		FollowNearestPlayer(StateWalking1, Random().NextFloat(120.0f, 160.0f));
		return true;
	}

	void TurtleBoss::OnUpdate(float timeMult)
	{
		BossBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		switch (_state) {
			case StateWalking1: {
				if (_stateTime <= 0.0f) {
					FollowNearestPlayer(StateWalking2, 16);
				} else if (!CanMoveToPosition(_speed.X, 0)) {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = -_speed.X;
				}
				break;
			}

			case StateWalking2: {
				if (_stateTime <= 0.0f) {
					_speed.X = 0.0f;

					PlaySfx("AttackStart"_s);

					_state = StateTransition;
					SetAnimation(AnimState::Idle);
					SetTransition((AnimState)1073741824, false, [this]() {
						_mace = std::make_shared<Mace>();
						_mace->OnActivated(ActorActivationDetails(
							_levelHandler,
							Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 2)
						));
						_levelHandler->AddActor(_mace);

						SetTransition((AnimState)1073741825, false, [this]() {
							_state = StateAttacking;
							_stateTime = 10.0f;
							_maceTime = 480.0f;
						});
					});
				} else if (!CanMoveToPosition(_speed.X, 0)) {
					_speed.X = 0;

					SetAnimation(AnimState::Idle);
				}
				break;
			}

			case StateAttacking: {
				_maceTime -= timeMult;
				if (_maceTime <= 0.0f && _mace != nullptr) {
					_mace->DecreaseHealth(INT32_MAX);
					_mace = nullptr;

					SetTransition((AnimState)1073741826, false, [this]() {
						FollowNearestPlayer(StateWalking1, Random().NextFloat(80.0f, 160.0f));
					});
				}
				break;
			}
		}

		_stateTime -= timeMult;
	}

	bool TurtleBoss::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_state == StateAttacking && _stateTime <= 0.0f) {
			if (auto mace = dynamic_cast<Mace*>(other.get())) {
				if (mace == _mace.get()) {
					_mace->DecreaseHealth(INT32_MAX);
					_mace = nullptr;

					PlaySfx("AttackEnd"_s);

					SetTransition((AnimState)1073741826, false, [this]() {
						FollowNearestPlayer(StateWalking1, Random().NextFloat(80.0f, 160.0f));
					});
					return true;
				}
			}
		}

		return EnemyBase::OnHandleCollision(other);
	}

	bool TurtleBoss::OnPerish(ActorBase* collider)
	{
		float shellSpeedY;
		if (_pos.Y > _levelHandler->WaterLevel()) {
			shellSpeedY = -0.65f;
		} else if (_levelHandler->IsReforged()) {
			shellSpeedY = -1.1f;
		} else {
			shellSpeedY = -0.98f;
		}

		std::shared_ptr<Enemies::TurtleShell> shell = std::make_shared<Enemies::TurtleShell>();
		uint8_t shellParams[9];
		*(float*)&shellParams[0] = _speed.X * 1.1f;
		*(float*)&shellParams[4] = shellSpeedY;
		shellParams[8] = 2;
		shell->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer()),
			shellParams
		));
		_levelHandler->AddActor(shell);

		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2), Explosion::Type::SmokeGray);

		CreateParticleDebris();
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText);
		_levelHandler->ShowLevelText(text);

		return BossBase::OnPerish(collider);
	}

	void TurtleBoss::FollowNearestPlayer(int newState, float time)
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

			_speed.X = (IsFacingLeft() ? -1.6f : 1.6f);

			SetAnimation(AnimState::Walk);
		}
	}

	TurtleBoss::Mace::~Mace()
	{
		if (_sound != nullptr) {
			_sound->stop();
			_sound = nullptr;
		}
	}

	Task<bool> TurtleBoss::Mace::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithAmmo = false;

		_health = INT32_MAX;

		async_await RequestMetadataAsync("Boss/TurtleBoss"_s);
		SetAnimation((AnimState)1073741827);

		_originPos = _pos;

		FollowNearestPlayer();

		_sound = PlaySfx("Mace"_s, 0.7f);
		if (_sound != nullptr) {
			_sound->setLooping(true);
		}

		async_return true;
	}

	void TurtleBoss::Mace::OnUpdate(float timeMult)
	{
		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		if (_sound != nullptr) {
			_sound->setPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
		}

		if (_returning) {
			Vector2f diff = (_targetSpeed - _speed);
			if (diff.SqrLength() > 1.0f) {
				_speed.X += diff.X * 0.04f;
				_speed.Y += diff.Y * 0.04f;
			}

		} else {
			if (_returnTime > 0.0f) {
				_returnTime -= timeMult;
			} else {
				_returning = true;

				_targetSpeed = (_originPos - _pos) / (TotalTime / 2);
			}
		}
	}

	void TurtleBoss::Mace::OnUpdateHitbox()
	{
		UpdateHitbox(18, 18);
	}

	void TurtleBoss::Mace::FollowNearestPlayer()
	{
		bool found = false;
		Vector2f targetPos = Vector2f(FLT_MAX, FLT_MAX);

		auto& players = _levelHandler->GetPlayers();
		for (auto player : players) {
			Vector2f newPos = player->GetPos();
			if ((_pos - newPos).SqrLength() < (_pos - targetPos).SqrLength()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			SetFacingLeft(targetPos.X < _originPos.X);

			_returnTime = (TotalTime / 2);

			Vector2f diff = (targetPos - _originPos);
			_speed.X = (diff.X / _returnTime);
			_speed.Y = (diff.Y / _returnTime);
		}
	}
}