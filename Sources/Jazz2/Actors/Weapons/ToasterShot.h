#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Toaster (shot)
		
		Short-range flamethrower projectile that drifts slowly outward with randomized spread, is extinguished on
		contact with water, and normally dies when it hits a wall. The powered-up variant lives longer and slides
		along walls instead of being extinguished by them.
	*/
	class ToasterShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		ToasterShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Toaster;
		}

		bool ThawsFrozen() const override {
			return true;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		void OnRicochet() override;

	private:
		/** @brief Speed a flame loses along its main axis in px per tick squared */
		static constexpr float Damping = 1024.0f / 65536.0f;
		/** @brief Fastest a flame goes along its main axis in px per tick */
		static constexpr float MaxSpeed = 6.0f;
		/** @brief Ticks a flame follows the player's height after it starts */
		static constexpr float FollowTicks = 8.0f;
		/** @brief Most a flame follows the player's height away from where it started in px */
		static constexpr float MaxFollowOffset = 32.0f;
		/** @brief Ticks one frame of the flame's animation lasts */
		static constexpr float TicksPerAnimFrame = 8.0f;

		Vector2f _gunspotPos;
		float _crossAccel;
		float _followOffsetY;
		float _followLeft;
		float _hiddenLeft;
		std::uint8_t _fired;
		bool _vertical;
		bool _burning;
	};
}