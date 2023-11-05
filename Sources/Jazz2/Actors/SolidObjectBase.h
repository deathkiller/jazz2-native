#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	class SolidObjectBase : public ActorBase
	{
	public:
		SolidObjectBase();
		~SolidObjectBase();

		bool IsOneWay;
		bool Movable;

		float Push(bool left, float timeMult);

	protected:
		static constexpr float PushSpeed = 0.5f;
		static constexpr float PushDecayTime = 6.0f;

		void OnUpdate(float timeMult) override;

	private:
		float _pushingSpeedX;
		float _pushingTime;

		bool TryPushInternal(float timeMult, float speedX);
	};
}