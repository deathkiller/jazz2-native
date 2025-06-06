﻿#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	/** @brief Base class of a (pushable) solid object */
	class SolidObjectBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		SolidObjectBase();
		~SolidObjectBase();

		bool IsOneWay;
		bool Movable;

		float Push(bool left, float timeMult);

	protected:
		/** @{ @name Constants */

		static constexpr float PushSpeed = 0.5f;
		static constexpr float PushDecayTime = 6.0f;

		/** @} */

		void OnUpdate(float timeMult) override;

	private:
		float _pushingSpeedX;
		float _pushingTime;

		bool TryPushInternal(float timeMult, float speedX);
	};
}