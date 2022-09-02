#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class Checkpoint : public ActorBase
	{
	public:
		Checkpoint();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	private:
		uint8_t _theme;
		bool _activated;
	};
}