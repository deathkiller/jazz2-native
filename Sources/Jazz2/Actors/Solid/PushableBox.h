#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Pushable box
		
		A solid rock or crate the player can push horizontally by walking into it, typically to reach higher
		ledges or solve simple block puzzles. It cannot be destroyed by weapons (shots ricochet off it),
		though it can be frozen.
	*/
	class PushableBox : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		PushableBox();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(ActorBase* other) override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		float GetIceShrapnelScale() const override;
	};
}