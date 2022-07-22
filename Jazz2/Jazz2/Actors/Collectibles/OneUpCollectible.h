#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class OneUpCollectible : public CollectibleBase
	{
	public:
		OneUpCollectible();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Collectible/OneUp");
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}