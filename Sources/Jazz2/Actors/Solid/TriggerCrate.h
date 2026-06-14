#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/** @brief State of @ref TriggerCrate */
	enum class TriggerCrateState {
		Off,	/**< Sets the trigger off */
		On,		/**< Sets the trigger on */
		Toggle	/**< Toggles the trigger */
	};

	/**
		@brief Trigger crate
		
		A wooden crate that, when destroyed by a shot, TNT or a charged player, sets, clears or toggles a
		numbered level trigger. Triggers in turn switch trigger-zone tiles on or off, opening or blocking paths
		elsewhere in the level.
	*/
	class TriggerCrate : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		TriggerCrate();

		bool OnHandleCollision(ActorBase* other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		TriggerCrateState _newState;
		std::uint8_t _triggerId;
	};
}