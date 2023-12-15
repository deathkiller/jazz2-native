#include "TurtleTough.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"
#include "../Explosion.h"
#include "../Player.h"
#include "../Solid/PushableBox.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Enemies
{
	TurtleTough::TurtleTough()
		:
		_stuck(false)
	{
	}

	void TurtleTough::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/TurtleTough"_s);
	}

	Task<bool> TurtleTough::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetHealthByDifficulty(4);
		_scoreValue = 500;

		SetState(ActorState::CollideWithTilesetReduced, true);

		async_await RequestMetadataAsync("Enemy/TurtleTough"_s);
		SetFacingLeft(nCine::Random().NextBool());
		SetAnimation(AnimState::Walk);

		_speed.X = (IsFacingLeft() ? -1 : 1) * DefaultSpeed;

		PlaceOnGround();

		async_return true;
	}

	void TurtleTough::OnUpdate(float timeMult)
	{
		EnemyBase::OnUpdate(timeMult);

		if (_frozenTimeLeft > 0.0f) {
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
	}

	void TurtleTough::OnUpdateHitbox()
	{
		UpdateHitbox(30, 40);
	}

	bool TurtleTough::OnPerish(ActorBase* collider)
	{
		CreateDeathDebris(collider);
		_levelHandler->PlayCommonSfx("Splat"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		if (!runtime_cast<Solid::PushableBox*>(collider)) {
			// Show explosion only if it was not killed by pushable box
			Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2), Explosion::Type::Large);
		}

		TryGenerateRandomDrop();

		return EnemyBase::OnPerish(collider);
	}
}