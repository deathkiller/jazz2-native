#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	enum class TriggerCrateState {
		Off,
		On,
		Toggle
	};

	class TriggerCrate : public SolidObjectBase
	{
	public:
		TriggerCrate();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		TriggerCrateState _newState;
		uint8_t _triggerId;
	};
}