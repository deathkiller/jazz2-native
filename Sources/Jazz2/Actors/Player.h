#pragma once

#include "ActorBase.h"
#include "../LevelInitialization.h"
#include "../ShieldType.h"
#include "../SuspendType.h"
#include "../WarpFlags.h"

namespace Death::IO
{
	class Stream;
}

#if defined(WITH_ANGELSCRIPT)
namespace Jazz2::Scripting
{
	class ScriptPlayerWrapper;
}
namespace Jazz2::Scripting::Legacy
{
	class jjPLAYER;
}
#endif

#if defined(WITH_MULTIPLAYER)
namespace Jazz2::Multiplayer
{
	class MpLevelHandler;
}
#endif

namespace Jazz2::UI
{
	class HUD;
}

using namespace Death::IO;

namespace Jazz2::Actors
{
	namespace Environment
	{
		class Bird;
		class SwingingVine;
	}

	namespace Solid
	{
		class PinballBumper;
		class PinballPaddle;
	}

	namespace Weapons
	{
		class Thunderbolt;
	}

	/**
		@brief Represents a controllable player
		
		The player-controlled rabbit character (Jazz, Spaz or Lori in JJ2) that runs, jumps, fires weapons,
		collects items and takes damage. Each character has its own special move (e.g., buttstomp, uppercut or
		sidekick), can enter a temporary Sugar Rush and may be morphed into other forms such as the Frog.
	*/
	class Player : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

		friend class UI::HUD;
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::ScriptPlayerWrapper;
		friend class Scripting::Legacy::jjPLAYER;
#endif
#if defined(WITH_MULTIPLAYER)
		friend class Jazz2::Multiplayer::MpLevelHandler;
#endif
		friend class Environment::SwingingVine;
		friend class Solid::PinballBumper;
		friend class Solid::PinballPaddle;
		friend class Weapons::Thunderbolt;

	public:
		/** @brief Modifier */
		enum class Modifier : std::uint8_t {
			None,				/**< No modifier */
			Airboard,			/**< Riding an airboard */
			Copter,				/**< Using a copter */
			LizardCopter		/**< Using a lizard copter */
		};

		/** @brief Special move type */
		enum class SpecialMoveType : std::uint8_t {
			None,				/**< No special move */
			Buttstomp,			/**< Buttstomp */
			Uppercut,			/**< Uppercut */
			Sidekick			/**< Sidekick */
		};

		/** @brief Type of invulnerability */
		enum class InvulnerableType {
			Transient,			/**< Invulnerable without any visual effect */
			Blinking,			/**< Invulnerable with blinking effect */
			Shielded			/**< Invulnerable due to an active shield */
		};

		/** @brief Creates a new instance */
		Player();
		~Player();

		/** @brief Returns player index */
		std::uint8_t GetPlayerIndex() const {
			return (std::uint8_t)_playerIndex;
		}

		/** @brief Returns player type */
		PlayerType GetPlayerType() const {
			return _playerType;
		}

		/**
		 * @brief Returns the fur color to actually use for this player
		 *
		 * The configured color, or 0 = none when the "Apply Colors" preference disables recoloring in the current
		 * session (or for this player index).
		 */
		std::uint32_t GetEffectiveFurColor() const;
		/**
		 * @brief Returns this player's flat offset into the shared palette texture (for the palette-aware shader)
		 *
		 * Returns -1 if the player is not being recolored.
		 */
		std::int32_t GetPaletteOffset() const;

		/** @brief Returns current special move */
		SpecialMoveType GetSpecialMove() const {
			return _currentSpecialMove;
		}

		/** @brief Return weapon ammo */
		ArrayView<const std::uint16_t> GetWeaponAmmo() const {
			return _weaponAmmo;
		}

		/** @brief Returns weapon upgrades */
		ArrayView<const std::uint8_t> GetWeaponUpgrades() const {
			return _weaponUpgrades;
		}

		/** @brief Returns `true` if sugar rush is active */
		bool HasSugarRush() const {
			return (_sugarRushLeft > 0.0f);
		}

