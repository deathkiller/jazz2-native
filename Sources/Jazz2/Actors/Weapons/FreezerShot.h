#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	class FreezerShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		FreezerShot();
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		WeaponType GetWeaponType() override {
			return WeaponType::Freezer;
		}

		float FrozenDuration() const {
			return ((_upgrades & 0x1) != 0 ? 280.0f : 180.0f);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;
		void OnRicochet() override;

	private:
		Vector2f _gunspotPos;
		int32_t _fired;
		float _particlesTime;
	};
}