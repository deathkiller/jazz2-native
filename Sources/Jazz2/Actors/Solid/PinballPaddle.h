#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Pinball paddle

		A flipper found in pinball-themed levels. It is **solid ground until the player asks it to fire**:
		standing or landing on one does nothing at all, and it kicks the player upward and away only while
		the jump key is held. The launch strength depends on where along the paddle the player is standing,
		growing with the distance from the mounted end.
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
		/** @brief How long the player stays curled up after the paddle last caught them (original 10 ticks) */
		static constexpr float PaddleHoldTime = 10.0f * (60.0f / 70.0f);
		/** @brief Where the pivot sits from the paddle's middle, towards the side it's mounted on (px) */
		static constexpr float LegacyPivotOffset = 15.0f;
		/** @brief Weakest non-Reforged launch, at and behind the paddle's middle (original 4 px/tick) */
		static constexpr float LegacyMinLaunch = 4.0f;
		/** @brief How long the non-Reforged launch keeps its sideways drift unbraked, in the original's ticks */
		static constexpr float LegacyDriftHoldTicks = 4.0f;
		/** @brief How long the non-Reforged launch rises without gravity, in the original's ticks */
		static constexpr float LegacyRiseHoldTicks = 6.0f;
		/** @brief How far the solid surface reaches away from the mounted end, as a fraction of the sprite */
		static constexpr float LengthAway = 0.6f;
		/** @brief How far the solid surface reaches past the mount, as a fraction of the sprite */
		static constexpr float LengthTowards = 0.3f;

		// A frame of the swing, the catch area follows it
		struct LegacyFrame {
			float HotspotY;
			float Width;
			float Height;
		};

		static constexpr LegacyFrame LegacyFrames[] = {
			{ -8.0f, 53.0f, 18.0f },
			{ -8.0f, 53.0f, 15.0f },
			{ -17.0f, 52.0f, 24.0f },
			{ -8.0f, 53.0f, 15.0f }
		};

		float _cooldown;
	};
}