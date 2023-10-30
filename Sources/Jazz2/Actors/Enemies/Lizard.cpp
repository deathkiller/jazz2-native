#include "Lizard.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

namespace Jazz2::Actors::Enemies
{
	Lizard::Lizard()
		:
		_stuck(false),
		_isFalling(false)
	{
	}

	void Lizard::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Enemy/Lizard"_s);
				break;
			case 1: // Xmas
				PreloadMetadataAsync("Enemy/LizardXmas"_s);
				break;
		}
	}

	Task<bool> Lizard::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		_isFalling = (details.Params[1] != 0);
		bool isFacingLeft = (_isFalling ? details.Params[2] != 0 : Random().NextBool());

		SetHealthByDifficulty(_isFalling ? 6 : 1);
		_scoreValue = 100;

		SetState(ActorState::CollideWithTilesetReduced, true);

		switch (theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Enemy/Lizard"_s);
				break;
			case 1: // Xmas
				async_await RequestMetadataAsync("Enemy/LizardXmas"_s);
				break;
		}
		SetFacingLeft(isFacingLeft);
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;

		if (_isFalling) {
			// Lizard lost its copter, check if spawn position is
			// empty, because walking Lizard has bigger hitbox
			OnUpdateHitbox();

			TileCollisionParams params = { TileDestructType::None, _speed.Y >= 0.0f };
			if (!_levelHandler->IsPositionEmpty(this, AABBInner, params)) {
				// Lizard was probably spawned into a wall, try to move it
				// from the wall by 4px steps (max. 12px) in both directions
				const float Adjust = 4.0f;

				for (int i = 1; i < 4; i++) {
					if (MoveInstantly(Vector2(Adjust * i, 0.0f), MoveType::Relative) ||
						MoveInstantly(Vector2(-Adjust * i, 0.0f), MoveType::Relative)) {
						// Empty spot found
						break;
					}
				}
			}
		} else {
			PlaceOnGround();
		}

		async_return true;
	}

	void Lizard::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f || _isFalling) {
			return;
		}

		if (GetState(ActorState::CanJump)) {
			if (!CanMoveToPosition(_speed.X * 4.0f, 0)) {
				if (_stuck) {
					MoveInstantly(Vector2f(0.0f, -2.0f), MoveType::Relative | MoveType::Force);
				} else {
					SetFacingLeft(!IsFacingLeft());
					_speed.X = (IsFacingLeft() ? -1.0f : 1.0f) * DefaultSpeed;
					_stuck = true;
				}
			} else {
				_stuck = false;
			}
		}

		if (Random().NextFloat() < 0.002f * timeMult) {
			PlaySfx("Noise"_s, 0.4f);
		}
	}

	void Lizard::OnUpdateHitbox()
	{
		UpdateHitbox(30, 30);
	}

	void Lizard::OnHitFloor(float timeMult)
	{
		EnemyBase::OnHitFloor(timeMult);

		// TODO: Partial workaround for stuck enemies
		SetState(ActorState::CanJump, true);

		if (_isFalling) {
			_isFalling = false;
			SetHealthByDifficulty(1);
		}
	}

	void Lizard::OnHitWall(float timeMult)
	{
		EnemyBase::OnHitWall(timeMult);

		// TODO: Partial workaround for stuck enemies
		SetState(ActorState::CanJump, true);
	}

	bool Lizard::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}