#pragma once

#include "GenericContainer.h"

namespace Jazz2::Actors::Solid
{
	class GemCrate : public GenericContainer
	{
	public:
		GemCrate();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool OnPerish(ActorBase* collider) override;
	};
}