		/** @brief Returns `true` if the player can jump */
		bool CanJump() const;
		/** @brief Returns `true` if the player can bread solid objects */
		bool CanBreakSolidObjects() const;
		/** @brief Returns `true` if the player can move vertically, i.e. not affected by gravity */
		bool CanMoveVertically() const;
		/** @brief Returns `true` if continuous jump is allowed */
		virtual bool IsContinuousJumpAllowed() const;
		/** @brief Returns `true` if ledge climbing is allowed */
		virtual bool IsLedgeClimbAllowed() const;

		/** @brief Called when the level is about to change */
		virtual bool OnLevelChanging(Actors::ActorBase* initiator, ExitType exitType);
		/** @brief Called at the beginning of the next level to reveive carry over information */
		virtual void ReceiveLevelCarryOver(ExitType exitType, const PlayerCarryOver& carryOver);
		/** @brief Returns current carry over information */
		virtual PlayerCarryOver PrepareLevelCarryOver();
		/** @brief Initializes player state from a stream */
		void InitializeFromStream(ILevelHandler* levelHandler, Stream& src, std::uint16_t version);
		/** @brief Serializes player state to a stream */
		void SerializeResumableToStream(Stream& dest);

		/** @brief Respawns the player */
		virtual bool Respawn(Vector2f pos);
		/** @brief Warps to a given position */
		virtual void WarpToPosition(Vector2f pos, WarpFlags flags);
		/** @brief Warps to the last checkpoint */
		void WarpToCheckpoint();
		/** @brief Returns current modifier */
		Modifier GetModifier() const;
		/** @brief Sets current modifier */
		virtual bool SetModifier(Modifier modifier, const std::shared_ptr<ActorBase>& decor = nullptr);
		/** @brief Takes damage */
		virtual bool TakeDamage(std::int32_t amount, float pushForce = 0.0f, bool ignoreInvulnerable = false);
		/** @brief Freezes the player for specified time */
		virtual bool Freeze(float timeLeft);
		/** @brief Sets invulnerability */
		virtual void SetInvulnerability(float timeLeft, InvulnerableType type);

		/** @brief Returns score */
		std::int32_t GetScore() const;
		/** @brief Adds score */
		virtual void AddScore(std::int32_t amount);
		/** @brief Adds health */
		virtual bool AddHealth(std::int32_t amount);
		/** @brief Returns lives */
		std::int32_t GetLives() const;
		/** @brief Adds lives */
		virtual bool AddLives(std::int32_t count);
		/** @brief Returns coins */
		std::int32_t GetCoins() const;
		/** @brief Adds coins */
		void AddCoins(std::int32_t count);
		/** @brief Adds coins without notification (internal use only) */
		void AddCoinsInternal(std::int32_t count);
		/** @brief Returns gems */
		std::int32_t GetGems(std::uint8_t gemType) const;
		/** @brief Adds gems */
		void AddGems(std::uint8_t gemType, std::int32_t count);
		/** @brief Returns food eaten */
		std::int32_t GetConsumedFood() const;
		/** @brief Consumes food */
		void ConsumeFood(bool isDrinkable);
		/** @brief Activates sugar rush */
		void ActivateSugarRush(float duration);
		/** @brief Adds weapon ammo */
		virtual bool AddAmmo(WeaponType weaponType, std::int16_t count);
		/** @brief Adds weapon upgrade */
		virtual void AddWeaponUpgrade(WeaponType weaponType, std::uint8_t upgrade);
		/** @brief Adds fast fire */
		bool AddFastFire(std::int32_t count);
		/** @brief Morphs to a given player type */
		virtual bool MorphTo(PlayerType type);
		/** @brief Reverts morpth to the original player type */
		void MorphRevert();
		/** @brief Sets duration of dizziness */
		virtual bool SetDizzy(float timeLeft);

		/** @brief Returns active shield */
		ShieldType GetActiveShield() const {
			return _activeShield;
		}

		/** @brief Sets active shield */
		virtual bool SetShield(ShieldType shieldType, float timeLeft);
		/** @brief Increases active shield time */
		virtual bool IncreaseShieldTime(float timeLeft);

