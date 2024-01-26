#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class IceBlock : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		IceBlock();

		void ResetTimeLeft();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		float _timeLeft;
	};
}