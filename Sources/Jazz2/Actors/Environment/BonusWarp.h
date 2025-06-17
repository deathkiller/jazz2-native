#pragma once

#include "../ActorBase.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	/** @brief Bonus warp */
	class BonusWarp : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		BonusWarp();

		static void Preload(const ActorActivationDetails& details);

		/** @brief Activates the warp */
		void Activate(Player* player);

		/** @brief Returns cost of the warp in coins */
		std::uint16_t GetCost() const {
			return _cost;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;

	private:
		std::uint8_t _warpTarget, _cost;
		bool _setLaps;
		bool _fast;
	};
}