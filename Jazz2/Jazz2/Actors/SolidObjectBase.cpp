#include "SolidObjectBase.h"

using namespace nCine;

namespace Jazz2::Actors
{
	SolidObjectBase::SolidObjectBase()
		:
		IsOneWay(false),
		Movable(false)
	{
		CollisionFlags |= CollisionFlags::CollideWithSolidObjects | CollisionFlags::IsSolidObject | CollisionFlags::SkipPerPixelCollisions;
	}

	bool SolidObjectBase::Push(bool left, float timeMult)
	{
		if (Movable) {
			for (int i = 0; i >= -4; i -= 2) {
				if (MoveInstantly(Vector2f((left ? -0.5f : 0.5f) * timeMult, i * timeMult), MoveType::Relative)) {
					return true;
				}
			}
		}

		return false;
	}
}