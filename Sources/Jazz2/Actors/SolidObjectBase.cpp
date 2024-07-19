#include "SolidObjectBase.h"
#include "../ILevelHandler.h"
#include "Player.h"

using namespace Jazz2::Tiles;
using namespace nCine;

namespace Jazz2::Actors
{
	SolidObjectBase::SolidObjectBase()
		: IsOneWay(false), Movable(false), _pushingSpeedX(0.0f), _pushingTime(0.0f)
	{
		SetState(ActorState::CollideWithSolidObjects | ActorState::CollideWithSolidObjectsBelow |
			ActorState::IsSolidObject | ActorState::SkipPerPixelCollisions, true);
	}

	SolidObjectBase::~SolidObjectBase()
	{
		auto players = _levelHandler->GetPlayers();
		for (auto* player : players) {
			player->CancelCarryingObject(this);
		}
	}

	float SolidObjectBase::Push(bool left, float timeMult)
	{
		if (Movable) {
			if (_pushingTime > 0.0f) {
				_pushingTime = PushDecayTime;
				return _pushingSpeedX;
			}

			float speedX = (left ? -PushSpeed : PushSpeed);
			if (TryPushInternal(timeMult, speedX)) {
				_pushingSpeedX = speedX;
				_pushingTime = PushDecayTime;
				return speedX;
			}
		}

		return 0.0f;
	}

	void SolidObjectBase::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		Vector2f lastPos = _pos;

		if (_pushingTime > 0.0f) {
			if (_frozenTimeLeft <= 0.0f) {
				_pushingTime -= timeMult;
			}

			if (!TryPushInternal(timeMult, _pushingSpeedX)) {
				_pushingTime = 0.0f;
			}
		}

		if (_pushingTime > 0.0f) {
			Vector2f diff = _pos - lastPos;
			AABBf aabb = AABBInner;
			aabb.T -= 1.0f;

			auto players = _levelHandler->GetPlayers();
			for (auto* player : players) {
				if (player->GetCarryingObject() == this) {
					AABBf aabb3 = aabb;
					aabb3.T -= 1.0f;
					if (aabb3.Overlaps(player->AABBInner) && player->GetState(ActorState::ApplyGravitation)) {
						// Try to adjust Y, because it collides with the box sometimes
						bool success = false;
						Vector2f playerPos = player->GetPos();
						Vector2f adjustedPos = Vector2f(playerPos.X + diff.X, playerPos.Y + diff.Y);
						for (std::int32_t i = 0; i < 8; i++) {
							if (player->MoveInstantly(adjustedPos, MoveType::Absolute)) {
								success = true;
								break;
							}
							adjustedPos.Y -= 0.5f * timeMult;
						}

						if (!success) {
							player->MoveInstantly(Vector2f(0.0f, diff.Y), MoveType::Relative);
						}

						player->UpdateCarryingObject(this);
					} else {
						player->CancelCarryingObject();
					}
				} else if (aabb.Overlaps(player->AABBInner) && player->GetSpeed().Y >= diff.Y * timeMult && !player->CanMoveVertically()) {
					player->UpdateCarryingObject(this);
				}
			}
		} else {
			// If it's not frozen, reset carrying state
			auto players = _levelHandler->GetPlayers();
			for (auto* player : players) {
				player->CancelCarryingObject(this);
			}
		}
	}

	bool SolidObjectBase::TryPushInternal(float timeMult, float speedX)
	{
		float farX = (speedX < 0.0f ? AABBInner.L : AABBInner.R);
		float farY = (AABBInner.T + AABBInner.B) * 0.5f;

		TileCollisionParams params = { TileDestructType::None, _speed.Y >= 0.0f };
		ActorState prevState = GetState();
		SetState(ActorState::CollideWithSolidObjectsBelow, false);
		bool success = _levelHandler->IsPositionEmpty(this, AABBf(farX - 1.0f, farY - 1.0f, farX + 1.0f, farY + 1.0f), params);
		SetState(prevState);

		if (success) {
			for (std::int32_t i = 0; i >= -2; i -= 2) {
				if (MoveInstantly(Vector2f(speedX * timeMult, i * timeMult), MoveType::Relative, params)) {
					return true;
				}
			}
		}

		return false;
	}
}