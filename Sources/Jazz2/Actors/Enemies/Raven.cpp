#include "Raven.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		// The original's rules, in its ticks and px per tick
		constexpr float NoticeDistance = 192.0f;
		constexpr float PatrolHeight = 64.0f;
		constexpr float SweepWidth = 96.0f;
		constexpr std::int32_t SweepStep = 4;
		constexpr std::int32_t PatrolTicksBeforeGlide = 176;
		constexpr std::int32_t GlideTicks = 141;
		constexpr float GlideSpeedY = 0.5f;
		constexpr float GlideAbovePlayer = 28.0f;
		constexpr float ArrivedDistance = 1.0f / 16.0f;
	}

	Raven::Raven()
		: _counter(0), _tickTime(0.0f), _angle(0), _angleStep(0), _ravenState(RavenState::Perched)
	{
	}

	void Raven::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Raven"_s);
	}

	Task<bool> Raven::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(2);
		_scoreValue = 300;

		_originPos = _pos;

		async_await RequestMetadataAsync("Enemy/Raven"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Raven::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		_tickTime += timeMult * TicksPerFrame;
		while (_tickTime >= 1.0f) {
			_tickTime -= 1.0f;
			AdvanceTick();
		}
	}

	void Raven::AdvanceTick()
	{
		_counter++;

		std::int32_t globalTick = GetElapsedTicks();
		// 2, 3 or 4 px per tick by difficulty
		float maxSpeed = 2.0f + (float)GetDifficultyIndex();
		Vector2f targetPos;
		switch (_ravenState) {
			case RavenState::Perched: {
				// It drifts slowly up and down while it waits
				MoveInstantly(Vector2f(0.0f, Sin1024(4 * globalTick)), MoveType::Relative | MoveType::Force);
				if (FindTarget(targetPos, NoticeDistance * NoticeDistance)) {
					// It sweeps from its own side over the player to the other one
					bool fromLeft = (_pos.X < targetPos.X);
					_angle = (fromLeft ? 768 : 256);
					_angleStep = (fromLeft ? SweepStep : -SweepStep);
					ChangeState(RavenState::Patrolling);
				}
				break;
			}

			case RavenState::Patrolling: {
				if (!FindTarget(targetPos, FLT_MAX)) {
					targetPos = _originPos;
				}
				_angle += _angleStep;
				float stepX = std::clamp(targetPos.X + SweepWidth * Sin1024(_angle) - _pos.X, -maxSpeed, maxSpeed);
				float stepY = std::clamp(targetPos.Y - PatrolHeight - _pos.Y, -maxSpeed, maxSpeed) + Sin1024(4 * globalTick);
				MoveInstantly(ClampAboveWater(_pos + Vector2f(stepX, stepY)), MoveType::Absolute | MoveType::Force);
				if (stepX < -0.25f) {
					Face(true);
				} else if (stepX > 0.25f) {
					Face(false);
				}

				if (_counter >= PatrolTicksBeforeGlide) {
					// Then it always glides down at the player, aimed where the player was when it set off
					float glideX = std::clamp(targetPos.X - _pos.X, -(maxSpeed + 1.0f), maxSpeed + 1.0f) * 0.25f;
					_speedPerTick = Vector2f(glideX, GlideSpeedY);
					ChangeState(RavenState::Gliding);
					SetAnimation(AnimState::TransitionAttack);
					PlaySfx("Attack"_s, 0.7f, Random().NextFloat(1.4f, 1.8f));
				}
				break;
			}

			case RavenState::Gliding: {
				MoveInstantly(ClampAboveWater(_pos + _speedPerTick), MoveType::Absolute | MoveType::Force);
				bool belowPlayer = (FindTarget(targetPos, FLT_MAX) && targetPos.Y < _pos.Y + GlideAbovePlayer);
				if (_counter >= GlideTicks || belowPlayer) {
					ChangeState(RavenState::Swooping);
					SetAnimation(AnimState::Idle);
				}
				break;
			}

			case RavenState::Swooping: {
				// Back to its perch, easing in
				float returnSpeed = maxSpeed + 2.0f;
				Vector2f diff = _originPos - _pos;
				_speedPerTick = (_speedPerTick + Vector2f(std::clamp(diff.X, -returnSpeed, returnSpeed), std::clamp(diff.Y, -returnSpeed, returnSpeed))) * 0.5f;
				MoveInstantly(_pos + _speedPerTick, MoveType::Absolute | MoveType::Force);
				if (std::abs(_speedPerTick.X) > 0.122f) {
					Face(_speedPerTick.X < 0.0f);
				}
				if (std::abs(diff.X) < ArrivedDistance && std::abs(diff.Y) < ArrivedDistance) {
					MoveInstantly(_originPos, MoveType::Absolute | MoveType::Force);
					_speedPerTick = Vector2f::Zero;
					ChangeState(RavenState::Perched);
				}
				break;
			}
		}
	}

	void Raven::ChangeState(RavenState state)
	{
		_ravenState = state;
		_counter = 0;
	}

	void Raven::Face(bool left)
	{
		if (IsFacingLeft() != left && _currentTransition == nullptr) {
			SetFacingLeft(left);
			if (_ravenState == RavenState::Patrolling) {
				SetTransition(AnimState::TransitionTurn, false);
			}
		}
	}

	void Raven::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteVariableInt32((std::int32_t)_ravenState);
		dest.WriteVariableInt32(_counter);
		dest.WriteValueAsLE<float>(_speedPerTick.X);
		dest.WriteValueAsLE<float>(_speedPerTick.Y);
		dest.WriteVariableInt32(_angle);
		dest.WriteVariableInt32(_angleStep);
	}

	void Raven::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::int32_t state = src.ReadVariableInt32();
		_ravenState = (state >= 0 && state <= (std::int32_t)RavenState::Swooping ? (RavenState)state : RavenState::Perched);
		_counter = src.ReadVariableInt32();
		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
		_angle = src.ReadVariableInt32();
		_angleStep = src.ReadVariableInt32();
		SetAnimation(_ravenState == RavenState::Gliding ? AnimState::TransitionAttack : AnimState::Idle);
	}

	bool Raven::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool Raven::FindTarget(Vector2f& targetPos, float maxDistanceSqr)
	{
		Player* player = FindNearestPlayer(maxDistanceSqr);
		if (player == nullptr) {
			return false;
		}
		targetPos = player->GetPos();
		return true;
	}
}
