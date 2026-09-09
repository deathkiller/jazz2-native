#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Pinball paddle

		A flipper found in pinball-themed levels. It is **solid ground until the player asks it to fire**:
		standing or landing on one does nothing at all, and it kicks the player upward and away only while
		the jump key is held, awarding score. The launch strength depends on where along the paddle the
		player is standing, growing with the distance from the mounted end.
	*/
	class PinballPaddle : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		PinballPaddle();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;

	private:
		/**
		 * @brief How long the paddle keeps hold of the player as it swings (original 7 ticks)
		 *
		 * Measured on the original: from the tick the paddle is activated, `ys` reads a flat -4.0 for seven
		 * ticks while the position moves 0.38 px, and only then is the player flung. Control is taken for
		 * that long so the sucker-tube pose the paddle puts them in survives the launch instead of being
		 * replaced by the ordinary jump animation on the very next frame.
		 */
		static constexpr float PaddleHoldTime = 7.0f * (60.0f / 70.0f);
		/**
		 * @brief Where the mounted end sits relative to the paddle's own tile (px)
		 *
		 * Half a tile towards whichever side the paddle is bolted to - right for a right-facing one, left for
		 * the mirrored one. The launch is measured from here.
		 */
		static constexpr float LegacyMountOffset = 16.0f;
		/**
		 * @brief Distance from the mount at which the non-Reforged launch starts growing (px)
		 *
		 * The launch is `max(4, distance - 16)` of the original's units, one unit per pixel. Measured eight
		 * pixels at a time over both paddles: -7/-15/-23/-31/-39 at 24 to 56 px on the right one and
		 * -9/-17/-25/-33/-41 on the left, which is the same slope two units apart - a mirrored pivot landing
		 * on a different pixel. Sixteen splits that difference and is within one unit of either.
		 */
		static constexpr float LegacyLaunchOffset = 16.0f;
		/** @brief Weakest non-Reforged launch, close to the mounted end (original 4 px/tick) */
		static constexpr float LegacyMinLaunch = 4.0f;
		/**
		 * @brief Distance from the mount at which the sideways drift starts growing (px)
		 *
		 * The launch carries the player away from the mount at `(distance - 8) / 32` of the original's units,
		 * which is what walks them down the paddle so a held key pumps - see the note where it is applied.
		 * Measured -0.4688 to -1.4688 on the right paddle and +0.5313 to +1.5313 on the left, over 24 to
		 * 56 px out, so exactly a thirty-second of a pixel per pixel either side of this.
		 */
		static constexpr float LegacyDriftOffset = 8.0f;
		/** @brief How much sideways drift the non-Reforged launch adds per pixel from the mount */
		static constexpr float LegacyDriftScale = 1.0f / 32.0f;
		/** @brief Least sideways drift, close to the mounted end (original 0.5 px/tick) */
		static constexpr float LegacyMinDrift = 0.5f;
		/**
		 * @brief How steeply the surface drops away from the mounted end (px per px)
		 *
		 * Measured resting heights on the original, 16 to 56 px out from the mount: 571.2, 573.6, 576.1,
		 * 578.5, 580.9 and 583.3, which is a flat 0.3 per pixel, and 0.6 lower throughout on the mirrored
		 * paddle. At the mount itself the surface is the paddle's own centre.
		 */
		static constexpr float LegacySlope = 0.3f;
		/** @brief How far from the mount the surface still carries the player (px) - it does at 56, not at 64 */
		static constexpr float LegacyMaxDistance = 60.0f;
		/** @brief How far the paddle looks for a player to carry (px) */
		static constexpr float CatchRadius = 48.0f;
		/** @brief How far above the surface a falling player is caught (px) */
		static constexpr float CatchAbove = 4.0f;
		/**
		 * @brief How far below the surface a player is still lifted onto it (px)
		 *
		 * Wide enough for the original's from-below behaviour, which does not block the player at the
		 * underside but lifts them out onto the top: driven up into a paddle they appear on its surface in
		 * one tick, 27 px above where they were. Narrow enough that someone merely standing on the floor
		 * underneath is left alone - in the test chamber that floor is 36 px below the surface even at the
		 * paddle's far end, which is the tightest this gets.
		 */
		static constexpr float CatchBelow = 32.0f;
		/**
		 * @brief How far the solid surface reaches away from the mounted end, as a fraction of the sprite
		 *
		 * Derived rather than guessed, because the sprite's own width is not known here. The original catches
		 * the player 56 px from the mount and lets them fall past at 64, so the far edge has to sit between
		 * 40 and 48 px from the paddle's tile. At 0.7 this engine still caught them at 64 and dropped them at
		 * 72, which puts the sprite between 69 and 80 px wide - and 0.6 of *any* width in that range lands in
		 * the 40-48 window. Standing too far out and still being launched is the visible symptom.
		 */
		static constexpr float LengthAway = 0.6f;
		/** @brief How far the solid surface reaches past the mount, as a fraction of the sprite */
		static constexpr float LengthTowards = 0.3f;

		float _cooldown;
	};
}