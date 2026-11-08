#include "Dragonfly.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		constexpr float NoticeDistance = 128.0f;
		constexpr float BesideDistance = 96.0f;
		constexpr float AboveDistance = 62.0f;
		constexpr float FlySpeed = 2.8f;
		constexpr float RetargetDistance = 16.0f;
		constexpr float IdleBobHeight = 5.0f;
		constexpr std::int32_t IdleBobTicks = 96;
		constexpr float HoverBobHeight = 4.5f;
		constexpr std::int32_t HoverBobTicks = 16;
		constexpr float DiveBeyondDistance = 84.0f;
		constexpr float DiveBelowDistance = 57.0f;
		constexpr std::int32_t DiveTicks = 140;
		constexpr float ReturnSpeed = 5.0f;
	}

	Dragonfly::Dragonfly()
		: _state(StateIdle), _counter(0), _animTicks(0), _tickTime(0.0f)
	{
	}

	Dragonfly::~Dragonfly()
	{
		StopNoise();
	}

	void Dragonfly::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Dragonfly"_s);
	}

	Task<bool> Dragonfly::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 200;

		_originPos = _basePos = _targetPos = _diveFrom = _pos;

		async_await RequestMetadataAsync("Enemy/Dragonfly"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Dragonfly::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			if (_noise != nullptr) {
				_noise->Pause();
			}
			return;
		}

		_tickTime += timeMult * TicksPerFrame;
		while (_tickTime >= 1.0f) {
			_tickTime -= 1.0f;
			AdvanceTick();
		}

		if (_noise != nullptr) {
			_noise->Play();
		}
	}

	void Dragonfly::AdvanceTick()
	{
		_counter++;

		// Measured in JJ2+: the wings beat every 6 ticks at rest and every 2 ticks once it's awake
		_animTicks++;
		SetAnimationFrame((_animTicks / (_state == StateIdle ? 6 : 2)) % std::max(_renderer.FrameCount, 1));

		Vector2f targetPos;
		switch (_state) {
			case StateIdle: {
				MoveInstantly(_basePos + Vector2f(0.0f, sinf(_counter * fTwoPi / IdleBobTicks) * IdleBobHeight), MoveType::Absolute | MoveType::Force);
				if (FindTargetPos(targetPos, NoticeDistance)) {
					_targetPos = targetPos;
					_state = StateFlying;
					_noise = PlaySfx("Noise"_s, 0.6f);
				}
				break;
			}

			case StateFlying: {
				// Measured in JJ2+: a straight line to a point beside and above the player
				Vector2f diff = _targetPos - _basePos;
				float length = diff.Length();
				if (length <= FlySpeed) {
					_basePos = _targetPos;
					_state = StateHovering;
					_counter = 0;
					StopNoise();
				} else {
					_basePos += diff / length * FlySpeed;
					SetFacingLeft(diff.X < 0.0f);
				}
				MoveInstantly(_basePos, MoveType::Absolute | MoveType::Force);
				break;
			}

			case StateHovering: {
				MoveInstantly(_basePos + Vector2f(0.0f, sinf(_counter * fTwoPi / HoverBobTicks) * HoverBobHeight), MoveType::Absolute | MoveType::Force);
				Player* target;
				if (FindTargetPos(targetPos, FLT_MAX) && (targetPos - _targetPos).Length() > RetargetDistance) {
					_targetPos = targetPos;
					_state = StateFlying;
					_noise = PlaySfx("Noise"_s, 0.6f);
				} else if ((target = FindNearestPlayer(FLT_MAX, false)) != nullptr && (target->GetPos() - _originPos).Length() > NoticeDistance) {
					// Measured in JJ2+: a player who left its area gets a parting dive, then it flies home
					Vector2f playerPos = target->GetPos();
					bool fromRight = (_basePos.X >= playerPos.X);
					_diveFrom = _basePos;
					_targetPos = Vector2f(playerPos.X + (fromRight ? -DiveBeyondDistance : DiveBeyondDistance), playerPos.Y + DiveBelowDistance);
					_state = StateDiving;
					_counter = 0;
					SetFacingLeft(fromRight);
					_noise = PlaySfx("Noise"_s, 0.6f);
				}
				break;
			}

			case StateDiving: {
				float t = std::min((float)_counter / DiveTicks, 1.0f);
				float eased = t * t * (3.0f - 2.0f * t);
				_basePos = _diveFrom + (_targetPos - _diveFrom) * eased;
				MoveInstantly(_basePos, MoveType::Absolute | MoveType::Force);
				if (_counter >= DiveTicks) {
					_state = StateReturning;
					_counter = 0;
				}
				break;
			}

			case StateReturning: {
				Vector2f diff = _originPos - _basePos;
				Vector2f step = Vector2f(std::clamp(diff.X, -ReturnSpeed, ReturnSpeed), std::clamp(diff.Y, -ReturnSpeed, ReturnSpeed));
				_basePos += step;
				if (step.X != 0.0f) {
					SetFacingLeft(step.X < 0.0f);
				}
				MoveInstantly(_basePos, MoveType::Absolute | MoveType::Force);
				if (std::abs(diff.X) <= ReturnSpeed && std::abs(diff.Y) <= ReturnSpeed) {
					_basePos = _targetPos = _originPos;
					_state = StateIdle;
					_counter = 0;
					StopNoise();
				}
				break;
			}
		}
	}

	Player* Dragonfly::FindNearestPlayer(float maxDistance, bool aboveWaterOnly) const
	{
		Player* target = nullptr;
		float targetDistance = maxDistance;
		float waterLevel = (aboveWaterOnly ? _levelHandler->GetWaterLevel() : FLT_MAX);
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}
			Vector2f playerPos = player->GetPos();
			float distance = (playerPos - _basePos).Length();
			if (distance < targetDistance && playerPos.Y < waterLevel) {
				target = player;
				targetDistance = distance;
			}
		}
		return target;
	}

	bool Dragonfly::FindTargetPos(Vector2f& targetPos, float maxDistance) const
	{
		Player* target = FindNearestPlayer(maxDistance, true);
		if (target == nullptr) {
			return false;
		}

		// Measured in JJ2+: it stays on its side of the player, or crosses over when it's closer than that
		Vector2f playerPos = target->GetPos();
		float dx = _basePos.X - playerPos.X;
		bool onRight = (dx >= 0.0f);
		if (std::abs(dx) < BesideDistance && _state == StateIdle) {
			onRight = !onRight;
		}
		targetPos = Vector2f(playerPos.X + (onRight ? BesideDistance : -BesideDistance), playerPos.Y - AboveDistance);

		// Can't fly into the water
		float waterLevel = _levelHandler->GetWaterLevel() - 12.0f;
		if (targetPos.Y > waterLevel) {
			targetPos.Y = waterLevel;
		}
		return true;
	}

	void Dragonfly::StopNoise()
	{
		if (_noise != nullptr) {
			_noise->Stop();
			_noise = nullptr;
		}
	}

	void Dragonfly::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteVariableInt32(_state);
		dest.WriteVariableInt32(_counter);
		dest.WriteValueAsLE<float>(_basePos.X);
		dest.WriteValueAsLE<float>(_basePos.Y);
		dest.WriteValueAsLE<float>(_targetPos.X);
		dest.WriteValueAsLE<float>(_targetPos.Y);
	}

	void Dragonfly::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_state = src.ReadVariableInt32();
		_counter = src.ReadVariableInt32();
		_basePos.X = src.ReadValueAsLE<float>();
		_basePos.Y = src.ReadValueAsLE<float>();
		_targetPos.X = src.ReadValueAsLE<float>();
		_targetPos.Y = src.ReadValueAsLE<float>();
	}

	void Dragonfly::OnHitWall(float timeMult)
	{
	}

	void Dragonfly::OnHitFloor(float timeMult)
	{
	}

	void Dragonfly::OnHitCeiling(float timeMult)
	{
	}

	bool Dragonfly::OnPerish(ActorBase* collider)
	{
		StopNoise();

		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}
