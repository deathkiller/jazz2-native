#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	/** @brief Base class of a (pushable) solid object */
	class SolidObjectBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		SolidObjectBase();
		~SolidObjectBase();

		/** @brief Whether the object can be passed through from below (i.e. acts as a one-way platform) */
		bool IsOneWay;
		/** @brief Whether the object can be pushed by a player */
		bool Movable;

		/** @brief Pushes the object in a given direction and returns the resulting horizontal speed (0 if it cannot move) */
		float Push(bool left, float timeMult);

	protected:
		/** @{ @name Constants */

		/** @brief Horizontal speed applied while the object is being pushed */
		static constexpr float PushSpeed = 0.5f;
		/** @brief Time (in frames) the push persists after the player stops pushing */
		static constexpr float PushDecayTime = 6.0f;

		/** @} */

		void OnUpdate(float timeMult) override;

	private:
		float _pushingSpeedX;
		float _pushingTime;

		bool TryPushInternal(float timeMult, float speedX);
	};
}