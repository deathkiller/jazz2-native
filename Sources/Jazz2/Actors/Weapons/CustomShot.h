#pragma once

#include "ShotBase.h"
#include "../../WeaponDefinition.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Shot of a custom weapon of the level

		Flies, bounces, homes in, pierces and goes off as the level describes its weapon (see @ref WeaponShotOptions).
		A shot of @ref ShotLook::Beam doesn't fly, it reaches from the muzzle as far as it would fly in its time.
		The parameters it's activated with are the upgrades, the @ref WeaponType of the weapon and flags: @ref PlaysSound,
		@ref IsBubble.
	*/
	class CustomShot : public ShotBase
	{
		DEATH_RUNTIME_OBJECT(ShotBase);

	public:
		/** @brief Flag of the parameters, the shot plays the sound of firing */
		static constexpr std::uint8_t PlaysSound = 0x01;
		/** @brief Flag of the parameters, the shot is an air bubble in place of a shot under water (see @ref WeaponOptions::ReplacedByBubbles) */
		static constexpr std::uint8_t IsBubble = 0x02;

		/** @brief Creates a new instance */
		CustomShot();

		/** @brief Called when the shot is fired */
		void OnFire(const std::shared_ptr<ActorBase>& owner, Vector2f gunspotPos, Vector2f speed, float angle, bool isFacingLeft);

		bool OnHandleCollision(ActorBase* other) override;
		/** @brief Returns the weapon of the game it's made from, @ref GetSourceWeapon() is the weapon of the level that fired it */
		WeaponType GetWeaponType() override;
		WeaponType GetSourceWeapon() override;
		float GetFrozenDuration() const override;
		bool ThawsFrozen() const override;

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		bool OnPerish(ActorBase* collider) override;
		void OnHitWall(float timeMult) override;
		void OnRicochet() override;

	private:
		static constexpr std::int32_t MaxRememberedHits = 16;

		WeaponType _slot;
		WeaponType _basedOn;
		WeaponShotOptions _options;
		Vector2f _gunspotPos;
		std::int32_t _fired;
		std::int32_t _pierceLeft;
		std::int32_t _bouncesLeft;
		std::int32_t _baseStrength;
		bool _detonated;
		bool _bubbleInWater;
		bool _flipX;
		// Frames since it was fired and since the last sparks, see ShotLook::Electro
		float _lookStep;
		float _particleSpawnTime;
		// The end of a beam, see ShotLook::Beam
		Vector2f _farPoint;
		// Identity only, never dereferenced, an enemy is hit once while the shot passes through it
		SmallVector<const ActorBase*, MaxRememberedHits> _hitActors;

		void TurnTo(bool left);
		void Move(float timeMult, Tiles::TileCollisionParams& params);
		void UpdateBeam(float timeMult);
		Vector2f GetBeamEnd(Vector2f from, float angle) const;
		void FollowNearestEnemy(float timeMult);
		bool IsOverlappingHitActor();
		void Detonate();
		void TurnIntoBubble();
	};
}
