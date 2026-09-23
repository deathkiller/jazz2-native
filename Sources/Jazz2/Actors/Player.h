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
		struct InventoryState {
			/** @brief Number of collected coins */
			std::int32_t Coins;
			/** @brief Amount of eaten food (drives sugar rush) */
			std::int32_t FoodEaten;
			/** @brief Number of collected gems, by gem type */
			std::int32_t Gems[4];
			/** @brief Remaining weapon ammo (in 1/256 units), by weapon type; `UINT16_MAX` means unlimited */
			std::uint16_t WeaponAmmo[(std::int32_t)WeaponType::Count];
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
		ArrayView<const std::uint16_t> GetWeaponAmmo() const {
			return _inventory.WeaponAmmo;
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
		/**
		 * @brief Returns how far the non-Reforged camera should lead the player horizontally, in pixels
		 *
		 * Zero unless a direction is actually held: measured, the original aims the lead at the *input*
		 * rather than at the speed, which is why a sidekick carrying 16 px/tick with nothing pressed does
		 * not pan the view at all. See @ref Rendering::PlayerViewport::UpdateCamera().
		 */
		float GetCameraLookAhead() const;
		/**
		 * @brief Returns how far the non-Reforged camera should drop below the player, in pixels
		 *
		 * Zero except during a buttstomp, which is the one place the original leads vertically at all --- it
		 * pans down through the wind-up so more of what is below comes into view, holds while the stomp runs,
		 * and returns to centre when it ends. See @ref LegacyCameraButtstompDrop.
		 */
		float GetCameraVerticalOffset() const;

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
		 * @brief How long a sucker tube keeps hold of the player (original 16 ticks)
		 *
		 * No `Legacy` prefix and no gate: measured from the original, but applied in **both** modes at the
		 * user's request. The tube is the original's mechanic either way - its speed comes out of the level
		 * in the original's units - and what Reforged had instead was a flat 10 ticks that never re-armed.
		 * See @ref movement-accuracy-objects-tube.
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
		static constexpr float TubeControlTime = 16.0f / LegacyFrameRateScale;
		/**
		 * @brief Where in its tile a horizontal sucker tube holds the player (original: 15 px below the top)
		 *
		 * Reported as a centring problem and measured as one. Riding the row of tubes in `tb_row`, the
		 * original holds @cpp y @ce at exactly 1327 for the whole ride, which is 15 px into the tile at row
		 * 41 --- one pixel above its centre. This engine snapped to 8, a quarter of the way down, and rode
		 * **7 px high**. The horizontal snap on the other branch has always used the tile centre; only the
		 * vertical one was off.
		 *
		 * About 1.8 px of that 7 is not the tube's: the two games' player origins differ by that much for
		 * the same standing position everywhere in the harness @m_span{m-text m-dim} (1330.5 against 1328.7
		 * on a floor, and the same gap after leaving the tube) @m_endspan and nothing else corrects for it.
		 * Matching the measured number leaves that residual rather than inventing a correction for it.
		 *
		 * No `Legacy` prefix and no gate, for the reason given on @ref TubeControlTime: the whole tube is
		 * shared with Reforged, which rode 7 px high for the same reason this did.
		 */
		static constexpr float TubeSnapY = 15.0f;
		/**
		 * @brief Ticks the non-Reforged dash outlives the Run key (original 16 ticks)
		 *
		 * The dash is a timed state rather than just a higher cap. Letting go of Run does not bleed the speed
		 * off at all - the player keeps the full @ref LegacyDashSpeed for this long and then the speed is
		 * clamped straight back to @ref LegacyWalkSpeed in a single tick, in mid-air exactly as on the ground.
		 */
		static constexpr float LegacyDashGraceTicks = 16.0f / LegacyFrameRateScale;
		/**
		 * @brief How far the non-Reforged camera leads the player while walking (original 28 px)
		 *
		 * Measured as the steady value of `camera - player` once the walk cap is reached, and it is a
		 * *distance*, so unlike a velocity it needs no frame-rate conversion. Reforged leads 80 px here,
		 * nearly three times as far, which is the whole of "the pan is more aggressive than the original".
		 */
		static constexpr float LegacyCameraWalkLead = 28.0f;
		/**
		 * @brief How far the non-Reforged camera leads the player while dashing (original 120 px)
		 *
		 * Keyed on @ref IsDashActive() rather than on the speed, which is the same thing by construction:
		 * the grace that outlives the Run key is exactly the window in which the speed is still the dash
		 * cap. `g_dash_relrun` is what establishes that - releasing Run does not shorten the lead, and it
		 * only starts closing once @ref UpdateDashState() clamps the speed back to @ref LegacyWalkSpeed.
		 */
		static constexpr float LegacyCameraDashLead = 120.0f;
		/**
		 * @brief How far the non-Reforged camera drops during a buttstomp, so more is visible below (original 60 px)
		 *
		 * The one place the original's camera leads *vertically*, and the only thing that stopped this being
		 * found earlier is that it is the sole exception to "there is no vertical lead to reproduce". Measured
		 * on `sp_butt_right`: `camy` sits at −225.87 while standing, and from the tick the wind-up pose appears
		 * it runs to −165.07 and stays there for the rest of the stomp --- 60.8 px, reached in about 30 ticks.
		 *
		 * The approach is the horizontal one's, at twice the step: a flat **2.00 px/tick** through the middle of
		 * it and an ease-out that halves exactly (1.00, 0.50, 0.25, 0.13, 0.06), which is
		 * @ref Jazz2::Rendering::PlayerViewport::LegacyCameraApproach against a doubled
		 * @ref Jazz2::Rendering::PlayerViewport::LegacyCameraMaxStep. The original's first five ticks briefly
		 * overshoot to 3.88 px/tick before settling back to 2, which this does not reproduce --- it is a fifth
		 * of a second at the very start of a 30-tick pan.
		 */
		static constexpr float LegacyCameraButtstompDrop = 60.0f;
		/**
		 * @brief Ticks the non-Reforged landing pose spends on each of its frames
		 *
		 * **Deliberately slower than the original, chosen by eye after three passes.** The original's rate is
		 * not in doubt: `an_land` shows its five frames at ticks 76, 78, 81, 84 and 87 and leaves at 91, and
		 * a scan of every landing in the captured traces finds **254** complete ones, all five frames, all
		 * **14 ticks** end to end --- so **2.75** a frame, a fifth of a second. This engine's own rate is
		 * `128/256` of a second across the same five frames, so **7.0** a frame.
		 *
		 * Both were rejected on sight: 7.0 as "very slow and chopped", then 2.75 as "too fast", then 4.0 ---
		 * already half again slower than the original --- as "still quite fast". The original's landing is
		 * simply over before it registers at this engine's framerate, so matching it was abandoned as the
		 * goal. Anything here above 2.75 is a preference and nothing above it should be read as measured.
		 *
		 * Per *frame*, not per landing, because the characters do not agree on how many there are: Jazz and
		 * Spaz land in five frames and Lori in seven. A whole-pose duration would run Lori's at five-sevenths
		 * the speed of theirs.
		 */
		static constexpr float LandAnimFrameTicks = 5.5f;
		/**
		 * @brief How much faster the sidekick's ending animation plays, in both modes
		 *
		 * **A deliberate departure from the measurement, asked for after seeing it.** The original holds its
		 * end pose for 12 ticks on `sp_spaz_side_rel` where ours runs 10, so by the trace the length was
		 * already right and this takes it *further* from the original --- it is here because the pose reads as
		 * slow in play and ending it sooner was preferred to matching the number.
		 *
		 * Applied in code rather than in the shared `SidekickC` metadata entry (`FrameRate: 30`) so that the
		 * rate stays visible next to the reason for it, and so the measured value it departs from is recorded
		 * somewhere. What made the ending look wrong in the first place was separate and *is* measured: nothing
		 * could interrupt it, so it played over a jump. That is fixed in Player::BeginStandardJump() rather
		 * than by making the transition cancellable, which ends it after a single frame.
		 */
		static constexpr float SidekickEndSpeedUp = 2.0f;
		/**
		 * @brief Extra launch speed a jump out of a run-in-place gets, worth about a tile of height
		 *
		 * Reported from play: *"a run-in-place running jump jumps you 1 tile higher than a regular running
		 * jump"*. This engine gave the same height, and near enough exactly so --- the carry runs at 17.31 and
		 * the dash cap is 18.67, so the speed term of the launch differs by a tenth of a pixel a tick.
		 *
		 * **Fitted here, not measured against the original**, for the same reason as
		 * @ref LegacyRevUpButtstompCostFraction: the wind-up cannot be driven from a script. `rt_jump` rises
		 * 177.4 px without it, and the value is chosen to put that 32 px higher; the rise is checked against
		 * that target rather than against a trace.
		 */
		static constexpr float LegacyRevUpJumpBoost = 3.70f;

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
		 * @{ @name Flying carrot
		 *
		 * Measured from a **hand-played recording**, because scripted input cannot reach it: with the carrot
		 * collected and @cpp jjPLAYER.fly @ce reading FLYCARROT for the whole run, eighty ticks of scripted
		 * Jump move the original's player not at all --- not even an ordinary jump. Left and Right do get
		 * through, so it is specifically the flight that reads the raw key, exactly as the rev-up does.
		 *
		 * The recording is 3871 ticks of continuous flight and settles every figure here. Vertically it is
		 * two accelerations and nothing else: hold **Up** and the player accelerates upwards at a flat 0.25
		 * per tick @m_span{m-text m-dim} (934 ticks of the recording step by exactly that) @m_endspan to the
		 * ordinary @ref LegacyVerticalSpeedLimit of 32; let go and they accelerate downwards at 0.0625
		 * @m_span{m-text m-dim} (1243 ticks) @m_endspan to a terminal of 12. Horizontally it is ordinary
		 * ground movement, 4 px/tick walking and 16 with Run held. Jumping inside the flight is an ordinary
		 * jump, launching at −10 and decaying at the usual held and released rates.
		 *
		 * Neither probe can log the Up key --- JJ2+ exposes no @cpp keyUp @ce --- so those 934 ticks appear
		 * in the trace with every logged key at zero. That is what a rise with no input means here.
		 *
		 * @b Down is **not** measured: the recording never presses it, so it falls like no input rather than
		 * inventing a rate for the "descends slowly on Down" the mechanic is reported to have.
		 */
		static constexpr float LegacyFlyRiseAccel = 0.25f * LegacyFrameRateScaleSqr;
		/**
		 * @brief What a climb decays at once Up is let go, until it reaches zero (original 0.75 px/tick^2)
		 *
		 * The flight has **two** downward rates, not one, and missing this is what made the carrot fly far too
		 * high: releasing Up at the 32 cap brakes to a stop in 44 ticks, where treating it as the fall rate
		 * below would have taken 512 and carried the player most of a level upwards first. Measured across a
		 * clean release in the recording - ticks 1331 to 1375 step by exactly 0.75 and the last one clamps to
		 * zero rather than overshooting.
		 *
		 * Deliberately its own constant and not @ref LegacyRiseGravityReleased, which is 0.875: an ordinary
		 * released jump and a released climb decay at different rates, and the recording contains 297 steps
		 * of 0.75 and none of 0.875.
		 */
		static constexpr float LegacyFlyRiseBrake = 0.75f * LegacyFrameRateScaleSqr;
		/**
		 * @brief What a climb decays at with **Jump** held (original 0.25 px/tick^2)
		 *
		 * The flight has the same held/released pair an ordinary jump does, with its own two numbers. Measured
		 * on the second recording, which jumps into the carrot from the ground: the launch decays at the
		 * ordinary 0.375 for the six ticks it is still a jump and then at **0.25** from the tick the flight
		 * animation takes over, for thirty ticks with Jump held throughout.
		 *
		 * Numerically the same as @ref LegacyFlyRiseAccel and not derived from it --- one is what Up adds and
		 * the other is what Jump fails to stop.
		 */
		static constexpr float LegacyFlyRiseBrakeHeld = 0.25f * LegacyFrameRateScaleSqr;
		/**
		 * @brief The descent **Down** holds the player to (original 1.0625 px/tick)
		 *
		 * Assigned outright rather than accelerated towards: the recording snaps from a 3.3125 px/tick fall to
		 * 1.0625 on the tick Down goes down, and then holds it for **734 consecutive ticks** without varying.
		 * Letting go resumes the ordinary @ref LegacyFlyFallAccel from wherever the speed is.
		 *
		 * Close to the copter's @ref LegacyCopterDescentSpeed but not the same, which is what "descends slowly
		 * on Down at a rate that is not the copter's" turns out to mean: 1.0625 against 1.0078.
		 *
		 * Only ever observed entered from a fall. Whether holding Down during a *climb* reverses it as
		 * abruptly is not measured; assigning is the simpler rule and the one every sample shows.
		 */
		static constexpr float LegacyFlyDescentSpeed = 1.0625f * LegacyFrameRateScale;
		/** @brief Downward acceleration once the flight is actually falling, to @ref LegacyFlyTerminalSpeed */
		static constexpr float LegacyFlyFallAccel = 0.0625f * LegacyFrameRateScaleSqr;
		/** @brief Terminal speed of the flight's unpowered descent (original 12 px/tick) */
		static constexpr float LegacyFlyTerminalSpeed = 12.0f * LegacyFrameRateScale;
		/**
		 * @brief How long a ride on the level's lizard copter lasts outside Reforged (original 279 ticks)
		 *
		 * Measured on the `cp_mod_*` set, which catches six or seven separate copters per scenario: every
		 * ride that begins on a freshly spawned one lasts **279 ticks**, in all eight scenarios and whatever
		 * is held during it, so it is a plain timer and not something the flying affects. That is a hair
		 * under four seconds at the original's 70.021 Hz. Reforged keeps its three, which is where this
		 * engine had both.
		 *
		 * The rest of the ride --- the two accelerations, the descent under Down, the horizontal --- needed
		 * no constants of its own: it is @ref LegacyFlyRiseAccel and the rest of the flying carrot's model,
		 * which the lizard copter turned out to share exactly. See Player::HandleWaterAndModifierMovement().
		 */
		static constexpr float LegacyLizardCopterDuration = 279.0f / LegacyFrameRateScale;
		/**
		 * @brief How long the flight lasts, which is for as long as the player keeps it
		 *
		 * The original flies until a Fly Off area or a death - the recording holds it for 3871 ticks without
		 * expiring. This engine gave it ten seconds. Expressed as a large duration rather than as a flag so
		 * every existing countdown, animation and decor path keeps working unchanged; the flight now ends
		 * where it should, at @ref Jazz2::EventType::AreaFlyOff, which used to cancel only the airboard.
		 */
		static constexpr float LegacyFlyDuration = 1.0e6f;
		/** @} */
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
		 * @brief Non-Reforged copter descent speed (original 129/128 px/tick)
		 *
		 * Measured identical for Jazz and Lori. Their horizontal speed while coptering tops out at the walk cap
		 * (@ref LegacyWalkSpeed), which the ordinary ground handling already produces.
		 *
		 * Read as a round 1 for a long time, and it is not: the original holds **1.0078125**, which is
		 * 129/128 and exactly one @ref LegacyCopterWindUp above a pixel a tick. The two constants being one
		 * step apart is almost certainly not a coincidence.
		 */
		static constexpr float LegacyCopterDescentSpeed = (129.0f / 128.0f) * LegacyFrameRateScale;
		/**
		 * @brief Rate the non-Reforged copter winds its descent up at (original 1/128 px/tick²)
		 *
		 * The copter does not simply assign @ref LegacyCopterDescentSpeed. It is a *ceiling* the descent
		 * accelerates towards at this rate, and the acceleration is only visible when the copter is engaged
		 * while falling slower than the ceiling - which is rare, because an ordinary fall passes it within
		 * nine ticks of the apex. Where it shows: engaging at the peak of a jump, off a ceiling, or straight
		 * after a warp, all of which start from little or no vertical speed.
		 *
		 * Measured on `cp_tap65`, which engages at 0.625 px/tick and then climbs 0.633, 0.641, 0.648, 0.656
		 * for the rest of the scenario - **0.0078125 a tick**, still only 0.977 forty-five ticks later. Its
		 * neighbour `cp_tap70` engages at 1.250, above the ceiling, and snaps to 1.0078 in one tick. So the
		 * rule is a clamp on both sides: below the ceiling it ramps, at or above it drops straight to it.
		 * `wp_walk` shows the same ramp from a standing start after a warp - 0.008, 0.016, 0.023, 0.031.
		 */
		static constexpr float LegacyCopterWindUp = (1.0f / 128.0f) * LegacyFrameRateScaleSqr;
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
		/**
		 * @brief Upward kick the same blast gives a player standing on the floor, at zero range
		 *
		 * A separate figure from @ref LegacyRFBlastRiseSpeed and a much smaller one, and unlike it this one is
		 * not constant --- it **grows with the range**, which is the opposite of what a blast of fixed strength
		 * does and is why it went unfitted for so long. Measured at four ranges against the true gap:
		 *
		 * Gap | 0.6 px | 11.7 | 20.6 | 23.7 | 35.7
		 * --- | --- | --- | --- | --- | ---
		 * Assigned @cpp ys @ce | −0.75 | −2.75 | −4.25 | −4.75 | none
		 *
		 * A straight line through those four is @cpp -(0.646 + 0.1732 * gap) @ce and reproduces every one of
		 * them to within 0.08, with the last point covered by @ref LegacyRFBlastReach cutting the blast off
		 * entirely. The horizontal component saturates at 8 over the same range, so the two clearly do not
		 * share a direction; the reading that would explain it is a shot that falls as it flies, putting the
		 * explosion further below the player the longer it travels and tilting the push upward. Nothing here
		 * measures the shot's own gravity, so this is a fit of the measurement rather than a model of it.
		 */
		static constexpr float LegacyRFBlastLiftBase = 0.646f * LegacyFrameRateScale;
		/** @brief How much @ref LegacyRFBlastLiftBase grows per pixel of range (original 0.1732 per px) */
		static constexpr float LegacyRFBlastLiftSlope = 0.1732f * LegacyFrameRateScale;
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
		 * The bracket was read off the scenario *names* and came out as `(36, 48]`, which is why this was 40.
		 * Both halves of that were wrong. The names count from the wall tile and the player's own half-width
		 * eats the rest, so in true gaps - taken from `wb_wallfind`, the position a dash comes to rest at -
		 * those two scenarios are **23.7 and 35.7 px**. And what reaches this is the distance from the
		 * *shot* to the player, not the gap: the shot stops half its own width short of the wall, so it is
		 * `gap + playerHalf - shotHalf`, about `gap + 3`. The bracket in the units actually compared here is
		 * therefore `(26.7, 38.7]`, and 40 sits outside it - which is why `wb_rf_r48` still threw the player
		 * 306 px after the flight itself had been fixed, where the original moves them 0.9.
		 */
		static constexpr float LegacyRFBlastReach = 32.0f;
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
		/**
		 * @brief Ticks a horizontal spring's launch speed is held before it clamps (original 4)
		 *
		 * The launch itself is the spring's own figure --- 16, 24 and 32 for red, green and blue --- but the
		 * original only carries that for four ticks and then clamps to @ref LegacyCarryExitRunSpeed, which is
		 * the same 16 every other carry ends at. Red is unaffected, being at the cap already; green and blue
		 * both drop, measured on `ob_spring_green_h` and `ob_spring_blue_h` at exactly tick 31 to 35.
		 *
		 * Worth knowing what this does *not* change: the applied-movement cap means the player travels 8 px a
		 * tick whether the speed reads 32 or 16, so the positions are identical either way and only the speed
		 * column moves. It still matters, because anything that *reads* the speed sees it - a jump out of the
		 * launch takes its boost from there.
		 */
		static constexpr float LegacySpringHoldTicks = 4.0f;
		/**
		 * @brief Ticks a spring launch's speed takes to rebuild after a double jump discards it (original 32)
		 *
		 * The rebuild runs at the launch speed divided by this, so a faster spring comes back faster. Measured
		 * off all three horizontal springs, and the third one tested the rule rather than adding to it: red
		 * launches at 16 and rebuilds at 0.50 px/tick², green at 24 at 0.75, which made 1.00 the prediction
		 * for blue's 32 - and that is what blue does. The ceiling is a flat @ref LegacyDashSpeed however fast
		 * the launch was, so blue reaches it well before the 32 ticks are up.
		 *
		 * Only a spring arms this. A dash's speed, when a double jump takes it, comes back at the ordinary air
		 * acceleration - measured on `sp_dj_dash` - so a rule keyed on "was carrying speed" would be wrong.
		 */
		static constexpr float LegacySpringRebuildTicks = 32.0f;
		/**
		 * @brief Ticks after a warp lands before control comes back outside Reforged (original 2 ticks)
		 *
		 * Measured on `wp_walk`: the original lands on tick 73, shows one frame of its warp-out pose on 74,
		 * and is walking again on 75. Ours waited for the whole @ref AnimState::TransitionWarpOut animation
		 * to finish, which is **38 ticks** - so every warp charged the player half a second of standing still
		 * that the original never does. The animation still plays; it simply stops being what control waits on.
		 */
		static constexpr float LegacyWarpOutControlTime = 2.0f / LegacyFrameRateScale;
		/** @brief Distance one of Lori's non-Reforged kicks covers (original 204.75 px) */
		static constexpr float LegacyLoriSidekickDistance = 204.75f;
		/**
		 * @brief Rate a blocked kick of Lori's spends @ref LegacyLoriSidekickDistance at, per frame
		 *
		 * Her ramp runs `n = 2..13` and `0.25 * n^2` over those twelve original ticks sums to exactly
		 * @ref LegacyLoriSidekickDistance - but summing the same quadratic at 60 FPS does not reach that
		 * total over the same elapsed time. It needs twelve frames, which is fourteen original ticks. The
		 * distance is what shows in an unobstructed kick and it is already right, so the ramp stands there.
		 * The *duration* only shows when something holds the kick up, where it becomes how long the player
		 * shoves a pushable, and there the budget is drained at the flat rate those two numbers imply
		 * instead. Twelve ticks either way, and 4.5 px of rock per kick rather than 6.
		 */
		static constexpr float LegacyLoriKickPushDrain = LegacyLoriSidekickDistance * LegacyFrameRateScale / 12.0f;
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
		 * @brief Non-Reforged speed a horizontal pole adds to the one the player arrived with (original 8 px/tick)
		 *
		 * The same shape as @ref LegacyPoleLaunchBonus, with a smaller bonus and a ceiling: the launch is
		 * `|entry speed| + 8`, capped at @ref LegacyHPoleMaxLaunch. Five entry speeds fit it exactly --
		 * 0 -> 8, 4 -> 12, 9.89 -> 17.888, 17.888 -> 20 (capped from 25.9) and 20 -> 20 (from 28).
		 *
		 * This replaced a multiply by 3 clamped between a floor of 8 and the ceiling, which fitted the four
		 * points then available because three of them sat *on* those limits and only `4 -> 12` constrained
		 * the factor. The fifth point is the one that discriminates: entering at 9.89 the multiply predicts
		 * 29.7 and the player leaves at the capped 20, where the original leaves at 17.888 -- 12% apart, and
		 * invisible in travel because every scenario that reached the pole at a run ended against the same
		 * wall. `2*v + 4` and `4*v - 4`, the two alternatives the old fit could not separate, are ruled out
		 * by it as well. The floor is gone with the multiply: `v + 8` never returns less than 8 on its own,
		 * so what looked like a separate rule at an entry speed of zero was only the bonus.
		 *
		 * Reading the entry speed off the original's trace needs care, and this is where the 9.89 comes
		 * from: it logs the speed *before* the tick's acceleration and this engine after it, so the grab
		 * tick reads 9.5215 there and the pole actually saw one dash step more. Taking the logged figure at
		 * face value gives 17.5 against a measured 17.888 and makes an exact rule look approximate.
		 */
		static constexpr float LegacyHPoleLaunchBonus = 8.0f * LegacyFrameRateScale;
		/** @brief Ceiling on the non-Reforged horizontal pole launch (original 20 px/tick) */
		static constexpr float LegacyHPoleMaxLaunch = 20.0f * LegacyFrameRateScale;
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
		 * @brief Fastest a non-Reforged player descends while inside a float-up area (original 4 px/tick)
		 *
		 * The field cannot simply assign its rise to someone whose move drives its own downward speed, so what
		 * it does instead is hold their descent down to this. Measured on `fu_col_butt`, a buttstomp dropped
		 * into the column from clear air: outside it the stomp covers 10 px a tick, and from the tick its feet
		 * enter the field it covers exactly **4.00**, for all 52 ticks it takes to cross - while `ys` keeps
		 * climbing underneath, 12 then 16. So it is a cap on the movement, not on the speed.
		 *
		 * Three entry speeds land on the same figure - 6 on an ordinary fall, 12 and 16 on the stomp - which
		 * is what rules out the other reading, that the field's 8 px lift is simply subtracted: 6 − 8 would
		 * carry the player upwards, and 16 − 8 would be twice what is measured.
		 *
		 * This engine skipped the whole field while a buttstomp was running and fell through at the full 10,
		 * which is the reported "buttstomping a float-up event should slow the fall noticeably".
		 */
		static constexpr float LegacyFloatUpFallCap = 4.0f * LegacyFrameRateScale;
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
		/**
		 * @brief Strength a non-Reforged accelerating belt gains while Run is held (original 8)
		 *
		 * Holding Run on one raises the speed it drives to, and by a *flat* amount rather than a factor:
		 * measured 18 px/tick at strength 4 and 24 at strength 8, against 6 and 12 with nothing held, which
		 * is the same 1.5 per unit shifted by 12 - exactly what eight more strength is worth. Two strengths
		 * were needed to see that, because one point cannot tell 4 x 4.5 from 6 x 3. See `bl_acc_right_run`
		 * and `bl_acc_right_p8_run`.
		 *
		 * Only the target moves. The ramp's step is unchanged; what differs about the ramp with Run held is
		 * that the brake it is applied after becomes the dash brake, which is visible as the step reading
		 * 1.573 instead of 1.878 - one @ref LegacyDashDecel rather than one @ref LegacyWalkDecel.
		 */
		static constexpr std::int32_t LegacyAccBeltRunBonus = 8;
		/**
		 * @brief Speed a non-Reforged carry clamps to when it ends with Run held (original 16 px/tick)
		 *
		 * Leaving a sucker tube or an accelerating belt clamps the horizontal speed --- to
		 * @ref LegacyWalkSpeed with nothing held, and to this with Run. It is **one** rule across every site
		 * that snaps, and it took the accelerating belt to see it: a tube carries 8 px/tick and Spaz's
		 * sidekick leaves at 15.88, so a clamp to 16 is inert at both and they looked like "no clamp at all
		 * while Run is held". The belt reaches 18 and 24, where it is not inert, and both strengths snap to
		 * exactly 16 before decaying at @ref LegacyDashDecel.
		 *
		 * Measured on `bl_acc_right_run` and `bl_acc_right_p8_run` against `bl_acc_right` / `bl_acc_right_p8`,
		 * which snap to 4 and decay at @ref LegacyWalkDecel.
		 */
		static constexpr float LegacyCarryExitRunSpeed = 16.0f * LegacyFrameRateScale;

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
		//
		// Ordered widest first - pointers, then four-byte values, then the enums, then the flags - rather than
		// grouped by what they mean. The flags in particular were scattered through the four-byte members in
		// about a dozen short runs, and each run cost up to three bytes of padding to re-align whatever
		// followed it. Keep new members in the right band; a `bool` dropped in among the floats is free to
		// write and costs four bytes.
		ActorBase* _carryingObject;

		std::int32_t _playerIndex;
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
		float _controllableTimeout;
		// The run-in-place wind-up accumulator: what it currently holds, how much the press being held has
		// already contributed (so it can be capped), how many presses have landed, how much of the tap window
		// is left, how long it has been wound up - which is what scales the pose's speed - the spark timer,
		// the end animation's own countdown, and a light launch's wait with the speed it will move off at.
		float _revUpCharge;
		float _revUpHeldCharge;
		std::int32_t _revUpPresses;
		float _revUpWindowLeft;
		float _revUpChargeTime;
		float _revUpSparkCooldown;
		float _revUpEndLeft;
		// Where the stopping chain has got to, so a pose is issued once rather than restarted every frame
		std::int32_t _stopPhase;
		// How far into the vine's 140-tick idle cycle the player is, see LegacyHookIdleCycle
		float _hookIdleTime;
		float _revUpLaunchLeft;
		float _revUpPendingSpeed;
		float _copterFramesLeft, _fireFramesLeft, _pushFramesLeft, _waterCooldownLeft;
		std::int32_t _inShallowWater;
		float _externalForceCooldown;
		float _springCooldown;
		// Per-player recoloring: packed 4-byte fur color (one section per byte) and the allocated palette offset into
		// the shared palette texture (-1 = none). The renderer samples this palette via a per-instance offset.
		std::uint32_t _furColor;
		std::int32_t _paletteOffset;

		ExitType _lastExitType;
		PlayerType _playerType, _playerTypeOriginal;
		SpecialMoveType _currentSpecialMove;
		LevelExitingState _levelExiting;
		Modifier _activeModifier;

		bool _isActivelyPushing, _wasActivelyPushing;
		// Whether an accelerating belt was carrying the player last frame. Leaving one clamps the speed to
		// the walk cap, the same way leaving a sucker tube does, so the transition has to be noticed.
		bool _wasOnAccBelt = false;
		// `true` only for the physics step in which the player is actually in contact with a wall or pushable object
		// (set in OnHitWall / OnPushSolidObject, cleared at the start of each PushSolidObjects). Lets the push animation
		// tell genuine pushing from the lingering grace timer (_pushFramesLeft).
		bool _pushContactThisFrame;
		// `true` only for the physics step in which a *sidekick* pushed a solid object. Narrower than
		// `_pushContactThisFrame`, which a plain wall also raises: a blocked kick has to be billed for the
		// distance it meant to cover rather than the crawl it manages, and a kick into a wall does not.
		bool _kickPushedThisFrame = false;
		// Speed a horizontal spring launched the player at, kept so a double jump that discards it knows how
		// fast to rebuild it; 0 when the speed did not come from a spring. See LegacySpringRebuildTicks.
		float _springLaunchSpeedX = 0.0f;
		// Per-frame acceleration of that rebuild while it is running, 0 when it is not
		float _springRebuildAccel = 0.0f;
		// Frames left of a horizontal spring launch's held speed; on reaching zero the speed clamps to the
		// ordinary carry exit. See LegacySpringHoldTicks.
		float _springHoldLeft = 0.0f;
		// Whether a kick was pushing on the previous step, so the step it stops on can be recognised and the
		// object told to stop with it rather than coasting on - see SolidObjectBase::StopPushing()
		bool _kickWasPushing = false;
		bool _controllable;
		bool _controllableExternal;
		bool _wasUpPressed, _wasDownPressed, _wasJumpPressed, _wasFirePressed, _isRunPressed;
		// Whether `_isRunPressed` has been seeded from the live key state yet, for `ToggleRunAction`. A Run key
		// already held when a level starts produces no rising edge inside it, so the toggle would stay off
		// until the key was released and hit again - see Player::OnHandleMovement().
		bool _runActionSeeded;
		// Past the lower threshold: enough to launch when the tapping stops, but not enough to show the wind-up
		// pose. Both latch, so the charge decaying back below a threshold does not undo the state it reached -
		// that decay is the grace in which a further tap resumes the wind-up.
		bool _revUpArmed;
		bool _revUpWound;
		// Whether the one-shot start transition has already played for this wind-up. A flag rather than a check
		// on the current animation, because the looping pose is re-decided every frame and would re-trigger it.
		bool _revUpStarted;
		bool _isAttachedToPole, _canPushFurther;
		bool _isFreefall, _inWater, _isLifting, _isSpring;
		bool _flyCheatActive;
		bool _inIdleTransition, _inLedgeTransition;
		// Set while the stopping chain is swapping one pose for the next: SetTransition() runs the outgoing
		// transition's callback first, which would otherwise read as the outgoing pose having run out
		bool _stopPhaseChanging;
		// Whether the vine's idle flourish is the transition currently playing, so its finish callback knows
		// to start it again and nothing else has to guess what the transition is
		bool _inHookIdleFlavor;
		bool _canDoubleJump;
		// Whether the copter's single attempt for this airtime has been spent. The original gives one, taken
		// at the first jump press after leaving the ground and gone whether or not it succeeded; landing hands
		// it back. Both are set in Player::HandleJump(), on the press itself rather than where the copter is
		// engaged, because _jumpTime hides a press made within ten frames of a jump from that code entirely.
		bool _copterChanceUsed = false;
		// Whether *this* press is the one that took it, read by the copter branch of HandleSpecialJump()
		bool _copterChanceThisPress = false;
		// Ticks until Lori's kick repeats, start to start; 0 when no kick is pending. See LegacyLoriKickPeriod.
		float _loriKickRepeatLeft = 0.0f;
		// Whether this player is currently standing on top of another player (local splitscreen co-op stacking, and
		// online stacking when this is an `MpPlayer`); guards cancelling the carry so a real solid object is untouched
		bool _stackCarrying;
		// Whether another player is standing on top of this one without immobilizing it - drives the lift animation
		// cosmetically (online; local co-op uses `_isLifting` instead so movement is restricted, like a solid object)
		bool _beingStoodOn;

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

		// Same ordering as the block above: the wide members first and the flags last
		std::shared_ptr<Environment::Bird> _spawnedBird;
		std::shared_ptr<ActorBase> _activeModifierDecor;
		SmallVector<LightEmitter, 0> _trail;
		std::unique_ptr<RenderCommand> _weaponFlareCommand;
		std::unique_ptr<RenderCommand> _shieldRenderCommands[2];
#if defined(WITH_AUDIO)
		std::shared_ptr<AudioBufferPlayer> _weaponSound;
#endif

		Vector2i _lastPolePos;
		Vector2f _trailLastPos;
		float _suspendTime;
		float _invulnerableTime;
		float _invulnerableBlinkTime;
		float _jumpTime;
		float _idleTime;
		float _hitFloorTime;
		float _keepRunningTime;
		/**
		 * @brief Whether the carry @ref _keepRunningTime is counting down came out of a horizontal pole
		 *
		 * Which decides one thing only: a horizontal pole launch travels its whole speed instead of being
		 * held to @ref LegacyAppliedSpeedCap. It cannot be read off `_keepRunningTime` alone, because a
		 * spring sets that too and a spring launch *is* capped - measured, it reports 32 and moves 8.
		 */
		bool _hPoleCarry;
		/**
		 * @brief Whether the carry @ref _keepRunningTime is counting down came out of a spring
		 *
		 * Which decides one thing only: a **spring's** carry refuses the crouch for its whole length. The
		 * underlying rule is not special to springs --- the original refuses the crouch while the player is in
		 * *any* forced state after a launch, and hands it over on the tick that state lapses, whether the key
		 * was pressed a moment before or seventy ticks earlier. What differs is which state does the blocking
		 * and how long it lasts, and that is why this cannot simply key on `_keepRunningTime`:
		 *
		 * - A **spring** blocks for the carry itself, all 93 ticks of it, and the crouch and the carry's exit
		 *   clamp land on the same tick. `an_crouch_spring` presses Down 18 ticks in and `an_crouch_spring_l`
		 *   presses it 15 ticks from the end; the original crouches at tick 126 and 125 respectively.
		 * - A **horizontal pole** blocks for its *exit animation* instead, about 20 ticks, and the speed carry
		 *   outlives it by another 34. `an_crouch_carry` presses Down at 130 and `an_crouch_pole_e` at 122;
		 *   the original crouches at tick **133 in both**, the tick after that animation's last frame. This
		 *   engine already reproduces it, because the pole's exit transition is what holds the pose here too.
		 *
		 * So blocking the pole on `_keepRunningTime` as well would over-block it by some fifty ticks, and that
		 * is the whole reason this flag exists. The rev-up launch arms the same timer and is deliberately left
		 * out: nothing has measured what Down does to one.
		 */
		bool _springCarry;
		/**
		 * @brief Whether the carry @ref _keepRunningTime is counting down came out of a run-in-place launch
		 *
		 * The third source of that timer, and it behaves like neither of the other two. Reported from play:
		 * the crouch is refused for the whole of a run-in-place run --- where a pole's carry allows it and a
		 * spring's refuses it, so this cannot be folded into either flag --- and the facing is *not* taken
		 * over by the direction of travel, so the player can turn round and shoot backwards while still being
		 * carried forwards.
		 */
		bool _revUpCarry;
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
		float _lastPoleTime;
		float _inTubeTime;
		float _dizzyTime;
		float _activeShieldTime;
		float _weaponFlareTime;
		std::int32_t _weaponFlareFrame;
		float _weaponCooldown;

		SuspendType _suspendType;
		ShieldType _activeShield;
		WeaponType _currentWeapon;
		WeaponWheelState _weaponWheelState;

		/** @brief Whether the jump key has been let go during the current non-Reforged ascent, which makes the rest of it heavier */
		bool _jumpReleased;
		/** @brief Whether the pole currently held was reached off a spring, which is what picks the rise gravity its launch decays under */
		bool _poleEnteredOnSpring;
		/**
		 * @brief Whether the current non-Reforged ascent came out of a float-up area
		 *
		 * The one launch source that picks its rise gravity from the **live** jump key rather than from
		 * @ref _jumpReleased --- there was no jump to release. A spring and a pole with nothing pressed both
		 * decay at the held rate, so the sources disagree about what "nothing pressed" means and the ascent
		 * has to remember where it came from. See @ref GetGravityModifier().
		 */
		bool _riseFromFloatUp;
		/**
		 * @brief Whether a float-up area was holding the speed at the last sample, which suppresses gravity
		 *
		 * Distinct from @ref _riseFromFloatUp, which outlives the column to pick the rate the ascent decays
		 * at. This one is the column itself: inside it the original assigns a speed and moves exactly that
		 * far, with no gravity at all on the tick. Sampled in @ref OnHandleAreaEvents(), which runs at the
		 * end of the frame, and read by the *next* frame's move - the same order the speed it guards is
		 * written and read in.
		 */
		bool _inFloatUpArea;
		/**
		 * @brief Whether the crouch was already up when this frame began, which is what a special move needs
		 *
		 * @ref HandleLookupAndCrouch() sets the crouch and @ref HandleJump() reads it in the same frame, so
		 * the live bit cannot tell "ducked a moment ago and then jumped" from "pressed both together" - and
		 * the original gives an ordinary jump for the second. See @ref IsSpecialMoveCrouchReady().
		 */
		bool _crouchHeldBefore;
		bool _weaponAllowed;
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

		// Run-in-place rev-up. Tapping Run on the spot winds the player up and then launches them forward.
		//
		// MEASURED, from a recording of the original played by hand - see the free-run mode in `Tests/README.md`.
		// Scripted `jjPLAYER.keyRun` taps reach this mechanic not at all, at any cadence, so it took a human
		// recording to get at: the original evidently reads the raw key here, upstream of the script override.
		// Kept under a `RevUp*` name rather than `Legacy*` because it applies in **both** modes, unlike everything
		// the latter prefix marks. Residuals are in `Docs/MovementAccuracyReference.dox`.
		/**
		 * @{ @name Run-in-place wind-up accumulator
		 *
		 * The wind-up is entered on a *charge* rather than on a tap count, which is the only shape that fits
		 * all of what the recordings show: two taps never start it however they are spaced; three do if the
		 * last one is held longer; four do without holding; tapping faster reaches it sooner; gaps of about
		 * 20 ticks or more never reach it at all because the decay outruns the gain; and once it is running
		 * there is a grace in which another tap resumes it rather than starting over, which is simply the
		 * charge not having decayed to nothing yet.
		 *
		 * The shape is measured, the four numbers are fitted to the boundary cases above rather than read off
		 * a trace - a human tapping cannot separate gain-per-tap from gain-per-held-tick closely enough for
		 * that. They are named for what they do, not `Legacy*`, for the reason the block above gives.
		 */
		/**
		 * @brief What one press is worth
		 *
		 * Set so that **three presses can never reach @ref RevUpChargeThreshold however fast they come** - at
		 * 4.0 three quick taps landed within a whisker of it and tipped over, which showed as the wind-up
		 * starting on three taps when they were rattled off quickly and not when they were spaced. Three still
		 * reach it if the last is held, because the hold adds on top of this.
		 */
		static constexpr float RevUpChargeGainTap = 3.4f;
		static constexpr float RevUpChargeGainHeld = 0.15f;
		/**
		 * @brief The most one press can add by being held
		 *
		 * Without it, simply *holding* Run while standing still would wind the player up, which it must not -
		 * holding Run on the spot is what an ordinary player does before setting off. The cap is what makes
		 * holding a press worth something without making one long press worth everything.
		 */
		static constexpr float RevUpChargeHeldCap = 4.0f;
		// What it loses each frame with the key up. Slow enough to leave the grace that lets a tap resume a
		// wind-up, fast enough that taps 20 ticks apart never accumulate.
		static constexpr float RevUpChargeDecay = 0.15f;
		/**
		 * @brief Presses below which nothing happens at all, whatever the charge
		 *
		 * The charge on its own cannot promise this: a press held to the cap is worth 3.4 + 4, so two of them
		 * reach 14.7 and would not only arm but wind up. Since "two taps never do anything" is a rule rather
		 * than a consequence, it is enforced as one.
		 */
		static constexpr std::int32_t RevUpPressesRequired = 3;
		/**
		 * @brief Enough to launch when the tapping stops, but not to show the wind-up
		 *
		 * Reached together with @ref RevUpPressesRequired, both being needed. Three plain taps land between this
		 * and @ref RevUpChargeThreshold, which is the case with its own behaviour: the player stands idle the
		 * whole time, and letting the key go plays the animations and sets them running.
		 */
		static constexpr float RevUpChargeLightThreshold = 9.0f;
		/**
		 * @brief Where the wind-up itself begins
		 *
		 * Sits above what three plain taps reach, which is what makes the third one needing to be held - or a
		 * fourth press - the difference between standing still and revving. At a ~10 tick cadence: two taps
		 * reach 8.9, three 12.6, three with the last held 16.6, four 16.4, one press held indefinitely only 8.
		 */
		static constexpr float RevUpChargeThreshold = 14.0f;
		/** @} */
		/**
		 * @brief How long a tap keeps the wind-up alive (original ~20 ticks)
		 *
		 * One timer does both jobs, which is what the recording shows: while the count is below the threshold an
		 * expiry simply forgets the taps, and once it is above, an expiry *is* the launch. Measured off the tap
		 * gaps - 12 to 16 ticks apart sustain the wind-up, 21 to 28 apart do not and each tap only flashes the
		 * start pose before falling back to idle. That is also why a burst with 28-tick gaps early on and a
		 * 21-tick gap before its launch is not a contradiction: the early gaps had no wind-up to interrupt.
		 */
		static constexpr float RevUpTapWindow = 18.0f;
		/**
		 * @brief How long the wind-up takes to reach full charge (original ~90 ticks)
		 *
		 * Charge is a function of *time spent revving*, not of the tap count: 15 taps over 75 ticks launched at
		 * 15.87 while 12 taps over 106 ticks launched at the full 16.0.
		 */
		static constexpr float RevUpFullChargeTime = 77.0f;
		// Launch speed at no charge and at full charge (original 7.0 and 16.0, the latter being the dash cap
		// exactly - the launch never exceeds what a dash reaches)
		static constexpr float RevUpMinLaunchSpeed = 8.1667f;
		static constexpr float RevUpMaxLaunchSpeed = 18.6667f;
		/**
		 * @brief Share of a full run-in-place launch each buttstomp out of the run costs
		 *
		 * The stomp keeps the run's horizontal speed instead of killing it, and a quarter of the launch per
		 * stomp is what makes the fourth one stop the player --- which is how it was reported: *"it usually
		 * takes about 4 buttstomps from a full stop"*.
		 *
		 * **Derived from that count, not measured.** The original's wind-up reads the raw Run key and produces
		 * nothing at any scripted cadence, so there is no paired trace of a stomp out of one to fit against ---
		 * see the note on `rt_*` in `Tests/README.md`. If this is ever recorded by hand in FreeRun, the decay
		 * is the thing to check first.
		 */
		static constexpr float LegacyRevUpButtstompCostFraction = 0.25f;
		/**
		 * @brief How long the launch speed is held before it drops to the walk cap (original ~320 ticks)
		 *
		 * Measured as a flat hold and then a **single-tick** snap to the walk cap, which is the dash grace's own
		 * behaviour rather than ordinary friction - and it ignores steering completely for its whole length, as
		 * the recording shows with Right held while the player is still travelling left at 16.
		 */
		static constexpr float RevUpKeepRunningTime = 274.0f;
		/**
		 * @brief How much faster the wind-up pose runs at full charge
		 *
		 * The original's wind-up advances a frame every 7 ticks when it starts and every 2 at full charge, so
		 * it ends up about 3.6x faster than it began - the base rate is the metadata's own @cpp FrameRate @ce
		 * and this is what it is scaled by. Fitted rather than derived: the renderer's duration does not map
		 * linearly onto the observed frame-change rate, so a factor of 2.5 measured only 1.6x.
		 */
		static constexpr float RevUpAnimSpeedUp = 6.0f;
		// Frames between floor sparks at the slowest wind-up. Divided by the same factor that scales the pose's
		// speed, so the sparks quicken exactly as the feet do instead of keeping a schedule of their own.
		static constexpr float RevUpSparkInterval = 6.8f;
		// Where a spark leaves the player, relative to the feet: just above the floor and a little behind,
		// which reads as coming off the back of the shoe rather than out of the middle of the player
		static constexpr float RevUpSparkOffsetY = -4.0f;
		static constexpr float RevUpSparkOffsetBack = 4.0f;

		/**
		 * @brief How long after jumping off a vine another can be grabbed
		 *
		 * Only has to outlast the two or three frames it takes to rise clear of the vine just released. It is
		 * deliberately short because the original re-grabs a *different* vine three tiles higher only 12 of its
		 * ticks later - the previous 12-frame value is 14 ticks and swallowed that grab completely.
		 */
		static constexpr float VineDropCooldown = 6.0f;

		/**
		 * @brief How long the rev-up's end animation holds the pose (original 6 ticks)
		 *
		 * Measured: the original shows its end animation for exactly 6 ticks, two frames of it, every time.
		 * The base state is held at @ref AnimState::RevUp for this long so the transition is not cancelled out
		 * from under it by the switch to a running pose.
		 */
		static constexpr float RevUpEndTime = 5.2f;
		/** @brief How long the rev-up's start animation holds the pose (original 4 to 8 ticks) */
		static constexpr float RevUpStartTime = 4.0f;

		/**
		 * @{ @name Sliding to a halt
		 *
		 * The original runs a fixed chain of **three** animations, and only one boundary in it is a speed.
		 * `walk_stop` plays once for its own length whatever the player was doing, `dash_stop` then loops
		 * until the speed falls below this, and `run_stop` plays once and hands over to standing - which is
		 * why `walk_stop` is exported for all three characters and was read by nothing here. Measured on
		 * `an_slide_stop` @m_span{m-text m-dim} (released at 16 px/tick) @m_endspan, `g_walk_rel20` and
		 * `sd_walk` @m_span{m-text m-dim} (released at 4, one of them on a slide tile) @m_endspan, which
		 * disagree on every speed and agree on the first stage lasting exactly 12 ticks.
		 *
		 * Deliberately **not** named `Legacy*` even though it was measured like one, and deliberately not
		 * gated on @ref ILevelHandler::IsReforged(): every `Legacy*` constant on this class is legacy-only,
		 * and that invariant is what makes the prefix worth reading. Reforged runs the same chain because
		 * the chain is an animation, not a movement model - it reads the speed once and changes none of it,
		 * so it costs Reforged nothing to share and leaves one description of stopping instead of two. Only
		 * this boundary is in speed units at all, and the rest of the chain is timed, so the two modes'
		 * different friction simply makes the middle stage longer or shorter.
		 */
		static constexpr float StopSettleSpeed = 1.0f * LegacyFrameRateScale;

		/**
		 * @brief Speed above which the dash animation is shown, measured on `an_run_start`
		 *
		 * Speeding up is picked from the current speed in three bands, exactly as sliding to a halt is: `run`
		 * to the walk cap, `dash_start` @m_span{m-text m-dim} (the spinning feet) @m_endspan from there to
		 * this, and `dash` above it. The original spends 11 ticks in the middle band and reaches the dash pose
		 * 23 ticks after the key; playing `dash_start` as a transition instead runs all eight of its frames
		 * and took 47.
		 */
		static constexpr float LegacyDashAnimSpeed = 8.0f * LegacyFrameRateScale;

		/**
		 * @brief Ticks one frame of a ground-run animation lasts at a standstill, before the speed is taken off it
		 *
		 * The original does not play the run animations at a rate of their own --- it plays them at a rate set
		 * by how fast the player is moving, and a frame lasts @cpp max(1, 12 - |speed|) @ce of its ticks. The
		 * assets say nothing about this: `run.aura`, `dash_start.aura` and `dash.aura` all carry a flat 0.5 s
		 * for Jazz and Spaz, so the rate is in the game's code and nowhere else.
		 *
		 * Measured across every scenario in the committed original traces, which agree on both characters:
		 * 10 ticks per frame at speed 2, 9 at 3, 8 at 4, 3 at 9, 2 at 10 and 1 from 11 up. The two ends are the
		 * ones that can be checked against a whole loop rather than a single frame, and both land exactly ---
		 * walking, 8 frames at 8 ticks is the 63-tick loop `g_walk` shows; dashing, 4 frames at 1 tick is the
		 * 4-tick loop `g_dash` shows. This engine used the animation's own duration with no speed term at all,
		 * which made the walk 1.5x too fast and the dash **4.3x too slow**.
		 *
		 * @ref LegacyRunAnimMinFrameTicks is the floor, and it is a real one rather than a safety clamp: the
		 * original cannot show two frames in one tick, so every speed from 11 up gives the same 1.
		 */
		static constexpr float LegacyRunAnimFrameTicks = 12.0f;
		/** @brief The floor on @ref LegacyRunAnimFrameTicks --- one frame per tick is all the original can draw */
		static constexpr float LegacyRunAnimMinFrameTicks = 1.0f;

		/**
		 * @brief The two-bit horizontal speed field of a composite animation state
		 *
		 * Used as a mask rather than as flags, because @ref AnimState::Dash is @ref AnimState::Walk |
		 * @ref AnimState::Run rather than a bit of its own - so testing for the dash with `&` finds the walk too.
		 */
		static constexpr AnimState HorizontalAnimMask = AnimState::Dash;
		/** @brief The vertical field beside it, @ref AnimState::Jump | @ref AnimState::Fall */
		static constexpr AnimState VerticalAnimMask = (AnimState)0x0000000C;

		/** @brief Not sliding to a halt - moving under the player's own steering, or not on the ground at all */
		static constexpr std::int32_t StopPhaseNone = 0;
		/** @brief `walk_stop`, played once for its own 12 ticks however fast the player was going */
		static constexpr std::int32_t StopPhaseSkid = 1;
		/** @brief `dash_stop`, looping until the speed falls below @ref StopSettleSpeed */
		static constexpr std::int32_t StopPhaseSlide = 2;
		/** @brief `run_stop`, played once and followed by standing, even after the player has stopped */
		static constexpr std::int32_t StopPhaseSettle = 3;
		/** @brief The skid held on its last frame, for a slide too slow to have reached @ref StopPhaseSlide */
		static constexpr std::int32_t StopPhaseHold = 4;
		/** @} */

		/**
		 * @brief How long each half of the vine's idle cycle lasts
		 *
		 * Hanging still on a vine is not a static pose in the original: it alternates `vine_idle` with
		 * `vine_idle_flavor` for ever, 70 ticks each. Measured on `ob_vine_low`, which hangs for 800 ticks
		 * and goes round six times, changing at 28/96/165/236/305/376/445/516/585/656/725. The flourish
		 * *loops* inside its half --- it is 40 ticks long for Jazz and runs nearly twice --- so the halves
		 * being equal is the rule rather than the animation's own length.
		 *
		 * Like the stop chain this is shared with Reforged: the sprite is exported for all three characters
		 * and was read by nothing, so there is no Reforged behaviour to preserve.
		 */
		static constexpr float LegacyHookIdleCycle = 70.0f / LegacyFrameRateScale;
		/**
		 * @brief How long the player stands still before a bored idle animation plays (original 140 ticks)
		 *
		 * The same mechanic as @ref LegacyHookIdleCycle one state up, at exactly twice the period, and this
		 * engine already had everything for it --- `IdleBored1`..`IdleBored5` and
		 * @ref AnimState::TransitionIdleBored --- with the threshold set to **600 frames**, which is 700 of
		 * the original's ticks. The original plays one every 140. Three long scenarios agree on it to the
		 * tick: `bl_acc_right`, `bl_right` and `tb_right` all hold the idle pose for 141 sampled ticks
		 * between flourishes, and the flourishes themselves run 104, 136, 189 and 216 --- different lengths
		 * because there are several animations and one is picked at random, exactly as here.
		 *
		 * `g_stand` is the clearest case of what the old figure cost: 250 ticks of standing still, the
		 * original plays a flourish at 196 and this engine never played one at all, because a 250-tick
		 * scenario cannot reach 700.
		 *
		 * Gated, unlike the vine cycle: 600 frames is a deliberate Reforged behaviour rather than an
		 * animation nothing read.
		 */
		static constexpr float LegacyIdleBoredTime = 140.0f / LegacyFrameRateScale;
		/**
		 * @brief Downward speed the original starts a vine drop at (original 4 px/tick)
		 *
		 * Letting go of a vine with Down is not a release into free fall: the original **assigns** a speed
		 * and the ordinary fall gravity takes it from there. Measured on `ob_vine_drop`, which hangs on the
		 * low vine until it has certainly settled and then lets go --- two ticks later it reads 4.125, and
		 * every tick after that adds exactly one @ref LegacyFallGravity, so the assignment is 4.0 and nothing
		 * else about the fall is special.
		 *
		 * This engine released into a standing start: 0.17 on the first tick and 48 ticks to reach the floor
		 * against the original's **16**. Reported as "very slow acceleration when dropping off a vine", and
		 * it is not an acceleration difference at all --- both accelerate at the same rate, one of them just
		 * begins four pixels a tick ahead.
		 */
		static constexpr float LegacyVineDropSpeed = 4.0f * LegacyFrameRateScale;
		/**
		 * @brief How far the player is lifted on the tick they jump off a vine or hook (original 12 px)
		 *
		 * A distance rather than a speed, so it is **not** scaled to this engine's frame rate --- the original
		 * moves the player this far once, on the tick the key is read, and every tick after that moves the 8 px
		 * @ref LegacyAppliedSpeedCap allows. Measured on `ob_vine_up`, where three grabs at three different
		 * heights all show it: 1443 to 1431, 1437 to 1425, and 1299 to 1287.
		 *
		 * It is worth about 10 px of rise, which does not sound like much until a level stacks two vines four
		 * tiles apart --- the original clears the upper one with a pixel to spare and this engine, launching
		 * from the same place without the lift, stopped 13 px under it and could never climb the pair. That is
		 * the reported "the player cannot reach the second vine".
		 *
		 * An earlier reading concluded there was no lift at all, from `ob_vine_low` going 1458 to 1438 over two
		 * ticks and being taken for two moves of a plain -10 jump. It is not: under the applied cap a plain
		 * jump moves 8 + 8 and lands at 1442. Only 12 + 8 reaches 1438. Two rates that co-vary again --- see
		 * the note on fitting in `Tests/README.md`.
		 */
		static constexpr float LegacyVineJumpLift = 12.0f;
		/**
		 * @brief Fastest descent the copter can still be *started* from (original ~2 px/tick)
		 *
		 * The copter's descent speed was never the difference --- where the original engages, it holds
		 * 1.0078 px/tick, which is @ref LegacyCopterDescentSpeed to three decimals. What it has and this
		 * engine did not is an **upper bound on engaging at all**: you can only start it early in a fall,
		 * before you are going too fast. This engine's test was @cpp > 0.01f @ce with no ceiling, so it
		 * engaged at any speed and pinned every descent at 1.0 --- which is exactly the reported "static
		 * here, variable in the original", seen from the wrong end.
		 *
		 * Measured with `cp_tap55`..`cp_tap80`, five scenarios sharing one jump and differing only in when
		 * the tapping starts. All five rise at tick 46 and begin falling at 61. Taps starting at 55 and 60
		 * are refused because the player is still rising; 65 engages 4 ticks into the fall, 70 at 9,
		 * `sp_jazz_copter` at 15 --- and 80, nineteen ticks in, is **refused**. That brackets the bound
		 * between the 1.875 px/tick that engages and the 2.375 that does not; 2.0 is the round figure
		 * between them and is what is used, but only the bracket is measured.
		 */
		static constexpr float LegacyCopterEngageMaxSpeed = 2.0f * LegacyFrameRateScale;
		/**
		 * @brief How often Lori's kick repeats while Down and Jump are held (original 35 ticks)
		 *
		 * Start to start, and it does not vary: `sp_lori_side_hold` holds both keys for the whole scenario
		 * and the original kicks at ticks 41, 76, 111, 146, 181 ... 566, twenty-two times in 800 with every
		 * gap exactly 35. The kick itself is the first 13 of those and the remaining 22 are the pause.
		 *
		 * The 35 is not an arbitrary timer: it is the **length of her kick animation**, nine frames at about
		 * four ticks each, which runs once per kick and starts over on the next. The drive is its first four
		 * frames and the rest is recovery. Worth knowing before this is retuned - moving the number without
		 * moving the animation would put the two out of step.
		 *
		 * It needs a timer because the repeat is **not** driven by pressing again. This engine already
		 * re-kicked on a fresh press, which is why a *tapped* Down+Jump looked right, and with the keys
		 * simply held there is no second rising edge: one kick of 204.75 px and then nothing for the
		 * remaining 735 ticks, against the original's 3636.75 px - which is the far wall.
		 *
		 * One kick travels the same distance in both @m_span{m-text m-dim} (204.75 against 204.5) @m_endspan
		 * so only the repeat was missing; the ramp inside it is @ref LegacyLoriKickRamp and was already right.
		 */
		static constexpr float LegacyLoriKickPeriod = 35.0f / LegacyFrameRateScale;
		/**
		 * @brief How long the wind-up loop runs between the two animations in a light launch
		 *
		 * The three-tap case is a whole rev-up in miniature - start animation, a little of the loop, end
		 * animation - rather than the two transitions back to back. Without the middle the loop never shows and
		 * the player reads as flicking straight from one pose to the other.
		 *
		 * The player stands still for the whole of it, which is also the answer to "0 speed at the start
		 * animation and about fifteen frames after": the three phases together come to about twenty, and the
		 * launch lands exactly as the end animation finishes rather than leaving a gap of idle behind it. Only
		 * the light case waits at all - the full wind-up's launch is measured to move on the very tick its end
		 * animation begins.
		 */
		static constexpr float RevUpLightMidTime = 10.0f;

		void UpdateAnimation(float timeMult);
		// Which of the three sliding-to-a-halt poses the current speed calls for, 0 for none
		bool IsSlidingToHalt();
		void UpdateHookIdleAnimation(float timeMult, AnimState newState);
		void IssueHookIdleFlavor();
		// Whether whatever transition is playing has to be taken off the screen for a shot to be seen. The
		// four *ShootTo* returns are the point of it: each is issued **non-cancellable** when a shot ends, so
		// firing again before one finishes leaves the *end* animation drawn over the new shot.
		bool ShouldCancelTransitionOnFire() const;
		void EnterStopPhase(std::int32_t phase);
		void ParkStopPose();
		void OnStopPoseFinished();
		void ApplyStopAnimation(bool sliding);
		void PushSolidObjects(float timeMult);
		void CheckEndOfSpecialMoves(float timeMult);
		void CheckSuspendState(float timeMult);
		void OnUpdatePhysics(float timeMult);
		void OnUpdateTimers(float timeMult);
		void OnHandleMovement(float timeMult, bool areaWeaponAllowed, bool canJumpPrev);
		void HandleHorizontalMovement(float timeMult);
		// Arms Lori's kick. Separate because she has no wind-up outside Reforged and it fires at the trigger
		void BeginLoriKick();
		// The whole trigger - pose, transition and kick - shared by the press and the timed repeat so the two
		// cannot drift
		void TriggerLoriKick();
		// Counts down LegacyLoriKickPeriod and kicks again if Down and Jump are still *held*
		void UpdateLoriKickRepeat(float timeMult);
		// Plays the ground-run animations at the rate the original takes from the player's speed rather than at
		// the animation's own - see LegacyRunAnimFrameTicks. Non-Reforged only, and last in UpdateAnimation()
		// because it has to see the final transition state for this frame.
		void UpdateLegacyRunAnimSpeed();
		void HandleWaterAndModifierMovement(float timeMult);
		void HandleLookupAndCrouch(float timeMult, bool canJumpPrev);
		void HandleJump(float timeMult);
		// The launch an ordinary jump from the ground performs, shared with the one-way relaunch below so the
		// two cannot drift apart
		void BeginStandardJump(float timeMult);
		// Whether a one-way platform is under the feet while the player is rising through it. The original
		// still counts that as ground for the jump test, so a held jump re-launches at every platform of a
		// ladder; this engine's `ActorState::CanJump` cannot answer it, because it is cleared for any rising
		// actor and also picks the ground-bound slope path. Non-Reforged only.
		bool IsOnLegacyOneWayFloor();
		// Whether there is ground of any kind - solid or one-way - directly under the feet, asked of the
		// tileset rather than of `ActorState::CanJump`. Needed wherever that flag has been cleared by hand
		// and so cannot answer: Spaz's dash clears it for its whole duration (see the sidekick trigger),
		// which is exactly when the kick-out jump has to decide whether it is over floor or over a gap.
		bool HasLegacyFloorBelow();
		// Performs that relaunch. Called after the move rather than from HandleJump(), because the original
		// asks the question of the position the move ended on - see the comment on the definition.
		void TryLegacyOneWayRejump(float timeMult);
		// Counts Run taps made standing still, shows the wind-up, and launches the player when the tapping stops
		void UpdateRevUp(float timeMult);
		// Whether the rev-up pose should be showing. Read by UpdateAnimation(), which is where the pose has to
		// be decided - see the comment there.
		bool IsRevvingUp() const;
		// Abandons a wind-up without launching, and puts the animation speed back. Jumping and crouching both
		// do this: the launch is what letting go of Run is for.
		void CancelRevUp();

	public:
		/** @brief Clears all run-in-place rev-up state, so it cannot survive a teleport or a probe reset */
		void ResetRevUpState();

	private:
		// Throws sparks backwards off the feet while revving up
		void EmitRevUpSparks(float charge);
		// Puts the stored launch speed into effect, with the no-friction window and the grace that ends it
		void ApplyRevUpLaunch();
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
		/**
		 * @brief Whether a crouch-launched special move may start this frame
		 *
		 * The crouch has to be **established before** Jump outside Reforged. Measured by holding Down and
		 * Jump together for 8, 16 and 24 ticks: the original rises 8.0 px in all three, exactly as a tapped
		 * jump does, and 132.0 with Jump held indefinitely, which is a plain standing jump. Only when Down
		 * is established first - `sp_upper_downhold`, twenty ticks early - does it give the 201.5 px
		 * uppercut. So it is a **gate** and not the strength curve the three earlier readings suggested;
		 * those compared a tap against a hold and read the difference as move strength when it is only how
		 * long the jump key was down.
		 */
		bool IsSpecialMoveCrouchReady() const;
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