#pragma once

#include "GenericContainer.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Crate container
		
		A wooden crate that breaks open when hit by a shot, TNT or a charged player, releasing the
		event-specified contents (any collectible the level designer placed inside) for the player to grab.
	*/
	class CrateContainer : public GenericContainer
	{
		DEATH_RUNTIME_OBJECT(GenericContainer);

	public:
		/** @brief Creates a new instance */
		CrateContainer();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
	};
}