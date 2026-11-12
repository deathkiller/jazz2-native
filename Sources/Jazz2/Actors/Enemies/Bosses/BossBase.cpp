#include "BossBase.h"
#include "../../../ILevelHandler.h"
#include "../../../Events/EventMap.h"
#include "../../Player.h"

namespace Jazz2::Actors::Bosses
{
	bool BossBase::OnPlayerDied()
	{
		// The original keeps the boss, its health and its state when a player dies
		return false;
	}

	bool BossBase::OnTileDeactivated()
	{
		// Boss cannot be deactivated
		return false;
	}

	void BossBase::SetHealthByDifficulty(std::int32_t health)
	{
		// The original's health never depends on difficulty or player count, only Reforged scales it
		if (!_levelHandler->IsReforged()) {
			_health = std::max<std::int32_t>(health, 1);
			_maxHealth = _health;
			return;
		}

		// Each player adds 50% health, up to +1000% (20 players)
		float multiplier = 1.0f + (std::clamp<std::int32_t>((std::int32_t)_levelHandler->GetPlayers().size() - 1, 0, 20) * 0.5f);
		health = (std::int32_t)(health * multiplier);

		EnemyBase::SetHealthByDifficulty(health);
	}
}