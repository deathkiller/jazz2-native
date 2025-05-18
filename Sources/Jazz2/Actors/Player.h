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

	/** @brief Represents a controllable player */
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
			None,
			Airboard,
			Copter,
			LizardCopter
		};

		/** @brief Special move type */
		enum class SpecialMoveType : std::uint8_t {
			None,
			Buttstomp,
			Uppercut,
			Sidekick
		};

		/** @brief Type of invulnerability */
		enum class InvulnerableType {
			Transient,
			Blinking,
			Shielded
		};

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
			None,
			Waiting,
			WaitingForWarp,
			Transition,
			Ready
		};

		/** @brief State of HUD weapon wheel */
		enum class WeaponWheelState {
			Hidden,
			Opening,
			Visible,
			Closing
		};
		
		/** @brief Reason the current weapon was changed */
		enum class SetCurrentWeaponReason {
			Unknown,			/**< Unspecified */
			User,				/**< Set by the user */
			Rollback,			/**< Set due to rollback */
			AddAmmo,			/**< Set because an ammo for a new weapon was collected */
		};

		/** @{ @name Constants */

		static constexpr float MaxDashingSpeed = 9.0f;
		static constexpr float MaxRunningSpeed = 4.0f;
		static constexpr float MaxVineSpeed = 2.0f;
		static constexpr float MaxDizzySpeed = 2.4f;
		static constexpr float MaxShallowWaterSpeed = 3.6f;
		static constexpr float Acceleration = 0.2f;
		static constexpr float Deceleration = 0.22f;

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
		float _externalForceCooldown;
		float _springCooldown;
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

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		void OnHitWall(float timeMult) override;

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

		void UpdateAnimation(float timeMult);
		void PushSolidObjects(float timeMult);
		void CheckEndOfSpecialMoves(float timeMult);
		void CheckSuspendState(float timeMult);
		void OnUpdatePhysics(float timeMult);
		void OnUpdateTimers(float timeMult);
		void OnHandleMovement(float timeMult, bool areaWeaponAllowed, bool canJumpPrev);
		void OnHandleWater();
		void OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock);
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
	};
}