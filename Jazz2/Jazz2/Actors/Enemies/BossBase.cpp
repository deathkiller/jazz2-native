#include "BossBase.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"

namespace Jazz2::Actors::Enemies
{
	bool BossBase::OnPlayerDied()
	{
		if ((_flags & (ActorFlags::IsCreatedFromEventMap | ActorFlags::IsFromGenerator)) != ActorFlags::None) {
			auto events = _levelHandler->EventMap();
			if (events != nullptr) {
				if ((_flags & ActorFlags::IsFromGenerator) == ActorFlags::IsFromGenerator) {
					events->ResetGenerator(_originTile.X, _originTile.Y);
				}

				events->Deactivate(_originTile.X, _originTile.Y);
			}

			OnDeactivatedBoss();
			CollisionFlags |= CollisionFlags::IsDestroyed | CollisionFlags::SkipPerPixelCollisions;
			return true;
		}

		return false;
	}

	bool BossBase::OnTileDeactivate(int tx1, int ty1, int tx2, int ty2)
	{
		return false;
	}
}