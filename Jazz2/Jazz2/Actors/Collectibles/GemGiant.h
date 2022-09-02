#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Collectibles
{
	class GemGiant : public ActorBase
	{
	public:
		GemGiant();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool OnPerish(ActorBase* collider) override;
	};
}