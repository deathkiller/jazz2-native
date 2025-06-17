#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/** @brief State of @ref TriggerCrate */
	enum class TriggerCrateState {
		Off,
		On,
		Toggle
	};

	/** @brief Trigger crate */
	class TriggerCrate : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		TriggerCrate();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		TriggerCrateState _newState;
		std::uint8_t _triggerId;
	};
}