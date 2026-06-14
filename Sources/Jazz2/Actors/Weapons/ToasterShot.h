#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
	 * @brief Toaster (shot)
	 *
	 * Short-range flamethrower projectile that drifts slowly outward with randomized spread, is extinguished on
	 * contact with water, and normally dies when it hits a wall. The powered-up variant lives longer and slides
	 * along walls instead of being extinguished by them.
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

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		void OnRicochet() override;

	private:
		Vector2f _gunspotPos;
		std::int32_t _fired;
	};
}