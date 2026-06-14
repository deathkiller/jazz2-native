#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
	 * @brief Helmut
	 *
	 * Helmeted creature that plods slowly along the ground, alternating between walking and pausing to
	 * idle, and reverses direction at walls and ledges. It has no special attack and harms the player
	 * only on contact; defeated by a single hit.
	 */
	class Helmut : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Helmut();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 0.9f;

		bool _idling;
		float _stateTime;
		bool _stuck;
		float _turnCooldown;
	};
}