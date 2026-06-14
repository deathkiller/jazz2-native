#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/**
	 * @brief Power-up weapon monitor
	 *
	 * A floating monitor that, when broken by the player, permanently upgrades the matching weapon to its
	 * powered-up version and tops up its ammunition.
	 */
	class PowerUpWeaponMonitor : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		PowerUpWeaponMonitor();

		bool OnHandleCollision(ActorBase* other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Destroys the monitor and applies the weapon upgrade to the specified player */
		void DestroyAndApplyToPlayer(Player* player);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		WeaponType _weaponType;
	};
}