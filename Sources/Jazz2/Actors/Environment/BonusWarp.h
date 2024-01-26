#pragma once

#include "../ActorBase.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	class BonusWarp : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		BonusWarp();

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/BonusWarp"_s);
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
		uint8_t _warpTarget, _cost;
		bool _setLaps;
		bool _fast;
	};
}