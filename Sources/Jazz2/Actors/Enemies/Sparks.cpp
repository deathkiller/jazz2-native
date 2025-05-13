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
			if (!player->IsInvulnerable()) {
				targetPos = player->GetPos();
				Vector2f direction = (targetPos - _pos);
				float length = direction.Length();
				if (length < 180.0f && targetPos.Y < _levelHandler->GetWaterLevel()) {
					if (length > 100.0f) {
						direction.Normalize();
						float maxSpeed = DefaultSpeed;
						switch (_levelHandler->GetDifficulty()) {
							case GameDifficulty::Normal: maxSpeed += 1.0f;
							case GameDifficulty::Hard: maxSpeed += 2.0f;
						}
						_speed.X = lerpByTime(_speed.X, direction.X * maxSpeed, 0.04f, timeMult);
						_speed.Y = lerpByTime(_speed.Y, direction.Y * maxSpeed, 0.04f, timeMult);
						SetFacingLeft(_speed.X >= 0.0f);
					}
					return;
				}
			}
		}
		
		_speed.X = lerpByTime(_speed.X, 0.0f, 0.04f, timeMult);
		_speed.Y = lerpByTime(_speed.Y, 0.0f, 0.04f, timeMult);
	}

	bool Sparks::OnPerish(ActorBase* collider)
	{
		CreateParticleDebrisOnPerish(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}