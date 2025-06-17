#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/** @brief Toaster (shot) */
	class ToasterShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
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