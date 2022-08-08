#pragma once

#include "GenericContainer.h"

namespace Jazz2::Actors::Solid
{
	class BarrelContainer : public GenericContainer
	{
	public:
		BarrelContainer();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnHandleCollision(ActorBase* other) override;
		bool OnPerish(ActorBase* collider) override;
	};
}