#include "BossBase.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"

namespace Jazz2::Actors::Enemies
{
	bool BossBase::OnPlayerDied()
	{
		if ((GetState() & (ActorState::IsCreatedFromEventMap | ActorState::IsFromGenerator)) != ActorState::None) {
			auto events = _levelHandler->EventMap();
			if (events != nullptr) {
				if (GetState(ActorState::IsFromGenerator)) {
					events->ResetGenerator(_originTile.X, _originTile.Y);
				}

				events->Deactivate(_originTile.X, _originTile.Y);
			}

			OnDeactivatedBoss();
			SetState(ActorState::IsDestroyed | ActorState::SkipPerPixelCollisions, true);
			return true;
		}

		return false;
	}

	bool BossBase::OnTileDeactivate(int tx1, int ty1, int tx2, int ty2)
	{
		return false;
	}
}