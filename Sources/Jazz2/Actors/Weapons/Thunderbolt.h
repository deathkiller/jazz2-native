#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	class Thunderbolt : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		Thunderbolt();

		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		WeaponType GetWeaponType() override {
			return WeaponType::Thunderbolt;
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		void OnAnimationFinished() override;
		void OnHitWall(float timeMult) override;
		void OnRicochet() override;

	private:
		bool _hit;
		float _lightProgress;
		Vector2f _farPoint;
		uint16_t _initialLayer;
		bool _firedUp;
	};
}