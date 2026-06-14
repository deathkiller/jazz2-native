#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
	 * @brief Water shield (shot)
	 *
	 * Projectile emitted by the water shield power-up. It flies straight with a slight random spread in its firing
	 * angle and dissipates on hitting a wall.
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
		Vector2f _gunspotPos;
		std::int32_t _fired;
	};
}