#include "SolidObjectBase.h"
#include "../ILevelHandler.h"

using namespace nCine;

namespace Jazz2::Actors
{
	SolidObjectBase::SolidObjectBase()
		:
		IsOneWay(false),
		Movable(false)
	{
		SetState(ActorState::CollideWithSolidObjects | ActorState::CollideWithSolidObjectsBelow |
			ActorState::IsSolidObject | ActorState::SkipPerPixelCollisions, true);
	}

	float SolidObjectBase::Push(bool left, float timeMult)
	{
		if (Movable) {
			float speedX = (left ? -0.5f : 0.5f) * timeMult;
			float farX = (left ? AABBInner.L : AABBInner.R);
			float farY = (AABBInner.T + AABBInner.B) * 0.5f;
			TileCollisionParams params = { TileDestructType::None, _speed.Y >= 0.0f };
			ActorState prevState = GetState();
			SetState(ActorState::CollideWithSolidObjectsBelow, false);
			bool success = _levelHandler->IsPositionEmpty(this, AABBf(farX - 1.0f, farY - 1.0f, farX + 1.0f, farY + 1.0f), params);
			SetState(prevState);

			if (success) {
				for (int i = 0; i >= -4; i -= 2) {
					if (MoveInstantly(Vector2f(speedX, i * timeMult), MoveType::Relative, params)) {
						return speedX;
					}
				}
			}
		}

		return 0.0f;
	}
}