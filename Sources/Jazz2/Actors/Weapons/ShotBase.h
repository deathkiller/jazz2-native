#pragma once

#include "../ActorBase.h"
#include "../../WeaponType.h"

#include "../../../nCine/Base/TimeStamp.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Weapons
{
	/**
		@brief Base class of a shot from a player's weapon
		
		Common base for the weapon projectiles fired by the player, providing shared damage, ownership, lifetime,
		movement and ricochet handling.
	*/
	class ShotBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		ShotBase();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Returns strength (damage) */
		inline std::int32_t GetStrength() {
			return _strength;
		}
		/** @brief Sets strength (damage) */
		void SetStrength(std::int32_t value) {
			_strength = static_cast<std::uint16_t>(std::clamp<std::int32_t>(value, 0, UINT16_MAX));
		}

		/** @brief Returns owner of the shot */
		Player* GetOwner();
		/** @brief Returns weapon type */
		virtual WeaponType GetWeaponType();
		/** @brief Returns the weapon of the level that fired the shot, the same as @ref GetWeaponType() unless the level made its own */
		virtual WeaponType GetSourceWeapon();
		/** @brief Returns `true` if the shot is of the powered-up weapon */
		bool IsUpgraded() const {
			return ((_upgrades & 0x01) != 0);
		}
		/** @brief Triggers shot ricochet */
		void TriggerRicochet(ActorBase* other);
		/** @brief Returns how long the shot freezes what it hits, 0 if it doesn't */
		float GetFrozenDuration() const {
			return _frozenDuration;
		}
		/** @brief Sets how long the shot freezes what it hits, 0 if it doesn't */
		void SetFrozenDuration(float value) {
			_frozenDuration = value;
		}
		/** @brief Returns `true` if the shot thaws what's frozen */
		virtual bool ThawsFrozen() const;

		/** @brief Converts a speed in px per tick of the original to px per frame */
		static constexpr float TickSpeedScale = 70.0f / 60.0f;
		/** @brief Converts an acceleration in px per tick squared of the original to px per frame squared */
		static constexpr float TickAccelScale = TickSpeedScale * TickSpeedScale;
		/** @brief Converts a duration in ticks of the original to frames */
		static constexpr float TicksToFrames = 60.0f / 70.0f;

	protected:
		/** @brief Most speed of the player a shot carries, in px per tick */
		static constexpr float MaxCarriedSpeed = 8.0f;
		/** @brief Rate the carried speed of the player fades at, in px per tick squared */
		static constexpr float CarriedSpeedDecay = 0.125f;
		/** @brief Most shot ricochets off the tiles */
		static constexpr std::int32_t MaxTileRicochets = 8;
		/** @brief Range of the random vertical speed a ricochet adds, in px per tick */
		static constexpr float RicochetRandomSpeedY = 8.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		// Ordered by size, so there is no padding between them
		std::shared_ptr<ActorBase> _owner;
		ActorBase* _lastRicochet;
		float _timeLeft;
		float _carriedSpeedX;
		float _acceleration;
		Vector2f _accelerationDir;
		float _speedCap;
		float _timeRate;
		std::uint16_t _strength;
		std::uint8_t _ricochetCount;
		std::uint8_t _ricochetLimit;
		std::uint8_t _upgrades;
		bool _carriedDecays;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		/** @brief Called on shot ricochet */
		virtual void OnRicochet();
		/** @brief Ricochet as the weapons of the game do it, mirrors the horizontal speed and randomizes the vertical one */
		void OnRicochetExact();

		/** @brief Returns the distance the carried speed moves the shot by in a step and fades the speed, for shots that don't use @ref TryMovement() */
		float AdvanceCarriedSpeed(float timeMult);
		/** @brief Makes the shot live a third shorter if it flies straight up, as the original does */
		void ShortenUpShotLifetime(float sinAngle);
		/** @brief Shows a shot that starts hidden inside the player once it is past the muzzle */
		void RevealPastMuzzle(Vector2f gunspotPos);
		/** @brief Sets the speed of the player the shot carries along, clamped, optionally fading out */
		void SetCarriedSpeed(float playerSpeedX, bool decays);
		/** @brief Makes the shot speed up along a direction, @p speedCap limits its own speed if positive */
		void SetAcceleration(Vector2f direction, float acceleration, float speedCap);

		void TryMovement(float timeMult, Tiles::TileCollisionParams& params);
		/**
		 * @brief Returns how many sub-steps a shot's movement should be split into
		 *
		 * @ref TryMovement() only tests the destination of a move, so no single step may be long enough to
		 * pass through thin geometry. The count therefore comes from the distance the shot actually covers
		 * this frame, bounded against @ref MaxMovementStep exactly as @ref ActorBase::TryStandardMovement()
		 * bounds it, which makes it independent of both the frame rate and the shot's speed.
		 */
		std::int32_t GetMovementSubstepCount(float timeMult) const;

	private:
		float _frozenDuration;
		TimeStamp _lastRicochetTime;
	};
}