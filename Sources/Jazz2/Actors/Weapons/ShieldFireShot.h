#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Fire shield (shot)
		
		Projectile emitted by the fire shield power-up. It flies straight and fast, casts a bright light, and
		dissipates on hitting a wall.
	*/
	class ShieldFireShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		ShieldFireShot();

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
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;

	private:
		Vector2f _gunspotPos;
		std::int32_t _fired;
	};
}