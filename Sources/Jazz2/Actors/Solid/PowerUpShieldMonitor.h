#pragma once

#include "../SolidObjectBase.h"
#include "../../ShieldType.h"

namespace Jazz2::Actors::Solid
{
	/**
	 * @brief Power-up shield monitor
	 *
	 * A floating monitor that, when broken by the player, grants a temporary protective shield of a given
	 * element (fire, water, lightning or laser) that surrounds the player and wears off after a while.
	 */
	class PowerUpShieldMonitor : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		PowerUpShieldMonitor();

		bool OnHandleCollision(ActorBase* other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Destroys the monitor and applies the shield to the specified player */
		void DestroyAndApplyToPlayer(Player* player);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		ShieldType _shieldType;
	};
}