		/**
		 * @brief Draws the active shield decoration around a position
		 *
		 * Shared by the locally-controlled @ref Player and by remote players (rendered as
		 * @ref Actors::Multiplayer::RemoteActor on clients), so the fire/water/lightning shield bubble looks
		 * identical regardless of which side owns the player. Nothing is drawn for @ref ShieldType::None.
		 *
		 * @param renderQueue           Render queue the shield draw commands are added to
		 * @param shieldType            Active shield type
		 * @param shieldTime            Remaining shield time, in frames (drives the fade-in/out alpha and scale)
		 * @param metadata              Metadata holding the shield animations (the player's metadata)
		 * @param elapsedFrames         Elapsed level frames, used to animate the shield
		 * @param pos                   World position the shield is centered on
		 * @param baseLayer             Base render layer; the shield is drawn just behind and in front of it
		 * @param shieldRenderCommands  Render commands owned by the caller, reused across frames
		 */
		static void DrawShield(RenderQueue& renderQueue, ShieldType shieldType, float shieldTime, Metadata* metadata,
			float elapsedFrames, Vector2f pos, std::uint16_t baseLayer, std::unique_ptr<RenderCommand> (&shieldRenderCommands)[2]);
		/** @brief Spawns bird companion */
		bool SpawnBird(std::uint8_t type, Vector2f pos);
		/** @brief Disables controls for specified time */
		bool DisableControllable(float timeout);
		/** @brief Sets checkpoint */
		void SetCheckpoint(Vector2f pos, float ambientLight);

		/** @brief Returns carrying object */
		ActorBase* GetCarryingObject() const {
			return _carryingObject;
		}
		/** @brief Cancels carrying object */
		void CancelCarryingObject(ActorBase* expectedActor = nullptr);
		/** @brief Updates carrying object */
		void UpdateCarryingObject(ActorBase* actor, SuspendType suspendType = SuspendType::None);

		/** @brief Switches current weapon to a given index */
		void SwitchToWeaponByIndex(std::uint32_t weaponIndex);
		/** @brief Returns weapon fire point and angle */
		void GetFirePointAndAngle(Vector3i& initialPos, Vector2f& gunspotPos, float& angle);

	protected:
		/** @brief State of level exiting */
		enum class LevelExitingState {
			None,				/**< Not exiting */
			Waiting,			/**< Waiting before exiting */
			WaitingForWarp,		/**< Waiting for a warp to complete */
			Transition,			/**< Playing the exit transition */
			Ready				/**< Ready to exit */
		};

		/** @brief State of HUD weapon wheel */
		enum class WeaponWheelState {
			Hidden,				/**< Hidden */
			Opening,			/**< Opening */
			Visible,			/**< Visible */
			Closing				/**< Closing */
		};
		
		/** @brief Reason the current weapon was changed */
		enum class SetCurrentWeaponReason {
			Unknown,			/**< Unspecified */
			User,				/**< Set by the user */
			Rollback,			/**< Set due to rollback */
			AddAmmo,			/**< Set because an ammo for a new weapon was collected */
			AddUpgrade,			/**< Set because a new upgrade for a weapon was collected */
			Shield				/**< Set because a shield was activated */
		};

		/** @{ @name Constants */

		/** @brief Maximum horizontal speed while dashing */
		static constexpr float MaxDashingSpeed = 9.0f;
		/** @brief Maximum horizontal speed while running */
		static constexpr float MaxRunningSpeed = 4.0f;
		/** @brief Maximum speed while climbing a vine */
		static constexpr float MaxVineSpeed = 2.0f;
		/** @brief Maximum horizontal speed while dizzy */
		static constexpr float MaxDizzySpeed = 2.4f;
		/** @brief Maximum horizontal speed in shallow water */
		static constexpr float MaxShallowWaterSpeed = 3.6f;
		/** @brief Horizontal acceleration */
		static constexpr float Acceleration = 0.2f;
		/** @brief Horizontal deceleration */
		static constexpr float Deceleration = 0.22f;
		/**
		 * @brief Speed above which the player is no longer considered to be pushing
		 *
		 * While genuinely pushing, the player is held to a slow speed (a wall pins it to 0, a movable object to roughly
		 * @ref SolidObjectBase "PushSpeed" times 1.2). Once they break free and accelerate past this, the push animation
		 * ends even if the push grace timer (@ref _pushFramesLeft) hasn't expired, so it doesn't linger while walking
		 * in open space.
		 */
		static constexpr float MaxPushingSpeed = 1.0f;

