#pragma once

#include "GenericContainer.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Ammo barrel
		
		A wooden barrel that bursts when broken by a shot, TNT or a charged player, scattering ammunition for
		a single weapon type. If no contents are specified, it yields a random handful of ammo for weapons the
		players already carry.
	*/
	class AmmoBarrel : public GenericContainer
	{
		DEATH_RUNTIME_OBJECT(GenericContainer);

	public:
		/** @brief Creates a new instance */
		AmmoBarrel();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
	};
}