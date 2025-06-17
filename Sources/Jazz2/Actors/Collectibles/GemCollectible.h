#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/** @brief Gem collectible */
	class GemCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		GemCollectible();

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