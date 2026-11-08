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
		constexpr float NoticeDistance = 192.0f;
		constexpr float PatrolHeight = 64.0f;
		constexpr float RiseSpeed = 2.3f;
		constexpr float PatrolAcceleration = 0.035f;
		constexpr float PatrolSpeed = 2.4f;
		constexpr std::int32_t PatrolTicksBeforeGlide = 176;
		constexpr float GlideStartDistance = 40.0f;
		constexpr std::int32_t GlideTicks = 76;
		constexpr float GlideSpeedX = 1.0f;
		constexpr float GlideSpeedY = 0.5f;
		constexpr float SwoopSpeed = 5.0f;
	}

	Raven::Raven()
		: _counter(0), _tickTime(0.0f), _ravenState(RavenState::Perched)
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

		Vector2f targetPos;
		switch (_ravenState) {
			case RavenState::Perched: {
				if (FindTarget(targetPos, NoticeDistance * NoticeDistance)) {
					ChangeState(RavenState::Patrolling);
					_speedPerTick = Vector2f::Zero;
				}
				break;
			}

			case RavenState::Patrolling: {
				// Measured in JJ2+: it flies back and forth over the player, 64 px above its perch
				bool found = FindTarget(targetPos, FLT_MAX);
				if (!found) {
					targetPos = _originPos;
				}

				float patrolY = _originPos.Y - PatrolHeight;
				float waterLevel = _levelHandler->GetWaterLevel() - 20.0f;
				if (patrolY > waterLevel) {
					patrolY = waterLevel;
				}

				float dx = targetPos.X - _pos.X;
				_speedPerTick.X = std::clamp(_speedPerTick.X + (dx < 0.0f ? -PatrolAcceleration : PatrolAcceleration), -PatrolSpeed, PatrolSpeed);
				_speedPerTick.Y = std::clamp(patrolY - _pos.Y, -RiseSpeed, RiseSpeed);
				MoveInstantly(_speedPerTick, MoveType::Relative | MoveType::Force);
				if (_speedPerTick.X != 0.0f) {
					Face(_speedPerTick.X < 0.0f);
				}

				if (found && _counter >= PatrolTicksBeforeGlide && std::abs(dx) < GlideStartDistance && (dx < 0.0f) == (_speedPerTick.X < 0.0f)) {
					ChangeState(RavenState::Gliding);
					SetAnimation(AnimState::TransitionAttack);
					PlaySfx("Attack"_s, 0.7f, Random().NextFloat(1.4f, 1.8f));
				}
				break;
			}

			case RavenState::Gliding: {
				MoveInstantly(Vector2f(IsFacingLeft() ? -GlideSpeedX : GlideSpeedX, GlideSpeedY), MoveType::Relative | MoveType::Force);
				if (_counter >= GlideTicks) {
					ChangeState(RavenState::Swooping);
					SetAnimation(AnimState::Idle);
				}
				break;
			}

			case RavenState::Swooping: {
				// Measured in JJ2+: it swoops straight back to its perch at up to 5 px a tick on each axis
				Vector2f diff = _originPos - _pos;
				Vector2f step = Vector2f(std::clamp(diff.X, -SwoopSpeed, SwoopSpeed), std::clamp(diff.Y, -SwoopSpeed, SwoopSpeed));
				MoveInstantly(step, MoveType::Relative | MoveType::Force);
				if (step.X != 0.0f) {
					Face(step.X < 0.0f);
				}
				if (std::abs(diff.X) <= SwoopSpeed && std::abs(diff.Y) <= SwoopSpeed) {
					ChangeState(RavenState::Patrolling);
					_speedPerTick = Vector2f::Zero;
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
	}

	void Raven::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::int32_t state = src.ReadVariableInt32();
		_ravenState = (state >= 0 && state <= (std::int32_t)RavenState::Swooping ? (RavenState)state : RavenState::Perched);
		_counter = src.ReadVariableInt32();
		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
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
		float nearestSqr = maxDistanceSqr;
		bool found = false;
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}
			float distanceSqr = (player->GetPos() - _pos).SqrLength();
			if (distanceSqr < nearestSqr) {
				nearestSqr = distanceSqr;
				targetPos = player->GetPos();
				found = true;
			}
		}
		return found;
	}
}
