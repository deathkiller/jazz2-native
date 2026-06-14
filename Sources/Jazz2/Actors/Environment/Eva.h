#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Eva
		
		Eva Earlong, the rabbit who waits at the end of certain levels. When a player who has been
		turned into a frog reaches her, she gives a kiss that reverts the frog morph back to the
		normal rabbit form.
	*/
	class Eva : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		Eva();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		float _animationTime;
	};
}