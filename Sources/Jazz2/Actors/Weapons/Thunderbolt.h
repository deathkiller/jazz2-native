#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
	 * @brief Thunderbolt (shot)
	 *
	 * Lightning weapon that strikes instantly as a beam spanning from the gun muzzle to a far point, damaging
	 * everything along its length and passing through solid tiles. The bolt follows the player's aim until they
	 * stop firing or change facing, then fades out.
	 */
	class Thunderbolt : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Creates a new instance */
		Thunderbolt();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		bool OnHandleCollision(ActorBase* other) override;

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
		std::uint16_t _initialLayer;
		bool _firedUp;
	};
}