#include "Sparks.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Sparks::Sparks()
	{
	}

	void Sparks::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Sparks"_s);
	}

	Task<bool> Sparks::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, false);

		SetHealthByDifficulty(1);
		_scoreValue = 100;

		async_await RequestMetadataAsync("Enemy/Sparks"_s);
		SetFacingLeft(true);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void Sparks::OnUpdate(float timeMult)
	{
		OnUpdateHitbox();
		HandleBlinking(timeMult);
		UpdateFrozenState(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		MoveInstantly(_speed * timeMult, MoveType::Relative | MoveType::Force);

		Vector2f targetPos;
		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			targetPos = player->GetPos();
			Vector2f direction = (_pos - targetPos);
			float length = direction.Length();
			if (length < 180.0f && targetPos.Y < _levelHandler->WaterLevel()) {
				if (length > 100.0f) {
					direction.Normalize();
					_speed = (direction * DefaultSpeed + _speed) * 0.5f;
				}
				return;
			}
		}

		_speed.X = 0.0f;
		_speed.Y = 0.0f;
	}

	bool Sparks::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}