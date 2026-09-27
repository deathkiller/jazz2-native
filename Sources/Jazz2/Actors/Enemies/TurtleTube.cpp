#include "TurtleTube.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	TurtleTube::TurtleTube()
		: _onWater(false), _phase(0.0f)
	{
	}

	void TurtleTube::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/TurtleTube"_s);
	}

	Task<bool> TurtleTube::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(2);
		_scoreValue = 200;

		async_await RequestMetadataAsync("Enemy/TurtleTube"_s);
		SetAnimation(AnimState::Idle);

		float adjustedWaterLevel = _levelHandler->GetWaterLevel() + WaterDifference;
		if (adjustedWaterLevel <= _pos.Y) {
			// Water is above the enemy, it's floating on the water
			_pos.Y = adjustedWaterLevel;
			SetState(ActorState::ApplyGravitation, false);
			_onWater = true;
		} else {
			// Water is below the enemy, apply gravitation and pause the animation
			_renderer.AnimPaused = true;
		}

		async_return true;
	}

	void TurtleTube::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		float adjustedWaterLevel = _levelHandler->GetWaterLevel() + WaterDifference;
		if (_onWater) {
			// Floating on the water
			_speed.X = sinApprox(_phase);

			_phase += timeMult * 0.02f;

			if (adjustedWaterLevel < _pos.Y) {
				// Water is above the enemy, return the enemy on the surface
				_pos.Y = adjustedWaterLevel;
				// The enemy is put back on the surface instead of swimming there, and a `ModifierSetWater` can
				// move that surface across the whole level at once, so the snap must not be taken for a path
				// travelled straight through everything in the column (see ResetPathTracking())
				ResetPathTracking();
			} else if (adjustedWaterLevel > _pos.Y) {
				// Water is below the enemy, apply gravitation and pause the animation 
				_speed.X = 0.0f;
				SetState(ActorState::ApplyGravitation, true);
				_onWater = false;
			}
		} else {
			if (adjustedWaterLevel <= _pos.Y) {
				// Water is above the enemy, return the enemy on the surface
				_pos.Y = adjustedWaterLevel;
				ResetPathTracking();
				SetState(ActorState::ApplyGravitation, false);
				_onWater = true;

				_renderer.AnimPaused = false;
			} else {
				_renderer.AnimPaused = true;
			}
		}
	}

	void TurtleTube::OnSerializeState(Stream& dest)
	{
		EnemyBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>(_onWater ? 1 : 0);
		dest.WriteValueAsLE<float>(_phase);
	}

	void TurtleTube::OnDeserializeState(Stream& src)
	{
		EnemyBase::OnDeserializeState(src);

		_onWater = (src.ReadValue<std::uint8_t>() != 0);
		_phase = src.ReadValueAsLE<float>();
	}

	bool TurtleTube::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}