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
		_endText(0)
	{
	}

	TurtleBoss::~TurtleBoss()
	{
		if (_currentMace != nullptr) {
			_currentMace->DecreaseHealth(INT32_MAX);
			_currentMace = nullptr;
		}
	}

	void TurtleBoss::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Boss/TurtleBoss"_s);
		PreloadMetadataAsync("Boss/TurtleShellTough"_s);
	}

	Task<bool> TurtleBoss::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_endText = details.Params[1];

		SetHealthByDifficulty(100);
		_scoreValue = 5000;

		co_await RequestMetadataAsync("Boss/TurtleBoss"_s);
		SetAnimation(AnimState::Idle);

		SetFacingLeft(true);

		co_return true;
	}

	bool TurtleBoss::OnActivatedBoss()
	{
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
						_currentMace = std::make_shared<Mace>();
						_currentMace->OnActivated({
							.LevelHandler = _levelHandler,
							.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 2)
						});
						_levelHandler->AddActor(_currentMace);

						SetTransition((AnimState)1073741825, false, [this]() {
							_state = StateAttacking;
							_stateTime = 10.0f;
						});
					});
				} else if (!CanMoveToPosition(_speed.X, 0)) {
					_speed.X = 0;

					SetAnimation(AnimState::Idle);
				}
				break;
			}
		}

		_stateTime -= timeMult;
	}

	bool TurtleBoss::OnPerish(ActorBase* collider)
	{
		std::shared_ptr<Enemies::TurtleShell> shell = std::make_shared<Enemies::TurtleShell>();
		uint8_t shellParams[9];
		*(float*)&shellParams[0] = _speed.X * 1.1f;
		*(float*)&shellParams[4] = 1.1f;
		shellParams[8] = 2;
		shell->OnActivated({
			.LevelHandler = _levelHandler,
			.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer()),
			.Params = shellParams
		});
		_levelHandler->AddActor(shell);

		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2), Explosion::Type::SmokeGray);

		CreateParticleDebris();
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		StringView text = _levelHandler->GetLevelText(_endText, -1, '|');
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
			if ((_pos - newPos).Length() < (_pos - targetPos).Length()) {
				targetPos = newPos;
				found = true;
			}
		}

		if (found) {
			_state = newState;
			_stateTime = time;

			SetFacingLeft (targetPos.X < _pos.X);

			_speed.X = (IsFacingLeft() ? -1.6f : 1.6f);

			SetAnimation(AnimState::Walk);
		}
	}

	Task<bool> TurtleBoss::Mace::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);
		CanCollideWithAmmo = false;

		_health = INT32_MAX;

		co_await RequestMetadataAsync("Boss/TurtleBoss"_s);
		SetAnimation((AnimState)1073741827);

		_originPos = _pos;

		FollowNearestPlayer();

		_sound = PlaySfx("Mace"_s, 0.7f);
		if (_sound != nullptr) {
			_sound->setLooping(true);
		}

		co_return true;
	}

	void TurtleBoss::Mace::OnUpdate(float timeMult)
	{
		MoveInstantly(Vector2f(_speed.X * timeMult, _speed.Y * timeMult), MoveType::Relative | MoveType::Force);

		if (_returning) {
			Vector2f diff = (_targetSpeed - _speed);
			if (diff.Length() > 1.0f) {
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
			if ((_pos - newPos).Length() < (_pos - targetPos).Length()) {
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