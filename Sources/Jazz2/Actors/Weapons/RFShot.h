#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
	 * @brief RF (shot)
	 *
	 * Rocket-fuel (RF) missile that flies straight while trailing smoke and explodes on impact with a wall or after
	 * its short lifetime, pushing nearby players away with its blast. RF missiles are fired in a spread, and the
	 * powered-up variant produces a larger explosion.
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
		Vector2f _gunspotPos;
		std::int32_t _fired;
		float _smokeTimer;
	};
}