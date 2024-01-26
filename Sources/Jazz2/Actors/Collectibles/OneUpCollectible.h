#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class OneUpCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		OneUpCollectible();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Collectible/OneUp"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}