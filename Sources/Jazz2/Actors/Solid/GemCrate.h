#pragma once

#include "GenericContainer.h"

namespace Jazz2::Actors::Solid
{
	/**
	 * @brief Gem crate
	 *
	 * A wooden crate that breaks open when hit by a shot, TNT or a charged player, scattering a burst of
	 * coloured gems (red, green, blue and purple) for the player to collect.
	 */
	class GemCrate : public GenericContainer
	{
		DEATH_RUNTIME_OBJECT(GenericContainer);

	public:
		/** @brief Creates a new instance */
		GemCrate();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
	};
}