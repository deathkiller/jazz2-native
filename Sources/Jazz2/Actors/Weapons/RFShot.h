#pragma once

#include "ShotBase.h"
#include "../Player.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief RF (shot)
		
		Rocket-fuel (RF) missile that flies straight while trailing smoke and explodes on impact with a wall or after
		its short lifetime, pushing nearby players away with its blast. RF missiles are fired in a spread, and the
		powered-up variant produces a larger explosion.
	*/
	class RFShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		RFShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::RF;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;
		void OnRicochet() override;

	private:
		/**
		 * @brief Non-Reforged flight speed (original 3 px/tick)
		 *
		 * Measured off the tick the blast reaches the player, against the distance to the wall it is fired
		 * into: 0.6 px away it lands 9 ticks later, 11.7 px 13 ticks, 20.6 px 16 and 23.7 px 17. Those are
		 * one tick per 3 px plus a flat @ref LegacyBlastDelay, and the flat part is what the two shortest
		 * ranges share. Half the engine's own 6 px/frame, which is why an RF is a movement technique in the
		 * original and barely one here - the player has time to jump into their own blast.
		 */
		static constexpr float LegacyFlightSpeed = 3.0f * Player::LegacyFrameRateScale;
		/**
		 * @brief Ticks a non-Reforged shot waits between reaching a wall and its blast moving anyone (original 9)
		 *
		 * The constant part of the flight times above, and the whole of it point blank - fired against the
		 * wall the original still takes 9 ticks to throw the player. What it *is* was not established; a
		 * fuse and an explosion whose knockback lands some frames into its animation both produce this.
		 */
		static constexpr float LegacyBlastDelay = 9.0f / Player::LegacyFrameRateScale;
		/** @brief @ref _blastDelayLeft when the fuse has not been lit, so reaching a wall twice cannot restart it */
		static constexpr float BlastDelayUnarmed = -1.0f;

		Vector2f _gunspotPos;
		std::int32_t _fired;
		float _smokeTimer;
		/** @brief Counts @ref LegacyBlastDelay down once a wall has been reached; @ref BlastDelayUnarmed until then */
		float _blastDelayLeft;
	};
}