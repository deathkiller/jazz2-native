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
			_speed.X = sinf(_phase);

			_phase += timeMult * 0.02f;

			if (adjustedWaterLevel < _pos.Y) {
				// Water is above the enemy, return the enemy on the surface
				_pos.Y = adjustedWaterLevel;
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
				SetState(ActorState::ApplyGravitation, false);
				_onWater = true;

				_renderer.AnimPaused = false;
			} else {
				_renderer.AnimPaused = true;
			}
		}
	}

	bool TurtleTube::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}