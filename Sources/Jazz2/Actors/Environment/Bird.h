#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Environment
{
	class Bird : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		Bird();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

		void FlyAway();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnAnimationFinished() override;

	private:
		uint8_t _type;
		Player* _owner;
		float _fireCooldown;
		float _attackTime;

		void TryFire();
	};
}