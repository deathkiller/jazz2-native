#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Collectibles
{
	class GemGiant : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		GemGiant();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
	};
}