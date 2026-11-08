#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Sparks
		
		Floating ghost-like enemy that hovers idly until a player approaches, then accelerates toward
		them. Once it reaches the player, it coasts on and slowly comes to a stop. It doesn't chase
		players under the water and is defeated by a single hit.
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
		bool _coasting;

		void AdvanceTick();
	};
}