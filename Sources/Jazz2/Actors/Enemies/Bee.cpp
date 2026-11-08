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
		constexpr float MaxSpeedX = 3.0f;
		constexpr float MaxSpeedY = 1.5f;
		constexpr std::int32_t AttackTicks = 142;
		constexpr std::int32_t ReturnTicks = 64;
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
				Vector2f targetPos;
				if (FindTarget(targetPos, AttackDistance)) {
					_beeState = BeeState::Attacking;
					_counter = 0;
					StartNoise();
					// Measured in JJ2+: each attack starts a bit lower, which makes it swing up and down
					MoveInstantly(Vector2f(0.0f, (float)Random().Next(16, 20)), MoveType::Relative | MoveType::Force);
				}
				break;
			}

			case BeeState::Attacking: {
				Vector2f targetPos;
				if (!FindTarget(targetPos, FLT_MAX)) {
					targetPos = _originPos;
				}

				// Can't fly into the water
				float waterLevel = _levelHandler->GetWaterLevel() - 12.0f;
				if (targetPos.Y > waterLevel) {
					targetPos.Y = waterLevel;
				}

				_speedPerTick.X = std::clamp(_speedPerTick.X + (targetPos.X > _pos.X ? Acceleration : -Acceleration), -MaxSpeedX, MaxSpeedX);
				_speedPerTick.Y = std::clamp(_speedPerTick.Y + (targetPos.Y > _pos.Y ? Acceleration : -Acceleration), -MaxSpeedY, MaxSpeedY);
				MoveInstantly(_speedPerTick, MoveType::Relative | MoveType::Force);
				if (_speedPerTick.X != 0.0f) {
					SetFacingLeft(_speedPerTick.X < 0.0f);
				}

				_counter++;
				if (_counter >= AttackTicks) {
					_beeState = BeeState::Returning;
					_counter = 0;
					_returnFrom = _pos;
					SetFacingLeft(_originPos.X < _pos.X);
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
					StopNoise();
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

		// The looping buzz lasts until the bee is back at its origin
		if (_beeState != BeeState::Idle) {
			StartNoise();
		}
	}

	bool Bee::OnPerish(ActorBase* collider)
	{
		StopNoise();

		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}

	bool Bee::FindTarget(Vector2f& targetPos, float maxDistance) const
	{
		float nearest = maxDistance;
		bool found = false;
		for (auto* player : _levelHandler->GetPlayers()) {
			if (player->GetHealth() <= 0) {
				continue;
			}
			float distance = (player->GetPos() - _pos).Length();
			if (distance < nearest) {
				nearest = distance;
				targetPos = player->GetPos();
				found = true;
			}
		}
		return found;
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
