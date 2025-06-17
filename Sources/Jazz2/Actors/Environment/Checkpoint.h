#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/** @brief Checkpoint */
	class Checkpoint : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		Checkpoint();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;

	private:
		std::uint8_t _theme;
		bool _activated;
	};
}