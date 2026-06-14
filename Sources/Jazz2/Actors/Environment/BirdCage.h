#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Environment
{
	/**
	 * @brief Bird cage
	 *
	 * Solid cage holding a captive companion bird (Chuck or Birdy). When the player breaks it
	 * open---by shooting it, hitting it with TNT, or smashing it while invincible---the bird is
	 * released and becomes the player's companion.
	 */
	class BirdCage : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		BirdCage();

		bool OnHandleCollision(ActorBase* other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;

	private:
		std::uint8_t _type;
		bool _activated;

		bool TryApplyToPlayer(Player* player);
	};
}