		/** @brief Velocity scale that maps original JJ2 (70 Hz) per-tick values onto this engine's 60 Hz timeMult baseline */
		static constexpr float LegacyFrameRateScale = 70.0f / 60.0f;
		/** @brief Acceleration scale for the 70->60 Hz mapping, squared because position is the double integral of acceleration */
		static constexpr float LegacyFrameRateScaleSqr = LegacyFrameRateScale * LegacyFrameRateScale;
		/** @brief Non-Reforged ground max-speed scale */
		static constexpr float LegacyGroundSpeedScale = LegacyFrameRateScale * 1.05f;
		/** @brief Non-Reforged ground acceleration/braking scale */
		static constexpr float LegacyGroundAccelScale = 0.95f;
		/** @brief Non-Reforged coast factor when reversing at running speed - low so pressing the opposite way keeps momentum
			noticeably longer than just releasing the key (which stops quickly) */
		static constexpr float LegacyRunBrakeScale = 0.4f;
		/** @brief Non-Reforged braking factor at walking speed - gentler than before so the player coasts longer before stopping/turning */
		static constexpr float LegacyWalkBrakeScale = 0.7f;
		/** @brief Non-Reforged deceleration multiplier when the direction key is released - gentle so the player coasts to a stop */
		static constexpr float LegacyReleaseBrakeScale = 1.0f;
		/** @brief Non-Reforged vertical velocity scale */
		static constexpr float LegacyVerticalSpeedScale = LegacyFrameRateScale * 0.94f;
		/** @brief Non-Reforged rising-gravity scale; the vertical tune is squared so jump height is preserved */
		static constexpr float LegacyVerticalGravityScale = LegacyFrameRateScaleSqr * 0.94f * 0.94f;
		/** @brief Non-Reforged falling-gravity scale; the original drops a touch faster than the rise, so less slow-down here */
		static constexpr float LegacyFallGravityScale = LegacyFrameRateScaleSqr * 0.977f;
		/** @brief Non-Reforged applied upward-speed cap (original JJ2 clamps applied vertical movement to 8 px/tick) */
		static constexpr float LegacyRiseSpeedCap = 8.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged launch scale for vertical special moves (uppercut). */
		static constexpr float LegacySpecialMoveScale = LegacyVerticalSpeedScale * 1.10f;

		/** @brief Display names of all weapons */
		static constexpr const char* WeaponNames[(std::int32_t)WeaponType::Count] = {
			"Blaster",
			"Bouncer",
			"Freezer",
			"Seeker",
			"RF",
			"Toaster",
			"TNT",
			"Pepper",
			"Electro",
			"Thunderbolt"
		};

		/** @} */

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		std::int32_t _playerIndex;
		bool _isActivelyPushing, _wasActivelyPushing;
		// `true` only for the physics step in which the player is actually in contact with a wall or pushable object
		// (set in OnHitWall / OnPushSolidObject, cleared at the start of each PushSolidObjects). Lets the push animation
		// tell genuine pushing from the lingering grace timer (_pushFramesLeft).
		bool _pushContactThisFrame;
		bool _controllable;
		bool _controllableExternal;
		float _controllableTimeout;
		ExitType _lastExitType;

		bool _wasUpPressed, _wasDownPressed, _wasJumpPressed, _wasFirePressed, _isRunPressed;

