#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class CarrotCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		CarrotCollectible();

		static void Preload(const ActorActivationDetails& details)
		{
			bool maxCarrot = (details.Params[0] != 0);
			if (maxCarrot) {
				PreloadMetadataAsync("Collectible/CarrotFull"_s);
			} else {
				PreloadMetadataAsync("Collectible/Carrot"_s);
			}
		}

	protected:
		bool _maxCarrot;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}