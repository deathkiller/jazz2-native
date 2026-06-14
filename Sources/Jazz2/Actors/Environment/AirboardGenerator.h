#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
	 * @brief Airboard generator
	 *
	 * Spawn point that hands the player an Airboard---a flying surfboard the rabbit rides
	 * through the air. Touching it grants the Airboard flight modifier, then the generator
	 * goes dormant for a configured delay before it can be picked up again.
	 */
	class AirboardGenerator : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		AirboardGenerator();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		std::uint8_t _delay;
		float _timeLeft;
		bool _active;
	};
}