		PlayerType _playerType, _playerTypeOriginal;
		SpecialMoveType _currentSpecialMove;
		bool _isAttachedToPole, _canPushFurther;
		float _copterFramesLeft, _fireFramesLeft, _pushFramesLeft, _waterCooldownLeft;
		LevelExitingState _levelExiting;
		bool _isFreefall, _inWater, _isLifting, _isSpring;
		std::int32_t _inShallowWater;
		Modifier _activeModifier;
		bool _inIdleTransition, _inLedgeTransition;
		bool _canDoubleJump;
		ActorBase* _carryingObject;
		// Whether this player is currently standing on top of another player (local splitscreen co-op stacking, and
		// online stacking when this is an `MpPlayer`); guards cancelling the carry so a real solid object is untouched
		bool _stackCarrying;
		// Whether another player is standing on top of this one without immobilizing it - drives the lift animation
		// cosmetically (online; local co-op uses `_isLifting` instead so movement is restricted, like a solid object)
		bool _beingStoodOn;
		float _externalForceCooldown;
		float _springCooldown;
		// Per-player recoloring: packed 4-byte fur color (one section per byte) and the allocated palette offset into
		// the shared palette texture (-1 = none). The renderer samples this palette via a per-instance offset.
		std::uint32_t _furColor;
		std::int32_t _paletteOffset;

		// Rebuilds the recolor palette in the player's palette slot and selects it on the renderer
		void RefreshColorPalette();
		// Releases the player's palette offset back to the shared pool (if any)
		void ReleasePaletteOffset();
#if defined(WITH_AUDIO)
		std::shared_ptr<AudioBufferPlayer> _copterSound;
#endif

		std::int32_t _lives, _coins, _coinsCheckpoint, _foodEaten, _foodEatenCheckpoint, _score;
		Vector2f _checkpointPos;
		float _checkpointLight;

		float _sugarRushLeft, _sugarRushStarsTime;
		float _shieldSpawnTime;
		std::int32_t _gems[4];
		std::int32_t _gemsCheckpoint[4];
		std::int32_t _gemsTotal[4];
		std::int32_t _gemsPitch;
		float _gemsTimer;
		float _bonusWarpTimer;

		SuspendType _suspendType;
		float _suspendTime;
		float _invulnerableTime;
		float _invulnerableBlinkTime;
		float _jumpTime;
		float _idleTime;
		float _hitFloorTime;
		float _keepRunningTime;
		float _lastPoleTime;
		Vector2i _lastPolePos;
		float _inTubeTime;
		float _dizzyTime;
		std::shared_ptr<Environment::Bird> _spawnedBird;
		std::shared_ptr<ActorBase> _activeModifierDecor;
		SmallVector<LightEmitter, 0> _trail;
		Vector2f _trailLastPos;
		ShieldType _activeShield;
		float _activeShieldTime;
		float _weaponFlareTime;
		std::int32_t _weaponFlareFrame;
		std::unique_ptr<RenderCommand> _weaponFlareCommand;
		std::unique_ptr<RenderCommand> _shieldRenderCommands[2];

		float _weaponCooldown;
		WeaponType _currentWeapon;
		bool _weaponAllowed;
		std::uint16_t _weaponAmmo[(std::int32_t)WeaponType::Count];
		std::uint16_t _weaponAmmoCheckpoint[(std::int32_t)WeaponType::Count];
		std::uint8_t _weaponUpgrades[(std::int32_t)WeaponType::Count];
		std::uint8_t _weaponUpgradesCheckpoint[(std::int32_t)WeaponType::Count];
		WeaponWheelState _weaponWheelState;
#if defined(WITH_AUDIO)
		std::shared_ptr<AudioBufferPlayer> _weaponSound;
#endif
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnTileDeactivated() override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

		bool OnHandleCollision(ActorBase* other) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		void OnHitWall(float timeMult) override;
		float GetGravityModifier(float baseGravity, bool isRising) const override;

