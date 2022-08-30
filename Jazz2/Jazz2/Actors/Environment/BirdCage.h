#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class BirdCage : public ActorBase
	{
	public:
		BirdCage();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	private:
		uint8_t _type;
		bool _activated;

		void ApplyToPlayer(Player* player);
	};
}