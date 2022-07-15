#pragma once

#include "../ActorBase.h"
#include "../LevelInitialization.h"

namespace Jazz2::Actors
{
	class Player : public ActorBase
	{
	public:
		enum class Modifier {
			None,
			Airboard,
			Copter,
			LizardCopter
		};

		Player();

		void OnUpdate(float timeMult) override;

		void ReceiveLevelCarryOver(ExitType exitType, const PlayerCarryOver& carryOver);
		PlayerCarryOver PrepareLevelCarryOver();

		void WarpToPosition(Vector2f pos, bool fast);
		bool SetModifier(Modifier modifier);
		bool TakeDamage(int amount, float pushForce);
		void SetInvulnerability(float time, bool withCircleEffect);

		void AddScore(uint32_t amount);
		bool AddHealth(int amount);
		void AddCoins(int count);
		void AddGems(int count);
		void ConsumeFood(bool isDrinkable);
		bool AddAmmo(WeaponType weaponType, int16_t count);
		void AddWeaponUpgrade(WeaponType weaponType, uint8_t upgrade);
		bool AddFastFire(int count);

	protected:
		enum class SpecialMoveType {
			None,
			Buttstomp,
			Uppercut,
			Sidekick
		};

		enum class LevelExitingState {
			None,
			Waiting,
			Transition,
			Ready
		};

		static constexpr float MaxDashingSpeed = 9.0f;
		static constexpr float MaxRunningSpeed = 4.0f;
		static constexpr float MaxVineSpeed = 2.0f;
		static constexpr float MaxDizzySpeed = 2.4f;
		static constexpr float MaxShallowWaterSpeed = 3.6f;
		static constexpr float Acceleration = 0.2f;
		static constexpr float Deceleration = 0.22f;

		static constexpr const char* WeaponNames[PlayerCarryOver::WeaponCount] = {
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
		//MovingPlatform* _carryingObject;
		//SwingingVine* _currentVine;
		bool _canDoubleJump;
		//SoundInstance* _copterSound;

		int _lives, _coins, _foodEaten, _score;
		Vector2f _checkpointPos;
		float _checkpointLight;

		float _sugarRushLeft, _sugarRushStarsTime;

		int _gems, _gemsPitch;
		float _gemsTimer;
		float _bonusWarpTimer;

		float _invulnerableTime;
		float _invulnerableBlinkTime;

		float _idleTime;
		float _keepRunningTime;
		float _lastPoleTime;
		Vector2i _lastPolePos;
		float _inTubeTime;
		float _dizzyTime;

		WeaponType _currentWeapon;
		bool _weaponAllowed;
		float _weaponCooldown;
		int16_t _weaponAmmo[PlayerCarryOver::WeaponCount];
		uint8_t _weaponUpgrades[PlayerCarryOver::WeaponCount];

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnTileDeactivate(int tx1, int ty1, int tx2, int ty2) override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdateHitbox() override;

		bool OnHandleCollision(ActorBase* other) override;
		void OnHitFloor() override;
		void OnHitCeiling() override;
		void OnHitWall() override;

	private:
		void UpdateAnimation(float timeMult);
		void PushSolidObjects(float timeMult);
		void CheckEndOfSpecialMoves(float timeMult);
		void CheckDestructibleTiles(float timeMult);
		void CheckSuspendedStatus();
		void OnHandleWater();
		void OnHandleAreaEvents(float timeMult, __out bool& areaWeaponAllowed, __out int& areaWaterBlock);

		bool SetPlayerTransition(AnimState state, bool cancellable, bool removeControl, SpecialMoveType specialMove, const std::function<void()>& callback = nullptr);
		void InitialPoleStage(bool horizontal);
		void NextPoleStage(bool horizontal, bool positive, int stagesLeft, float lastSpeed);
		void EndDamagingMove();
		bool CanFreefall();

		void OnPerishInner();

		void SwitchToNextWeapon();
		void SwitchToWeaponByIndex(int weaponIndex);
		bool FireWeapon(WeaponType weaponType);
		void GetFirePointAndAngle(Vector3i& initialPos, Vector2f& gunspotPos, float& angle);
		void FireWeaponBlaster();
	};
}