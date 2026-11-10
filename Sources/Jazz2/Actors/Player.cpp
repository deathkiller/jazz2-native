#include "Player.h"
#include "../ContentResolver.h"
#include "../ILevelHandler.h"
#include "../Events/EventMap.h"
#include "../Tiles/TileMap.h"
#include "../PreferencesCache.h"
#include "SolidObjectBase.h"
#include "Explosion.h"
#include "PlayerCorpse.h"
#include "Environment/Bird.h"
#include "Environment/BonusWarp.h"
#include "Environment/Spring.h"
#include "Environment/SwingingVine.h"
#include "Enemies/EnemyBase.h"
#include "Enemies/TurtleShell.h"

#include "Weapons/BlasterShot.h"
#include "Weapons/BouncerShot.h"
#include "Weapons/CustomShot.h"
#include "Weapons/ElectroShot.h"
#include "Weapons/FreezerShot.h"
#include "Weapons/PepperShot.h"
#include "Weapons/RFShot.h"
#include "Weapons/SeekerShot.h"
#include "Weapons/ShieldFireShot.h"
#include "Weapons/ShieldLightningShot.h"
#include "Weapons/ShieldWaterShot.h"
#include "Weapons/ToasterShot.h"
#include "Weapons/TNT.h"
#include "Weapons/Thunderbolt.h"

#include "../../nCine/tracy.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Base/FrameTimer.h"
#include "../../nCine/Graphics/RenderQueue.h"

#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Actors
{
	// Player-specific animations
	static constexpr AnimState Shield = (AnimState)536870928;
	static constexpr AnimState ShieldFire = (AnimState)536870929;
	static constexpr AnimState ShieldLightning = (AnimState)536870931;
	static constexpr AnimState ShieldWater = (AnimState)536870930;
	static constexpr AnimState SugarRush = (AnimState)536870913;
	static constexpr AnimState WeaponFlare = (AnimState)536870950;
	// The feathers a hurt bird loses
	static constexpr AnimState BirdFeathers = (AnimState)536870960;
	// Perched bird poses and the beak attack; no pose bits may be set in them, or the bird would aim up
	static constexpr AnimState BirdPerch = (AnimState)0x20100000;
	static constexpr AnimState BirdPerchTurnLeft = (AnimState)0x20200000;
	static constexpr AnimState BirdPerchLookLeft = (AnimState)0x20300000;
	static constexpr AnimState BirdPerchTurnLeftBack = (AnimState)0x20400000;
	static constexpr AnimState BirdPerchTurnRight = (AnimState)0x20500000;
	static constexpr AnimState BirdPerchLookRight = (AnimState)0x20600000;
	static constexpr AnimState BirdPerchTurnRightBack = (AnimState)0x20700000;
	static constexpr AnimState BirdPerchWorm = (AnimState)0x20800000;
	static constexpr AnimState BirdCharge = (AnimState)0x20900000;
	// Diagonal beak charge with Up or Down held
	static constexpr AnimState BirdChargeUp = (AnimState)0x20A00000;
	static constexpr AnimState BirdChargeDown = (AnimState)0x20B00000;
	// Bits kept into the next frame; bits 16-19 (Freefall, Lift, Spring, RevUp) are decided fresh every frame
	static constexpr AnimState CompositeAnimMask = (AnimState)0xFFF03F60;
	static constexpr AnimState TransformFrogFromJazz = (AnimState)0x60000000;
	static constexpr AnimState TransformFrogFromSpaz = (AnimState)0x60000001;
	static constexpr AnimState TransformFrogFromLori = (AnimState)0x60000002;

	static inline void CountDown(float& timeLeft, float timeMult)
	{
		if (timeLeft > 0.0f) {
			timeLeft -= timeMult;
		}
	}

	Player::Player()
		:
		// 0 = original colors; remote players get theirs from the network
		_furColor(PreferencesCache::PlayerFurColor), _birdPose(0), _birdChargeDirY(0),
		_isActivelyPushing(false), _wasActivelyPushing(false), _wasOnAccBelt(false), _pushContactThisFrame(false),
		_kickPushedThisFrame(false), _kickWasPushing(false), _controllable(true), _controllableExternal(true),
		_wasUpPressed(false), _wasDownPressed(false), _wasJumpPressed(false), _wasFirePressed(false), _isRunPressed(false),
		_runActionSeeded(false), _fireReleasedWhileFrozen(false), _inDeathEvent(false), _revUpSfxActive(false), _isAttachedToPole(false),
		_canPushFurther(false), _isFreefall(false), _inWater(false), _isLifting(false), _isSpring(false), _flyCheatActive(false),
		_inIdleTransition(false), _inLedgeTransition(false), _stopPhaseChanging(false), _inHookIdleFlavor(false),
		_copterCarried(false), _riseOwnedByJump(false), _copterPressAllowed(true), _loriKickShotUsed(false),
		_stackCarrying(false), _beingStoodOn(false), _hPoleCarry(false), _springCarry(false), _revUpCarry(false),
		_jumpReleased(false), _poleEnteredOnSpring(false), _riseFromFloatUp(false), _inFloatUpArea(false),
		_crouchHeldBefore(false), _weaponAllowed(true), _birdPerched(false), _birdStanding(false), _birdCharging(false),
		_levelExitSuppressed(false), _lightHiddenByScript(false)
	{
	}

	Player::~Player()
	{
		if (_spawnedBird != nullptr) {
			_spawnedBird->FlyAway();
			_spawnedBird = nullptr;
		}
		StopAllActiveSounds();
		ReleasePaletteOffset();
	}

	void Player::StopAllActiveSounds()
	{
#if defined(WITH_AUDIO)
		for (auto* sound : { &_copterSound, &_airboardSound, &_airboardTurnSound, &_revUpSound, &_weaponSound }) {
			if (*sound != nullptr) {
				(*sound)->Stop();
				*sound = nullptr;
			}
		}
#endif
	}

	const Player::CharacterTraits& Player::GetCharacterTraits(PlayerType type)
	{
		static const CharacterTraits Traits[] = {
			{ "Interactive/PlayerJazz"_s, 1.1f, 5 },
			{ "Interactive/PlayerSpaz"_s, 1.6f, 4 },
			{ "Interactive/PlayerLori"_s, 1.3f, 3 },
			{ "Interactive/PlayerFrog"_s, 1.0f, 0 },
			{ "Interactive/PlayerBird"_s, 1.0f, 0 },
			{ "Interactive/PlayerBird2"_s, 1.0f, 0 },
			// Spectate mode and unknown types fall back to Jazz metadata, the player is invisible anyway
			{ "Interactive/PlayerJazz"_s, 1.0f, 0 }
		};

		switch (type) {
			case PlayerType::Jazz: return Traits[0];
			case PlayerType::Spaz: return Traits[1];
			case PlayerType::Lori: return Traits[2];
			case PlayerType::Frog: return Traits[3];
			case PlayerType::Bird: return Traits[4];
			case PlayerType::Bird2: return Traits[5];
			default: return Traits[6];
		}
	}

	std::uint32_t Player::GetEffectiveFurColor() const
	{
		// The bird has no fur ranges to recolor, so it keeps its own colors
		if (IsBird()) {
			return 0;
		}

		// Team modes may force a color even when the player picked none
		std::uint32_t furColor = _levelHandler->GetPlayerFurColor(this, _furColor);

		// Online always recolors; local games depend on the "Apply Colors" preference
		if (furColor == 0) {
			return 0;
		}
		if (!_levelHandler->IsLocalSession()) {
			return furColor;
		}
		// A forced color ignores the "Apply Colors" preference
		if (_levelHandler->IsPlayerColorForced(this)) {
			return furColor;
		}
		switch (PreferencesCache::PlayerColors) {
			case PlayerColorMode::FirstLocalPlayer: return (_playerIndex == 0 ? furColor : 0);
			case PlayerColorMode::AllLocalPlayers: return furColor;
			default: return 0; // OnlineOnly
		}
	}

	std::int32_t Player::GetPaletteOffset() const
	{
		return _paletteOffset;
	}

	void Player::RefreshColorPalette()
	{
		auto& resolver = ContentResolver::Get();
		if (resolver.IsHeadless()) {
			return;
		}

		std::uint32_t furColor = GetEffectiveFurColor();

		// Recoloring needs indexed sprites; read the flag off the first loaded animation, since some may be deferred
		bool isIndexed = false;
		if (furColor != 0 && _metadata != nullptr) {
			for (const GraphicResource& animation : _metadata->Animations) {
				if (animation.Base != nullptr) {
					isIndexed = ((animation.Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);
					break;
				}
			}
		}
		if (furColor != 0 && !isIndexed) {
			LOGW("Player sprites are not indexed - recoloring disabled");
		}

		// Acquire before releasing, so an unchanged color keeps its slot
		std::int32_t newOffset = (isIndexed ? resolver.AcquirePaletteOffset(furColor, _playerType) : -1);
		if (_paletteOffset >= 0) {
			resolver.ReleasePaletteOffset(_paletteOffset);
		}
		_paletteOffset = newOffset;
		_renderer.SetPalette(_paletteOffset);
	}

	void Player::ReleasePaletteOffset()
	{
		if (_paletteOffset >= 0) {
			ContentResolver::Get().ReleasePaletteOffset(_paletteOffset);
			_paletteOffset = -1;
		}
	}

	Task<bool> Player::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_playerTypeOriginal = (PlayerType)details.Params[0];
		_playerType = _playerTypeOriginal;
		_checkpointPlayerType = _playerTypeOriginal;
		_playerTypeBeforeMorph = _playerTypeOriginal;
		_playerIndex = details.Params[1];
		RefreshCharacterProfile();

		// Load indexed only when recolored, otherwise raw palette indices would be drawn
		bool useIndexed = (GetEffectiveFurColor() != 0);
		// TODO: Spectate mode loads Jazz metadata, but the player is invisible
		async_await RequestMetadataAsync(GetCharacterTraits(_playerType).Metadata, useIndexed);

		SetAnimation(AnimState::Fall);

		RefreshColorPalette();

		std::memset(_inventory.WeaponAmmo, 0, sizeof(_inventory.WeaponAmmo));
		std::memset(_inventoryCheckpoint.WeaponAmmo, 0, sizeof(_inventoryCheckpoint.WeaponAmmo));
		std::memset(_inventory.WeaponUpgrades, 0, sizeof(_inventory.WeaponUpgrades));
		std::memset(_inventoryCheckpoint.WeaponUpgrades, 0, sizeof(_inventoryCheckpoint.WeaponUpgrades));

		ApplyLevelWeaponRules();

		if (_playerType == PlayerType::Spectate) {
			// Spectate mode - no collision, no gravity, invisible
			SetState(ActorState::PreserveOnRollback | ActorState::ExcludeSimilar | ActorState::CollideWithTilesetLegacy, true);
			SetState(ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjects | ActorState::IsSolidObject, false);
			_health = 0;
			_maxHealth = 0;
			_renderer.SetDrawEnabled(false);
			
			// Empty hitbox for spectate mode
			AABB = {};
			AABBInner = {};
		} else {
			SetState(ActorState::PreserveOnRollback | ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjects |
				ActorState::IsSolidObject | ActorState::ExcludeSimilar | ActorState::KeepsMomentumWhenFrozen | ActorState::CollideWithTilesetLegacy, true);

			_health = 5;
			_maxHealth = _health;
		}

		_currentWeapon = WeaponType::Blaster;

		_checkpointPos = Vector2f((float)details.Pos.X, (float)details.Pos.Y);
		_checkpointLight = _levelHandler->GetDefaultAmbientLight();
		_currentAmbientLight = _checkpointLight;
		_trailLastPos = _checkpointPos;

		async_return true;
	}

	bool Player::CanJump() const
	{
		return (GetState(ActorState::CanJump) || _carryingObject != nullptr);
	}

	bool Player::IsOnLegacyOneWayFloor()
	{
		// Measured in the original: crossing a one-way platform with jump held launches a fresh jump.
		// Asked separately because setting CanJump while rising would snap the player onto the platform.
		if (_levelHandler->IsReforged() || _speed.Y >= 0.0f || _suspendType != SuspendType::None ||
			(_state & ActorState::ApplyGravitation) != ActorState::ApplyGravitation) {
			return false;
		}

		// Solid only with Downwards means a one-way; asking once would also catch the floor just left
		if (!HasLegacyFloorBelow()) {
			return false;
		}
		AABBf aabb = AABBInner;
		aabb.T = aabb.B;
		aabb.B += CollisionCheckStep;
		TileCollisionParams passableParams = { TileDestructType::None, false };
		return _levelHandler->IsPositionEmpty(this, aabb, passableParams);
	}

	bool Player::HasLegacyFloorBelow()
	{
		AABBf aabb = AABBInner;
		aabb.T = aabb.B;
		aabb.B += CollisionCheckStep;
		TileCollisionParams params = { TileDestructType::None, true };
		return !_levelHandler->IsPositionEmpty(this, aabb, params);
	}

	bool Player::IsRammingFrozen(const ActorBase* target) const
	{
		return (target->IsFrozen() && std::abs(_speed.X) > FrozenRamSpeed);
	}

	bool Player::CanBreakSolidObjects() const
	{
		if (_sugarRushLeft > 0.0f) {
			return true;
		}

		if (IsButtstompStarting()) {
			return false;
		}

		// A move in its wind-up breaks nothing, so the player doesn't drop through a monitor (matches the original)
		if (!_levelHandler->IsReforged()) {
			if (_currentSpecialMove == SpecialMoveType::Uppercut && _uppercutTimeLeft <= 0.0f) {
				return false;
			}
			if (_currentSpecialMove == SpecialMoveType::Sidekick && _sidekickDistanceLeft <= 0.0f) {
				return false;
			}
		}

		return (_currentSpecialMove != SpecialMoveType::None);
	}

	bool Player::CanMoveVertically() const
	{
		return (_inWater || _activeModifier != Modifier::None || IsBird());
	}

	bool Player::IsInWater() const
	{
		return _inWater;
	}

	bool Player::IsContinuousJumpAllowed() const
	{
		return PreferencesCache::EnableContinuousJump;
	}

	bool Player::IsLedgeClimbAllowed() const
	{
		return _levelHandler->IsLedgeClimbEnabled(this);
	}

	bool Player::OnTileDeactivated()
	{
		return false;
	}

	void Player::OnUpdate(float timeMult)
	{
		ZoneScoped;

#if defined(DEATH_DEBUG)
		if (PreferencesCache::AllowCheats && _levelHandler->PlayerActionPressed(this, PlayerAction::ChangeWeapon)) {
			float moveDistance = (_levelHandler->PlayerActionPressed(this, PlayerAction::Run) ? 400.0f : 100.0f);
			if (_levelHandler->PlayerActionHit(this, PlayerAction::Left)) {
				MoveInstantly(Vector2f(-moveDistance, 0.0f), MoveType::Relative | MoveType::Force);
			}
			if (_levelHandler->PlayerActionHit(this, PlayerAction::Right)) {
				MoveInstantly(Vector2f(moveDistance, 0.0f), MoveType::Relative | MoveType::Force);
			}
			if (_levelHandler->PlayerActionHit(this, PlayerAction::Up)) {
				MoveInstantly(Vector2f(0.0f, -moveDistance), MoveType::Relative | MoveType::Force);
			}
			if (_levelHandler->PlayerActionHit(this, PlayerAction::Down)) {
				MoveInstantly(Vector2f(0.0f, moveDistance), MoveType::Relative | MoveType::Force);
			}
		}
#endif

		if DEATH_UNLIKELY(_playerType == PlayerType::Spectate) {
			OnHandleSpectate(timeMult);
			return;
		}

		RefreshCharacterProfile();

		// Delayed spawning
		if (_lastExitType != ExitType::None) {
			_controllableTimeout -= timeMult;
			if (_controllableTimeout > 0.0f) {
				return;
			}

			bool isFrozen = ((_lastExitType & ExitType::Frozen) == ExitType::Frozen);
			_isFreefall = (isFrozen || CanFreefall());
			SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpOutFreefall : AnimState::TransitionWarpOut, false, true, SpecialMoveType::None, [this, isFrozen]() {
				SetState(ActorState::ApplyGravitation, true);
				if (isFrozen) {
					SetAnimation(AnimState::Freefall);
					Freeze(100.0f);
				} else {
					_controllable = true;
					// UpdateAnimation() was skipped while not controllable
					UpdateAnimation(0.0f);
				}
			});

			_renderer.SetDrawEnabled(true);
			PlayPlayerSfx("WarpOut"_s, 1.0f / _levelHandler->GetPlayers().size());
			_levelHandler->PlayerExecuteRumble(this, "Warp"_s);

			_lastExitType = ExitType::None;
		}

		// Local splitscreen resolves stacking before physics, so jump input sees CanJump()
		if (_levelHandler->CanPlayersCollide()) {
			UpdatePlayerStacking(timeMult, /*snap:*/ true);
		}

		bool canJumpPrev = CanJump();
		_frameStartSpeedY = _speed.Y;

		// Measured in the original: the keys steer this tick's move, not the next one
		bool steerBeforeMove = (!_levelHandler->IsReforged() && !IsBird() && _health > 0);
		// Counted like UpdateFrozenState(), which decrements before it brakes
		bool frozenFrictionApplied = (steerBeforeMove && _frozenTimeLeft > timeMult);
		if (steerBeforeMove) {
			UpdateRunAction();
			if (frozenFrictionApplied) {
				ApplyFrozenFriction(timeMult);
			}
			HandleHorizontalMovement(timeMult);
		}

		OnUpdatePhysics(timeMult, frozenFrictionApplied);

		UpdateAnimation(timeMult);
		CheckSuspendState(timeMult);
		CheckEndOfSpecialMoves(timeMult);

		OnHandleWater();

		bool areaWeaponAllowed = true;
		std::int32_t areaWaterBlock = -1;
		OnHandleAreaEvents(timeMult, areaWeaponAllowed, areaWaterBlock);

		// Shallow Water
		if (areaWaterBlock != -1) {
			if (_inShallowWater == -1) {
				OnWaterSplash(Vector2f(_pos.X, (float)areaWaterBlock), true);
			}

			_inShallowWater = areaWaterBlock;
		} else if (_inShallowWater != -1) {
			OnWaterSplash(Vector2f(_pos.X, (float)_inShallowWater), false);

			_inShallowWater = -1;
		}

		OnUpdateTimers(timeMult);
		OnHandleMovement(timeMult, areaWeaponAllowed, canJumpPrev, steerBeforeMove);

#if defined(WITH_AUDIO)
		if (_airboardTurnSound != nullptr && !_airboardTurnSound->IsPlaying()) {
			if (_activeModifier == Modifier::Airboard) {
				PlayPlayerSfx("AirboardTurnEnd"_s, 2.0f, 0.8f);
			}
			_airboardTurnSound = nullptr;
		}
#endif

		// Weapon switching; the frog and the first bird have no weapons
		if (((_controllable && _controllableExternal) || !_levelHandler->IsReforged()) && _playerType != PlayerType::Frog && _playerType != PlayerType::Bird) {
			bool isGamepad;
			if (_levelHandler->PlayerActionHit(this, PlayerAction::ChangeWeapon, true, isGamepad)) {
				if (!isGamepad || PreferencesCache::WeaponWheel == WeaponWheelStyle::Disabled) {
					SwitchToNextWeapon();
				}
			} else {
				for (std::uint32_t i = 0; i <= (std::uint32_t)PlayerAction::SwitchToThunderbolt - (std::uint32_t)PlayerAction::SwitchToBlaster; i++) {
					if (_levelHandler->PlayerActionHit(this, (PlayerAction)(i + (std::uint32_t)PlayerAction::SwitchToBlaster))) {
						SwitchToWeaponByIndex(i);
					}
				}
			}
		}
	}

	float Player::GetGravityModifier(float baseGravity, bool isRising) const
	{
		// Reforged keeps the symmetric gravity
		if (_levelHandler->IsReforged()) {
			return baseGravity;
		}

		// No gravity inside a float-up column (matches the original), except during a buttstomp, which is only capped
		if (_inFloatUpArea && _currentSpecialMove != SpecialMoveType::Buttstomp) {
			return 0.0f;
		}

		// Measured in the original: the rise decelerates ~3x faster than the fall accelerates
		if (_pos.Y >= _levelHandler->GetPlayerWaterLevel()) {
			return LegacyWaterGravity;
		}
		// An uppercut is nearly weightless for its whole length
		if (_uppercutTimeLeft > 0.0f) {
			return LegacyUppercutGravity;
		}
		if (!isRising) {
			return LegacyFallGravity;
		}
		// Releasing jump makes the rest of the ascent heavier, easing off near the apex. A float-up rise reads
		// the live key (there was no jump to release), a jump reads the latch set in HandleJump().
		if (_riseFromFloatUp) {
			if (_levelHandler->PlayerActionPressed(const_cast<Player*>(this), PlayerAction::Jump)) {
				return LegacyRiseGravity;
			}
		} else if (!_jumpReleased) {
			return LegacyRiseGravity;
		}
		return (std::abs(_speed.Y) > LegacyRiseBrakeEaseSpeed
			? LegacyRiseGravityReleased
			: LegacyRiseGravityReleasedNearApex);
	}

	void Player::OnUpdatePhysics(float timeMult, bool frozenFrictionApplied)
	{
		// Force collisions every frame even if player doesn't move
		SetState(ActorState::IsDirty, true);

		// The applied rise cap reproduces the original constant-speed rise; special moves launch via forces and skip it
		if (!_levelHandler->IsReforged()) {
			// The float-up marker lapses once the ascent ends
			if (_speed.Y >= 0.0f) {
				_riseFromFloatUp = false;
			}
			_maxRiseSpeed = (_currentSpecialMove == SpecialMoveType::None ? LegacyRiseSpeedCap : 0.0f);
			// Internal speed limit only governs how long a launch keeps rising at the cap (measured off a pole launch)
			_verticalSpeedLimit = ((_inTubeTime > 0.0f || _speed.Y < -16.0f) ? LegacyVerticalSpeedLimit : 16.0f);
			// Same limit on both axes as in the original, so horizontal springs aren't clamped; a sidekick needs more
			_horizontalSpeedLimit = (_currentSpecialMove == SpecialMoveType::Sidekick
				? std::max(LegacyLoriSidekickSpeed, LegacySpazSidekickSpeed)
				: LegacyVerticalSpeedLimit);
			// Applied movement is capped like the rise. Lori's kick is exempt (measured in the original);
			// both sidekicks also use the cap to not overshoot their fixed distance.
			if (_currentSpecialMove == SpecialMoveType::Sidekick && _sidekickDistanceLeft > 0.0f) {
				float remainingPerTick = _sidekickDistanceLeft / std::max(timeMult, 0.01f);
				_maxAppliedSpeedX = (_playerType == PlayerType::Lori
					? remainingPerTick
					: std::min(LegacyAppliedSpeedCap, remainingPerTick));

				// Her kick ramps up; direction from facing, since a stop mid-kick zeroes the speed
				if (_playerType == PlayerType::Lori) {
					_sidekickTime += timeMult;
					float ramp = LegacyLoriKickRamp * _sidekickTime * _sidekickTime;
					_speed.X = (IsFacingLeft() ? -ramp : ramp);
				}
			} else if (_inTubeTime > 0.0f) {
				// A sucker tube asks for speeds above the cap, so it is exempt
				_maxAppliedSpeedX = std::max(LegacyAppliedSpeedCap, std::abs(_speed.X));
			} else if (std::int32_t accBelt = GetAccBeltStrength()) {
				// Measured in the original: an accelerating belt raises the cap to its strength plus the walk cap
				float beltCap = std::abs((float)accBelt) * LegacyAccBeltSpeed + LegacyWalkSpeed;
				_maxAppliedSpeedX = std::max(LegacyAppliedSpeedCap, beltCap);
			} else if (_hPoleCarry && _keepRunningTime > 0.0f) {
				// A horizontal pole launch is exempt for its whole carry (measured in the original)
				_maxAppliedSpeedX = std::max(LegacyAppliedSpeedCap, std::abs(_speed.X));
			} else {
				_hPoleCarry = false;
				// So is a script's ball roll, as measured in JJ2+
				_maxAppliedSpeedX = (_ballCarryLeft > 0.0f
					? std::max(LegacyAppliedSpeedCap, std::abs(_speed.X))
					: LegacyAppliedSpeedCap);
			}
			if (!GetState(ActorState::CollideWithTileset) && _inTubeTime <= 0.0f) {
				// Noclip travels the whole speed on both axes, as measured in JJ2+
				_maxAppliedSpeedX = 0.0f;
				_maxRiseSpeed = 0.0f;
			}
			// The landing allowance is measured off the player only
			_landingTolerance = LegacyLandingTolerance;
		}

		// Tubes are exempt from speed clamps in both modes; only existing limits are raised (0 means no cap)
		if (_inTubeTime > 0.0f) {
			_verticalSpeedLimit = std::max(_verticalSpeedLimit, LegacyVerticalSpeedLimit);
			_horizontalSpeedLimit = std::max(_horizontalSpeedLimit, LegacyVerticalSpeedLimit);
			if (_maxAppliedSpeedX > 0.0f) {
				_maxAppliedSpeedX = std::max(_maxAppliedSpeedX, std::abs(_speed.X));
			}
		}

		// A bird's gravity is removed every frame since many things restore it; speeds clamp to the flight cap
		// every tick like in the original, except in a tube
		if (IsBird()) {
			SetState(ActorState::ApplyGravitation, false);
			if (_inTubeTime <= 0.0f) {
				float maxSpeed = GetBirdMaxSpeed();
				_speed.X = std::clamp(_speed.X, -maxSpeed, maxSpeed);
				_speed.Y = std::clamp(_speed.Y, -maxSpeed, maxSpeed);
			}
		}

		// Process level bounds (if not warping)
		if (_currentTransition == nullptr ||
			(_currentTransition->State != AnimState::TransitionWarpIn && _currentTransition->State != AnimState::TransitionWarpInFreefall &&
				_currentTransition->State != AnimState::TransitionWarpOut && _currentTransition->State != AnimState::TransitionWarpOutFreefall)) {
			Vector2f lastPos = _pos;
			Recti levelBounds = _levelHandler->GetLevelBounds();
			if (lastPos.X < levelBounds.X) {
				lastPos.X = float(levelBounds.X);
				_pos = lastPos;
			} else if (lastPos.X > levelBounds.X + levelBounds.W) {
				lastPos.X = float(levelBounds.X + levelBounds.W);
				_pos = lastPos;
			}
		}

		// Reset vertical speed if the position is managed by carrying object
		if (_carryingObject != nullptr) {
			_speed.Y = 0.0f;
			_externalForce.Y = 0.0f;
			_internalForceY = 0.0f;
		}

		PushSolidObjects(timeMult);

		// Custom implementation of `ActorBase::OnUpdate(timeMult)`
		TileCollisionParams params = { TileDestructType::Collapse, _speed.Y >= 0.0f };
		if (_currentSpecialMove == SpecialMoveType::Sidekick || _sugarRushLeft > 0.0f) {
			params.DestructType |= TileDestructType::Special;
		} else if (_currentSpecialMove == SpecialMoveType::Buttstomp || _currentSpecialMove == SpecialMoveType::Uppercut) {
			params.DestructType |= TileDestructType::Special | TileDestructType::VerticalMove;
		}
		if (std::abs(_speed.X) > std::numeric_limits<float>::epsilon() || std::abs(_speed.Y) > std::numeric_limits<float>::epsilon() || _sugarRushLeft > 0.0f) {
			params.DestructType |= TileDestructType::Speed;
			params.Speed = (_sugarRushLeft > 0.0f ? 64.0f : std::max(std::abs(_speed.X), std::abs(_speed.Y)));
		}
		
		TryStandardMovement(timeMult, params);

		if (_tubeHeldSpeedY < 0.0f) {
			// A script launch keeps its speed while held under or overlapping a floor, as measured in JJ2+
			TileCollisionParams probe = { TileDestructType::None, true };
			AABBf box = AABBInner;
			box.B += 3.0f;
			_tubeHeldTimeLeft -= timeMult;
			if (_tubeHeldTimeLeft > 0.0f && (_speed.Y == _tubeHeldSpeedY || !_levelHandler->IsPositionEmpty(this, box, probe))) {
				_speed.Y = _tubeHeldSpeedY;
				_unstuckCooldown = std::max(_unstuckCooldown, 2.0f);
			} else {
				_tubeHeldSpeedY = 0.0f;
			}
		}

		// Measured in the original: the rise never overshoots the apex by more than one tick of fall gravity
		if (!_levelHandler->IsReforged() && _frameStartSpeedY < 0.0f &&
			(GetState() & ActorState::ApplyGravitation) == ActorState::ApplyGravitation) {
			float oneStep = LegacyFallGravity * timeMult;
			if (_speed.Y > oneStep && _speed.Y <= LegacyRiseGravityReleased * timeMult) {
				_speed.Y = oneStep;
			}
		}

		// Relaunches the jump when a one-way platform is crossed with jump held
		TryLegacyOneWayRejump(timeMult);

		// The original bounds the top of the level; clamped after the move so the frame's step can't overshoot it
		if (!_levelHandler->IsReforged()) {
			float topBound = float(_levelHandler->GetLevelBounds().Y);
			if (_pos.Y < topBound) {
				// Stop the momentum too, like a tile ceiling, so the player doesn't hang against the top
				bool rising = (_speed.Y < 0.0f || _internalForceY < 0.0f || _externalForce.Y < 0.0f);
				MoveInstantly(Vector2f(_pos.X, topBound), MoveType::Absolute | MoveType::Force);
				if (rising) {
					if (_speed.Y < 0.0f) {
						_speed.Y = 0.0f;
					}
					if (_internalForceY < 0.0f) {
						_internalForceY = 0.0f;
					}
					OnHitCeiling(timeMult);
				}
			}
		}

		if ((GetState() & ActorState::ApplyGravitation) != ActorState::ApplyGravitation) {
			_externalForce.Y = std::min(_externalForce.Y + 0.002f * timeMult, 0.0f);
		}

		// Original terminal velocity, downward only; a buttstomp is held at its own speed (lower in a float-up field).
		// Without gravity (noclip or a tube) the speed is kept.
		if (!_levelHandler->IsReforged() && GetState(ActorState::ApplyGravitation)) {
			float fallCap;
			if (_currentSpecialMove == SpecialMoveType::Buttstomp) {
				fallCap = (_inFloatUpArea ? LegacyFloatUpFallCap : LegacyButtstompSpeed);
			} else {
				fallCap = LegacyFallSpeedCap;
			}
			if (_speed.Y > fallCap) {
				_speed.Y = fallCap;
			}
		}

		if (params.TilesDestroyed > 0) {
			AddScore(params.TilesDestroyed * 50);
			_levelHandler->PlayerExecuteRumble(this, "BreakTile"_s);
		}

		OnUpdateHitbox();

		if (_frozenTimeLeft > 0.0f) {
			// Holding Fire thaws faster, as measured in JJ2+, unless held since before the freeze
			constexpr float FireThawTicksPerTick = 8.0f;
			if (!_levelHandler->PlayerActionPressed(this, PlayerAction::Fire)) {
				_fireReleasedWhileFrozen = true;
			} else if (_fireReleasedWhileFrozen) {
				float chip = FireThawTicksPerTick * timeMult;
				if (_frozenTimeLeft > chip) {
					_frozenTimeLeft -= chip;
				} else {
					ThawOnNextUpdate();
				}
			}
		} else {
			_fireReleasedWhileFrozen = false;
		}

		if (UpdateFrozenState(timeMult, frozenFrictionApplied)) {
			if (_controllableTimeout > 0.0f) {
				_controllableTimeout = 0.01f;
			}
			_levelHandler->PlayerExecuteRumble(this, "Hurt"_s);
		}
	}

	void Player::OnUpdateTimers(float timeMult)
	{
		// Invulnerability
		if (_invulnerableTime > 0.0f) {
			_invulnerableTime -= timeMult;

			if (_invulnerableTime <= 0.0f) {
				SetState(ActorState::IsInvulnerable, false);
				_renderer.SetDrawEnabled(true);
				_shieldSpawnTime = ShieldDisabled;
			} else if (_shieldSpawnTime > ShieldDisabled) {
				_shieldSpawnTime -= timeMult;
				if (_shieldSpawnTime <= 0.0f) {
					_shieldSpawnTime += 1.0f;

					auto* tilemap = _levelHandler->TileMap();
					if (tilemap != nullptr) {
						auto* res = _metadata->FindAnimation(Shield);
						if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
							Vector2i texSize = res->Base->TextureDiffuse->GetSize();
							Recti frameRect = res->Base->GetFrameRect(res->FrameOffset);
							Vector2i frameOffset = res->Base->GetFrameOffset(res->FrameOffset);

							Tiles::TileMap::DestructibleDebris debris = { };
							debris.Pos = _pos;
							debris.Depth = _renderer.GetLayer() - 2;
							debris.Size = Vector2f((float)frameRect.W, (float)frameRect.H);
							debris.FrameOffset = Vector2f(frameOffset.X + (frameRect.W - res->Base->FrameDimensions.X) * 0.5f,
								frameOffset.Y + (frameRect.H - res->Base->FrameDimensions.Y) * 0.5f);
							debris.Speed = Vector2f::Zero;
							debris.Acceleration = Vector2f::Zero;

							debris.Scale = 0.9f;
							debris.ScaleSpeed = -0.014f;
							debris.Alpha = 0.7f;
							debris.AlphaSpeed = -0.016f;

							debris.Time = 160.0f;

							debris.TexScaleX = (float(frameRect.W) / float(texSize.X));
							debris.TexBiasX = (float(frameRect.X) / float(texSize.X));
							debris.TexScaleY = (float(frameRect.H) / float(texSize.Y));
							debris.TexBiasY = (float(frameRect.Y) / float(texSize.Y));

							debris.DiffuseTexture = res->Base->TextureDiffuse.get();
							// -1 = not indexed
							debris.PaletteOffset = (((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);
							debris.Flags = Tiles::TileMap::DebrisFlags::AdditiveBlending;

							tilemap->CreateDebris(debris);
						}
					}
				}
			} else if (_invulnerableBlinkTime >= 0.0f && (_currentTransition == nullptr || _currentTransition->State != AnimState::Hurt)) {
				_invulnerableBlinkTime -= timeMult;
				if (_invulnerableBlinkTime <= 0.0f) {
					_renderer.SetDrawEnabled(!_renderer.IsDrawEnabled());
					_invulnerableBlinkTime = 3.0f;
				}
			} else {
				_renderer.SetDrawEnabled(true);
			}
		}

		if (_controllableTimeout > 0.0f) {
			_controllableTimeout -= timeMult;

			if (_controllableTimeout <= 0.0f) {
				_controllable = true;

				if (_isAttachedToPole) {
					// Something went wrong, detach so the player doesn't get stuck
					for (std::int32_t i = -1; i > -6; i--) {
						if (MoveInstantly(Vector2f(_speed.X, (float)i), MoveType::Relative)) {
							break;
						}
					}

					SetState(ActorState::ApplyGravitation, true);
					_isAttachedToPole = false;
					_wasActivelyPushing = false;

					_controllableTimeout = 4.0f;
					_lastPoleTime = 10.0f;
				}
			} else {
				_controllable = false;
			}
		}

		CountDown(_jumpTime, timeMult);
		UpdateLoriKickRepeat(timeMult);
		CountDown(_externalForceCooldown, timeMult);
		CountDown(_springCooldown, timeMult);
		if (_uppercutTimeLeft > 0.0f) {
			_uppercutTimeLeft -= timeMult;
			if (_uppercutTimeLeft < 0.0f) {
				_uppercutTimeLeft = 0.0f;
			}
		}
		// Before HandleSpecialJump(), so it counts like the original
		CountDown(_doubleJumpWindowLeft, timeMult);
		CountDown(_onPinballPaddleTime, timeMult);
		// Re-armed in HandleJump() while the key is down in the air
		CountDown(_copterDeafLeft, timeMult);
		if (_copterLandGraceLeft > 0.0f) {
			_copterLandGraceLeft -= timeMult;
			if (_copterLandGraceLeft <= 0.0f && !_copterCarried) {
				SetCopterFlight(0.0f);
			}
		}
		if (_springHoldLeft > 0.0f) {
			_springHoldLeft -= timeMult;
			if (_springHoldLeft <= 0.0f) {
				_springHoldLeft = 0.0f;
				// A horizontal spring carry ends at the same speed as every other carry
				_speed.X = std::clamp(_speed.X, -LegacyCarryExitRunSpeed, LegacyCarryExitRunSpeed);
				_hPoleCarry = false;
				_ballCarryLeft = 0.0f;
			}
		}
		if (_ballCarryLeft > 0.0f) {
			_ballCarryLeft = std::max(_ballCarryLeft - timeMult, 0.0f);
		}
		if (_rfBlastLeft > 0.0f) {
			_rfBlastLeft -= timeMult;
			if (_rfBlastLeft <= 0.0f) {
				_rfBlastLeft = 0.0f;
				// The original snaps to the walk cap when the hold ends, then decelerates normally
				_speed.X = std::clamp(_speed.X, -LegacyWalkSpeed, LegacyWalkSpeed);
			}
		}
		CountDown(_weaponCooldown, timeMult);
		CountDown(_bonusWarpTimer, timeMult);
		CountDown(_lastPoleTime, timeMult);
		if (_gemsTimer > 0.0f) {
			_gemsTimer -= timeMult;

			if (_gemsTimer <= 0.0f) {
				_gemsPitch = 0;
			}
		}

		CountDown(_waterCooldownLeft, timeMult);

		// Weapons
		if (_fireFramesLeft > 0.0f) {
			_fireFramesLeft -= timeMult;

			if (_fireFramesLeft <= 0.0f) {
				// Play post-fire animation, but not over a special move
				if ((_currentAnimation->State & (AnimState::Walk | AnimState::Run | AnimState::Dash | AnimState::Buttstomp | AnimState::Uppercut | AnimState::Swim | AnimState::Airboard | AnimState::Lift | AnimState::Spring)) == AnimState::Idle &&
					!_inIdleTransition && !_isAttachedToPole) {

					if ((_currentAnimation->State & AnimState::Hook) == AnimState::Hook) {
						SetTransition(AnimState::TransitionHookShootToHook, false);
					} else if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
						SetAnimation(AnimState::Copter);
						SetTransition(AnimState::TransitionCopterShootToCopter, false);
					} else if ((_currentAnimation->State & (AnimState::Jump | AnimState::Fall)) != AnimState::Idle) {
						// Rising counts as airborne too, so the ground transition isn't played in mid-air
						SetTransition(AnimState::TransitionFallShootToFall, false);
					} else if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
						SetAnimation(AnimState::Crouch, true);
					} else {
						SetTransition(AnimState::TransitionShootToIdle, false);
					}
				}
			}
		}
		CountDown(_weaponFlareTime, timeMult);
		// Dizziness
		CountDown(_dizzyTime, timeMult);

		// Shield
		if (_activeShieldTime > 0.0f) {
			_activeShieldTime -= timeMult;
			if (_activeShieldTime <= 0.0f) {
				_activeShield = ShieldType::None;
			}
		}

		// Sugar Rush
		if (_sugarRushLeft > 0.0f) {
			_sugarRushLeft -= timeMult;

			if (_sugarRushLeft > 0.0f) {
				_sugarRushStarsTime -= timeMult;
				if (_sugarRushStarsTime <= 0.0f) {
					_sugarRushStarsTime = Random().FastFloat(2.0f, 8.0f);
					SpawnSugarRushStar(_levelHandler, _metadata, _pos, _renderer.GetLayer());
				}
			} else {
				_renderer.Initialize(ActorRendererType::Default);
			}
		}

		// Weapon Wheel Transitions
		switch (_weaponWheelState) {
			case WeaponWheelState::Opening:
				if (_renderer.GetRendererType() == ActorRendererType::Default) {
					_renderer.Initialize(ActorRendererType::Outline);
				}
				_weaponWheelState = WeaponWheelState::Visible;
				break;
			case WeaponWheelState::Closing:
				if (_renderer.GetRendererType() == ActorRendererType::Outline) {
					_renderer.Initialize(ActorRendererType::Default);
				}
				_weaponWheelState = WeaponWheelState::Hidden;
				break;
		}

		// Copter
		if (_activeModifier == Modifier::Copter || _activeModifier == Modifier::LizardCopter) {
			SetCopterFlight(_copterFramesLeft - timeMult, IsFlyCheatActive() ? FlightType::Cheat : FlightType::Normal);
			if (_copterFramesLeft <= 0.0f) {
				SetModifier(Modifier::None);
			} else if (_activeModifierDecor != nullptr) {
				_activeModifierDecor->MoveInstantly(_pos, MoveType::Absolute | MoveType::Force);
			}
		}

#if defined(WITH_AUDIO)
		if (_copterSound != nullptr) {
			if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
				_copterSound->SetPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_copterSound->Stop();
				_copterSound = nullptr;
			}
		}
		// Follows the player until it finishes, since near-field smoothing is fixed where it was last placed
		if (_revUpSound != nullptr) {
			if (_revUpSound->IsPlaying()) {
				_revUpSound->SetPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_revUpSound = nullptr;
			}
		}
		if (_airboardSound != nullptr) {
			if ((_currentAnimation->State & AnimState::Airboard) == AnimState::Airboard) {
				_airboardSound->SetPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_airboardSound->Stop();
				_airboardSound = nullptr;
			}
		}
#endif

		// Trail
		if (PreferencesCache::ShowPlayerTrails) {
			for (std::int32_t i = 0; i < _trail.size(); i++) {
				auto& part = _trail[i];
				part.Intensity -= timeMult * 0.04f;
				part.Brightness -= timeMult * 0.04f;
				part.RadiusFar -= timeMult * 0.4f;
				if (part.RadiusFar <= 0.0f) {
					_trail.eraseUnordered(i);
					i--;
				}
			}

			// The uppercut leaves a trail for the whole move, whatever its speed
			bool isUppercut = (_currentSpecialMove == SpecialMoveType::Uppercut);
			if ((_keepRunningTime > 0.0f || isUppercut || _speed.SqrLength() > (_isRunPressed ? 36.0f : 100.0f)) && _inTubeTime <= 0.0f) {
				constexpr float TrailDivision = 10.0f;
				Vector2f trailDelta = (_pos - _trailLastPos);
				std::int32_t trailDistance = (std::int32_t)(trailDelta.Length() / TrailDivision);
				if (trailDistance > 0) {
					trailDelta.Normalize();
					while (trailDistance-- > 0) {
						_trailLastPos += trailDelta * TrailDivision;

						auto& light = _trail.emplace_back();
						light.Pos = _trailLastPos;
						light.Intensity = 0.5f;
						light.Brightness = 0.8f;
						light.RadiusNear = 0.0f;
						light.RadiusFar = 28.0f;
					}
				}
			} else {
				_trailLastPos = _pos;
			}
		}

		// Tube
		if (_inTubeTime > 0.0f) {
			_inTubeTime -= timeMult;

			if (_inTubeTime <= 0.0f) {
				_controllable = true;
				SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset, true);

				// Measured in the original: leaving a tube clamps to the walk cap (or the run cap with Run held)
				float exitCap = (_isRunPressed ? LegacyCarryExitRunSpeed : LegacyWalkSpeed);
				_speed.X = std::clamp(_speed.X, -exitCap, exitCap);
			} else {
				// Not controllable in a tube, so weapons are disabled
				StopWeaponSfx();
			}
		}
	}

	void Player::OnHandleMovement(float timeMult, bool areaWeaponAllowed, bool canJumpPrev, bool steeredBeforeMove)
	{
		if (!steeredBeforeMove) {
			UpdateRunAction();
		}

		if (_health <= 0) {
			return;
		}

		// The original gives no boost on running off a ledge, gaps are cleared by the speed cap and fall gravity alone

		if (IsBird()) {
			HandleBirdMovement(timeMult);
		} else if (!steeredBeforeMove) {
			HandleHorizontalMovement(timeMult);
		}

		if (_hitFloorTime > 0.0f) {
			_hitFloorTime -= timeMult;
		}

		if (!_controllable || !_controllableExternal || _currentSpecialMove == SpecialMoveType::Buttstomp) {
			// Weapons are automatically disabled if player is not controllable
			if (_currentWeapon != WeaponType::Thunderbolt || _fireFramesLeft <= 0.0f) {
				StopWeaponSfx();
			}
			// Measured in the original: Lori can still shoot during her sidekick, Spaz can't
			if (!_levelHandler->IsReforged() && _controllableExternal &&
				_currentSpecialMove == SpecialMoveType::Sidekick && _playerType == PlayerType::Lori) {
				HandleWeaponFire(areaWeaponAllowed);
			}
			return;
		}

		if (IsBird()) {
			// HandleBirdMovement() already did everything
		} else if (_inWater || _activeModifier != Modifier::None) {
			HandleWaterAndModifierMovement(timeMult);
		} else {
			// Captured before this frame's crouch, to tell a crouch already held from Down + Jump on the same tick
			_crouchHeldBefore = ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch);
			HandleLookupAndCrouch(timeMult, canJumpPrev);
			HandleJump(timeMult);
			// After the jump, so a jump out of a wind-up isn't a launch
			UpdateRevUp(timeMult);
		}

		// A buttstomp started this frame blocks the shot like the check above, or the shoot pose cancels its start
		if (_currentSpecialMove != SpecialMoveType::Buttstomp) {
			HandleWeaponFire(areaWeaponAllowed);
		}
	}

	void Player::UpdateRunAction()
	{
		if (PreferencesCache::ToggleRunAction) {
			if (!_runActionSeeded) {
				// Run held at level start gives no rising edge, so follow the live key until it's first seen down
				_isRunPressed = _levelHandler->PlayerActionPressed(this, PlayerAction::Run);
				_runActionSeeded = _isRunPressed;
			} else if (_levelHandler->PlayerActionHit(this, PlayerAction::Run)) {
				_isRunPressed = !_isRunPressed;
			}
		} else {
			_isRunPressed = _levelHandler->PlayerActionPressed(this, PlayerAction::Run);
		}
		if (_isRunPressed && !_characterProfile.CanRun) {
			_isRunPressed = false;
		}
	}

	bool Player::IsRevvingUp() const
	{
		// Grounded only; a wind-up is suspended in the air, but the pose isn't shown there
		return (_revUp.Wound && _revUp.WindowLeft > 0.0f && CanJump());
	}

	void Player::CancelRevUp()
	{
		// A pending light launch must be cancelled too, even with the charge spent
		if (_revUp.Charge <= 0.0f && !_revUp.Wound && _revUp.LaunchLeft <= 0.0f) {
			return;
		}

		_revUp = {};
		// Restore the duration the charge-dependent wind-up changed
		_renderer.AnimDuration = _currentAnimation->AnimDuration;
		// Only a launch lets the last press's sound play out
		StopRevUpSfx();
	}

	void Player::PlayRevUpSfx()
	{
		// Exclusive, so remote peers replace the previous instance too
#if defined(WITH_AUDIO)
		if (_revUpSound != nullptr) {
			_revUpSound->Stop();
		}
		_revUpSound = PlaySfx("RevUp"_s, RevUpSfxGain, _revUp.SfxPitch, SfxFlags::Exclusive);
#else
		// A dedicated server has no audio but still tells the peers
		PlaySfx("RevUp"_s, RevUpSfxGain, _revUp.SfxPitch, SfxFlags::Exclusive);
#endif
		_revUpSfxActive = true;
	}

	void Player::StopRevUpSfx()
	{
		// Tracked apart from the instance, which a dedicated server never has
		if (!_revUpSfxActive) {
			return;
		}
		_revUpSfxActive = false;

#if defined(WITH_AUDIO)
		_levelHandler->StopSfx(this, "RevUp"_s, _revUpSound.get());
		_revUpSound = nullptr;
#else
		_levelHandler->StopSfx(this, "RevUp"_s, nullptr);
#endif
	}

	void Player::StartWeaponSfx(WeaponType weaponType, StringView identifier, float gain, float pitch)
	{
		if (_weaponSoundType != WeaponType::Unknown) {
			return;
		}
		_weaponSoundType = weaponType;

		// Also without audio, a dedicated server still tells the peers
		SfxFlags flags = (SfxFlags::Exclusive | SfxFlags::Looping);
#if defined(WITH_AUDIO)
		_weaponSound = PlaySfx(identifier, gain, pitch, flags);
#else
		PlaySfx(identifier, gain, pitch, flags);
#endif
	}

	void Player::StopWeaponSfx()
	{
		if (_weaponSoundType == WeaponType::Unknown) {
			return;
		}

		const WeaponDefinition& definition = _levelHandler->GetWeaponDefinition(_weaponSoundType);
		String customSound = (definition.Kind == WeaponKind::Custom ? _levelHandler->GetWeapons().GetFireSoundIdentifier(definition.GetCustom().Fire) : String());
		StringView identifier = (!customSound.empty() ? StringView(customSound)
			: (_weaponSoundType == WeaponType::Thunderbolt ? "WeaponThunderbolt"_s : "WeaponToaster"_s));
		_weaponSoundType = WeaponType::Unknown;

#if defined(WITH_AUDIO)
		_levelHandler->StopSfx(this, identifier, _weaponSound.get());
		_weaponSound = nullptr;
#else
		_levelHandler->StopSfx(this, identifier, nullptr);
#endif
	}

	void Player::ApplyRevUpLaunch()
	{
		PlaySfx("RevUpLaunch"_s);
		// The last press's sound plays out
		_revUpSfxActive = false;
		_speed.X = _revUp.PendingSpeed;
		_externalForce.X = 0.0f;
		_keepRunningTime = RevUpKeepRunningTime;
		// Its own kind of carry, see @ref _revUpCarry
		_springCarry = false;
		_hPoleCarry = false;
		_revUpCarry = true;
		// The dash grace makes the hold end with a snap to the walk cap (Reforged decays instead)
		_dashGraceLeft = RevUpKeepRunningTime + 2.0f;
		_revUp.LaunchLeft = 0.0f;
		_revUp.PendingSpeed = 0.0f;
	}

	void Player::UpdateRevUp(float timeMult)
	{
		// A light launch waits through its animation phases first; counted before anything else so taps can't restart it
		if (_revUp.LaunchLeft > 0.0f) {
			float before = _revUp.LaunchLeft;
			_revUp.LaunchLeft -= timeMult;
			// Wait for the RevUp base pose, or the transition gets cancelled the next frame
			if (!_revUp.Started && (_currentAnimation->State & AnimState::RevUp) == AnimState::RevUp) {
				_revUp.Started = true;
				SetPlayerTransition(AnimState::TransitionRevUpStart, true, false, SpecialMoveType::None);
			}
			// The end animation goes in after the start animation and the loop have had their time
			if (before > RevUpEndTime && _revUp.LaunchLeft <= RevUpEndTime) {
				SetPlayerTransition(AnimState::TransitionRevUpEnd, true, false, SpecialMoveType::None);
			}
			if (_revUp.LaunchLeft <= 0.0f) {
				ApplyRevUpLaunch();
			}
			return;
		}

		// Standing still with nothing else going on; holding a direction abandons the wind-up
		bool eligible = (_controllable && _controllableExternal && !_isLifting &&
			_suspendType == SuspendType::None &&
			_activeModifier == Modifier::None && !_inWater && _dizzyTime <= 0.0f &&
			std::abs(_levelHandler->PlayerHorizontalMovement(this)) <= 0.4f &&
			std::abs(_speed.X) < 1.0f && _characterProfile.CanRun);

		// Leaving the ground suspends a wind-up (some levels rely on it), a buttstomp may carry it down too
		bool airborne = !CanJump();
		bool specialMoveOk = (_currentSpecialMove == SpecialMoveType::None ||
			(airborne && _currentSpecialMove == SpecialMoveType::Buttstomp));
		if (!eligible || !specialMoveOk) {
			CancelRevUp();
			return;
		}
		if (airborne) {
			// Frozen until landing
			return;
		}

		// The button's edge, not `_isRunPressed`, which flips with `ToggleRunAction`
		bool runHit = _levelHandler->PlayerActionHit(this, PlayerAction::Run);
		if (runHit) {
			_revUp.Charge += RevUpChargeGainTap;
			_revUp.HeldCharge = 0.0f;
			_revUp.Presses++;
			_revUp.WindowLeft = RevUpTapWindow;
		} else if (_levelHandler->PlayerActionPressed(this, PlayerAction::Run)) {
			// Holding adds too, up to a cap per press
			float gain = std::min(RevUpChargeGainHeld * timeMult, RevUpChargeHeldCap - _revUp.HeldCharge);
			if (gain > 0.0f) {
				_revUp.Charge += gain;
				_revUp.HeldCharge += gain;
			}
			_revUp.WindowLeft = RevUpTapWindow;
		} else if (_revUp.Charge > 0.0f) {
			_revUp.Charge = std::max(_revUp.Charge - RevUpChargeDecay * timeMult, 0.0f);
			if (_revUp.Armed) {
				_revUp.WindowLeft -= timeMult;
			}
		}

		// Armed gives a light launch on release; past the second threshold the wind-up pose appears
		if (_revUp.Charge >= RevUpChargeLightThreshold && _revUp.Presses >= RevUpPressesRequired) {
			_revUp.Armed = true;
		}
		if (!_revUp.Wound) {
			if (_revUp.Armed && _revUp.Charge >= RevUpChargeThreshold) {
				_revUp.Wound = true;
			} else if (!_revUp.Armed || _revUp.WindowLeft > 0.0f) {
				// Not yet, or still being fed; the charge bleeds away
				if (_revUp.Charge <= 0.0f) {
					CancelRevUp();
				}
				return;
			}
			// Armed but never wound up and tapping stopped - fall through to the light launch
		}

		// Charge follows the time spent revving, not the tap count
		if (_revUp.Wound) {
			_revUp.ChargeTime += timeMult;
		}
		float charge = std::min(_revUp.ChargeTime / RevUpFullChargeTime, 1.0f);

		if (_revUp.Wound && _revUp.WindowLeft > 0.0f) {
			// The looping pose is chosen in UpdateAnimation(); the start transition waits for it to be the base state
			if (!_revUp.Started && (_currentAnimation->State & AnimState::RevUp) == AnimState::RevUp) {
				_revUp.Started = true;
				SetPlayerTransition(AnimState::TransitionRevUpStart, true, false, SpecialMoveType::None);
			}
			// Measured in the original: the wind-up speeds up 3.5x by full charge; not during a transition
			if (_currentTransition == nullptr && (_currentAnimation->State & AnimState::RevUp) == AnimState::RevUp) {
				_renderer.AnimDuration = _currentAnimation->AnimDuration / (1.0f + charge * RevUpAnimSpeedUp);
			}

			// A sound per Run press, but at most once per pose cycle
			_revUp.SfxCooldown -= timeMult;
			if (runHit) {
				if (_revUp.SfxCooldown <= 0.0f) {
					PlayRevUpSfx();
					auto* revUpAnim = _metadata->FindAnimation(AnimState::RevUp);
					float cycleTicks = (revUpAnim != nullptr ? revUpAnim->AnimDuration / FrameTimer::SecondsPerFrame : 0.0f);
					_revUp.SfxCooldown = cycleTicks / (1.0f + charge * RevUpAnimSpeedUp);
				}
				// Every press raises the pitch, even ones swallowed by the cooldown
				_revUp.SfxPitch = std::min(_revUp.SfxPitch + RevUpSfxPitchStep, RevUpSfxMaxPitch);
			}

			// Sparks quicken with the pose
			_revUpSparkCooldown -= timeMult;
			if (_revUpSparkCooldown <= 0.0f) {
				_revUpSparkCooldown = RevUpSparkInterval / (1.0f + charge * RevUpAnimSpeedUp);
				EmitRevUpSparks(_levelHandler, _metadata, _pos, _renderer.GetLayer(), IsFacingLeft(), charge);
			}
			return;
		}

		// Tapping stopped - launch like a horizontal spring in the facing direction; held keys don't steer it
		float launchSpeed = lerp(RevUpMinLaunchSpeed, RevUpMaxLaunchSpeed, charge);
		// Cleared before the transition, so UpdateAnimation() releases the rev-up pose
		bool wasWound = _revUp.Wound;
		_revUp = {};
		_revUp.PendingSpeed = (IsFacingLeft() ? -launchSpeed : launchSpeed);
		if (wasWound) {
			// Measured: the full wind-up moves when its end animation begins
			ApplyRevUpLaunch();
		} else {
			// The light case moves off when all three phases end
			_revUp.LaunchLeft = RevUpStartTime + RevUpLightMidTime + RevUpEndTime;
		}
		_wasActivelyPushing = false;
		_renderer.AnimDuration = _currentAnimation->AnimDuration;

		// The base state is held at RevUp during the animations, so the transitions can stay cancellable
		if (wasWound) {
			_revUp.EndLeft = RevUpEndTime;
			SetPlayerTransition(AnimState::TransitionRevUpEnd, true, false, SpecialMoveType::None);
		} else {
			// Never wound up: play start, loop and end now; the end is fired from the countdown above
			_revUp.EndLeft = RevUpStartTime + RevUpLightMidTime + RevUpEndTime;
		}
	}

	float Player::UpdateRemoteRevUp(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos,
		std::uint16_t baseLayer, bool facingLeft, float timeMult, float& revUpTime, float& sparkCooldown)
	{
		// Without the spark animation it's not a player
		if (metadata == nullptr || metadata->FindAnimation((AnimState)0x4F000010) == nullptr) {
			return 1.0f;
		}

		// Same as UpdateRevUp(), off the receiver's own clock, which gives the identical charge
		revUpTime += timeMult;
		float charge = std::min(revUpTime / RevUpFullChargeTime, 1.0f);

		sparkCooldown -= timeMult;
		if (sparkCooldown <= 0.0f) {
			sparkCooldown = RevUpSparkInterval / (1.0f + charge * RevUpAnimSpeedUp);
			EmitRevUpSparks(levelHandler, metadata, pos, baseLayer, facingLeft, charge);
		}

		return (1.0f + charge * RevUpAnimSpeedUp);
	}

	void Player::EmitRevUpSparks(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos,
		std::uint16_t baseLayer, bool facingLeft, float charge)
	{
		// Debris rather than an explosion, since sparks need velocity and gravity
		auto tilemap = levelHandler->TileMap();
		if (tilemap == nullptr || metadata == nullptr) {
			return;
		}

		auto* res = metadata->FindAnimation((AnimState)0x4F000010); // RevUpSpark
		if (res == nullptr || res->Base->TextureDiffuse == nullptr) {
			return;
		}

		auto& resBase = res->Base;
		Vector2i texSize = resBase->TextureDiffuse->GetSize();
		// Away from the direction the player is trying to go
		float back = (facingLeft ? 1.0f : -1.0f);
		std::int32_t count = 1 + Random().Fast(0, 2 + (std::int32_t)(charge * 2.0f));

		for (std::int32_t i = 0; i < count; i++) {
			// A shallow backwards spray, faster the harder the wind-up
			float speed = Random().FastFloat(1.44f, 2.88f) * (1.0f + charge);
			float spread = Random().FastFloat(-0.6f, 0.2f);
			float sinSpread, cosSpread;
			sincosApprox(spread, sinSpread, cosSpread);
			Vector2f dir = Vector2f(back * cosSpread, -std::abs(sinSpread) * 0.5f - Random().FastFloat(0.05f, 0.25f));
			float size = Random().FastFloat(1.5f, 3.0f);

			Tiles::TileMap::DestructibleDebris spark = {};
			// Just behind the heel, with little scatter
			spark.Pos = Vector2f(pos.X + back * (RevUpSparkOffsetBack + Random().FastFloat(0.0f, 2.0f)),
				pos.Y + 22.0f + RevUpSparkOffsetY);
			spark.Depth = baseLayer - 2;
			spark.Size = Vector2f(size, size);
			spark.Speed = dir * speed;
			// Gravity only, so a bounce keeps its horizontal speed
			spark.Acceleration = Vector2f(0.0f, 0.22f);

			spark.Scale = 1.0f;
			spark.ScaleSpeed = -0.004f;
			spark.Alpha = 1.0f;
			// Fades slowly to survive a bounce, paced to the lifetime below
			spark.AlphaSpeed = Random().FastFloat(-0.016f, -0.011f);
			spark.Angle = Random().FastFloat(0.0f, fRadAngle360);
			spark.AngleSpeed = Random().FastFloat(-0.4f, 0.4f);

			spark.Time = 48.0f;
			// Settles after two or three hops
			spark.Elasticity = 0.45f;

			std::int32_t frame = Random().Fast(0, std::max<std::int32_t>(1, resBase->FrameCount));
			Recti frameRect = resBase->GetFrameRect(frame);
			Vector2i frameOffset = resBase->GetFrameOffset(frame);
			// Otherwise a trimmed frame is stretched across its whole cell
			spark.FrameOffset = Vector2f((float)frameOffset.X, (float)frameOffset.Y);
			spark.TexScaleX = (float(frameRect.W) / float(texSize.X));
			spark.TexBiasX = (float(frameRect.X) / float(texSize.X));
			spark.TexScaleY = (float(frameRect.H) / float(texSize.Y));
			spark.TexBiasY = (float(frameRect.Y) / float(texSize.Y));

			spark.DiffuseTexture = resBase->TextureDiffuse.get();
			spark.PaletteOffset = (((resBase->Flags & Resources::GenericGraphicResourceFlags::Indexed) == Resources::GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);
			spark.Flags = Tiles::TileMap::DebrisFlags::AdditiveBlending | Tiles::TileMap::DebrisFlags::Bounce;

			tilemap->CreateDebris(spark);
		}
	}

	float Player::GetCameraLookAhead() const
	{
		// Measured in the original: the lead follows the direction held, not the speed; a special move eases it to center
		if (_currentSpecialMove != SpecialMoveType::None) {
			return 0.0f;
		}
		float movement = _levelHandler->PlayerHorizontalMovement(const_cast<Player*>(this));
		if (std::abs(movement) <= 0.4f) {
			return 0.0f;
		}
		// Pressed against a wall the lead returns to zero; read as travel, since a wall leaves a phantom speed
		if (std::abs(_pos.X - _frameStartPos.X) < 0.001f) {
			return 0.0f;
		}
		// A run-in-place carry leads only the way it is going, like in the original
		if (_revUpCarry && _keepRunningTime > 0.0f && movement * _speed.X < 0.0f) {
			return 0.0f;
		}
		// A bird has no dash, Run doesn't make it any faster
		bool dashLead = (IsDashActive() && !IsBird());
		return (movement < 0.0f ? -1.0f : 1.0f) * (dashLead ? LegacyCameraDashLead : LegacyCameraWalkLead);
	}

	float Player::GetCameraVerticalOffset() const
	{
		// The original's only vertical lead, keyed on the move rather than the Down key
		return (_currentSpecialMove == SpecialMoveType::Buttstomp ? LegacyCameraButtstompDrop : 0.0f);
	}

	bool Player::IsDashActive() const
	{
		// Non-Reforged reads the state the previous tick left, see UpdateDashState()
		return (_levelHandler->IsReforged() ? _isRunPressed : _dashGraceLeft > 0.0f);
	}

	bool Player::IsButtstompStarting() const
	{
		// Only its own start transition, a stomp forced by a spring can run under an unrelated one (e.g. warp-out pose)
		return (_currentSpecialMove == SpecialMoveType::Buttstomp && _currentTransition != nullptr &&
			_currentTransition->State == AnimState::TransitionButtstompStart);
	}

	std::int32_t Player::GetSlideStrength() const
	{
		// A floor event, applies only when standing on it
		if (!GetState(ActorState::CanJump)) {
			return -1;
		}

		std::uint8_t* p;
		if (_levelHandler->EventMap()->GetEventByPosition(_pos.X, _pos.Y + 32, &p) != EventType::ModifierSlide) {
			return -1;
		}

		// The parameter is 2 bits wide; a larger value would give a negative brake
		return std::min<std::int32_t>(p[0], 3);
	}

	std::int32_t Player::GetAccBeltStrength() const
	{
		if (!GetState(ActorState::CanJump)) {
			return 0;
		}

		std::uint8_t* p;
		if (_levelHandler->EventMap()->GetEventByPosition(_pos.X, _pos.Y + 32, &p) != EventType::AreaHForce) {
			return 0;
		}

		// Slots 2 and 3 of `AreaHForce` are the accelerating belt, left and right
		return (std::int32_t)p[3] - (std::int32_t)p[2];
	}

	void Player::UpdateDashState(float timeMult)
	{
		if (_levelHandler->IsReforged()) {
			return;
		}

		if (_isRunPressed) {
			_dashGraceLeft = LegacyDashGraceTicks;
			return;
		}
		if (_dashGraceLeft <= 0.0f) {
			return;
		}

		// The original's dash keeps full speed for its grace after Run is released, then drops to the walk cap
		_dashGraceLeft -= timeMult;
		if (_dashGraceLeft <= 0.0f) {
			_dashGraceLeft = 0.0f;
			// Only the dash's own speed; launches, sidekicks and RF blasts own theirs
			if (_keepRunningTime <= 0.0f && _rfBlastLeft <= 0.0f && _currentSpecialMove != SpecialMoveType::Sidekick) {
				_speed.X = std::clamp(_speed.X, -LegacyWalkSpeed, LegacyWalkSpeed);
			}
		}
	}

	void Player::ResetRevUpState()
	{
		_revUp = {};
		_revUpSparkCooldown = 0.0f;
		StopRevUpSfx();
	}

	bool Player::IsSpecialMoveCrouchReady() const
	{
		if ((_currentAnimation->State & AnimState::Crouch) != AnimState::Crouch) {
			return false;
		}
		// Reforged fires the move on the live crouch bit
		if (_levelHandler->IsReforged()) {
			return true;
		}
		// Must be grounded, so no uppercut in mid-air
		return (_crouchHeldBefore && CanJump());
	}

	void Player::ResetLegacyMovementState()
	{
		// Nothing of a dash, blast, kick, rise or carry survives a death or a warp
		_dashGraceLeft = 0.0f;
		_rfBlastLeft = 0.0f;
		_sidekickDistanceLeft = 0.0f;
		_sidekickTime = 0.0f;
		_jumpReleased = false;
		_riseOwnedByJump = false;
		_isSpring = false;
		_poleEnteredOnSpring = false;
		_riseFromFloatUp = false;
		_inFloatUpArea = false;
		_hPoleCarry = false;
		_springCarry = false;
		_revUpCarry = false;
		_springLaunchSpeedX = 0.0f;
		_springRebuildAccel = 0.0f;
		_springHoldLeft = 0.0f;
	}

	void Player::HandleHorizontalMovement(float timeMult)
	{
		// Frozen, it slides by what it had or what pushed it; keys aren't read at all
		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		UpdateHorizontalSpeed(timeMult);
		// Measured in the original: the dash state follows the speed change, so Run speeds up from the next tick
		// and the end of the grace snaps after the brake
		UpdateDashState(timeMult);
	}

	void Player::UpdateHorizontalSpeed(float timeMult)
	{
		if (_keepRunningTime <= 0.0f) {
			// An RF blast owns the horizontal speed while it carries the player
			if (_rfBlastLeft > 0.0f) {
				return;
			}
			if (!GetState(ActorState::CollideWithTileset) && _inTubeTime <= 0.0f) {
				// Noclip keeps its speed, as measured in JJ2+
				return;
			}

			bool canWalk = (_controllable && _controllableExternal && !_isLifting && _suspendType != SuspendType::SwingingVine &&
				(_playerType != PlayerType::Frog || !_levelHandler->PlayerActionPressed(this, PlayerAction::Fire)));

			float playerMovement = _levelHandler->PlayerHorizontalMovement(this);
			float playerMovementVelocity = std::abs(playerMovement);
			// Measured in the original: Lori's kick follows the direction key, so it can be turned mid-kick
			if (!_levelHandler->IsReforged() && _playerType == PlayerType::Lori &&
				_currentSpecialMove == SpecialMoveType::Sidekick && playerMovementVelocity > 0.4f) {
				SetFacingLeft(playerMovement < 0.0f);
			}
			if (IsCopterPushActive()) {
				// The keys only turn the player, the push drives the speed
				if (canWalk && playerMovementVelocity > 0.4f) {
					SetFacingLeft(_dizzyTime > 0.0f ? (playerMovement > 0.0f) : (playerMovement < 0.0f));
				}
				ApplyCopterPush(timeMult);
			} else if (_currentSpecialMove == SpecialMoveType::Buttstomp) {
				if (_levelHandler->IsReforged()) {
					_speed.X = 0.2f * playerMovement;
					if (_isRunPressed) {
						_speed.X *= 2.6f;
					}
				} else if (playerMovementVelocity > 0.4f) {
					// Measured in the original: a stomp drifts sideways at a flat rate, doubled while Run is held
					_speed.X = LegacyButtstompDriftSpeed * (playerMovement < 0.0f ? -1.0f : 1.0f);
					if (_isRunPressed) {
						_speed.X *= 2.0f;
					}
				} else {
					_speed.X = 0.0f;
				}
			} else if (canWalk && playerMovementVelocity > 0.4f) {
				SetAnimation(_currentAnimation->State & ~(AnimState::Lookup | AnimState::Crouch));

				bool wasFacingLeft = IsFacingLeft();
				bool isFacingLeft;
				if (_dizzyTime > 0.0f) {
					isFacingLeft = (playerMovement > 0.0f);
				} else {
					isFacingLeft = (playerMovement < 0.0f);
				}

				if (isFacingLeft != wasFacingLeft) {
					SetFacingLeft(isFacingLeft);
					// Reset push frames on turning, to prevent the pushing animation
					_pushFramesLeft = 0.0f;
#if defined(WITH_AUDIO)
					if (_activeModifier == Modifier::Airboard) {
						_airboardTurnSound = PlayPlayerSfx("AirboardTurnStart"_s, 2.0f, 0.8f);
					}
#endif
				}

				_isActivelyPushing = _wasActivelyPushing = true;

				// The original has one acceleration for ground and air, depending only on the dash
				float acceleration = (_levelHandler->IsReforged() ? Acceleration
					: (IsDashActive() ? LegacyDashAccel : LegacyWalkAccel));

				if (_dizzyTime > 0.0f || _playerType == PlayerType::Frog) {
					_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -MaxDizzySpeed * playerMovementVelocity, MaxDizzySpeed * playerMovementVelocity);
				} else if (_inShallowWater != -1 && _levelHandler->IsReforged() && _playerType != PlayerType::Lori) {
					// Slower in shallow water in Reforged, except Lori, who can't ledge climb or double jump
					_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -MaxShallowWaterSpeed * playerMovementVelocity, MaxShallowWaterSpeed * playerMovementVelocity);
				} else {
					if (_suspendType == SuspendType::None && !_inWater && (_levelHandler->IsReforged() ? _isRunPressed : IsDashActive())) {
						float maxDashSpeed = (_levelHandler->IsReforged() ? MaxDashingSpeed : LegacyDashSpeed);
						if (_ballCarryLeft > 0.0f && !_levelHandler->IsReforged()) {
							// A rolling ball keeps speeding up past the dash speed (measured in JJ2+)
							maxDashSpeed = std::max(maxDashSpeed, std::abs(_speed.X) + acceleration * timeMult);
						}
						_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -maxDashSpeed * playerMovementVelocity, maxDashSpeed * playerMovementVelocity);
					} else if (_suspendType == SuspendType::Vine) {
						if (_wasFirePressed) {
							_speed.X = 0.0f;
						} else {
							// Run doubles the vine speed (measured in the original)
							float maxVineSpeed = (_levelHandler->IsReforged() ? MaxVineSpeed : LegacyVineSpeed);
							if (_isRunPressed) {
								maxVineSpeed *= (_levelHandler->IsReforged() ? 1.6f : 2.0f);
							}
							_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -maxVineSpeed * playerMovementVelocity, maxVineSpeed * playerMovementVelocity);
						}
					} else if (_inWater && !_levelHandler->IsReforged()) {
						// Water has its own cap and acceleration, keyed on Run; the original settles one step above the cap
						float maxWaterSpeed = LegacyWaterSpeed * (_isRunPressed ? 2.0f : 1.0f) + LegacyWaterAccelX / LegacyFrameRateScale;
						_speed.X = std::clamp(_speed.X + LegacyWaterAccelX * timeMult * (isFacingLeft ? -1 : 1), -maxWaterSpeed * playerMovementVelocity, maxWaterSpeed * playerMovementVelocity);
					} else if (_suspendType != SuspendType::Hook) {
						float maxRunSpeed = (_levelHandler->IsReforged() ? MaxRunningSpeed : LegacyWalkSpeed);
						float accelHere = acceleration;
						// A double jump out of a spring launch rebuilds speed up to the dash cap, like the original
						if (_springRebuildAccel > 0.0f && !CanJump()) {
							accelHere = _springRebuildAccel;
							maxRunSpeed = LegacyDashSpeed;
						}
						_speed.X = std::clamp(_speed.X + accelHere * timeMult * (isFacingLeft ? -1 : 1), -maxRunSpeed * playerMovementVelocity, maxRunSpeed * playerMovementVelocity);
					}
				}

				if (CanJump()) {
					_wasUpPressed = _wasDownPressed = false;
				}
			} else if (_inTubeTime <= 0.0f && _currentSpecialMove != SpecialMoveType::Sidekick) {
				// A sidekick owns its speed for its fixed distance, so it isn't braked
				float absSpeedX = std::abs(_speed.X);
				// Coast to a stop, braking harder while dashing and less on a slide tile
				float deceleration;
				if (_levelHandler->IsReforged()) {
					deceleration = Deceleration;
				} else {
					std::int32_t slideStrength = GetSlideStrength();
					if (slideStrength >= 0) {
						deceleration = (IsDashActive()
							? LegacySlideDashDecel - slideStrength * LegacySlideDashDecelStep
							: LegacySlideWalkDecel - slideStrength * LegacySlideWalkDecelStep);
					} else {
						deceleration = (IsDashActive() ? LegacyDashDecel : LegacyWalkDecel);
					}
				}
				if (_inWater && !_levelHandler->IsReforged()) {
					// Coasting, not braking, see LegacyWaterCoast
					_speed.X = (absSpeedX < LegacyWaterCoastStop ? 0.0f : _speed.X * std::pow(LegacyWaterCoast, timeMult * LegacyFrameRateScale));
				} else {
					_speed.X = std::max((absSpeedX - deceleration * timeMult), 0.0f) * (_speed.X < 0.0f ? -1.0f : 1.0f);
				}

				// Measured in the original: an accelerating belt applies after the brake
				if (!_levelHandler->IsReforged()) {
					std::int32_t accBelt = GetAccBeltStrength();
					if (accBelt != 0) {
						// Run adds a flat bonus to the magnitude, so a leftward belt is boosted too
						std::int32_t strength = accBelt;
						if (_isRunPressed) {
							strength += (accBelt < 0 ? -LegacyAccBeltRunBonus : LegacyAccBeltRunBonus);
						}
						float target = strength * LegacyAccBeltSpeed;
						_speed.X = std::clamp(_speed.X + accBelt * LegacyAccBeltStep * timeMult,
							std::min(target, 0.0f), std::max(target, 0.0f));
					} else if (_wasOnAccBelt) {
						// Leaving one clamps like leaving a sucker tube (measured in the original)
						float exitCap = (_isRunPressed ? LegacyCarryExitRunSpeed : LegacyWalkSpeed);
						_speed.X = std::clamp(_speed.X, -exitCap, exitCap);
					}
					_wasOnAccBelt = (accBelt != 0);
				}

				_isActivelyPushing = false;

				absSpeedX = std::abs(_speed.X);
				// Reforged only; the original never turns the player to face the way they're travelling
				if (absSpeedX > 4.0f) {
					if (_levelHandler->IsReforged()) {
						SetFacingLeft(_speed.X < 0.0f);
					}
				} else if (absSpeedX < 0.001f) {
					_wasActivelyPushing = false;
				}
			}
		} else {
			_keepRunningTime -= timeMult;

			_isActivelyPushing = _wasActivelyPushing = true;

			float absSpeedX = std::abs(_speed.X);
			if (absSpeedX > 1.0f) {
				// Out of a run-in-place the player can turn and shoot backwards while carried forwards
				if (!_revUpCarry || _levelHandler->IsReforged()) {
					SetFacingLeft(_speed.X < 0.0f);
				} else {
					float movement = _levelHandler->PlayerHorizontalMovement(this);
					if (std::abs(movement) > 0.4f) {
						SetFacingLeft(movement < 0.0f);
					}
				}
			} else if (absSpeedX < 1.0f) {
				_keepRunningTime = 0.0f;
			}

			// The carry ends with the same clamp as every other carry (measured in the original)
			if (!_levelHandler->IsReforged() && _keepRunningTime <= 0.0f) {
				float exitCap = (_isRunPressed ? LegacyCarryExitRunSpeed : LegacyWalkSpeed);
				_speed.X = std::clamp(_speed.X, -exitCap, exitCap);
			}
		}
	}

	void Player::HandleBirdMovement(float timeMult)
	{
		if (_birdChargeHitCooldown > 0.0f) {
			_birdChargeHitCooldown -= timeMult;
		}

		// A sucker tube drives the player directly, the bird included
		if (_inTubeTime > 0.0f) {
			_birdAccelX = 0.0f;
			_birdCharging = false;
			_birdChargeDirY = 0;
			return;
		}

		// Without control the bird brakes and sinks
		bool canFly = (_controllable && _controllableExternal);
		float horizontalMovement = (canFly ? _levelHandler->PlayerHorizontalMovement(this) : 0.0f);
		float verticalMovement = (canFly ? _levelHandler->PlayerVerticalMovement(this) : 0.0f);
		// Jump is a second Up key
		bool climbing = (canFly && (verticalMovement < -0.3f || _levelHandler->PlayerActionPressed(this, PlayerAction::Jump)));
		bool diving = (canFly && !climbing && verticalMovement > 0.3f);
		bool steering = (std::abs(horizontalMovement) > 0.4f);
		bool fireHeld = (canFly && _weaponAllowed && _levelHandler->PlayerActionPressed(this, PlayerAction::Fire));
		float maxSpeed = GetBirdMaxSpeed();
		float chargeMinSpeed = maxSpeed * BirdChargeMinSpeedRatio;

		// Against a wall the speed and acceleration drop to zero, as measured in JJ2+; checked here because
		// OnHitWall() is skipped while pressing into the floor
		float wallDirection = (_speed.X != 0.0f ? _speed.X : (steering ? horizontalMovement : 0.0f));
		if (wallDirection != 0.0f) {
			if (auto* tiles = _levelHandler->TileMap()) {
				AABBf ahead = AABBInner;
				float step = (wallDirection < 0.0f ? -1.0f : 1.0f);
				ahead.L += step;
				ahead.R += step;
				ahead.T += 2.0f;
				ahead.B -= 2.0f;
				TileCollisionParams wallParams = { TileDestructType::None, false };
				if (!tiles->IsTileEmpty(ahead, wallParams)) {
					_speed.X = 0.0f;
					_birdAccelX = 0.0f;
				}
			}
		}

		// The beak charge: Fire held above half the cap flies at the cap, diagonally with Up or Down. The original
		// assigns it after the move, so the last frame's decision is carried out here; a wall or lost control ends it.
		float chargeDirection = 0.0f;
		if (_birdCharging) {
			if (canFly && _currentTransition == nullptr && std::abs(_speed.X) > chargeMinSpeed) {
				chargeDirection = (_speed.X < 0.0f ? -1.0f : 1.0f);
				_speed.X = maxSpeed * chargeDirection;
				_speed.Y = (float)_birdChargeDirY * maxSpeed;
			} else {
				_birdCharging = false;
			}
		}
		// Releasing Fire ends it at once, there's no cooldown
		bool charging = (_birdCharging && fireHeld);

		// Measured in JJ2+: acceleration ramps up over four ticks (also through a turn), releasing zeroes it
		if (steering) {
			float direction = (horizontalMovement < 0.0f ? -1.0f : 1.0f);
			float target = BirdAcceleration * direction;
			float rampStep = (BirdAcceleration / BirdAccelRampTime) * timeMult;
			_birdAccelX = (_birdAccelX < target ? std::min(_birdAccelX + rampStep, target) : std::max(_birdAccelX - rampStep, target));
			_speed.X = std::clamp(_speed.X + _birdAccelX * timeMult, -maxSpeed, maxSpeed);

			// Facing turns at once, except during a charge
			bool isFacingLeft = (direction < 0.0f);
			if (!charging && isFacingLeft != IsFacingLeft()) {
				SetFacingLeft(isFacingLeft);
			}
		} else {
			_birdAccelX = 0.0f;
			_speed.X = std::max(std::abs(_speed.X) - BirdBrake * timeMult, 0.0f) * (_speed.X < 0.0f ? -1.0f : 1.0f);
		}

		// During a charge steering takes off a fixed shortfall from the cap, not a rate
		if (charging && timeMult > 0.0f) {
			float shortfall = std::max(maxSpeed - _speed.X * chargeDirection, 0.0f) / (timeMult * LegacyFrameRateScale);
			_speed.X = (maxSpeed - std::min(shortfall, maxSpeed)) * chargeDirection;
		}

		// Vertically, Up assigns a flat climb, otherwise the bird is pulled down (more with Down); no brake
		if (charging) {
			// Only the charge's own vertical speed, assigned above
		} else if (climbing) {
			_speed.Y = -BirdClimbSpeed;
		} else {
			float pull = (diving ? BirdDiveGravity : BirdSinkGravity);
			// A hurt's knock decays faster while it still carries the bird upwards
			if (_speed.Y < 0.0f && _currentTransition != nullptr && _currentTransition->State == AnimState::Hurt) {
				pull += BirdHurtRiseGravity;
			}
			_speed.Y += pull * timeMult;
		}
		_speed.Y = std::clamp(_speed.Y, -maxSpeed, maxSpeed);

		// Decided on the speed after steering, like the original; the charge flies the way the bird is going
		_birdCharging = (_playerType == PlayerType::Bird && fireHeld && _currentTransition == nullptr && std::abs(_speed.X) > chargeMinSpeed);
		if (_birdCharging) {
			_birdChargeDirY = (climbing ? -1 : (diving ? 1 : 0));
			bool isFacingLeft = (_speed.X < 0.0f);
			if (isFacingLeft != IsFacingLeft()) {
				SetFacingLeft(isFacingLeft);
			}
		} else {
			_birdChargeDirY = 0;
		}

		// Sitting still on the floor perches after BirdPerchTime, any movement ends it; Fire keeps the first bird awake
		AABBf below = AABBInner;
		below.T = below.B;
		below.B += CollisionCheckStep;
		TileCollisionParams params = { TileDestructType::None, true };
		bool onFloor = !_levelHandler->IsPositionEmpty(this, below, params);
		bool still = (onFloor && !climbing && _speed.X == 0.0f && _speed.Y >= 0.0f &&
			!(_playerType == PlayerType::Bird && fireHeld));
		if (still) {
			_birdIdleTime += timeMult;
			if (!_birdPerched && _birdIdleTime >= BirdPerchTime) {
				EnterBirdPerch();
			}
		} else {
			_birdIdleTime = 0.0f;
			if (_birdPerched || _birdStanding) {
				LeaveBirdPerch();
			}
		}

		// An external push wears off like the bird's own speed
		_externalForce.Y = std::max(std::abs(_externalForce.Y) - BirdBrake * timeMult, 0.0f) * (_externalForce.Y < 0.0f ? -1.0f : 1.0f);
		_internalForceY = 0.0f;
	}

	float Player::GetBirdMaxSpeed() const
	{
		// Flies at the walk cap in both modes
		return (_levelHandler->IsReforged() ? MaxRunningSpeed : LegacyBirdSpeed);
	}

	void Player::EnterBirdPerch()
	{
		_birdPerched = true;
		_birdPose = 0;
		_birdPoseTime = 0.0f;

		// Lift onto the taller standing box as the pose changes, unless the ceiling is too low
		if (!_birdStanding) {
			TileCollisionParams params = { TileDestructType::None, true };
			_birdStanding = MoveInstantly(Vector2f(0.0f, -BirdStandingDrop), MoveType::Relative, params);
			OnUpdateHitbox();
		}
	}

	void Player::LeaveBirdPerch()
	{
		// Back to the flying box, which only shrinks from below, so nothing can end up inside a wall
		_birdPerched = false;
		_birdStanding = false;
		_birdPose = 0;
		OnUpdateHitbox();
	}

	void Player::UpdateBirdPerchPose(float timeMult)
	{
		// Perched poses play one whole animation at a time like the original; the odds are fitted, not measured
		static constexpr AnimState Poses[] = {
			AnimState::Idle, BirdPerch, BirdPerchTurnLeft, BirdPerchLookLeft, BirdPerchTurnLeftBack,
			BirdPerchTurnRight, BirdPerchLookRight, BirdPerchTurnRightBack, BirdPerchWorm
		};

		_birdPoseTime -= timeMult;
		if (_birdPose != 0 && _birdPoseTime > 0.0f) {
			return;
		}

		std::uint8_t next;
		switch (_birdPose) {
			case 2: next = 3; break;
			case 3: next = (Random().NextFloat() < 0.67f ? 3 : 4); break;
			case 5: next = 6; break;
			case 6: next = (Random().NextFloat() < 0.67f ? 6 : 7); break;
			case 4: case 7: case 8: next = 1; break;
			default: {
				// From the idle, and also as the first pose
				float r = Random().NextFloat();
				next = (r < 0.5f ? 1 : (r < 0.67f ? 2 : (r < 0.83f ? 5 : 8)));
				break;
			}
		}

		_birdPose = next;
		if (!SetAnimation(Poses[next])) {
			SetAnimation(AnimState::Idle);
		}
		// One whole cycle of the pose
		_birdPoseTime = (_currentAnimation != nullptr && _currentAnimation->AnimDuration > 0.0f
			? _currentAnimation->AnimDuration / FrameTimer::SecondsPerFrame
			: FrameTimer::FramesPerSecond);
	}

	void Player::TriggerLoriKick()
	{
		_controllable = false;
		_controllableTimeout = 40.0f;
		SetAnimation(AnimState::Uppercut);
		// Measured in the original: unlike Spaz's, her kick has no wind-up
		if (_levelHandler->IsReforged()) {
			SetPlayerTransition(AnimState::TransitionUppercutA, true, false, SpecialMoveType::Sidekick, [this]() {
				BeginLoriKick();
			});
		} else {
			// One animation for the whole kick, as the original plays it
			SetPlayerTransition(AnimState::TransitionSidekick, true, false, SpecialMoveType::Sidekick);
			BeginLoriKick();
			// Start to start, so the pause after the kick takes care of itself
			_loriKickRepeatLeft = LegacyLoriKickPeriod;
		}
	}

	void Player::UpdateLoriKickRepeat(float timeMult)
	{
		if (_loriKickRepeatLeft <= 0.0f) {
			return;
		}
		_loriKickRepeatLeft -= timeMult;
		if (_loriKickRepeatLeft > 0.0f) {
			return;
		}
		// The original repeats while Down and Jump are held, grounded only; releasing either key ends the run
		if (_playerType != PlayerType::Lori || _levelHandler->IsReforged() ||
			!_levelHandler->PlayerActionPressed(this, PlayerAction::Down) ||
			!_levelHandler->PlayerActionPressed(this, PlayerAction::Jump)) {
			_loriKickRepeatLeft = 0.0f;
			return;
		}
		// Anything else is a wait, not a cancel; a kick still finishing doesn't block the next one
		if (!_controllableExternal ||
			(_currentSpecialMove != SpecialMoveType::None && _currentSpecialMove != SpecialMoveType::Sidekick) ||
			_suspendType != SuspendType::None || _activeModifier != Modifier::None || _inWater ||
			_dizzyTime > 0.0f || !CanJump()) {
			_loriKickRepeatLeft = std::numeric_limits<float>::epsilon();
			return;
		}
		TriggerLoriKick();
	}

	void Player::BeginLoriKick()
	{
		// One shot per kick, see HandleWeaponFire()
		_loriKickShotUsed = false;

		_externalForce.X = 4.0f * (IsFacingLeft() ? -1.0f : 1.0f);
		_speed.X = (_levelHandler->IsReforged() ? 9.3f : LegacyLoriSidekickSpeed) * (IsFacingLeft() ? -1.0f : 1.0f);
		// Armed where the kick actually begins
		if (!_levelHandler->IsReforged()) {
			_sidekickDistanceLeft = LegacyLoriSidekickDistance;
			_sidekickTime = 0.0f;
		}
		// Outside Reforged her kick stays grounded so it follows slopes, like the original
		if (_levelHandler->IsReforged()) {
			// Otherwise the player would count as grounded when the dash ends in mid-air
			SetState(ActorState::CanJump, false);
			SetState(ActorState::ApplyGravitation, false);
		}
	}

	void Player::HandleWaterAndModifierMovement(float timeMult)
	{
		// Measured in the original: Up climbs, Down descends at a fixed speed, otherwise brake then fall; no hovering.
		// The lizard copter measures the same.
		bool legacyFlight = (!_inWater && !_levelHandler->IsReforged() &&
			(_activeModifier == Modifier::Copter || _activeModifier == Modifier::LizardCopter));
		if (legacyFlight) {
			float verticalMovement = _levelHandler->PlayerVerticalMovement(this);
			if (verticalMovement < -0.3f) {
				_speed.Y = std::max(_speed.Y - LegacyFlyRiseAccel * timeMult, -LegacyVerticalSpeedLimit);
			} else if (verticalMovement > 0.3f) {
				// Assigned, not accelerated towards
				_speed.Y = LegacyFlyDescentSpeed;
			} else if (_speed.Y < 0.0f) {
				// Held/released brake like a jump, clamped at zero
				float brake = (_levelHandler->PlayerActionPressed(this, PlayerAction::Jump)
					? LegacyFlyRiseBrakeHeld : LegacyFlyRiseBrake);
				_speed.Y = std::min(_speed.Y + brake * timeMult, 0.0f);
			} else {
				_speed.Y = std::min(_speed.Y + LegacyFlyFallAccel * timeMult, LegacyFlyTerminalSpeed);
			}
			// No early return, the jump-off below must run too
		} else if (_inWater && !_levelHandler->IsReforged()) {
			// Water always pulls down and the stroke is added to it, so holding nothing sinks slowly.
			// A float-up field is left alone, since OnHandleAreaEvents() already assigned the rise.
			if (!_inFloatUpArea) {
				float playerMovement = _levelHandler->PlayerVerticalMovement(this);
				float playerMovementVelocity = std::abs(playerMovement);
				float accel = LegacyWaterGravity;
				float max = LegacyWaterSpeed;
				if (playerMovementVelocity > 0.3f) {
					float runMult = (_isRunPressed ? 2.0f : 1.0f);
					accel += LegacyWaterAccelY * runMult * (playerMovement > 0.0f ? 1.0f : -1.0f);
					max *= runMult * playerMovementVelocity;
				}
				// The cap is one net acceleration step above nominal, which gives the original's asymmetry; one tick's
				// step as a speed, so the acceleration scale comes off once
				max += std::abs(accel) / LegacyFrameRateScale;
				_speed.Y = std::clamp(_speed.Y + accel * timeMult, -max, max);
				// Standing on the bottom means no vertical speed
				if (_speed.Y > 0.0f && GetState(ActorState::CanJump)) {
					_speed.Y = 0.0f;
				}
			}
		} else {
			float playerMovement = _levelHandler->PlayerVerticalMovement(this);
			float playerMovementVelocity = std::abs(playerMovement);
			if (playerMovementVelocity > 0.3f) {
				float mult, max;
				switch (_activeModifier) {
					case Modifier::Airboard: mult = (playerMovement > 0 ? 1.0f : -0.2f); max = MaxRunningSpeed; break;
					case Modifier::LizardCopter: mult = (playerMovement > 0 ? 2.0f : -4.0f); max = (playerMovement > 0 ? MaxRunningSpeed : 2.0f * MaxRunningSpeed); break;
					default: mult = (playerMovement > 0 ? 1.0f : -1.0f); max = MaxRunningSpeed; break;
				}

				_speed.Y = std::clamp(_speed.Y + Acceleration * timeMult * mult, -max * playerMovementVelocity, max * playerMovementVelocity);
			} else {
				_speed.Y = std::max((std::abs(_speed.Y) - Deceleration * timeMult), 0.0f) * (_speed.Y < 0.0f ? -1.0f : 1.0f);
			}
		}

		if (_activeModifier == Modifier::LizardCopter && _levelHandler->PlayerActionHit(this, PlayerAction::Jump)) {
			// Jump off the copter
			// TODO: Copter shouldn't disappear immediately
			SetModifier(Modifier::None);
			// Don't trigger buttstomp or copter ears
			_wasDownPressed = true;
			_wasJumpPressed = true;
		}
	}

	void Player::HandleLookupAndCrouch(float timeMult, bool canJumpPrev)
	{
		// Look-up
		if (_levelHandler->PlayerActionPressed(this, PlayerAction::Up)) {
			if (!_wasUpPressed && _dizzyTime <= 0.0f) {
				// Check also previous CanJump to avoid animation glitches on Springs
				if (((canJumpPrev && CanJump()) || (_suspendType != SuspendType::None && _suspendType != SuspendType::SwingingVine)) && !_isLifting && std::abs(_speed.X) < std::numeric_limits<float>::epsilon()) {
					_wasUpPressed = true;

					SetAnimation(AnimState::Lookup | (_currentAnimation->State & AnimState::Hook));
				}
			}
		} else if (_wasUpPressed) {
			_wasUpPressed = false;

			SetAnimation(_currentAnimation->State & ~AnimState::Lookup);
		}

		// The original drops the crouch on leaving the ground, so no uppercut can start in mid-air
		if (!_levelHandler->IsReforged() && !CanJump() && _currentSpecialMove == SpecialMoveType::None &&
			(_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
			SetAnimation(_currentAnimation->State & ~AnimState::Crouch);
		}

		// Crouch / Buttstomp - different bindings on the ground and in the air
		if (_levelHandler->PlayerActionPressed(this, CanJump() ? PlayerAction::Down : PlayerAction::Buttstomp)) {
			if (_suspendType == SuspendType::SwingingVine) {
				// TODO: Swinging vine
			} else if (_suspendType != SuspendType::None) {
				// Jump off vine/hook
				if (IsContinuousJumpAllowed() || _levelHandler->PlayerActionHit(this, CanJump() ? PlayerAction::Down : PlayerAction::Buttstomp)) {
					_wasDownPressed = true;

					MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative | MoveType::Force);
					_suspendType = SuspendType::None;
					// Short enough to still catch a vine two tiles below, like the original
					_suspendTime = VineDropCooldown;

					SetState(ActorState::ApplyGravitation, true);
					// The original assigns a drop speed rather than releasing into free fall
					if (!_levelHandler->IsReforged()) {
						_speed.Y = LegacyVineDropSpeed;
					}
				}
			} else if (_dizzyTime <= 0.0f) {
				// Check also previous CanJump to avoid animation glitches on Springs
				if (canJumpPrev && CanJump()) {
					// Down abandons a wind-up
					CancelRevUp();
					// The original crouches once no direction is held, even while sliding; spring and run-in-place
					// carries refuse it for their whole length, a pole only for a while
					bool canCrouch = (_levelHandler->IsReforged()
						? std::abs(_speed.X) < std::numeric_limits<float>::epsilon()
						: (std::abs(_levelHandler->PlayerHorizontalMovement(this)) <= 0.4f &&
							!((_springCarry || _revUpCarry) && _keepRunningTime > 0.0f) &&
							// The carry counts down only once control is back
							!(_hPoleCarry && _keepRunningTime > HPoleKeepRunningTime - (LegacyHPoleCrouchBlockTime - PoleLaunchControlTimeout))));
					if (!_isLifting && canCrouch) {
						_wasDownPressed = true;
						if (_fireFramesLeft > 0.0f) {
							SetAnimation(AnimState::Crouch | AnimState::Shoot);
						} else {
							SetAnimation(AnimState::Crouch);
						}
					}
				} else if (!CanJump() && !_wasDownPressed && _playerType != PlayerType::Frog) {
					_wasDownPressed = true;

					// A stomp out of a run-in-place costs a quarter of the launch speed (from play reports, not measured)
					if (!_levelHandler->IsReforged() && _revUpCarry && _keepRunningTime > 0.0f) {
						float cost = RevUpMaxLaunchSpeed * LegacyRevUpButtstompCostFraction;
						_speed.X = (_speed.X > 0.0f
							? std::max(_speed.X - cost, 0.0f)
							: std::min(_speed.X + cost, 0.0f));
					} else {
						_speed.X = 0.0f;
					}
					_speed.Y = 0.0f;
					_internalForceY = 0.0f;
					_externalForce.Y = 0.0f;
					SetState(ActorState::ApplyGravitation, false);
					SetAnimation(AnimState::Buttstomp);
					SetPlayerTransition(AnimState::TransitionButtstompStart, true, false, SpecialMoveType::Buttstomp, [this]() {
						// Measured in the original, the same for all characters
						_speed.Y = (_levelHandler->IsReforged() ? 9.0f : LegacyButtstompSpeed);
						SetState(ActorState::ApplyGravitation, true);
						SetAnimation(AnimState::Buttstomp);
						PlaySfx("Buttstomp"_s, 1.0f, 0.8f);
						PlaySfx("Buttstomp2"_s);
					});
				}
			}
		} else if (_wasDownPressed) {
			_wasDownPressed = false;

			SetAnimation(_currentAnimation->State & ~AnimState::Crouch);
		}
	}

	void Player::HandleJump(float timeMult)
	{
		// Jump
		if (_levelHandler->PlayerActionPressed(this, PlayerAction::Jump)) {
			// Outside Reforged a jump only suspends a wind-up (some levels rely on it)
			if (_levelHandler->IsReforged()) {
				CancelRevUp();
			}

			// The copter's own deaf window, re-armed by each airborne press (measured in the original);
			// read before re-arming it
			if (!_levelHandler->IsReforged() && !CanJump()) {
				_copterPressAllowed = (_copterDeafLeft <= 0.0f);
				// Re-armed by a press, not a held key, so holding Jump lets the copter start on the way down
				if (!_wasJumpPressed) {
					_copterDeafLeft = LegacyJumpCooldown;
				}
				// A press during a jump's rise restores the held rise gravity, see GetGravityModifier()
				if (_riseOwnedByJump) {
					_jumpReleased = false;
				}
			} else {
				_copterPressAllowed = true;
			}

			if (!_wasJumpPressed) {
				_wasJumpPressed = true;

				if (_suspendType == SuspendType::None && _jumpTime <= 0.0f) {
					if (_isLifting && CanJump() && _currentSpecialMove == SpecialMoveType::None) {
						SetState(ActorState::CanJump, false);
						SetAnimation(_currentAnimation->State & ~(AnimState::Lookup | AnimState::Crouch));
						PlayPlayerSfx("Jump"_s);
						_carryingObject = nullptr;

						SetState(ActorState::IsSolidObject | ActorState::CollideWithSolidObjects, false);

						_isLifting = false;
						_controllable = false;
						_jumpTime = 12.0f;

						_speed.Y = -3.0f;
						_internalForceY = -0.88f;

						// Jumping from under another player: Reforged bumps sideways, the original boosts the one on top
						// (the boost speed is not measured)
						for (auto* other : _levelHandler->GetPlayers()) {
							if (other != this && other->_stackCarrying && other->_carryingObject == this) {
								if (_levelHandler->IsReforged()) {
									_speed.X = (_pos.X <= other->GetPos().X ? -1.0f : 1.0f) * PlayerBumpMinSeparationSpeed;
								} else {
									other->CancelCarryingObject();
									other->SetState(ActorState::CanJump, false);
									other->_speed.Y = -LegacyJumpSpeed;
									// Held rise gravity, so the boost is worth a full jump
									other->_jumpReleased = false;
									other->_isSpring = false;
								}
								break;
							}
						}

						SetTransition(AnimState::TransitionLiftEnd, false, [this]() {
							_controllable = true;
							SetState(ActorState::CollideWithSolidObjects, true);
						});
					} else {
						HandleSpecialJump(timeMult);
					}
				}
				// Reset only now, the double jump in HandleSpecialJump() reads how long the key was up
				_jumpUpTime = 0.0f;
			}

			if (_suspendType != SuspendType::None) {
				// Drop off hook/vine
				if (IsContinuousJumpAllowed() || _levelHandler->PlayerActionHit(this, PlayerAction::Jump)) {
					if (_suspendType == SuspendType::SwingingVine) {
						if (auto* vine = runtime_cast<Environment::SwingingVine>(_carryingObject)) {
							_speed.X = vine->GetReleaseSpeedX();
						}
						CancelCarryingObject();
						_springCooldown = 30.0f;
					} else {
						// Detach, or continuous jump would nudge the player up every frame while still attached
						_suspendType = SuspendType::None;
						// Short enough to still catch another vine above, like the original
						_suspendTime = VineDropCooldown;
						SetState(ActorState::ApplyGravitation, true);
						// The release lifts the player first; not forced, so it can't go through a ceiling
						if (!_levelHandler->IsReforged()) {
							MoveInstantly(Vector2f(0.0f, -LegacyVineJumpLift), MoveType::Relative);
						}
						// Let the jump below fire despite the cooldown of the jump that led onto the vine
						_jumpTime = 0.0f;
					}
					SetState(ActorState::CanJump, true);
					ResetAirJumps();
				}
			}

			if (!CanJump()) {
				// Extend copter time, except a counter carried over a landing
				if (_copterFramesLeft > 0.0f && !_copterCarried) {
					SetCopterFlight(GetCopterFlightTime(), IsFlyCheatActive() ? FlightType::Cheat : FlightType::Normal);
				}
			// The Down gate isn't the original's rule, but it's closer than none until the real one is known
			} else if (_currentSpecialMove == SpecialMoveType::None && _jumpTime <= 0.0f && !_levelHandler->PlayerActionPressed(this, PlayerAction::Down)) {
				// Standard jump
				if (IsContinuousJumpAllowed() || _levelHandler->PlayerActionHit(this, PlayerAction::Jump)) {
					BeginStandardJump(timeMult);
				}
			}
		} else {
			_jumpUpTime += timeMult;
			if (_wasJumpPressed) {
				_wasJumpPressed = false;

				// Cleared in both modes, so Spaz's double jump can be shortened too
				if (_internalForceY < 0.0f) {
					_internalForceY = 0.0f;
				}

				// Releasing makes the rest of the ascent heavier, but a spring launch can't be shortened
				if (!_isSpring) {
					_jumpReleased = true;
				}

				// Releasing opens the double jump window
				_doubleJumpWindowLeft = LegacyDoubleJumpWindowTime;
			}
		}
	}

	void Player::BeginStandardJump(float timeMult)
	{
		// The sidekick's ending pose is non-cancellable, so the jump drops it by hand
		if (_currentTransition != nullptr && _currentTransition->State == AnimState::TransitionUppercutEnd) {
			ForceCancelTransition();
		}

		SetState(ActorState::CanJump, false);
		_isFreefall = false;
		SetAnimation(_currentAnimation->State & (~AnimState::Lookup & ~AnimState::Crouch));
		PlayPlayerSfx("Jump"_s);
		_jumpTime = 10.0f;
		_carryingObject = nullptr;

		// Gravity may be off because of an active copter
		SetState(ActorState::ApplyGravitation, true);
		SetState(ActorState::IsSolidObject, false);

		// A jump right after landing takes the copter's counter along, see LegacyCopterLandGrace
		if (_copterLandGraceLeft > 0.0f && _copterFramesLeft > 0.0f) {
			_copterCarried = true;
			_copterLandGraceLeft = 0.0f;
		}

		if (_levelHandler->IsReforged()) {
			_speed.Y = -3.6f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f);
			_internalForceY = -1.02f - 0.07f * (1.0f - timeMult);
			if (_playerType == PlayerType::Lori) {
				_speed.Y *= 1.3f;
			}
		} else {
			// Measured in the original: a flat launch plus a quarter of the horizontal speed, no threshold
			_speed.Y = -(LegacyJumpSpeed + std::abs(_speed.X) * LegacySpeedJumpScale);
			// A jump out of a run-in-place clears a tile more than an ordinary running jump
			if (_revUpCarry && _keepRunningTime > 0.0f) {
				_speed.Y -= LegacyRevUpJumpBoost;
			}
			_jumpReleased = false;
			_riseFromFloatUp = false;
			_riseOwnedByJump = true;
		}
	}

	void Player::TryLegacyOneWayRejump(float timeMult)
	{
		// Runs after the move, because the original launches from the position the move ended on
		if (!IsOnLegacyOneWayFloor() || _currentSpecialMove != SpecialMoveType::None || !_controllable ||
			_levelHandler->PlayerActionPressed(this, PlayerAction::Down) ||
			!_levelHandler->PlayerActionPressed(this, PlayerAction::Jump)) {
			return;
		}
		// The platform counts as ground, so it restores the double jump too (OnHitFloor() never runs here)
		ResetAirJumps();
		// No `_jumpTime` gate, the original's chained launches come faster than the cooldown
		BeginStandardJump(timeMult);
	}

	void Player::HandleSpecialJump(float timeMult)
	{
		// Measured in the original: a special move needs the player stopped and the crouch established before
		// Jump, otherwise it's an ordinary jump (and the buttstomp follows if Down is still held).
		// StopSettleSpeed instead of 0, because our speed decays on a different curve.
		if (!_levelHandler->IsReforged() && CanJump() &&
			(_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch &&
			(std::abs(_speed.X) >= StopSettleSpeed || !IsSpecialMoveCrouchReady())) {
			_wasDownPressed = false;
			BeginStandardJump(timeMult);
			return;
		}

		const CharacterProfile& profile = _characterProfile;
		if (IsSpecialMoveCrouchReady()) {
			switch (profile.GroundJump) {
				case CharacterGroundJump::Jazz: {
					_controllable = false;
					SetAnimation(AnimState::Uppercut);
					SetPlayerTransition(AnimState::TransitionUppercutA, true, true, SpecialMoveType::Uppercut, [this]() {
						if (_levelHandler->IsReforged()) {
							_externalForce.Y = -1.4f;
							_speed.Y = -2.0f;
						} else {
							// Measured in the original: a plain speed, height from gravity nearly off for a fixed time
							_speed.Y = -LegacyUppercutSpeed;
							_externalForce.Y = 0.0f;
							_uppercutTimeLeft = LegacyUppercutTicks;
						}
						// Leave the moving platform, or it zeroes the vertical speed every frame
						_carryingObject = nullptr;
						SetState(ActorState::CanJump, false);
						SetPlayerTransition(AnimState::TransitionUppercutB, true, true, SpecialMoveType::Uppercut);
					});
					SetLegacyGroundMoveWindUp();
					break;
				}
				case CharacterGroundJump::Spaz: {
					_controllable = false;
					_controllableTimeout = (_levelHandler->IsReforged() ? 60.0f : 120.0f);
					SetAnimation(AnimState::Uppercut);
					SetPlayerTransition(AnimState::TransitionUppercutA, true, false, SpecialMoveType::Sidekick, [this]() {
						_externalForce.X = 8.0f * (IsFacingLeft() ? -1.0f : 1.0f);
						_speed.X = (_levelHandler->IsReforged() ? 14.4f : LegacySpazSidekickSpeed) * (IsFacingLeft() ? -1.0f : 1.0f);
						// The dash length is counted by distance, `_controllableTimeout` overshot at low frame rates
						if (!_levelHandler->IsReforged()) {
							_sidekickDistanceLeft = LegacySpazSidekickDistance;
							_sidekickTime = 0.0f;
						}
						// Gravity is off, so nothing else would clear CanJump and the dash could end in a full jump
						SetState(ActorState::CanJump, false);
						SetState(ActorState::ApplyGravitation, false);
						SetPlayerTransition(AnimState::TransitionUppercutB, true, false, SpecialMoveType::Sidekick);
					});
					SetLegacyGroundMoveWindUp();

					PlayPlayerSfx("Sidekick"_s);
					break;
				}
				case CharacterGroundJump::Lori: {
					// She kicks repeatedly while Down and Jump are held, the repeat is timed
					TriggerLoriKick();
					break;
				}
				case CharacterGroundJump::Jump: {
					// Down stays as it was, so the jump doesn't turn into a buttstomp
					BeginStandardJump(timeMult);
					break;
				}
				default: break;
			}
		} else {
			switch (profile.AirJump) {
				case CharacterAirJump::Copter: TryStartCopter(profile); break;
				case CharacterAirJump::DoubleJump: TryDoubleJump(profile, timeMult); break;
				default: break;
			}
		}
	}

	void Player::TryStartCopter(const CharacterProfile& profile)
	{
		// Falling is read at the start of the frame, the original applies gravity after this test
		float copterSpeedY = (_levelHandler->IsReforged() ? _speed.Y : _frameStartSpeedY);
		// The original refuses to start the copter past a fall speed, already flying is exempt
		bool alreadyFlying = ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter);
		// `_copterPressAllowed` is the deaf window every press re-arms, see HandleJump()
		bool copterSpeedAllowed = (copterSpeedY > 0.01f && (_levelHandler->IsReforged() || alreadyFlying ||
			(_copterPressAllowed && copterSpeedY < LegacyCopterEngageMaxSpeed)));
		bool copterTimeLeft = (_copterAirTime < profile.CopterDurationMax / LegacyFrameRateScale);
		if (copterSpeedAllowed && copterTimeLeft && !CanJump() && (_currentAnimation->State & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
			SetState(ActorState::ApplyGravitation, false);
			// Outside Reforged engaging only caps the descent, a slower fall winds up from there
			float descentSpeed = GetCopterDescentSpeed(profile);
			if (_levelHandler->IsReforged()) {
				_speed.Y = descentSpeed;
			} else if (alreadyFlying) {
				// A tap that keeps the ears going leaves the wind-up alone, measured at a speed of 3
			} else if (profile.CopterSpeedY >= 0.0f) {
				_speed.Y = std::min(copterSpeedY, descentSpeed);
			} else {
				// Rising ears keep the fall and brake it, see LegacyCopterAscentBrake
				_speed.Y = copterSpeedY;
			}
			_externalForce.Y = 0.0f;
			if ((_currentAnimation->State & AnimState::Copter) != AnimState::Copter) {
				SetAnimation(AnimState::Copter);
			}
			if (_copterFramesLeft <= 0.0f) {
				_copterCarried = false;
			}
			if (!_copterCarried) {
				SetCopterFlight(GetCopterFlightTime(), FlightType::Normal);
			}
#if defined(WITH_AUDIO)
			if (_copterSound == nullptr) {
				_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
				if (_copterSound != nullptr) {
					_copterSound->SetLooping(true);
				}
			}
#endif
		}
	}

	void Player::TryDoubleJump(const CharacterProfile& profile, float timeMult)
	{
		// Outside Reforged: not during a spring rise, only while falling (read at the start of the frame)
		// and within a window after the first press was released. Matches the original.
		bool inDoubleJumpWindow = (_levelHandler->IsReforged() ||
			(!_isSpring && _frameStartSpeedY > 0.0f && _doubleJumpWindowLeft > 0.0f && _jumpUpTime >= LegacyDoubleJumpKeyUpTime));
		if (!CanJump() && _doubleJumpCount < profile.DoubleJumpCountMax && inDoubleJumpWindow) {
			_doubleJumpCount++;
			_isFreefall = false;

			float speedX = (profile.DoubleJumpSpeedX != 0.0f
				? profile.DoubleJumpSpeedX * LegacyFrameRateScale * (IsFacingLeft() ? -1.0f : 1.0f)
				: 0.0f);
			if (_levelHandler->IsReforged()) {
				// Scaled by the vertical speed against its default of 8
				float jumpScale = profile.DoubleJumpSpeedY / CharacterProfile::GetDefault(PlayerType::Spaz).DoubleJumpSpeedY;
				_internalForceY = (-1.15f - 0.1f * (1.0f - timeMult)) * jumpScale;
				_speed.Y = (-0.6f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f)) * jumpScale;
				_speed.X = std::clamp(_speed.X * 0.4f, -1.0f, 1.0f) + speedX;
			} else {
				// Measured in the original: a plain speed like the first jump, and the horizontal speed is dropped
				_speed.Y = profile.DoubleJumpSpeedY * LegacyFrameRateScale;
				// Measured with 5: no faster than the walk speed, or the dash speed while the dash is up
				float capX = (IsDashActive() ? LegacyDashSpeed : LegacyWalkSpeed);
				_speed.X = std::clamp(speedX, -capX, capX);
				_internalForceY = 0.0f;
				_jumpReleased = false;
				_riseFromFloatUp = false;
				_riseOwnedByJump = true;
				// A horizontal spring's speed rebuilds instead, at a rate set by the launch speed (measured)
				if (_springLaunchSpeedX > 0.0f) {
					_springRebuildAccel = _springLaunchSpeedX * LegacyFrameRateScale / LegacySpringRebuildTicks;
					// One rebuild per launch
					_springLaunchSpeedX = 0.0f;
				}
			}

			PlayPlayerSfx("DoubleJump"_s);

			SetTransition(AnimState::Spring, false);
		}
	}

	void Player::ResetAirJumps()
	{
		_doubleJumpCount = 0;
		_copterAirTime = 0.0f;
	}

	void Player::RefreshCharacterProfile()
	{
		_characterProfile = _levelHandler->GetCharacterProfile(_playerType);
		if (_playerType == PlayerType::Frog || IsBird()) {
			// The frog and the birds have no jump moves, a script can't give them any
			CharacterProfile defaultProfile = CharacterProfile::GetDefault(_playerType);
			_characterProfile.AirJump = defaultProfile.AirJump;
			_characterProfile.GroundJump = defaultProfile.GroundJump;
		}
	}

	float Player::GetCopterDescentSpeed(const CharacterProfile& profile) const
	{
		return (_levelHandler->IsReforged() ? 1.5f : LegacyCopterDescentSpeed) * profile.CopterSpeedY;
	}

	void Player::SetLegacyGroundMoveWindUp()
	{
		// The metadata's transition is longer than the original's wind-up
		if (!_levelHandler->IsReforged() && _currentTransition != nullptr && _currentTransition->State == AnimState::TransitionUppercutA) {
			if (_renderer.AnimDuration > 0.0f) {
				_renderer.AnimTime *= LegacyGroundMoveWindUp / _renderer.AnimDuration;
			}
			_renderer.AnimDuration = LegacyGroundMoveWindUp;
		}
	}

	float Player::GetCopterFlightTime() const
	{
		return (_levelHandler->IsReforged() ? 70.0f : LegacyCopterFlightTime);
	}

	bool Player::IsCopterPushActive() const
	{
		return (_characterProfile.CopterSpeedX != 0.0f && _activeModifier == Modifier::None &&
			_suspendType == SuspendType::None && (_currentAnimation->State & AnimState::Copter) == AnimState::Copter);
	}

	void Player::ApplyCopterPush(float timeMult)
	{
		// Measured in JJ2+: the speed is brought down to the push, then a 32nd of it is added, capped by walk/dash speed
		float push = std::abs(_characterProfile.CopterSpeedX);
		float sign = (IsFacingLeft() ? -1.0f : 1.0f) * (_characterProfile.CopterSpeedX < 0.0f ? -1.0f : 1.0f);
		float along = std::min(_speed.X * sign, push * LegacyFrameRateScale) + (push / 32.0f) * LegacyFrameRateScaleSqr * timeMult;
		float cap = (_levelHandler->IsReforged()
			? (_isRunPressed ? MaxDashingSpeed : MaxRunningSpeed)
			: (IsDashActive() ? LegacyDashSpeed : LegacyWalkSpeed));
		_speed.X = std::clamp(along * sign, -cap, cap);
	}

	void Player::HandleWeaponFire(bool areaWeaponAllowed)
	{
		// Fire
		bool weaponInUse = false;
		// Measured in the original: no shooting while riding a pinball paddle
		bool onPinballPaddle = (!_levelHandler->IsReforged() && _onPinballPaddleTime > 0.0f);
		// Measured in the original: one shot per Lori's kick, the weapon cooldown still applies on top
		bool loriKickShotSpent = (!_levelHandler->IsReforged() && _playerType == PlayerType::Lori &&
			_currentSpecialMove == SpecialMoveType::Sidekick && _loriKickShotUsed);
		if (_weaponAllowed && areaWeaponAllowed && !onPinballPaddle && !loriKickShotSpent &&
			_levelHandler->PlayerActionPressed(this, PlayerAction::Fire)) {
			if (!_isLifting && _suspendType != SuspendType::SwingingVine && !_canPushFurther) {
				if (_playerType == PlayerType::Bird) {
					// The first bird has no shot, Fire is its beak attack in HandleBirdMovement()
				} else if (_playerType == PlayerType::Bird2) {
					// The second bird shoots on the press, not while held
					if (_levelHandler->PlayerActionHit(this, PlayerAction::Fire) && _inventory.WeaponAmmo[(std::int32_t)_currentWeapon] != 0) {
						// A shot wakes a perched bird (holding the key doesn't keep it awake), it shoots from its flying pose
						_birdIdleTime = 0.0f;
						if (_birdPerched || _birdStanding) {
							LeaveBirdPerch();
							SetAnimation(AnimState::Idle);
						}
						// Virtual FireCurrentWeapon(), so in multiplayer the server handles it
						if (FireCurrentWeapon(_currentWeapon)) {
							_levelHandler->PlayerExecuteRumble(this, "Fire"_s);
						}
					}
				} else if (_playerType == PlayerType::Frog) {
					if (_currentTransition == nullptr && std::abs(_speed.X) < 0.1f && std::abs(_speed.Y) < 0.1f && std::abs(_externalForce.X) < 0.1f && std::abs(_externalForce.Y) < 0.1f) {
						PlayPlayerSfx("Tongue"_s, 0.8f);

						_controllable = false;
						_controllableTimeout = 120.0f;

						SetTransition(_currentAnimation->State | AnimState::Shoot, false, [this]() {
							_controllable = true;
							_controllableTimeout = 0.0f;
						});
					}
				} else if (_inventory.WeaponAmmo[(std::int32_t)_currentWeapon] != 0) {
					_wasFirePressed = true;

					// Shooting has higher priority than pushing if object can't be moved further anymore
					_pushFramesLeft = 0.0f;

					bool weaponCooledDown = (_weaponCooldown <= 0.0f);
					weaponInUse = FireCurrentWeapon(_currentWeapon);
					if (weaponInUse) {
						// The kick's one shot is spent only when a shot really leaves, cleared in BeginLoriKick()
						if (_currentSpecialMove == SpecialMoveType::Sidekick && _playerType == PlayerType::Lori) {
							_loriKickShotUsed = true;
						}

						if (ShouldCancelTransitionOnFire()) {
							ForceCancelTransition();
						}

						SetShootingAnimation();
						// Rewind the animation, if it should be played only once
						if (weaponCooledDown) {
							if (_currentAnimation->LoopMode == AnimationLoopMode::Once) {
								_renderer.AnimTime = 0.0f;
							}
							auto rumbleEffect = (_currentWeapon == WeaponType::Toaster || _currentWeapon == WeaponType::Thunderbolt ? "FireWeak"_s : "Fire"_s);
							_levelHandler->PlayerExecuteRumble(this, rumbleEffect);
						}

						_fireFramesLeft = 20.0f;
					}
				}
			}
		} else if (_wasFirePressed) {
			_wasFirePressed = false;

			// TNT, capped by default, is placed by every press as the weapon of the game places it
			if (_currentWeapon == WeaponType::TNT || _levelHandler->GetWeaponDefinition(_currentWeapon).Options.Style != WeaponStyle::Capped) {
				_weaponCooldown = 0.0f;
			}
		}

		if (_weaponSoundType != WeaponType::Unknown) {
			if (weaponInUse && _currentWeapon == _weaponSoundType) {
#if defined(WITH_AUDIO)
				if (_weaponSound != nullptr) {
					_weaponSound->SetPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
				}
#endif
			} else {
				// A weapon switched while firing ends it too (needed for remote players)
				bool wasThunderbolt = (_weaponSoundType == WeaponType::Thunderbolt);
				StopWeaponSfx();

				if (wasThunderbolt) {
					PlayPlayerSfx("WeaponThunderboltEnd"_s, 0.8f);
					_levelHandler->PlayerExecuteRumble(this, "Fire"_s);
				}
			}
		}
	}

	bool Player::OnDraw(RenderQueue& renderQueue)
	{
		// A pose without a gunspot has no muzzle to put the flare on, see GetGunspotOffset()
		Vector2i gunspotOffset;
		if (_weaponFlareTime > 0.0f && !_inWater && _currentTransition == nullptr && GetGunspotOffset(gunspotOffset)) {
			auto* res = _metadata->FindAnimation(WeaponFlare);
			if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
				// A recolored player has indexed anims, so the flare needs the PaletteRemap shader too
				bool flareIndexed = ((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);

				auto& command = _weaponFlareCommand;
				if (command == nullptr) {
					command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
					command->GetMaterial().SetBlendingEnabled(true);
				}

				bool shaderChanged = (flareIndexed
					? command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::PaletteRemap))
					: command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite));
				if (shaderChanged) {
					command->GetMaterial().ReserveUniformsDataMemory();
					command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
					//command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
					command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

					auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
					if (textureUniform && textureUniform->GetIntValue(0) != 0) {
						textureUniform->SetIntValue(0); // GL_TEXTURE0
					}
					if (flareIndexed) {
						auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
						if (paletteUniform != nullptr) {
							paletteUniform->SetIntValue(1); // GL_TEXTURE1
						}
					}
				}

				Vector2i texSize = res->Base->TextureDiffuse->GetSize();
				std::int32_t curAnimFrame = res->FrameOffset + (_weaponFlareFrame % res->FrameCount);
				Recti frameRect = res->Base->GetFrameRect(curAnimFrame);
				float texScaleX = (float(frameRect.W) / float(texSize.X));
				float texBiasX = (float(frameRect.X) / float(texSize.X));
				float texScaleY = (float(frameRect.H) / float(texSize.Y));
				float texBiasY = (float(frameRect.Y) / float(texSize.Y));

				float scaleY = std::max(_weaponFlareTime / 8.0f, 0.4f) * GetCharacterTraits(_playerType).WeaponFlareScaleY;

				bool facingLeft = IsFacingLeft();
				bool lookUp = (_currentAnimation->State & AnimState::Lookup) == AnimState::Lookup;

				float gunspotPosX, gunspotPosY;
				if (lookUp) {
					gunspotPosX = _pos.X + gunspotOffset.X * (facingLeft ? 1 : -1);
					gunspotPosY = _pos.Y - (gunspotOffset.Y - 3) - res->Base->FrameDimensions.Y;
				} else {
					gunspotPosX = _pos.X + (gunspotOffset.X - 7) * (facingLeft ? 1 : -1);
					gunspotPosY = _pos.Y - gunspotOffset.Y;
					if (facingLeft) {
						texBiasX += texScaleX;
						texScaleX *= -1.0f;
					}
				}

				if (!PreferencesCache::UnalignedViewport) {
					gunspotPosX = floorFast(gunspotPosX);
					gunspotPosY = floorFast(gunspotPosY);
				}

				auto instanceBlock = command->GetInstanceBlock();
				instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				// The quad is sized by the frame's own area, not the whole cell
				instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(frameRect.W, frameRect.H * scaleY);
				instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(1.0f, 1.0f, 1.0f, 1.8f);

				Matrix4x4f worldMatrix = Matrix4x4f::Translation(gunspotPosX, gunspotPosY, 0.0f);
				if (lookUp) {
					worldMatrix.RotateZ(-fRadAngle90);
				}
				worldMatrix.Translate(frameRect.W * -0.5f, frameRect.H * scaleY * -0.5f, 0.0f);
				command->SetTransformation(worldMatrix);
				command->SetLayer(_renderer.GetLayer() + 2);
				command->GetMaterial().SetTexture(*res->Base->TextureDiffuse.get());
				if (flareIndexed) {
					// The default palette keeps the flare's original colors whatever the fur recolor is
					Texture* defaultPalette = ContentResolver::Get().GetPaletteTexture();
					if (defaultPalette != nullptr) {
						command->GetMaterial().SetTexture(1, *defaultPalette);
					}
					RHI::UniformCache* palOffsetUniform = instanceBlock->GetUniform(Material::PaletteOffsetUniformName);
					if (palOffsetUniform != nullptr) {
						palOffsetUniform->SetFloatValue(0.0f);
					}
				}

				renderQueue.AddCommand(command.get());
			}
		}

		DrawShield(renderQueue, _activeShield, _activeShieldTime, _metadata, _levelHandler->GetElapsedFrames(), _pos, _renderer.GetLayer(), _shieldRenderCommands);

		return ActorBase::OnDraw(renderQueue);
	}

	void Player::SpawnSugarRushStar(ILevelHandler* levelHandler, Metadata* metadata, Vector2f pos, std::uint16_t baseLayer)
	{
		auto* tilemap = levelHandler->TileMap();
		if (tilemap == nullptr || metadata == nullptr) {
			return;
		}

		auto* res = metadata->FindAnimation(SugarRush);
		if (res == nullptr || res->Base->TextureDiffuse == nullptr) {
			return;
		}

		Vector2i texSize = res->Base->TextureDiffuse->GetSize();
		std::int32_t frame = res->FrameOffset + Random().Next(0, res->FrameCount);
		Recti frameRect = res->Base->GetFrameRect(frame);
		Vector2i frameOffset = res->Base->GetFrameOffset(frame);
		float speedX = Random().FastFloat(-4.0f, 4.0f);

		Tiles::TileMap::DestructibleDebris debris = { };
		debris.Pos = pos;
		debris.Depth = baseLayer - 2;
		debris.Size = Vector2f((float)frameRect.W, (float)frameRect.H);
		debris.FrameOffset = Vector2f(frameOffset.X + (frameRect.W - res->Base->FrameDimensions.X) * 0.5f,
			frameOffset.Y + (frameRect.H - res->Base->FrameDimensions.Y) * 0.5f);
		debris.Speed = Vector2f(speedX, Random().FastFloat(-4.0f, -2.2f));
		debris.Acceleration = Vector2f(0.0f, 0.2f);

		debris.Scale = Random().FastFloat(0.1f, 0.5f);
		debris.ScaleSpeed = -0.002f;
		debris.Angle = Random().FastFloat(0.0f, fTwoPi);
		debris.AngleSpeed = speedX * 0.04f;
		debris.Alpha = 1.0f;
		debris.AlphaSpeed = -0.018f;

		debris.Time = 160.0f;

		debris.TexScaleX = (float(frameRect.W) / float(texSize.X));
		debris.TexBiasX = (float(frameRect.X) / float(texSize.X));
		debris.TexScaleY = (float(frameRect.H) / float(texSize.Y));
		debris.TexBiasY = (float(frameRect.Y) / float(texSize.Y));

		debris.DiffuseTexture = res->Base->TextureDiffuse.get();
		// Recolor through the palette when the sprite is indexed (-1 = baked/RGBA, behavior unchanged)
		debris.PaletteOffset = (((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);

		tilemap->CreateDebris(debris);
	}

	void Player::DrawShield(RenderQueue& renderQueue, ShieldType shieldType, float shieldTime, Metadata* metadata,
		float elapsedFrames, Vector2f pos, std::uint16_t baseLayer, std::unique_ptr<RenderCommand> (&shieldRenderCommands)[2])
	{
		// Local aliases, so the code is shared by Player::OnDraw() and RemoteActor
		auto& _shieldRenderCommands = shieldRenderCommands;
		Metadata* const _metadata = metadata;
		const Vector2f _pos = pos;
		const float _activeShieldTime = shieldTime;

		switch (shieldType) {
			case ShieldType::Fire: {
				auto* res = _metadata->FindAnimation(ShieldFire);
				if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
					// The fixed-function tier draws the noise itself, over coordinates that run across several tiles
					if (res->Base->TextureDiffuse->GetWrap() != SamplerWrapping::Repeat) {
						res->Base->TextureDiffuse->SetWrap(SamplerWrapping::Repeat);
					}

					constexpr float PosMultiplier = 0.003f;
					float frames = elapsedFrames;
					float shieldAlpha = std::min(_activeShieldTime * 0.01f, 1.0f);
					float shieldScale = std::min(_activeShieldTime * 0.016f + 0.6f, 1.0f);
					float shieldSize = 70.0f * shieldScale;

					float shieldPosX = _pos.X - shieldSize * 0.5f;
					float shieldPosY = _pos.Y - shieldSize * 0.5f;

					if (!PreferencesCache::UnalignedViewport) {
						shieldPosX = floorFast(shieldPosX);
						shieldPosY = floorFast(shieldPosY);
					}

					{
						auto& command = _shieldRenderCommands[0];
						if (command == nullptr) {
							command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
							command->GetMaterial().SetBlendingEnabled(true);
						}

						if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::ShieldFire))) {
							command->GetMaterial().ReserveUniformsDataMemory();
							command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
							command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

							auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->GetIntValue(0) != 0) {
								textureUniform->SetIntValue(0); // GL_TEXTURE0
							}
						}

						auto instanceBlock = command->GetInstanceBlock();
						instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(
							frames * -0.008f + _pos.X * PosMultiplier, frames * 0.006f - sinApprox(frames * 0.006f),
							-sinApprox(frames * 0.015f), frames * 0.006f + _pos.Y * PosMultiplier);
						instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(shieldSize, shieldSize);
						instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(2.0f, 2.0f, 0.8f, 0.9f * shieldAlpha);

						command->SetTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
						command->SetLayer(baseLayer - 4);
						command->GetMaterial().SetTexture(*res->Base->TextureDiffuse.get());

						renderQueue.AddCommand(command.get());
					}
					{
						auto& command = _shieldRenderCommands[1];
						if (command == nullptr) {
							command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
							command->GetMaterial().SetBlendingEnabled(true);
						}

						if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::ShieldFire))) {
							command->GetMaterial().ReserveUniformsDataMemory();
							command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
							command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

							auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->GetIntValue(0) != 0) {
								textureUniform->SetIntValue(0); // GL_TEXTURE0
							}
						}

						auto instanceBlock = command->GetInstanceBlock();
						instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(
							frames * 0.006f, sinApprox(frames * 0.006f) + _pos.Y * PosMultiplier,
							sinApprox(frames * 0.015f) + _pos.X * PosMultiplier, frames * -0.006f);
						instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(shieldSize, shieldSize);
						instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(2.0f, 2.0f, 1.0f, 1.0f * shieldAlpha);

						command->SetTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
						command->SetLayer(baseLayer + 4);
						command->GetMaterial().SetTexture(*res->Base->TextureDiffuse.get());

						renderQueue.AddCommand(command.get());
					}
				}
				break;
			}
			case ShieldType::Water: {
				auto* res = _metadata->FindAnimation(ShieldWater);
				if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
					float frames = elapsedFrames;
					float shieldAlpha = std::min(_activeShieldTime * 0.01f, 1.0f);
					float shieldScale = std::min(_activeShieldTime * 0.016f + 0.6f, 1.0f);

					auto& command = _shieldRenderCommands[1];
					if (command == nullptr) {
						command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
						command->GetMaterial().SetBlendingEnabled(true);
					}

					bool shieldIndexed = ((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);
					if (ContentResolver::Get().ConfigureSpriteShader(*command, shieldIndexed)) {
						// Water shield blends additively
						command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
					}

					Vector2i texSize = res->Base->TextureDiffuse->GetSize();
					std::int32_t curAnimFrame = res->FrameOffset + ((std::int32_t)(frames * 0.24f) % res->FrameCount);
					Recti frameRect = res->Base->GetFrameRect(curAnimFrame);
					Vector2i frameOffset = res->Base->GetFrameOffset(curAnimFrame);
					float texScaleX = (float(frameRect.W) / float(texSize.X));
					float texBiasX = (float(frameRect.X) / float(texSize.X));
					float texScaleY = (float(frameRect.H) / float(texSize.Y));
					float texBiasY = (float(frameRect.Y) / float(texSize.Y));

					// The quad is sized by the frame, but the cell is centered on the player, or the shield would wobble
					float shieldPosX = _pos.X + (frameOffset.X - res->Base->FrameDimensions.X * 0.5f) * shieldScale;
					float shieldPosY = _pos.Y + (frameOffset.Y - res->Base->FrameDimensions.Y * 0.5f) * shieldScale;

					if (!PreferencesCache::UnalignedViewport) {
						shieldPosX = floorFast(shieldPosX);
						shieldPosY = floorFast(shieldPosY);
					}

					auto instanceBlock = command->GetInstanceBlock();
					instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(frameRect.W * shieldScale, frameRect.H * shieldScale);
					instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(1.0f, 1.0f, 1.0f, shieldAlpha);

					command->SetTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
					command->SetLayer(baseLayer + 4);
					// Use the default palette (offset 0) so the shield keeps its own colors, not the player's fur recolor
					ContentResolver::Get().BindSpritePalette(*command, *res->Base->TextureDiffuse.get(), shieldIndexed, res->PaletteOffset);

					renderQueue.AddCommand(command.get());
				}
				break;
			}
			case ShieldType::Lightning: {
				auto* res = _metadata->FindAnimation(ShieldLightning);
				if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
					// The fixed-function tier draws the noise itself, over coordinates that run across several tiles
					if (res->Base->TextureDiffuse->GetWrap() != SamplerWrapping::Repeat) {
						res->Base->TextureDiffuse->SetWrap(SamplerWrapping::Repeat);
					}

					constexpr float PosMultiplier = 0.001f;
					float frames = elapsedFrames;
					float shieldAlpha = std::min(_activeShieldTime * 0.01f, 1.0f);
					float shieldScale = std::min(_activeShieldTime * 0.016f + 0.6f, 1.0f);
					float shieldSize = 70.0f * shieldScale + sinApprox(frames * 0.06f) * 4.0f;

					float shieldPosX = _pos.X - shieldSize * 0.5f;
					float shieldPosY = _pos.Y - shieldSize * 0.5f;

					if (!PreferencesCache::UnalignedViewport) {
						shieldPosX = floorFast(shieldPosX);
						shieldPosY = floorFast(shieldPosY);
					}

					{
						auto& command = _shieldRenderCommands[0];
						if (command == nullptr) {
							command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
							command->GetMaterial().SetBlendingEnabled(true);
						}

						if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::ShieldLightning))) {
							command->GetMaterial().ReserveUniformsDataMemory();
							command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
							command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

							auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->GetIntValue(0) != 0) {
								textureUniform->SetIntValue(0); // GL_TEXTURE0
							}
						}

						auto instanceBlock = command->GetInstanceBlock();
						instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(
							frames * -0.008f + _pos.X * PosMultiplier, frames * 0.006f - sinApprox(frames * 0.006f) + _pos.Y * PosMultiplier,
							-sinApprox(frames * 0.015f), frames * 0.006f);
						instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(shieldSize, shieldSize);
						instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(2.0f, 2.0f, 0.8f, 0.9f * shieldAlpha);

						command->SetTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
						command->SetLayer(baseLayer - 4);
						command->GetMaterial().SetTexture(*res->Base->TextureDiffuse.get());

						renderQueue.AddCommand(command.get());
					}
					{
						auto& command = _shieldRenderCommands[1];
						if (command == nullptr) {
							command = std::make_unique<RenderCommand>(RenderCommand::Type::Sprite);
							command->GetMaterial().SetBlendingEnabled(true);
						}

						if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::ShieldLightning))) {
							command->GetMaterial().ReserveUniformsDataMemory();
							command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
							command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

							auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->GetIntValue(0) != 0) {
								textureUniform->SetIntValue(0); // GL_TEXTURE0
							}
						}

						auto* instanceBlock = command->GetInstanceBlock();
						instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(
							frames * 0.006f + _pos.X * PosMultiplier, sinApprox(frames * 0.006f) + _pos.Y * PosMultiplier,
							sinApprox(frames * 0.015f), frames * -0.006f);
						instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(shieldSize, shieldSize);
						instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(2.0f, 2.0f, 1.0f, shieldAlpha);

						command->SetTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
						command->SetLayer(baseLayer + 4);
						command->GetMaterial().SetTexture(*res->Base->TextureDiffuse.get());

						renderQueue.AddCommand(command.get());
					}
				}
				break;
			}
		}
	}

	void Player::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (!_lightHiddenByScript) {
			OnEmitRemotedLights(lights);
		}

		for (std::int32_t i = 0; i < (std::int32_t)_trail.size(); i++) {
			lights.emplace_back(_trail[i]);
		}
	}

	void Player::OnEmitRemotedLights(SmallVectorImpl<LightEmitter>& lights)
	{
		// The trail is a local decoration, it's not synced
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = 1.0f;
		if (_sugarRushLeft > 0.0f) {
			light.Brightness = 0.4f;
			light.RadiusNear = 60.0f;
			light.RadiusFar = 180.0f;
		} else {
			light.RadiusNear = 40.0f;
			light.RadiusFar = 110.0f;
		}
	}

	bool Player::OnPerish(ActorBase* collider)
	{
		if (_currentTransition != nullptr && _currentTransition->State == AnimState::TransitionDeath) {
			return false;
		}

		SetState(ActorState::IsInvulnerable, true);

		ForceCancelTransition();

		Metadata* metadataBeforeMorph = (_playerType == PlayerType::Frog ? RequestMorphMetadata(_playerTypeBeforeMorph) : nullptr);
		if (metadataBeforeMorph != nullptr) {
			// Back to the character the frog was taken from
			ApplyMorphType(_playerTypeBeforeMorph, metadataBeforeMorph);
			// The recolor scheme is per character, see MorphTo()
			RefreshColorPalette();

			// Refresh animation state
			AnimState prevState = (_currentAnimation != nullptr ? _currentAnimation->State : AnimState::Idle);
			_currentSpecialMove = SpecialMoveType::None;
			_currentAnimation = nullptr;
			if (!SetAnimation(prevState)) {
				SetAnimation(AnimState::Idle);
			}

			// Morph to original type with animation and then trigger death
			SetPlayerTransition(AnimState::TransitionFromFrog, false, true, SpecialMoveType::None, [this]() {
				OnPerishInner();
			});
		} else {
			OnPerishInner();
		}

		return false;
	}

	void Player::OnUpdateHitbox()
	{
		if (_playerType == PlayerType::Spectate) {
			AABBInner = {};
			return;
		}

		// The hitbox must stay constant relative to the hotspot, otherwise the player gets stuck at walls
		if (IsBird()) {
			// Only the bird's body, centered on the hotspot, a perched bird stands lower
			float halfWidth = (_levelHandler->IsReforged() ? 11.0f : 10.0f);
			float bottom = (_birdStanding ? 12.0f + BirdStandingDrop : 12.0f);
			AABBInner = AABBf(_pos.X - halfWidth, _pos.Y - 8.0f, _pos.X + halfWidth, _pos.Y + bottom);
		} else if (_levelHandler->IsReforged()) {
			AABBInner = AABBf(_pos.X - 11.0f, _pos.Y + 8.0f - 18.0f, _pos.X + 11.0f, _pos.Y + 8.0f + 12.0f);
		} else {
			AABBInner = AABBf(_pos.X - 10.0f, _pos.Y + 8.0f - 16.0f, _pos.X + 10.0f, _pos.Y + 8.0f + 12.0f);
		}
	}

	bool Player::OnHandleCollision(ActorBase* other)
	{
		ZoneScoped;

		bool handled = false;
		bool removeSpecialMove = false;
		if (auto* turtleShell = runtime_cast<Enemies::TurtleShell>(other)) {
			if (IsButtstompStarting() && _sugarRushLeft <= 0.0f) {
				// Buttstomp is in starting transition, do nothing yet unless sugar rush is active
			} else if (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f) {
				other->DecreaseHealth(INT32_MAX, this);
				handled = true;

				if (_currentSpecialMove == SpecialMoveType::Buttstomp) {
					removeSpecialMove = true;
					_speed.Y *= -0.6f;
					SetState(ActorState::CanJump, false);
				}
			}
		} else if (auto* enemy = runtime_cast<Enemies::EnemyBase>(other)) {
			// The beak attack hurts only in Reforged or with JJ2+'s `canHurt`
			bool birdCharge = (_birdCharging && (_levelHandler->IsReforged() || _characterProfile.CanHurt));
			bool rammingFrozen = IsRammingFrozen(enemy);
			bool otherAttack = (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f || rammingFrozen);
			if (IsButtstompStarting() && _sugarRushLeft <= 0.0f) {
				// Buttstomp is in starting transition, do nothing yet unless sugar rush or shield is active
			} else if (otherAttack || birdCharge) {
				// The charge hurts the same enemy only once per BirdChargeHitInterval
				bool canHit = (otherAttack || _birdChargeHitCooldown <= 0.0f || _birdChargeLastHit.lock().get() != enemy);
				if (!enemy->IsInvulnerable() && canHit) {
					enemy->DecreaseHealth(4, this);
					handled = true;

					if (birdCharge) {
						_birdChargeLastHit = enemy->shared_from_this();
						_birdChargeHitCooldown = BirdChargeHitInterval;
					}

					Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 2), Explosion::Type::Small);

					if (_sugarRushLeft > 0.0f) {
						if (!_inWater && CanJump()) {
							_speed.Y = 3;
							SetState(ActorState::CanJump, false);
							_externalForce.Y = -0.6f;
						}
						_speed.Y *= -0.5f;
					}
					if (_currentSpecialMove == SpecialMoveType::Buttstomp) {
						removeSpecialMove = true;
						if (_levelHandler->IsReforged()) {
							_speed.Y *= -0.6f;
						} else {
							// Measured in the original: a flat bounce, not a fraction of the impact, at the released rise rate
							_speed.Y = -LegacyEnemyStompBounce;
							_jumpReleased = true;
							_riseOwnedByJump = false;
						}
						SetState(ActorState::CanJump, false);
						GrantInvulnerability(FrameTimer::FramesPerSecond, InvulnerableType::Transient);
					} else if (_currentSpecialMove != SpecialMoveType::None && enemy->GetHealth() > 0) {
						removeSpecialMove = true;
						_externalForce.X = 0.0f;
						_externalForce.Y = 0.0f;

						if (_currentSpecialMove == SpecialMoveType::Sidekick) {
							_speed.X *= 0.5f;
						}
					}

					if (_currentSpecialMove == SpecialMoveType::None && _sugarRushLeft <= 0.0f && rammingFrozen) {
						// As measured in JJ2+: back at half the speed, up at a flat 6, and an enemy that survives it thaws
						_speed.X *= -0.5f;
						_speed.Y = FrozenRamBounceSpeedY;
						SetState(ActorState::CanJump, false);
						if (enemy->GetHealth() > 0) {
							enemy->ThawOnNextUpdate();
						}
					}

					_levelHandler->PlayerExecuteRumble(this, "Land"_s);
				}
			} else if (enemy->CanHurtPlayer()) {
				if (!IsInvulnerable()) {
					if (_activeShieldTime > 0.0f) {
						DecreaseShieldTime(5.0f * FrameTimer::FramesPerSecond);
						float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
						GrantInvulnerability(invulnerableTime, InvulnerableType::Blinking);
						PlayPlayerSfx("HurtSoft"_s);
					} else {
						TakeDamage(1, 4 * (_pos.X > enemy->GetPos().X ? 1.0f : -1.0f));
					}
				}
			}
		} else if (auto* spring = runtime_cast<Environment::Spring>(other)) {
			// Hitbox only, plus the path taken, a spring can be skipped over at a low frame rate
			if (_controllableExternal && (_currentTransition == nullptr || _currentTransition->State != AnimState::TransitionLedgeClimb) && _springCooldown <= 0.0f &&
				(spring->AABBInner.Overlaps(AABBInner) || HasCrossedOver(spring))) {
				Vector2f force = spring->Activate();
				OnHitSpring(spring->GetPos(), force, spring->KeepSpeedX, spring->KeepSpeedY, removeSpecialMove);
			}

			handled = true;
		} else if (auto* bonusWarp = runtime_cast<Environment::BonusWarp>(other)) {
			if (_currentTransition == nullptr || _currentTransitionCancellable) {
				auto cost = bonusWarp->GetCost();
				if (cost <= _inventory.Coins) {
					_inventory.Coins -= cost;
					bonusWarp->Activate(this);

					// Convert remaing coins to gems and equivalent score
					if (_levelHandler->WarpsTransmuteCoins()) {
						_inventory.Gems[0] += _inventory.Coins;
						AddScore(_inventory.Coins * 100);
						_inventory.Coins = 0;
					}
				} else if (_bonusWarpTimer <= 0.0f) {
					_levelHandler->HandlePlayerCoins(this, _inventory.Coins, _inventory.Coins);
					PlaySfx("BonusWarpNotEnoughCoins"_s);

					_bonusWarpTimer = 400.0f;
				}
			}

			handled = true;
		} else if (auto* otherPlayer = runtime_cast<Player>(other)) {
			// Players bump each other apart (both alive and not warping), vertical contact is left to stacking
			if (_levelHandler->CanPlayersCollide() && _health > 0 && otherPlayer->_health > 0 &&
				(_currentTransition == nullptr ||
				 (_currentTransition->State != AnimState::TransitionWarpIn && _currentTransition->State != AnimState::TransitionWarpInFreefall &&
				  _currentTransition->State != AnimState::TransitionWarpOut && _currentTransition->State != AnimState::TransitionWarpOutFreefall))) {
				ApplyPlayerBump(*otherPlayer, /*stackingEnabled:*/ true);
				// Handled, so the reverse collision doesn't bump both players again
				handled = true;
			}
		}

		if (removeSpecialMove) {
			_controllable = true;
			EndDamagingMove();
		}

		return handled;
	}

	void Player::OnHitFloor(float timeMult)
	{
		// The heavier rise gravity lasts only for one ascent
		_jumpReleased = false;
		_riseOwnedByJump = false;
		// A horizontal spring launch runs along the ground, so it's spent only once slowed to walking pace
		if (std::abs(_speed.X) < LegacyWalkSpeed) {
			_springLaunchSpeedX = 0.0f;
		}
		_springRebuildAccel = 0.0f;
		// `_springHoldLeft` is not cleared here, a horizontal spring launch runs along the ground

		if (_activeModifier == Modifier::None && (_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
			// The counter outlives only a landing the copter snapped onto, see LegacyCopterLandGrace
			if (_levelHandler->IsReforged() || _copterCarried || _copterLandGraceLeft <= 0.0f) {
				SetCopterFlight(0.0f);
			}
			SetAnimation(_currentAnimation->State & ~AnimState::Copter);
			if (!_isAttachedToPole) {
				SetState(ActorState::ApplyGravitation, true);
			}
		}

		if (_levelHandler->EventMap()->IsHurting(_pos.X, _pos.Y + 24.0f, Direction::Up)) {
			if (!IsInvulnerable() && _sugarRushLeft <= 0.0f) {
				if (_activeShieldTime > 0.0f) {
					DecreaseShieldTime(5.0f * FrameTimer::FramesPerSecond);
					float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
					GrantInvulnerability(invulnerableTime, InvulnerableType::Blinking);
					PlayPlayerSfx("HurtSoft"_s);
				} else {
					TakeDamage(1, _speed.X * 0.25f);
				}
			}
		} else if (!_inWater && _activeModifier == Modifier::None && !IsBird()) {
			if (_hitFloorTime <= 0.0f && !CanJump()) {
				_hitFloorTime = 30.0f;
				PlaySfx("Land"_s, 0.8f);
				if (PreferencesCache::GamepadRumble >= 2) {
					// "Land" effect is enabled only for Strong preset
					_levelHandler->PlayerExecuteRumble(this, "Land"_s);
				}

				if (Random().NextFloat() < 0.6f) {
					Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y + 20, _renderer.GetLayer() - 2), Explosion::Type::TinyDark);
				}
			}
		} else {
			// Prevent stucking with water/airboard, a bird doesn't land either
			SetState(ActorState::CanJump, false);
			if (_speed.Y > 0.0f) {
				_speed.Y = 0.0f;
			}
		}

		ResetAirJumps();
		_isFreefall = false;
		// The copter's deaf window belongs to the airtime that armed it
		_copterDeafLeft = 0.0f;

		SetState(ActorState::IsSolidObject, true);
	}

	void Player::OnHitCeiling(float timeMult)
	{
		// Stop the upward push, otherwise a strong launch sticks the player to the ceiling
		if (_externalForce.Y < 0.0f) {
			_externalForce.Y = 0.0f;
		}

		if (_tubeHeldSpeedY < 0.0f) {
			// Measured in JJ2+: held under a floor, a script launch keeps its speed until it gets out
			_speed.Y = _tubeHeldSpeedY;
			// Neither pushed out sideways in the meantime
			_unstuckCooldown = std::max(_unstuckCooldown, 2.0f);
		}

		// In the original an uppercut that reaches a ceiling ends there
		if (!_levelHandler->IsReforged() && _currentSpecialMove == SpecialMoveType::Uppercut) {
			EndDamagingMove();
		}

		if (_levelHandler->EventMap()->IsHurting(_pos.X, _pos.Y - 4.0f, Direction::Down)) {
			if (!IsInvulnerable() && _sugarRushLeft <= 0.0f) {
				if (_activeShieldTime > 0.0f) {
					DecreaseShieldTime(5.0f * FrameTimer::FramesPerSecond);
					float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
					GrantInvulnerability(invulnerableTime, InvulnerableType::Blinking);
					PlayPlayerSfx("HurtSoft"_s);
				} else {
					TakeDamage(1, _speed.X * 0.25f);
				}
			}
		}
	}

	void Player::OnHitWall(float timeMult)
	{
		// Reset speed and show Push animation, but never inside a sucker tube or the player gets stuck in it
		if (_inTubeTime <= 0.0f) {
			_speed.X = 0.0f;
			_pushFramesLeft = 12.0f;
			_pushContactThisFrame = true;
			_keepRunningTime = 0.0f;
			// A wall ends the extended dash grace of a rev-up launch, or the player keeps dashing against it
			if (_dashGraceLeft > LegacyDashGraceTicks) {
				_dashGraceLeft = 0.0f;
			}
			// And a wind-up or a pending light launch too
			CancelRevUp();
		}

		if (_levelHandler->EventMap()->IsHurting(_pos.X + (_speed.X > 0.0f ? 16.0f : -16.0f), _pos.Y, (_speed.X > 0.0f ? Direction::Left : Direction::Right))) {
			if (!IsInvulnerable() && _sugarRushLeft <= 0.0f) {
				if (_activeShieldTime > 0.0f) {
					DecreaseShieldTime(5.0f * FrameTimer::FramesPerSecond);
					float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
					GrantInvulnerability(invulnerableTime, InvulnerableType::Blinking);
					PlayPlayerSfx("HurtSoft"_s);
				} else {
					TakeDamage(1, _speed.X * 0.25f);
				}
			}
		} else {
			if (IsLedgeClimbAllowed() && _isActivelyPushing && _suspendType == SuspendType::None && _activeModifier == Modifier::None && !CanJump() &&
				!_inWater && _currentSpecialMove == SpecialMoveType::None && (_currentTransition == nullptr || _currentTransition->State != AnimState::TransitionUppercutEnd) &&
				_speed.Y >= -1.0f && _externalForce.Y >= 0.0f && _copterFramesLeft <= 0.0f && _keepRunningTime <= 0.0f && _fireFramesLeft <= 0.0f && _dizzyTime <= 0.0f) {

				// Check if the character supports ledge climbing
				if (_metadata->FindAnimation(AnimState::TransitionLedgeClimb)) {
					constexpr std::int32_t MaxTolerancePixels = 6;

					SetState(ActorState::CollideWithTilesetReduced, false);

					float x = (IsFacingLeft() ? -8.0f : 8.0f);
					AABBf hitbox1 = AABBInner + Vector2f(x, -42.0f - MaxTolerancePixels);				// Empty space to climb to
					AABBf hitbox2 = AABBInner + Vector2f(x, -42.0f + 2.0f);								// Wall below the empty space
					AABBf hitbox3 = AABBInner + Vector2f(x, -42.0f + 2.0f + 24.0f);						// Wall between the player and the wall above (vertically)
					AABBf hitbox4 = AABBInner + Vector2f(x, 20.0f);										// Wall below the player
					AABBf hitbox5 = AABBf(AABBInner.L + 2, hitbox1.T, AABBInner.R - 2, AABBInner.B);	// Player can't climb through walls
					TileCollisionParams params = { TileDestructType::None, false };
					if (_levelHandler->IsPositionEmpty(this, hitbox1, params) &&
						!_levelHandler->IsPositionEmpty(this, hitbox2, params) &&
						!_levelHandler->IsPositionEmpty(this, hitbox3, params) &&
						!_levelHandler->IsPositionEmpty(this, hitbox4, params) &&
						 _levelHandler->IsPositionEmpty(this, hitbox5, params)) {

						uint8_t* wallParams;
						if (_levelHandler->EventMap()->GetEventByPosition(IsFacingLeft() ? hitbox2.L : hitbox2.R, hitbox2.B, &wallParams) != EventType::ModifierNoClimb &&
							// Don't allow to climp through tube modifier
							_levelHandler->EventMap()->GetEventByPosition((hitbox1.L + hitbox1.R) * 0.5f, hitbox1.T, &wallParams) != EventType::ModifierTube) {
							// Move the player upwards, if it is in tolerance, so the animation will look better
							AABBf aabb = AABBInner + Vector2f(x, -42.0f);
							for (std::int32_t y = 0; y >= -MaxTolerancePixels; y -= 1) {
								if (_levelHandler->IsPositionEmpty(this, aabb, params)) {
									MoveInstantly(Vector2f(0.0f, (float)y), MoveType::Relative | MoveType::Force, params);
									break;
								}
								aabb.T -= 1.0f;
								aabb.B -= 1.0f;
							}

							// Prepare the player for animation
							_controllable = false;
							SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects, false);

							_speed.X = 0.0f;
							// Gravity is off for the whole climb, so this one speed is the entire rise. Plain level
							// gravity, not GetGravityModifier() (an invented move), clamped so it always lifts.
							_speed.Y = std::min(-1.4f + _levelHandler->GetGravity(), -0.5f);

#if defined(WITH_PHYSICS_PROBE)
							if (PreferencesCache::PhysicsProbe) {
								LOGI("[climb] start y={:.1f} speed={:.4f} jumpReleased={}", _pos.Y, _speed.Y, _jumpReleased ? 1 : 0);
							}
#endif

							_externalForce.X = 0.0f;
							_externalForce.Y = 0.0f;
							_internalForceY = 0.0f;
							_pushFramesLeft = 0.0f;
							_fireFramesLeft = 0.0f;
							_copterFramesLeft = 0.0f;

							// Stick the player to wall
							MoveInstantly(Vector2f(IsFacingLeft() ? -6.0f : 6.0f, 0.0f), MoveType::Relative | MoveType::Force, params);

							SetAnimation(AnimState::Idle);
							SetTransition(AnimState::TransitionLedgeClimb, false, [this]() {
								// Reset the player to normal state
								_controllable = true;
								SetState(ActorState::CanJump | ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects, true);
								_pushFramesLeft = 0.0f;
								_fireFramesLeft = 0.0f;
								_copterFramesLeft = 0.0f;
								_hitFloorTime = 60.0f;

								_speed.Y = 0.0f;

								// Move it far from the ledge
								TileCollisionParams params = { TileDestructType::None, false };
								MoveInstantly(Vector2f(IsFacingLeft() ? -4.0f : 4.0f, 0.0f), MoveType::Relative, params);

								// Move the player upwards, so it will not be stuck in the wall
								for (float y = -1.0f; y > -24.0f; y -= 1.0f) {
									if (MoveInstantly(Vector2f(0.0f, y), MoveType::Relative, params)) {
										break;
									}
								}

#if defined(WITH_PHYSICS_PROBE)
								if (PreferencesCache::PhysicsProbe) {
									LOGI("[climb] end y={:.1f} onFloor={}", _pos.Y, GetState(ActorState::CanJump) ? 1 : 0);
								}
#endif
							});
						}
					}

					SetState(ActorState::CollideWithTilesetReduced, true);
				}
			}
		}
	}

	void Player::OnPushSolidObject(float timeMult, float pushSpeedX)
	{
		if (std::abs(pushSpeedX) > 0.0f) {
			if (_levelHandler->IsReforged()) {
				_speed.X = pushSpeedX * 1.2f * timeMult;
			} else {
				// Measured in the original: a flat push speed, Run makes no difference (not scaled by timeMult)
				_speed.X = std::copysign(LegacyPushSpeed, pushSpeedX);
			}
			_pushFramesLeft = 12.0f;
			_pushContactThisFrame = true;
			_fireFramesLeft = 0.0f;
			_canPushFurther = true;
		} else {
			_canPushFurther = false;
		}
	}

	void Player::OnHitSpring(Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY, bool& removeSpecialMove)
	{
		if (IsBird()) {
			// A spring only nudges a bird, the original clamps it to the flight cap on the next tick
			float maxSpeed = GetBirdMaxSpeed();
			if (std::abs(force.X) > 0.0f) {
				_speed.X = std::copysign(maxSpeed, force.X);
			} else if (std::abs(force.Y) > 0.0f) {
				_speed.Y = std::copysign(maxSpeed, force.Y);
			} else {
				return;
			}
			_springCooldown = 10.0f;
			_levelHandler->PlayerExecuteRumble(this, "Spring"_s);
			return;
		}

		std::int32_t sign = ((force.X + force.Y) > std::numeric_limits<float>::epsilon() ? 1 : -1);
		if (std::abs(force.X) > 0.0f) {
			// Reforged pulls the player halfway towards the spring, the original doesn't move them at all
			if (_levelHandler->IsReforged()) {
				MoveInstantly(Vector2f(_pos.X, (_pos.Y + pos.Y) * 0.5f), MoveType::Absolute);
			}

			_copterFramesLeft = 0.0f;
			if (!_levelHandler->IsReforged()) {
				// The original simply assigns the spring's speed
				_speed.X = force.X;
				_externalForce.X = 0.0f;
				// A double jump out of this launch rebuilds the speed at a rate this launch sets
				_springLaunchSpeedX = std::abs(force.X);
				// The launch is carried for a few ticks, then clamps like every other carry
				_springHoldLeft = (std::abs(force.X) > LegacyCarryExitRunSpeed
					? LegacySpringHoldTicks / LegacyFrameRateScale
					: 0.0f);
			} else {
				_speed.X = (1.0f + std::abs(force.X)) * sign;
				_externalForce.X = force.X * 0.6f;
			}
			_springCooldown = 10.0f;
			SetState(ActorState::CanJump, false);
			// A spring restores the double jump like landing does, except in Reforged
			if (!_levelHandler->IsReforged()) {
				ResetAirJumps();
			}

			_wasActivelyPushing = false;
			_keepRunningTime = (_levelHandler->IsReforged() ? 100.0f : 80.0f);
			// This carry refuses the crouch for its whole length, a pole's doesn't
			_springCarry = true;
			_hPoleCarry = false;
			_revUpCarry = false;

			if (!keepSpeedY) {
				_speed.Y = 0.0f;
				_externalForce.Y = 0.0f;
			}

			if (_currentSpecialMove != SpecialMoveType::Sidekick) {
				if (_inIdleTransition) {
					_inIdleTransition = false;
					CancelTransition();
				}

				removeSpecialMove = true;
				_controllableTimeout = 2.0f;
				if (_activeModifier == Modifier::None) {
					SetAnimation(_currentAnimation->State & ~(AnimState::Crouch | AnimState::Lookup | AnimState::Buttstomp));
					SetPlayerTransition(AnimState::Dash | AnimState::Jump, true, false, SpecialMoveType::None);
				}
			}

			_levelHandler->PlayerExecuteRumble(this, "Spring"_s);
		} else if (std::abs(force.Y) > 0.0f) {
			MoveInstantly(Vector2f(lerp(_pos.X, pos.X, 0.3f), _pos.Y), MoveType::Absolute);

			if (_activeModifier == Modifier::None && _copterFramesLeft > 0.0f) {
				_copterFramesLeft = 0.0f;
				SetAnimation(_currentAnimation->State & ~AnimState::Copter);
				SetState(ActorState::ApplyGravitation, true);
			}

			if (!_levelHandler->IsReforged()) {
				// The spring's speed verbatim, the rise cap makes a stronger spring last longer, not rise faster
				_speed.Y = force.Y;
				_externalForce.Y = 0.0f;
				// Measured in the original: a spring ascent decays at the held rate whatever the jump key does
				_jumpReleased = false;
				_riseFromFloatUp = false;
				_riseOwnedByJump = false;
			} else {
				_speed.Y = (4.0f + std::abs(force.Y)) * sign;
				if (!GetState(ActorState::ApplyGravitation)) {
					_externalForce.Y = force.Y * 0.14f;
				} else {
					_externalForce.Y = force.Y;
				}
			}
			_springCooldown = 10.0f;
			SetState(ActorState::CanJump, false);

			// A vertical spring drops the horizontal speed, except during a rev-up launch (extended dash grace)
			bool revUpLaunch = (_dashGraceLeft > LegacyDashGraceTicks);
			if (!keepSpeedX && !revUpLaunch) {
				_speed.X = 0.0f;
				_externalForce.X = 0.0f;
				_keepRunningTime = 0.0f;
			} else if (!_levelHandler->IsReforged() && ((_hPoleCarry && _keepRunningTime > 0.0f) || _ballCarryLeft > 0.0f)) {
				// The pole's carry ends a few ticks after it, as measured in JJ2+
				_springHoldLeft = LegacyVerticalSpringCarryTicks / LegacyFrameRateScale;
			}

			if (_inIdleTransition) {
				_inIdleTransition = false;
				CancelTransition();
			}

			if (sign > 0) {
				removeSpecialMove = false;
				if (_activeModifier == Modifier::None) {
					_currentSpecialMove = SpecialMoveType::Buttstomp;
					if (_tubeBallPoseLeft <= 0.0f) {
						SetAnimation(AnimState::Buttstomp);
					}
				}
			} else {
				removeSpecialMove = true;
				_isSpring = true;
				// A spring restores the double jump like landing does, except in Reforged
				if (!_levelHandler->IsReforged()) {
					ResetAirJumps();
				}
				if (_activeModifier == Modifier::None) {
					SetAnimation(_currentAnimation->State & ~(AnimState::Crouch | AnimState::Lookup));
				}
			}

			_levelHandler->PlayerExecuteRumble(this, "Spring"_s);
			PlayPlayerSfx("Spring"_s);
		}
	}

	void Player::OnWaterSplash(Vector2f pos, bool inwards)
	{
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)pos.X, (std::int32_t)pos.Y, _renderer.GetLayer() + 2), Explosion::Type::WaterSplash);
		_levelHandler->PlayCommonSfx("WaterSplash"_s, Vector3f(pos.X, pos.Y, 0.0f), inwards ? 0.7f : 1.0f, 0.5f);
	}

	void Player::UpdateAnimation(float timeMult)
	{
		// Counted down here, UpdateRevUp() is skipped in water and under a modifier
		if (_revUp.EndLeft > 0.0f) {
			_revUp.EndLeft -= timeMult;
		}

		// Measured in the original: the double-jump pose lasts only as long as the rise
		if (!_levelHandler->IsReforged() && _speed.Y >= 0.0f && _currentTransition != nullptr &&
			_currentTransition->State == AnimState::Spring) {
			// The transition is deliberately non-cancellable, so nothing else can end it
			ForceCancelTransition();
		}

		if (_tubeBallPoseLeft > 0.0f) {
			_tubeBallPoseLeft -= timeMult;
			if (!_inWater && _activeModifier == Modifier::None && !IsBird()) {
				// A transition still plays over it, replacing it would cancel e.g. the buttstomp's start
				ApplyStopAnimation(false);
				if (_currentTransition == nullptr) {
					SetAnimation(AnimState::Dash | AnimState::Jump);
				}
				return;
			}
		}

		if (!_controllable) {
			// Cancel a showing stop pose too, or it plays for as long as control is gone (e.g. in a tube)
			ApplyStopAnimation(false);
			return;
		}

		if (IsBird()) {
			// A perched bird plays its own poses, see UpdateBirdPerchPose()
			if (_birdPerched) {
				UpdateBirdPerchPose(timeMult);
				return;
			}
			// In flight it has one pose (no shooting pose), plus the beak attack poses
			AnimState flightPose = AnimState::Idle;
			// A charge that a wall has just stopped is already over, HandleBirdMovement() only gets to say so after this
			if (_birdCharging && std::abs(_speed.X) > GetBirdMaxSpeed() * BirdChargeMinSpeedRatio) {
				flightPose = (_birdChargeDirY < 0 ? BirdChargeUp : (_birdChargeDirY > 0 ? BirdChargeDown : BirdCharge));
			}
			AnimState currentState = _currentAnimation->State;
			if (currentState != flightPose) {
				// The original shares one frame counter for the flight and charge poses, so a switch keeps the frame
				bool carryFrame = (_currentTransition == nullptr && _renderer.AnimDuration > 0.0f &&
					(currentState == AnimState::Idle || currentState == BirdCharge || currentState == BirdChargeUp || currentState == BirdChargeDown));
				float phase = (carryFrame ? _renderer.AnimTime / _renderer.AnimDuration : 0.0f);
				// Metadata without the two diagonal poses falls back to the level one
				if (!SetAnimation(flightPose) && flightPose != BirdCharge && flightPose != AnimState::Idle) {
					SetAnimation(BirdCharge);
				}
				if (carryFrame && _currentTransition == nullptr && _renderer.AnimDuration > 0.0f) {
					_renderer.AnimTime = (phase < 1.0f ? phase : 0.0f) * _renderer.AnimDuration;
				}
			}
			// The original beats its wings at a flat rate, Reforged slows the beat while it hovers
			if (_levelHandler->IsReforged() && _currentTransition == nullptr) {
				float speedRatio = std::min(_speed.Length() / GetBirdMaxSpeed(), 1.0f);
				_renderer.AnimDuration = _currentAnimation->AnimDuration * (1.8f - 0.8f * speedRatio);
			}
			return;
		}

		bool sliding = IsSlidingToHalt();
		// The last stage of the stop pose outlives the player reaching zero speed
		bool stopPoseShowing = (sliding && (_stopPhase != StopPhase::None || std::abs(_speed.X) > 0.0f));

		AnimState oldState = _currentAnimation->State;
		AnimState newState;
		if (_inWater) {
			newState = AnimState::Swim;
		} else if (_activeModifier == Modifier::Airboard) {
			newState = AnimState::Airboard;
		} else if (_activeModifier == Modifier::Copter) {
			newState = AnimState::Copter;
			// The shoot bit has to be carried here too, these branches don't build a composite
			if (_fireFramesLeft > 0.0f) {
				newState |= AnimState::Shoot;
			}
		} else if (_activeModifier == Modifier::LizardCopter) {
			newState = AnimState::Hook;
			if (_fireFramesLeft > 0.0f) {
				newState |= AnimState::Shoot;
			}
		} else if (_suspendType == SuspendType::SwingingVine) {
			newState = AnimState::Swing;
		} else if (IsRevvingUp() || _revUp.EndLeft > 0.0f) {
			// Decided here, not in UpdateRevUp(), and held through the end animation so Dash doesn't cancel it
			newState = AnimState::RevUp;
		} else if (_onPinballPaddleTime > 0.0f) {
			// The original shows the curled-up ball on a pinball paddle
			newState = AnimState::Dash | AnimState::Jump;
		} else if (_isLifting || (_beingStoodOn && CanJump() && std::abs(_speed.X) < 1.0f && std::abs(_speed.Y) < 1.0f)) {
			// `_beingStoodOn` is the cosmetic online case, shown only while grounded and still
			newState = AnimState::Lift;
		} else if (CanJump() && _isActivelyPushing && _pushFramesLeft > 0.0f && _keepRunningTime <= 0.0f && _fireFramesLeft <= 0.0f &&
			(_pushContactThisFrame || std::abs(_speed.X) <= MaxPushingSpeed)) {
			// The grace timer stops flicker, but only while still in contact or held to the push speed
			newState = AnimState::Push;

			if (_inIdleTransition) {
				_inIdleTransition = false;
				CancelTransition();
			}
		} else {
			// Only certain ones don't need to be preserved from earlier state, others should be set as expected
			AnimState composite = (_currentAnimation->State & CompositeAnimMask);
			if (_suspendType == SuspendType::None && !CanJump() && (composite & AnimState::Lookup) == AnimState::Lookup) {
				// Looking up ends in the air (a script's launch), shots go sideways there, as in the original
				composite &= ~AnimState::Lookup;
				_wasUpPressed = false;
			}
			// A buttstomp that ran under the tube's ball (see TubeBallPoseTime) gets its own pose back
			if (_currentSpecialMove == SpecialMoveType::Buttstomp && !IsButtstompStarting()) {
				composite |= AnimState::Buttstomp;
			}

			if (stopPoseShowing) {
				// Sliding to a halt, the base state stays idle so the stop pose transition isn't cancelled
			} else if (_isActivelyPushing == _wasActivelyPushing || !_levelHandler->IsReforged()) {
				float absSpeedX = std::abs(_speed.X);
				// The threshold must track the actual walk cap
				float dashAnimThreshold = (_levelHandler->IsReforged() ? MaxRunningSpeed : LegacyDashAnimSpeed);
				// Measured in the original: a run-in-place carry picks the pose by the Run key, not by the speed
				if (!_levelHandler->IsReforged() && _revUpCarry && _keepRunningTime > 0.0f) {
					composite |= (_isRunPressed ? AnimState::Dash : AnimState::Run);
				} else if (absSpeedX > dashAnimThreshold) {
					composite |= AnimState::Dash;
				} else if (!_levelHandler->IsReforged() && absSpeedX > LegacyWalkSpeed) {
					// Speeding up is picked by speed too, in the same three bands the stop uses
					composite |= AnimState::Run;
				} else if (_keepRunningTime > 0.0f) {
					composite |= AnimState::Run;
				} else if (absSpeedX > (_fireFramesLeft > 0.0f ? 1.0f : 0.0f)) {	// Shooting needs higher threshold to fix pushing into a wall
					composite |= AnimState::Walk;
				}
			}

			if (_fireFramesLeft > 0.0f) {
				composite |= AnimState::Shoot;
			}

			if (_suspendType != SuspendType::None) {
				composite |= AnimState::Hook;
			} else {
				if (CanJump()) {
					// Grounded, no vertical speed
					if (_dizzyTime > 0.0f) {
						composite |= AnimState::Dizzy;
					}
				} else if (_speed.Y < 0.0f) {
					// Jumping, ver. speed is negative
					if (_isSpring) {
						composite |= AnimState::Spring;
					} else {
						composite |= AnimState::Jump;
					}

				} else if (_isFreefall) {
					// Free falling, ver. speed is positive
					composite |= AnimState::Freefall;
					_isSpring = false;
				} else {
					// Falling, ver. speed is positive
					composite |= AnimState::Fall;
					_isSpring = false;
				}
			}

			newState = composite;
		}

		if (newState == AnimState::Idle) {
			// Measured in the original: the idle gap counts from the previous flourish finishing
			bool holdIdleTimer = (!_levelHandler->IsReforged() && _currentTransition != nullptr);
			if (holdIdleTimer) {
				// Not idle while a transition is playing
			} else if (_idleTime > (_levelHandler->IsReforged() ? 600.0f : LegacyIdleBoredTime)) {
				_idleTime = 0.0f;

				if (_currentTransition == nullptr) {
					constexpr StringView IdleBored[] = {
						"IdleBored1"_s, "IdleBored2"_s, "IdleBored3"_s, "IdleBored4"_s, "IdleBored5"_s
					};
					std::int32_t maxIdx = GetCharacterTraits(_playerType).IdleBoredAnimCount;
					if (maxIdx > 0) {
						std::int32_t selectedIdx = Random().Fast(0, maxIdx);
						if (SetTransition((AnimState)(536870944 + selectedIdx), true)) {
							PlayPlayerSfx(IdleBored[selectedIdx]);
						}
					}
				}
			} else {
				_idleTime += timeMult;
			}
		} else {
			_idleTime = 0.0f;
		}

		SetAnimation(newState);

		if (!_isAttachedToPole) {
			switch (oldState) {
				case AnimState::Walk:
					// Reforged only, outside it this is a speed band, not a transition
					if (newState == AnimState::Dash && _levelHandler->IsReforged()) {
						SetTransition(AnimState::TransitionRunToDash, true);
					}
					break;
				// The stop pose is picked by speed after this switch
				case AnimState::Dash:
					break;
				case AnimState::Fall:
				case AnimState::Freefall:
					if (newState == AnimState::Idle) {
						// The metadata's landing pose runs much slower than the original's
						if (SetTransition(AnimState::TransitionFallToIdle, true) && !_levelHandler->IsReforged() &&
							_currentTransition != nullptr && _currentTransition->FrameCount > 0) {
							_renderer.AnimDuration = _currentTransition->FrameCount * LandAnimFrameTicks / LegacyTickRate;
						}
					}
					break;
				case AnimState::Idle:
					if (newState == AnimState::Jump) {
						SetTransition(AnimState::TransitionIdleToJump, true);
					} else if (newState != AnimState::Idle) {
						_inLedgeTransition = false;
						if (_currentTransition != nullptr && _currentTransition->State == AnimState::TransitionLedge) {
							CancelTransition();
						}
					} else if (!_inLedgeTransition && !stopPoseShowing && _carryingObject == nullptr && std::abs(_speed.X) < 1.0f && std::abs(_speed.Y) < 1.0f) {
						AABBf aabbL = AABBf(AABBInner.L + 2, AABBInner.B - 10, AABBInner.L + 4, AABBInner.B + 28);
						AABBf aabbR = AABBf(AABBInner.R - 4, AABBInner.B - 10, AABBInner.R - 2, AABBInner.B + 28);
						TileCollisionParams params = { TileDestructType::None, true };
						if (IsFacingLeft()
							? (_levelHandler->IsPositionEmpty(this, aabbL, params) && !_levelHandler->IsPositionEmpty(this, aabbR, params))
							: (!_levelHandler->IsPositionEmpty(this, aabbL, params) && _levelHandler->IsPositionEmpty(this, aabbR, params))) {

							_inLedgeTransition = true;
							if (_playerType == PlayerType::Spaz) {
								// Spaz's and Lori's animation should be continual, so reset it in callback
								SetTransition(AnimState::TransitionLedge, true, [this]() {
									_inLedgeTransition = false;
								});
							} else {
								SetTransition(AnimState::TransitionLedge, true);
							}

							PlayPlayerSfx("Ledge"_s);
						}
					}
					break;
			}
		}

		ApplyStopAnimation(sliding);
		UpdateHookIdleAnimation(timeMult, newState);
		UpdateLegacyRunAnimSpeed();
	}

	void Player::UpdateLegacyRunAnimSpeed()
	{
		// A playing transition owns its duration, so this runs after ApplyStopAnimation()
		if (_levelHandler->IsReforged() || _currentTransition != nullptr || _renderer.FrameCount <= 0) {
			return;
		}

		AnimState state = _currentAnimation->State;
		float frameTicks;
		if ((state & AnimState::Spring) == AnimState::Spring) {
			// The spring launch pose follows the vertical speed, the shooting pose keeps its own rate
			if ((state & AnimState::Shoot) == AnimState::Shoot) {
				return;
			}
			float speedTicks = std::abs(_speed.Y) / LegacyFrameRateScale;
			frameTicks = std::clamp(LegacyRunAnimFrameTicks - speedTicks,
				LegacySpringAnimMinFrameTicks, LegacySpringAnimMaxFrameTicks);
		} else if ((state & HorizontalAnimMask) != AnimState::Idle && (state & VerticalAnimMask) == AnimState::Idle &&
			(state & AnimState::Freefall) != AnimState::Freefall) {
			// Grounded running only, airborne poses keep their own rate
			float speedTicks = std::abs(_speed.X) / LegacyFrameRateScale;
			frameTicks = std::max(LegacyRunAnimMinFrameTicks, LegacyRunAnimFrameTicks - speedTicks);
		} else {
			return;
		}

		float duration = _renderer.FrameCount * frameTicks / LegacyTickRate;
		if (duration == _renderer.AnimDuration) {
			return;
		}

		// Rescaled to keep the animation phase
		if (_renderer.AnimDuration > 0.0f) {
			_renderer.AnimTime *= duration / _renderer.AnimDuration;
		}
		_renderer.AnimDuration = duration;
	}

	void Player::UpdateHookIdleAnimation(float timeMult, AnimState newState)
	{
		// Hanging still on a vine alternates idle and flavor poses, the lizard copter (also Hook) loops the flavor.
		// Moving or shooting ends it, because `newState` is then not exactly Hook.
		bool hangingOnVine = (_suspendType != SuspendType::None);
		bool hangingOnCopter = (_activeModifier == Modifier::LizardCopter);
		if (newState != AnimState::Hook || (!hangingOnVine && !hangingOnCopter)) {
			_hookIdleTime = 0.0f;
			if (_inHookIdleFlavor) {
				_inHookIdleFlavor = false;
				CancelTransition();
			}
			return;
		}

		// A copter loops it instead of alternating, as in the original
		if (hangingOnCopter) {
			// But not over the tail of a shot
			if (_currentTransition != nullptr && _currentTransition->State == AnimState::TransitionHookShootToHook) {
				_inHookIdleFlavor = false;
				return;
			}
			// Keyed on what is playing, a shot replaces the transition without clearing the flag
			if (_currentTransition == nullptr || _currentTransition->State != AnimState::TransitionHookIdleFlavor) {
				_inHookIdleFlavor = true;
				IssueHookIdleFlavor();
			}
			return;
		}

		_hookIdleTime += timeMult;
		if (_hookIdleTime >= LegacyHookIdleCycle * 2.0f) {
			_hookIdleTime -= LegacyHookIdleCycle * 2.0f;
		}

		bool wantFlavor = (_hookIdleTime >= LegacyHookIdleCycle);
		if (wantFlavor == _inHookIdleFlavor) {
			return;
		}

		if (wantFlavor) {
			_inHookIdleFlavor = true;
			IssueHookIdleFlavor();
		} else {
			_inHookIdleFlavor = false;
			CancelTransition();
		}
	}

	bool Player::ShouldCancelTransitionOnFire() const
	{
		if (_currentTransition == nullptr) {
			return false;
		}
		// Non-cancellable endings (of a shot or a special move) that a new shot must still replace
		AnimState state = _currentTransition->State;
		return (state == AnimState::Spring ||
			state == AnimState::TransitionShootToIdle ||
			state == AnimState::TransitionHookShootToHook ||
			state == AnimState::TransitionCopterShootToCopter ||
			state == AnimState::TransitionFallShootToFall ||
			state == AnimState::TransitionUppercutEnd);
	}

	void Player::IssueHookIdleFlavor()
	{
		// Restarted from the finish callback, cancelling clears the flag first. A missing animation would
		// run the callback at once and recurse endlessly, so it stays off.
		if (_metadata == nullptr || _metadata->FindAnimation(AnimState::TransitionHookIdleFlavor) == nullptr) {
			_inHookIdleFlavor = false;
			return;
		}
		SetTransition(AnimState::TransitionHookIdleFlavor, true, [this]() {
			if (_inHookIdleFlavor) {
				IssueHookIdleFlavor();
			}
		});
	}

	bool Player::IsSlidingToHalt()
	{
		// Coasting to a stop rather than steering, regardless of speed. Carries and rev-ups are excluded.
		// A crouch is a deliberate input, not coasting
		if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
			return false;
		}
		// A shot replaces the skid, in both modes
		if (_fireFramesLeft > 0.0f) {
			return false;
		}
		// The special move's ending pose, the move itself is already cleared by then
		if (_currentTransition != nullptr && _currentTransition->State == AnimState::TransitionUppercutEnd) {
			return false;
		}
		return (CanJump() && _controllable && _currentSpecialMove == SpecialMoveType::None &&
			_suspendType == SuspendType::None && _activeModifier == Modifier::None && !_inWater &&
			_revUp.LaunchLeft <= 0.0f && !IsRevvingUp() && _revUp.EndLeft <= 0.0f && _keepRunningTime <= 0.0f &&
			std::abs(_levelHandler->PlayerHorizontalMovement(this)) <= 0.4f);
	}

	void Player::EnterStopPhase(StopPhase phase)
	{
		// Measured in the original: skids on `walk_stop`, slides on `dash_stop` and settles on `run_stop`
		AnimState pose;
		switch (phase) {
			case StopPhase::Slide: pose = AnimState::TransitionDashToIdle; break;
			case StopPhase::Settle: pose = AnimState::TransitionRunToIdle; break;
			default: pose = AnimState::TransitionWalkToIdle; break;
		}

		// The next pose is issued from the finish callback, the flag ignores the outgoing transition's callback
		_stopPhaseChanging = true;
		_inIdleTransition = true;
		SetTransition(pose, true, [this]() { OnStopPoseFinished(); });
		_stopPhaseChanging = false;
		_stopPhase = phase;

		if (phase == StopPhase::Hold) {
			ParkStopPose();
		}
	}

	void Player::ParkStopPose()
	{
		// Holds the skid on its last frame until the player stops, as the original does for a slow slide
		if (_renderer.FrameCount > 0 && _renderer.AnimDuration > 0.0f) {
			_renderer.AnimTime = _renderer.AnimDuration * (_renderer.FrameCount - 0.5f) / _renderer.FrameCount;
		}
	}

	void Player::OnStopPoseFinished()
	{
		// Also reached when the pose is replaced or cancelled
		if (_stopPhaseChanging || !_inIdleTransition || !IsSlidingToHalt()) {
			return;
		}

		switch (_stopPhase) {
			case StopPhase::Skid:
				// A slow slide just holds the skid until the player stops
				EnterStopPhase(std::abs(_speed.X) >= StopSettleSpeed ? StopPhase::Slide : StopPhase::Hold);
				break;
			case StopPhase::Slide:
			case StopPhase::Hold:
				// `dash_stop` loops while the speed keeps it, a transition can't loop by itself
				EnterStopPhase(_stopPhase);
				break;
			case StopPhase::Settle:
				// Standing follows it, and only it
				_stopPhase = StopPhase::None;
				_inIdleTransition = false;
				break;
			default:
				break;
		}
	}

	void Player::ApplyStopAnimation(bool sliding)
	{
		if (!sliding) {
			if (_stopPhase != StopPhase::None) {
				// Moving again, or no longer in a state that can slide at all
				_stopPhase = StopPhase::None;
				_inIdleTransition = false;
				CancelTransition();
			}
			return;
		}

		float absSpeedX = std::abs(_speed.X);
		switch (_stopPhase) {
			case StopPhase::None:
				if (absSpeedX > 0.0f) {
					EnterStopPhase(StopPhase::Skid);
				}
				break;
			case StopPhase::Slide:
				if (absSpeedX < StopSettleSpeed) {
					EnterStopPhase(StopPhase::Settle);
				}
				break;
			case StopPhase::Hold:
				if (absSpeedX <= 0.0f) {
					_stopPhase = StopPhase::None;
					_inIdleTransition = false;
					CancelTransition();
				} else {
					ParkStopPose();
				}
				break;
			default:
				break;
		}
	}

	void Player::PushSolidObjects(float timeMult)
	{
		// Re-established below and in OnHitWall()
		_pushContactThisFrame = false;
		// A kick met a pushable, set only by the push path below (not by a plain wall)
		_kickPushedThisFrame = false;

		if (_pushFramesLeft > 0.0f) {
			_pushFramesLeft -= timeMult;
		} else {
			_canPushFurther = false;
		}

		// Measured in the original: a sidekick pushes at the ordinary push speed, only while it has distance left
		bool kickActive = (!_levelHandler->IsReforged() && _currentSpecialMove == SpecialMoveType::Sidekick);
		bool kickPushing = (kickActive && _sidekickDistanceLeft > 0.0f);
		// And when the drive ends, the object stops with it
		bool kickStopping = (kickActive && !kickPushing && _kickWasPushing);
		_kickWasPushing = kickPushing;
		if (kickPushing || kickStopping || (CanJump() && _controllable && _controllableExternal && _isActivelyPushing /*&& std::abs(_speed.X) > 0.0f*/)) {
			float offset = (IsFacingLeft() ? -4.0f : 4.0f);
			AABBf hitbox = { AABBInner.L + offset, AABBInner.T + 8.0f, AABBInner.R + offset, AABBInner.B - 14.0f };
			TileCollisionParams params = { TileDestructType::None, false };
			ActorBase* collider;
			if (!_levelHandler->IsPositionEmpty(this, hitbox, params, &collider)) {
				if (auto* solidObject = runtime_cast<SolidObjectBase>(collider)) {
					SetState(ActorState::IsSolidObject, false);
					if (kickStopping) {
						solidObject->StopPushing();
					} else {
						// A kick pushes by facing, its speed may already be zero
						float pushSpeedX = solidObject->Push(kickPushing ? IsFacingLeft() : (_speed.X < 0), timeMult);
						OnPushSolidObject(timeMult, pushSpeedX);
						_kickPushedThisFrame = kickPushing;
					}
					SetState(ActorState::IsSolidObject, true);
				}
			}
		} else {
			// A solid object or a player on our head, only local co-op restricts movement (online it's cosmetic)
			bool liftedBySolid = false;
			if (GetState(ActorState::IsSolidObject)) {
				AABBf aabb = AABBf(AABBInner.L, AABBInner.T - 20.0f, AABBInner.R, AABBInner.T + 6.0f);
				TileCollisionParams params = { TileDestructType::None, false };
				ActorBase* collider;
				ActorState prevState = GetState();
				SetState(ActorState::CollideWithTileset, false);
				if (!_levelHandler->IsPositionEmpty(this, aabb, params, &collider)) {
					if (auto* solidObject = runtime_cast<SolidObjectBase>(collider)) {
						liftedBySolid = (AABBInner.T >= solidObject->AABBInner.T);
					}
				}
				SetState(prevState);
			}

			// Only locally or on the server, an online client gets `_beingStoodOn` from the server
			bool authoritative = (_levelHandler->IsServer() || _levelHandler->CanPlayersCollide());
			bool stoodOnByPlayer = (authoritative && IsBeingStoodOnByPlayer());
			// Only local splitscreen co-op turns being stood on into a real (movement-restricting) lift
			bool stoodOnRestricts = (stoodOnByPlayer && _levelHandler->CanPlayersCollide());
			if (liftedBySolid || stoodOnRestricts) {
				if (!_isLifting && std::abs(_speed.Y) < 1.0f) {
					_isLifting = true;
					SetTransition(AnimState::TransitionLiftStart, true);
				}
			} else {
				_isLifting = false;
			}
			// Cosmetic-only "being stood on" (online): drives the lift pose without restricting movement
			if (authoritative) {
				_beingStoodOn = (stoodOnByPlayer && !stoodOnRestricts);
			}
		}
	}

	void Player::CheckEndOfSpecialMoves(float timeMult)
	{
		// Buttstomp
		if (_currentSpecialMove == SpecialMoveType::Buttstomp && (CanJump() || _suspendType != SuspendType::None)) {
			EndDamagingMove();
			if (_suspendType == SuspendType::None && !_isSpring) {
				std::int32_t tx = (std::int32_t)_pos.X / 32;
				std::int32_t ty = ((std::int32_t)_pos.Y + 24) / 32;

				std::uint8_t* eventParams;
				if (_levelHandler->EventMap()->GetEventByPosition(tx, ty, &eventParams) == EventType::GemStomp) {
					_levelHandler->EventMap()->StoreTileEvent(tx, ty, EventType::Empty);

					for (std::int32_t i = 0; i < 8; i++) {
						float fx = Random().NextFloat(-12.0f, 12.0f);
						float fy = Random().NextFloat(-2.0f, 0.2f);

						std::uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] = { 0, 0x01 | 0x04 };
						std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(EventType::Gem, spawnParams, ActorState::None, Vector3i((std::int32_t)(_pos.X + fx * 2.0f), (std::int32_t)(_pos.Y + fy * 4.0f), _renderer.GetLayer() - 10));
						if (actor != nullptr) {
							actor->AddExternalForce(fx, fy);
							_levelHandler->AddActor(actor);
						}
					}
				}

				if (_levelHandler->IsReforged()) {
					_controllable = false;
					SetTransition(AnimState::TransitionButtstompEnd, false, [this]() {
						_controllable = true;
					});
				} else {
					// The player stops dead, the original's occasional hop is just its de-penetration
					SetTransition(AnimState::TransitionButtstompEnd, true);
				}
			} else {
				_controllable = true;
			}
		}

		// Uppercut - Reforged ends it once the rise has decayed, the original after a fixed time
		if (_currentSpecialMove == SpecialMoveType::Uppercut && _currentTransition == nullptr && ((_currentAnimation->State & AnimState::Uppercut) == AnimState::Uppercut) &&
			(_levelHandler->IsReforged() ? _speed.Y > -2.0f : _uppercutTimeLeft <= 0.0f)) {
			EndDamagingMove();
		}

		// Sidekick - the dash covers a fixed distance, so the distance itself is counted
		bool sidekickSpent = false;
		// Only while the move is in progress, other endings leave a remainder behind
		if (_currentSpecialMove == SpecialMoveType::Sidekick && _sidekickDistanceLeft > 0.0f) {
			// A kick held up by a pushable spends what it meant to travel, or it would never end (measured)
			float travelled;
			if (_kickPushedThisFrame) {
				travelled = (_playerType == PlayerType::Lori
					? LegacyLoriKickPushDrain
					: LegacyAppliedSpeedCap) * timeMult;
			} else {
				travelled = std::abs(_pos.X - _frameStartPos.X);
			}
			_sidekickDistanceLeft -= travelled;
			// A remainder below the float resolution of the position would never be worked off (mid-air freeze)
			if (_sidekickDistanceLeft < SidekickDistanceEpsilon) {
				_sidekickDistanceLeft = 0.0f;
			}
			if (_sidekickDistanceLeft <= 0.0f) {
				_sidekickDistanceLeft = 0.0f;
				_externalForce.X = 0.0f;
				sidekickSpent = true;
				// Measured in the original: Spaz's kick ending with Jump held starts a jump (before the clamp, so the
				// boost uses the kick's speed). Grounded is asked of the tileset, the kick cleared CanJump. Lori's
				// kick repeats instead.
				if (!_levelHandler->IsReforged() && _playerType != PlayerType::Lori &&
					_levelHandler->PlayerActionPressed(this, PlayerAction::Jump) &&
					(_carryingObject != nullptr || HasLegacyFloorBelow())) {
					BeginStandardJump(timeMult);
				}

				if (_playerType == PlayerType::Lori) {
					_speed.X = 0.0f;
				} else if (_levelHandler->IsReforged()) {
					_speed.X = std::clamp(_speed.X, -LegacyWalkSpeed, LegacyWalkSpeed);
				} else {
					// Measured in the original: Lori stops dead, Spaz snaps to the walk cap only with Run up,
					// the same clamp every carry ends with
					float exitCap = (_isRunPressed ? LegacyCarryExitRunSpeed : LegacyWalkSpeed);
					_speed.X = std::clamp(_speed.X, -exitCap, exitCap);
				}
			}
		}

		if (_currentSpecialMove == SpecialMoveType::Sidekick && _currentTransition == nullptr && (sidekickSpent || _controllable || std::abs(_speed.X) < 0.01f)) {
			EndDamagingMove();
			_controllable = true;
			_controllableTimeout = 0.0f;
			// Lori's kick has its own ending outside Reforged
			bool loriOwnEnding = (_playerType == PlayerType::Lori && !_levelHandler->IsReforged());
			if (_suspendType == SuspendType::None && !loriOwnEnding) {
				// Non-cancellable, or the next base state change would drop it at once, a jump force-cancels it
				if (SetTransition(AnimState::TransitionUppercutEnd, false) && _renderer.AnimDuration > 0.0f) {
					_renderer.AnimDuration /= SidekickEndSpeedUp;
				}
			}
		}

		// Copter Ears
		if (_activeModifier != Modifier::Copter && _activeModifier != Modifier::LizardCopter) {
			if (_copterFramesLeft <= 0.0f) {
				_copterCarried = false;
			}
			// TODO: Is this still needed?
			bool cancelCopter;
			if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
				const CharacterProfile& profile = _characterProfile;
				_copterAirTime += timeMult;
				cancelCopter = (CanJump() || _suspendType != SuspendType::None || _copterFramesLeft <= 0.0f ||
					_copterAirTime >= profile.CopterDurationMax / LegacyFrameRateScale);

				SetCopterFlight(_copterFramesLeft - timeMult, FlightType::Normal);
				// Gravity is kept off every frame, a warp turns it back on and the copter survives a warp
				if (!_levelHandler->IsReforged()) {
					SetState(ActorState::ApplyGravitation, false);
				}
				// The descent is the copter's own acceleration, not level gravity (measured in the original)
				float descentSpeed = GetCopterDescentSpeed(profile);
				if (_levelHandler->IsReforged()) {
					_speed.Y = std::min(_speed.Y + _levelHandler->GetGravity() * timeMult, descentSpeed);
				} else if (profile.CopterSpeedY >= 0.0f) {
					// The wind-up scales with the speed, measured at 0.5, 1 and 3
					_speed.Y = std::min(_speed.Y + LegacyCopterWindUp * profile.CopterSpeedY * timeMult, descentSpeed);
				} else if (_speed.Y > 0.0f) {
					// Rising ears brake a fall first, see LegacyCopterAscentBrake
					_speed.Y = std::max(_speed.Y - LegacyCopterAscentBrake * timeMult, 0.0f);
				} else {
					// ...and then rise only while Jump is held, a released key starts the wind-up again from zero
					if (!_wasJumpPressed) {
						_speed.Y = 0.0f;
					}
					_speed.Y = std::max(_speed.Y + LegacyCopterWindUp * profile.CopterSpeedY * timeMult, descentSpeed);
				}

				// Descending sideways, the ears drop the player straight onto a floor that is close, see LegacyCopterFloorSnap
				if (!_levelHandler->IsReforged() && !cancelCopter && _speed.Y > 0.0f && _speed.X != 0.0f) {
					TileCollisionParams params = { TileDestructType::None, true };
					AABBf below = AABBf(AABBInner.L, AABBInner.T + LegacyCopterFloorSnap, AABBInner.R, AABBInner.B + LegacyCopterFloorSnap);
					if (!_levelHandler->IsPositionEmpty(this, below, params)) {
						for (float drop = LegacyCopterFloorSnap - 0.25f; drop > 0.0f; drop -= 0.25f) {
							if (MoveInstantly(Vector2f(0.0f, drop), MoveType::Relative)) {
								break;
							}
						}
						_copterLandGraceLeft = LegacyCopterLandGrace;
					}
				}
			} else if (_copterCarried && _copterFramesLeft > 0.0f) {
				// Carried over a landing: the counter runs during the rise, and the ears take over once the player falls
				cancelCopter = false;
				SetCopterFlight(_copterFramesLeft - timeMult, FlightType::Normal);
				if (_copterFramesLeft > 0.0f && !CanJump() && _speed.Y >= 0.0f && _suspendType == SuspendType::None) {
					SetAnimation(AnimState::Copter);
					SetState(ActorState::ApplyGravitation, false);
#if defined(WITH_AUDIO)
					if (_copterSound == nullptr) {
						_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
						if (_copterSound != nullptr) {
							_copterSound->SetLooping(true);
						}
					}
#endif
				}
			} else {
				cancelCopter = ((_currentAnimation->State & AnimState::Fall) == AnimState::Fall && _copterFramesLeft > 0.0f);
			}

			if (cancelCopter) {
				// A landing the ears dropped onto keeps the counter for a moment, see LegacyCopterLandGrace
				if (_copterLandGraceLeft <= 0.0f || _copterCarried || _copterFramesLeft <= 0.0f || _suspendType != SuspendType::None) {
					_copterFramesLeft = 0.0f;
					_copterCarried = false;
				}
				SetAnimation(_currentAnimation->State & ~AnimState::Copter);
				if (!_isAttachedToPole) {
					SetState(ActorState::ApplyGravitation, true);
				}
				// A flight ending doesn't hand the airtime's attempt back
			}
		}
	}

	void Player::CheckSuspendState(float timeMult)
	{
		if (_suspendTime > 0.0f) {
			_suspendTime -= timeMult;
			return;
		}

		// A bird flies past vines and hooks rather than grabbing them
		if (_suspendType == SuspendType::SwingingVine || _activeModifier != Modifier::None || IsBird()) {
			return;
		}

		auto tiles = _levelHandler->TileMap();
		if (tiles == nullptr) {
			return;
		}

		AnimState currentState = _currentAnimation->State;

		SuspendType newSuspendState = tiles->GetTileSuspendState(_pos.X, _pos.Y - 1.0f);
		Vector2f snapOffset = Vector2f::Zero;

		if (_suspendType != SuspendType::None) {
			// Already attached, the plain point check decides when the player slides off the end of a vine
			if (newSuspendState == _suspendType) {
				return;
			}
		} else {
			// The grab box is anchored to where the settle loop below parks the player
			constexpr float ContactOffset = 4.0f;
			// A falling player catches the vine a bit past the contact line, it looks better
			constexpr float ContactDelay = 4.0f;
			// Vines and hooks spanning only a single tile would be nearly impossible to grab without this
			constexpr float ToleranceX = 14.0f;
			// How much higher than the grab line the vine may be and still be caught
			constexpr float ToleranceUp = 2.0f;
			// The player reaches higher while rising
			constexpr float ToleranceUpRising = 8.0f;

			// The box covers the whole distance travelled this frame, so a fast player can't tunnel through a vine
			float sweep = std::abs(_speed.Y) * timeMult;
			float grabLine = ContactOffset + (_speed.Y > 0.0f ? ContactDelay : 0.0f);
			float reachUp = (!CanJump() && _speed.Y < 0.0f ? ToleranceUpRising : ToleranceUp);

			float toleranceUp = grabLine + reachUp + (_speed.Y > 0.0f ? sweep : 0.0f);
			float toleranceDown = -grabLine + (_speed.Y < 0.0f ? sweep : 0.0f);

			newSuspendState = tiles->GetTileSuspendState(_pos.X, _pos.Y - 1.0f, ToleranceX, toleranceUp, toleranceDown, snapOffset);
			if (newSuspendState == SuspendType::None) {
				return;
			}
		}

		if (newSuspendState != SuspendType::None && _playerType != PlayerType::Frog && _frozenTimeLeft <= 0.0f) {
			// In original (non-Reforged) gameplay, grabbing a vine cancels an ongoing buttstomp
			if (!_levelHandler->IsReforged() && _currentSpecialMove == SpecialMoveType::Buttstomp && newSuspendState == SuspendType::Vine) {
				EndDamagingMove();
			}

			if (_currentSpecialMove == SpecialMoveType::None) {
				// Align with the found attachment point, but never through a solid wall or a ceiling
				if (snapOffset != Vector2f::Zero && !MoveInstantly(snapOffset, MoveType::Relative)) {
					return;
				}

				_suspendType = newSuspendState;
				SetState(ActorState::ApplyGravitation, false);

				if (_speed.Y > 0.0f && newSuspendState == SuspendType::Vine) {
					PlayPlayerSfx("HookAttach"_s, 0.8f, 1.2f);
				}

				_speed.Y = 0.0f;
				_externalForce.Y = 0.0f;
				_isFreefall = false;
				_isSpring = false;
				_copterFramesLeft = 0.0f;

				if (newSuspendState == SuspendType::Hook || _wasFirePressed) {
					_speed.X = 0.0f;
					_externalForce.X = 0.0f;
				}

				// A fast approach can grab from above the vine, so search down for it first
				constexpr float MaxSettleSearch = 32.0f;
				float settleSearched = 0.0f;
				while (settleSearched < MaxSettleSearch && tiles->GetTileSuspendState(_pos.X, _pos.Y - 1) == SuspendType::None) {
					MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative | MoveType::Force);
					settleSearched += 1.0f;
				}
				if (tiles->GetTileSuspendState(_pos.X, _pos.Y - 1) == SuspendType::None) {
					// Nothing within reach, put the player back and let the jump carry on
					MoveInstantly(Vector2f(0.0f, -settleSearched), MoveType::Relative | MoveType::Force);
					_suspendType = SuspendType::None;
					SetState(ActorState::ApplyGravitation, true);
					return;
				}

				// Move downwards until we're on the standard height
				while (tiles->GetTileSuspendState(_pos.X, _pos.Y - 1) != SuspendType::None) {
					MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative | MoveType::Force);
				}
				MoveInstantly(Vector2f(0.0f, -1.0f), MoveType::Relative | MoveType::Force);

				SetAnimation(AnimState::Hook);
			}
		} else {
			_suspendType = SuspendType::None;
			_suspendTime = 8.0f;
			if ((currentState & (AnimState::Buttstomp | AnimState::Copter)) == AnimState::Idle && !_isAttachedToPole) {
				SetState(ActorState::ApplyGravitation, true);
			}
		}
	}

	void Player::OnHandleWater()
	{
		// A bird goes on flying under water, it doesn't swim
		if (IsBird()) {
			return;
		}

		// Nobody swims with low-gravity water, only gravity is lower there
		if (_levelHandler->GetWaterInteraction() == WaterInteraction::LowGravity) {
			if (_inWater) {
				_inWater = false;
				SetState(ActorState::ApplyGravitation | ActorState::CanJump, true);
				_renderer.SetRotation(0.0f);
			}
			return;
		}

		// Far from the surface the water moved (by a script), so no cooldown, splash or push
		const float waterLevel = _levelHandler->GetPlayerWaterLevel();
		const bool farFromSurface = (std::abs(_pos.Y - waterLevel) > 40.0f);

		if (_inWater) {
			if (_pos.Y >= waterLevel) {
				SetState(ActorState::ApplyGravitation, false);

				if (std::abs(_speed.X) > 1.0f || std::abs(_speed.Y) > 1.0f) {
					float angle;
					if (_speed.X == 0.0f) {
						if (IsFacingLeft()) {
							angle = atan2Approx(-_speed.Y, -std::numeric_limits<float>::epsilon());
						} else {
							angle = atan2Approx(_speed.Y, std::numeric_limits<float>::epsilon());
						}
					} else if (_speed.X < 0.0f) {
						angle = atan2Approx(-_speed.Y, -_speed.X);
					} else {
						angle = atan2Approx(_speed.Y, _speed.X);
					}

					if (angle > fPi) {
						angle = angle - fTwoPi;
					}

					_renderer.SetRotation(std::clamp(angle, -fPiOver3, fPiOver3));
				}

				// Adjust swimming animation speed
				if (_currentTransition == nullptr) {
					_renderer.AnimDuration = std::max(_currentAnimation->AnimDuration + 1.0f - Vector2f(_speed.X, _speed.Y).Length() * 0.26f, 0.4f);
				}

			} else if (_waterCooldownLeft <= 0.0f || farFromSurface) {
				_inWater = false;
				_waterCooldownLeft = 20.0f;

				SetState(ActorState::ApplyGravitation | ActorState::CanJump, true);
				_renderer.SetRotation(0.0f);

				SetAnimation(AnimState::Jump);

				if (!farFromSurface) {
					_externalForce.Y = -0.6f;
					OnWaterSplash(Vector2f(_pos.X, waterLevel), false);
				}
			}
		} else {
			if (_pos.Y >= waterLevel && (_waterCooldownLeft <= 0.0f || farFromSurface)) {
				_inWater = true;
				_waterCooldownLeft = 20.0f;
				_flyCheatActive = false;

				if (_activeModifier == Modifier::Copter || _activeModifier == Modifier::Airboard) {
					SetModifier(Modifier::None);
				}

				_controllable = true;
				EndDamagingMove();

				if (!farFromSurface) {
					OnWaterSplash(Vector2f(_pos.X, waterLevel), true);
				}
			}

			// Adjust walking animation speed, outside Reforged UpdateLegacyRunAnimSpeed() does it
			if (_levelHandler->IsReforged() && _currentAnimation->State == AnimState::Walk && _currentTransition == nullptr) {
				_renderer.AnimDuration = _currentAnimation->AnimDuration * (1.4f - 0.4f * std::min(std::abs(_speed.X), MaxRunningSpeed) / MaxRunningSpeed);
			}
		}
	}

	void Player::OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock)
	{
		areaWeaponAllowed = true;
		areaWaterBlock = -1;
		// Re-sampled below
		_inFloatUpArea = false;

		auto events = _levelHandler->EventMap();
		if (events == nullptr) {
			return;
		}

		// Tile events are sampled along the whole path, so a fast player doesn't skip any tile
		constexpr float SweepStep = Tiles::TileSet::DefaultTileSize / 2;
		// Only an upper bound, a teleport resets the path anyway
		constexpr std::int32_t MaxSweepSamples = 16;

		Vector2f delta = _pos - _frameStartPos;
		float distance = std::max(std::abs(delta.X), std::abs(delta.Y));
		std::int32_t sampleCount = (distance > SweepStep
			? std::min<std::int32_t>((std::int32_t)(distance / SweepStep) + 1, MaxSweepSamples)
			: 1);

		// Only the sample that first enters a tile may act on its event
		Vector2i lastTile = Vector2i(INT32_MIN, INT32_MIN);
		bool statesHandled = false;

		for (std::int32_t i = 1; i <= sampleCount; i++) {
			// From just past the frame's start position to exactly the current position
			bool isLastSample = (i == sampleCount);
			Vector2f samplePos = (isLastSample ? _pos : _frameStartPos + delta * ((float)i / sampleCount));

			// Rounded the same way as EventMap::GetEventByPosition()
			Vector2i tile = Vector2i((std::int32_t)samplePos.X / Tiles::TileSet::DefaultTileSize,
				(std::int32_t)samplePos.Y / Tiles::TileSet::DefaultTileSize);

			AreaEventPass pass = (isLastSample ? AreaEventPass::States : AreaEventPass::None);
			if (tile != lastTile) {
				pass |= AreaEventPass::Effects;
				lastTile = tile;
			}
			if (pass == AreaEventPass::None) {
				continue;
			}

			statesHandled |= isLastSample;

			if (HandleAreaEventAt(samplePos.X, samplePos.Y, timeMult, pass, areaWeaponAllowed, areaWaterBlock)) {
				break;
			}
		}

		// An event stopped the walk early, the state pass still has to run at the final position
		if (!statesHandled) {
			HandleAreaEventAt(_pos.X, _pos.Y, timeMult, AreaEventPass::States, areaWeaponAllowed, areaWaterBlock);
		}

		// Leaving the tile of the last script callback lets it be called again
		if (_lastCallbackTile != Vector2i((std::int32_t)_pos.X / Tiles::TileSet::DefaultTileSize, (std::int32_t)_pos.Y / Tiles::TileSet::DefaultTileSize)) {
			_lastCallbackTile = Vector2i(INT32_MIN, INT32_MIN);
		}

		// Cleared only after this frame's samples, so a path leaving the exit cannot take it on the way out
		if (_levelExitSuppressed) {
			std::uint8_t* exitParams;
			if (events->GetEventByPosition(_pos.X, _pos.Y, &exitParams) != EventType::AreaEndOfLevel) {
				_levelExitSuppressed = false;
			}
		}

		// TODO: Implement Slide modifier with JJ2+ parameter

		// Check floating from each corner of an extended hitbox, so the player can't pass a single tile wide gap
		constexpr float ExtendedHitbox = 2.0f;

		std::uint8_t* p;
		// The original lets a buttstomp run in a float-up field with a capped descent, Reforged skips the field
		bool buttstomping = (_currentSpecialMove == SpecialMoveType::Buttstomp);
		if (!_isAttachedToPole && (_currentTransition == nullptr || _currentTransition->State != AnimState::TransitionLedgeClimb)) {
			if (!buttstomping || !_levelHandler->IsReforged()) {
				// Swept along the whole path too, or low frame rates miss float-up tiles. One hit is enough.
				bool floatUpFound = false;
				for (std::int32_t i = 1; i <= sampleCount && !floatUpFound; i++) {
					Vector2f samplePos = (i == sampleCount ? _pos : _frameStartPos + delta * ((float)i / sampleCount));
					Vector2f off = samplePos - _pos;
					floatUpFound =
						(events->GetEventByPosition(samplePos.X, samplePos.Y, &p) == EventType::AreaFloatUp) ||
						(events->GetEventByPosition(AABBInner.L + off.X - ExtendedHitbox, AABBInner.T + off.Y - ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
						(events->GetEventByPosition(AABBInner.R + off.X + ExtendedHitbox, AABBInner.T + off.Y - ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
						(events->GetEventByPosition(AABBInner.R + off.X + ExtendedHitbox, AABBInner.B + off.Y + ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
						(events->GetEventByPosition(AABBInner.L + off.X - ExtendedHitbox, AABBInner.B + off.Y + ExtendedHitbox, &p) == EventType::AreaFloatUp);
				}
				if (floatUpFound) {
					// External force of pinball bumber has higher priority
					if (_externalForceCooldown <= 0.0f || _speed.Y < 0.0f) {
						if (buttstomping) {
							// Only marked, the descent cap in OnUpdatePhysics() does the rest
							_inFloatUpArea = true;
						} else if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
							_speed.Y = std::max(_speed.Y - _levelHandler->GetGravity() * timeMult * 8.0f, -6.0f);
						} else if (!_levelHandler->IsReforged() && (GetState(ActorState::ApplyGravitation) || _inWater)) {
							// Measured in the original: a fixed rise speed in the field, the same under water
							_speed.Y = -LegacyFloatUpSpeed;
							_externalForce.Y = 0.0f;
							// Selects the decay rate after leaving, see GetGravityModifier()
							_riseFromFloatUp = true;
							// No gravity while still inside
							_inFloatUpArea = true;
						} else if (GetState(ActorState::ApplyGravitation)) {
							float gravity = _levelHandler->GetGravity();
							_externalForce.Y = -2.0f * gravity * timeMult;
							_speed.Y = std::min(gravity * timeMult, _speed.Y);
						} else {
							_speed.Y = std::max(_speed.Y - _levelHandler->GetGravity() * timeMult, -6.0f);
						}
					}
				}
			}

			if ((events->GetEventByPosition(_pos.X, _pos.Y, &p) == EventType::AreaHForce) ||
				(events->GetEventByPosition(AABBInner.L - ExtendedHitbox, AABBInner.T - ExtendedHitbox, &p) == EventType::AreaHForce) ||
				(events->GetEventByPosition(AABBInner.R + ExtendedHitbox, AABBInner.T - ExtendedHitbox, &p) == EventType::AreaHForce) ||
				(events->GetEventByPosition(AABBInner.R + ExtendedHitbox, AABBInner.B + ExtendedHitbox, &p) == EventType::AreaHForce) ||
				(events->GetEventByPosition(AABBInner.L - ExtendedHitbox, AABBInner.B + ExtendedHitbox, &p) == EventType::AreaHForce)
			   ) {
				std::uint8_t p1 = p[4];
				std::uint8_t p2 = p[5];
				if ((p2 != 0 || p1 != 0)) {
					// Measured in the original, the direction comes from the signed parameter, not the event type
					float factor = (_levelHandler->IsReforged() ? 0.7f : LegacyWindFactor);
					MoveInstantly(Vector2f((p2 - p1) * factor * timeMult, 0), MoveType::Relative);
				}
			}

			if (GetState(ActorState::CanJump)) {
				// Floor events
				switch (events->GetEventByPosition(_pos.X, _pos.Y + 32, &p)) {
					case EventType::AreaHForce: {
						std::uint8_t p1 = p[0];
						std::uint8_t p2 = p[1];
						std::uint8_t p3 = p[2];
						std::uint8_t p4 = p[3];
						if (p2 != 0 || p1 != 0) {
							// Position move like the wind, measured in the original at twice its strength
							float factor = (_levelHandler->IsReforged() ? 0.7f : LegacyBeltFactor);
							MoveInstantly(Vector2f((p2 - p1) * factor * timeMult, 0), MoveType::Relative);
						}
						// Accelerating belt (Reforged only), the non-Reforged one must come after the brake in HandleHorizontalMovement()
						if ((p4 != 0 || p3 != 0) && _levelHandler->IsReforged()) {
							_speed.X += (p4 - p3) * 0.1f;
						}
						break;
					}
				}
			}
		}
	}

	bool Player::HandleAreaEventAt(float x, float y, float timeMult, AreaEventPass pass, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock)
	{
		auto events = _levelHandler->EventMap();

		std::uint8_t* p;
		EventType tileEvent = events->GetEventByPosition(x, y, &p);

		// States are read only from the tile the player ended the frame in, not from samples in between
		if ((pass & AreaEventPass::States) == AreaEventPass::States) {
			switch (tileEvent) {
				case EventType::LightAmbient: { // Intensity, Red, Green, Blue, Flicker
					// TODO: Change only player view, handle splitscreen multiplayer
					_levelHandler->SetAmbientLight(this, p[0] / 255.0f);
					break;
				}
				case EventType::AreaNoFire: {
					if (p[0] == 0) {
						areaWeaponAllowed = false;
					}
					break;
				}
				case EventType::AreaWaterBlock: {
					// The adjustment is signed, a negative one raises the water above the tile
					areaWaterBlock = ((std::int32_t)y / 32) * 32 + (std::int8_t)p[0];
					break;
				}
				default: {
					break;
				}
			}
		}

		if ((pass & AreaEventPass::Effects) != AreaEventPass::Effects) {
			return false;
		}

		switch (tileEvent) {
			case EventType::WarpOrigin: { // Warp ID, Fast, Set Lap
				// Allow warping only if not in non-cancellable transition, except for some cosmetic but non-cancellable ones
				if (_currentTransition == nullptr || _currentTransitionCancellable ||
					   (_currentTransition->State == (AnimState::Dash | AnimState::Jump) ||
						_currentTransition->State == AnimState::Spring ||
						_currentTransition->State == AnimState::TransitionCopterShootToCopter ||
						_currentTransition->State == AnimState::TransitionFallShootToFall ||
						_currentTransition->State == AnimState::TransitionHookShootToHook ||
						_currentTransition->State == AnimState::TransitionShootToIdle ||
						_currentTransition->State == AnimState::TransitionUppercutEnd)) {
					Vector2f c = events->GetWarpTarget(p[0]);
					if (c.X >= 0.0f && c.Y >= 0.0f) {
						WarpFlags flags = WarpFlags::Default;
						if (p[1] != 0) {
							flags |= WarpFlags::Fast;
						}
						if (p[2] != 0) {
							flags |= WarpFlags::IncrementLaps;
						}

						WarpToPosition(c, flags);
						return true;
					}
				}
				break;
			}
			case EventType::ModifierDeath: {
				_inDeathEvent = true;
				TakeDamage(INT32_MAX, 0.0f, true);
				_inDeathEvent = false;
				return true;
			}
			case EventType::ModifierSetWater: {
				_levelHandler->BroadcastTriggeredEvent(this, EventType::ModifierSetWater, p);
				break;
			}
			case EventType::ModifierLimitCameraView: { // Left, Width
				// Parameters start at an odd offset, a direct uint16 load crashes on MIPS and SH-4
				EventParamsReader eventParams(p);
				std::uint16_t left = eventParams.GetUint16(0);
				std::uint16_t width = eventParams.GetUint16(2);
				_levelHandler->LimitCameraView(this, _pos,
					(left == 0 ? (std::int32_t)(x / Tiles::TileSet::DefaultTileSize) : left) * Tiles::TileSet::DefaultTileSize,
					width * Tiles::TileSet::DefaultTileSize);
				break;
			}
			case EventType::ModifierHPole: {
				// A declined pole returns false, so the rest of the path is still examined
				return InitialPoleStage(true, Vector2f(x, y));
			}
			case EventType::ModifierVPole: {
				return InitialPoleStage(false, Vector2f(x, y));
			}
			case EventType::ModifierTube: { // XSpeed, YSpeed, Wait Time, Trig Sample, Become No-clip, No-clip Only
				// TODO: Implement other parameters
				bool becomeNoclip = (p[4] != 0);
				bool noclipOnly = (p[5] != 0);
				if (noclipOnly == GetState(ActorState::CollideWithTileset)) {
					// Noclip Only tubes are for noclip players only, and the other way around
					break;
				}

				// The warp-out pose a player can still be in from a warp just before isn't drawn over the ball
				if (_currentTransition != nullptr && (_currentTransition->State == AnimState::TransitionWarpOut ||
					_currentTransition->State == AnimState::TransitionWarpOutFreefall)) {
					_currentTransitionCancellable = true;
					CancelTransition();
				}

				EndDamagingMove();

				SetAnimation(AnimState::Dash | AnimState::Jump);
				_tubeBallPoseLeft = TubeBallPoseTime;
				_tubeHeldSpeedY = 0.0f;

				_controllable = false;
				SetState(ActorState::CanJump, false);
				// The original keeps gravity on through a tube, only Reforged switches it off
				if (_levelHandler->IsReforged()) {
					SetState(ActorState::ApplyGravitation, false);
				}

				// Parameters are in px per 70 Hz tick of the original, the same in both modes
				_speed.X = (float)(std::int8_t)p[0] * LegacyFrameRateScale;
				_speed.Y = (float)(std::int8_t)p[1] * LegacyFrameRateScale;

				// Snap onto the tile the event was found in, not the one the frame ended in
				float tubeSnapY = TubeSnapY;
				Vector2f pos = Vector2f(x, y);
				if (_speed.X == 0.0f) {
					pos.X = (floorFast(pos.X / 32) * 32) + 16;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				} else if (_speed.Y == 0.0f) {
					pos.Y = (floorFast(pos.Y / 32) * 32) + tubeSnapY;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				} else if (_inTubeTime <= 0.0f) {
					pos.X = (floorFast(pos.X / 32) * 32) + 16;
					pos.Y = (floorFast(pos.Y / 32) * 32) + tubeSnapY;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				}

				SetState(ActorState::CollideWithTileset, !becomeNoclip);
				// Noclip has no gravity, the tube speed is kept until a noclip-only tube ends it
				if (becomeNoclip) {
					SetState(ActorState::ApplyGravitation, false);
				} else if (!_levelHandler->IsReforged()) {
					SetState(ActorState::ApplyGravitation, true);
				}
				_inTubeTime = (becomeNoclip ? 600.0f : TubeControlTime);
				return true;
			}
			case EventType::AreaEndOfLevel: { // ExitType, Fast (No score count, only black screen), TextID, TextOffset, Coins
				if (_levelExiting == LevelExitingState::None && !_levelExitSuppressed) {
					// TODO: Implement Fast parameter
					ExitType exitType = (ExitType)p[0];
					if (p[1] != 0) {
						exitType |= ExitType::FastTransition;
					}
					StringView nextLevel;
					if (p[2] != 0) {
						nextLevel = _levelHandler->GetLevelText(p[2], p[3], '|');
					}
					// An exit to an already-visited special level does nothing, checked before the coins
					if (!_levelHandler->CanTakeLevelExit(exitType, nextLevel)) {
						break;
					}

					// Unaligned parameter, a direct cast crashed real PSP hardware
					std::uint16_t coinsRequired = EventParamsReader(p).GetUint16(4);
					if (coinsRequired <= _inventory.Coins) {
						_inventory.Coins -= coinsRequired;
						_levelHandler->BeginLevelChange(this, exitType, nextLevel);
						return true;
					} else if (_bonusWarpTimer <= 0.0f) {
						_levelHandler->HandlePlayerCoins(this, _inventory.Coins, _inventory.Coins);
						PlaySfx("BonusWarpNotEnoughCoins"_s);

						_bonusWarpTimer = 400.0f;
					}
				}
				break;
			}
			case EventType::AreaText: { // Text, TextOffset, Vanish
				std::uint8_t index = p[1];
				StringView text = _levelHandler->GetLevelText(p[0], index != 0 ? index : -1, '|');
				_levelHandler->ShowLevelText(text, this);

				if (p[2] != 0) {
					events->StoreTileEvent((std::int32_t)(x / 32), (std::int32_t)(y / 32), EventType::Empty);
				}
				break;
			}
			case EventType::AreaCallback: { // Function, Param, Vanish
				// Skip AreaCallbacks if player is currently warping
				if (_currentTransition == nullptr ||
					(_currentTransition->State != AnimState::TransitionWarpIn && _currentTransition->State != AnimState::TransitionWarpOut &&
					 _currentTransition->State != AnimState::TransitionWarpInFreefall && _currentTransition->State != AnimState::TransitionWarpOutFreefall)) {
					// Called once on entering the tile, not on every frame the player stays in it
					Vector2i tile = Vector2i((std::int32_t)x / Tiles::TileSet::DefaultTileSize, (std::int32_t)y / Tiles::TileSet::DefaultTileSize);
					if (tile != _lastCallbackTile) {
						_lastCallbackTile = tile;
						_levelHandler->BroadcastTriggeredEvent(this, EventType::AreaCallback, p);
					}
				}
				break;
			}
			case EventType::AreaActivateBoss: { // Music
				_levelHandler->BroadcastTriggeredEvent(this, EventType::AreaActivateBoss, p);

				// Deactivate sugar rush if it's active
				if (_sugarRushLeft > 1.0f) {
					_sugarRushLeft = 1.0f;
				}
				break;
			}
			case EventType::AreaFlyOff: {
				// Ends the flying carrot too, outside Reforged it has no duration of its own
				if ((_activeModifier == Modifier::Airboard || _activeModifier == Modifier::Copter) && !IsFlyCheatActive()) {
					SetModifier(Modifier::None);
				}
				// A bird reverts, as at a Revert Morph area
				if (IsBird()) {
					MorphRevert();
					return true;
				}
				break;
			}
			case EventType::AreaRevertMorph: {
				// Straight back to the level's starting character, even from a nested temporary form
				if (_playerType != _playerTypeOriginal) {
					MorphTo(_playerTypeOriginal);
					return true;
				}
				break;
			}
			case EventType::AreaMorphToFrog: {
				if (_playerType != PlayerType::Frog) {
					MorphTo(PlayerType::Frog);
					return true;
				}
				break;
			}
			case EventType::AreaNoFire: {
				// Only the permanent switches, the temporary block (0) is handled in the state pass
				switch (p[0]) {
					case 1: _weaponAllowed = true; break;
					case 2: _weaponAllowed = false; break;
				}
				break;
			}
			case EventType::TriggerZone: { // Trigger ID, Turn On, Switch
				// TODO: Implement Switch parameter
				_levelHandler->SetTrigger(p[0], p[1] != 0);
				break;
			}

			case EventType::RollingRockTrigger: { // Rock ID
				_levelHandler->BroadcastTriggeredEvent(this, EventType::RollingRockTrigger, p);
				break;
			}
		}

		return false;
	}

	void Player::OnHandleSpectate(float timeMult)
	{
		constexpr float SpectateSpeed = 8.0f;

		Vector2f speed;

		float playerMovement = _levelHandler->PlayerHorizontalMovement(this);
		if (std::abs(playerMovement) > 0.3f) {
			speed.X = playerMovement * SpectateSpeed;
		} else {
			speed.X = 0.0f;
		}

		float playerMovementVert = _levelHandler->PlayerVerticalMovement(this);
		if (std::abs(playerMovementVert) > 0.3f) {
			speed.Y = playerMovementVert * SpectateSpeed;
		} else {
			speed.Y = 0.0f;
		}

		if (_levelHandler->PlayerActionPressed(this, PlayerAction::Run)) {
			speed *= 2.0f;
		} else if (_levelHandler->PlayerActionPressed(this, PlayerAction::Jump)) {
			speed *= 0.5f;
		}

		auto levelBounds = _levelHandler->GetLevelBounds();
		_pos.X = std::clamp(_pos.X + speed.X * timeMult, float(levelBounds.X), float(levelBounds.X + levelBounds.W));
		_pos.Y = std::clamp(_pos.Y + speed.Y * timeMult, float(levelBounds.Y), float(levelBounds.Y + levelBounds.H));
	}

	std::shared_ptr<AudioBufferPlayer> Player::PlayPlayerSfx(StringView identifier, float gain, float pitch)
	{
		auto it = _metadata->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _metadata->Sounds.end()) {
			AudioBuffer* buffer;
			// The samples may not be loaded yet (see ContentResolver::ResolveSound)
			if (ContentResolver::Get().ResolveSound(it->second)) {
				std::int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (std::int32_t)it->second.Buffers.size()) : 0);
				buffer = &it->second.Buffers[idx]->Buffer;
			} else {
				buffer = nullptr;
			}
			
			return _levelHandler->PlaySfx(this, identifier, buffer, Vector3f::Zero, true, gain, pitch);
		}

		// A sound of the weapons of the level, see ActorBase::PlaySfx()
		if (auto* sound = ContentResolver::Get().FindWeaponSound(identifier)) {
			AudioBuffer* buffer = (!sound->Buffers.empty() ? &sound->Buffers[0]->Buffer : nullptr);
			return _levelHandler->PlaySfx(this, identifier, buffer, Vector3f::Zero, true, gain, pitch);
		}

		return nullptr;
	}

	bool Player::SetPlayerTransition(AnimState state, bool cancellable, bool removeControl, SpecialMoveType specialMove, Function<void()>&& callback)
	{
		if (removeControl) {
			_controllable = false;
			_controllableTimeout = 0.0f;
		}

		_currentSpecialMove = specialMove;
		return SetTransition(state, cancellable, Death::move(callback));
	}

	bool Player::CanFreefall()
	{
		AABBf aabb = AABBf(_pos.X - 14, _pos.Y + 8 - 12, _pos.X + 14, _pos.Y + 8 + 12 + 100);
		TileCollisionParams params = { TileDestructType::None, true };
		return _levelHandler->IsPositionEmpty(this, aabb, params);
	}

	void Player::OnPerishInner()
	{
		_trailLastPos = _pos;
		// Before the rest, which only reaches the instances playing here
		StopRevUpSfx();
		StopWeaponSfx();
		StopAllActiveSounds();

		SetState(ActorState::CanJump, false);
		_speed.X = 0.0f;
		_speed.Y = 0.0f;
		_externalForce.X = 0.0f;
		_externalForce.Y = 0.0f;
		_internalForceY = 0.0f;
		_fireFramesLeft = 0.0f;
		_copterFramesLeft = 0.0f;
		_pushFramesLeft = 0.0f;
		_weaponCooldown = 0.0f;
		_controllableTimeout = 0.0f;
		_inShallowWater = -1;
		_keepRunningTime = 0.0f;
		_invulnerableTime = 0.0f;
		_lastPoleTime = 0.0f;
		_isAttachedToPole = false;
		SetDizzy(0.0f);
		SetModifier(Modifier::None);
		SetShield(ShieldType::None, 0.0f);

		SetPlayerTransition(AnimState::TransitionDeath, false, true, SpecialMoveType::None, [this]() {
			_speed.X = 0.0f;
			_speed.Y = 0.0f;
			_externalForce.X = 0.0f;
			_externalForce.Y = 0.0f;
			_internalForceY = 0.0f;
			_inShallowWater = -1;
			_keepRunningTime = 0.0f;
			_carryingObject = nullptr;

			if (_lives > 1 || !_levelHandler->IsLocalSession()) {
				if (_lives > 1 && _lives < UINT8_MAX) {
					_lives--;
				}

				// Revert coins, gems, ammo and weapon upgrades; food eaten is reverted only if Reforged
				std::int32_t foodEaten = _inventory.FoodEaten;
				_inventory = _inventoryCheckpoint;
				if (!_levelHandler->IsReforged()) {
					_inventory.FoodEaten = foodEaten;
				}

				// Remove all fast fires and Blaster upgrades
				_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] = 0;

				// Reset current weapon to Blaster if player has no ammo on checkpoint
				if (_inventory.WeaponAmmo[(std::int32_t)_currentWeapon] == 0) {
					SetCurrentWeapon(WeaponType::Blaster, SetCurrentWeaponReason::Rollback);
				}

				if (_sugarRushLeft > 0.0f) {
					_sugarRushLeft = 0.0f;
					_renderer.Initialize(ActorRendererType::Default);
				}

				// Spawn corpse with the effective fur color
				std::uint32_t furColor = GetEffectiveFurColor();
				std::uint8_t playerParams[6] = { (std::uint8_t)_playerType, (std::uint8_t)(IsFacingLeft() ? 1 : 0),
					(std::uint8_t)(furColor & 0xFF), (std::uint8_t)((furColor >> 8) & 0xFF),
					(std::uint8_t)((furColor >> 16) & 0xFF), (std::uint8_t)((furColor >> 24) & 0xFF) };
				std::shared_ptr<PlayerCorpse> corpse = std::make_shared<PlayerCorpse>();
				corpse->OnActivated(ActorActivationDetails(
					_levelHandler,
					Vector3i(_pos.X, _pos.Y, _renderer.GetLayer() - 40),
					playerParams
				));
				_levelHandler->AddActor(corpse);

				// Revert the player type too, a morph monitor could have changed it since the checkpoint
				if (_playerType != _checkpointPlayerType && _playerType != PlayerType::Spectate && _checkpointPlayerType != PlayerType::Spectate) {
					MorphToInstantly(_checkpointPlayerType);
				}

				SetAnimation(AnimState::Idle);

				if (_levelHandler->HandlePlayerDied(this)) {
					// Reset health
					_health = _maxHealth;

					// Player can be respawned immediately
					if (_invulnerableTime <= 0.0f) {
						SetState(ActorState::IsInvulnerable, false);
					}
					SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects, true);
					_controllable = true;

					// Return to the last save point
					MoveInstantly(_checkpointPos, MoveType::Absolute | MoveType::Force);
					_levelHandler->SetAmbientLight(this, _checkpointLight);
				} else {
					// Respawn is delayed
					_controllable = false;
					_renderer.SetDrawEnabled(false);

					_invulnerableTime = 0.0f;
					SetState(ActorState::IsInvulnerable, true);
					SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors, false);
				}
			} else {
				_lives = 0;
				_renderer.SetDrawEnabled(false);

				_levelHandler->HandleGameOver(this);
			}
		});

		PlayPlayerSfx("Die"_s, 1.3f);
		_levelHandler->PlayerExecuteRumble(this, "Die"_s);
	}

	void Player::SwitchToNextWeapon()
	{
		StopWeaponSfx();

		// Find next available weapon
		WeaponType weaponType = (WeaponType)(((std::int32_t)_currentWeapon + 1) % (std::int32_t)WeaponType::Count);
		for (std::int32_t i = 0; i < (std::int32_t)WeaponType::Count && _inventory.WeaponAmmo[(std::int32_t)weaponType] == 0; i++) {
			weaponType = (WeaponType)(((std::int32_t)weaponType + 1) % (std::int32_t)WeaponType::Count);
		}
		SetCurrentWeapon(weaponType, SetCurrentWeaponReason::User);

		_weaponCooldown = 1.0f;
	}

	void Player::SwitchToWeaponByIndex(std::uint32_t weaponIndex)
	{
		if (weaponIndex >= (std::uint32_t)WeaponType::Count || _inventory.WeaponAmmo[weaponIndex] == 0) {
			PlayPlayerSfx("ChangeWeapon"_s);
			return;
		}
		StopWeaponSfx();
		SetCurrentWeapon((WeaponType)weaponIndex, SetCurrentWeaponReason::User);
		_weaponCooldown = 1.0f;
	}

	template<typename T, WeaponType weaponType>
	void Player::FireWeapon(float cooldownBase, float cooldownUpgrade, bool emitFlare)
	{
		// NOTE: cooldownBase and cooldownUpgrade cannot be template parameters in Emscripten
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		std::shared_ptr<T> shot = std::make_shared<T>();
		std::uint8_t shotParams[1] = { _inventory.WeaponUpgrades[(std::int32_t)weaponType] };
		shot->OnActivated(ActorActivationDetails(
			_levelHandler,
			initialPos,
			shotParams
		));
		shot->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
		_levelHandler->AddActor(shot);

		std::int32_t fastFire = (_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		_weaponCooldown = cooldownBase - (fastFire * cooldownUpgrade);

		if (emitFlare) {
			EmitWeaponFlare();
		}
	}

	template<typename T, WeaponType weaponType>
	void Player::FireBirdShotPair(float spread)
	{
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		std::uint8_t shotParams[1] = { _inventory.WeaponUpgrades[(std::int32_t)weaponType] };
		const float offsets[] = { 0.0f, spread };
		for (float offset : offsets) {
			std::shared_ptr<T> shot = std::make_shared<T>();
			shot->OnActivated(ActorActivationDetails(
				_levelHandler,
				initialPos,
				shotParams
			));
			shot->OnFire(shared_from_this(), gunspotPos, _speed, angle + offset, IsFacingLeft());
			_levelHandler->AddActor(shot);
		}

		EmitWeaponFlare();
	}

	void Player::FireBirdShots()
	{
		// The second bird fires a pair of shots for one round, measured in the original
		// Weapons without a bullet to double fire a blaster pair instead, like the companion bird
		float spread = (IsFacingLeft() ? -BirdShotSpread : BirdShotSpread);
		std::uint16_t ammoDecrease = 256;
		switch (_currentWeapon) {
			case WeaponType::Bouncer: FireBirdShotPair<Weapons::BouncerShot, WeaponType::Bouncer>(spread); break;
			case WeaponType::Freezer: FireBirdShotPair<Weapons::FreezerShot, WeaponType::Freezer>(spread); break;
			case WeaponType::Seeker: FireBirdShotPair<Weapons::SeekerShot, WeaponType::Seeker>(spread); break;
			case WeaponType::Electro: FireBirdShotPair<Weapons::ElectroShot, WeaponType::Electro>(spread); break;
			default: {
				FireBirdShotPair<Weapons::BlasterShot, WeaponType::Blaster>(spread);
				if (_levelHandler->GetWeaponDefinition(WeaponType::Blaster).Options.DefaultSample) {
					PlayPlayerSfx("WeaponBlaster"_s);
				}
				ammoDecrease = 0;
				break;
			}
		}

		if (ammoDecrease > 0 && _inventory.WeaponAmmo[(std::int32_t)_currentWeapon] != UINT16_MAX) {
			auto& currentAmmo = _inventory.WeaponAmmo[(std::int32_t)_currentWeapon];
			if (ammoDecrease > currentAmmo) {
				ammoDecrease = currentAmmo;
			}
			currentAmmo -= ammoDecrease;

			// No ammo, switch weapons
			if (currentAmmo == 0) {
				_inventory.WeaponUpgrades[(std::int32_t)_currentWeapon] &= ~0x01;
				SwitchToNextWeapon();
				PlayPlayerSfx("ChangeWeapon"_s);
			}
		}
	}

	void Player::FireWeaponRF()
	{
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		// Measured in the original: the shot starts at the player's leading edge, not the centre
		if (!_levelHandler->IsReforged()) {
			float halfWidth = (AABBInner.R - AABBInner.L) * 0.5f;
			initialPos.X = (std::int32_t)(_pos.X + (IsFacingLeft() ? -halfWidth : halfWidth));
		}

		uint8_t shotParams[1] = { _inventory.WeaponUpgrades[(std::int32_t)WeaponType::RF] };

		if ((_inventory.WeaponUpgrades[(std::int32_t)WeaponType::RF] & 0x1) != 0) {
			std::shared_ptr<Weapons::RFShot> shot1 = std::make_shared<Weapons::RFShot>();
			shot1->OnActivated(ActorActivationDetails(
				_levelHandler,
				initialPos,
				shotParams
			));
			shot1->OnFire(shared_from_this(), gunspotPos, _speed, angle - 0.3f, IsFacingLeft());
			_levelHandler->AddActor(shot1);

			std::shared_ptr<Weapons::RFShot> shot2 = std::make_shared<Weapons::RFShot>();
			shot2->OnActivated(ActorActivationDetails(
				_levelHandler,
				initialPos,
				shotParams
			));
			shot2->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
			_levelHandler->AddActor(shot2);

			std::shared_ptr<Weapons::RFShot> shot3 = std::make_shared<Weapons::RFShot>();
			shot3->OnActivated(ActorActivationDetails(
				_levelHandler,
				initialPos,
				shotParams
			));
			shot3->OnFire(shared_from_this(), gunspotPos, _speed, angle + 0.3f, IsFacingLeft());
			_levelHandler->AddActor(shot3);
		} else {
			std::shared_ptr<Weapons::RFShot> shot1 = std::make_shared<Weapons::RFShot>();
			shot1->OnActivated(ActorActivationDetails(
				_levelHandler,
				initialPos,
				shotParams
			));
			shot1->OnFire(shared_from_this(), gunspotPos, _speed, angle - 0.26f, IsFacingLeft());
			_levelHandler->AddActor(shot1);

			std::shared_ptr<Weapons::RFShot> shot2 = std::make_shared<Weapons::RFShot>();
			shot2->OnActivated(ActorActivationDetails(
				_levelHandler,
				initialPos,
				shotParams
			));
			shot2->OnFire(shared_from_this(), gunspotPos, _speed, angle + 0.26f, IsFacingLeft());
			_levelHandler->AddActor(shot2);
		}

		_weaponCooldown = 120.0f;
		EmitWeaponFlare();
	}

	void Player::FireWeaponPepper()
	{
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		uint8_t shotParams[1] = { _inventory.WeaponUpgrades[(std::int32_t)WeaponType::Pepper] };

		std::shared_ptr<Weapons::PepperShot> shot1 = std::make_shared<Weapons::PepperShot>();
		shot1->OnActivated(ActorActivationDetails(
			_levelHandler,
			initialPos,
			shotParams
		));
		shot1->OnFire(shared_from_this(), gunspotPos, _speed, angle - Random().NextFloat(-0.2f, 0.2f), IsFacingLeft());
		_levelHandler->AddActor(shot1);

		std::shared_ptr<Weapons::PepperShot> shot2 = std::make_shared<Weapons::PepperShot>();
		shot2->OnActivated(ActorActivationDetails(
			_levelHandler,
			initialPos,
			shotParams
		));
		shot2->OnFire(shared_from_this(), gunspotPos, _speed, angle + Random().NextFloat(-0.2f, 0.2f), IsFacingLeft());
		_levelHandler->AddActor(shot2);

		std::int32_t fastFire = (_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		_weaponCooldown = 30.0f - (fastFire * 2.76f);
		EmitWeaponFlare();
	}

	void Player::FireWeaponTNT()
	{
		std::shared_ptr<Weapons::TNT> tnt = std::make_shared<Weapons::TNT>();
		tnt->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 2)
		));
		tnt->OnFire(shared_from_this());
		_levelHandler->AddActor(tnt);

		_levelHandler->PlayerExecuteRumble(this, "FireWeak"_s);

		_weaponCooldown = 60.0f;
	}

	bool Player::FireWeaponThunderbolt()
	{
		if (_isActivelyPushing || _inWater || _isAttachedToPole) {
			return false;
		}

		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		std::shared_ptr<Weapons::Thunderbolt> shot = std::make_shared<Weapons::Thunderbolt>();
		uint8_t shotParams[1] = { _inventory.WeaponUpgrades[(std::int32_t)WeaponType::Thunderbolt] };
		shot->OnActivated(ActorActivationDetails(
			_levelHandler,
			initialPos,
			shotParams
		));
		shot->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
		_levelHandler->AddActor(shot);

		_weaponCooldown = 12.0f - (_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] * 0.1f);

		if (!_inWater && (_currentAnimation->State & AnimState::Lookup) != AnimState::Lookup) {
			AddExternalForce(IsFacingLeft() ? 2.0f : -2.0f, 0.0f);
		}

		if (_weaponSoundType == WeaponType::Unknown) {
			PlaySfx("WeaponThunderboltStart"_s, 0.5f);
			// Pitch passed here so the peers get it too
			StartWeaponSfx(WeaponType::Thunderbolt, "WeaponThunderbolt"_s, 1.0f, Random().FastFloat(1.05f, 1.2f));
#if defined(WITH_AUDIO)
			if (_weaponSound != nullptr) {
				_weaponSound->SetLowPass(0.9f);
			}
#endif
		}

		return true;
	}

	bool Player::FireCurrentWeapon(WeaponType weaponType)
	{
		if (_playerType == PlayerType::Bird2) {
			// The second bird fires on the press only, so the weapon's cooldown doesn't apply to it
			FireBirdShots();
			return true;
		}

		if (_weaponCooldown > 0.0f) {
			return (weaponType != WeaponType::TNT);
		}

		std::uint16_t ammoDecrease = 256;
		const WeaponDefinition& definition = _levelHandler->GetWeaponDefinition(weaponType);
		if (!definition.Options.Allowed) {
			return false;
		}

		// The level may fire the shots itself (a script that controls the bullets), the blaster only without a shield
		if (!(weaponType == WeaponType::Blaster && _activeShield != ShieldType::None) && _levelHandler->HandlePlayerFire(this, weaponType)) {
			std::int32_t fastFire = (_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
			_weaponCooldown = 30.0f - (fastFire * 2.76f);
			EmitWeaponFlare();
			if (weaponType == WeaponType::Blaster) {
				ammoDecrease = 0;
			}
		} else if (_inWater && definition.Options.ReplacedByBubbles) {
			Vector3i initialPos;
			Vector2f gunspotPos;
			float angle;
			GetFirePointAndAngle(initialPos, gunspotPos, angle);
			std::uint8_t shotParams[3] = { 0, std::uint8_t(weaponType), Weapons::CustomShot::IsBubble };
			std::shared_ptr<Weapons::CustomShot> shot = std::make_shared<Weapons::CustomShot>();
			shot->OnActivated(ActorActivationDetails(_levelHandler, initialPos, shotParams));
			shot->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
			_levelHandler->AddActor(shot);
			_weaponCooldown = 30.0f;
			if (weaponType == WeaponType::Blaster) {
				ammoDecrease = 0;
			} else if (definition.Kind == WeaponKind::Custom) {
				ammoDecrease = definition.GetCustom().Fire.AmmoPerShot;
			}
		} else if (definition.Kind == WeaponKind::Custom && !(weaponType == WeaponType::Blaster && _activeShield != ShieldType::None &&
			definition.Options.ReplacedByShield)) {
			if (!FireCustomWeapon(weaponType, definition, ammoDecrease)) {
				return false;
			}
		} else switch (weaponType) {
			case WeaponType::Blaster:
				switch (definition.Options.ReplacedByShield ? _activeShield : ShieldType::None) {
					case ShieldType::Fire: {
						if (_inWater) {
							return false;
						}
						FireWeapon<Weapons::ShieldFireShot, WeaponType::Blaster>(10.0f, 0.0f, true);
						break;
					}
					case ShieldType::Water: {
						FireWeapon<Weapons::ShieldWaterShot, WeaponType::Blaster>(8.0f, 0.0f);
						break;
					}
					case ShieldType::Lightning: {
						FireWeapon<Weapons::ShieldLightningShot, WeaponType::Blaster>(10.0f, 0.0f);
						break;
					}
					default: {
						FireWeapon<Weapons::BlasterShot, WeaponType::Blaster>(30.0f, 2.76f, true);
						if (_levelHandler->GetWeaponDefinition(WeaponType::Blaster).Options.DefaultSample) {
							PlayPlayerSfx("WeaponBlaster"_s);
						}
						break;
					}
				}
				ammoDecrease = 0;
				break;

			case WeaponType::Bouncer: FireWeapon<Weapons::BouncerShot, WeaponType::Bouncer>(30.0f, 2.76f, true); break;
				case WeaponType::Freezer:
					// TODO: Add upgraded freezer
					FireWeapon<Weapons::FreezerShot, WeaponType::Freezer>(30.0f, 2.76f);
					break;
				case WeaponType::Seeker: FireWeapon<Weapons::SeekerShot, WeaponType::Seeker>(120.0f, 0.0f, true); break;
				case WeaponType::RF: FireWeaponRF(); break;

				case WeaponType::Toaster: {
					if (_inWater) {
						return false;
					}
					FireWeapon<Weapons::ToasterShot, WeaponType::Toaster>(6.0f, 0.0f);
					StartWeaponSfx(WeaponType::Toaster, "WeaponToaster"_s, 0.6f, 1.0f);
					ammoDecrease = 50;
					break;
				}

				case WeaponType::TNT: FireWeaponTNT(); break;
				case WeaponType::Pepper: FireWeaponPepper(); break;
				case WeaponType::Electro: FireWeapon<Weapons::ElectroShot, WeaponType::Electro>(30.0f, 2.76f, true); break;

				case WeaponType::Thunderbolt: {
					if (!FireWeaponThunderbolt()) {
						return false;
					}
					ammoDecrease = ((_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Thunderbolt] & 0x1) != 0 ? 40 : 80); // Lower ammo consumption with upgrade
					break;
				}

			default:
				return false;
		}

		auto& currentAmmo = _inventory.WeaponAmmo[(std::int32_t)weaponType];
		if (currentAmmo == UINT16_MAX) {
			ammoDecrease = 0;
		} else if (ammoDecrease > currentAmmo) {
			ammoDecrease = currentAmmo;
		}
		currentAmmo -= ammoDecrease;

		// No ammo, switch weapons
		if (currentAmmo == 0) {
			// Remove upgrade if no ammo left
			_inventory.WeaponUpgrades[(std::int32_t)weaponType] &= ~0x01;

			SwitchToNextWeapon();
			PlayPlayerSfx("ChangeWeapon"_s);
			_weaponCooldown = 20.0f;
		}

		return (weaponType != WeaponType::TNT);
	}

	bool Player::FireCustomWeapon(WeaponType weaponType, const WeaponDefinition& definition, std::uint16_t& ammoDecrease)
	{
		const WeaponFireOptions& fire = definition.GetCustom().Fire;
		if (!fire.AllowedInWater && _inWater) {
			return false;
		}
		// As the thunderbolt, see FireWeaponThunderbolt()
		if (fire.StandStill && (_isActivelyPushing || _isAttachedToPole)) {
			return false;
		}
		if (definition.Options.Style == WeaponStyle::Missile && !_levelHandler->PlayerActionHit(this, PlayerAction::Fire)) {
			return false;
		}

		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		// The player plays the sound if it has it, the shot otherwise
		String sound = _levelHandler->GetWeapons().GetFireSoundIdentifier(fire);
		bool playSound = (definition.Options.DefaultSample && !sound.empty());
		bool playerHasSound = (playSound && (fire.SoundIndex >= 0 || _metadata->Sounds.find(String::nullTerminatedView(sound)) != _metadata->Sounds.end()));

		std::int32_t count = std::max<std::int32_t>(fire.ShotsPerFire, 1);
		for (std::int32_t i = 0; i < count; i++) {
			float offset = (count > 1 ? fire.SpreadAngle * (float(i) / float(count - 1) - 0.5f) : 0.0f);
			if (fire.RandomSpread > 0.0f) {
				offset += Random().NextFloat(-fire.RandomSpread, fire.RandomSpread);
			}
			// Without the player's own sound, the first shot plays it from its metadata
			std::uint8_t shotParams[3] = { _inventory.WeaponUpgrades[(std::int32_t)weaponType], std::uint8_t(weaponType),
				std::uint8_t(i == 0 && playSound && !playerHasSound && !fire.LoopSound ? 1 : 0) };
			std::shared_ptr<Weapons::CustomShot> shot = std::make_shared<Weapons::CustomShot>();
			shot->OnActivated(ActorActivationDetails(_levelHandler, initialPos, shotParams));
			shot->OnFire(shared_from_this(), gunspotPos, _speed, angle + offset, IsFacingLeft());
			_levelHandler->AddActor(shot);
		}

		if (playerHasSound) {
			if (fire.LoopSound) {
				StartWeaponSfx(weaponType, sound, 0.6f, 1.0f);
			} else {
				PlayPlayerSfx(sound);
			}
		}

		std::int32_t fastFire = (_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		_weaponCooldown = std::max(fire.Cooldown - fastFire * fire.FastFireStep, 1.0f);
		if (definition.Options.Style == WeaponStyle::Popcorn) {
			_weaponCooldown = std::max(_weaponCooldown, fire.Cooldown * 0.5f);
		}

		if (fire.Recoil != 0.0f) {
			AddExternalForce(IsFacingLeft() ? fire.Recoil : -fire.Recoil, 0.0f);
		}
		if (fire.Flare) {
			EmitWeaponFlare();
		}

		ammoDecrease = fire.AmmoPerShot;
		return true;
	}

	void Player::EmitWeaponFlare()
	{
		_weaponFlareFrame = (Random().Next() & 0xFFFF);
		_weaponFlareTime = 6.0f;
	}

	void Player::SetCurrentWeapon(WeaponType weaponType, SetCurrentWeaponReason reason)
	{
		// Handle only local sessions here, online sessions are handled in derived classes
		if (reason == SetCurrentWeaponReason::AddAmmo && !PreferencesCache::SwitchToNewWeapon && _levelHandler->IsLocalSession()) {
			return;
		}

		_currentWeapon = weaponType;
	}

	bool Player::SetShootingAnimation()
	{
		// The muzzle is read off the current pose, so a pose without a shooting variant would fire from nowhere
		// Drop the action bits, most specific first, until a shooting variant with a gunspot is found
		static const AnimState Droppable[] = {
			AnimState::Uppercut, AnimState::Buttstomp, AnimState::Spring, AnimState::Lift, AnimState::Dash,
			AnimState::Run, AnimState::Walk, AnimState::Jump, AnimState::Fall, AnimState::Crouch
		};

		auto hasMuzzle = [this](AnimState state) -> bool {
			auto* anim = _metadata->FindAnimation(state);
			return (anim != nullptr && anim->Base != nullptr &&
				anim->Base->Gunspot != Vector2i(ContentResolver::InvalidValue, ContentResolver::InvalidValue));
		};

		AnimState state = _currentAnimation->State;
		// The original shows the shooting pose instead of the uppercut
		if (!_levelHandler->IsReforged() && _currentSpecialMove == SpecialMoveType::Uppercut) {
			state &= ~AnimState::Uppercut;
		}
		if (SetAnimation(state | AnimState::Shoot)) {
			return true;
		}

		// A pose that already has a muzzle is kept (e.g. Lori's kick re-applies its pose every frame)
		Vector2i muzzle;
		if (GetGunspotOffset(muzzle)) {
			return false;
		}

		AnimState withoutMuzzle = AnimState::Idle;
		bool hasWithoutMuzzle = false;
		for (std::int32_t i = 0; ; i++) {
			AnimState candidate = state | AnimState::Shoot;
			if (hasMuzzle(candidate)) {
				return SetAnimation(candidate);
			}
			if (!hasWithoutMuzzle && _metadata->FindAnimation(candidate) != nullptr) {
				withoutMuzzle = candidate;
				hasWithoutMuzzle = true;
			}
			if (i >= (std::int32_t)arraySize(Droppable)) {
				break;
			}
			state &= ~Droppable[i];
		}
		if (hasWithoutMuzzle) {
			return SetAnimation(withoutMuzzle);
		}
		return SetAnimation(AnimState::Shoot);
	}

	bool Player::GetGunspotOffset(Vector2i& offset) const
	{
		// Some poses have no gunspot, fire from the hotspot instead of an InvalidValue offset
		const auto* base = _currentAnimation->Base;
		if (base->Gunspot == Vector2i(ContentResolver::InvalidValue, ContentResolver::InvalidValue)) {
			offset = Vector2i();
			return false;
		}
		offset = base->Hotspot - base->Gunspot;
		return true;
	}

	void Player::GetFirePointAndAngle(Vector3i& initialPos, Vector2f& gunspotPos, float& angle)
	{
		if (ShouldCancelTransitionOnFire()) {
			ForceCancelTransition();
		}

		SetShootingAnimation();

		initialPos = Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 2);
		gunspotPos = _pos;

		Vector2i gunspotOffset;
		GetGunspotOffset(gunspotOffset);

		if (_inWater) {
			angle = _renderer.GetRotation();

			std::int32_t size = (_currentAnimation->Base->FrameDimensions.X / 2);
			float sinAngle, cosAngle;
			sincosApprox(angle, sinAngle, cosAngle);
			gunspotPos.X += (cosAngle * size) * (IsFacingLeft() ? -1.0f : 1.0f);
			gunspotPos.Y += (sinAngle * size) * (IsFacingLeft() ? -1.0f : 1.0f) - gunspotOffset.Y;
		} else {
			gunspotPos.X += gunspotOffset.X * (IsFacingLeft() ? 1 : -1);
			gunspotPos.Y -= gunspotOffset.Y;

			if ((_currentAnimation->State & AnimState::Lookup) == AnimState::Lookup) {
				initialPos.X = (std::int32_t)gunspotPos.X;
				angle = (IsFacingLeft() ? fRadAngle90 : fRadAngle270);
			} else {
				initialPos.Y = (std::int32_t)gunspotPos.Y;
				angle = 0.0f;
			}
		}
	}

	bool Player::OnLevelChanging(Actors::ActorBase* initiator, ExitType exitType)
	{
		// A spectator isn't simulated, so it could never finish the transitions below
		if DEATH_UNLIKELY(_playerType == PlayerType::Spectate) {
			_levelExiting = LevelExitingState::Ready;
			return true;
		}

		// Deactivate any shield
		if (_activeShieldTime > 70.0f) {
			_activeShieldTime = 70.0f;
		}

		if (_spawnedBird != nullptr) {
			_spawnedBird->FlyAway();
			_spawnedBird = nullptr;
		}

		switch (_levelExiting) {
			case LevelExitingState::Waiting: {
				// A bird never lands, so it ends the level once it hovers
				if ((CanJump() || IsBird()) && std::abs(_speed.X) < 1.0f && std::abs(_speed.Y) < 1.0f) {
					_levelExiting = LevelExitingState::Transition;

					ForceCancelTransition();

					if (!SetPlayerTransition(AnimState::TransitionEndOfLevel, false, true, SpecialMoveType::None, [this]() {
						_renderer.SetDrawEnabled(false);
						_levelExiting = LevelExitingState::Ready;
					})) {
						// The sheet couldn't be loaded and the callback already ran, keep the player visible
						_renderer.SetDrawEnabled(true);
					}
					PlayPlayerSfx("EndOfLevel1"_s, 1.0f / _levelHandler->GetPlayers().size());

					SetState(ActorState::ApplyGravitation, false);
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					_externalForce.X = 0.0f;
					_externalForce.Y = 0.0f;
					_internalForceY = 0.0f;
				} else if (_lastPoleTime <= 0.0f) {
					// Waiting timeout - use warp transition instead
					_levelExiting = LevelExitingState::Transition;

					ForceCancelTransition();

					SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpInFreefall : AnimState::TransitionWarpIn, false, true, SpecialMoveType::None, [this]() {
						_renderer.SetDrawEnabled(false);
						_levelExiting = LevelExitingState::Ready;
					});
					PlayPlayerSfx("WarpIn"_s, 1.0f / _levelHandler->GetPlayers().size());
					_levelHandler->PlayerExecuteRumble(this, "Warp"_s);

					SetState(ActorState::ApplyGravitation, false);
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					_externalForce.X = 0.0f;
					_externalForce.Y = 0.0f;
					_internalForceY = 0.0f;
				} else {
					// Refresh animation state, because UpdateAnimation() is not called when _controllable is false
					AnimState oldState = _currentAnimation->State;
					AnimState newState = (oldState & CompositeAnimMask);
					if (std::abs(_speed.X) > std::numeric_limits<float>::epsilon()) {
						newState |= AnimState::Walk;
					}
					if (!CanJump()) {
						if (_speed.Y < 0.0f) {
							newState |= AnimState::Jump;
						} else {
							newState |= AnimState::Fall;
						}
					}
					SetAnimation(newState);
					if ((oldState == AnimState::Fall || oldState == AnimState::Freefall) && newState == AnimState::Idle) {
						SetTransition(AnimState::TransitionFallToIdle, true);
					}
				}
				return false;
			}

			case LevelExitingState::WaitingForWarp: {
				if (_lastPoleTime <= 0.0f) {
					_levelExiting = LevelExitingState::Transition;

					ForceCancelTransition();

					SetPlayerTransition(_isFreefall || _inWater ? AnimState::TransitionWarpInFreefall : AnimState::TransitionWarpIn, false, true, SpecialMoveType::None, [this]() {
						_renderer.SetDrawEnabled(false);
						_levelExiting = LevelExitingState::Ready;
					});
					PlayPlayerSfx("WarpIn"_s, 1.0f / _levelHandler->GetPlayers().size());
					_levelHandler->PlayerExecuteRumble(this, "Warp"_s);

					SetState(ActorState::ApplyGravitation, false);
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					_externalForce.X = 0.0f;
					_externalForce.Y = 0.0f;
					_internalForceY = 0.0f;
				} else {
					// Refresh animation state, because UpdateAnimation() is not called when _controllable is false
					AnimState oldState = _currentAnimation->State;
					AnimState newState = (oldState & CompositeAnimMask);
					if (std::abs(_speed.X) > std::numeric_limits<float>::epsilon()) {
						newState |= AnimState::Walk;
					}
					if (!CanJump()) {
						if (_speed.Y < 0.0f) {
							newState |= AnimState::Jump;
						} else {
							newState |= AnimState::Fall;
						}
					}
					SetAnimation(newState);
					if ((oldState == AnimState::Fall || oldState == AnimState::Freefall) && newState == AnimState::Idle) {
						SetTransition(AnimState::TransitionFallToIdle, true);
					}
				}
				return false;
			}

			case LevelExitingState::Transition:
				return false;

			case LevelExitingState::Ready:
				return true;
		}

		if (_health <= 0) {
			// Player is dead, just skip the transition
			_levelExiting = LevelExitingState::Ready;
			return true;
		}

		if (_suspendType != SuspendType::None) {
			MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative | MoveType::Force);
			_suspendType = SuspendType::None;
			_suspendTime = 60.0f;
		}

		_controllable = false;
		SetState(ActorState::IsInvulnerable | ActorState::ApplyGravitation, true);
		_fireFramesLeft = 0.0f;
		_copterFramesLeft = 0.0f;
		_pushFramesLeft = 0.0f;
		_invulnerableTime = 0.0f;

		if (_sugarRushLeft > 1.0f) {
			_sugarRushLeft = 1.0f;
		}

		_renderer.SetDrawEnabled(true);

		ExitType exitTypeMasked = (exitType & ExitType::TypeMask);
		if ((exitType & ExitType::FastTransition) == ExitType::FastTransition) {
			if (exitTypeMasked == ExitType::Warp || exitTypeMasked == ExitType::Bonus || exitTypeMasked == ExitType::Boss) {
				_levelExiting = LevelExitingState::WaitingForWarp;

				// Re-used for waiting timeout
				_lastPoleTime = 0.0f;
				return false;
			} else {
				_levelExiting = LevelExitingState::Ready;
				return true;
			}
		} else {
			if (initiator == this || (initiator == nullptr && _playerIndex == 0)) {
				PlayPlayerSfx("EndOfLevel"_s);
			}

			if (exitTypeMasked == ExitType::Warp || exitTypeMasked == ExitType::Bonus || exitTypeMasked == ExitType::Boss || _inWater) {
				_levelExiting = LevelExitingState::WaitingForWarp;

				// Re-used for waiting timeout
				_lastPoleTime = 100.0f;
			} else {
				_levelExiting = LevelExitingState::Waiting;
				SetFacingLeft(false);

				// Re-used for waiting timeout
				_lastPoleTime = 300.0f;
			}
			return false;
		}
	}

	void Player::ReceiveLevelCarryOver(ExitType exitType, const PlayerCarryOver& carryOver)
	{
		_lives = (std::int32_t)carryOver.Lives;
		_score = carryOver.Score;
		_inventory.FoodEaten = (std::int32_t)carryOver.FoodEaten;
		_inventoryCheckpoint.FoodEaten = _inventory.FoodEaten;
		_currentWeapon = carryOver.CurrentWeapon;

		std::memcpy(_gemsTotal, carryOver.Gems, sizeof(_gemsTotal));
		std::memcpy(_inventory.WeaponAmmo, carryOver.Ammo, sizeof(_inventory.WeaponAmmo));
		std::memcpy(_inventoryCheckpoint.WeaponAmmo, carryOver.Ammo, sizeof(_inventoryCheckpoint.WeaponAmmo));
		std::memcpy(_inventory.WeaponUpgrades, carryOver.WeaponUpgrades, sizeof(_inventory.WeaponUpgrades));
		std::memcpy(_inventoryCheckpoint.WeaponUpgrades, carryOver.WeaponUpgrades, sizeof(_inventoryCheckpoint.WeaponUpgrades));

		ApplyLevelWeaponRules();

		ExitType exitTypeMasked = (exitType & ExitType::TypeMask);
		if (exitTypeMasked == ExitType::Warp || exitTypeMasked == ExitType::Bonus || exitTypeMasked == ExitType::Boss) {
			// Use delayed spawning
			SetState(ActorState::ApplyGravitation, false);
			_renderer.SetDrawEnabled(false);
			_lastExitType = exitType;
			_controllable = false;
			_controllableTimeout = ((exitType & ExitType::FastTransition) == ExitType::FastTransition ? 5.0f : 55.0f);
		} else if ((exitType & ExitType::Frozen) == ExitType::Frozen) {
			// Use instant spawning
			Freeze(100.0f);
		}

		// Preload all weapons
		for (std::int32_t i = 0; i < std::int32_t(arraySize(_inventory.WeaponAmmo)); i++) {
			if (_inventory.WeaponAmmo[i] != 0) {
				const WeaponDefinition& definition = _levelHandler->GetWeaponDefinition(WeaponType(i));
				String path = (definition.Kind == WeaponKind::Custom ? _levelHandler->GetWeapons().GetSpriteMetadataPath(definition.GetCustom().Shot.Sprite) : String());
				PreloadMetadataAsync(path.empty() ? String("Weapon/"_s + WeaponNames[i]) : path);
			}
		}
	}

	std::uint16_t Player::GetAmmoLimit(const WeaponOptions& options)
	{
		std::int32_t limit = options.GetMaximumAmmo() * std::max<std::int32_t>(options.Multiplier, 1) * 256;
		return std::uint16_t(std::min<std::int32_t>(limit, UINT16_MAX - 1));
	}

	void Player::ApplyLevelWeaponRules()
	{
		for (std::int32_t i = 0; i < std::int32_t(WeaponType::Count); i++) {
			const WeaponDefinition& definition = _levelHandler->GetWeaponDefinition(WeaponType(i));
			std::uint16_t limit = GetAmmoLimit(definition.Options);
			std::int32_t startAmmo = definition.StartAmmo * std::max<std::int32_t>(definition.Options.Multiplier, 1) * 256;
			for (InventoryState* inventory : { &_inventory, &_inventoryCheckpoint }) {
				std::uint16_t& ammo = inventory->WeaponAmmo[i];
				if (!definition.Options.AllowedPowerup || !definition.Options.Allowed) {
					inventory->WeaponUpgrades[i] &= ~0x01;
				}
				if (!definition.Options.Allowed) {
					ammo = 0;
				} else if (definition.Options.Infinite) {
					ammo = UINT16_MAX;
				} else if (ammo == UINT16_MAX || definition.Options.Replenishes) {
					ammo = limit;
				} else {
					ammo = std::uint16_t(std::clamp<std::int32_t>(std::max<std::int32_t>(ammo, startAmmo), 0, limit));
				}
			}
		}

		if (_inventory.WeaponAmmo[(std::int32_t)_currentWeapon] == 0) {
			for (std::int32_t i = 0; i < std::int32_t(WeaponType::Count); i++) {
				if (_inventory.WeaponAmmo[i] != 0) {
					_currentWeapon = WeaponType(i);
					break;
				}
			}
		}
	}

	PlayerCarryOver Player::PrepareLevelCarryOver()
	{
		PlayerCarryOver carryOver;
		carryOver.Type = GetPersistentPlayerType();
		carryOver.Lives = (_lives > UINT8_MAX ? UINT8_MAX : (std::uint8_t)_lives);
		carryOver.Score = _score;
		carryOver.FoodEaten = (_inventory.FoodEaten > UINT8_MAX ? UINT8_MAX : (std::uint8_t)_inventory.FoodEaten);
		carryOver.CurrentWeapon = _currentWeapon;

		for (std::size_t i = 0; i < arraySize(carryOver.Gems); i++) {
			carryOver.Gems[i] = _gemsTotal[i] + _inventory.Gems[i];
		}

		std::memcpy(carryOver.Ammo, _inventory.WeaponAmmo, sizeof(_inventory.WeaponAmmo));
		std::memcpy(carryOver.WeaponUpgrades, _inventory.WeaponUpgrades, sizeof(_inventory.WeaponUpgrades));

		return carryOver;
	}

	void Player::ReceiveReturnCarryOver(ExitType exitType, const PlayerCarryOver& carryOver)
	{
		ReceiveLevelCarryOver(exitType, carryOver);

		// Gems of this level are already in the carried-over total, don't count them twice
		for (std::size_t i = 0; i < arraySize(_gemsTotal); i++) {
			_gemsTotal[i] = std::max<std::int32_t>(carryOver.Gems[i] - _inventory.Gems[i], 0);
		}

		if (carryOver.Type != PlayerType::None && carryOver.Type != _playerType) {
			MorphToInstantly(carryOver.Type);
		}

		// Stop the speed the player ran into the exit with, as a warp does
		_speed = Vector2f::Zero;
		_externalForce = Vector2f::Zero;
		_internalForceY = 0.0f;

		_levelExitSuppressed = true;
	}

	namespace
	{
		void WriteInventoryState(Stream& dest, const Player::InventoryState& inventory)
		{
			dest.WriteVariableInt32(inventory.Coins);
			dest.WriteVariableInt32(inventory.FoodEaten);
			for (std::int32_t gems : inventory.Gems) {
				dest.WriteVariableInt32(gems);
			}
			dest.WriteVariableInt32((std::int32_t)WeaponType::Count);
			for (std::int32_t i = 0; i < (std::int32_t)WeaponType::Count; i++) {
				dest.WriteValueAsLE<std::uint16_t>(inventory.WeaponAmmo[i]);
				dest.WriteValue<std::uint8_t>(inventory.WeaponUpgrades[i]);
			}
		}

		void ReadInventoryState(Stream& src, Player::InventoryState& inventory)
		{
			inventory.Coins = src.ReadVariableInt32();
			inventory.FoodEaten = src.ReadVariableInt32();
			for (std::int32_t& gems : inventory.Gems) {
				gems = src.ReadVariableInt32();
			}
			std::int32_t weaponCount = src.ReadVariableInt32();
			for (std::int32_t i = 0; i < weaponCount; i++) {
				std::uint16_t ammo = src.ReadValueAsLE<std::uint16_t>();
				std::uint8_t upgrades = src.ReadValue<std::uint8_t>();
				if (i < (std::int32_t)WeaponType::Count) {
					inventory.WeaponAmmo[i] = ammo;
					inventory.WeaponUpgrades[i] = upgrades;
				}
			}
		}
	}

	void Player::OnSerializeState(Stream& dest)
	{
		// Only the persistent state, temporary effects end with the level anyway
		ActorBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>((std::uint8_t)GetPersistentPlayerType());
		dest.WriteValue<std::uint8_t>((std::uint8_t)_playerTypeOriginal);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_checkpointPlayerType);
		dest.WriteVariableInt32(_lives);
		dest.WriteVariableInt32(_score);
		WriteInventoryState(dest, _inventory);
		WriteInventoryState(dest, _inventoryCheckpoint);
		for (std::int32_t gems : _gemsTotal) {
			dest.WriteVariableInt32(gems);
		}
		dest.WriteValueAsLE<float>(_checkpointPos.X);
		dest.WriteValueAsLE<float>(_checkpointPos.Y);
		dest.WriteValueAsLE<float>(_checkpointLight);
		dest.WriteValueAsLE<float>(_currentAmbientLight);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_currentWeapon);
		// Goes last, so older builds can skip it and older states end without it
		dest.WriteValue<std::uint8_t>((std::uint8_t)GetPlayerTypeBeforeMorph());
	}

	void Player::OnDeserializeState(Stream& src)
	{
		ActorBase::OnDeserializeState(src);

		// Invulnerability is not restored, the flag would stay set forever without the time that clears it
		SetState(ActorState::IsInvulnerable, false);

		PlayerType playerType = (PlayerType)src.ReadValue<std::uint8_t>();
		_playerTypeOriginal = (PlayerType)src.ReadValue<std::uint8_t>();
		_checkpointPlayerType = (PlayerType)src.ReadValue<std::uint8_t>();
		MorphToInstantly(playerType);

		_lives = src.ReadVariableInt32();
		_score = src.ReadVariableInt32();
		ReadInventoryState(src, _inventory);
		ReadInventoryState(src, _inventoryCheckpoint);
		for (std::int32_t& gems : _gemsTotal) {
			gems = src.ReadVariableInt32();
		}
		_checkpointPos.X = src.ReadValueAsLE<float>();
		_checkpointPos.Y = src.ReadValueAsLE<float>();
		_checkpointLight = src.ReadValueAsLE<float>();
		float ambientLight = src.ReadValueAsLE<float>();
		_levelHandler->SetAmbientLight(this, ambientLight);

		WeaponType currentWeapon = (WeaponType)src.ReadValue<std::uint8_t>();
		if ((std::int32_t)currentWeapon >= (std::int32_t)WeaponType::Count || _inventory.WeaponAmmo[(std::int32_t)currentWeapon] == 0) {
			currentWeapon = WeaponType::Blaster;
		}
		_currentWeapon = currentWeapon;
		for (std::int32_t i = 0; i < std::int32_t(arraySize(_inventory.WeaponAmmo)); i++) {
			if (_inventory.WeaponAmmo[i] != 0) {
				PreloadMetadataAsync(String("Weapon/"_s + WeaponNames[i]));
			}
		}

		if (IsTemporaryForm(_playerType)) {
			// Restore the character before the morph, or fall back to the checkpoint or starting one
			auto isCharacter = [](PlayerType type) {
				return (type == PlayerType::Jazz || type == PlayerType::Spaz || type == PlayerType::Lori);
			};
			PlayerType beforeMorph = (src.GetPosition() < src.GetSize() ? (PlayerType)src.ReadValue<std::uint8_t>() : PlayerType::None);
			if (!isCharacter(beforeMorph)) {
				beforeMorph = (isCharacter(_checkpointPlayerType) ? _checkpointPlayerType : _playerTypeOriginal);
			}
			if (isCharacter(beforeMorph)) {
				_playerTypeBeforeMorph = beforeMorph;
			}
		}
	}

	void Player::InitializeFromStream(ILevelHandler* levelHandler, Stream& src, std::uint16_t version)
	{
		std::uint8_t playerIndex = src.ReadVariableInt32();
		PlayerType playerType = (PlayerType)src.ReadValue<std::uint8_t>();
		PlayerType playerTypeOriginal = (PlayerType)src.ReadValue<std::uint8_t>();
		float checkpointPosX = src.ReadValueAsLE<float>();
		float checkpointPosY = src.ReadValueAsLE<float>();

		std::uint8_t playerParams[2] = { (std::uint8_t)playerType, (std::uint8_t)playerIndex };
		OnActivated(Actors::ActorActivationDetails(
			levelHandler,
			Vector3i((std::int32_t)checkpointPosX, (std::int32_t)checkpointPosY, ILevelHandler::PlayerZ - playerIndex),
			playerParams
		));

		_playerTypeOriginal = playerTypeOriginal;

		_checkpointLight = src.ReadValueAsLE<float>();
		_lives = src.ReadVariableInt32();
		_inventory.Coins = src.ReadVariableInt32();
		_inventoryCheckpoint.Coins = _inventory.Coins;
		_inventory.FoodEaten = src.ReadVariableInt32();
		_inventoryCheckpoint.FoodEaten = _inventory.FoodEaten;
		_score = src.ReadVariableInt32();

		_inventory.Gems[0] = src.ReadVariableInt32();
		if (version >= 3) {
			// Gem types are split since v3.0.0
			_inventory.Gems[1] = src.ReadVariableInt32();
			_inventory.Gems[2] = src.ReadVariableInt32();
			_inventory.Gems[3] = src.ReadVariableInt32();

			_gemsTotal[0] = src.ReadVariableInt32();
			_gemsTotal[1] = src.ReadVariableInt32();
			_gemsTotal[2] = src.ReadVariableInt32();
			_gemsTotal[3] = src.ReadVariableInt32();
		}
		std::memcpy(_inventoryCheckpoint.Gems, _inventory.Gems, sizeof(_inventory.Gems));

		levelHandler->SetAmbientLight(this, _checkpointLight);

		std::int32_t weaponCount = src.ReadVariableInt32();
		DEATH_ASSERT(weaponCount == std::int32_t(arraySize(_inventoryCheckpoint.WeaponAmmo)), "Weapon count mismatch", );
		_currentWeapon = (WeaponType)src.ReadVariableInt32();
		src.Read(_inventoryCheckpoint.WeaponAmmo, sizeof(_inventoryCheckpoint.WeaponAmmo));
		src.Read(_inventoryCheckpoint.WeaponUpgrades, sizeof(_inventoryCheckpoint.WeaponUpgrades));

		std::memcpy(_inventory.WeaponAmmo, _inventoryCheckpoint.WeaponAmmo, sizeof(_inventoryCheckpoint.WeaponAmmo));
		std::memcpy(_inventory.WeaponUpgrades, _inventoryCheckpoint.WeaponUpgrades, sizeof(_inventoryCheckpoint.WeaponUpgrades));

		// Reset current weapon to Blaster if player has no ammo on checkpoint
		if (_inventory.WeaponAmmo[(std::int32_t)_currentWeapon] == 0) {
			SetCurrentWeapon(WeaponType::Blaster, SetCurrentWeaponReason::Rollback);
		}
	}

	void Player::SerializeResumableToStream(Stream& dest)
	{
		dest.WriteVariableInt32(_playerIndex);
		// The player is resumed at the checkpoint, so also with the type it had there
		dest.WriteValue<std::uint8_t>((std::uint8_t)_checkpointPlayerType);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_playerTypeOriginal);
		dest.WriteValueAsLE<float>(_checkpointPos.X);
		dest.WriteValueAsLE<float>(_checkpointPos.Y);
		dest.WriteValueAsLE<float>(_checkpointLight);
		dest.WriteVariableInt32(_lives);
		dest.WriteVariableInt32(_inventoryCheckpoint.Coins);
		dest.WriteVariableInt32(_inventoryCheckpoint.FoodEaten);
		dest.WriteVariableInt32(_score);
		dest.WriteVariableInt32(_inventoryCheckpoint.Gems[0]);
		dest.WriteVariableInt32(_inventoryCheckpoint.Gems[1]);
		dest.WriteVariableInt32(_inventoryCheckpoint.Gems[2]);
		dest.WriteVariableInt32(_inventoryCheckpoint.Gems[3]);
		dest.WriteVariableInt32(_gemsTotal[0]);
		dest.WriteVariableInt32(_gemsTotal[1]);
		dest.WriteVariableInt32(_gemsTotal[2]);
		dest.WriteVariableInt32(_gemsTotal[3]);
		dest.WriteVariableInt32(std::int32_t(arraySize(_inventoryCheckpoint.WeaponAmmo)));
		dest.WriteVariableInt32((std::int32_t)_currentWeapon);
		dest.Write(_inventoryCheckpoint.WeaponAmmo, sizeof(_inventoryCheckpoint.WeaponAmmo));
		dest.Write(_inventoryCheckpoint.WeaponUpgrades, sizeof(_inventoryCheckpoint.WeaponUpgrades));
	}

#if defined(WITH_EDITOR)
	void Player::TakeOverPlayTestState(const Player& other)
	{
		_lives = other._lives;
		_score = other._score;
		_health = std::clamp<std::int32_t>(other._health, 1, _maxHealth);
		_inventory = other._inventory;
		_inventoryCheckpoint = other._inventoryCheckpoint;
		std::memcpy(_gemsTotal, other._gemsTotal, sizeof(_gemsTotal));
		if (other._activeShield != ShieldType::None && other._activeShieldTime > 0.0f) {
			SetShield(other._activeShield, other._activeShieldTime);
		}
		// The weapon has to be one there's ammo for, as after a rollback
		WeaponType weapon = other._currentWeapon;
		if (_inventory.WeaponAmmo[(std::int32_t)weapon] == 0) {
			weapon = WeaponType::Blaster;
		}
		SetCurrentWeapon(weapon, SetCurrentWeaponReason::Rollback);
	}
#endif

	bool Player::Respawn(Vector2f pos)
	{
		if ((GetState() & (ActorState::IsInvulnerable | ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors)) != ActorState::IsInvulnerable) {
			return false;
		}

		_health = _maxHealth;

		MoveInstantly(pos, MoveType::Absolute | MoveType::Force);

		_controllable = true;
		_renderer.SetDrawEnabled(true);
		SetState(ActorState::IsInvulnerable, false);
		SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors, true);

		ResetLegacyMovementState();

		return true;
	}

	void Player::WarpToPosition(Vector2f pos, WarpFlags flags)
	{
		if ((flags & WarpFlags::Fast) == WarpFlags::Fast) {
			Vector2f posPrev = _pos;
			bool hideTrail = (posPrev - pos).Length() > 250.0f;
			MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
			if (hideTrail) {
				_trailLastPos = _pos;
			}
			_levelHandler->HandlePlayerWarped(this, posPrev, flags);
		} else {
			EndDamagingMove();
			SetState(ActorState::IsInvulnerable, true);
			SetState(ActorState::ApplyGravitation, false);

			SetAnimation(_currentAnimation->State & ~(AnimState::Uppercut | AnimState::Buttstomp));

			_speed.X = 0.0f;
			_speed.Y = 0.0f;
			_externalForce.X = 0.0f;
			_externalForce.Y = 0.0f;
			_internalForceY = 0.0f;
			_fireFramesLeft = 0.0f;
			// Measured in the original: a warp doesn't end the copter outside Reforged
			if (_levelHandler->IsReforged()) {
				_copterFramesLeft = 0.0f;
			}
			_pushFramesLeft = 0.0f;

			ResetLegacyMovementState();

			// For warping from the water
			_renderer.SetRotation(0.0f);

			if ((flags & WarpFlags::SkipWarpIn) == WarpFlags::SkipWarpIn) {
				DoWarpOut(pos, flags);
			} else {
				PlayPlayerSfx("WarpIn"_s);
				_levelHandler->PlayerExecuteRumble(this, "Warp"_s);

				SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpInFreefall : AnimState::TransitionWarpIn, false, true, SpecialMoveType::None, [this, pos, flags]() {
					DoWarpOut(pos, flags);
				});
			}
		}
	}

	void Player::DoWarpOut(Vector2f pos, WarpFlags flags)
	{
		Vector2f posPrev = _pos;
		MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
		_trailLastPos = _pos;
		PlayPlayerSfx("WarpOut"_s);
		_levelHandler->PlayerExecuteRumble(this, "Warp"_s);

		_levelHandler->HandlePlayerWarped(this, posPrev, flags);

		_isFreefall |= CanFreefall();

		SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpOutFreefall : AnimState::TransitionWarpOut, false, true, SpecialMoveType::None, [this, flags]() {
			SetState(ActorState::IsInvulnerable, false);
			// Don't re-enable gravity if any modifier is active or a copter survived the warp
			if (_activeModifier == Modifier::None && _copterFramesLeft <= 0.0f) {
				SetState(ActorState::ApplyGravitation, true);
			}

			if ((flags & WarpFlags::Freeze) == WarpFlags::Freeze) {
				Freeze(100.0f);
			} else {
				_controllable = true;
				// UpdateAnimation() was probably skipped in this step, because _controllable was false, so call it here
				UpdateAnimation(0.0f);
			}
		});

		// Outside Reforged, control returns shortly after the warp-out instead of waiting for the animation
		// Must come after SetPlayerTransition(), which resets _controllableTimeout
		if (!_levelHandler->IsReforged() && (flags & WarpFlags::Freeze) != WarpFlags::Freeze) {
			SetState(ActorState::IsInvulnerable, false);
			// Not with a modifier or a copter that survived the warp, the copter owns the descent
			if (_activeModifier == Modifier::None && _copterFramesLeft <= 0.0f) {
				SetState(ActorState::ApplyGravitation, true);
			}
			_controllableTimeout = LegacyWarpOutControlTime;
		}
	}

	void Player::WarpToCheckpoint()
	{
		WarpToPosition(_checkpointPos, WarpFlags::SkipWarpIn);
		_levelHandler->SetAmbientLight(this, _checkpointLight);
	}

	bool Player::InitialPoleStage(bool horizontal, Vector2f eventPos)
	{
		if (_isAttachedToPole || _playerType == PlayerType::Frog || IsBird() ||
			_activeModifier == Modifier::Copter || _activeModifier == Modifier::Airboard) {
			return false;
		}

		// Use the tile the event was found in, not the one the frame ended in
		std::int32_t x = (std::int32_t)eventPos.X / Tiles::TileSet::DefaultTileSize;
		std::int32_t y = (std::int32_t)eventPos.Y / Tiles::TileSet::DefaultTileSize;

		// Only the exact tile just used is excluded, adjacent poles chain as in the original
		if (_lastPoleTime > 0.0f && x == _lastPolePos.X && y == _lastPolePos.Y) {
			return false;
		}

		_lastPoleTime = 80.0f;
		_lastPolePos = Vector2i(x, y);

		float activeForce, lastSpeed;
		if (horizontal) {
			activeForce = (std::abs(_externalForce.X) > 1.0f ? _externalForce.X : _speed.X);
			lastSpeed = _speed.X;
		} else {
			activeForce = _speed.Y;
			lastSpeed = _speed.Y;
		}
		// Measured in the original: a horizontal pole entered with zero speed launches left
		bool positive = (horizontal && !_levelHandler->IsReforged() ? activeForce > 0.0f : activeForce >= 0.0f);

		float tx = static_cast<float>(x * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2);
		float ty = static_cast<float>(y * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2);

		if (_levelHandler->IsReforged()) {
			auto* events = _levelHandler->EventMap();
			std::uint8_t* p;
			if (horizontal) {
				if (events->GetEventByPosition(x, (eventPos.Y < ty ? y - 1 : y + 1), &p) == EventType::ModifierHPole) {
					ty = eventPos.Y;
				}
			} else {
				if (events->GetEventByPosition((eventPos.X < tx ? x - 1 : x + 1), y, &p) == EventType::ModifierVPole) {
					tx = eventPos.X;
				}
			}
		}

		MoveInstantly(Vector2f(tx, ty), MoveType::Absolute | MoveType::Force);
		OnUpdateHitbox();

		_speed.X = 0.0f;
		_speed.Y = 0.0f;
		_externalForce.X = 0.0f;
		_externalForce.Y = 0.0f;
		_internalForceY = 0.0f;
		SetState(ActorState::ApplyGravitation, false);
		_renderer.SetRotation(0.0f);
		_isAttachedToPole = true;
		// Remembered now, _isSpring is cleared long before the launch
		_poleEnteredOnSpring = _isSpring;
		if (_inIdleTransition) {
			_inIdleTransition = false;
			CancelTransition();
		}

		_keepRunningTime = 0.0f;
		_pushFramesLeft = 0.0f;
		_fireFramesLeft = 0.0f;
		_copterFramesLeft = 0.0f;

		SetAnimation(_currentAnimation->State & ~(AnimState::Uppercut /*| AnimState::Sidekick*/ | AnimState::Buttstomp));

		AnimState poleAnim = (horizontal ? AnimState::TransitionPoleHSlow : AnimState::TransitionPoleVSlow);
		SetPlayerTransition(poleAnim, false, true, SpecialMoveType::None, [this, horizontal, positive, lastSpeed]() {
			NextPoleStage(horizontal, positive, 2, lastSpeed);
		});

		_controllableTimeout = 80.0f;

		PlayPlayerSfx("Pole"_s, 0.8f, 0.6f);
		return true;
	}

	void Player::NextPoleStage(bool horizontal, bool positive, std::int32_t stagesLeft, float lastSpeed)
	{
		if (_inIdleTransition) {
			_inIdleTransition = false;
			CancelTransition();
		}

		if (stagesLeft > 0) {
			AnimState poleAnim = (horizontal ? AnimState::TransitionPoleH : AnimState::TransitionPoleV);
			SetPlayerTransition(poleAnim, false, true, SpecialMoveType::None, [this, horizontal, positive, stagesLeft, lastSpeed]() {
				NextPoleStage(horizontal, positive, stagesLeft - 1, lastSpeed);
			});

			_controllableTimeout = 80.0f;

			PlayPlayerSfx("Pole"_s, 1.0f, 0.6f);
		} else {
			std::int32_t sign = (positive ? 1 : -1);
			if (horizontal) {
				// To prevent stucking
				for (std::int32_t i = -1; i > -6; i--) {
					if (MoveInstantly(Vector2f(_speed.X, (float)i), MoveType::Relative)) {
						break;
					}
				}

				if (_levelHandler->IsReforged()) {
					_speed.X = 10 * sign + lastSpeed * 0.2f;
					_externalForce.X = 10.0f * sign;
				} else {
					// Measured in the original: entry speed plus a fixed bonus, capped
					_speed.X = std::min(std::abs(lastSpeed) + LegacyHPoleLaunchBonus, LegacyHPoleMaxLaunch) * sign;
					_externalForce.X = 0.0f;
					// Unlike other carries, this one is travelled in full, not held to the cap
					_hPoleCarry = true;
				}
				SetFacingLeft(!positive);

				_keepRunningTime = HPoleKeepRunningTime;
				// A pole's carry blocks the crouch only for its exit pose, unlike a spring's
				_springCarry = false;
				// Nor is it a run-in-place carry
				_revUpCarry = false;

				SetPlayerTransition(AnimState::Dash | AnimState::Jump, true, true, SpecialMoveType::None);
			} else {
				MoveInstantly(Vector2f(0.0f, sign * 16.0f), MoveType::Relative | MoveType::Force);

				// Non-Reforged: stronger vertical launch so spring->pole chains gain ~50% more height
				if (_levelHandler->IsReforged()) {
					_speed.Y = 4.0f * sign + lastSpeed * 1.4f;
					_externalForce.Y = 1.3f * sign;
				} else {
					// Measured in the original: entry speed plus a fixed bonus
					_speed.Y = (LegacyPoleLaunchBonus + std::abs(lastSpeed)) * sign;
					_externalForce.Y = 0.0f;
					// Measured in the original: the ascent decays at the released-jump rate even with jump held,
					// except when the pole was entered off a spring
					_jumpReleased = !_poleEnteredOnSpring;
					_riseOwnedByJump = false;
				}
			}

			SetState(ActorState::ApplyGravitation, true);
			_isAttachedToPole = false;
			_wasActivelyPushing = false;

			_controllableTimeout = PoleLaunchControlTimeout;
			_lastPoleTime = 10.0f;

			PlayPlayerSfx("HookAttach"_s, 0.8f, 1.2f);
		}
	}

	Player::Modifier Player::GetModifier() const
	{
		return _activeModifier;
	}

	bool Player::IsFlyCheatActive() const
	{
		return _flyCheatActive;
	}

	void Player::SetCopterFlight(float timeLeft, FlightType type)
	{
		if (timeLeft <= 0.0f) {
			_copterFramesLeft = 0.0f;
			_flyCheatActive = false;
			_copterCarried = false;
			_copterLandGraceLeft = 0.0f;
			return;
		}

		_copterFramesLeft = timeLeft;
		_flyCheatActive = (type == FlightType::Cheat);
	}

	void Player::EnableFlyCheat(bool active)
	{
		if (active) {
			_flyCheatActive = true;
		} else {
			SetCopterFlight(0.0f);
		}
	}

	bool Player::SetModifier(Modifier modifier, const std::shared_ptr<ActorBase>& decor)
	{
		if (_activeModifier == modifier) {
			return false;
		}
		// A bird can't use an airboard or a copter, the pickup stays for someone else
		if (IsBird() && modifier != Modifier::None) {
			return false;
		}

		if (_activeModifierDecor != nullptr) {
			_activeModifierDecor->OnDetach(this);
			_activeModifierDecor = nullptr;
		}

		switch (modifier) {
			case Modifier::Airboard: {
				_controllable = true;
				EndDamagingMove();
				SetState(ActorState::ApplyGravitation, false);

				_speed.Y = 0.0f;
				_externalForce.Y = 0.0f;
				_internalForceY = 0.0f;

				_activeModifier = Modifier::Airboard;

#if defined(WITH_AUDIO)
			if (_airboardSound == nullptr) {
				_airboardSound = PlaySfx("Airboard"_s, 0.6f, 1.0f);
				if (_airboardSound != nullptr) {
					_airboardSound->SetLooping(true);
				}
			}
#endif

				MoveInstantly(Vector2f(0.0f, -16.0f), MoveType::Relative);
				break;
			}
			case Modifier::Copter: {
				_controllable = true;
				EndDamagingMove();
				SetState(ActorState::ApplyGravitation, false);

				_speed.Y = 0.0f;
				_externalForce.Y = 0.0f;
				_internalForceY = 0.0f;

				_activeModifier = Modifier::Copter;

				// Ten seconds in Reforged, the original flies until a Fly Off area or a death
				SetCopterFlight(_flyCheatActive ? 1e6f
						: (_levelHandler->IsReforged() ? (10.0f * FrameTimer::FramesPerSecond) : LegacyFlyDuration),
					_flyCheatActive ? FlightType::Cheat : FlightType::Normal);

#if defined(WITH_AUDIO)
				if (_copterSound == nullptr) {
					_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
					if (_copterSound != nullptr) {
						_copterSound->SetLooping(true);
					}
				}
#endif
				break;
			}
			case Modifier::LizardCopter: {
				_controllable = true;
				EndDamagingMove();
				SetState(ActorState::ApplyGravitation, false);

				_speed.Y = 0.0f;
				_externalForce.Y = 0.0f;
				_internalForceY = 0.0f;

				_activeModifier = Modifier::LizardCopter;
				// An MP client may not know the copter yet, it flies without it
				_activeModifierDecor = decor;
				if (_activeModifierDecor != nullptr) {
					_activeModifierDecor->OnDetach(this);
				}


				SetCopterFlight(_levelHandler->IsReforged()
					? 3.0f * FrameTimer::FramesPerSecond
					: LegacyLizardCopterDuration, FlightType::Normal);
				break;
			}

			default: {
				SetState(ActorState::CollideWithTileset | ActorState::CollideWithTilesetReduced, true);
				SetCopterFlight(0.0f);
				_activeModifier = Modifier::None;

#if defined(WITH_AUDIO)
				if (_copterSound != nullptr) {
					_copterSound->Stop();
					_copterSound = nullptr;
				}
				if (_airboardSound != nullptr) {
					_airboardSound->Stop();
					_airboardSound = nullptr;
				}
#endif

				SetState(ActorState::CanJump | ActorState::ApplyGravitation, true);

				SetAnimation(AnimState::Fall);
				break;
			}
		}

		return true;
	}

	bool Player::TakeDamage(std::int32_t amount, float pushForce, bool ignoreInvulnerable)
	{
		if (amount <= 0 || _health <= 0 || (!ignoreInvulnerable && GetState(ActorState::IsInvulnerable)) || _levelExiting != LevelExitingState::None) {
			return false;
		}

		// Cancel active climbing and copter
		if (_currentTransition != nullptr && _currentTransition->State == AnimState::TransitionLedgeClimb) {
			ForceCancelTransition();
			MoveInstantly(Vector2f(IsFacingLeft() ? 6.0f : -6.0f, 0.0f), MoveType::Relative | MoveType::Force);
		} else if ((_activeModifier == Modifier::Copter || _activeModifier == Modifier::LizardCopter) && !IsFlyCheatActive()) {
			SetModifier(Modifier::None);
		}

		if (_spawnedBird != nullptr) {
			_spawnedBird->FlyAway();
			_spawnedBird = nullptr;
			// Bird acts as extra life
			_health++;
		}

		DecreaseHealth(amount);

		_speed.X = 0.0f;
		_internalForceY = 0.0f;
		_fireFramesLeft = 0.0f;
		// Being hit ends a rev-up launch or wind-up, as running into a wall does
		_keepRunningTime = 0.0f;
		if (_dashGraceLeft > LegacyDashGraceTicks) {
			_dashGraceLeft = 0.0f;
		}
		CancelRevUp();
		if (!IsFlyCheatActive()) {
			_copterFramesLeft = 0.0f;
		}
		_pushFramesLeft = 0.0f;
		SetState(ActorState::CanJump, false);
		_isAttachedToPole = false;

		if (_health > 0) {
			_externalForce.X = pushForce;

			if (IsBird()) {
				// Measured in the original: the bird is kept and thrown back and up, losing a few feathers
				float direction = (pushForce != 0.0f ? (pushForce < 0.0f ? -1.0f : 1.0f) : (IsFacingLeft() ? 1.0f : -1.0f));
				float maxSpeed = GetBirdMaxSpeed();
				_externalForce.X = 0.0f;
				_speed.X = std::min(BirdHurtKick, maxSpeed) * direction;
				_speed.Y = std::max(_speed.Y - BirdHurtKick, -maxSpeed);
				_birdAccelX = 0.0f;
				CreateSpriteDebris(BirdFeathers, 8, 0.05f);
			} else if (!_inWater && _activeModifier == Modifier::None) {
				_speed.Y = -6.5f;

				SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects, true);
				SetAnimation(AnimState::Idle);
			}

			SetPlayerTransition(AnimState::Hurt, false, true, SpecialMoveType::None, [this]() {
				_controllable = true;
			});

			float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
			GrantInvulnerability(invulnerableTime, InvulnerableType::Blinking);
			PlayPlayerSfx("Hurt"_s);
			_levelHandler->PlayerExecuteRumble(this, "Hurt"_s);
		} else {
			_externalForce.X = 0.0f;
			_speed.Y = 0.0f;

			PlayPlayerSfx("Die"_s, 1.3f);
		}

		return true;
	}

	bool Player::Freeze(float timeLeft)
	{
		if (_currentTransition != nullptr && _currentTransition->State == AnimState::TransitionDeath) {
			// Don't allow freezing during death transition
			return false;
		}

		if (timeLeft > 0.0f) {
			EndDamagingMove();
			_renderer.AnimPaused = true;
			_controllable = false;
			_controllableTimeout = timeLeft;
			_frozenTimeLeft = timeLeft;
		} else if (_frozenTimeLeft > 0.0f) {
			_controllable = true;
			_controllableTimeout = 0.0f;
			ThawOnNextUpdate();
		}

		return true;
	}

	void Player::SetInvulnerability(float timeLeft, InvulnerableType type)
	{
		if (timeLeft <= 0.0f) {
			if (_invulnerableTime > 0.0f) {
				SetState(ActorState::IsInvulnerable, false);
				_invulnerableTime = 0.0f;
				_shieldSpawnTime = ShieldDisabled;
				_renderer.SetDrawEnabled(true);
			}
			return;
		}

		if (type == InvulnerableType::Shielded) {
			if (_invulnerableTime > 0.0f) {
				// If the player is already blinking, show it now
				_renderer.SetDrawEnabled(true);
			}
			if (_shieldSpawnTime <= ShieldDisabled) {
				_shieldSpawnTime = 1.0f;
			}
		} else {
			_invulnerableBlinkTime = (type == InvulnerableType::Transient ? -1.0f : 0.0f);
			_shieldSpawnTime = ShieldDisabled;
		}

		SetState(ActorState::IsInvulnerable, true);
		_invulnerableTime = timeLeft;
	}

	Player::InvulnerableType Player::GetInvulnerableType() const
	{
		if (_shieldSpawnTime > ShieldDisabled) {
			return InvulnerableType::Shielded;
		}
		// Transient has no visual effect
		return (_invulnerableBlinkTime >= 0.0f ? InvulnerableType::Blinking : InvulnerableType::Transient);
	}

	void Player::GrantInvulnerability(float timeLeft, InvulnerableType type)
	{
		if (timeLeft <= 0.0f) {
			// Not a revocation, clearing is SetInvulnerability()'s job
			return;
		}

		if (_invulnerableTime > 0.0f) {
			// Never shorten a running invulnerability or weaken its effect, each checked separately
			if (GetInvulnerableStrength(GetInvulnerableType()) > GetInvulnerableStrength(type)) {
				type = GetInvulnerableType();
			}
			if (timeLeft < _invulnerableTime) {
				timeLeft = _invulnerableTime;
			}
		}

		// Through the virtual, so the server replicates the resolved values
		SetInvulnerability(timeLeft, type);
	}

	void Player::EndDamagingMove()
	{
		if (_currentSpecialMove == SpecialMoveType::None) {
			return;
		}

		SetState(ActorState::ApplyGravitation, true);
		SetAnimation(_currentAnimation->State & ~(AnimState::Uppercut | AnimState::Buttstomp));

		if (_currentSpecialMove == SpecialMoveType::Uppercut) {
			if (_suspendType == SuspendType::None) {
				SetTransition(AnimState::TransitionUppercutEnd, false);
			}
			_controllable = true;
			// A move cut short must not carry the weightless gravity into the fall
			_uppercutTimeLeft = 0.0f;

			if (_externalForce.Y < 0.0f) {
				_externalForce.Y = 0.0f;
			}
		} else if (_currentSpecialMove == SpecialMoveType::Sidekick) {
			CancelTransition();
			_controllable = true;
			_controllableTimeout = 10;

			// The end speed is decided in CheckEndOfSpecialMoves(), only drop the remaining distance
			_sidekickDistanceLeft = 0.0f;
			_sidekickTime = 0.0f;
		}

		_currentSpecialMove = SpecialMoveType::None;
	}

	std::int32_t Player::GetScore() const
	{
		return _score;
	}

	void Player::AddScore(std::int32_t amount)
	{
		_score = std::min<std::int32_t>(std::max<std::int32_t>(_score + amount, 0), 999999999);
	}

	bool Player::AddHealth(std::int32_t amount)
	{
		// A level script may raise the maximum health above 5
		const std::int32_t HealthLimit = std::max<std::int32_t>(_maxHealth, 5);

		if (_health >= HealthLimit) {
			return false;
		}

		if (amount < 0) {
			_health = HealthLimit;
			PlayPlayerSfx("PickupMaxCarrot"_s);
		} else {
			_health = std::min(_health + amount, HealthLimit);
			if (_maxHealth < _health) {
				_maxHealth = _health;
			}
			PlayPlayerSfx("PickupFood"_s);
		}

		return true;
	}

	std::int32_t Player::GetLives() const
	{
		return _lives;
	}

	bool Player::AddLives(std::int32_t count)
	{
		constexpr std::int32_t LivesLimit = 99;

		if (_lives >= LivesLimit) {
			return false;
		}

		_lives = std::min(_lives + count, LivesLimit);
		PlayPlayerSfx("PickupOneUp"_s);
		return true;
	}

	std::int32_t Player::GetCoins() const
	{
		return _inventory.Coins;
	}

	void Player::AddCoins(std::int32_t count)
	{
		std::int32_t prevCoins = _inventory.Coins;
		_inventory.Coins += count;
		_levelHandler->HandlePlayerCoins(this, prevCoins, _inventory.Coins);
		PlayPlayerSfx("PickupCoin"_s);
	}

	void Player::AddCoinsInternal(std::int32_t count)
	{
		_inventory.Coins += count;
	}

	std::int32_t Player::GetGems(std::uint8_t gemType) const
	{
		if (gemType >= arraySize(_inventory.Gems)) {
			return 0;
		}

		return _inventory.Gems[gemType];
	}

	void Player::AddGems(std::uint8_t gemType, std::int32_t count)
	{
		if (gemType >= arraySize(_inventory.Gems)) {
			return;
		}

		std::int32_t prevGems = _inventory.Gems[gemType];
		_inventory.Gems[gemType] += count;
		_levelHandler->HandlePlayerGems(this, gemType, prevGems, _inventory.Gems[gemType]);
		float pitch = 1.0f - fabs(2.0f * fmod(_gemsPitch * 0.05f, 1.0f) - 1.0f);
		PlayPlayerSfx("PickupGem"_s, 1.0f, std::min(0.7f + pitch * 0.6f, 1.3f));
		_gemsTimer = 120.0f;
		// The pitch above repeats every 20 gems
		_gemsPitch = (_gemsPitch + 1) % 20;
	}

	std::int32_t Player::GetConsumedFood() const
	{
		return _inventory.FoodEaten;
	}

	void Player::ConsumeFood(bool isDrinkable)
	{
		PlayPlayerSfx(isDrinkable ? "PickupDrink"_s : "PickupFood"_s);

		_inventory.FoodEaten++;
		if (_inventory.FoodEaten >= 100 && _levelHandler->CanActivateSugarRush()) {
			_inventory.FoodEaten = _inventory.FoodEaten % 100;
			ActivateSugarRush(1300.0f);
		}
	}

	void Player::ActivateSugarRush(float duration)
	{
		if (_sugarRushLeft > 0.0f) {
			_sugarRushLeft = std::max(duration, 1.0f);
			return;
		}
		if (duration <= 0.0f) {
			return;
		}
		_sugarRushLeft = duration;
		_renderer.Initialize(ActorRendererType::PartialWhiteMask);
		_weaponWheelState = WeaponWheelState::Hidden;
		_levelHandler->HandleActivateSugarRush(this);
	}

	void Player::DeactivateSugarRush()
	{
		if (_sugarRushLeft <= 0.0f) {
			return;
		}

		_sugarRushLeft = 0.0f;

		// Only revert the renderer if sugar rush set it
		if (_renderer.GetRendererType() == ActorRendererType::PartialWhiteMask) {
			_renderer.Initialize(ActorRendererType::Default);
		}
	}

	bool Player::AddAmmo(WeaponType weaponType, std::int16_t count)
	{
		if (weaponType >= WeaponType::Count) {
			return false;
		}
		const WeaponDefinition& definition = _levelHandler->GetWeaponDefinition(weaponType);
		std::uint16_t& ammo = _inventory.WeaponAmmo[(std::int32_t)weaponType];
		std::uint16_t limit = GetAmmoLimit(definition.Options);
		if (!definition.Options.Allowed || ammo == UINT16_MAX || ammo >= limit) {
			return false;
		}

		bool switchTo = (ammo == 0);

		std::int32_t added = count * std::max<std::int32_t>(definition.Options.Multiplier, 1) * 256;
		ammo = std::uint16_t(std::clamp<std::int32_t>(std::int32_t(ammo) + added, 0, limit));

		if (switchTo) {
			SetCurrentWeapon(weaponType, SetCurrentWeaponReason::AddAmmo);

			String customPath = (definition.Kind == WeaponKind::Custom ? _levelHandler->GetWeapons().GetSpriteMetadataPath(definition.GetCustom().Shot.Sprite) : String());
			if (!customPath.empty()) {
				PreloadMetadataAsync(customPath);
			} else switch (_currentWeapon) {
				case WeaponType::Blaster: PreloadMetadataAsync("Weapon/Blaster"_s); break;
				case WeaponType::Bouncer: PreloadMetadataAsync("Weapon/Bouncer"_s); break;
				case WeaponType::Freezer: PreloadMetadataAsync("Weapon/Freezer"_s); break;
				case WeaponType::Seeker: PreloadMetadataAsync("Weapon/Seeker"_s); break;
				case WeaponType::RF: PreloadMetadataAsync("Weapon/RF"_s); break;
				case WeaponType::Toaster: PreloadMetadataAsync("Weapon/Toaster"_s); break;
				case WeaponType::TNT: PreloadMetadataAsync("Weapon/TNT"_s); break;
				case WeaponType::Pepper: PreloadMetadataAsync("Weapon/Pepper"_s); break;
				case WeaponType::Electro: PreloadMetadataAsync("Weapon/Electro"_s); break;
				case WeaponType::Thunderbolt: PreloadMetadataAsync("Weapon/Thunderbolt"_s); break;
			}
		}

		PlayPlayerSfx("PickupAmmo"_s);
		return true;
	}

	void Player::AddWeaponUpgrade(WeaponType weaponType, std::uint8_t upgrade)
	{
		if (weaponType >= WeaponType::Count) {
			return;
		}
		const WeaponOptions& options = _levelHandler->GetWeaponDefinition(weaponType).Options;
		if (!options.Allowed || !options.AllowedPowerup) {
			upgrade &= ~0x01;
			if (upgrade == 0) {
				return;
			}
		}

		bool shouldSwitchToWeapon = (_inventory.WeaponUpgrades[(std::int32_t)weaponType] == 0);

		_inventory.WeaponUpgrades[(std::int32_t)weaponType] |= upgrade;

		if (shouldSwitchToWeapon) {
			SetCurrentWeapon(weaponType, SetCurrentWeaponReason::AddUpgrade);
		}
	}

	bool Player::AddFastFire(std::int32_t count)
	{
		const std::int32_t FastFireLimit = 9;

		std::int32_t current = (_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		if (current >= FastFireLimit) {
			return false;
		}

		current = std::min(current + count, FastFireLimit);

		_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] = (std::uint8_t)((_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] & 0x1) | (current << 1));

		PlayPlayerSfx("PickupAmmo"_s);

		return true;
	}

	bool Player::MorphTo(PlayerType type)
	{
		if (_playerType == type) {
			return false;
		}

		// Nothing changes unless the new character's sprites are available
		Metadata* metadata = RequestMorphMetadata(type);
		if (metadata == nullptr) {
			return false;
		}

		PlayerType playerTypePrevious = _playerType;
		// Birds have no transformation, they use the puff of smoke
		bool isBirdMorph = (type == PlayerType::Bird || type == PlayerType::Bird2 ||
			playerTypePrevious == PlayerType::Bird || playerTypePrevious == PlayerType::Bird2);

		// End the stopping chain first, its callback would otherwise see the cleared animation below
		ApplyStopAnimation(false);

		ApplyMorphType(type, metadata);
		// The recolor scheme is per character
		RefreshColorPalette();

		// Refresh animation state, the new metadata has at least the idle pose
		if ((_currentSpecialMove == SpecialMoveType::None) ||
			(_currentSpecialMove == SpecialMoveType::Buttstomp && (type == PlayerType::Jazz || type == PlayerType::Spaz || type == PlayerType::Lori))) {
			AnimState prevAnim = (_currentAnimation != nullptr ? _currentAnimation->State : AnimState::Idle);
			_currentAnimation = nullptr;
			if (!SetAnimation(prevAnim)) {
				SetAnimation(AnimState::Idle);
			}
		} else {
			_currentAnimation = nullptr;
			if (!SetAnimation(AnimState::Fall)) {
				SetAnimation(AnimState::Idle);
			}

			// The move's transitions end with it when morphing into or out of a bird
			if (isBirdMorph) {
				ForceCancelTransition();
			}

			SetState(ActorState::ApplyGravitation, true);
			_controllable = true;

			if (_currentSpecialMove == SpecialMoveType::Uppercut && _externalForce.Y < 0.0f) {
				_externalForce.Y = 0.0f;
			}

			_currentSpecialMove = SpecialMoveType::None;
		}

		ApplyBirdMorph(playerTypePrevious);

		// Set transition
		if (type == PlayerType::Frog && !isBirdMorph) {
			PlayPlayerSfx("Transform");

			_controllable = false;
			_controllableTimeout = 120.0f;

			switch (playerTypePrevious) {
				case PlayerType::Jazz:
					SetTransition(TransformFrogFromJazz, false, [this]() {
						_controllable = true;
						_controllableTimeout = 0.0f;
					});
					break;
				case PlayerType::Spaz:
					SetTransition(TransformFrogFromSpaz, false, [this]() {
						_controllable = true;
						_controllableTimeout = 0.0f;
					});
					break;
				case PlayerType::Lori:
					SetTransition(TransformFrogFromLori, false, [this]() {
						_controllable = true;
						_controllableTimeout = 0.0f;
					});
					break;
			}
		} else if (playerTypePrevious == PlayerType::Frog && !isBirdMorph) {
			_controllable = false;
			_controllableTimeout = 120.0f;

			SetTransition(AnimState::TransitionFromFrog, false, [this]() {
				_controllable = true;
				_controllableTimeout = 0.0f;
			});
		} else {
			if (isBirdMorph) {
				// Drop a cosmetic shooting transition, ones like hurt or warp play out
				if (ShouldCancelTransitionOnFire()) {
					ForceCancelTransition();
				}
			}

			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X - 12.0f), (std::int32_t)(_pos.Y - 6.0f), _renderer.GetLayer() + 4), Explosion::Type::SmokeBrown);
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X - 8.0f), (std::int32_t)(_pos.Y + 28.0f), _renderer.GetLayer() + 4), Explosion::Type::SmokeBrown);
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + 12.0f), (std::int32_t)(_pos.Y + 10.0f), _renderer.GetLayer() + 4), Explosion::Type::SmokeBrown);

			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)(_pos.Y + 12.0f), _renderer.GetLayer() + 6), Explosion::Type::SmokeBrown);
		}

		return true;
	}

	bool Player::MorphToInstantly(PlayerType type)
	{
		if (_playerType == type) {
			return false;
		}

		// Same as in MorphTo(), nothing changes unless the new character can be shown
		Metadata* metadata = RequestMorphMetadata(type);
		if (metadata == nullptr) {
			return false;
		}

		PlayerType playerTypePrevious = _playerType;

		ApplyStopAnimation(false);

		ApplyMorphType(type, metadata);
		RefreshColorPalette();

		// Refresh animation state, special moves of the previous type cannot continue
		AnimState prevState = (_currentAnimation != nullptr ? _currentAnimation->State : AnimState::Idle);
		_currentSpecialMove = SpecialMoveType::None;
		_currentAnimation = nullptr;
		if (!SetAnimation(prevState)) {
			SetAnimation(AnimState::Idle);
		}

		ApplyBirdMorph(playerTypePrevious);

		return true;
	}

	void Player::MorphRevert()
	{
		MorphTo(IsTemporaryForm(_playerType) ? _playerTypeBeforeMorph : _playerTypeOriginal);
	}

	bool Player::CanMorphTo(PlayerType type)
	{
		return (_playerType == type || RequestMorphMetadata(type) != nullptr);
	}

	Metadata* Player::RequestMorphMetadata(PlayerType type)
	{
		// The fur color depends on the type, resolve it for the new one
		PlayerType playerTypePrevious = _playerType;
		_playerType = type;
		// Indexed only when recolored (must match the renderer's current palette state)
		bool useIndexed = (GetEffectiveFurColor() != 0);
		_playerType = playerTypePrevious;

		Metadata* metadata = ContentResolver::Get().RequestMetadata(GetCharacterTraits(type).Metadata, useIndexed);
		if (metadata == nullptr || metadata->FindAnimation(AnimState::Idle) == nullptr) {
			LOGW("Cannot morph to player type {}, its sprites are not available", (std::int32_t)type);
			return nullptr;
		}
		return metadata;
	}

	void Player::ApplyMorphType(PlayerType type, Metadata* metadata)
	{
		// Remember the character the first temporary form was taken from
		if (!IsTemporaryForm(type)) {
			_playerTypeBeforeMorph = type;
		} else if (!IsTemporaryForm(_playerType)) {
			_playerTypeBeforeMorph = _playerType;
		}

		_playerType = type;
		_metadata = metadata;
		RefreshCharacterProfile();
	}

	void Player::ApplyBirdMorph(PlayerType playerTypePrevious)
	{
		// One bird into the other keeps the flight, only the attack differs
		bool wasBird = (playerTypePrevious == PlayerType::Bird || playerTypePrevious == PlayerType::Bird2);
		_birdCharging = false;
		_birdChargeDirY = 0;

		if (IsBird() && !wasBird) {
			// End modifiers, vines and water, gravity is removed every frame in OnUpdatePhysics()
			if (_activeModifier != Modifier::None) {
				SetModifier(Modifier::None);
			}
			if (_suspendType == SuspendType::SwingingVine) {
				CancelCarryingObject();
			}
			_suspendType = SuspendType::None;
			_inWater = false;
			_renderer.SetRotation(0.0f);
			_isFreefall = false;
			_keepRunningTime = 0.0f;
			_externalForce.Y = 0.0f;
			_internalForceY = 0.0f;
			CancelRevUp();
			ResetLegacyMovementState();

			_birdAccelX = 0.0f;
			_birdIdleTime = 0.0f;
			_birdPerched = false;
			_birdPose = 0;
			// A bird made on the floor stands where the rabbit did, as in the original
			_birdStanding = true;
			OnUpdateHitbox();
			AABBf below = AABBInner;
			below.T = below.B;
			below.B += CollisionCheckStep;
			TileCollisionParams params = { TileDestructType::None, true };
			if (_levelHandler->IsPositionEmpty(this, below, params)) {
				_birdStanding = false;
				OnUpdateHitbox();
			}
		} else if (!IsBird() && wasBird) {
			_birdAccelX = 0.0f;
			_birdIdleTime = 0.0f;
			_birdPerched = false;
			_birdStanding = false;
			_birdPose = 0;

			// The character's hitbox is taller, lift it out of the floor
			OnUpdateHitbox();
			TileCollisionParams params = { TileDestructType::None, true };
			if (!_levelHandler->IsPositionEmpty(this, AABBInner, params)) {
				for (std::int32_t lift = 1; lift <= 8; lift++) {
					if (MoveInstantly(Vector2f(0.0f, -float(lift)), MoveType::Relative, params)) {
						break;
					}
				}
			}

			// Gravity comes back, unless a tube still carries the player
			if (_inTubeTime <= 0.0f) {
				SetState(ActorState::ApplyGravitation, true);
			}
		}
	}

	bool Player::SetDizzy(float timeLeft)
	{
		bool wasNotDizzy = (_dizzyTime <= 0.0f);
		_dizzyTime = timeLeft;
		return wasNotDizzy;
	}

	bool Player::ApplyBlastKnockback(bool pushLeft, float distanceSqr)
	{
		float sign = (pushLeft ? -1.0f : 1.0f);
		if (_levelHandler->IsReforged()) {
			AddExternalForce(4.0f * sign, 0.0f);
		} else {
			// The caller's search reaches further than the original, limit it here
			if (distanceSqr > LegacyRFBlastReach * LegacyRFBlastReach) {
				return false;
			}
			// Matches the original: a fixed speed held for a fixed time, no decay
			_speed.X = LegacyRFBlastSpeed * sign;
			_rfBlastLeft = LegacyRFBlastHoldTicks;
			if (!GetState(ActorState::CanJump)) {
				_speed.Y = -LegacyRFBlastRiseSpeed;
				// The rise decays at the released rate even with jump held
				_internalForceY = 0.0f;
				_jumpReleased = true;
				_riseOwnedByJump = false;
			} else {
				// On the floor the lift grows with the range, fitted to measurements of the original
				_speed.Y = -(LegacyRFBlastLiftBase + LegacyRFBlastLiftSlope * sqrtApprox(distanceSqr));
				_internalForceY = 0.0f;
				_jumpReleased = true;
				_riseOwnedByJump = false;
			}
		}

		return true;
	}

	bool Player::SetShield(ShieldType shieldType, float time)
	{
		bool shouldSwitchToBlaster = (shieldType != ShieldType::None && _activeShield == ShieldType::None);

		_activeShield = shieldType;
		_activeShieldTime = (shieldType != ShieldType::None ? time : 0.0f);

		if (shouldSwitchToBlaster) {
			SetCurrentWeapon(WeaponType::Blaster, SetCurrentWeaponReason::Shield);
		}

		return true;
	}

	bool Player::IncreaseShieldTime(float time)
	{
		if (_activeShieldTime <= 0.0f) {
			return false;
		}

		_activeShieldTime += time;
		PlayPlayerSfx("PickupGem"_s);
		return true;
	}

	void Player::DecreaseShieldTime(float time)
	{
		// An absorbed hit never drops the shield right before it would expire anyway
		if (_activeShieldTime > time) {
			_activeShieldTime -= time;
		}
	}

	bool Player::SpawnBird(std::uint8_t type, Vector2f pos)
	{
		if (_spawnedBird != nullptr) {
			return false;
		}

		_spawnedBird = std::make_shared<Environment::Bird>();
		std::uint8_t birdParams[2] = { type, (std::uint8_t)_playerIndex };
		_spawnedBird->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((std::int32_t)pos.X, (std::int32_t)pos.Y, _renderer.GetLayer() + 80),
			birdParams
		));
		_levelHandler->AddActor(_spawnedBird);
		return true;
	}

	bool Player::DisableControllable(float timeout)
	{
		if (!_controllable) {
			if (timeout <= 0.0f) {
				_controllable = true;
				_controllableTimeout = 0.0f;
				return true;
			} else {
				return false;
			}
		}

		if (timeout <= 0.0f) {
			return false;
		}

		_controllable = false;
		if (timeout == std::numeric_limits<float>::infinity()) {
			_controllableTimeout = 0.0f;
		} else {
			_controllableTimeout = timeout;
		}

		SetAnimation(AnimState::Idle);
		return true;
	}

	void Player::SetCheckpoint(Vector2f pos, float ambientLight)
	{
		_checkpointPos = Vector2f(pos.X, pos.Y - 20.0f);
		_checkpointLight = ambientLight;
		// Frog and Bird are only temporary forms
		_checkpointPlayerType = GetPlayerTypeBeforeMorph();
		
		_inventoryCheckpoint = _inventory;
	}

	void Player::CancelCarryingObject(ActorBase* expectedActor)
	{
		if (expectedActor != nullptr && _carryingObject != expectedActor) {
			return;
		}

		_carryingObject = nullptr;

		if (_suspendType == SuspendType::SwingingVine) {
			_suspendType = SuspendType::None;
			SetState(ActorState::ApplyGravitation, true);
			_renderer.SetRotation(0.0f);
		}
	}

	void Player::UpdateCarryingObject(ActorBase* actor, SuspendType suspendType)
	{
		DEATH_DEBUG_ASSERT(actor != nullptr);

		if (_carryingObject != nullptr && _carryingObject != actor) {
			return;
		}
		// A bird flies past a swinging vine like any other vine
		if (suspendType == SuspendType::SwingingVine && IsBird()) {
			return;
		}

		_carryingObject = actor;

		ResetAirJumps();

		if (suspendType == SuspendType::SwingingVine) {
			_suspendType = suspendType;
			SetState(ActorState::ApplyGravitation, false);
		} else if (_suspendType == SuspendType::SwingingVine) {
			_suspendType = SuspendType::None;
			SetState(ActorState::ApplyGravitation, true);
			_renderer.SetRotation(0.0f);
		}
	}

	bool Player::ApplyPlayerBump(Player& other, bool stackingEnabled)
	{
		// Resolve along the axis of least penetration
		const AABBf& a = AABBInner;
		const AABBf& b = other.AABBInner;
		float overlapX = std::min(a.R, b.R) - std::max(a.L, b.L);
		float overlapY = std::min(a.B, b.B) - std::max(a.T, b.T);
		if (overlapX <= 0.0f || overlapY <= 0.0f) {
			// Boxes don't overlap (per-pixel collision reported the hit)
			return false;
		}

		if (overlapY <= overlapX && stackingEnabled) {
			// One player is standing on the other, UpdatePlayerStacking() handles it
			return false;
		}

		// Equal-mass elastic bump, applied only while approaching so it can't compound across frames
		Vector2f normal;
		float penetration;
		if (overlapX < overlapY) {
			normal = Vector2f(_pos.X < other._pos.X ? -1.0f : 1.0f, 0.0f);
			penetration = overlapX;
		} else {
			normal = Vector2f(0.0f, _pos.Y < other._pos.Y ? -1.0f : 1.0f);
			penetration = overlapY;
		}

		float push = std::min(penetration * 0.5f, PlayerBumpMaxSeparationPerFrame);
		MoveInstantly(Vector2f(normal.X * push, normal.Y * push), MoveType::Relative);
		other.MoveInstantly(Vector2f(-normal.X * push, -normal.Y * push), MoveType::Relative);

		Vector2f relativeSpeed = _speed - other._speed;
		float approachSpeed = relativeSpeed.X * normal.X + relativeSpeed.Y * normal.Y;
		if (approachSpeed < 0.0f) {
			float impulse = std::max(-(1.0f + PlayerBumpRestitution) * approachSpeed * 0.5f, PlayerBumpMinSeparationSpeed);
			_speed += normal * impulse;
			other._speed += normal * (-impulse);
		}
		return true;
	}

	void Player::UpdatePlayerStacking(float timeMult, bool snap)
	{
		// A bird flies over another player instead of perching on its head
		ActorBase* ground = (!IsBird() ? _levelHandler->FindPlayerToStandOn(this, timeMult) : nullptr);
		if (ground != nullptr) {
			if (snap) {
				// Rest on the other player's head, computed from our own hitbox since a RemoteActor's box is taller
				float groundHead = ground->GetPos().Y + (AABBInner.T - _pos.Y);
				float delta = groundHead - AABBInner.B + PlayerStackSinkDepth;
				if (delta != 0.0f) {
					MoveInstantly(Vector2f(0.0f, delta), MoveType::Relative);
				}
			}
			// Carrying lets us jump off and rest on top instead of falling through
			UpdateCarryingObject(ground);
			_stackCarrying = true;
		} else if (_stackCarrying) {
			// No longer standing on a player, never cancels a real solid object
			CancelCarryingObject();
			_stackCarrying = false;
		}
	}

	bool Player::IsBeingStoodOnByPlayer() const
	{
		// Sees only locally simulated players, so online it's driven from the server
		for (auto* other : _levelHandler->GetPlayers()) {
			if (other != this && other->_stackCarrying && other->_carryingObject == this) {
				return true;
			}
		}
		return false;
	}
}

