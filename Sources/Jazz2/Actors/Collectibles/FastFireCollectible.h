#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/**
		@brief Fast fire (collectible)
		
		The fast-fire power-up from JJ2, depicted as the player's blaster. Collecting it increases the
		player's blaster fire rate (cumulative up to a maximum) and awards points.
	*/
	class FastFireCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		FastFireCollectible();
		~FastFireCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;

	private:
		// Allocated palette offset into the shared palette texture for recoloring to match the player (-1 = none)
		std::int32_t _paletteOffset;
	};
}