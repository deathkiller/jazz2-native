#include "Bee.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

#include <float.h>

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		constexpr float AttackDistance = 256.0f;
		constexpr float Acceleration = 1.0f / 16.0f;
		constexpr std::int32_t AttackTicks = 141;
		constexpr std::int32_t ReturnTicks = 64;
		constexpr float CircleRadius = 16.0f;
		constexpr float BobHeight = 4.0f;
	}

	Bee::Bee()
		: _counter(0), _tickTime(0.0f), _beeState(BeeState::Idle)
	{
	}

	Bee::~Bee()
	{
		StopNoise();
	}

	void Bee::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Bee"_s);
	}

	Task<bool> Bee::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 200;

		_originPos = _returnFrom = _pos;

		async_await RequestMetadataAsync("Enemy/Bee"_s);
		SetAnimation(AnimState::Idle);
		// It buzzes all the time, as in the original
		StartNoise();

		async_return true;
	}

	void Bee::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

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

	void Bee::AdvanceTick()
	{
		switch (_beeState) {
			case BeeState::Idle: {
				// A small circle around its home, plus a bob all bees share
				_counter++;
				Vector2f pos = _originPos + Vector2f(CircleRadius * Sin1024(8 * _counter),
					CircleRadius * Sin1024(8 * _counter + 256) + BobHeight * Sin1024(8 * GetElapsedTicks()));
				MoveInstantly(ClampAboveWater(pos), MoveType::Absolute | MoveType::Force);
				SetFacingLeft((_counter & 127) < 63);

				Vector2f targetPos;
				if (FindTarget(targetPos, AttackDistance * AttackDistance)) {
					_beeState = BeeState::Attacking;
					_counter = 0;
					SetFacingLeft(targetPos.X < _pos.X);
				}
				break;
			}

			case BeeState::Attacking: {
				Vector2f targetPos;
				if (!FindTarget(targetPos, FLT_MAX)) {
					targetPos = _originPos;
				}

				// It steers its speed toward the player, at most 2, 3 or 4 px per tick by difficulty
				float maxSpeed = 2.0f + (float)GetDifficultyIndex();
				Vector2f desired = Vector2f(std::clamp(targetPos.X - _pos.X, -maxSpeed, maxSpeed), std::clamp(targetPos.Y - _pos.Y, -maxSpeed, maxSpeed));
				_speedPerTick.X += std::clamp(desired.X - _speedPerTick.X, -Acceleration, Acceleration);
				_speedPerTick.Y += std::clamp(desired.Y - _speedPerTick.Y, -Acceleration, Acceleration);
				MoveInstantly(ClampAboveWater(_pos + _speedPerTick), MoveType::Absolute | MoveType::Force);
				if (_speedPerTick.X != 0.0f) {
					SetFacingLeft(_speedPerTick.X < 0.0f);
				}

				_counter++;
				if (_counter >= AttackTicks) {
					_beeState = BeeState::Returning;
					_counter = 0;
					_returnFrom = _pos;
					SetFacingLeft((_originPos.X - _pos.X) / ReturnTicks < -0.5f);
				}
				break;
			}

			case BeeState::Returning: {
				_counter++;
				float t = std::min((float)_counter / ReturnTicks, 1.0f);
				MoveInstantly(_returnFrom + (_originPos - _returnFrom) * t, MoveType::Absolute | MoveType::Force);
				if (_counter >= ReturnTicks) {
					_beeState = BeeState::Idle;
					_counter = 0;
					_speedPerTick = Vector2f::Zero;
				}
				break;
			}
		}
	}

	void Bee::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>((std::uint8_t)_beeState);
		dest.WriteVariableInt32(_counter);
		dest.WriteValueAsLE<float>(_speedPerTick.X);
		dest.WriteValueAsLE<float>(_speedPerTick.Y);
		dest.WriteValueAsLE<float>(_returnFrom.X);
		dest.WriteValueAsLE<float>(_returnFrom.Y);
	}

	void Bee::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		std::uint8_t state = src.ReadValue<std::uint8_t>();
		_beeState = (state <= (std::uint8_t)BeeState::Returning ? (BeeState)state : BeeState::Idle);
		_counter = src.ReadVariableInt32();
		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
		_returnFrom.X = src.ReadValueAsLE<float>();
		_returnFrom.Y = src.ReadValueAsLE<float>();

		StartNoise();
	}

	bool Bee::OnPerish(ActorBase* collider)
	{
		StopNoise();

		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool Bee::FindTarget(Vector2f& targetPos, float maxDistanceSqr) const
	{
		Player* player = FindNearestPlayer(maxDistanceSqr);
		if (player == nullptr) {
			return false;
		}
		targetPos = player->GetPos();
		return true;
	}

	void Bee::StartNoise()
	{
		if (_noise == nullptr) {
			_noise = PlaySfx("Noise"_s, 0.5f, 2.0f);
			if (_noise != nullptr) {
				_noise->SetLooping(true);
			}
		}
	}

	void Bee::StopNoise()
	{
		if (_noise != nullptr) {
			_noise->Stop();
			_noise = nullptr;
		}
	}
}
