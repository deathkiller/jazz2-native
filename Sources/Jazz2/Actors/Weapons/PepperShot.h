﻿#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/** @brief Pepper (shot) */
	class PepperShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		PepperShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Pepper;
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
	};
}