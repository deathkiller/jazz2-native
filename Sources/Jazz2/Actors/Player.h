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
#if defined(WITH_PHYSICS_PROBE)
		// Reports the state that gates the special moves, so a move that did not happen can be told apart
		// from one that happened differently (see Tests/PhysicsProbe.cpp)
		friend class Jazz2::Tests::PhysicsProbe;
#endif
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

		/**
			@brief How much of an effect a @ref InvulnerableType shows, for comparing two of them

			@ref GrantInvulnerability() will not replace a running effect with one that ranks lower. The
			order is the declaration order, but spelled out here because it is a rule rather than an
			accident of it.
		*/
		static constexpr std::int32_t GetInvulnerableStrength(InvulnerableType type) {
			return (type == InvulnerableType::Shielded ? 2 : (type == InvulnerableType::Blinking ? 1 : 0));
		}

		/** @brief Type of copter flight state */
		enum class FlightType {
			Normal,				/**< Timed/regular flight */
			Cheat				/**< Cheat-enabled flight */
		};

		/**
			@brief Constant per-character properties

			Character-specific constants shared by all players of the same @ref PlayerType, so simple character
			differences live in one table instead of being scattered across switches. Behavioral differences
			(special moves, double jump, copter) remain in code.
		*/
		struct CharacterTraits {
			/** @brief Path to the metadata with the character's animations and sounds */
			StringView Metadata;
			/** @brief Vertical scale of the weapon flare effect */
			float WeaponFlareScaleY;
			/** @brief Number of bored idle animations the character can play */
			std::int32_t IdleBoredAnimCount;
		};

		/** @brief Returns constant per-character properties for a given player type */
		static const CharacterTraits& GetCharacterTraits(PlayerType type);

		/**
			@brief Collected items and weapon loadout of a player

			Grouped into one value struct so the checkpoint snapshot and its rollback are single assignments
			instead of a hand-maintained set of parallel fields.
		*/
		/** @brief Ammo value that means unlimited (blaster) */
		static constexpr std::uint32_t AmmoUnlimited = UINT32_MAX;

		struct InventoryState {
			/** @brief Number of collected coins */
			std::int32_t Coins;
			/** @brief Amount of eaten food (drives sugar rush) */
			std::int32_t FoodEaten;
			/** @brief Number of collected gems, by gem type */
			std::int32_t Gems[4];
			/** @brief Remaining weapon ammo (in 1/256 units), by weapon type; `AmmoUnlimited` means unlimited */
			std::uint32_t WeaponAmmo[(std::int32_t)WeaponType::Count];
			/** @brief Weapon upgrade flags, by weapon type */
			std::uint8_t WeaponUpgrades[(std::int32_t)WeaponType::Count];
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
		ArrayView<const std::uint32_t> GetWeaponAmmo() const {
			return _inventory.WeaponAmmo;
		}

		/** @brief Returns current ammo limit per weapon (in whole units) */
		std::int32_t GetAmmoLimit() const;

		/** @brief Converts ammo to the 16-bit representation used by multiplayer packets */
		static std::uint16_t AmmoToWire(std::uint32_t ammo) {
			return (ammo == AmmoUnlimited ? UINT16_MAX : (std::uint16_t)std::min(ammo, (std::uint32_t)(UINT16_MAX - 1)));
		}

		/** @brief Returns weapon upgrades */
		ArrayView<const std::uint8_t> GetWeaponUpgrades() const {
			return _inventory.WeaponUpgrades;
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
		/** @brief Returns `true` if the player is currently in water */
		bool IsInWater() const;
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
		/** @brief Returns whether fly-cheat behavior is currently active */
		bool IsFlyCheatActive() const;
		/** @brief Sets copter flight duration and type (`timeLeft <= 0` disables flight cheat state) */
		void SetCopterFlight(float timeLeft, FlightType type = FlightType::Normal);
		/** @brief Enables or disables fly cheat behavior, enabling it applies to the next cheat flight activation */
		virtual void EnableFlyCheat(bool active);
		/** @brief Takes damage */
		virtual bool TakeDamage(std::int32_t amount, float pushForce = 0.0f, bool ignoreInvulnerable = false);
		/** @brief Freezes the player for specified time */
		virtual bool Freeze(float timeLeft);
		/**
			@brief Sets invulnerability to exactly @p timeLeft of @p type, replacing whatever is running

			The unconditional setter, and the one a server's authoritative state is applied through - so it
			has to be able to LOWER the value and to swap a stronger effect for a weaker one, otherwise a
			correction can never arrive. @cpp timeLeft <= 0 @ce clears it.

			Gameplay wants @ref GrantInvulnerability() instead: picking something up should not be able to
			cut short an invulnerability that is already running.
		*/
		virtual void SetInvulnerability(float timeLeft, InvulnerableType type);
		/**
			@brief Grants invulnerability without ever weakening what is already running

			Keeps whichever of the two is longer and whichever effect is stronger
			@m_span{m-text m-dim} (@ref InvulnerableType::Shielded > @ref InvulnerableType::Blinking >
			@ref InvulnerableType::Transient) @m_endspan, then applies the result through
			@ref SetInvulnerability(). This is what every pickup, hurt and enemy bounce goes through; a
			grant of nothing does nothing.
		*/
		void GrantInvulnerability(float timeLeft, InvulnerableType type);
		/** @brief Returns the effect the currently running invulnerability is showing */
		InvulnerableType GetInvulnerableType() const;

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
		/** @brief Deactivates sugar rush immediately and restores the default renderer */
		void DeactivateSugarRush();
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
		/**
		 * @brief Throws the player away from a shot's blast
		 *
		 * Only the RF does this, and only in non-Reforged mode, where it is a movement technique rather than
		 * an accident - see @ref LegacyRFBlastSpeed for the measurements. Reforged keeps the small external
		 * force it always had, at whatever range the caller's own search allowed.
		 *
		 * @param pushLeft      Whether the blast is to the player's right, so they are thrown left
		 * @param distanceSqr   Squared distance from the blast, tested against the measured reach
		 * @return              `false` if the blast was too far away to move the player
		 */
		bool ApplyBlastKnockback(bool pushLeft, float distanceSqr);

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
		/** @brief Returns the ambient light intensity currently applied to this player */
		float GetCurrentAmbientLight() const {
			return _currentAmbientLight;
		}
		/** @brief Records the ambient light intensity currently applied to this player, called by the level handler */
		void SetCurrentAmbientLight(float value) {
			_currentAmbientLight = value;
		}
		/**
		 * @brief Overrides the ambient light intensity to restore when respawning at the checkpoint
		 *
		 * Used in online sessions, where checkpoints are activated by the server and the owning client would
		 * otherwise fall back to the level default after dying (see @ref SetCheckpoint).
		 */
		void SetCheckpointAmbientLight(float ambientLight) {
			_checkpointLight = ambientLight;
		}

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

	public:
		/**
		 * @{ @name Non-Reforged movement constants
		 *
		 * Measured off the original game rather than guessed: the probe in `_pt.j2as` drives the player through
		 * a fixed matrix of scripted input and logs its position and speed every tick, in both games, so these
		 * can be compared run for run. The original ticks at a measured 70.02 Hz and works in 1/65536 px units,
		 * which is why its values come out as clean fractions of 65536.
		 *
		 * Everything below is the original's own per-tick figure times a conversion to this engine's 60 Hz
		 * timeMult baseline: velocities by @ref LegacyFrameRateScale, accelerations by its square, because
		 * position is the double integral of acceleration. Scaling both exactly preserves jump height *and*
		 * real-time duration at once, so no further tuning factor belongs here.
		 */

		/**
		 * @brief Tick rate of the original game
		 *
		 * Measured at 70.021 Hz, which is what @cpp Tests/Tools/CompareTraces.ps1 @ce converts the original's
		 * logged speeds with; a round 70.0 is used here, so every comparison against a reference trace carries
		 * a systematic 0.03% offset. That is an order of magnitude below the per-scenario accuracy the
		 * reference page reports, so it has been left alone deliberately rather than re-tuning every constant
		 * below --- but the two figures are one measurement and should not drift further apart.
		 */
		static constexpr float LegacyTickRate = 70.0f;
		/** @brief Velocity scale that maps original per-tick values (px/tick at 70 Hz) onto this engine's 60 Hz timeMult baseline */
		static constexpr float LegacyFrameRateScale = LegacyTickRate / 60.0f;
		/** @brief Acceleration scale for the same mapping, squared because position is the double integral of acceleration */
		static constexpr float LegacyFrameRateScaleSqr = LegacyFrameRateScale * LegacyFrameRateScale;

		/** @brief Non-Reforged walk speed cap (original 4 px/tick) */
		static constexpr float LegacyWalkSpeed = 4.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged dash speed cap while Run is held (original 16 px/tick) */
		static constexpr float LegacyDashSpeed = 16.0f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged horizontal acceleration with no Run (original 12000/65536 px/tick^2)
		 *
		 * The original applies this in the direction of the *input*, whichever way the player happens to be
		 * moving and whether it is on the ground or in the air - one table for everything. There is no skid
		 * factor, no separate airborne rate and no loss of momentum when the direction is flipped, which is
		 * what makes turning around symmetric and, in the air, slow enough to be a commitment.
		 */
		static constexpr float LegacyWalkAccel = (12000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged horizontal acceleration while Run is held (original 24000/65536 px/tick^2) */
		static constexpr float LegacyDashAccel = (24000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged deceleration with no direction held (original 8000/65536 px/tick^2) */
		static constexpr float LegacyWalkDecel = (8000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged deceleration with no direction held while the dash is still active (original 28000/65536 px/tick^2) */
		static constexpr float LegacyDashDecel = (28000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged deceleration on a slide tile of strength 0, no direction held (original 4000/65536)
		 *
		 * A `MODIFIER_SLIDE` tile does not add anything to the movement; it swaps the brake for a weaker one,
		 * so the player coasts further. At strength 0 this is exactly **half** of @ref LegacyWalkDecel, but
		 * the dash pair is not halved - 12000 against 28000, a ratio of 3:7 - so the two are measured
		 * separately rather than one factor being applied to both.
		 */
		static constexpr float LegacySlideWalkDecel = (4000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged deceleration on a strength-0 slide while the dash is active (original 12000/65536) */
		static constexpr float LegacySlideDashDecel = (12000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief How much each step of a slide tile's Strength takes off @ref LegacySlideWalkDecel (orig 800/65536)
		 *
		 * The tile carries a 2-bit Strength, and all four values are linear in it. Measured across the whole
		 * range: the walk brake runs 4000, 3200, 2400, 1600 and the dash brake 12000, 9500, 7000, 4500, so
		 * the steps are 800 and 2500. Worth measuring all four rather than two - the ratios between strength
		 * 0 and 3 are 2.5 and 2.67, which fits no single divisor and would have been fitted wrongly.
		 */
		static constexpr float LegacySlideWalkDecelStep = (800.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief The same step for @ref LegacySlideDashDecel (original 2500/65536 px/tick^2 per strength) */
		static constexpr float LegacySlideDashDecelStep = (2500.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief How long a non-Reforged sucker tube keeps hold of the player (original 16 ticks)
		 *
		 * A duration, so it divides by @ref LegacyFrameRateScale rather than multiplying. Re-armed every tick
		 * the player is inside a tube tile, which is what makes the total depend on the tube's own speed: a
		 * tube set to 8 px/tick keeps the player in its 32 px tile for four ticks and so holds for 20, while
		 * one set to 20 keeps them for two and holds for 18. That difference is exactly the two tiles' worth
		 * of re-arming, which is how the window was separated from the exit condition - a single tube cannot
		 * tell them apart, and a 30-tile row of them carries the player the whole way in both games.
		 *
		 * The window suppresses horizontal friction but *not* gravity: a tube firing straight up holds its
		 * speed only while the player is still in the tile, and the ascent decays normally from there.
		 */
		static constexpr float LegacyTubeControlTime = 16.0f / LegacyFrameRateScale;
		/**
		 * @brief Ticks the non-Reforged dash outlives the Run key (original 16 ticks)
		 *
		 * The dash is a timed state rather than just a higher cap. Letting go of Run does not bleed the speed
		 * off at all - the player keeps the full @ref LegacyDashSpeed for this long and then the speed is
		 * clamped straight back to @ref LegacyWalkSpeed in a single tick, in mid-air exactly as on the ground.
		 */
		static constexpr float LegacyDashGraceTicks = 16.0f / LegacyFrameRateScale;

		/** @brief Non-Reforged jump launch speed from a standstill (original 10 px/tick) */
		static constexpr float LegacyJumpSpeed = 10.0f * LegacyFrameRateScale;
		/**
		 * @brief Extra non-Reforged launch speed per unit of horizontal speed (original exactly 1/4)
		 *
		 * The original's launch is `-(10 + |xSpeed| / 4)` with no threshold and no dead zone, so a dashing jump
		 * leaves the ground at -14 against a standing jump's -10. The factor needs no frame-rate conversion of
		 * its own, both sides of it being velocities.
		 */
		static constexpr float LegacySpeedJumpScale = 0.25f;
		/** @brief Non-Reforged rise gravity while the jump key is held (original 0.375 px/tick^2) */
		static constexpr float LegacyRiseGravity = 0.375f * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged rise gravity once the jump key is released (original 0.875 px/tick^2)
		 *
		 * Releasing jump early shortens the jump by making the rest of the *rise* heavier, not by clamping the
		 * speed - the original switches to this from the tick the key comes up and keeps it for the rest of the
		 * ascent, which is what gives the tap hop its own arc rather than a truncated one.
		 *
		 * It eases off for the last part of that ascent - see @ref LegacyRiseGravityReleasedNearApex.
		 */
		static constexpr float LegacyRiseGravityReleased = 0.875f * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged rise gravity once the jump key is released and the rise is nearly spent
		 *        (original 0.625 px/tick^2)
		 *
		 * The released rise is not one rate: it decelerates at @ref LegacyRiseGravityReleased while the rise
		 * is still faster than @ref LegacyRiseBrakeEaseSpeed and at this lighter rate below it. Measured on a
		 * sweep of thirty standing jumps released one tick apart (`ap_r01`..`ap_r30`): the step is 0.875 at
		 * `ys` = -1.125 and 0.625 at -1.000, on either side of a threshold of exactly one pixel per tick.
		 *
		 * The held rise has no such band - it is a flat 0.375 from the launch to the apex.
		 */
		static constexpr float LegacyRiseGravityReleasedNearApex = 0.625f * LegacyFrameRateScaleSqr;
		/** @brief Rise speed below which the non-Reforged released rise eases off (original 1 px/tick) */
		static constexpr float LegacyRiseBrakeEaseSpeed = 1.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged fall gravity (original 0.125 px/tick^2) */
		static constexpr float LegacyFallGravity = 0.125f * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged underwater gravity (original 1024/65536 px/tick^2 - a very slow sink) */
		static constexpr float LegacyWaterGravity = (1024.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged cap on applied movement (the original clamps it to 8 px/tick on both axes)
		 *
		 * Measured: a dash reports an xSpeed of 16 px/tick but only ever travels 8, so the speed above the cap
		 * is momentum rather than motion. It still counts wherever the speed itself is read - most visibly in
		 * the jump launch boost, which is what makes a dashing jump launch at -14 while moving no faster than a
		 * capped run.
		 */
		static constexpr float LegacyAppliedSpeedCap = 8.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged applied upward-speed cap - the same limit, on the vertical axis */
		static constexpr float LegacyRiseSpeedCap = LegacyAppliedSpeedCap;
		/**
		 * @brief Non-Reforged ceiling on internal speed, on either axis (original 32 px/tick)
		 *
		 * Read off a pole launch in the original: it assigns 40.125 and the very next tick reports exactly
		 * 32.00. Applied movement is capped at @ref LegacyRiseSpeedCap regardless, so this never changes how
		 * fast the player visibly moves - only how long a launch keeps riding that cap, and therefore how
		 * far it gets. It happens to equal a blue spring's own launch exactly, so a spring passes through
		 * untouched while a pole is clamped, which is what the original does to each.
		 *
		 * The original applies the same figure horizontally, and so does @ref OnUpdatePhysics(): a spring
		 * carries the identical strength on either axis, so anything lower here silently flattens the
		 * horizontal colours into one another. Only a sidekick is assigned above it.
		 */
		static constexpr float LegacyVerticalSpeedLimit = 32.0f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged bounce off an enemy hit by a buttstomp (original 13 px/tick)
		 *
		 * Measured against the test level's own tube turtle: the stomp arrives at its governed 10 px/tick and
		 * leaves at a flat **13**, so the bounce is *faster* than the fall that caused it and no fraction of
		 * the impact could ever produce it. The ascent that follows decays at the released rise rate rather
		 * than the held one, like a pole launch and unlike a spring - see @ref GetGravityModifier().
		 */
		static constexpr float LegacyEnemyStompBounce = 13.0f * LegacyFrameRateScale;
		/**
		 * @brief How far below a ledge's surface the non-Reforged player may arrive and still be put on top of it
		 *
		 * Measured by running at gaps of one to six tiles: the original clears a two-tile walk (arriving
		 * 6.25 px below the far edge) and a five-tile dash (18.1 px below), and fails a three-tile walk
		 * (20.25 px) and a six-tile dash (27.6 px) - so the allowance sits between 18.1 and 20.25 px, five
		 * eighths of a tile. Fed to @cpp ActorBase::_landingTolerance @ce; without it the player lands a whole
		 * gap short of the original at both speeds.
		 */
		static constexpr float LegacyLandingTolerance = 20.0f;
		/** @brief Non-Reforged terminal fall speed (original 12 px/tick) */
		static constexpr float LegacyFallSpeedCap = 12.0f * LegacyFrameRateScale;

		/** @brief Non-Reforged buttstomp descent speed (original 10 px/tick, the same for all three characters) */
		static constexpr float LegacyButtstompSpeed = 10.0f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged sideways drift speed during a buttstomp (original 12000/65536 px/tick, doubled with Run)
		 *
		 * A flat rate rather than an acceleration, applied through both phases of the move - the pause at the
		 * top and the descent alike. In the original this bypasses the player's speed entirely (its xSpeed
		 * reads zero for the whole stomp while the position still creeps sideways), which is worth knowing if
		 * this is ever measured again: watch the position, not the speed.
		 */
		static constexpr float LegacyButtstompDriftSpeed = (12000.0f / 65536.0f) * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged speed while pushing a solid object (original 0.375 px/tick)
		 *
		 * Player and object move together at this flat rate whatever the player was doing beforehand - walking
		 * into a box and dashing into a rock push at exactly the same speed. As with the buttstomp drift, the
		 * original's own xSpeed reads zero throughout while the positions creep along.
		 */
		static constexpr float LegacyPushSpeed = 0.375f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged vine climbing speed cap (original 2 px/tick, doubled while Run is held)
		 *
		 * Measured on the level's own vine: half the walk cap on its own, exactly the walk cap with Run. The
		 * engine's vine cap happened to be right but its Run bonus was 1.6x rather than 2x.
		 */
		static constexpr float LegacyVineSpeed = 2.0f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged copter descent speed (original 1 px/tick)
		 *
		 * Measured identical for Jazz and Lori. Their horizontal speed while coptering tops out at the walk cap
		 * (@ref LegacyWalkSpeed), which the ordinary ground handling already produces.
		 */
		static constexpr float LegacyCopterDescentSpeed = 1.0f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged launch speed of Spaz's double jump (original 8 px/tick)
		 *
		 * A straight speed assignment, like the ordinary jump and unlike the engine's old model of a small
		 * speed plus a sustained internal force - which is what made it feel far too weak. Height is then
		 * controlled by how long the key is held, through the same @ref LegacyRiseGravityReleased switch the
		 * first jump uses. Measured: it adds about 80 px over a plain 132 px jump, and it only triggers within
		 * roughly 20 of the original's ticks after the apex - pressed 30 ticks late it does nothing at all.
		 */
		static constexpr float LegacyDoubleJumpSpeed = 8.0f * LegacyFrameRateScale;
		/**
		 * @brief How long Spaz's non-Reforged double jump stays available (original 30 ticks)
		 *
		 * The window is a **timer started when the jump key is let go**, not a band of fall speeds. Those two
		 * look identical if the first press is always released at the same tick, because the fall speed then
		 * grows with the delay - which is exactly what the `sp_dj_d*` family does, and why it was originally
		 * read as a speed window of `(0, 3.75)`.
		 *
		 * Releasing at a *different* tick separates them. Measured across four release ticks: accepted at
		 * delays of 5, 10, 20, 22, 25, 28 and 29 ticks, refused at 30, 35 and 40. `sp_dj_r60_d28` is accepted
		 * at a fall speed of 3.00 while `sp_dj_hold` is refused at **the same 3.00**, differing only in a
		 * delay of 28 against 35 - so the speed cannot be what decides it. Nine scenarios used to double
		 * jump here where the original does nothing at all.
		 *
		 * Thirty rather than the twenty-nine of the longest accepted delay, because it is counted the way the
		 * original counts it: set on the tick of the release, decremented at the top of every tick after it,
		 * and tested for "still above zero" - so a press 29 ticks later sees 1 and one 30 ticks later sees 0.
		 *
		 * A duration, so it divides by @ref LegacyFrameRateScale rather than multiplying.
		 */
		static constexpr float LegacyDoubleJumpWindowTime = 30.0f / LegacyFrameRateScale;
		/**
		 * @brief Launch speed of Jazz's non-Reforged uppercut (original 6.59375 px/tick of travel)
		 *
		 * The move is a straight speed assignment like every other launch, and what makes it climb ~225 px
		 * on so modest a speed is that gravity is almost switched off while it runs - see
		 * @ref LegacyUppercutGravity.
		 *
		 * This is taken from the **distance travelled**, not from the speed column, because the two disagree:
		 * the original reports `ys` = −7.5 at the launch decaying to −6.59, while the position advances
		 * 6.59/tick decaying to 5.69 - a constant offset of about 0.84 that the applied rise cap cannot
		 * explain, since every value involved is already under it. The same reported-versus-travelled
		 * decoupling turns up in the dash and the buttstomp drift, and as there, the travel is what the
		 * player sees and nothing reads this move's vertical speed.
		 */
		static constexpr float LegacyUppercutSpeed = 6.59375f * LegacyFrameRateScale;
		/**
		 * @brief Gravity while a non-Reforged uppercut is driving (original 2048/65536 px/tick²)
		 *
		 * A twelfth of the ordinary rise gravity, which is why the rise stays nearly linear for its whole
		 * length: measured `ys` = −7.5 at the launch decaying by exactly 0.03125 a tick to −6.59375 thirty
		 * ticks later, then ordinary gravity from the tick the move ends.
		 */
		static constexpr float LegacyUppercutGravity = (2048.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Ticks a non-Reforged uppercut drives for before ordinary gravity resumes (original 30) */
		static constexpr float LegacyUppercutTicks = 30.0f / LegacyFrameRateScale;
		/**
		 * @brief Speed a non-Reforged RF blast throws the player at (original 8.0 px/tick)
		 *
		 * Firing an RF into a nearby wall throws the player away from it, which is how some jumps are made.
		 * Measured by parking the player a set distance from the wall on the test level's upper floor and
		 * firing from a standstill: the speed snaps to exactly this - the applied-movement cap, so every
		 * pixel of it is travelled - and is then **held** for @ref LegacyRFBlastHoldTicks with friction and
		 * the direction keys both suppressed, even on the ground, where nothing else in the game slides at a
		 * constant speed. When the hold ends the speed snaps to the walk cap and decelerates at the walking
		 * rate, exactly as Spaz's sidekick does, for about 302 px of travel in all.
		 *
		 * One case disagrees and is not reproduced: fired while jammed against the wall the original throws
		 * the player at 6.3125 rather than 8.0, for 226.6 px. Two clean measurements at 24 and 36 px both
		 * give a round 8.0, so that is what this follows.
		 */
		static constexpr float LegacyRFBlastSpeed = 8.0f * LegacyFrameRateScale;
		/** @brief Upward kick a non-Reforged RF blast gives a player who is off the ground (original 12 px/tick) */
		static constexpr float LegacyRFBlastRiseSpeed = 12.0f * LegacyFrameRateScale;
		/** @brief Ticks a non-Reforged RF blast holds the thrown speed for before friction resumes (original 30) */
		static constexpr float LegacyRFBlastHoldTicks = 30.0f / LegacyFrameRateScale;
		/**
		 * @brief How close to a non-Reforged RF blast the player has to be to be thrown by it (original ~40 px)
		 *
		 * Measured as a bracket rather than a value: parked 24 and 36 px from the wall the player is thrown,
		 * parked 48 and 64 px away nothing happens. Parked 12 px away nothing happens either, but that is
		 * the muzzle ending up inside the wall rather than a minimum range - the ammo is still spent - and
		 * firing from *against* the wall does throw the player, so no lower bound is applied.
		 *
		 * The bracket is therefore `(36, 48]` and this has to sit inside it. An earlier 32 was below the
		 * nearest throw that was actually observed, so the 36 px case rejected the blast that measurement
		 * says lands - including the probe's own `wb_rf_r36` scenario.
		 */
		static constexpr float LegacyRFBlastReach = 40.0f;
		/** @brief Non-Reforged sidekick launch speed for Spaz (original 16 px/tick) */
		static constexpr float LegacySpazSidekickSpeed = 16.0f * LegacyFrameRateScale;
		/**
		 * @brief Distance the driven part of Spaz's non-Reforged sidekick covers (original 440 px)
		 *
		 * His dash is not a special case at all: it holds a speed of 16 - so the applied cap makes him travel
		 * 8 px/tick - for about 55 of the original's ticks, and then the speed simply snaps back to the walk
		 * cap and the ordinary walk deceleration coasts him to a stop. That coast adds a further ~65 px, for
		 * the ~505 px he is measured to cover in total, so only the driven 440 px belongs here. Expressed as a
		 * distance rather than a duration because that is what was measured reliably.
		 */
		static constexpr float LegacySpazSidekickDistance = 440.0f;
		/** @brief Distance one of Lori's non-Reforged kicks covers (original 204.75 px) */
		static constexpr float LegacyLoriSidekickDistance = 204.75f;
		/**
		 * @brief Quadratic ramp of Lori's non-Reforged kick, per tick squared
		 *
		 * Her kick accelerates rather than starting at its peak: measured tick by tick, its speed is exactly
		 * `(n / 2)^2` px per original tick - 6.25, 9, 12.25, 16, 20.25, 25, 30.25, 36, 42.25 - i.e. quadratic
		 * in the elapsed time, running for 13 ticks and summing to @ref LegacyLoriSidekickDistance. Starting
		 * it at the peak instead (a sixth of the way in) is what made the kick look far too fast.
		 *
		 * The `0.25` is the original's own coefficient; the cube of the frame-rate scale converts a speed that
		 * is itself quadratic in ticks - one factor for the speed and two for the time.
		 */
		static constexpr float LegacyLoriKickRamp = 0.25f * LegacyFrameRateScale * LegacyFrameRateScale * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged sidekick launch speed for Lori (original 42.25 px/tick)
		 *
		 * Far above the applied-movement cap, so most of it is momentum: she travels at the cap and the excess
		 * is what keeps her going, which is why her dash covers so much more ground than Spaz's. Her kick is
		 * also the one move exempt from @ref LegacyAppliedSpeedCap - see @ref OnUpdatePhysics() - since its
		 * measured 204.75 px in 13 ticks is more than the cap would allow.
		 */
		static constexpr float LegacyLoriSidekickSpeed = 42.25f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged speed a vertical pole adds to the one the player arrived with (original 15.625 px/tick)
		 *
		 * The launch is `15.625 + |entry speed|`, measured with a slope of exactly 1.0 over entry speeds from
		 * 6.6 to 24.5 - so a pole hands back everything it was given plus a fixed bonus. That is what makes
		 * poles compound: chain them, or enter one off a spring, and each adds its bonus to an already larger
		 * speed. A single pole entered from a jump reaches ~4 tiles more than the jump alone; entered off a
		 * blue spring the same pole throws the player 25 tiles.
		 */
		static constexpr float LegacyPoleLaunchBonus = 15.625f * LegacyFrameRateScale;
		/**
		 * @brief Factor a non-Reforged horizontal pole multiplies the entry speed by (original exactly 3)
		 *
		 * Unlike the vertical pole, which *adds* @ref LegacyPoleLaunchBonus, the horizontal one multiplies -
		 * and then clamps between @ref LegacyHPoleMinLaunch and @ref LegacyHPoleMaxLaunch. Fitted exactly on
		 * four entry speeds: 0 -> 8 (the floor), 4 -> 12, 9.34 -> 20 (clamped from 28), 16 -> 20 (from 48).
		 *
		 * Only the second of those four actually determines this factor - the last two are on the ceiling and
		 * the first on the floor - so it rests on a single point, and `2*v + 4` and `4*v - 4` fit that point
		 * just as well. What distinguishes them is the stretch between 2.67 and 6.67, which nothing measures
		 * yet. Read it as the shape that survived four samples rather than as a value pinned down.
		 */
		static constexpr float LegacyHPoleLaunchScale = 3.0f;
		/** @brief Ceiling on the non-Reforged horizontal pole launch (original 20 px/tick) */
		static constexpr float LegacyHPoleMaxLaunch = 20.0f * LegacyFrameRateScale;
		/**
		 * @brief Floor on the non-Reforged horizontal pole launch (original 8 px/tick)
		 *
		 * Below about a third of @ref LegacyHPoleMaxLaunch the multiply is not what decides the launch: the
		 * original never returns less than 8, whatever the entry speed. Measured at an entry speed of exactly
		 * zero - a player who drops onto a pole rather than running into one - where the multiply would give
		 * nothing at all and the player would stay in the tile and re-grab it for ever.
		 */
		static constexpr float LegacyHPoleMinLaunch = 8.0f * LegacyFrameRateScale;
		/**
		 * @brief Speed a non-Reforged float-up area holds the player at (original 8 px/tick)
		 *
		 * Not an acceleration and not a force: measured inside a solid ten-tile column of them, the original
		 * reads exactly -8.0 on every one of the 43 ticks the player is in the field, and only starts
		 * decaying - at the ordinary rise gravity - once they leave it. It needs no jump and lifts a
		 * *grounded* player straight off the floor: pressing nothing gives a trace identical to jumping.
		 *
		 * The sawtooth a diagonal ladder of them produces is therefore geometry, not the rule. The player
		 * keeps leaving and re-entering discrete tiles, and the ascent decays in the gaps between - which
		 * averages half the assignment, ~3.9 px/tick, whatever the decay rate happens to be.
		 */
		static constexpr float LegacyFloatUpSpeed = 8.0f * LegacyFrameRateScale;
		/**
		 * @brief Pixels a non-Reforged wind area moves the player per unit of its strength (original 0.5)
		 *
		 * A *position* move, not a speed: the original's `xs` reads 0.0000 for the whole time a player is
		 * being blown along. Fitted on two strengths, 8 -> 4.0 px/tick and 4 -> 2.0, so it is linear.
		 */
		static constexpr float LegacyWindFactor = 0.5f * LegacyFrameRateScale;
		/**
		 * @brief Pixels a non-Reforged belt moves the player per unit of its strength (original 1.0)
		 *
		 * Also a position move, and **twice** @ref LegacyWindFactor - the two mechanics share an event type
		 * in this engine but not a strength. Fitted on 8 -> 8.0 px/tick and the parameterless default -> 2.0,
		 * which is what pins the default strength at 2 rather than the 3 the converter used to substitute.
		 */
		static constexpr float LegacyBeltFactor = 1.0f * LegacyFrameRateScale;
		/**
		 * @brief Speed a non-Reforged accelerating belt drives the player to, per unit of strength (orig 1.5)
		 *
		 * The accelerating belt is the one of the three that works on the *speed*: it ramps `xs` up by about
		 * a third of the target each tick and then holds it there. Fitted on 8 -> 12.0 px/tick and the
		 * parameterless default -> 6.0, which pins that default at 4.
		 */
		static constexpr float LegacyAccBeltSpeed = 1.5f * LegacyFrameRateScale;
		/**
		 * @brief How much an accelerating belt adds per tick, per unit of strength (original 0.5 px/tick^2)
		 *
		 * A third of @ref LegacyAccBeltSpeed, which is what makes the ramp take three ticks. Applied *after*
		 * the brake, not before: the original's `xs` runs 2.0, 3.878, 5.756 and then holds exactly 6.000 on
		 * a default belt, and those middle values are this step minus one friction step of 0.122 each tick.
		 * Adding it before the brake - where the floor-event handler naturally runs - leaves the steady state
		 * one friction step short, at 5.862 instead of 6.000.
		 */
		static constexpr float LegacyAccBeltStep = 0.5f * LegacyFrameRateScaleSqr;

		// There were two hand-tuned scale factors here, `LegacyVerticalSpeedScale` (x0.94) and
		// `LegacySpecialMoveScale` (x1.10 on top of it), for the two manoeuvres measurement had not reached:
		// the ledge hop and the uppercut. Both are gone. The uppercut was measured, and the ledge hop turned
		// out not to exist - the original clears gaps ballistically, on constants that are already exact.
		// Nothing in the non-Reforged model is a fudge factor any more: every constant is the original's own,
		// scaled by the frame-rate conversion and nothing else.

		/** @} */

	protected:
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
		// Whether an accelerating belt was carrying the player last frame. Leaving one clamps the speed to
		// the walk cap, the same way leaving a sucker tube does, so the transition has to be noticed.
		bool _wasOnAccBelt = false;
		// `_speed.Y` as it stood at the start of the frame, before this frame's gravity. The non-Reforged
		// double-jump window is tested against this rather than the live value, because that is what the
		// original tests: its gravity lands after the check, so a press at the exact apex reads 0.0000 and
		// is refused, where reading afterwards turns the same press into +0.44 and accepts it.
		float _frameStartSpeedY = 0.0f;
		// Counts down from LegacyDoubleJumpWindowTime, started when the jump key is let go. The
		// non-Reforged double jump is only accepted while it is running.
		float _doubleJumpWindowLeft = 0.0f;
		// Counts down while a pinball paddle has the player on it, re-armed by the paddle every frame it sees
		// them. A timer rather than a flag because the paddle is a separate actor and may update either side
		// of the player in the same frame, which a plain "set it, clear it" pair cannot survive.
		//
		// It exists for the pose: the original puts the player into the same curled-up ball a sucker tube
		// uses for as long as they are standing on a paddle - visible in its trace as animation 58 several
		// ticks *before* the jump press that fires it. Setting the animation from the paddle alone cannot
		// work, because UpdateAnimation() re-derives it from the player's own state every frame.
		float _onPinballPaddleTime = 0.0f;
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
		bool _flyCheatActive;
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
		std::shared_ptr<AudioBufferPlayer> _airboardSound;
		std::shared_ptr<AudioBufferPlayer> _airboardTurnSound;
#endif

		std::int32_t _lives, _score;
		InventoryState _inventory;
		InventoryState _inventoryCheckpoint;
		Vector2f _checkpointPos;
		float _checkpointLight;
		// Ambient light currently applied to this player. Mirrors the assigned viewport, but is tracked on the actor
		// itself so it's also known where no viewport exists - a dedicated server, or a server-side shadow of a
		// remote player. That's what makes a checkpoint able to remember the light it was activated in.
		float _currentAmbientLight;

		float _sugarRushLeft, _sugarRushStarsTime;
		float _shieldSpawnTime;
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
		/** @brief Ticks the non-Reforged dash state still has left after Run was let go (see @ref LegacyDashGraceTicks) */
		float _dashGraceLeft;
		/** @brief Pixels of travel a non-Reforged sidekick still has; counting distance rather than time makes it exact whatever the frame rate (see @ref CheckEndOfSpecialMoves()) */
		float _sidekickDistanceLeft;
		/** @brief Ticks elapsed in the current non-Reforged sidekick, for Lori's quadratic ramp (see @ref LegacyLoriKickRamp) */
		float _sidekickTime;
		/** @brief Ticks an RF blast still holds the horizontal speed for, with friction and input ignored (see @ref LegacyRFBlastHoldTicks) */
		float _rfBlastLeft;
		/** @brief Ticks a non-Reforged uppercut still drives for, during which gravity is almost off (see @ref LegacyUppercutGravity) */
		float _uppercutTimeLeft;
		/** @brief Whether the jump key has been let go during the current non-Reforged ascent, which makes the rest of it heavier */
		bool _jumpReleased;
		/** @brief Whether the pole currently held was reached off a spring, which is what picks the rise gravity its launch decays under */
		bool _poleEnteredOnSpring;
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
		void OnEmitRemotedLights(SmallVectorImpl<LightEmitter>& lights) override;

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

		/**
		 * @brief Called each frame while the player is in @ref PlayerType::Spectate mode
		 *
		 * The default implementation flies the (invisible) player around the level with the movement keys, which is
		 * what the camera follows. Multiplayer overrides it to let the spectator lock onto another player instead.
		 */
		virtual void OnHandleSpectate(float timeMult);

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
		void HandleHorizontalMovement(float timeMult);
		void HandleWaterAndModifierMovement(float timeMult);
		void HandleLookupAndCrouch(float timeMult, bool canJumpPrev);
		void HandleJump(float timeMult);
		// Whether the non-Reforged dash is up - Run held, or still inside the grace that outlives it
		bool IsDashActive() const;
		// The Strength of the `MODIFIER_SLIDE` tile under the player, or -1 when there is none. Read from the
		// tile below rather than the one the player is in, the same way a belt is, and only while grounded.
		std::int32_t GetSlideStrength() const;
		// The signed strength of the accelerating belt under the player - negative for one pulling left, 0
		// for none. Read the same way, and for the same reason: it is a floor event.
		std::int32_t GetAccBeltStrength() const;
		// Advances that grace and applies the snap back to the walk cap when it lapses
		void UpdateDashState(float timeMult);
		// Drops the non-Reforged movement state that outlives a single frame, for the paths that hand the
		// player a clean slate (respawn, warp). Each of these is advanced from a place that a dead or warping
		// player never reaches, so without this they thaw at their old value once control comes back.
		void ResetLegacyMovementState();
		void HandleSpecialJump(float timeMult);
		void HandleWeaponFire(bool areaWeaponAllowed);
		void OnHandleWater();
		void OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock);
		// What a single point of the path travelled this frame is allowed to act on, because the two halves of
		// a tile event apply to different parts of the path (see Player::OnHandleAreaEvents())
		enum class AreaEventPass {
			None = 0x00,
			// The one-shot effects the tile triggers - a warp, a pole, a text, a script callback. None of them
			// is idempotent, so only the sample that first enters a tile may run them
			Effects = 0x01,
			// The state the tile describes - whether weapons may fire, how deep the shallow water is, the
			// ambient light. Only the tile the player ended the frame in decides those
			States = 0x02,
		};

		DEATH_PRIVATE_ENUM_FLAGS(AreaEventPass);

		// Handles the tile event at a single point of the path travelled this frame, returns `true` when the
		// event took the player over (a warp, a pole, a tube, ...) and the rest of the path must be ignored
		bool HandleAreaEventAt(float x, float y, float timeMult, AreaEventPass pass, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock);
		void DoWarpOut(Vector2f pos, WarpFlags flags);
		// Attaches the player to a pole, returns `false` if it was declined (already on one, just came off this
		// one, wrong character, ...). `eventPos` is where the pole event was actually met, which is not
		// necessarily where the player ended the frame - at speed it can be a tile or two further on (see
		// Player::OnHandleAreaEvents())
		bool InitialPoleStage(bool horizontal, Vector2f eventPos);
		void NextPoleStage(bool horizontal, bool positive, std::int32_t stagesLeft, float lastSpeed);
		void StopAllActiveSounds();

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