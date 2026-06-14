#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Seeker (shot)
		
		Homing missile that periodically searches for the nearest enemy within range and steers toward it, exploding on
		impact with a wall or after its lifetime and pushing nearby players away with the blast. The powered-up variant
		is stronger, longer-lived and re-targets more frequently.
	*/
	class SeekerShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		SeekerShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Seeker;
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
		float _defaultRecomputeTime;
		float _followRecomputeTime;

		void FollowNeareastEnemy(float timeMult);
	};
}