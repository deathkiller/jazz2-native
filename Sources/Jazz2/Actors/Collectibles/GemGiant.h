#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Collectibles
{
	/**
	 * @brief Giant gem
	 *
	 * The large gem crystal from JJ2 that cannot be collected directly. It must be destroyed with weapons
	 * fire, TNT, or a special-move attack, whereupon it shatters into a scattered burst of regular gems.
	 */
	class GemGiant : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		GemGiant();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
	};
}