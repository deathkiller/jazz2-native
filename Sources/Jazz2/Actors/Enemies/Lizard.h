#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Lizard
		
		Grounded reptile that walks back and forth, hissing occasionally and turning around at walls and
		ledges. It can also drop in from above after losing its copter, harming the player only on
		contact. Defeated by a single hit, with a festive Xmas variant available.
	*/
	class Lizard : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Lizard();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnHitFloor(float timeMult) override;
		void OnHitWall(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 1.0f;

		bool _stuck;
		bool _isFalling;
	};
}