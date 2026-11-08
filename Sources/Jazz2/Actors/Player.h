#pragma once

#include "ActorBase.h"
#include "../LevelInitialization.h"
#include "../ShieldType.h"
#include "../SuspendType.h"
#include "../WarpFlags.h"
#include "../WeaponDefinition.h"

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
		
		The rabbit (Jazz, Spaz or Lori) that runs, jumps, fires and collects items. It can also be morphed
		into temporary forms such as the frog or the bird.
	*/
	class Player : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

		friend class UI::HUD;
#if defined(WITH_PHYSICS_PROBE)
		// Reads the state that gates the special moves
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
		 * @brief Returns the rank of a @ref InvulnerableType
		 *
		 * @ref GrantInvulnerability() never replaces a running effect with a lower-ranked one.
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
		 * @brief Constant per-character properties
		 *
		 * Shared by all players of the same @ref PlayerType. Behavioral differences stay in code.
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

		/** @brief Collected items and weapon loadout of a player */
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

		/** @brief Returns `true` while morphed into either of the two birds */
		bool IsBird() const {
			return (_playerType == PlayerType::Bird || _playerType == PlayerType::Bird2);
		}

		/** @brief Returns `true` for temporary forms (the frog and both birds) */
		static bool IsTemporaryForm(PlayerType type) {
			return (type == PlayerType::Frog || type == PlayerType::Bird || type == PlayerType::Bird2);
		}

		/**
		 * @brief Returns the player type to carry over to the next level or to store
		 *
		 * A bird is never carried over, so the character it was taken from is returned instead.
		 */
		PlayerType GetPersistentPlayerType() const {
			return (IsBird() ? _playerTypeBeforeMorph : _playerType);
		}

		/** @brief Returns the character a temporary form was taken from, or the current type */
		PlayerType GetPlayerTypeBeforeMorph() const {
			return (IsTemporaryForm(_playerType) ? _playerTypeBeforeMorph : _playerType);
		}

		/** @brief Returns the character the player started the level with */
		PlayerType GetOriginalPlayerType() const {
			return _playerTypeOriginal;
		}

		/**
		 * @brief Returns the fur color to actually use for this player
		 *
		 * Returns 0 if recoloring is disabled by preferences or while morphed into a bird.
		 */
		std::uint32_t GetEffectiveFurColor() const;
		/** @brief Returns the player's offset into the shared palette texture, or -1 if not recolored */
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
		
		/** @brief Returns the weapon the player holds */
		WeaponType GetCurrentWeapon() const {
			return _currentWeapon;
		}

		/** @brief Returns `true` if sugar rush is active */
		bool HasSugarRush() const {
			return (_sugarRushLeft > 0.0f);
		}

		/** @brief Returns `true` if the player can jump */
		bool CanJump() const;
		/** @brief Returns `true` if the player can bread solid objects */
		bool CanBreakSolidObjects() const;
		/** @brief Returns `true` if a buttstomp is still in its start transition */
		bool IsButtstompStarting() const;
		/** @brief Returns `true` if the player runs into a frozen @p target fast enough to break it */
		bool IsRammingFrozen(const ActorBase* target) const;
		/** @brief Returns `true` if the player can move vertically, i.e. not affected by gravity */
		bool CanMoveVertically() const;
		/** @brief Returns `true` if the player is currently in water */
		bool IsInWater() const;
		/** @brief Returns `true` if continuous jump is allowed */
		virtual bool IsContinuousJumpAllowed() const;
		/** @brief Returns `true` if ledge climbing is allowed */
		bool IsLedgeClimbAllowed() const;
		/**
		 * @brief Returns how far the non-Reforged camera should lead the player horizontally, in pixels
		 *
		 * Zero unless a direction is held, the original follows the input rather than the speed.
		 */
		float GetCameraLookAhead() const;
		/**
		 * @brief Returns how far the non-Reforged camera should drop below the player, in pixels
		 *
		 * Zero except during a buttstomp. Matches the original.
		 */
		float GetCameraVerticalOffset() const;

		/** @brief Called when the level is about to change */
		virtual bool OnLevelChanging(Actors::ActorBase* initiator, ExitType exitType);
		/** @brief Called at the beginning of the next level to reveive carry over information */
		virtual void ReceiveLevelCarryOver(ExitType exitType, const PlayerCarryOver& carryOver);
		/** @brief Returns current carry over information */
		virtual PlayerCarryOver PrepareLevelCarryOver();
		/**
		 * @brief Called when the player returns to a restored level to receive carry over information
		 *
		 * Applies only the progress made in the level it's returning from. The exit it stands on is ignored
		 * until the player steps off it.
		 */
		void ReceiveReturnCarryOver(ExitType exitType, const PlayerCarryOver& carryOver);
		/** @brief Initializes player state from a stream */
		void InitializeFromStream(ILevelHandler* levelHandler, Stream& src, std::uint16_t version);
		/** @brief Serializes player state to a stream */
		void SerializeResumableToStream(Stream& dest);
#if defined(WITH_EDITOR)
		/**
		 * @brief Takes the current state of another player, for a restarted play-test
		 *
		 * Unlike @ref SerializeResumableToStream(), takes the state as it is now, not as it was at the checkpoint.
		 */
		void TakeOverPlayTestState(const Player& other);
