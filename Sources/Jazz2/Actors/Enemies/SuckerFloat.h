#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Floating sucker
		
		Balloon-like sucker that hovers in a small circular path around its origin. When hit by an
		ordinary shot it pops and drops a deflating @ref Sucker that scoots off in the direction of the
		impact; only special moves, freezing, or heavy weapons destroy it outright.
	*/
	class SuckerFloat : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		SuckerFloat();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _phase;
		Vector2i _originPos;
	};
}