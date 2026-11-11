#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Freezer (shot)
		
		Projectile of the freezer gun; it deals no direct damage but freezes any enemy it strikes into a block of ice
		for a fixed duration, and disappears on hitting a wall. The powered-up variant is faster and freezes for longer.
	*/
	class FreezerShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		FreezerShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Freezer;
		}

		/** @brief Makes the shot of the powered-up weapon wave around its path, @p phase is in 1/1024 of a turn */
		void SetWavePhase(float phase) {
			_wavePhase = phase;
			_waves = true;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;
		void OnRicochet() override;

	private:
		/** @brief Turns the wave of a powered-up shot advances per tick in 1/1024 of a turn */
		static constexpr float WaveStep = 64.0f;
		/** @brief Updates a shot is hidden for after it starts, normal and powered-up */
		static constexpr std::int32_t HiddenUpdates = 2;
		static constexpr std::int32_t HiddenUpdatesPoweredUp = 3;

		Vector2f _gunspotPos;
		float _particlesTime;
		float _wavePhase;
		std::uint8_t _fired;
		bool _waves;
	};
}