#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Turtle shell
		
		Empty shell left behind when a @ref Turtle is knocked out of it, sliding along the ground with
		friction until it stops. While moving it bowls over other enemies, shells, and crates in its
		path, and can be set sliding again by shooting it; certain weapons destroy it outright.
	*/
	class TurtleShell : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		TurtleShell();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		bool OnHandleCollision(ActorBase* other) override;
		void OnHitFloor(float timeMult) override;

	private:
		float _lastAngle;
	};
}