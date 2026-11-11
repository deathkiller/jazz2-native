#pragma once

#include "EnemyBase.h"

namespace Jazz2::Actors::Enemies
{
	/**
		@brief Fencer
		
		Sword-wielding enemy that stands its ground until a player comes near, then faces them and
		lunges with a hopping thrust, springing either forward or backward unpredictably between
		attacks. Takes several hits to defeat.
	*/
	class Fencer : public EnemyBase
	{
		DEATH_RUNTIME_OBJECT(EnemyBase);

	public:
		/** @brief Creates a new instance */
		Fencer();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnPerish(ActorBase* collider) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;
		void OnHitFloor(float timeMult) override;
		float GetGravityModifier(float baseGravity, bool isRising) const override;

	private:
		/** @brief Distance within which it turns toward the nearest player */
		static constexpr float SightDistance = 362.0f;
		/** @brief Largest vertical distance to a player it reacts to */
		static constexpr float SightHeight = 64.0f;
		/** @brief Distance within which it hops toward a player who has just fired */
		static constexpr float ShotReactDistance = 256.0f;
		/** @brief Distance within which it hops back from a player who isn't firing */
		static constexpr float HopBackDistance = 160.0f;
		/** @brief How recently a player must have fired, in original ticks */
		static constexpr float ShotWindowTicks = 20.0f;
		/** @brief Time on the ground before it hops back, in original ticks */
		static constexpr float HopBackWaitTicks = 141.0f;
		/** @brief Gravity of a hop in px per original tick squared */
		static constexpr float HopGravity = 0.25f * TicksPerFrame * TicksPerFrame;
		/** @brief Length of the hop animation in original ticks, measured in JJ2+ */
		static constexpr float HopAnimTicks = 20.0f;

		float _stateTime;
		bool _airborne;

		void Hop(float speedX, float speedY, float offsetY);
	};
}