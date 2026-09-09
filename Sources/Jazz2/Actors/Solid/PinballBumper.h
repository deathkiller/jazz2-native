#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Pinball bumper
		
		A round bumper found in pinball-themed levels that flashes and forcefully knocks the player away in the
		direction they struck it, awarding score, much like the bumpers on a real pinball table.
	*/
	class PinballBumper : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		PinballBumper();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
		/**
		 * @brief How far from its centre the bumper catches the player (px)
		 *
		 * Whatever is asked for here is widened by roughly half the player's own hitbox, because the search
		 * is an overlap test rather than a point-in-circle one. Measured by dropping past a bumper at a set
		 * horizontal offset: the original fires at 0 and at 16 px and misses from 48, where this reaches 48
		 * as well - so it is a little wide. Vertically the two already agree, both catching the player about
		 * 36 px above or below the centre.
		 *
		 * **Left at 16 deliberately.** The obvious fix of shrinking it pulls the *vertical* reach in by the
		 * same amount, and the launch is radial: the player then meets the bumper nearly level with it and
		 * leaves almost horizontally. Tried at 6, which fixed the spurious hit at 48 px and turned the 16 px
		 * launch from -6.93 into -2.68 against the original's -6.75. The over-reach is one offset out of
		 * three being wrong where the alternative is two; separating the two axes needs the catch to become
		 * a box rather than a radius, which is a change to the shared search and not yet measured well
		 * enough to justify.
		 */
		static constexpr float TriggerRadius = 16.0f;
		/**
		 * @brief Non-Reforged launch, as a fraction of the offset from the bumper's centre
		 *
		 * The whole non-Reforged model is `speed = (playerPos - bumperPos + 1) / 4`, assigned rather than
		 * added and independent of how fast the player arrived. It reproduces every hit measured to four
		 * decimal places - see the note where it is applied. What it replaced was a normalised radial
		 * impulse times a blind 15, which is the wrong *shape* as well as 35% weak: normalising throws away
		 * the distance, which is what the original actually scales with.
		 */
		static constexpr float LegacyImpulseScale = 0.25f;
		/**
		 * @brief Pixel added to the offset before scaling, on both axes
		 *
		 * Small and load-bearing: without it a drop through the exact centre is a fixed point and the player
		 * bounces on the spot for ever, which is what this engine did thirteen times in a row. With it the
		 * offset grows every bounce - the rule being linear in it - and the player is thrown clear after
		 * about eight, as in the original.
		 */
		static constexpr float LegacyImpulseBias = 1.0f;

		float _cooldown;
		float _lightIntensity;
		float _lightBrightness;
	};
}