#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Environment
{
	/**
		@brief Bird
		
		Companion bird (the red Chuck or the yellow Birdy) freed from a bird cage that follows
		its owning player around. It automatically attacks nearby enemies---Chuck fires shots
		while Birdy dives at them---and flies away if abandoned.
	*/
	class Bird : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		Bird();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Leaves the owner */
		void FlyAway();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnAnimationFinished() override;

	private:
		std::uint8_t _type;
		Player* _owner;
		float _fireCooldown;
		float _attackTime;

		void TryFire();
	};
}