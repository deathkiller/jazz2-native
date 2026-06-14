#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Sparks
		
		Floating ghost-like enemy that hovers idly until a player approaches, then slowly drifts toward
		them to attack, chasing faster on higher difficulties and ignoring invulnerable players. It
		stays above the water and is defeated by a single hit.
	*/
	class Sparks : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Sparks();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 2.0f;
	};
}