#include "BossBase.h"
#include "../../../ILevelHandler.h"
#include "../../../Events/EventMap.h"

namespace Jazz2::Actors::Bosses
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

	bool BossBase::OnTileDeactivated()
	{
		// Boss cannot be deactivated
		return false;
	}

	void BossBase::SetHealthByDifficulty(std::int32_t health)
	{
		// Each player adds 50% health, up to +1000% (20 players)
		if (_levelHandler->IsReforged()) {
			float multiplier = 1.0f + (std::clamp((std::int32_t)_levelHandler->GetPlayers().size() - 1, 0, 20) * 0.5f);
			health = (std::int32_t)(health * multiplier);
		}

		EnemyBase::SetHealthByDifficulty(health);
	}
}