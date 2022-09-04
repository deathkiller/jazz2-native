#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class BirdCage : public ActorBase
	{
	public:
		BirdCage();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;

	private:
		uint8_t _type;
		bool _activated;

		void ApplyToPlayer(Player* player);
	};
}