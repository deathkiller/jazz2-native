#pragma once

#include "GenericContainer.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Ammo crate
		
		A wooden crate marked with a weapon icon that breaks open when hit by a shot, TNT or a charged player,
		spilling ammunition for that weapon. If no contents are specified, it yields a random handful of ammo
		for weapons the players already carry.
	*/
	class AmmoCrate : public GenericContainer
	{
		DEATH_RUNTIME_OBJECT(GenericContainer);

	public:
		/** @brief Creates a new instance */
		AmmoCrate();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
	};
}