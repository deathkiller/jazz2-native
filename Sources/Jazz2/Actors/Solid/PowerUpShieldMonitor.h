#pragma once

#include "../SolidObjectBase.h"
#include "../../ShieldType.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Power-up shield monitor
		
		A floating monitor that, when broken by the player, grants a temporary protective shield of a given
		element (fire, water, lightning or laser) that surrounds the player and wears off after a while.
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
		/** @brief How long a shield lasts when the monitor is broken by a jump or a move, 2100 ticks of the original */
		static constexpr float StompShieldDuration = 2100.0f * 6.0f / 7.0f;
		/** @brief How long a shield lasts when the monitor is shot, 2800 ticks of the original */
		static constexpr float ShotShieldDuration = 2800.0f * 6.0f / 7.0f;

		void DestroyAndApplyToPlayer(Player* player, bool byShot = false);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		ShieldType _shieldType;
	};
}