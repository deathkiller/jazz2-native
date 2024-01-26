#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Environment
{
	class BirdCage : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		BirdCage();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;

	private:
		uint8_t _type;
		bool _activated;

		bool TryApplyToPlayer(Player* player);
	};
}