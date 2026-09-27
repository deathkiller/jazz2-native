#pragma once

#include "../EnemyBase.h"

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Base class of an enemy boss
		
		A boss is a powerful, level-ending enemy with a large health pool shown on a dedicated health bar.
		Defeating it typically completes the level and shows the level's end text.
	*/
	class BossBase : public Enemies::EnemyBase
	{
		DEATH_RUNTIME_OBJECT(Enemies::EnemyBase);

	public:
		/** @brief Called when the boss is activated */
		virtual bool OnActivatedBoss() = 0;
		/** @brief Called when the boss is deactivated */
		virtual void OnDeactivatedBoss() {};

		/** @brief Called when a player died, returns `true` if the boss was deactivated */
		virtual bool OnPlayerDied();

	protected:
		// A restored boss has to come back fresh and dormant, its sub-actors and multi-phase fight can't be resumed,
		// so it's spawned again by the event map instead
		bool IsSerializable() const override { return false; }
		bool OnTileDeactivated() override;
		void SetHealthByDifficulty(std::int32_t health) override;
	};
}