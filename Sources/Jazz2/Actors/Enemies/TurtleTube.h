#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
	 * @brief Tube turtle
	 *
	 * Turtle riding an inner tube that bobs on the water surface, drifting gently side to side. If the
	 * water level drops below it, it falls under gravity and freezes its animation until it floats
	 * again. Harms the player on contact and takes a couple of hits to defeat.
	 */
	class TurtleTube : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		TurtleTube();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		static constexpr float WaterDifference = -16.0f;

		bool _onWater;
		float _phase;
	};
}