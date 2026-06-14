#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Fat chick
		
		Plump bird that waddles back and forth on the ground, turning at walls and ledges and homing in
		when a player draws close. When the player is right in front of it, it performs a short melee
		peck attack. Takes several hits to defeat.
	*/
	class FatChick : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		FatChick();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.9f;

		bool _isAttacking;
		bool _stuck;

		void Attack();
	};
}