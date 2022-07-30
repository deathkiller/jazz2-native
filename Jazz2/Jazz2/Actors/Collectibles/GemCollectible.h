#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class GemCollectible : public CollectibleBase
	{
	public:
		GemCollectible();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Collectible/Gems"_s);
		}

	protected:
		uint16_t _gemType;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		void OnCollect(Player* player) override;
	};
}