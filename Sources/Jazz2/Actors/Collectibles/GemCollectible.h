#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class GemCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		GemCollectible();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Collectible/Gems"_s);
		}

	protected:
		std::uint8_t _gemType;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		void OnCollect(Player* player) override;
	};
}