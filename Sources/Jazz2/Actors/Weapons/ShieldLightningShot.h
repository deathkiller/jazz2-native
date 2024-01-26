#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	class ShieldLightningShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		ShieldLightningShot();
		~ShieldLightningShot();

		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Blaster;
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
		int32_t _fired;
		std::shared_ptr<AudioBufferPlayer> _noise;
	};
}