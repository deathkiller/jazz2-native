#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/**
	 * @brief Gem (collectible)
	 *
	 * The colored gems collected for points throughout JJ2. Red, green, and blue gems are worth
	 * increasing amounts (1, 5, and 10 respectively); purple gems also exist. Each gem adds to the
	 * player's gem count and score.
	 */
	class GemCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		GemCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		float _ignoreTime;
		std::uint8_t _gemType;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnCollect(Player* player) override;
	};
}