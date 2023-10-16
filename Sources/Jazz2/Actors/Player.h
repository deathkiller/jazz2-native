#pragma once

#include "ActorBase.h"
#include "../LevelInitialization.h"
#include "../ShieldType.h"

#if defined(WITH_ANGELSCRIPT)
namespace Jazz2::Scripting
{
	class ScriptPlayerWrapper;
	class jjPLAYER;
}
#endif

#if defined(WITH_MULTIPLAYER)
namespace Jazz2::Multiplayer
{
	class MultiLevelHandler;
}
#endif

namespace Jazz2::UI
{
	class HUD;
}

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

	class Player : public ActorBase
	{
		friend class UI::HUD;
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::ScriptPlayerWrapper;
		friend class Scripting::jjPLAYER;
#endif
#if defined(WITH_MULTIPLAYER)
		friend class Multiplayer::MultiLevelHandler;
#endif
		friend class Environment::SwingingVine;
		friend class Solid::PinballBumper;
		friend class Solid::PinballPaddle;
		friend class Weapons::Thunderbolt;

	public:
		enum class Modifier : uint8_t {
			None,
			Airboard,
			Copter,
			LizardCopter
		};

		enum class SpecialMoveType : uint8_t {
			None,
			Buttstomp,
			Uppercut,
			Sidekick
		};

		Player();
		~Player();

		PlayerType GetPlayerType() const {
			return _playerType;
		}

		SpecialMoveType GetSpecialMove() const {
			return _currentSpecialMove;
		}

		const uint16_t* GetWeaponAmmo() const {
			return _weaponAmmo;
		}

		const uint8_t* GetWeaponUpgrades() const {
			return _weaponUpgrades;
		}

		bool HasSugarRush() const {
			return (_sugarRushLeft > 0.0f);
		}

		bool CanBreakSolidObjects() const;
		bool CanMoveVertically() const;

		bool OnLevelChanging(ExitType exitType);
		void ReceiveLevelCarryOver(ExitType exitType, const PlayerCarryOver& carryOver);
		PlayerCarryOver PrepareLevelCarryOver();

		void WarpToPosition(Vector2f pos, bool fast);
		bool SetModifier(Modifier modifier, const std::shared_ptr<ActorBase>& decor = nullptr);
		bool TakeDamage(int amount, float pushForce = 0.0f);
		void SetInvulnerability(float time, bool withCircleEffect);

		void AddScore(uint32_t amount);
		bool AddHealth(int amount);
		bool AddLives(int count);
		void AddCoins(int count);
		void AddGems(int count);
		void ConsumeFood(bool isDrinkable);
		void ActivateSugarRush(float duration);
		bool AddAmmo(WeaponType weaponType, int16_t count);
		void AddWeaponUpgrade(WeaponType weaponType, uint8_t upgrade);
		bool AddFastFire(int count);
		void MorphTo(PlayerType type);
		void MorphRevert();
		bool SetDizzyTime(float time);

		ShieldType GetActiveShield() const {
			return _activeShield;
		}

		bool SetShield(ShieldType shieldType, float time);
		bool IncreaseShieldTime(float time);
		bool SpawnBird(uint8_t type, Vector2f pos);
		bool DisableControllable(float timeout);
		void SetCheckpoint(Vector2f pos, float ambientLight);

		ActorBase* GetCarryingObject() const {
			return _carryingObject;
		}
		void SetCarryingObject(ActorBase* actor, bool resetSpeed = false, SuspendType suspendType = SuspendType::None);

		void SwitchToWeaponByIndex(uint32_t weaponIndex);
		void GetFirePointAndAngle(Vector3i& initialPos, Vector2f& gunspotPos, float& angle);

	protected:
		enum class LevelExitingState {
			None,
			Waiting,
			WaitingForWarp,
			Transition,
			Ready
		};

		enum class WeaponWheelState {
			Hidden,
			Opening,
			Visible,
			Closing
		};

		static constexpr float MaxDashingSpeed = 9.0f;
		static constexpr float MaxRunningSpeed = 4.0f;
		static constexpr float MaxVineSpeed = 2.0f;
		static constexpr float MaxDizzySpeed = 2.4f;
		static constexpr float MaxShallowWaterSpeed = 3.6f;
		static constexpr float Acceleration = 0.2f;
		static constexpr float Deceleration = 0.22f;

		static constexpr const char* WeaponNames[(int)WeaponType::Count] = {
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

		int _playerIndex;
		bool _isActivelyPushing, _wasActivelyPushing;
		bool _controllable;
		bool _controllableExternal;
		float _controllableTimeout;
		ExitType _lastExitType;

		bool _wasUpPressed, _wasDownPressed, _wasJumpPressed, _wasFirePressed;

		PlayerType _playerType, _playerTypeOriginal;
		SpecialMoveType _currentSpecialMove;
		bool _isAttachedToPole;
		float _copterFramesLeft, _fireFramesLeft, _pushFramesLeft, _waterCooldownLeft;
		LevelExitingState _levelExiting;
		bool _isFreefall, _inWater, _isLifting, _isSpring;
		int _inShallowWater;
		Modifier _activeModifier;
		bool _inIdleTransition, _inLedgeTransition;
		ActorBase* _carryingObject;
		bool _canDoubleJump;
		float _springCooldown;
		std::shared_ptr<AudioBufferPlayer> _copterSound;

		int _lives, _coins, _coinsCheckpoint, _foodEaten, _score;
		Vector2f _checkpointPos;
		float _checkpointLight;

		float _sugarRushLeft, _sugarRushStarsTime;
		float _shieldSpawnTime;
		int _gems, _gemsCheckpoint, _gemsPitch;
		float _gemsTimer;
		float _bonusWarpTimer;

		SuspendType _suspendType;
		float _suspendTime;
		float _invulnerableTime;
		float _invulnerableBlinkTime;
		float _jumpTime;
		float _idleTime;
		float _keepRunningTime;
		float _lastPoleTime;
		Vector2i _lastPolePos;
		float _inTubeTime;
		float _dizzyTime;
		std::shared_ptr<Environment::Bird> _spawnedBird;
		std::shared_ptr<ActorBase> _activeModifierDecor;
		Array<LightEmitter> _trail;
		Vector2f _trailLastPos;
		ShieldType _activeShield;
		float _activeShieldTime;
		std::unique_ptr<RenderCommand> _shieldRenderCommands[2];

		float _weaponCooldown;
		WeaponType _currentWeapon;
		bool _weaponAllowed;
		uint16_t _weaponAmmo[(int)WeaponType::Count];
		uint16_t _weaponAmmoCheckpoint[(int)WeaponType::Count];
		uint8_t _weaponUpgrades[(int)WeaponType::Count];
		uint8_t _weaponUpgradesCheckpoint[(int)WeaponType::Count];
		std::shared_ptr<AudioBufferPlayer> _weaponSound;
		WeaponWheelState _weaponWheelState;

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

	private:
		static constexpr float ShieldDisabled = -1000000000000.0f;

		void UpdateAnimation(float timeMult);
		void PushSolidObjects(float timeMult);
		void CheckEndOfSpecialMoves(float timeMult);
		void CheckSuspendState(float timeMult);
		void OnHandleWater();
		void OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, int& areaWaterBlock);

		std::shared_ptr<AudioBufferPlayer> PlayPlayerSfx(const StringView& identifier, float gain = 1.0f, float pitch = 1.0f);
		bool SetPlayerTransition(AnimState state, bool cancellable, bool removeControl, SpecialMoveType specialMove, const std::function<void()>& callback = nullptr);
		void InitialPoleStage(bool horizontal);
		void NextPoleStage(bool horizontal, bool positive, int stagesLeft, float lastSpeed);
		void EndDamagingMove();
		bool CanFreefall();

		void OnPerishInner();

		void SwitchToNextWeapon();
		template<typename T, WeaponType weaponType>
		void FireWeapon(float cooldownBase, float cooldownUpgrade);
		void FireWeaponPepper();
		void FireWeaponRF();
		void FireWeaponTNT();
		bool FireWeaponThunderbolt();
		bool FireCurrentWeapon(WeaponType weaponType);
	};
}