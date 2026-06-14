#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Moth
		
		Small decorative moth that rests on surfaces until a player brushes against it, at which
		point it flutters away in a randomized direction before settling again. It is purely
		ambient and does not harm the player.
	*/
	class Moth : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		Moth();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		float _timer;
		std::int32_t _direction;
	};
}