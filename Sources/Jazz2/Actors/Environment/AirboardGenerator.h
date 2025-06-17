#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/** @brief Airboard generator */
	class AirboardGenerator : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		AirboardGenerator();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

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