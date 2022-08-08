#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	class BouncerShot : public ShotBase
	{
	public:
		BouncerShot();
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Bouncer;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall() override;
		void OnHitFloor() override;
		void OnHitCeiling() override;
		void OnRicochet() override;

	private:
		Vector2f _gunspotPos;
		bool _fired;
		float _targetSpeedX;
		float _hitLimit;
	};
}