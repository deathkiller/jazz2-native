#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	class Stopwatch : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		Stopwatch();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Collectible/Stopwatch"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}