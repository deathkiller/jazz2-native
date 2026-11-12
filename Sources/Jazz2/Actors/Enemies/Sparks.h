#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Sparks
		
		Floating ghost-like enemy that creeps up on a player who turns away from it and slows down
		while it is watched. With nobody near it stops where it is. Defeated by a single hit.
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
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		Vector2f _speedPerTick;
		float _tickTime;

		void AdvanceTick();
		static float ApproachSpeed(float speed, float target);
	};
}