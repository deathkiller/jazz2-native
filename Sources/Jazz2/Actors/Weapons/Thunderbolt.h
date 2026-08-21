#pragma once

#include "ShotBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Thunderbolt (shot)
		
		Lightning weapon that strikes instantly as a beam spanning from the gun muzzle to a far point, damaging
		everything along its length and passing through solid tiles. The bolt follows the player's aim until they
		stop firing or change facing, then fades out.
	*/
	class Thunderbolt : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief How far the beam extends from the muzzle */
		static constexpr float BeamDistance = 140.0f;
		/** @brief How fast the beam lifetime progress advances per frame */
		static constexpr float LightProgressSpeed = 0.123f;
		/**
			@brief Offset added to the animation state of the first bolt of a burst

			The bolt respawns every few frames while the trigger is held, but the muzzle flash should play only
			when the player starts shooting. The first bolt uses states 3/4 (aliases of the same beam graphics),
			carrying the distinction to remote clients over the existing animation synchronization.
		*/
		static constexpr std::uint32_t InitialShotAnimOffset = 3;

		/** @brief Creates a new instance */
		Thunderbolt();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		/** @brief Occasionally spawns a spark discharging off the beam, shared with the remote representation */
		static void CreateSparks(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos, Vector2f farPoint, std::uint16_t layer, float timeMult);
		/** @brief Emits the beam lights, with a brief intense muzzle flash on the first bolt of a burst; shared with the remote representation */
		static void EmitBeamLights(SmallVectorImpl<LightEmitter>& lights, Vector2f pos, Vector2f farPoint, float lightProgress, bool muzzleFlash);

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
		bool _initialShot;
	};
}