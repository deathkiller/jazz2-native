#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Bomb : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		Bomb();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;

	private:
		float _timeLeft;
	};
}