		/**
		 * @brief Keeps this player and @p other from overlapping (so they can't pass through) and bumps them apart
		 *
		 * Equal-mass elastic separation along the axis of least penetration. Shared by local splitscreen co-op and
		 * online sessions (where @ref Multiplayer::PlayerOnServer adds the authoritative knockback resync on top).
		 *
		 * @param other            The other player in contact
		 * @param stackingEnabled  When `true`, vertical overlap is left to @ref UpdatePlayerStacking instead of bumped
		 * @return `true` if the players were separated (a bump was applied)
		 */
		bool ApplyPlayerBump(Player& other, bool stackingEnabled);
		/**
		 * @brief Resolves this player standing on top of another player as a one-way platform
		 *
		 * Carrying makes @ref CanJump() return `true` (so the player can jump off) and zeroes vertical speed so it
		 * rests instead of falling through. Call before the physics update so jump input sees it. Shared by local
		 * splitscreen co-op and online sessions.
		 *
		 * @param timeMult  Frame time multiplier
		 * @param snap      Whether to reposition our feet onto the player below (the side that simulates this player);
		 *                  `false` only grounds it (the server's shadow of a remote player, whose position comes from
		 *                  its client)
		 */
		void UpdatePlayerStacking(float timeMult, bool snap);

		/** @brief Reduces remaining shield time when a hit is absorbed (only if more than @p time remains) */
		virtual void DecreaseShieldTime(float time);

		/** @brief Called when a solid object is pushed */
		virtual void OnPushSolidObject(float timeMult, float pushSpeedX);
		/** @brief Called when a spring is hit */
		virtual void OnHitSpring(Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY, bool& removeSpecialMove);
		/** @brief Called when water should splash */
		virtual void OnWaterSplash(Vector2f pos, bool inwards);

		/** @brief Plays a sound effect for the player */
		std::shared_ptr<AudioBufferPlayer> PlayPlayerSfx(StringView identifier, float gain = 1.0f, float pitch = 1.0f);
		/** @brief Starts a player animation transition */
		bool SetPlayerTransition(AnimState state, bool cancellable, bool removeControl, SpecialMoveType specialMove, Function<void()>&& callback = {});
		/** @brief Returns `true` if the player should freefall */
		bool CanFreefall();
		/** @brief Ends active damaging move */
		void EndDamagingMove();

		/** @brief Fires currently equipped weapon */
		virtual bool FireCurrentWeapon(WeaponType weaponType);
		/** @brief Emits weapon flare after firing */
		virtual void EmitWeaponFlare();
		/** @brief Sets current weapon */
		virtual void SetCurrentWeapon(WeaponType weaponType, SetCurrentWeaponReason reason);

	private:
		static constexpr float ShieldDisabled = -1000000000000.0f;

		// Player-vs-player bump tuning for local splitscreen co-op; mirrors the server-side values in PlayerOnServer
		static constexpr float PlayerBumpMaxSeparationPerFrame = 4.0f;
		static constexpr float PlayerBumpRestitution = 0.5f;
		static constexpr float PlayerBumpMinSeparationSpeed = 5.0f;
		// How far the upper player sinks into the one it stands on, so a stack reads as connected instead of floating
		// on the exact hitbox edge
		static constexpr float PlayerStackSinkDepth = 4.0f;

		void UpdateAnimation(float timeMult);
		void PushSolidObjects(float timeMult);
		void CheckEndOfSpecialMoves(float timeMult);
		void CheckSuspendState(float timeMult);
		void OnUpdatePhysics(float timeMult);
		void OnUpdateTimers(float timeMult);
		void OnHandleMovement(float timeMult, bool areaWeaponAllowed, bool canJumpPrev);
		void OnHandleWater();
		void OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock);
		void OnHandleSpectate(float timeMult);
		void DoWarpOut(Vector2f pos, WarpFlags flags);
		void InitialPoleStage(bool horizontal);
		void NextPoleStage(bool horizontal, bool positive, std::int32_t stagesLeft, float lastSpeed);

		void OnPerishInner();

		void SwitchToNextWeapon();
		template<typename T, WeaponType weaponType>
		void FireWeapon(float cooldownBase, float cooldownUpgrade, bool emitFlare = false);
		void FireWeaponPepper();
		void FireWeaponRF();
		void FireWeaponTNT();
		bool FireWeaponThunderbolt();

		// Returns `true` if another player is currently standing on top of this one (so we should show the lift
		// animation). Searches the player list, which on a client only contains locally-simulated players.
		bool IsBeingStoodOnByPlayer() const;
	};
}