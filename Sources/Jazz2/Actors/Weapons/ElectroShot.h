#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Electro (shot)
		
		Projectile of the electro blaster, a high-strength energy ball that passes through solid tiles instead of
		stopping at walls and trails an expanding swirl of electric particles as it travels. The powered-up variant
		is slightly faster.
	*/
	class ElectroShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/**
			@brief Animation state of the powered-up variant

			The shot is invisible (it renders only through locally-spawned particles), so its animation state is
			reused to carry the variant to remote clients over the existing animation synchronization.
		*/
		static constexpr AnimState PoweredUpAnimState = (AnimState)2;

		/** @brief Creates a new instance */
		ElectroShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		/** @brief Spawns one ring of swirl particles, shared with the remote representation of the shot */
		static void CreateParticles(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos, std::uint16_t layer, float currentStep, bool facingLeft, bool poweredUp);
		/** @brief Emits the light surrounding the swirl, shared with the remote representation of the shot */
		static void EmitParticleLight(SmallVectorImpl<LightEmitter>& lights, Vector2f pos, float currentStep);

		WeaponType GetWeaponType() override {
			return WeaponType::Electro;
		}

		bool OnHandleCollision(ActorBase* other) override;

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
		std::int32_t _fired;
		float _currentStep;
		float _particleSpawnTime;
	};
}