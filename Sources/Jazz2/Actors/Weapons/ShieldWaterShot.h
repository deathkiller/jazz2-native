#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Water shield (shot)
		
		Projectile emitted by the water shield power-up. It flies straight with a slight random spread in its firing
		angle and dissipates on hitting a wall.
	*/
	class ShieldWaterShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		ShieldWaterShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Blaster;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;

	private:
		/** @brief Upward push on a bubble in px per tick squared */
		static constexpr float Buoyancy = 2048.0f / 65536.0f;
		/** @brief Ticks after which a bubble near the player turns back */
		static constexpr float TurnBackTicks = 20.0f;
		/** @brief Distance in px within which a bubble turns back */
		static constexpr float TurnBackDistance = 32.0f;
		/** @brief Ticks one frame of the bubble's animation lasts */
		static constexpr float TicksPerAnimFrame = 8.0f;
		/** @brief Updates a bubble is hidden for after it starts */
		static constexpr std::int32_t HiddenUpdates = 7;

		Vector2f _gunspotPos;
		float _age = 0.0f;
		std::uint8_t _fired;
		bool _turnedBack = false;
	};
}