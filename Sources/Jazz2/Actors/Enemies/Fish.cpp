#include "Fish.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

namespace Jazz2::Actors::Enemies
{
	namespace
	{
		constexpr float SwimRadius = 64.0f;
		constexpr std::int32_t SwimPeriod = 1024;
		constexpr std::int32_t SwimTicksBeforeDash = 280;
		constexpr float DashDistance = 320.0f;
		constexpr float DashDivisor = 64.0f;
		constexpr std::int32_t DashTicks = 100;
		constexpr std::int32_t ReturnTicks = 64;
	}

	Fish::Fish()
		: _state(StateSwimming), _counter(0), _swimCounter(0), _tickTime(0.0f)
	{
	}

	void Fish::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Fish"_s);
	}

	Task<bool> Fish::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 100;

		_originPos = _returnFrom = _pos;

		async_await RequestMetadataAsync("Enemy/Fish"_s);
		SetFacingLeft(false);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Fish::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		// Measured in JJ2+: it doesn't move at all when it's not under the water
		if (_frozenTimeLeft > 0.0f || _originPos.Y < _levelHandler->GetWaterLevel()) {
			return;
		}

		_tickTime += timeMult * TicksPerFrame;
		while (_tickTime >= 1.0f) {
			_tickTime -= 1.0f;
			AdvanceTick();
		}
	}

	void Fish::AdvanceTick()
	{
		_counter++;

		Vector2f newPos;
		switch (_state) {
			case StateSwimming: {
				// Measured in JJ2+: a figure-eight of 64 px, once every 1024 ticks
				_swimCounter++;
				newPos = GetSwimPos();
				if (newPos.X != _pos.X) {
					SetFacingLeft(newPos.X < _pos.X);
				}

				if (_swimCounter >= SwimTicksBeforeDash) {
					Player* target = nullptr;
					float targetDistance = DashDistance;
					for (auto* player : _levelHandler->GetPlayers()) {
						float distance = (player->GetPos() - _pos).Length();
						if (player->GetHealth() > 0 && distance < targetDistance) {
							target = player;
							targetDistance = distance;
						}
					}
					if (target != nullptr) {
						// Measured in JJ2+: it covers a 64th of the distance each tick, so it overshoots
						_speedPerTick = (target->GetPos() - _pos) / DashDivisor;
						SetFacingLeft(_speedPerTick.X < 0.0f);
						SetAnimation(AnimState::TransitionAttack);
						_state = StateDashing;
						_counter = 0;
						newPos = _pos;
					}
				}
				break;
			}

			case StateDashing: {
				newPos = _pos + _speedPerTick;
				if (_counter >= DashTicks) {
					_returnFrom = newPos;
					SetFacingLeft(_originPos.X < newPos.X);
					SetAnimation(AnimState::Idle);
					_state = StateReturning;
					_counter = 0;
				}
				break;
			}

			case StateReturning: {
				float t = std::min((float)_counter / ReturnTicks, 1.0f);
				Vector2f swimStart = _originPos + Vector2f(0.0f, SwimRadius);
				newPos = _returnFrom + (swimStart - _returnFrom) * t;
				if (_counter >= ReturnTicks) {
					_state = StateSwimming;
					_counter = 0;
					_swimCounter = 0;
				}
				break;
			}
		}

		// Stays under the water surface
		float minY = _levelHandler->GetWaterLevel() + 8.0f;
		if (newPos.Y < minY) {
			newPos.Y = minY;
		}
		MoveInstantly(newPos, MoveType::Absolute | MoveType::Force);
	}

	Vector2f Fish::GetSwimPos() const
	{
		float angle = (float)_swimCounter * fTwoPi / SwimPeriod;
		return _originPos + Vector2f(sinf(angle) * SwimRadius, cosf(angle * 2.0f) * SwimRadius);
	}

	void Fish::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteVariableInt32(_state);
		dest.WriteVariableInt32(_counter);
		dest.WriteVariableInt32(_swimCounter);
		dest.WriteValueAsLE<float>(_speedPerTick.X);
		dest.WriteValueAsLE<float>(_speedPerTick.Y);
		dest.WriteValueAsLE<float>(_returnFrom.X);
		dest.WriteValueAsLE<float>(_returnFrom.Y);
	}

	void Fish::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_state = src.ReadVariableInt32();
		_counter = src.ReadVariableInt32();
		_swimCounter = src.ReadVariableInt32();
		_speedPerTick.X = src.ReadValueAsLE<float>();
		_speedPerTick.Y = src.ReadValueAsLE<float>();
		_returnFrom.X = src.ReadValueAsLE<float>();
		_returnFrom.Y = src.ReadValueAsLE<float>();
		SetAnimation(_state == StateDashing ? AnimState::TransitionAttack : AnimState::Idle);
	}

	bool Fish::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 2), Explosion::Type::SmallDark);

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}
