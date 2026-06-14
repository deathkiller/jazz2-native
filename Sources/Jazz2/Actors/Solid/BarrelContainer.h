#pragma once

#include "GenericContainer.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Barrel container
		
		A wooden barrel that breaks apart when hit by a shot, TNT or a charged player, releasing the
		event-specified contents (any collectible the level designer placed inside) for the player to grab.
	*/
	class BarrelContainer : public GenericContainer
	{
		DEATH_RUNTIME_OBJECT(GenericContainer);

	public:
		/** @brief Creates a new instance */
		BarrelContainer();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
	};
}