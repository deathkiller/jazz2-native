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
		float _searchTicks;
		bool _hasTarget;
		Vector2f _targetPos;
		/** @brief Farthest enemy a rocket goes for in px */
		static constexpr float TargetRange = 256.0f;
		/** @brief Length of the pull towards a target within about 180 px in px per tick, a seventh of it is the speed the rocket settles at */
		static constexpr float PullLength = 24.0f;
		/** @brief Distance in px from which the pull grows with the distance instead of staying the same */
		static constexpr float PullLinearDistance = 181.0f;
		/** @brief Share of the pull per px of distance beyond @ref PullLinearDistance */
		static constexpr float PullLinearFactor = 6.0f / 64.0f;
		/** @brief How many times the old speed counts less after a tick, per tick */
		static constexpr float SpeedKeep = 1.0f / 8.0f;

		void FollowNeareastEnemy(float timeMult);
		bool HasClearLine(Vector2f target) const;
	};
}