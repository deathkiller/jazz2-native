#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
	 * @brief Checkpoint
	 *
	 * Mid-level marker that the player activates by touching it, saving the spot as the
	 * respawn position. After dying, the rabbit reappears at the last activated checkpoint
	 * instead of the level start.
	 */
	class Checkpoint : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		Checkpoint();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;

	private:
		std::uint8_t _theme;
		bool _activated;
	};
}