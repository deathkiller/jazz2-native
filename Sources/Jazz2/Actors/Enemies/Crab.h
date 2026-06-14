#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Crab
		
		Walks steadily along the ground, turning around when it hits a wall or ledge it cannot pass,
		while chittering and making footstep sounds. Takes several hits to defeat and bursts in a large
		explosion when destroyed.
	*/
	class Crab : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Crab();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.7f;

		float _noiseCooldown;
		float _stepCooldown;
		bool _canJumpPrev;
		bool _stuck;
	};
}