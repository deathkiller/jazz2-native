#pragma once

#include "../../ActorBase.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	class BonusWarp : public ActorBase
	{
	public:
		BonusWarp();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/BonusWarp");
		}

		void Activate(Player* player);

		uint16_t GetCost() const
		{
			return _cost;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;

	private:
		uint16_t _warpTarget, _cost;
		bool _setLaps;
		bool _fast;
	};
}