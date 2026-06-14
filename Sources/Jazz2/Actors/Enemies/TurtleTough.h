#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Tough turtle
		
		Large armored turtle that marches back and forth along the ground, turning at walls and ledges
		and harming the player only on contact. Far more durable than a regular @ref Turtle, it takes
		many hits and bursts in a large explosion when destroyed.
	*/
	class TurtleTough : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		TurtleTough();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float DefaultSpeed = 1.0f;

		bool _stuck;
	};
}