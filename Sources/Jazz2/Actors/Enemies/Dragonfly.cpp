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
		// The original's rules, in its ticks and px per tick
		constexpr float NoticeDistance = 128.0f;
		constexpr float BesideDistance = 96.0f;
		constexpr float AboveDistance = 64.0f;
		constexpr float HoverBobHeight = 4.0f;
		constexpr std::int32_t ApproachTicks = 64;
		constexpr std::int32_t HoverTicks = 560;
		constexpr std::int32_t ChargeTicks = 141;
		constexpr float ArrivedDistance = 1.0f / 16.0f;
	}

	Dragonfly::Dragonfly()
		: _state(StateIdle), _counter(0), _animTicks(0), _tickTime(0.0f), _phase(Random().Next(0, 256)), _wobbleTime(0), _behind(false)
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

		_originPos = _basePos = _pos;

		async_await RequestMetadataAsync("Enemy/Dragonfly"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Idle);
		// It buzzes all the time, as in the original
		StartNoise();

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

		// Measured in JJ2+: the wings beat every 6 ticks at rest and on the way home, every 2 ticks while it's after a player
		_animTicks++;
		bool calm = (_state == StateIdle || _state == StateReturning);
		SetAnimationFrame((_animTicks / (calm ? 6 : 2)) % std::max<std::int32_t>(_renderer.FrameCount, 1));

		std::int32_t globalTick = GetElapsedTicks();
		// 2, 3 or 4 px per tick by difficulty
		float maxSpeed = 2.0f + (float)GetDifficultyIndex();

		switch (_state) {
			case StateIdle: {
				_basePos.Y += 0.5f * Sin1024(16 * (globalTick & 63));
				if (Player* target = FindNearestPlayer(NoticeDistance * NoticeDistance)) {
					// A straight line to a point past the player and above it, fixed when it notices the player
					Vector2f playerPos = target->GetPos();
					float side = (playerPos.X > _basePos.X ? 1.0f : -1.0f);
					SetFacingLeft(side < 0.0f);
					Vector2f targetPos = Vector2f(playerPos.X + side * BesideDistance, playerPos.Y - AboveDistance + HoverBobHeight * Sin1024(32 * (globalTick - _phase)));
					_speedPerTick = (targetPos - _basePos) / (float)ApproachTicks;
					_state = StateFlying;
					_counter = 0;
				}
				break;
			}

			case StateFlying: {
				_basePos += _speedPerTick;
				if (_counter >= ApproachTicks) {
					_state = StateHovering;
					_counter = 0;
					_wobbleTime = 0;
				}
				break;
			}

			case StateHovering: {
				Player* target = FindNearestPlayer(FLT_MAX);
				if (target == nullptr) {
					break;
				}
				// It keeps beside the player, on Hard it sometimes slips behind the player's back
				if (--_wobbleTime <= 0) {
					_behind = (Random().Next(0, 2) != 0);
					_wobbleTime = 16 + Random().Next(0, 64);
				}
				Vector2f playerPos = target->GetPos();
				float side = (IsFacingLeft() ? -1.0f : 1.0f);
				if (_levelHandler->GetDifficulty() == GameDifficulty::Hard && _behind) {
					side = (target->IsFacingLeft() ? -1.0f : 1.0f);
				}
				_phase--;
				Vector2f hoverPos = Vector2f(playerPos.X - side * BesideDistance, playerPos.Y - AboveDistance + HoverBobHeight * Sin1024(32 * (globalTick - _phase)));
				_basePos.X += std::clamp(hoverPos.X - _basePos.X, -maxSpeed, maxSpeed);
				_basePos.Y += std::clamp(hoverPos.Y - _basePos.Y, -maxSpeed, maxSpeed) + Sin1024(4 * globalTick);
				SetFacingLeft(playerPos.X < _basePos.X);

				if (_counter >= HoverTicks) {
					_state = StateDiving;
					_counter = 0;
					_speedPerTick = Vector2f::Zero;
				}
				break;
			}

			case StateDiving: {
				// Then it charges, steering its speed toward the player
				if (Player* target = FindNearestPlayer(FLT_MAX)) {
					Vector2f diff = target->GetPos() - _basePos;
					Vector2f desired = Vector2f(std::clamp(diff.X, -maxSpeed, maxSpeed), std::clamp(diff.Y, -maxSpeed, maxSpeed));
					float steer = (maxSpeed - 1.0f) / 64.0f;
					_speedPerTick.X += std::clamp(desired.X - _speedPerTick.X, -steer, steer);
					_speedPerTick.Y += std::clamp(desired.Y - _speedPerTick.Y, -steer, steer);
				}
				_basePos += _speedPerTick;
				if (_speedPerTick.X != 0.0f) {
					SetFacingLeft(_speedPerTick.X < 0.0f);
				}
				if (_counter >= ChargeTicks) {
					_state = StateReturning;
					_counter = 0;
				}
				break;
			}

			case StateReturning: {
				float returnSpeed = maxSpeed + 2.0f;
				Vector2f diff = _originPos - _basePos;
				_speedPerTick = (_speedPerTick + Vector2f(std::clamp(diff.X, -returnSpeed, returnSpeed), std::clamp(diff.Y, -returnSpeed, returnSpeed))) * 0.5f;
				_basePos += _speedPerTick;
				if (std::abs(_speedPerTick.X) > 0.122f) {
					SetFacingLeft(_speedPerTick.X < 0.0f);
				}
				if (std::abs(diff.X) < ArrivedDistance && std::abs(diff.Y) < ArrivedDistance) {
					_basePos = _originPos;
					_speedPerTick = Vector2f::Zero;
					_state = StateIdle;
					_counter = 0;
				}
				break;
			}
		}

		_basePos = ClampAboveWater(_basePos);
		MoveInstantly(_basePos, MoveType::Absolute | MoveType::Force);
	}

	void Dragonfly::StartNoise()
	{
		if (_noise == nullptr) {
			_noise = PlaySfx("Noise"_s, 0.6f);
			if (_noise != nullptr) {
				_noise->SetLooping(true);
			}
		}
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
		dest.WriteValueAsLE<float>(_speedPerTick.X);
		dest.WriteValueAsLE<float>(_speedPerTick.Y);
		dest.WriteVariableInt32(_phase);
		dest.WriteVariableInt32(_wobbleTime);
		dest.WriteValue<std::uint8_t>(_behind ? 1 : 0);
	}

	void Dragonfly::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_state = src.ReadVariableInt32();
		_counter = src.ReadVariableInt32();
		_basePos.X = src.ReadValueAsLE<float>();
		_basePos.Y = src.ReadValueAsLE<float>();
		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
		_phase = src.ReadVariableInt32();
		_wobbleTime = src.ReadVariableInt32();
		_behind = (src.ReadValue<std::uint8_t>() != 0);
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