#endif

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
		/** @brief Sets copter flight duration and type, `timeLeft <= 0` disables it */
		void SetCopterFlight(float timeLeft, FlightType type = FlightType::Normal);
		/** @brief Enables or disables fly cheat behavior */
		virtual void EnableFlyCheat(bool active);
		/** @brief Takes damage */
		virtual bool TakeDamage(std::int32_t amount, float pushForce = 0.0f, bool ignoreInvulnerable = false);
		/** @brief Freezes the player for specified time */
		virtual bool Freeze(float timeLeft);
		/**
		 * @brief Sets invulnerability, replacing whatever is running
		 *
		 * Can also weaken it, so server corrections go through it. @cpp timeLeft <= 0 @ce clears it.
		 * Gameplay should use @ref GrantInvulnerability() instead.
		 */
		virtual void SetInvulnerability(float timeLeft, InvulnerableType type);
		/**
		 * @brief Grants invulnerability without weakening what is already running
		 *
		 * Keeps the longer time and the stronger effect.
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
		/**
		 * @brief Returns `true` if the player can morph to a given player type
		 *
		 * `false` if the character's sprites aren't available.
		 */
		bool CanMorphTo(PlayerType type);
		/**
		 * @brief Morphs to a given player type
		 *
		 * Returns `false` if the player already has the type or its sprites aren't available.
		 */
		virtual bool MorphTo(PlayerType type);
		/**
		 * @brief Changes the player type immediately, without any transition or effect
		 *
		 * Returns `false` if the player already has the type or its sprites aren't available.
		 */
		virtual bool MorphToInstantly(PlayerType type);
		/**
		 * @brief Reverts a morph
		 *
		 * A temporary form reverts to the character it was taken from, others to the starting character.
		 */
		void MorphRevert();
		/** @brief Sets duration of dizziness */
		virtual bool SetDizzy(float timeLeft);
		/**
		 * @brief Throws the player away from an RF blast
		 *
		 * Non-Reforged only, measured in the original.
		 *
		 * @param pushLeft      Whether the player is thrown left
		 * @param distanceSqr   Squared distance from the blast
		 * @return              `false` if the blast was too far away
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
		 * Shared by local and remote players.
		 *
		 * @param renderQueue           Render queue
		 * @param shieldType            Active shield type
		 * @param shieldTime            Remaining shield time, in frames
		 * @param metadata              Player's metadata
		 * @param elapsedFrames         Elapsed level frames
		 * @param pos                   World position
		 * @param baseLayer             Base render layer
		 * @param shieldRenderCommands  Render commands owned by the caller
		 */
		static void DrawShield(RenderQueue& renderQueue, ShieldType shieldType, float shieldTime, Metadata* metadata,
			float elapsedFrames, Vector2f pos, std::uint16_t baseLayer, std::unique_ptr<RenderCommand> (&shieldRenderCommands)[2]);
		/**
		 * @brief Spawns a single star of the sugar rush trail at a position
		 *
		 * Shared by local and remote players, every peer spawns its own stars.
		 *
		 * @param levelHandler  Level handler
		 * @param metadata      Player's metadata
		 * @param pos           World position
		 * @param baseLayer     Base render layer
		 */
		static void SpawnSugarRushStar(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos, std::uint16_t baseLayer);
		/**
		 * @brief Advances a remote player's run-in-place and returns its animation speed factor
		 *
		 * The charge depends only on how long the pose has been up, so it's reproduced locally without being sent.
		 *
		 * @param levelHandler   Level handler
		 * @param metadata       Player's metadata
		 * @param pos            World position
		 * @param baseLayer      Base render layer
		 * @param facingLeft     Whether the player faces left
		 * @param timeMult       Frame time
		 * @param revUpTime      Caller's accumulator for how long the pose has been up
		 * @param sparkCooldown  Caller's accumulator for the spark interval
		 * @return               Divisor for the pose's animation duration
		 */
		static float UpdateRemoteRevUp(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos,
			std::uint16_t baseLayer, bool facingLeft, float timeMult, float& revUpTime, float& sparkCooldown);
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
		/** @brief Records the ambient light intensity currently applied to this player */
		void SetCurrentAmbientLight(float value) {
			_currentAmbientLight = value;
		}
		/**
		 * @brief Overrides the ambient light intensity to restore when respawning at the checkpoint
		 *
		 * Used online, where checkpoints are activated by the server.
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
		enum class LevelExitingState : std::uint8_t {
			None,				/**< Not exiting */
			Waiting,			/**< Waiting before exiting */
			WaitingForWarp,		/**< Waiting for a warp to complete */
			Transition,			/**< Playing the exit transition */
			Ready				/**< Ready to exit */
		};

		/** @brief State of HUD weapon wheel */
		enum class WeaponWheelState : std::uint8_t {
			Hidden,				/**< Hidden */
			Opening,			/**< Opening */
			Visible,			/**< Visible */
			Closing				/**< Closing */
		};

		/** @brief Phase of the chain of poses a player sliding to a halt goes through */
		enum class StopPhase : std::uint8_t {
			None,				/**< Not sliding to a halt */
			Skid,				/**< `walk_stop`, played once */
			Slide,				/**< `dash_stop`, looping until the speed falls below @ref StopSettleSpeed */
			Settle,				/**< `run_stop`, played once and followed by standing */
			Hold				/**< The skid held on its last frame, for a slide too slow to reach @ref StopPhase::Slide */
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
		 * Ends the push animation even if the push grace timer hasn't expired yet.
		 */
		static constexpr float MaxPushingSpeed = 1.0f;

	public:
		/**
		 * @{ @name Non-Reforged movement constants
		 *
		 * Measured in the original and converted to this engine's 60 Hz baseline, velocities by
		 * @ref LegacyFrameRateScale and accelerations by its square.
		 */

		/** @brief Tick rate of the original game */
		static constexpr float LegacyTickRate = 70.0f;
		/** @brief Velocity scale from the original's ticks to this engine's 60 Hz baseline */
		static constexpr float LegacyFrameRateScale = LegacyTickRate / 60.0f;
		/** @brief Acceleration scale from the original's ticks to this engine's 60 Hz baseline */
		static constexpr float LegacyFrameRateScaleSqr = LegacyFrameRateScale * LegacyFrameRateScale;
		/** @brief Non-Reforged walk speed cap */
		static constexpr float LegacyWalkSpeed = 4.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged dash speed cap while Run is held */
		static constexpr float LegacyDashSpeed = 16.0f * LegacyFrameRateScale;
		/** @brief Horizontal speed above which running into a frozen object breaks it, in all modes */
		static constexpr float FrozenRamSpeed = 12.0f * LegacyFrameRateScale;
		/** @brief Vertical speed of the bounce off a broken frozen object */
		static constexpr float FrozenRamBounceSpeedY = -6.0f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged horizontal acceleration with no Run
		 *
		 * Applied in the direction of the input, on the ground and in the air alike, so turning around is symmetric.
		 */
		static constexpr float LegacyWalkAccel = (12000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged horizontal acceleration while Run is held */
		static constexpr float LegacyDashAccel = (24000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged deceleration with no direction held */
		static constexpr float LegacyWalkDecel = (8000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged deceleration with no direction held while the dash is active */
		static constexpr float LegacyDashDecel = (28000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged deceleration on a strength-0 slide tile, no direction held
		 *
		 * The walk and dash slide brakes don't share a ratio, so both are measured separately.
		 */
		static constexpr float LegacySlideWalkDecel = (4000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged deceleration on a strength-0 slide tile while the dash is active */
		static constexpr float LegacySlideDashDecel = (12000.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief How much each step of a slide tile's strength takes off @ref LegacySlideWalkDecel
		 *
		 * Linear in the strength. Measured in the original.
		 */
		static constexpr float LegacySlideWalkDecelStep = (800.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief The same step for @ref LegacySlideDashDecel */
		static constexpr float LegacySlideDashDecelStep = (2500.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief How long a sucker tube keeps hold of the player
		 *
		 * Measured in the original, but used in both modes. Re-armed every frame inside a tube tile, it suppresses
		 * horizontal friction but not gravity.
		 */
		static constexpr float TubeControlTime = 16.0f / LegacyFrameRateScale;
		/**
		 * @brief Where in its tile a horizontal sucker tube holds the player, from the top
		 *
		 * Measured in the original, used in both modes.
		 */
		static constexpr float TubeSnapY = 15.0f;
		/**
		 * @brief How long the sucker tube's ball pose stays after leaving the last tube tile
		 *
		 * Affects only the pose, not the movement. Measured in the original.
		 */
		static constexpr float TubeBallPoseTime = 35.0f / LegacyFrameRateScale;
		/**
		 * @brief How long the non-Reforged dash outlives the Run key
		 *
		 * The full @ref LegacyDashSpeed is kept until then, and clamped back to @ref LegacyWalkSpeed in a single frame.
		 */
		static constexpr float LegacyDashGraceTicks = 16.0f / LegacyFrameRateScale;
		/** @brief How far the non-Reforged camera leads the player while walking */
		static constexpr float LegacyCameraWalkLead = 28.0f;
		/** @brief How far the non-Reforged camera leads the player while dashing, keyed on @ref IsDashActive() */
		static constexpr float LegacyCameraDashLead = 120.0f;
		/**
		 * @brief How far the non-Reforged camera drops during a buttstomp
		 *
		 * The only vertical camera lead in the original. Measured in the original.
		 */
		static constexpr float LegacyCameraButtstompDrop = 60.0f;
		/**
		 * @brief Ticks the non-Reforged landing pose spends on each of its frames
		 *
		 * Deliberately slower than the original, chosen by eye. Per frame, because Lori's landing has more
		 * frames than Jazz's and Spaz's.
		 */
		static constexpr float LandAnimFrameTicks = 3.85f;
		/**
		 * @brief How much faster the sidekick's ending animation plays, in both modes
		 *
		 * Deliberately faster than the original, the pose looked too slow in play.
		 */
		static constexpr float SidekickEndSpeedUp = 2.0f;
		/**
		 * @brief Extra launch speed of a jump out of a run-in-place, about a tile of height
		 *
		 * Fitted, not measured, because the wind-up can't be driven from a script.
		 */
		static constexpr float LegacyRevUpJumpBoost = 3.70f;

		/** @brief Non-Reforged jump launch speed from a standstill */
		static constexpr float LegacyJumpSpeed = 10.0f * LegacyFrameRateScale;
		/** @brief Extra non-Reforged launch speed per unit of horizontal speed */
		static constexpr float LegacySpeedJumpScale = 0.25f;
		/** @brief Non-Reforged rise gravity while the jump key is held */
		static constexpr float LegacyRiseGravity = 0.375f * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged rise gravity once the jump key is released
		 *
		 * Releasing jump makes the rest of the rise heavier instead of clamping the speed. It eases off near
		 * the apex, see @ref LegacyRiseGravityReleasedNearApex.
		 */
		static constexpr float LegacyRiseGravityReleased = 0.875f * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged released rise gravity below @ref LegacyRiseBrakeEaseSpeed */
		static constexpr float LegacyRiseGravityReleasedNearApex = 0.625f * LegacyFrameRateScaleSqr;
		/** @brief Rise speed below which the non-Reforged released rise eases off */
		static constexpr float LegacyRiseBrakeEaseSpeed = 1.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged fall gravity */
		static constexpr float LegacyFallGravity = 0.125f * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged underwater gravity
		 *
		 * Applied in HandleWaterAndModifierMovement(), because OnHandleWater() disables gravity for a swimming player.
		 */
		static constexpr float LegacyWaterGravity = (1024.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged cap on applied movement, on both axes
		 *
		 * The speed above the cap is momentum, it still counts wherever the speed itself is read (e.g., jump launch).
		 */
		static constexpr float LegacyAppliedSpeedCap = 8.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged applied upward-speed cap */
		static constexpr float LegacyRiseSpeedCap = LegacyAppliedSpeedCap;
		/**
		 * @brief Non-Reforged limit of internal speed, on either axis
		 *
		 * Changes only how long a launch rides @ref LegacyRiseSpeedCap. Only a sidekick is assigned above it.
		 */
		static constexpr float LegacyVerticalSpeedLimit = 32.0f * LegacyFrameRateScale;

		/**
		 * @{ @name Flying carrot
		 *
		 * Measured from hand-played recordings in the original, scripted input can't fly.
		 */
		static constexpr float LegacyFlyRiseAccel = 0.25f * LegacyFrameRateScaleSqr;
		/**
		 * @brief Deceleration of a climb once Up is released, until it reaches zero
		 *
		 * Different from @ref LegacyRiseGravityReleased. Measured in the original.
		 */
		static constexpr float LegacyFlyRiseBrake = 0.75f * LegacyFrameRateScaleSqr;
		/** @brief Deceleration of a climb while Jump is held */
		static constexpr float LegacyFlyRiseBrakeHeld = 0.25f * LegacyFrameRateScaleSqr;
		/**
		 * @brief Descent speed while Down is held
		 *
		 * Assigned outright, not accelerated towards.
		 */
		static constexpr float LegacyFlyDescentSpeed = 1.0625f * LegacyFrameRateScale;
		/** @brief Downward acceleration once the flight is actually falling, to @ref LegacyFlyTerminalSpeed */
		static constexpr float LegacyFlyFallAccel = 0.0625f * LegacyFrameRateScaleSqr;
		/** @brief Terminal speed of the flight's unpowered descent */
		static constexpr float LegacyFlyTerminalSpeed = 12.0f * LegacyFrameRateScale;
		/**
		 * @brief How long a ride on the lizard copter lasts outside Reforged
		 *
		 * A plain timer. The rest of the ride shares the flying carrot's model.
		 */
		static constexpr float LegacyLizardCopterDuration = 279.0f / LegacyFrameRateScale;
		/**
		 * @brief How long the flight lasts, practically until it is cancelled
		 *
		 * The original flies until a Fly Off area or a death.
		 */
		static constexpr float LegacyFlyDuration = 1.0e6f;
		/** @} */
		/**
		 * @brief Non-Reforged bounce off an enemy hit by a buttstomp
		 *
		 * Faster than the fall itself. The ascent decays at the released rise rate, see @ref GetGravityModifier().
		 */
		static constexpr float LegacyEnemyStompBounce = 13.0f * LegacyFrameRateScale;
		/** @brief How far below a ledge's surface the non-Reforged player may arrive and still land on it */
		static constexpr float LegacyLandingTolerance = 20.0f;
		/** @brief Non-Reforged terminal fall speed */
		static constexpr float LegacyFallSpeedCap = 12.0f * LegacyFrameRateScale;

		/** @brief Non-Reforged buttstomp descent speed, the same for all characters */
		static constexpr float LegacyButtstompSpeed = 10.0f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged sideways drift speed during a buttstomp, doubled with Run
		 *
		 * A flat rate applied through the whole move, bypassing the player's speed.
		 */
		static constexpr float LegacyButtstompDriftSpeed = (12000.0f / 65536.0f) * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged speed while pushing a solid object
		 *
		 * A flat rate, whatever the player was doing before.
		 */
		static constexpr float LegacyPushSpeed = 0.375f * LegacyFrameRateScale;
		/** @brief Non-Reforged vine climbing speed cap, doubled while Run is held */
		static constexpr float LegacyVineSpeed = 2.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged copter descent speed, the same for Jazz and Lori */
		static constexpr float LegacyCopterDescentSpeed = (129.0f / 128.0f) * LegacyFrameRateScale;
		/**
		 * @brief Rate the non-Reforged copter descent accelerates towards @ref LegacyCopterDescentSpeed
		 *
		 * Visible only when the copter is engaged with little vertical speed, a faster fall drops straight to it.
		 */
		static constexpr float LegacyCopterWindUp = (1.0f / 128.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief Rate copter ears set to rise brake a fall at, before the rise begins
		 *
		 * The rise then winds up only while Jump is held, so tapping hovers rather than climbs. Measured in JJ2+.
		 */
		static constexpr float LegacyCopterAscentBrake = (3.0f / 8.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief How long Jump has to be up before a press can double jump
		 *
		 * Tapping faster never double jumps. Measured in JJ2+.
		 */
		static constexpr float LegacyDoubleJumpKeyUpTime = 4.5f / LegacyFrameRateScale;
		/**
		 * @brief Wind-up of the uppercut and the sidekick, in seconds
		 *
		 * The same for both moves and every character in the original, transitions are played to fit it.
		 */
		static constexpr float LegacyGroundMoveWindUp = 14.5f / LegacyTickRate;
		/**
		 * @brief How close a floor has to be for copter ears moving sideways to drop onto it
		 *
		 * Not without a horizontal speed. Measured in JJ2+.
		 */
		static constexpr float LegacyCopterFloorSnap = 8.0f;
		/** @brief How long a press keeps copter ears going, measured in JJ2+ */
		static constexpr float LegacyCopterFlightTime = 70.0f / LegacyFrameRateScale;
		/**
		 * @brief How long copter ears outlive a landing they dropped onto
		 *
		 * A jump right after keeps them going. Measured in JJ2+.
		 */
		static constexpr float LegacyCopterLandGrace = 3.5f / LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged launch speed of Spaz's double jump
		 *
		 * A straight speed assignment, height is controlled by holding the key like the first jump.
		 */
		static constexpr float LegacyDoubleJumpSpeed = 8.0f * LegacyFrameRateScale;
		/**
		 * @brief How long Spaz's non-Reforged double jump stays available
		 *
		 * A timer started when the jump key is released, not a band of fall speeds. Measured in the original.
		 */
		static constexpr float LegacyDoubleJumpWindowTime = 30.0f / LegacyFrameRateScale;
		/**
		 * @brief Launch speed of Jazz's non-Reforged uppercut
		 *
		 * Taken from the distance travelled in the original, not its reported speed. Gravity is nearly off
		 * while it runs, see @ref LegacyUppercutGravity.
		 */
		static constexpr float LegacyUppercutSpeed = 6.59375f * LegacyFrameRateScale;
		/** @brief Gravity while a non-Reforged uppercut is driving */
		static constexpr float LegacyUppercutGravity = (2048.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		/** @brief How long a non-Reforged uppercut drives before ordinary gravity resumes */
		static constexpr float LegacyUppercutTicks = 30.0f / LegacyFrameRateScale;
		/**
		 * @brief Speed a non-Reforged RF blast throws the player at
		 *
		 * Held for @ref LegacyRFBlastHoldTicks with friction and direction keys suppressed, then it decelerates
		 * like after a sidekick. Measured in the original.
		 */
		static constexpr float LegacyRFBlastSpeed = 8.0f * LegacyFrameRateScale;
		/** @brief Upward kick a non-Reforged RF blast gives a player who is off the ground */
		static constexpr float LegacyRFBlastRiseSpeed = 12.0f * LegacyFrameRateScale;
		/**
		 * @brief Upward kick the same blast gives a player standing on the floor, at zero range
		 *
		 * Grows with the range, a linear fit of the measurement in the original.
		 */
		static constexpr float LegacyRFBlastLiftBase = 0.646f * LegacyFrameRateScale;
		/** @brief How much @ref LegacyRFBlastLiftBase grows per pixel of range */
		static constexpr float LegacyRFBlastLiftSlope = 0.1732f * LegacyFrameRateScale;
		/** @brief How long a non-Reforged RF blast holds the thrown speed before friction resumes */
		static constexpr float LegacyRFBlastHoldTicks = 30.0f / LegacyFrameRateScale;
		/**
		 * @brief How close to a non-Reforged RF blast the player has to be to be thrown by it
		 *
		 * Distance from the shot, not from the wall. No lower bound.
		 */
		static constexpr float LegacyRFBlastReach = 32.0f;
		/** @brief Non-Reforged sidekick launch speed for Spaz */
		static constexpr float LegacySpazSidekickSpeed = 16.0f * LegacyFrameRateScale;
		/**
		 * @brief Distance the driven part of Spaz's non-Reforged sidekick covers
		 *
		 * Afterwards the speed snaps to the walk cap and the ordinary deceleration stops him.
		 */
		static constexpr float LegacySpazSidekickDistance = 440.0f;
		/**
		 * @brief Remainder of a sidekick's distance that counts as none left
		 *
		 * Below float resolution of level coordinates, a smaller remainder could never be worked off and the
		 * kick would hang in mid-air.
		 */
		static constexpr float SidekickDistanceEpsilon = 1.0f / 64.0f;
		/**
		 * @brief Ticks a horizontal spring's launch speed is held before it clamps to @ref LegacyCarryExitRunSpeed
		 *
		 * Changes only the speed, not the travel, but a jump out of the launch takes its boost from it.
		 */
		static constexpr float LegacySpringHoldTicks = 4.0f;
		/** @brief Ticks a horizontal pole's carry lasts after a vertical spring that keeps the horizontal speed */
		static constexpr float LegacyVerticalSpringCarryTicks = 6.0f;
		/**
		 * @brief Ticks a spring launch's speed takes to rebuild after a double jump discards it
		 *
		 * The rebuild runs at the launch speed divided by this, up to @ref LegacyDashSpeed. Only a spring arms it.
		 */
		static constexpr float LegacySpringRebuildTicks = 32.0f;
		/**
		 * @brief How long after a warp lands control comes back outside Reforged
		 *
		 * The warp-out animation still plays, but control doesn't wait for it. Matches the original.
		 */
		static constexpr float LegacyWarpOutControlTime = 2.0f / LegacyFrameRateScale;
		/** @brief Distance one of Lori's non-Reforged kicks covers */
		static constexpr float LegacyLoriSidekickDistance = 204.75f;
		/**
		 * @brief Rate a blocked kick of Lori's spends @ref LegacyLoriSidekickDistance at, per frame
		 *
		 * A flat rate, so a blocked kick lasts as long as the original's.
		 */
		static constexpr float LegacyLoriKickPushDrain = LegacyLoriSidekickDistance * LegacyFrameRateScale / 12.0f;
		/**
		 * @brief Quadratic ramp of Lori's non-Reforged kick, per tick squared
		 *
		 * Her kick accelerates quadratically instead of starting at its peak. Measured in the original.
		 */
		static constexpr float LegacyLoriKickRamp = 0.25f * LegacyFrameRateScale * LegacyFrameRateScale * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged sidekick launch speed for Lori
		 *
		 * Her kick is the one move exempt from @ref LegacyAppliedSpeedCap, see @ref OnUpdatePhysics().
		 */
		static constexpr float LegacyLoriSidekickSpeed = 42.25f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged speed a vertical pole adds to the one the player arrived with
		 *
		 * So chained poles or a pole entered off a spring compound.
		 */
		static constexpr float LegacyPoleLaunchBonus = 15.625f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged speed a horizontal pole adds to the one the player arrived with
		 *
		 * Capped at @ref LegacyHPoleMaxLaunch. Measured in the original.
		 */
		static constexpr float LegacyHPoleLaunchBonus = 8.0f * LegacyFrameRateScale;
		/** @brief Ceiling on the non-Reforged horizontal pole launch */
		static constexpr float LegacyHPoleMaxLaunch = 20.0f * LegacyFrameRateScale;
		/** @brief How long the horizontal pole's launch carries the player */
		static constexpr float HPoleKeepRunningTime = 60.0f;
		/** @brief How long a pole's launch takes control away from the player */
		static constexpr float PoleLaunchControlTimeout = 4.0f;
		/**
		 * @brief How long the non-Reforged horizontal pole refuses the crouch after its launch
		 *
		 * Lasts as long as the pole's exit animation. Measured in the original.
		 */
		static constexpr float LegacyHPoleCrouchBlockTime = 35.0f / LegacyFrameRateScale;
		/**
		 * @brief Speed a non-Reforged float-up area holds the player at
		 *
		 * Assigned every frame inside the area, even to a grounded player. Measured in the original.
		 */
		static constexpr float LegacyFloatUpSpeed = 8.0f * LegacyFrameRateScale;
		/**
		 * @brief Fastest a non-Reforged player descends while inside a float-up area
		 *
		 * Caps the movement, not the speed, so it also slows a buttstomp. Measured in the original.
		 */
		static constexpr float LegacyFloatUpFallCap = 4.0f * LegacyFrameRateScale;
		/**
		 * @brief Non-Reforged swimming speed cap, doubled while Run is held
		 *
		 * Applies on both axes. The original tests the cap before adding, so the speed ends one step past it.
		 */
		static constexpr float LegacyWaterSpeed = 2.0f * LegacyFrameRateScale;
		/** @brief Non-Reforged horizontal swimming acceleration, the same with Run held */
		static constexpr float LegacyWaterAccelX = (1.0f / 16.0f) * LegacyFrameRateScaleSqr;
		/** @brief Non-Reforged vertical swimming acceleration, doubled while Run is held */
		static constexpr float LegacyWaterAccelY = (1.0f / 8.0f) * LegacyFrameRateScaleSqr;
		/**
		 * @brief Non-Reforged horizontal coasting under water with nothing held, as a share of the speed per tick
		 *
		 * Applied until the speed is below @ref LegacyWaterCoastStop. Measured in JJ2+.
		 */
		static constexpr float LegacyWaterCoast = 31.0f / 32.0f;
		/** @brief Horizontal speed under water below which the coasting stops at once */
		static constexpr float LegacyWaterCoastStop = 0.75f * LegacyFrameRateScale;
		/**
		 * @brief Pixels a non-Reforged wind area moves the player per unit of its strength
		 *
		 * Moves the position, not the speed.
		 */
		static constexpr float LegacyWindFactor = 0.5f * LegacyFrameRateScale;
		/**
		 * @brief Pixels a non-Reforged belt moves the player per unit of its strength
		 *
		 * Moves the position, not the speed. Twice @ref LegacyWindFactor, they don't share a strength.
		 */
		static constexpr float LegacyBeltFactor = 1.0f * LegacyFrameRateScale;
		/**
		 * @brief Speed a non-Reforged accelerating belt drives the player to, per unit of strength
		 *
		 * Unlike the other belts, it ramps the speed up and then holds it.
		 */
		static constexpr float LegacyAccBeltSpeed = 1.5f * LegacyFrameRateScale;
		/**
		 * @brief How much an accelerating belt adds per tick, per unit of strength
		 *
		 * Applied after the brake, otherwise the steady state falls one friction step short.
		 */
		static constexpr float LegacyAccBeltStep = 0.5f * LegacyFrameRateScaleSqr;
		/**
		 * @brief Strength a non-Reforged accelerating belt gains while Run is held
		 *
		 * Raises only the target speed, by a flat amount. Measured in the original.
		 */
		static constexpr std::int32_t LegacyAccBeltRunBonus = 8;
		/**
		 * @brief Speed a non-Reforged carry clamps to when it ends with Run held
		 *
		 * Leaving a sucker tube or an accelerating belt clamps the speed to this, or to @ref LegacyWalkSpeed
		 * with nothing held.
		 */
		static constexpr float LegacyCarryExitRunSpeed = 16.0f * LegacyFrameRateScale;

		// Every non-Reforged constant is the original's own, scaled only by the frame-rate conversion

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
		// Ordered widest first, flags last (bit-fields are initialized in the constructor). A `bool` among floats costs four bytes.

		// State of the run-in-place wind-up, reset as a whole
		struct RevUpState {
			// Accumulated charge, and how much the held press added (capped)
			float Charge = 0.0f;
			float HeldCharge = 0.0f;
			// Remaining tap window
			float WindowLeft = 0.0f;
			// Time wound up, scales the pose's speed
			float ChargeTime = 0.0f;
			// End animation countdown
			float EndLeft = 0.0f;
			// A light launch's wait and the speed it moves off at
			float LaunchLeft = 0.0f;
			float PendingSpeed = 0.0f;
			// Until a Run press may restart the wind-up sound
			float SfxCooldown = 0.0f;
			// Pitch of the next wind-up sound
			float SfxPitch = RevUpSfxMinPitch;
			// Presses so far
			std::int32_t Presses = 0;
			// Enough to launch / enough to show the wind-up pose; both latch, so charge decay doesn't undo them
			bool Armed = false;
			bool Wound = false;
			// Start transition already played; the looping pose is re-decided every frame and would re-trigger it
			bool Started = false;
		};

		// Rebuilds the recolor palette in the player's palette slot and selects it on the renderer
		void RefreshColorPalette();
		// Releases the player's palette offset back to the shared pool (if any)
		void ReleasePaletteOffset();

		ActorBase* _carryingObject = nullptr;
		std::shared_ptr<Environment::Bird> _spawnedBird;
		std::shared_ptr<ActorBase> _activeModifierDecor;
		// Enemy the beak charge hit last
		std::weak_ptr<ActorBase> _birdChargeLastHit;
		SmallVector<LightEmitter, 0> _trail;
		std::unique_ptr<RenderCommand> _weaponFlareCommand;
		std::unique_ptr<RenderCommand> _shieldRenderCommands[2];
#if defined(WITH_AUDIO)
		std::shared_ptr<AudioBufferPlayer> _copterSound;
		std::shared_ptr<AudioBufferPlayer> _airboardSound;
		std::shared_ptr<AudioBufferPlayer> _airboardTurnSound;
		std::shared_ptr<AudioBufferPlayer> _revUpSound;
		std::shared_ptr<AudioBufferPlayer> _weaponSound;
#endif

		InventoryState _inventory = {};
		InventoryState _inventoryCheckpoint = {};
		// Refreshed every frame and on morph
		CharacterProfile _characterProfile = CharacterProfile::GetDefault(PlayerType::None);
		RevUpState _revUp;
		Vector2f _checkpointPos;
		Vector2f _trailLastPos;
		Vector2i _lastPolePos;
		// Script callback tile last called, called again only after the player leaves it
		Vector2i _lastCallbackTile = Vector2i(INT32_MIN, INT32_MIN);

		std::int32_t _playerIndex = 0;
		std::int32_t _lives = 0, _score = 0;
		std::int32_t _gemsTotal[4] = {};
		// Packed fur color (one section per byte) and the palette offset in the shared palette texture (-1 = none)
		std::uint32_t _furColor;
		std::int32_t _paletteOffset = -1;
		std::int32_t _inShallowWater = -1;
		// Double jumps taken in this airtime
		std::int32_t _doubleJumpCount = 0;

		// `_speed.Y` before this frame's gravity; the non-Reforged double-jump window tests this, as the original does
		float _frameStartSpeedY = 0.0f;
		// Started when Jump is let go, the non-Reforged double jump is only accepted while it runs
		float _doubleJumpWindowLeft = 0.0f;
		// Re-armed by a pinball paddle every frame, keeps the tube ball pose; a timer since the paddle may update either side of the player
		float _onPinballPaddleTime = 0.0f;
		// Re-armed on every tube tile
		float _tubeBallPoseLeft = 0.0f;
		// The rise a script's sucker tube gave, kept against a ceiling while the tube lasts
		float _tubeHeldSpeedY = 0.0f;
		// Limit on holding that speed, so a ceiling can't keep the player for good
		float _tubeHeldTimeLeft = 0.0f;
		float _controllableTimeout = 0.0f;
		// Outside RevUpState because it carries over between wind-ups
		float _revUpSparkCooldown = 0.0f;
		// Position in the vine's idle cycle
		float _hookIdleTime = 0.0f;
		float _copterFramesLeft = 0.0f, _fireFramesLeft = 0.0f, _pushFramesLeft = 0.0f, _waterCooldownLeft = 0.0f;
		float _externalForceCooldown = 0.0f;
		float _springCooldown = 0.0f;
		// Horizontal spring launch speed, for rebuilding it after a double jump; 0 if not from a spring
		float _springLaunchSpeedX = 0.0f;
		// Per-frame acceleration of that rebuild, 0 when not running
		float _springRebuildAccel = 0.0f;
		// Remaining hold of a horizontal spring launch's speed
		float _springHoldLeft = 0.0f;
		// Remaining script ball (`jjPLAYER::ballTime`), carried uncapped like a pole launch
		float _ballCarryLeft = 0.0f;
		// Remaining landed copter counter, see `_copterCarried`
		float _copterLandGraceLeft = 0.0f;
		// Frames Jump has been up
		float _jumpUpTime = 1000.0f;
		// Copter flight time in this airtime
		float _copterAirTime = 0.0f;
		// Copter deaf window, re-armed on every airborne tick Jump is down; not `_jumpTime`, which also gates the double jump
		float _copterDeafLeft = 0.0f;
		// Ticks until Lori's kick repeats, 0 when none is pending
		float _loriKickRepeatLeft = 0.0f;
		float _checkpointLight = 1.0f;
		// Ambient light of this player, tracked here so it's known without a viewport (e.g. dedicated server)
		float _currentAmbientLight = 1.0f;
		float _sugarRushLeft = 0.0f, _sugarRushStarsTime = 0.0f;
		float _shieldSpawnTime = ShieldDisabled;
		float _gemsTimer = 0.0f;
		float _bonusWarpTimer = 0.0f;
		float _suspendTime = 0.0f;
		float _invulnerableTime = 0.0f;
		float _invulnerableBlinkTime = 0.0f;
		float _jumpTime = 0.0f;
		float _idleTime = 0.0f;
		float _hitFloorTime = 5.0f;
		float _keepRunningTime = 0.0f;
		/** @brief Remaining non-Reforged dash grace after Run was let go */
		float _dashGraceLeft = 0.0f;
		/** @brief Remaining non-Reforged sidekick distance, exact whatever the frame rate */
		float _sidekickDistanceLeft = 0.0f;
		/** @brief Time in the current non-Reforged sidekick, for Lori's ramp */
		float _sidekickTime = 0.0f;
		/** @brief Remaining RF blast hold of the horizontal speed, friction and input ignored */
		float _rfBlastLeft = 0.0f;
		/** @brief Remaining non-Reforged uppercut drive, gravity almost off */
		float _uppercutTimeLeft = 0.0f;
		float _lastPoleTime = 0.0f;
		float _inTubeTime = 0.0f;
		float _dizzyTime = 0.0f;
		float _activeShieldTime = 0.0f;
		float _weaponFlareTime = 0.0f;
		float _weaponCooldown = 0.0f;
		// Bird steering acceleration, wound up by a held direction and zeroed on release
		float _birdAccelX = 0.0f;
		// Time a bird has sat still on the floor (perches it), and time left of the current perched pose
		float _birdIdleTime = 0.0f;
		float _birdPoseTime = 0.0f;
		// Until the enemy the beak charge hit last can be hit again
		float _birdChargeHitCooldown = 0.0f;

		std::uint16_t _weaponFlareFrame = 0;

		ExitType _lastExitType = ExitType::None;
		PlayerType _playerType = PlayerType::Jazz, _playerTypeOriginal = PlayerType::Jazz;
		// Restored on respawn at the checkpoint
		PlayerType _checkpointPlayerType = PlayerType::Jazz;
		// Character a temporary form (frog or bird) was taken from, otherwise the current type
		PlayerType _playerTypeBeforeMorph = PlayerType::Jazz;
		SpecialMoveType _currentSpecialMove = SpecialMoveType::None;
		LevelExitingState _levelExiting = LevelExitingState::None;
		Modifier _activeModifier = Modifier::None;
		SuspendType _suspendType = SuspendType::None;
		ShieldType _activeShield = ShieldType::None;
		WeaponType _currentWeapon = WeaponType::Blaster;
		// Continuous weapon whose looping sound is playing, or WeaponType::Unknown; a dedicated server has no `_weaponSound`
		WeaponType _weaponSoundType = WeaponType::Unknown;
		WeaponWheelState _weaponWheelState = WeaponWheelState::Hidden;
		// Stopping chain progress, so a pose is issued once
		StopPhase _stopPhase = StopPhase::None;
		// Gem pickup pitch step, resets after a while without gems
		std::uint8_t _gemsPitch = 0;
		// Perched pose on screen (0-8), 0 for none yet
		std::int8_t _birdPose : 5;
		// Vertical direction of the beak charge (-1 up, 1 down, 0 level), applied next frame
		std::int8_t _birdChargeDirY : 3;

		bool _isActivelyPushing : 1, _wasActivelyPushing : 1;
		// Leaving an accelerating belt clamps the speed to the walk cap
		bool _wasOnAccBelt : 1;
		// Actually touching a wall or pushable object this physics step, unlike the `_pushFramesLeft` grace
		bool _pushContactThisFrame : 1;
		// A sidekick pushed a solid object this step (a plain wall doesn't count)
		bool _kickPushedThisFrame : 1;
		// A kick was pushing last step, so the object stops with it instead of coasting
		bool _kickWasPushing : 1;
		bool _controllable : 1;
		bool _controllableExternal : 1;
		bool _wasUpPressed : 1, _wasDownPressed : 1, _wasJumpPressed : 1, _wasFirePressed : 1, _isRunPressed : 1;
		// `_isRunPressed` seeded from the live key, so `ToggleRunAction` works with Run held at level start
		bool _runActionSeeded : 1;
		// Fire released since freezing, only then does holding it chip the ice away
		bool _fireReleasedWhileFrozen : 1;
		// Inside the damage of a death event or the pit
		bool _inDeathEvent : 1;
		// Wind-up sound is still ours to stop (not after a launch); a dedicated server has no `_revUpSound`
		bool _revUpSfxActive : 1;
		bool _isAttachedToPole : 1, _canPushFurther : 1;
		bool _isFreefall : 1, _inWater : 1, _isLifting : 1, _isSpring : 1;
		bool _flyCheatActive : 1;
		bool _inIdleTransition : 1, _inLedgeTransition : 1;
		// Stopping chain is swapping poses, so the outgoing callback isn't read as the pose running out
		bool _stopPhaseChanging : 1;
		// Vine idle flourish is the current transition, so its callback restarts it
		bool _inHookIdleFlavor : 1;
		// A jump carried the landed copter's counter into the air
		bool _copterCarried : 1;
		// Ascent launched by a jump (not a spring, pole, bounce or RF blast), only these react to a fresh press
		bool _riseOwnedByJump : 1;
		// This press fell outside the copter's deaf window, taken before the window is re-armed
		bool _copterPressAllowed : 1;
		// Lori's kick already spent its one allowed shot
		bool _loriKickShotUsed : 1;
		// Standing on top of another player; guards cancelling the carry so a real solid object is untouched
		bool _stackCarrying : 1;
		// Another player stands on this one (online, cosmetic lift animation; local co-op uses `_isLifting`)
		bool _beingStoodOn : 1;
		/**
		 * @brief Whether the carry @ref _keepRunningTime is counting down came out of a horizontal pole
		 *
		 * Such a launch isn't held to @ref LegacyAppliedSpeedCap, unlike a spring's.
		 */
		bool _hPoleCarry : 1;
		/**
		 * @brief Whether the carry @ref _keepRunningTime is counting down came out of a spring
		 *
		 * A spring's carry refuses the crouch for its whole length, a horizontal pole only for its exit animation.
		 */
		bool _springCarry : 1;
		/**
		 * @brief Whether the carry @ref _keepRunningTime is counting down came out of a run-in-place launch
		 *
		 * Refuses the crouch for the whole run, and the facing doesn't follow the direction of travel.
		 */
		bool _revUpCarry : 1;
		/** @brief Whether Jump was let go during the current non-Reforged ascent, making the rest heavier */
		bool _jumpReleased : 1;
		/** @brief Whether the held pole was reached off a spring, which picks its launch's rise gravity */
		bool _poleEnteredOnSpring : 1;
		/**
		 * @brief Whether the current non-Reforged ascent came out of a float-up area
		 *
		 * Its rise gravity follows the live jump key rather than @ref _jumpReleased.
		 */
		bool _riseFromFloatUp : 1;
		/**
		 * @brief Whether a float-up area was holding the speed at the last sample, which suppresses gravity
		 *
		 * Sampled at the end of the frame and read by the next frame's move.
		 */
		bool _inFloatUpArea : 1;
		/**
		 * @brief Whether the crouch was already up when this frame began
		 *
		 * Pressing Down and Jump together gives an ordinary jump, as in the original.
		 */
		bool _crouchHeldBefore : 1;
		bool _weaponAllowed : 1;
		// Bird is perched, and stands on the taller perch box (refused under a low ceiling)
		bool _birdPerched : 1;
		bool _birdStanding : 1;
		// Beak charge under way
		bool _birdCharging : 1;
		// Restored standing on the exit used, ignored until the player steps off it
		bool _levelExitSuppressed : 1;
		// A script turned the player's light off (`jjPLAYER.lightType` set to `LIGHT::NONE`)
		bool _lightHiddenByScript : 1;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnTileDeactivated() override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;
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
		 * @brief Pushes this player and @p other apart so they can't overlap
		 *
		 * Equal-mass elastic separation along the axis of least penetration.
		 *
		 * @param other            The other player in contact
		 * @param stackingEnabled  If `true`, vertical overlap is left to @ref UpdatePlayerStacking
		 * @return `true` if the players were separated
		 */
		bool ApplyPlayerBump(Player& other, bool stackingEnabled);
		/**
		 * @brief Lets this player stand on top of another player as on a one-way platform
		 *
		 * Call before the physics update so jump input sees it.
		 *
		 * @param timeMult  Frame time multiplier
		 * @param snap      Whether to move the feet onto the player below, `false` only grounds it
		 */
		void UpdatePlayerStacking(float timeMult, bool snap);

		/** @brief Reduces remaining shield time when a hit is absorbed (only if more than @p time remains) */
		virtual void DecreaseShieldTime(float time);

		/**
		 * @brief Called each frame while the player is in @ref PlayerType::Spectate mode
		 *
		 * By default, flies the invisible player around with the movement keys.
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

		// Player-vs-player bump tuning for local splitscreen co-op, mirrors PlayerOnServer
		static constexpr float PlayerBumpMaxSeparationPerFrame = 4.0f;
		static constexpr float PlayerBumpRestitution = 0.5f;
		static constexpr float PlayerBumpMinSeparationSpeed = 5.0f;
		// How far the upper player sinks into the one below, so a stack looks connected
		static constexpr float PlayerStackSinkDepth = 4.0f;

		// Run-in-place rev-up, tapping Run on the spot winds up and launches the player. Measured in the original
		// by hand; `RevUp*` rather than `Legacy*` because it applies in both modes.
		/**
		 * @{ @name Run-in-place wind-up accumulator
		 *
		 * The wind-up is entered on a decaying charge rather than a tap count. The shape is measured, the
		 * numbers are fitted to the boundary cases.
		 */
		/**
		 * @brief What one press is worth
		 *
		 * Three quick taps must never reach @ref RevUpChargeThreshold, only with the last one held.
		 */
		static constexpr float RevUpChargeGainTap = 3.4f;
		static constexpr float RevUpChargeGainHeld = 0.15f;
		/**
		 * @brief The most one press can add by being held
		 *
		 * Otherwise just holding Run on the spot would wind the player up.
		 */
		static constexpr float RevUpChargeHeldCap = 4.0f;
		// Loss per frame with the key up, slow enough for a tap to resume a wind-up
		static constexpr float RevUpChargeDecay = 0.15f;
		/**
		 * @brief Presses below which nothing happens at all, whatever the charge
		 *
		 * Two held presses could reach the threshold by charge alone.
		 */
		static constexpr std::int32_t RevUpPressesRequired = 3;
		/**
		 * @brief Enough to launch when the tapping stops, but not to show the wind-up
		 *
		 * The player stands idle meanwhile and launches on release.
		 */
		static constexpr float RevUpChargeLightThreshold = 9.0f;
		/** @brief Where the wind-up itself begins, above what three plain taps reach */
		static constexpr float RevUpChargeThreshold = 14.0f;
		/** @} */
		/**
		 * @brief How long a tap keeps the wind-up alive (original ~20 ticks)
		 *
		 * Below the threshold an expiry forgets the taps, above it an expiry is the launch.
		 */
		static constexpr float RevUpTapWindow = 18.0f;
		/**
		 * @brief How long the wind-up takes to reach full charge (original ~90 ticks)
		 *
		 * Charge depends on time spent revving, not on the tap count.
		 */
		static constexpr float RevUpFullChargeTime = 77.0f;
		// Launch speed at no and full charge, the latter being the dash cap
		static constexpr float RevUpMinLaunchSpeed = 8.1667f;
		static constexpr float RevUpMaxLaunchSpeed = 18.6667f;
		/**
		 * @brief Share of a full run-in-place launch each buttstomp out of the run costs
		 *
		 * Derived from the reported "about 4 buttstomps from a full stop", not measured.
		 */
		static constexpr float LegacyRevUpButtstompCostFraction = 0.25f;
		/**
		 * @brief How long the launch speed is held before it drops to the walk cap (original ~320 ticks)
		 *
		 * A flat hold ignoring steering, then a single-tick snap to the walk cap.
		 */
		static constexpr float RevUpKeepRunningTime = 274.0f;
		/** @brief How much faster the wind-up pose runs at full charge, fitted to the original */
		static constexpr float RevUpAnimSpeedUp = 6.0f;
		// Frames between floor sparks at the slowest wind-up, scaled with the pose's speed
		static constexpr float RevUpSparkInterval = 6.8f;
		// Where a spark leaves the player, relative to the feet
		static constexpr float RevUpSparkOffsetY = -4.0f;
		static constexpr float RevUpSparkOffsetBack = 4.0f;
		// The wind-up sound plays on every press, so it's quieter
		static constexpr float RevUpSfxGain = 0.5f;
		// Wind-up sound pitch, raised a step on every Run press once wound up
		static constexpr float RevUpSfxMinPitch = 0.9f;
		static constexpr float RevUpSfxMaxPitch = 1.04f;
		static constexpr float RevUpSfxPitchStep = 0.006f;

		/**
		 * @brief How long after jumping off a vine another can be grabbed
		 *
		 * Kept short, the original re-grabs a different vine three tiles higher soon after.
		 */
		static constexpr float VineDropCooldown = 6.0f;
		/**
		 * @brief How long the copter is deaf for after the jump key was last down (original 11 to 14 ticks)
		 *
		 * Re-armed on every airborne tick the key is down, so a tap train holds the copter off for the whole
		 * airtime. Separate from @cpp _jumpTime @ce, the double jump doesn't wait for it.
		 */
		static constexpr float LegacyJumpCooldown = 12.5f / LegacyFrameRateScale;

		/**
		 * @brief How long the rev-up's end animation holds the pose (original 6 ticks)
		 *
		 * The base state stays @ref AnimState::RevUp meanwhile, so a running pose doesn't cancel it.
		 */
		static constexpr float RevUpEndTime = 5.2f;
		/** @brief How long the rev-up's start animation holds the pose (original 4 to 8 ticks) */
		static constexpr float RevUpStartTime = 4.0f;

		/**
		 * @{ @name Sliding to a halt
		 *
		 * `walk_stop` plays once, `dash_stop` loops until the speed falls below this, then `run_stop` plays
		 * once. Measured in the original and shared with Reforged, since it's only an animation.
		 */
		static constexpr float StopSettleSpeed = 1.0f * LegacyFrameRateScale;

		/**
		 * @brief Speed above which the dash animation is shown
		 *
		 * `run` up to the walk cap, `dash_start` from there to this, `dash` above it. Measured in the original.
		 */
		static constexpr float LegacyDashAnimSpeed = 8.0f * LegacyFrameRateScale;

		/**
		 * @brief Ticks one frame of a ground-run animation lasts at a standstill
		 *
		 * A frame lasts @cpp max(1, 12 - |speed|) @ce ticks, as in the original, not the animation's own rate.
		 */
		static constexpr float LegacyRunAnimFrameTicks = 12.0f;
		/** @brief The floor on @ref LegacyRunAnimFrameTicks, one frame per tick */
		static constexpr float LegacyRunAnimMinFrameTicks = 1.0f;
		/**
		 * @brief Fastest the non-Reforged spring pose runs (original 2 ticks per frame)
		 *
		 * The launch pose follows the same @cpp 12 - speed @ce rule off the vertical speed, with its own floor and ceiling.
		 */
		static constexpr float LegacySpringAnimMinFrameTicks = 2.0f;
		/** @brief Slowest the non-Reforged spring pose runs (original 8 ticks per frame) */
		static constexpr float LegacySpringAnimMaxFrameTicks = 8.0f;

		/**
		 * @brief The two-bit horizontal speed field of a composite animation state
		 *
		 * A mask, because @ref AnimState::Dash is @ref AnimState::Walk | @ref AnimState::Run.
		 */
		static constexpr AnimState HorizontalAnimMask = AnimState::Dash;
		/** @brief The vertical field beside it, @ref AnimState::Jump | @ref AnimState::Fall */
		static constexpr AnimState VerticalAnimMask = (AnimState)0x0000000C;
		/** @} */

		/**
		 * @brief How long each half of the vine's idle cycle lasts
		 *
		 * Hanging on a vine alternates `vine_idle` and `vine_idle_flavor` forever, as in the original. Shared with Reforged.
		 */
		static constexpr float LegacyHookIdleCycle = 70.0f / LegacyFrameRateScale;
		/** @brief How long the player stands still before a bored idle animation plays (original 140 ticks) */
		static constexpr float LegacyIdleBoredTime = 140.0f / LegacyFrameRateScale;
		/**
		 * @brief Downward speed a vine drop starts at (original 4 px/tick)
		 *
		 * Assigned on release, then ordinary fall gravity applies.
		 */
		static constexpr float LegacyVineDropSpeed = 4.0f * LegacyFrameRateScale;
		/**
		 * @brief How far the player is lifted on the tick they jump off a vine or hook (original 12 px)
		 *
		 * A one-off distance, so not scaled to the frame rate. Needed to reach vines stacked four tiles apart.
		 */
		static constexpr float LegacyVineJumpLift = 12.0f;
		/**
		 * @brief Fastest descent the copter can still be started from
		 *
		 * Fitted to this engine rather than the original (2.3125), because the standing jump is about 3.5 px
		 * short. If that is fixed, put it back to 2.3125.
		 */
		static constexpr float LegacyCopterEngageMaxSpeed = 2.40625f * LegacyFrameRateScale;
		/**
		 * @brief How often Lori's kick repeats while Down and Jump are held (original 35 ticks)
		 *
		 * Start to start, the length of her kick animation, so keep the two in step.
		 */
		static constexpr float LegacyLoriKickPeriod = 35.0f / LegacyFrameRateScale;
		/**
		 * @brief How long the wind-up loop runs between the two animations in a light launch
		 *
		 * The player stands still for all of it and launches as the end animation finishes.
		 */
		static constexpr float RevUpLightMidTime = 10.0f;

		// Fastest a bird flies outside Reforged, on either axis; Reforged uses its own walk cap
		static constexpr float LegacyBirdSpeed = 4.0f * LegacyFrameRateScale;
		// Bird flight below is measured in the original and used in both modes
		//
		// Acceleration of a held direction once wound up, Run makes no difference
		static constexpr float BirdAcceleration = (8192.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		// Time to wind up the acceleration from nothing or to reverse it, reset on release
		static constexpr float BirdAccelRampTime = 4.0f / LegacyFrameRateScale;
		// Linear deceleration with no direction held
		static constexpr float BirdBrake = (4096.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		// Vertical speed Up or Jump assigns to a bird
		static constexpr float BirdClimbSpeed = 1.0f * LegacyFrameRateScale;
		// Pull on a bird with nothing vertical held, and with Down held
		static constexpr float BirdSinkGravity = (1024.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		static constexpr float BirdDiveGravity = (1536.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		// Knockback of a hurt bird (backwards and up), and extra deceleration while still rising
		static constexpr float BirdHurtKick = 4.0f * LegacyFrameRateScale;
		static constexpr float BirdHurtRiseGravity = (16384.0f / 65536.0f) * LegacyFrameRateScaleSqr;
		// Time sitting still on the floor before a bird perches
		static constexpr float BirdPerchTime = 37.0f / LegacyFrameRateScale;
		// Share of the flight cap the first bird must exceed for Fire to start its beak charge
		static constexpr float BirdChargeMinSpeedRatio = 0.5f;
		// Delay before the beak charge can hurt the same enemy again (Reforged only, the original's beak does no damage)
		static constexpr float BirdChargeHitInterval = 30.0f;
		// How much lower a perched bird stands than its flying box reaches
		static constexpr float BirdStandingDrop = 8.0f;
		// Angle below the first shot of the second bird's pair, in radians
		static constexpr float BirdShotSpread = 0.2f;

		void UpdateAnimation(float timeMult);
		// Which of the three sliding-to-a-halt poses the current speed calls for, 0 for none
		bool IsSlidingToHalt();
		void UpdateHookIdleAnimation(float timeMult, AnimState newState);
		void IssueHookIdleFlavor();
		// Whether the playing transition (e.g. a non-cancellable *ShootTo* return) must go for a new shot to be seen
		bool ShouldCancelTransitionOnFire() const;
		void EnterStopPhase(StopPhase phase);
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
		// Bird flight on both axes, its own pull instead of gravity, no jump or special move
		void HandleBirdMovement(float timeMult);
		// Bird flight cap on either axis in the current mode
		float GetBirdMaxSpeed() const;
		// Perches a bird that has sat still long enough
		void EnterBirdPerch();
		// Takes a perched bird off its perch, back to the flying box
		void LeaveBirdPerch();
		// Plays the perched poses one after another
		void UpdateBirdPerchPose(float timeMult);
		// Fires the second bird's pair of shots for one round of ammo, only from FireCurrentWeapon()
		void FireBirdShots();
		template<typename T, WeaponType weaponType>
		void FireBirdShotPair(float spread);
		// Sets up or removes bird flight after a morph
		void ApplyBirdMorph(PlayerType playerTypePrevious);
		// Returns the metadata for the given type, or nullptr; doesn't change the player
		Metadata* RequestMorphMetadata(PlayerType type);
		// Switches the player type and metadata, remembering the character a temporary form is taken from
		void ApplyMorphType(PlayerType type, Metadata* metadata);
		// Arms Lori's kick, she has no wind-up outside Reforged
		void BeginLoriKick();
		// Pose, transition and kick, shared by the press and the timed repeat
		void TriggerLoriKick();
		// Kicks again if Down and Jump are still held
		void UpdateLoriKickRepeat(float timeMult);
		// Plays ground-run animations at a rate taken from the speed, non-Reforged only; must run last in UpdateAnimation()
		void UpdateLegacyRunAnimSpeed();
		void HandleWaterAndModifierMovement(float timeMult);
		void HandleLookupAndCrouch(float timeMult, bool canJumpPrev);
		void HandleJump(float timeMult);
		// Gives back the double jumps and the copter time of an airtime
		void ResetAirJumps();
		// Fetches the profile of the current player type into _characterProfile
		void RefreshCharacterProfile();
		// Starts or caps the copter flight, as the air jump of a character
		void TryStartCopter(const CharacterProfile& profile);
		// The double jump, as the air jump of a character
		void TryDoubleJump(const CharacterProfile& profile, float timeMult);
		// Returns the descent speed of the copter flight
		float GetCopterDescentSpeed(const CharacterProfile& profile) const;
		// How long a press keeps copter ears going
		float GetCopterFlightTime() const;
		// Shortens the wind-up transition of the uppercut and the sidekick just started
		void SetLegacyGroundMoveWindUp();
		// Whether the copter flight has a horizontal speed of its own
		bool IsCopterPushActive() const;
		// Drives the horizontal speed of a copter flight with CharacterProfile::CopterSpeedX
		void ApplyCopterPush(float timeMult);
		// An ordinary jump from the ground, shared with the one-way relaunch
		void BeginStandardJump(float timeMult);
		// Whether the player is rising through a one-way platform, still ground for the original's jump test (non-Reforged)
		bool IsOnLegacyOneWayFloor();
		// Whether solid or one-way ground is directly under the feet, asked of the tileset where `ActorState::CanJump` was cleared
		bool HasLegacyFloorBelow();
		// Performs that relaunch, after the move because the original tests the position the move ended on
		void TryLegacyOneWayRejump(float timeMult);
		// Counts Run taps made standing still, shows the wind-up, and launches the player when the tapping stops
		void UpdateRevUp(float timeMult);
		// Whether the rev-up pose should be showing
		bool IsRevvingUp() const;
		// Abandons a wind-up without launching (on jump or crouch) and restores the animation speed
		void CancelRevUp();
		// Gunspot of the current animation relative to its hotspot, false (and zero) if its sheet has none
		// Switches to a shooting pose that has a muzzle, so the shot doesn't start at a pose's hotspot
		bool SetShootingAnimation();
		bool GetGunspotOffset(Vector2i& offset) const;

	public:
		/** @brief Clears all run-in-place rev-up state */
		void ResetRevUpState();

	private:
		// Throws sparks off the feet while revving up; static so remote players can use it too
		static void EmitRevUpSparks(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos,
			std::uint16_t baseLayer, bool facingLeft, float charge);
		// Applies the stored launch speed, with the no-friction window and its grace
		void ApplyRevUpLaunch();
		// Restarts the wind-up sound without stacking, here and on the peers
		void PlayRevUpSfx();
		// Cuts off the wind-up sound here and on the peers
		void StopRevUpSfx();
		// Starts the looping sound of a continuous weapon here and on the peers, unless one is already playing
		void StartWeaponSfx(WeaponType weaponType, StringView identifier, float gain, float pitch);
		// Stops the looping weapon sound here and on the peers
		void StopWeaponSfx();
		// Whether the non-Reforged dash is up - Run held, or still inside its grace
		bool IsDashActive() const;
		// Strength of the `MODIFIER_SLIDE` tile below the grounded player, or -1
		std::int32_t GetSlideStrength() const;
		// Signed strength of the accelerating belt below the player (negative pulls left), 0 for none
		std::int32_t GetAccBeltStrength() const;
		// Advances the dash grace and snaps back to the walk cap when it lapses
		void UpdateDashState(float timeMult);
		// Clears non-Reforged movement state that outlives a frame, on respawn or warp
		void ResetLegacyMovementState();
		/**
		 * @brief Whether a crouch-launched special move may start this frame
		 *
		 * Outside Reforged, the crouch must be established before Jump, as in the original.
		 */
		bool IsSpecialMoveCrouchReady() const;
		void HandleSpecialJump(float timeMult);
		void HandleWeaponFire(bool areaWeaponAllowed);
		void OnHandleWater();
		void OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock);
		// What a point of the path travelled this frame may act on
		enum class AreaEventPass {
			None = 0x00,
			// One-shot effects (warp, pole, text, script callback), only on first entering a tile
			Effects = 0x01,
			// Tile states (weapons, shallow water, ambient light), only from the tile the frame ended in
			States = 0x02,
		};

		DEATH_PRIVATE_ENUM_FLAGS(AreaEventPass);

		// Handles the tile event at one point of the path, returns `true` if it took the player over and the rest must be ignored
		bool HandleAreaEventAt(float x, float y, float timeMult, AreaEventPass pass, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock);
		void DoWarpOut(Vector2f pos, WarpFlags flags);
		// Attaches the player to a pole met at `eventPos`, returns `false` if declined
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
		bool FireCustomWeapon(WeaponType weaponType, const WeaponDefinition& definition, std::uint16_t& ammoDecrease);
		// Applies the ammo the level allows of each weapon to the inventory
		void ApplyLevelWeaponRules();
		static std::uint16_t GetAmmoLimit(const WeaponOptions& options);

		// Returns `true` if another (locally simulated) player stands on top of this one
		bool IsBeingStoodOnByPlayer() const;
	};
}
