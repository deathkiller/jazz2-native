#include "Skeleton.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	Skeleton::Skeleton()
		:
		_stuck(false)
	{
	}

	void Skeleton::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/Skeleton"_s);
	}

	Task<bool> Skeleton::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(3);
		_scoreValue = 200;

		SetState(ActorState::CollideWithTilesetReduced, true);

		async_await RequestMetadataAsync("Enemy/Skeleton"_s);
		SetFacingLeft(Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;

		PlaceOnGround();

		async_return true;
	}

	void Skeleton::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (GetState(ActorState::CanJump)) {
			if (!CanMoveToPosition(_speed.X * 4, 0)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}
	}

	void Skeleton::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	void Skeleton::OnHealthChanged(ActorBase* collider)
	{
		CreateSpriteDebris((AnimState)2, Random().Next(1, 3)); // Bone

		EnemyBase::OnHealthChanged(collider);
	}

	bool Skeleton::OnPerish(ActorBase* collider)
	{
		// TODO: Sound of bones
		// TODO: Use CreateDeathDebris(collider); instead?
		CreateParticleDebris();
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		if (_frozenTimeLeft <= 0.0f) {
			CreateSpriteDebris((AnimState)2, Random().Next(9, 12)); // Bone
			CreateSpriteDebris((AnimState)2, 1); // Skull
		}

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}