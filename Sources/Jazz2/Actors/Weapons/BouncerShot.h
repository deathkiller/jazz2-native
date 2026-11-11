#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Bouncer (shot)
		
		Elastic projectile that ricochets off walls, floors and ceilings while affected by gravity, bouncing
		around until its lifetime expires or it has bounced too many times. The powered-up variant lives longer
		and gradually steers its horizontal speed toward a target velocity.
	*/
	class BouncerShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		BouncerShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Bouncer;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		void OnRicochet() override;
		float GetGravityModifier(float baseGravity, bool isRising) const override;

	private:
		/** @brief Speeds a bounce up along the horizontal direction in px per tick squared */
		static constexpr float Acceleration = 0.25f;
		/** @brief Constant downward acceleration on top of gravity in px per tick squared */
		static constexpr float ConstantFall = 6000.0f / 65536.0f;
		/** @brief Gravity of the shot in px per tick squared */
		static constexpr float Gravity = 0.125f;
		/** @brief Bounces after which the shot is spent, normal and powered-up */
		static constexpr std::int32_t MaxBounces = 16;
		static constexpr std::int32_t MaxBouncesPoweredUp = 32;

		void OnBounce();

		Vector2f _gunspotPos;
		std::uint8_t _fired;
		std::uint8_t _bounces;
	};
}