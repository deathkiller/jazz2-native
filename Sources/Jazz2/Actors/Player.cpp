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
#include "Enemies/EnemyBase.h"
#include "Enemies/TurtleShell.h"

#include "Weapons/BlasterShot.h"
#include "Weapons/BouncerShot.h"
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
	static constexpr AnimState CompositeAnimMask = (AnimState)0xFFF83F60;
	static constexpr AnimState TransformFrogFromJazz = (AnimState)0x60000000;
	static constexpr AnimState TransformFrogFromSpaz = (AnimState)0x60000001;
	static constexpr AnimState TransformFrogFromLori = (AnimState)0x60000002;

	Player::Player()
		:
		_playerIndex(0),
		_playerType(PlayerType::Jazz), _playerTypeOriginal(PlayerType::Jazz),
		_isActivelyPushing(false),
		_wasActivelyPushing(false),
		_pushContactThisFrame(false),
		_controllable(true),
		_controllableExternal(true),
		_controllableTimeout(0.0f),
		_lastExitType(ExitType::None),
		_wasUpPressed(false), _wasDownPressed(false), _wasJumpPressed(false), _wasFirePressed(false), _isRunPressed(false),
		_currentSpecialMove(SpecialMoveType::None),
		_isAttachedToPole(false), _canPushFurther(false),
		_copterFramesLeft(0.0f), _fireFramesLeft(0.0f), _pushFramesLeft(0.0f), _waterCooldownLeft(0.0f),
		_levelExiting(LevelExitingState::None),
		_isFreefall(false), _inWater(false), _isLifting(false), _isSpring(false),
		_flyCheatActive(false),
		_inShallowWater(-1),
		_activeModifier(Modifier::None),
		_externalForceCooldown(0.0f),
		_springCooldown(0.0f),
		_dashGraceLeft(0.0f), _sidekickDistanceLeft(0.0f), _sidekickTime(0.0f), _rfBlastLeft(0.0f),
		_uppercutTimeLeft(0.0f), _jumpReleased(false), _poleEnteredOnSpring(false),
		_inIdleTransition(false), _inLedgeTransition(false), _canDoubleJump(true),
		_carryingObject(nullptr), _stackCarrying(false), _beingStoodOn(false),
		// Per-player recolor; 0 = use the original colors. The local player defaults to the user's profile color;
		// remote players get their color from the network (see MpLevelHandler).
		_furColor(PreferencesCache::PlayerFurColor), _paletteOffset(-1),
		_lives(0), _score(0),
		_inventory{}, _inventoryCheckpoint{},
		_checkpointLight(1.0f), _currentAmbientLight(1.0f),
		_sugarRushLeft(0.0f), _sugarRushStarsTime(0.0f),
		_shieldSpawnTime(ShieldDisabled),
		_gemsTotal{}, _gemsPitch(0),
		_gemsTimer(0.0f),
		_bonusWarpTimer(0.0f),
		_suspendType(SuspendType::None),
		_suspendTime(0.0f),
		_invulnerableTime(0.0f),
		_invulnerableBlinkTime(0.0f),
		_jumpTime(0.0f),
		_idleTime(0.0f),
		_hitFloorTime(5.0f),
		_keepRunningTime(0.0f),
		_lastPoleTime(0.0f),
		_inTubeTime(0.0f),
		_dizzyTime(0.0f),
		_activeShield(ShieldType::None),
		_activeShieldTime(0.0f),
		_weaponFlareTime(0.0f),
		_weaponCooldown(0.0f),
		_weaponAllowed(true),
		_weaponWheelState(WeaponWheelState::Hidden)
	{
	}

	Player::~Player()
	{
		if (_spawnedBird != nullptr) {
			_spawnedBird->FlyAway();
			_spawnedBird = nullptr;
		}
		StopAllActiveSounds();
		// Release the shared palette offset back to the pool (e.g., on disconnect/level end)
		ReleasePaletteOffset();
	}

	void Player::StopAllActiveSounds()
	{
#if defined(WITH_AUDIO)
		if (_copterSound != nullptr) {
			_copterSound->stop();
			_copterSound = nullptr;
		}
		if (_airboardSound != nullptr) {
			_airboardSound->stop();
			_airboardSound = nullptr;
		}
		if (_airboardTurnSound != nullptr) {
			_airboardTurnSound->stop();
			_airboardTurnSound = nullptr;
		}
		if (_weaponSound != nullptr) {
			_weaponSound->stop();
			_weaponSound = nullptr;
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
			// Spectate mode and unknown types fall back to Jazz metadata, the player is invisible anyway
			{ "Interactive/PlayerJazz"_s, 1.0f, 0 }
		};

		switch (type) {
			case PlayerType::Jazz: return Traits[0];
			case PlayerType::Spaz: return Traits[1];
			case PlayerType::Lori: return Traits[2];
			case PlayerType::Frog: return Traits[3];
			default: return Traits[4];
		}
	}

	std::uint32_t Player::GetEffectiveFurColor() const
	{
		// The level handler may force a color (multiplayer team modes recolor players to their team color); this
		// applies even when the player picked no custom color of their own.
		std::uint32_t furColor = _levelHandler->GetPlayerFurColor(this, _furColor);

		// Online multiplayer always recolors the local player; local games depend on the "Apply Colors" preference
		// (and the player index for the "first player only" mode).
		if (furColor == 0) {
			return 0;
		}
		if (!_levelHandler->IsLocalSession()) {
			return furColor;
		}
		// A game-mode-forced color (e.g., team coloring) applies to every local player regardless of the cosmetic
		// "Apply Colors" preference, which only governs the player's own chosen color.
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

		// Recoloring needs indexed sprites (palette index in the red channel); otherwise the PaletteRemap shader
		// would treat the already-baked colors as palette indices and corrupt the sprite, so fall back to normal
		// rendering. If this warns, the player .res animations aren't being loaded indexed.
		bool isIndexed = (furColor != 0 && _metadata != nullptr && !_metadata->Animations.empty() && _metadata->Animations[0].Base != nullptr &&
			(_metadata->Animations[0].Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);
		if (furColor != 0 && !isIndexed) {
			LOGW("Player sprites are not indexed - recoloring disabled");
		}

		// Acquire the new (shared, reference-counted) palette before releasing the old one, so an unchanged fur color
		// keeps its slot instead of being freed and immediately rebuilt
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
		_playerIndex = details.Params[1];

		// Load the anim set as indexed ONLY when this player is actually being recolored, so it can be recolored at
		// draw time. Loading indexed without applying a palette would render raw palette indices (glitched sprites).
		bool useIndexed = (GetEffectiveFurColor() != 0);
		// TODO: Spectate mode - loads Jazz metadata for fallback, but player will be invisible
		async_await RequestMetadataAsync(GetCharacterTraits(_playerType).Metadata, useIndexed);

		SetAnimation(AnimState::Fall);

		// Build and bind the per-player recolor palette now that the renderer has a texture
		RefreshColorPalette();

		std::memset(_inventory.WeaponAmmo, 0, sizeof(_inventory.WeaponAmmo));
		std::memset(_inventoryCheckpoint.WeaponAmmo, 0, sizeof(_inventoryCheckpoint.WeaponAmmo));
		std::memset(_inventory.WeaponUpgrades, 0, sizeof(_inventory.WeaponUpgrades));
		std::memset(_inventoryCheckpoint.WeaponUpgrades, 0, sizeof(_inventoryCheckpoint.WeaponUpgrades));

		_inventory.WeaponAmmo[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
		_inventoryCheckpoint.WeaponAmmo[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;

		if (_playerType == PlayerType::Spectate) {
			// Spectate mode - no collision, no gravity, invisible
			SetState(ActorState::PreserveOnRollback | ActorState::ExcludeSimilar, true);
			SetState(ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjects | ActorState::IsSolidObject, false);
			_health = 0;
			_maxHealth = 0;
			_renderer.setDrawEnabled(false);
			
			// Empty hitbox for spectate mode
			AABB = {};
			AABBInner = {};
		} else {
			SetState(ActorState::PreserveOnRollback | ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjects |
				ActorState::IsSolidObject | ActorState::ExcludeSimilar, true);

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

	bool Player::CanBreakSolidObjects() const
	{
		if (_sugarRushLeft > 0.0f) {
			return true;
		}

		if (_currentSpecialMove == SpecialMoveType::Buttstomp && _currentTransition != nullptr) {
			// Buttstomp is probably in starting transition, do nothing yet
			return false;
		}

		return (_currentSpecialMove != SpecialMoveType::None);
	}

	bool Player::CanMoveVertically() const
	{
		return (_inWater || _activeModifier != Modifier::None);
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
		return PreferencesCache::EnableLedgeClimb;
	}

	bool Player::OnTileDeactivated()
	{
		// Player cannot be deactivated
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
					// UpdateAnimation() was probably skipped in this step, because _controllable was false, so call it here
					UpdateAnimation(0.0f);
				}
			});

			_renderer.setDrawEnabled(true);
			PlayPlayerSfx("WarpOut"_s, 1.0f / _levelHandler->GetPlayers().size());
			_levelHandler->PlayerExecuteRumble(this, "Warp"_s);

			_lastExitType = ExitType::None;
		}

		// In local splitscreen co-op, resolve standing on another player before the physics update, so jump input
		// sees CanJump() == true (online sessions call UpdatePlayerStacking from the MpPlayer subclasses instead,
		// where CanPlayersCollide() is false)
		if (_levelHandler->CanPlayersCollide()) {
			UpdatePlayerStacking(timeMult, /*snap:*/ true);
		}

		bool canJumpPrev = CanJump();
		// Captured before the physics, which is where gravity lands - see `_frameStartSpeedY`
		_frameStartSpeedY = _speed.Y;
		OnUpdatePhysics(timeMult);

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
		OnHandleMovement(timeMult, areaWeaponAllowed, canJumpPrev);

#if defined(WITH_AUDIO)
		if (_airboardTurnSound != nullptr && !_airboardTurnSound->isPlaying()) {
			if (_activeModifier == Modifier::Airboard) {
				PlayPlayerSfx("AirboardTurnEnd"_s, 2.0f, 0.8f);
			}
			_airboardTurnSound = nullptr;
		}
#endif

		// Handle weapon switching
		if (((_controllable && _controllableExternal) || !_levelHandler->IsReforged()) && _playerType != PlayerType::Frog) {
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
		// In Reforged mode keep the engine's symmetric gravity, that jump uses the internal-force model.
		if (_levelHandler->IsReforged()) {
			return baseGravity;
		}

		// Original JJ2: in air the rise decelerates ~3x faster than the fall accelerates, which gives the
		// recognizable "snappy up, floaty down" arc. All three figures are the original's own, measured with
		// the trajectory probe (see the constants in Player.h).
		if (_pos.Y >= _levelHandler->GetWaterLevel()) {
			return LegacyWaterGravity;
		}
		// An uppercut is nearly weightless for its whole length, which is what lets 7.5 px/tick climb 228 px
		if (_uppercutTimeLeft > 0.0f) {
			return LegacyUppercutGravity;
		}
		if (!isRising) {
			return LegacyFallGravity;
		}
		// Letting go of jump makes the rest of the ascent heavier rather than truncating the speed, which is
		// how the original shortens a jump - and why a tap hop has its own arc instead of a clipped one.
		//
		// That heavier rate then eases off for the last pixel per tick of the rise. The held rate does not:
		// it is a flat 0.375 all the way to the apex, on 379 measured samples.
		if (!_jumpReleased) {
			return LegacyRiseGravity;
		}
		return (std::abs(_speed.Y) > LegacyRiseBrakeEaseSpeed
			? LegacyRiseGravityReleased
			: LegacyRiseGravityReleasedNearApex);
	}

	void Player::OnUpdatePhysics(float timeMult)
	{
		// Force collisions every frame even if player doesn't move
		SetState(ActorState::IsDirty, true);

		// The applied rise cap reproduces the original constant-speed rise for jumps/springs; special moves
		// (uppercut/sidekick) launch via forces and skip it. The cap stays on even in a tube, so a sucker tube
		// ascends slowly like the original - but the raised vertical-speed limit is kept while in the tube and
		// while the launch momentum is still bleeding off, so a strong suck (e.g. -30) keeps rising at the cap
		// for a long time (high) instead of being clamped to 16 the instant the tube ends.
		if (!_levelHandler->IsReforged()) {
			_maxRiseSpeed = (_currentSpecialMove == SpecialMoveType::None ? LegacyRiseSpeedCap : 0.0f);
			// This limit is on the *internal* speed, and since applied movement is capped at 8 regardless, it
			// costs nothing in travel - it only governs how long a launch keeps rising at that cap. The
			// original's own limit is 32 of its units, read straight off a pole launch: it assigns 40.125 and
			// the very next tick reports exactly 32.00. That is 37.33 here, which is also precisely a blue
			// spring's own launch, so a spring fits without being truncated while a pole is clamped the way
			// the original clamps it. An earlier 32 *here* was below the spring and cost it 5.33 on the tick
			// it fired; a later 64 cleared everything but then never clamped the pole either.
			_verticalSpeedLimit = ((_inTubeTime > 0.0f || _speed.Y < -16.0f) ? LegacyVerticalSpeedLimit : 16.0f);
			// The original's limit is the same 32 px/tick on both axes, so this is LegacyVerticalSpeedLimit as
			// well. Pinning it to the dash instead (the engine default 16, lifted to LegacyDashSpeed's 18.67)
			// looked right - the dash is the fastest thing the player *steers* - but every horizontal launch is
			// assigned above it and was then clamped away on the very next tick: green and blue horizontal
			// springs (28 and 37.33) both came back down to 18.67, which is exactly a red one, and a pole
			// launch could never reach the 20 px/tick ceiling it is clamped to. A sidekick outruns even the
			// original's limit - Lori's is nearly 50 - so it keeps the room it needs, the same way it is exempt
			// from the applied cap below.
			_horizontalSpeedLimit = (_currentSpecialMove == SpecialMoveType::Sidekick
				? std::max(LegacyLoriSidekickSpeed, LegacySpazSidekickSpeed)
				: LegacyVerticalSpeedLimit);
			// ...but only half of that is ever actually travelled: applied movement is capped the same way the
			// rise is. The full momentum still matters wherever the speed itself is read, notably the jump
			// launch boost, which is why a dashing jump launches at -14 while moving at the cap. This holds for
			// spring and pole launches too - measured, a blue spring assigns a speed of 32 and still only
			// climbs at 8.
			// Lori's kick is the one move that outruns the cap - measured, its last tick alone travels 42 px -
			// so hers is exempt. Spaz's is not: his speed sits at a flat 16 while he visibly travels 8, which
			// is the cap doing its job. In both cases the dash covers a *fixed* distance, so the cap is also
			// reused to keep the final frame from overshooting it.
			if (_currentSpecialMove == SpecialMoveType::Sidekick && _sidekickDistanceLeft > 0.0f) {
				float remainingPerTick = _sidekickDistanceLeft / std::max(timeMult, 0.01f);
				_maxAppliedSpeedX = (_playerType == PlayerType::Lori
					? remainingPerTick
					: std::min(LegacyAppliedSpeedCap, remainingPerTick));

				// Her kick accelerates from a sixth of its peak rather than starting there
				if (_playerType == PlayerType::Lori) {
					_sidekickTime += timeMult;
					_speed.X = std::copysign(LegacyLoriKickRamp * _sidekickTime * _sidekickTime, _speed.X);
				}
			} else if (_inTubeTime > 0.0f) {
				// A sucker tube drives the player directly and asks for speeds well above the cap (a level can
				// set 20 or more), so it is exempt here for the same reason the vertical limit is exempt above -
				// otherwise a horizontal tube crawls at less than half the rate the level asked for.
				_maxAppliedSpeedX = std::max(LegacyAppliedSpeedCap, std::abs(_speed.X));
			} else if (GetAccBeltStrength() != 0) {
				// An accelerating belt is exempt for the same reason: measured, a strength-8 one moves the
				// original's player 12 px/tick, straight through a cap of 8. The plain belt and the wind
				// need no exemption because they move the *position* and never touch the speed at all.
				_maxAppliedSpeedX = std::max(LegacyAppliedSpeedCap, std::abs(_speed.X));
			} else {
				_maxAppliedSpeedX = LegacyAppliedSpeedCap;
			}
			// Only the player gets the original's landing allowance - it is measured off the player, and the
			// shared collision code would otherwise hand it to every enemy and pickup in the level as well
			_landingTolerance = LegacyLandingTolerance;
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

		// The original's rise never overshoots the apex by more than one tick of fall gravity. Where the
		// deceleration would carry the speed further than that, it ends the rise at zero instead and only
		// gravity is added, so the first falling tick reads exactly +0.125 however fast the rise was being
		// braked. Measured across `ap_r01`..`ap_r30`: from -0.375 a released rise lands on +0.250, which is
		// one step past zero and kept, but from -0.250 and -0.125 it lands on +0.125 rather than the +0.375
		// and +0.500 the deceleration alone would give.
		//
		// Without this the engine carries a full frame of the heavy released rate across zero - up to +0.94
		// in the original's units - and that overshoot is not a one-tick artefact: it becomes a permanent
		// offset on the whole descent that follows, which is what left `sp_dj_r50_d25` 11 px short.
		//
		// The guard has to be tight, because plenty of things assign an upward or downward speed during the
		// move: this only fires when the frame *started* the player rising and ended with a small positive
		// speed that nothing but gravity could have produced.
		if (!_levelHandler->IsReforged() && _frameStartSpeedY < 0.0f &&
			(GetState() & ActorState::ApplyGravitation) == ActorState::ApplyGravitation) {
			float oneStep = LegacyFallGravity * timeMult;
			if (_speed.Y > oneStep && _speed.Y <= LegacyRiseGravityReleased * timeMult) {
				_speed.Y = oneStep;
			}
		}

		if ((GetState() & ActorState::ApplyGravitation) != ActorState::ApplyGravitation) {
			_externalForce.Y = std::min(_externalForce.Y + 0.002f * timeMult, 0.0f);
		}

		// Original JJ2 terminal velocity. Only the downward cap is applied,
		// so the upward jump impulse keeps its momentum.
		//
		// A buttstomp descends at its own, lower speed, and the original *governs* it: read off a long drop,
		// its `ys` sits at exactly 10.0 for every tick of the descent. Assigning the speed once and letting
		// gravity take over from there - which is what the transition callback does - accelerates instead,
		// so a stomp entered from a short hop was about right while one entered from a long fall ran away to
		// the ordinary fall cap, 20% too fast, and hit whatever was below that much harder.
		if (!_levelHandler->IsReforged()) {
			float fallCap = (_currentSpecialMove == SpecialMoveType::Buttstomp ? LegacyButtstompSpeed : LegacyFallSpeedCap);
			if (_speed.Y > fallCap) {
				_speed.Y = fallCap;
			}
		}

		if (params.TilesDestroyed > 0) {
			AddScore(params.TilesDestroyed * 50);
			_levelHandler->PlayerExecuteRumble(this, "BreakTile"_s);
		}

		OnUpdateHitbox();

		//UpdateFrozenState(timeMult);
		if (_renderer.AnimPaused) {
			if (_frozenTimeLeft <= 0.0f) {
				_renderer.AnimPaused = false;
				_renderer.Initialize(ActorRendererType::Default);

				for (std::int32_t i = 0; i < 10; i++) {
					Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 10), Explosion::Type::IceShrapnel);
				}

				Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 90), Explosion::Type::SmokeWhite);

				_levelHandler->PlayCommonSfx("IceBreak"_s, Vector3f(_pos.X, _pos.Y, 0.0f));
				_levelHandler->PlayerExecuteRumble(this, "Hurt"_s);
			} else {
				// Cannot be directly in `ActorBase::HandleFrozenStateChange()` due to bug in `BaseSprite::updateRenderCommand()`,
				// it would be called before `BaseSprite::updateRenderCommand()` but after `SceneNode::transform()`
				_renderer.Initialize(ActorRendererType::FrozenMask);
				_frozenTimeLeft -= timeMult;
			}
		}
	}

	void Player::OnUpdateTimers(float timeMult)
	{
		// Invulnerability
		if (_invulnerableTime > 0.0f) {
			_invulnerableTime -= timeMult;

			if (_invulnerableTime <= 0.0f) {
				SetState(ActorState::IsInvulnerable, false);
				_renderer.setDrawEnabled(true);
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
							Vector2i size = res->Base->FrameDimensions;

							Tiles::TileMap::DestructibleDebris debris = { };
							debris.Pos = _pos;
							debris.Depth = _renderer.layer() - 2;
							debris.Size = Vector2f((float)size.X, (float)size.Y);
							debris.Speed = Vector2f::Zero;
							debris.Acceleration = Vector2f::Zero;

							debris.Scale = 0.9f;
							debris.ScaleSpeed = -0.014f;
							debris.Alpha = 0.7f;
							debris.AlphaSpeed = -0.016f;

							debris.Time = 160.0f;

							debris.TexScaleX = (size.X / float(texSize.X));
							debris.TexBiasX = 0.0f;
							debris.TexScaleY = (size.Y / float(texSize.Y));
							debris.TexBiasY = 0.0f;

							debris.DiffuseTexture = res->Base->TextureDiffuse.get();
							// Recolor through the palette when the sprite is indexed (-1 = baked/RGBA, behavior unchanged)
							debris.PaletteOffset = (((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);
							debris.Flags = Tiles::TileMap::DebrisFlags::AdditiveBlending;

							tilemap->CreateDebris(debris);
						}
					}
				}
			} else if (_invulnerableBlinkTime >= 0.0f && (_currentTransition == nullptr || _currentTransition->State != AnimState::Hurt)) {
				_invulnerableBlinkTime -= timeMult;
				if (_invulnerableBlinkTime <= 0.0f) {
					_renderer.setDrawEnabled(!_renderer.isDrawEnabled());
					_invulnerableBlinkTime = 3.0f;
				}
			} else {
				_renderer.setDrawEnabled(true);
			}
		}

		if (_controllableTimeout > 0.0f) {
			_controllableTimeout -= timeMult;

			if (_controllableTimeout <= 0.0f) {
				_controllable = true;

				if (_isAttachedToPole) {
					// Something went wrong, detach and try to continue
					// To prevent stucking
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

		if (_jumpTime > 0.0f) {
			_jumpTime -= timeMult;
		}
		if (_externalForceCooldown > 0.0f) {
			_externalForceCooldown -= timeMult;
		}
		if (_springCooldown > 0.0f) {
			_springCooldown -= timeMult;
		}
		if (_uppercutTimeLeft > 0.0f) {
			_uppercutTimeLeft -= timeMult;
			if (_uppercutTimeLeft < 0.0f) {
				_uppercutTimeLeft = 0.0f;
			}
		}
		// Runs before HandleSpecialJump() does, which is what makes this count the same way the original's
		// does: armed on the tick of the release and already one lower by the time the next tick reads it
		if (_doubleJumpWindowLeft > 0.0f) {
			_doubleJumpWindowLeft -= timeMult;
		}
		if (_onPinballPaddleTime > 0.0f) {
			_onPinballPaddleTime -= timeMult;
		}
		if (_rfBlastLeft > 0.0f) {
			_rfBlastLeft -= timeMult;
			if (_rfBlastLeft <= 0.0f) {
				_rfBlastLeft = 0.0f;
				// When the hold ends the original snaps the speed to the walk cap and lets the ordinary
				// walking deceleration take it from there - the same ending as Spaz's sidekick, and the
				// reason the throw covers ~302 px rather than the 240 the held part alone would give
				_speed.X = std::clamp(_speed.X, -LegacyWalkSpeed, LegacyWalkSpeed);
			}
		}
		if (_weaponCooldown > 0.0f) {
			_weaponCooldown -= timeMult;
		}
		if (_bonusWarpTimer > 0.0f) {
			_bonusWarpTimer -= timeMult;
		}
		if (_lastPoleTime > 0.0f) {
			_lastPoleTime -= timeMult;
		}
		if (_gemsTimer > 0.0f) {
			_gemsTimer -= timeMult;

			if (_gemsTimer <= 0.0f) {
				_gemsPitch = 0;
			}
		}

		if (_waterCooldownLeft > 0.0f) {
			_waterCooldownLeft -= timeMult;
		}

		// Weapons
		if (_fireFramesLeft > 0.0f) {
			_fireFramesLeft -= timeMult;

			if (_fireFramesLeft <= 0.0f) {
				// Play post-fire animation
				if ((_currentAnimation->State & (AnimState::Walk | AnimState::Run | AnimState::Dash | AnimState::Buttstomp | AnimState::Swim | AnimState::Airboard | AnimState::Lift | AnimState::Spring)) == AnimState::Idle &&
					(_currentTransition == nullptr || (_currentTransition->State != AnimState::TransitionRunToIdle && _currentTransition->State != AnimState::TransitionDashToIdle)) &&
					!_isAttachedToPole) {

					if ((_currentAnimation->State & AnimState::Hook) == AnimState::Hook) {
						SetTransition(AnimState::TransitionHookShootToHook, false);
					} else if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
						SetAnimation(AnimState::Copter);
						SetTransition(AnimState::TransitionCopterShootToCopter, false);
					} else if ((_currentAnimation->State & AnimState::Fall) == AnimState::Fall) {
						SetTransition(AnimState::TransitionFallShootToFall, false);
					} else if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
						SetAnimation(AnimState::Crouch, true);
					} else {
						SetTransition(AnimState::TransitionShootToIdle, false);
					}
				}
			}
		}
		if (_weaponFlareTime > 0.0f) {
			_weaponFlareTime -= timeMult;
		}

		// Dizziness
		if (_dizzyTime > 0.0f) {
			_dizzyTime -= timeMult;
		}

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

					auto* tilemap = _levelHandler->TileMap();
					if (tilemap != nullptr) {
						auto* res = _metadata->FindAnimation(SugarRush);
						if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
							Vector2i texSize = res->Base->TextureDiffuse->GetSize();
							Vector2i size = res->Base->FrameDimensions;
							Vector2i frameConf = res->Base->FrameConfiguration;
							std::int32_t frame = res->FrameOffset + Random().Next(0, res->FrameCount);
							float speedX = Random().FastFloat(-4.0f, 4.0f);

							Tiles::TileMap::DestructibleDebris debris = { };
							debris.Pos = _pos;
							debris.Depth = _renderer.layer() - 2;
							debris.Size = Vector2f((float)size.X, (float)size.Y);
							debris.Speed = Vector2f(speedX, Random().FastFloat(-4.0f, -2.2f));
							debris.Acceleration = Vector2f(0.0f, 0.2f);

							debris.Scale = Random().FastFloat(0.1f, 0.5f);
							debris.ScaleSpeed = -0.002f;
							debris.Angle = Random().FastFloat(0.0f, fTwoPi);
							debris.AngleSpeed = speedX * 0.04f;
							debris.Alpha = 1.0f;
							debris.AlphaSpeed = -0.018f;

							debris.Time = 160.0f;

							debris.TexScaleX = (size.X / float(texSize.X));
							debris.TexBiasX = ((float)(frame % frameConf.X) / frameConf.X);
							debris.TexScaleY = (size.Y / float(texSize.Y));
							debris.TexBiasY = ((float)(frame / frameConf.X) / frameConf.Y);

							debris.DiffuseTexture = res->Base->TextureDiffuse.get();
							// Recolor through the palette when the sprite is indexed (-1 = baked/RGBA, behavior unchanged)
							debris.PaletteOffset = (((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);

							tilemap->CreateDebris(debris);
						}
					}
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
				_copterSound->setPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_copterSound->stop();
				_copterSound = nullptr;
			}
		}
		if (_airboardSound != nullptr) {
			if ((_currentAnimation->State & AnimState::Airboard) == AnimState::Airboard) {
				_airboardSound->setPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_airboardSound->stop();
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

			if ((_keepRunningTime > 0.0f || _speed.SqrLength() > (_isRunPressed ? 36.0f : 100.0f)) && _inTubeTime <= 0.0f) {
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

				// The original does not hand the tube's speed back: when the window lapses it clamps to the
				// walk cap and coasts from there. Measured on a tube set to 20 px/tick, where `xs` reads a
				// flat 20 until the window ends and then **4.0** on the very next tick. Without it the player
				// keeps the whole tube speed, which took that tube 1066.9 px against the original's 407.5.
				if (!_levelHandler->IsReforged()) {
					_speed.X = std::clamp(_speed.X, -LegacyWalkSpeed, LegacyWalkSpeed);
				}
			} else {
				// Skip controls, player is not controllable in tube
#if defined(WITH_AUDIO)
				// Weapons are automatically disabled if player is not controllable
				if (_weaponSound != nullptr) {
					_weaponSound->stop();
					_weaponSound = nullptr;
				}
#endif
			}
		}
	}

	void Player::OnHandleMovement(float timeMult, bool areaWeaponAllowed, bool canJumpPrev)
	{
		// Move
		if (PreferencesCache::ToggleRunAction) {
			if (_levelHandler->PlayerActionHit(this, PlayerAction::Run)) {
				_isRunPressed = !_isRunPressed;
			}
		} else {
			_isRunPressed = _levelHandler->PlayerActionPressed(this, PlayerAction::Run);
		}

		if (_health <= 0) {
			return;
		}

		// There was an "original-style ledge hop" here - a small upward boost on running off a ledge, so the
		// player would clear a gap instead of dropping into it. Measured over gaps of one to four tiles, the
		// original gives **no** such boost: running off the edge, `ys` goes straight into the fall gravity
		// (0, 0.125, 0.250, ... ) and the player crosses a four-tile gap having dropped under 10 px, landing
		// on the far edge purely because 8 px of travel a tick outruns the fall. A walk drops far enough to
		// miss the far edge of a three-tile gap and falls in, which is the same arithmetic and not a
		// separate rule. So gap-clearing needs no code at all: the applied speed cap and the fall gravity
		// already produce it, and both are measured exactly.

		HandleHorizontalMovement(timeMult);

		if (_hitFloorTime > 0.0f) {
			_hitFloorTime -= timeMult;
		}

		if (!_controllable || !_controllableExternal || _currentSpecialMove == SpecialMoveType::Buttstomp) {
#if defined(WITH_AUDIO)
			// Weapons are automatically disabled if player is not controllable
			if (_currentWeapon != WeaponType::Thunderbolt || _fireFramesLeft <= 0.0f) {
				if (_weaponSound != nullptr) {
					_weaponSound->stop();
					_weaponSound = nullptr;
				}
			}
#endif
			return;
		}

		if (_inWater || _activeModifier != Modifier::None) {
			HandleWaterAndModifierMovement(timeMult);
		} else {
			HandleLookupAndCrouch(timeMult, canJumpPrev);
			HandleJump(timeMult);
		}

		HandleWeaponFire(areaWeaponAllowed);
	}

	bool Player::IsDashActive() const
	{
		return (_isRunPressed || _dashGraceLeft > 0.0f);
	}

	std::int32_t Player::GetSlideStrength() const
	{
		// Only underfoot counts: it is a floor event, read one tile down like a belt is, and it cannot apply
		// to a player who is not standing on it
		if (!GetState(ActorState::CanJump)) {
			return -1;
		}

		std::uint8_t* p;
		if (_levelHandler->EventMap()->GetEventByPosition(_pos.X, _pos.Y + 32, &p) != EventType::ModifierSlide) {
			return -1;
		}

		// The parameter is 2 bits wide, so anything else would be a malformed level - but the arithmetic
		// below turns a large value into a *negative* brake, which would accelerate the player for ever
		return std::min((std::int32_t)p[0], 3);
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

		// Slots 2 and 3 of an `AreaHForce` are the accelerating belt, left and right - see EventConverter.cpp
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

		// The original's dash doesn't bleed away when Run is let go - it keeps the full speed for its grace and
		// then drops to the walk cap in a single tick, in mid-air just the same as on the ground
		_dashGraceLeft -= timeMult;
		if (_dashGraceLeft <= 0.0f) {
			_dashGraceLeft = 0.0f;
			// Only the dash's *own* speed is dropped. Anything still carrying the player owns the horizontal
			// speed for its whole length and has nothing to do with the Run key - a spring or pole launch
			// (`_keepRunningTime`), a sidekick, an RF blast - so clamping here would cut a launch the player
			// cannot steer down to walking pace a quarter of a second in, and make how far it goes depend on
			// whether Run happened to be held on the way into it.
			if (_keepRunningTime <= 0.0f && _rfBlastLeft <= 0.0f && _currentSpecialMove != SpecialMoveType::Sidekick) {
				_speed.X = std::clamp(_speed.X, -LegacyWalkSpeed, LegacyWalkSpeed);
			}
		}
	}

	void Player::ResetLegacyMovementState()
	{
		// `_dashGraceLeft` is advanced by UpdateDashState(), reached only through HandleHorizontalMovement(),
		// which OnHandleMovement() skips while `_health <= 0` and OnUpdate() skips for the whole warp-in
		// window - so a player who died or warped mid-dash came back with the grace still at full value and
		// dashed for a moment with nothing held, then had the speed clamped out from under them.
		_dashGraceLeft = 0.0f;
		// Likewise `_rfBlastLeft`, which suppresses all horizontal handling while it lasts: a warp during a
		// blast left the player unable to steer for the rest of the hold on the far side of it.
		_rfBlastLeft = 0.0f;
		// A kick's remaining distance never belongs to the next life or the far side of a warp
		_sidekickDistanceLeft = 0.0f;
		_sidekickTime = 0.0f;
		// Nothing is rising, so the next ascent must pick its own gravity rather than inherit this one
		_jumpReleased = false;
		_isSpring = false;
		_poleEnteredOnSpring = false;
	}

	void Player::HandleHorizontalMovement(float timeMult)
	{
		UpdateDashState(timeMult);

		if (_keepRunningTime <= 0.0f) {
			// While an RF blast is carrying the player, nothing touches the horizontal speed - not friction,
			// not the direction keys. See ApplyBlastKnockback(). Only this branch is skipped: the
			// `_keepRunningTime` branch below merely reads the speed, and bailing out of the whole function
			// also froze that countdown - the only one in the class - so a spring's no-control window grew by
			// the length of the blast and the push/facing state stayed latched wherever the blast caught it.
			if (_rfBlastLeft > 0.0f) {
				return;
			}

			bool canWalk = (_controllable && _controllableExternal && !_isLifting && _suspendType != SuspendType::SwingingVine &&
				(_playerType != PlayerType::Frog || !_levelHandler->PlayerActionPressed(this, PlayerAction::Fire)));

			float playerMovement = _levelHandler->PlayerHorizontalMovement(this);
			float playerMovementVelocity = std::abs(playerMovement);
			if (_currentSpecialMove == SpecialMoveType::Buttstomp) {
				if (_levelHandler->IsReforged()) {
					_speed.X = 0.2f * playerMovement;
					if (_isRunPressed) {
						_speed.X *= 2.6f;
					}
				} else if (playerMovementVelocity > 0.4f) {
					// Measured: a stomp drops whatever speed was carried into it and then drifts sideways at a
					// flat rate, the same through the pause at the top and the descent, doubled while Run is
					// held. Assigned as a speed here; the original moves the position directly and leaves its
					// xSpeed at zero, but nothing reads the speed during a stomp so the result is the same.
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
					// If player changed direction, reset push frames to prevent pushing animation
					_pushFramesLeft = 0.0f;
#if defined(WITH_AUDIO)
					if (_activeModifier == Modifier::Airboard) {
						_airboardTurnSound = PlayPlayerSfx("AirboardTurnStart"_s, 2.0f, 0.8f);
					}
#endif
				}

				_isActivelyPushing = _wasActivelyPushing = true;

				// The original has a single acceleration for everything - same on the ground and in the air,
				// applied in the direction of the input whichever way the player is moving - and it depends
				// only on whether the dash is up. Turning around is therefore symmetric and keeps its momentum,
				// which is exactly what makes it slow in the air.
				float acceleration = (_levelHandler->IsReforged() ? Acceleration
					: (IsDashActive() ? LegacyDashAccel : LegacyWalkAccel));

				if (_dizzyTime > 0.0f || _playerType == PlayerType::Frog) {
					_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -MaxDizzySpeed * playerMovementVelocity, MaxDizzySpeed * playerMovementVelocity);
				} else if (_inShallowWater != -1 && _levelHandler->IsReforged() && _playerType != PlayerType::Lori) {
					// Use lower speed in shallow water if Reforged
					// Also, exclude Lori, because she can't ledge climb or double jump (rescue/01_colon1)
					_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -MaxShallowWaterSpeed * playerMovementVelocity, MaxShallowWaterSpeed * playerMovementVelocity);
				} else {
					if (_suspendType == SuspendType::None && !_inWater && (_levelHandler->IsReforged() ? _isRunPressed : IsDashActive())) {
						float maxDashSpeed = (_levelHandler->IsReforged() ? MaxDashingSpeed : LegacyDashSpeed);
						_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -maxDashSpeed * playerMovementVelocity, maxDashSpeed * playerMovementVelocity);
					} else if (_suspendType == SuspendType::Vine) {
						if (_wasFirePressed) {
							_speed.X = 0.0f;
						} else {
							// If Run is pressed, player moves faster on vines - measured as exactly double,
							// which takes it from half the walk cap up to the walk cap
							float maxVineSpeed = (_levelHandler->IsReforged() ? MaxVineSpeed : LegacyVineSpeed);
							if (_isRunPressed) {
								maxVineSpeed *= (_levelHandler->IsReforged() ? 1.6f : 2.0f);
							}
							_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -maxVineSpeed * playerMovementVelocity, maxVineSpeed * playerMovementVelocity);
						}
					} else if (_suspendType != SuspendType::Hook) {
						float maxRunSpeed = (_levelHandler->IsReforged() ? MaxRunningSpeed : LegacyWalkSpeed);
						_speed.X = std::clamp(_speed.X + acceleration * timeMult * (isFacingLeft ? -1 : 1), -maxRunSpeed * playerMovementVelocity, maxRunSpeed * playerMovementVelocity);
					}
				}

				if (CanJump()) {
					_wasUpPressed = _wasDownPressed = false;
				}
			} else if (_inTubeTime <= 0.0f && _currentSpecialMove != SpecialMoveType::Sidekick) {
				// A sidekick covers a fixed distance and owns its speed until that distance runs out (see
				// CheckEndOfSpecialMoves()), so it must not be braked here. It reaches this branch because the
				// move takes control away, which makes `canWalk` false with nothing held - and with the dash
				// brake being ~3.8x the walk brake, a kick begun with Run still down bled out after ~225 px of
				// its measured 440, leaving the rest of the distance armed to fire later.
				float absSpeedX = std::abs(_speed.X);
				// With nothing held the original coasts to a stop, braking harder while the dash is still up -
				// and braking *less* on a slide tile, which is the whole of what that event does. It adds no
				// speed of its own; it only swaps the brake, so a player who lets go slides further.
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
				_speed.X = std::max((absSpeedX - deceleration * timeMult), 0.0f) * (_speed.X < 0.0f ? -1.0f : 1.0f);

				// An accelerating belt is applied *after* the brake, and that ordering is the measurement: the
				// original ramps xs 2.0, 3.878, 5.756 and then holds exactly 6.000, which is a step of half
				// the strength added on top of a friction step of 0.122 each tick, clamped to the target. Adding
				// it before the brake instead - which is where the floor-event handler runs - leaves the steady
				// state one friction step short, at 5.862.
				if (!_levelHandler->IsReforged()) {
					std::int32_t accBelt = GetAccBeltStrength();
					if (accBelt != 0) {
						float target = accBelt * LegacyAccBeltSpeed;
						_speed.X = std::clamp(_speed.X + accBelt * LegacyAccBeltStep * timeMult,
							std::min(target, 0.0f), std::max(target, 0.0f));
					} else if (_wasOnAccBelt) {
						// Leaving one clamps to the walk cap, exactly as leaving a sucker tube does. Measured:
						// the original coasts ~87 px past the end of a belt where keeping the belt speed carries
						// the player 182, and 560 past on a strength-8 one.
						_speed.X = std::clamp(_speed.X, -LegacyWalkSpeed, LegacyWalkSpeed);
					}
					_wasOnAccBelt = (accBelt != 0);
				}

				_isActivelyPushing = false;

				absSpeedX = std::abs(_speed.X);
				if (absSpeedX > 4.0f) {
					SetFacingLeft(_speed.X < 0.0f);
				} else if (absSpeedX < 0.001f) {
					_wasActivelyPushing = false;
				}
			}
		} else {
			_keepRunningTime -= timeMult;

			_isActivelyPushing = _wasActivelyPushing = true;

			float absSpeedX = std::abs(_speed.X);
			if (absSpeedX > 1.0f) {
				SetFacingLeft(_speed.X < 0.0f);
			} else if (absSpeedX < 1.0f) {
				_keepRunningTime = 0.0f;
			}
		}
	}

	void Player::HandleWaterAndModifierMovement(float timeMult)
	{
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

		if (_activeModifier == Modifier::LizardCopter && _levelHandler->PlayerActionHit(this, PlayerAction::Jump)) {
			// Allow to jump off the copter
			// TODO: Copter shouldn't diappear immediately
			SetModifier(Modifier::None);
			// Don't trigger buttstomp or copter ears when pressing down
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

		// Crouch / Buttstomp - Uses different bindings whether it's in the air or not
		if (_levelHandler->PlayerActionPressed(this, CanJump() ? PlayerAction::Down : PlayerAction::Buttstomp)) {
			if (_suspendType == SuspendType::SwingingVine) {
				// TODO: Swinging vine
			} else if (_suspendType != SuspendType::None) {
				// Jump off vine/hook
				if (IsContinuousJumpAllowed() || _levelHandler->PlayerActionHit(this, CanJump() ? PlayerAction::Down : PlayerAction::Buttstomp)) {
					_wasDownPressed = true;

					MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative | MoveType::Force);
					_suspendType = SuspendType::None;
					// The grab box reaches above the player, so dropping down has to be ignored long enough
					// for gravity to carry them clear of the vine they just let go of - the 4 px above are
					// nowhere near enough on their own
					_suspendTime = 12.0f;

					SetState(ActorState::ApplyGravitation, true);
				}
			} else if (_dizzyTime <= 0.0f) {
				// Check also previous CanJump to avoid animation glitches on Springs
				if (canJumpPrev && CanJump()) {
					if (!_isLifting && std::abs(_speed.X) < std::numeric_limits<float>::epsilon()) {
						_wasDownPressed = true;
						if (_fireFramesLeft > 0.0f) {
							SetAnimation(AnimState::Crouch | AnimState::Shoot);
						} else {
							SetAnimation(AnimState::Crouch);
						}
					}
				} else if (!CanJump() && !_wasDownPressed && _playerType != PlayerType::Frog) {
					_wasDownPressed = true;

					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					_internalForceY = 0.0f;
					_externalForce.Y = 0.0f;
					SetState(ActorState::ApplyGravitation, false);
					SetAnimation(AnimState::Buttstomp);
					SetPlayerTransition(AnimState::TransitionButtstompStart, true, false, SpecialMoveType::Buttstomp, [this]() {
						// Measured: the original's buttstomp falls at exactly 10 px/tick, the same for all three
						// characters, and it is well under the applied cap so it really does descend that fast
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

						// If we're jumping out from under another player (not a solid object), bump out sideways too,
						// so we arc away and don't just fall straight back onto them into the lift state again
						for (auto* other : _levelHandler->GetPlayers()) {
							if (other != this && other->_stackCarrying && other->_carryingObject == this) {
								_speed.X = (_pos.X <= other->GetPos().X ? -1.0f : 1.0f) * PlayerBumpMinSeparationSpeed;
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
			}

			if (_suspendType != SuspendType::None) {
				// Drop off hook/vine
				if (IsContinuousJumpAllowed() || _levelHandler->PlayerActionHit(this, PlayerAction::Jump)) {
					if (_suspendType == SuspendType::SwingingVine) {
						CancelCarryingObject();
						_springCooldown = 30.0f;
					} else {
						MoveInstantly(Vector2(0.0f, -4.0f), MoveType::Relative | MoveType::Force);
					}
					SetState(ActorState::CanJump, true);
					_canDoubleJump = true;
				}
			}

			if (!CanJump()) {
				// Extend copter time
				if (_copterFramesLeft > 0.0f) {
					SetCopterFlight(70.0f, IsFlyCheatActive() ? FlightType::Cheat : FlightType::Normal);
				}
			// The Down gate is NOT what the original uses, but dropping it for non-Reforged was tried and is
			// worse, so it stays until the real rule is known. Measured with Down and Jump both held through
			// a sidekick: Spaz jumps - at tick 111, launching -13.939, so with the kick's ~15.7 of
			// horizontal speed still feeding the launch boost, and with the special move already over -
			// while Lori never jumps at all. So the original is gating on some crouch-or-coast state rather
			// than on the key, and the two characters differ because his kick runs 81 ticks and hers 5.
			// Ignoring the key outright gets Spaz 147.8 px against his 216 and gives Lori 128.3 where the
			// original gives 0, which is one mismatch traded for another. See the reference page.
			} else if (_currentSpecialMove == SpecialMoveType::None && _jumpTime <= 0.0f && !_levelHandler->PlayerActionPressed(this, PlayerAction::Down)) {
				// Standard jump
				if (IsContinuousJumpAllowed() || _levelHandler->PlayerActionHit(this, PlayerAction::Jump)) {
					SetState(ActorState::CanJump, false);
					_isFreefall = false;
					SetAnimation(_currentAnimation->State & (~AnimState::Lookup & ~AnimState::Crouch));
					PlayPlayerSfx("Jump"_s);
					_jumpTime = 10.0f;
					_carryingObject = nullptr;

					// Gravitation is sometimes off because of active copter, turn it on again
					SetState(ActorState::ApplyGravitation, true);
					SetState(ActorState::IsSolidObject, false);

					if (_levelHandler->IsReforged()) {
						_speed.Y = -3.6f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f);
						_internalForceY = -1.02f - 0.07f * (1.0f - timeMult);
						if (_playerType == PlayerType::Lori) {
							_speed.Y *= 1.3f;
						}
					} else {
						// The original's instant launch impulse, measured: a flat -10 plus a quarter of whatever
						// horizontal speed is being carried, with no threshold - so a standing jump leaves at -10
						// and a full dash at -14. The applied rise cap then holds the ascent at a constant speed
						// for the first frames, which is the original's feel.
						_speed.Y = -(LegacyJumpSpeed + std::abs(_speed.X) * LegacySpeedJumpScale);
						_jumpReleased = false;
					}
				}
			}
		} else {
			if (_wasJumpPressed) {
				_wasJumpPressed = false;

				// A Reforged jump rises on an internal force, so clearing that is all there is to it. Spaz's
				// double jump uses the internal force in both modes, which is why this is cleared either way:
				// without it the double jump always reached its full height, with no way to make a shorter
				// one. It stays outside the `_isSpring` guard below for exactly that reason - putting it
				// inside meant a double jump taken into a spring could no longer be shortened at all.
				if (_internalForceY < 0.0f) {
					_internalForceY = 0.0f;
				}

				// The original-style jump rises on speed alone and is shortened by the rest of the ascent
				// becoming heavier instead - see GetGravityModifier(). Releasing jump early cuts that ascent,
				// but only one the player is responsible for: a spring throws them up whether they touch the
				// button or not, so tapping jump on the way up must not shorten it (`_isSpring` is cleared the
				// moment they start falling).
				if (!_isSpring) {
					_jumpReleased = true;
				}

				// Letting go is also what opens the double-jump window - see LegacyDoubleJumpWindowTime
				_doubleJumpWindowLeft = LegacyDoubleJumpWindowTime;
			}
		}
	}

	void Player::HandleSpecialJump(float timeMult)
	{
		switch (_playerType) {
			case PlayerType::Jazz: {
				if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
					_controllable = false;
					SetAnimation(AnimState::Uppercut);
					SetPlayerTransition(AnimState::TransitionUppercutA, true, true, SpecialMoveType::Uppercut, [this]() {
						if (_levelHandler->IsReforged()) {
							_externalForce.Y = -1.4f;
							_speed.Y = -2.0f;
						} else {
							// Measured: a plain speed assignment, and the height comes from gravity being nearly
							// switched off for a fixed number of ticks rather than from a sustained force. Arming
							// the countdown here rather than at the trigger is what makes the ~14-tick wind-up
							// (during which the original does not move at all) come out right.
							_speed.Y = -LegacyUppercutSpeed;
							_externalForce.Y = 0.0f;
							_uppercutTimeLeft = LegacyUppercutTicks;
						}
						SetState(ActorState::CanJump, false);
						SetPlayerTransition(AnimState::TransitionUppercutB, true, true, SpecialMoveType::Uppercut);
					});
				} else {
					if (_speed.Y > 0.01f && !CanJump() && (_currentAnimation->State & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
						SetState(ActorState::ApplyGravitation, false);
						_speed.Y = (_levelHandler->IsReforged() ? 1.5f : LegacyCopterDescentSpeed);
						_externalForce.Y = 0.0f;
						if ((_currentAnimation->State & AnimState::Copter) != AnimState::Copter) {
							SetAnimation(AnimState::Copter);
						}
						SetCopterFlight(70.0f, FlightType::Normal);
#if defined(WITH_AUDIO)
						if (_copterSound == nullptr) {
							_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
							if (_copterSound != nullptr) {
								_copterSound->setLooping(true);
							}
						}
#endif
					}
				}
				break;
			}
			case PlayerType::Spaz: {
				if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
					_controllable = false;
					_controllableTimeout = (_levelHandler->IsReforged() ? 60.0f : 120.0f);
					SetAnimation(AnimState::Uppercut);
					SetPlayerTransition(AnimState::TransitionUppercutA, true, false, SpecialMoveType::Sidekick, [this]() {
						_externalForce.X = 8.0f * (IsFacingLeft() ? -1.0f : 1.0f);
						_speed.X = (_levelHandler->IsReforged() ? 14.4f : LegacySpazSidekickSpeed) * (IsFacingLeft() ? -1.0f : 1.0f);
						// The dash begins here, after the wind-up, so this is where its length is armed. It is
						// counted down in CheckEndOfSpecialMoves() rather than left to `_controllableTimeout`,
						// which is only read a frame later and so overshot badly at low frame rates.
						if (!_levelHandler->IsReforged()) {
							_sidekickDistanceLeft = LegacySpazSidekickDistance;
							_sidekickTime = 0.0f;
						}
						// The dash starts from a crouch, so the player is standing and `CanJump()` is true. Gravity
						// is off for its duration, which means nothing clears that flag again (only the gravity
						// handling does) and the player would still count as grounded when the dash ends in mid-air
						// - good for a full jump straight out of it. Clearing it here leaves the double jump as the
						// only option, and landing re-establishes it normally.
						SetState(ActorState::CanJump, false);
						SetState(ActorState::ApplyGravitation, false);
						SetPlayerTransition(AnimState::TransitionUppercutB, true, false, SpecialMoveType::Sidekick);
					});

					PlayPlayerSfx("Sidekick"_s);
				} else {
					// The double jump assigns the vertical speed outright, so allowing it while a spring is still
					// carrying the player up would replace a launch of -16 or so with its own -0.6 and cut the arc
					// short. `_isSpring` covers exactly the rising part of a spring launch (it is cleared as soon
					// as the player starts falling), so the jump simply waits for the apex instead.
					//
					// The original also only accepts the second press while the player is falling and within
					// LegacyDoubleJumpWindowTime of letting the first press go, so a press that comes too
					// late is simply lost. Nothing else gates it - in particular not the fall speed, which
					// `sp_dj_r85_d20` and `sp_dj_r85_d25` settle: both are accepted at about 4.0 and 4.6
					// px/tick, well past the 3.75 the window was first read as.
					//
					// The falling half is tested against the speed at the *start* of the frame, not the live
					// one. The original applies its gravity after this check, so a press landing on the exact
					// apex reads 0.0000 there and is refused; reading the live value here turns that same
					// press into +0.44 and accepts it, which made `sp_spaz_dj_hold` rise 166.8 px against 132.1.
					//
					// Non-Reforged only, like the rest of this rework: in Reforged the double jump has always
					// been accepted during a spring rise and changing that is a separate decision.
					bool inDoubleJumpWindow = (_levelHandler->IsReforged() ||
						(!_isSpring && _frameStartSpeedY > 0.0f && _doubleJumpWindowLeft > 0.0f));
					if (!CanJump() && _canDoubleJump && inDoubleJumpWindow) {
						_canDoubleJump = false;
						_isFreefall = false;

						if (_levelHandler->IsReforged()) {
							_internalForceY = -1.15f - 0.1f * (1.0f - timeMult);
							_speed.Y = -0.6f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f);
							_speed.X = std::clamp(_speed.X * 0.4f, -1.0f, 1.0f);
						} else {
							// Measured: a straight speed assignment, exactly like the first jump, and the height
							// is then governed by how long the key is held through the same released-gravity
							// switch.
							//
							// The horizontal speed is thrown away outright - a dash double jump leaves at
							// zero and has to build its speed up again from the air acceleration, which is
							// what makes the move a way to *stop* as well as to climb. Measured directly:
							// `sp_dj_a_dash` reads 16.00 the tick before the press and 0.3662 - exactly one
							// dash acceleration step - the tick after, and the walking cases the same with a
							// 0.1831 step. Reforged only damps it (x0.4 clamped to 1), which is why this was
							// long read as "the speed carries over": every earlier measurement was a travel
							// to a wall that both games reach either way.
							_speed.Y = -LegacyDoubleJumpSpeed;
							_speed.X = 0.0f;
							_internalForceY = 0.0f;
							_jumpReleased = false;
						}

						PlayPlayerSfx("DoubleJump"_s);

						SetTransition(AnimState::Spring, false);
					}
				}
				break;
			}
			case PlayerType::Lori: {
				// Unlike Spaz she kicks over and over while Down and Jump are worked, and the repeats need no
				// pacing of their own: the move holds control for its whole length and the crouch has to come
				// back before the next press counts, which measures out to a kick every 45 of the original's
				// ticks against a press every 15 - exactly what the original does with that input
				if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
					_controllable = false;
					_controllableTimeout = 40.0f;
					SetAnimation(AnimState::Uppercut);
					SetPlayerTransition(AnimState::TransitionUppercutA, true, false, SpecialMoveType::Sidekick, [this]() {
						_externalForce.X = 4.0f * (IsFacingLeft() ? -1.0f : 1.0f);
						_speed.X = (_levelHandler->IsReforged() ? 9.3f : LegacyLoriSidekickSpeed) * (IsFacingLeft() ? -1.0f : 1.0f);
						// As for Spaz: the kick begins here, after the wind-up, and its length is counted down
						// where the move ends. Armed at the trigger instead, a four-tick kick expires during the
						// wind-up and dies on the tick it starts - which is what a ~5 px kick looked like.
						if (!_levelHandler->IsReforged()) {
							_sidekickDistanceLeft = LegacyLoriSidekickDistance;
							_sidekickTime = 0.0f;
						}
						// As with Spaz above, the dash would otherwise leave the player counting as grounded when
						// it ends in mid-air (Lori has copter ears rather than a double jump, so for her that
						// simply means no jump until she lands)
						SetState(ActorState::CanJump, false);
						SetState(ActorState::ApplyGravitation, false);
					});
				} else {
					if (_speed.Y > 0.01f && !CanJump() && (_currentAnimation->State & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
						SetState(ActorState::ApplyGravitation, false);
						_speed.Y = (_levelHandler->IsReforged() ? 1.5f : LegacyCopterDescentSpeed);
						_externalForce.Y = 0.0f;
						if ((_currentAnimation->State & AnimState::Copter) != AnimState::Copter) {
							SetAnimation(AnimState::Copter);
						}
						SetCopterFlight(70.0f, FlightType::Normal);
#if defined(WITH_AUDIO)
						if (_copterSound == nullptr) {
							_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
							if (_copterSound != nullptr) {
								_copterSound->setLooping(true);
							}
						}
#endif
					}
				}
				break;
			}
		}
	}

	void Player::HandleWeaponFire(bool areaWeaponAllowed)
	{
		// Fire
		bool weaponInUse = false;
		if (_weaponAllowed && areaWeaponAllowed && _levelHandler->PlayerActionPressed(this, PlayerAction::Fire)) {
			if (!_isLifting && _suspendType != SuspendType::SwingingVine && !_canPushFurther) {
				if (_playerType == PlayerType::Frog) {
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
						if (_currentTransition != nullptr && (_currentTransition->State == AnimState::Spring || _currentTransition->State == AnimState::TransitionShootToIdle)) {
							ForceCancelTransition();
						}

						SetAnimation(_currentAnimation->State | AnimState::Shoot);
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

			_weaponCooldown = 0.0f;
		}

#if defined(WITH_AUDIO)
		if (_weaponSound != nullptr) {
			if (weaponInUse) {
				_weaponSound->setPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_weaponSound->stop();
				_weaponSound = nullptr;

				if (_currentWeapon == WeaponType::Thunderbolt) {
					PlayPlayerSfx("WeaponThunderboltEnd"_s, 0.8f);
					_levelHandler->PlayerExecuteRumble(this, "Fire"_s);
				}
			}
		}
#endif
	}

	bool Player::OnDraw(RenderQueue& renderQueue)
	{
		if (_weaponFlareTime > 0.0f && !_inWater && _currentTransition == nullptr) {
			auto* res = _metadata->FindAnimation(WeaponFlare);
			if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
				// When the player is recolored its anims (incl. the flare) are indexed, so the flare must go through
				// the PaletteRemap shader too. Its colors aren't in the fur range, so it still renders yellow.
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
				std::int32_t gunspotOffsetX = (_currentAnimation->Base->Hotspot.X - _currentAnimation->Base->Gunspot.X);
				std::int32_t gunspotOffsetY = (_currentAnimation->Base->Hotspot.Y - _currentAnimation->Base->Gunspot.Y);

				float gunspotPosX, gunspotPosY;
				if (lookUp) {
					gunspotPosX = _pos.X + (gunspotOffsetX) * (facingLeft ? 1 : -1);
					gunspotPosY = _pos.Y - (gunspotOffsetY - 3) - res->Base->FrameDimensions.Y;
				} else {
					gunspotPosX = _pos.X + (gunspotOffsetX - 7) * (facingLeft ? 1 : -1);
					gunspotPosY = _pos.Y - gunspotOffsetY;
					if (facingLeft) {
						texBiasX += texScaleX;
						texScaleX *= -1.0f;
					}
				}

				if (!PreferencesCache::UnalignedViewport) {
					gunspotPosX = std::floor(gunspotPosX);
					gunspotPosY = std::floor(gunspotPosY);
				}

				auto instanceBlock = command->GetInstanceBlock();
				instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				// The quad is sized by the frame's own area - the texture rectangle above covers exactly
				// that, and stretching a trimmed frame over the whole cell would visibly distort it
				instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(frameRect.W, frameRect.H * scaleY);
				instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatValue(1.0f, 1.0f, 1.0f, 1.8f);

				Matrix4x4f worldMatrix = Matrix4x4f::Translation(gunspotPosX, gunspotPosY, 0.0f);
				if (lookUp) {
					worldMatrix.RotateZ(-fRadAngle90);
				}
				worldMatrix.Translate(frameRect.W * -0.5f, frameRect.H * scaleY * -0.5f, 0.0f);
				command->SetTransformation(worldMatrix);
				command->SetLayer(_renderer.layer() + 2);
				command->GetMaterial().SetTexture(*res->Base->TextureDiffuse.get());
				if (flareIndexed) {
					// Use the DEFAULT palette (row 0, palette offset 0) so the flare keeps its original colors even
					// when the player's fur recolor would otherwise overlap the flare's palette indices
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

		DrawShield(renderQueue, _activeShield, _activeShieldTime, _metadata, _levelHandler->GetElapsedFrames(), _pos, _renderer.layer(), _shieldRenderCommands);

		return ActorBase::OnDraw(renderQueue);
	}

	void Player::DrawShield(RenderQueue& renderQueue, ShieldType shieldType, float shieldTime, Metadata* metadata,
		float elapsedFrames, Vector2f pos, std::uint16_t baseLayer, std::unique_ptr<RenderCommand> (&shieldRenderCommands)[2])
	{
		// Local aliases so the drawing code below is shared verbatim by Player::OnDraw (which owns the live state)
		// and by the remote-player path (RemoteActor), which passes its own server-synced copies of the same values
		auto& _shieldRenderCommands = shieldRenderCommands;
		Metadata* const _metadata = metadata;
		const Vector2f _pos = pos;
		const float _activeShieldTime = shieldTime;

		switch (shieldType) {
			case ShieldType::Fire: {
				auto* res = _metadata->FindAnimation(ShieldFire);
				if (res != nullptr && res->Base->TextureDiffuse != nullptr) {
					constexpr float PosMultiplier = 0.003f;
					float frames = elapsedFrames;
					float shieldAlpha = std::min(_activeShieldTime * 0.01f, 1.0f);
					float shieldScale = std::min(_activeShieldTime * 0.016f + 0.6f, 1.0f);
					float shieldSize = 70.0f * shieldScale;

					float shieldPosX = _pos.X - shieldSize * 0.5f;
					float shieldPosY = _pos.Y - shieldSize * 0.5f;

					if (!PreferencesCache::UnalignedViewport) {
						shieldPosX = std::floor(shieldPosX);
						shieldPosY = std::floor(shieldPosY);
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
					float texScaleX = (float(frameRect.W) / float(texSize.X));
					float texBiasX = (float(frameRect.X) / float(texSize.X));
					float texScaleY = (float(frameRect.H) / float(texSize.Y));
					float texBiasY = (float(frameRect.Y) / float(texSize.Y));

					// The quad is sized by the frame's own area - the texture rectangle above covers exactly
					// that, and stretching a trimmed frame over the whole cell would visibly distort it
					float shieldPosX = _pos.X - frameRect.W * shieldScale * 0.5f;
					float shieldPosY = _pos.Y - frameRect.H * shieldScale * 0.5f;

					if (!PreferencesCache::UnalignedViewport) {
						shieldPosX = std::floor(shieldPosX);
						shieldPosY = std::floor(shieldPosY);
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
					constexpr float PosMultiplier = 0.001f;
					float frames = elapsedFrames;
					float shieldAlpha = std::min(_activeShieldTime * 0.01f, 1.0f);
					float shieldScale = std::min(_activeShieldTime * 0.016f + 0.6f, 1.0f);
					float shieldSize = 70.0f * shieldScale + sinApprox(frames * 0.06f) * 4.0f;

					float shieldPosX = _pos.X - shieldSize * 0.5f;
					float shieldPosY = _pos.Y - shieldSize * 0.5f;

					if (!PreferencesCache::UnalignedViewport) {
						shieldPosX = std::floor(shieldPosX);
						shieldPosY = std::floor(shieldPosY);
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
		OnEmitRemotedLights(lights);

		for (std::int32_t i = 0; i < (std::int32_t)_trail.size(); i++) {
			lights.emplace_back(_trail[i]);
		}
	}

	void Player::OnEmitRemotedLights(SmallVectorImpl<LightEmitter>& lights)
	{
		// The trail is left out on purpose - it's a local decoration controlled by a client-side preference
		// (and a long trail would be a stream of constantly changing lights on the wire)
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

		if (_playerType == PlayerType::Frog) {
			_playerType = _playerTypeOriginal;

			// Load original metadata, indexed only when recolored (must match the renderer's current palette state)
			bool useIndexed = (GetEffectiveFurColor() != 0);
			RequestMetadata(GetCharacterTraits(_playerType).Metadata, useIndexed);

			// Refresh animation state
			AnimState prevState = _currentAnimation->State;
			_currentSpecialMove = SpecialMoveType::None;
			_currentAnimation = nullptr;
			SetAnimation(prevState);

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

		// The sprite is always located relative to the hotspot.
		// The coldspot is usually located at the ground level of the sprite,
		// but for falling sprites for some reason somewhere above the hotspot instead.
		// It is absolutely important that the position of the hitbox stays constant
		// to the hotspot, though; otherwise getting stuck at walls happens all the time.
		if (_levelHandler->IsReforged()) {
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
			if (_currentSpecialMove == SpecialMoveType::Buttstomp && _currentTransition != nullptr && _sugarRushLeft <= 0.0f) {
				// Buttstomp is probably in starting transition, do nothing yet unless sugar rush is active
			} else if (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f) {
				other->DecreaseHealth(INT32_MAX, this);
				handled = true;

				if ((_currentAnimation->State & AnimState::Buttstomp) == AnimState::Buttstomp) {
					removeSpecialMove = true;
					_speed.Y *= -0.6f;
					SetState(ActorState::CanJump, false);
				}
			}
		} else if (auto* enemy = runtime_cast<Enemies::EnemyBase>(other)) {
			if (_currentSpecialMove == SpecialMoveType::Buttstomp && _currentTransition != nullptr && _sugarRushLeft <= 0.0f) {
				// Buttstomp is probably in starting transition, do nothing yet unless sugar rush or shield is active
			} else if (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f || (enemy->IsFrozen() && _speed.Length() >= 9.0f)) {
				if (!enemy->IsInvulnerable()) {
					enemy->DecreaseHealth(4, this);
					handled = true;

					Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 2), Explosion::Type::Small);

					if (_sugarRushLeft > 0.0f) {
						if (!_inWater && CanJump()) {
							_speed.Y = 3;
							SetState(ActorState::CanJump, false);
							_externalForce.Y = -0.6f;
						}
						_speed.Y *= -0.5f;
					}
					if ((_currentAnimation->State & AnimState::Buttstomp) == AnimState::Buttstomp) {
						removeSpecialMove = true;
						if (_levelHandler->IsReforged()) {
							_speed.Y *= -0.6f;
						} else {
							// Measured: a flat -13, not a fraction of the impact. The stomp's own descent is
							// governed at 10, and the bounce leaves at 13 - faster than the fall that caused
							// it - so scaling the impact could not produce this figure whatever the factor.
							// The ascent then decays at the *released* rise rate, like a pole launch and
							// unlike a spring, which is what `_jumpReleased` selects.
							_speed.Y = -LegacyEnemyStompBounce;
							_jumpReleased = true;
						}
						SetState(ActorState::CanJump, false);
						SetInvulnerability(FrameTimer::FramesPerSecond, InvulnerableType::Transient);
					} else if (_currentSpecialMove != SpecialMoveType::None && enemy->GetHealth() > 0) {
						removeSpecialMove = true;
						_externalForce.X = 0.0f;
						_externalForce.Y = 0.0f;

						if (_currentSpecialMove == SpecialMoveType::Sidekick) {
							_speed.X *= 0.5f;
						}
					}

					if (_currentSpecialMove == SpecialMoveType::None && _sugarRushLeft <= 0.0f && enemy->IsFrozen()) {
						_speed = -_speed;
						if (_speed.Y > -4.0f) {
							_speed.Y = -4.0f;
						}
						SetState(ActorState::CanJump, false);
					}

					_levelHandler->PlayerExecuteRumble(this, "Land"_s);
				}
			} else if (enemy->CanHurtPlayer()) {
				if (!IsInvulnerable()) {
					if (_activeShieldTime > 0.0f) {
						DecreaseShieldTime(5.0f * FrameTimer::FramesPerSecond);
						float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
						SetInvulnerability(invulnerableTime, InvulnerableType::Blinking);
						PlayPlayerSfx("HurtSoft"_s);
					} else {
						TakeDamage(1, 4 * (_pos.X > enemy->GetPos().X ? 1.0f : -1.0f));
					}
				}
			}
		} else if (auto* spring = runtime_cast<Environment::Spring>(other)) {
			// Collide only with hitbox here, not with the (larger) sprite box - but a spring is small enough to
			// be jumped clean over in one step at a low frame rate, so the path taken counts as well
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
					_inventory.Gems[0] += _inventory.Coins;
					AddScore(_inventory.Coins * 100);
					_inventory.Coins = 0;
				} else if (_bonusWarpTimer <= 0.0f) {
					_levelHandler->HandlePlayerCoins(this, _inventory.Coins, _inventory.Coins);
					PlaySfx("BonusWarpNotEnoughCoins"_s);

					_bonusWarpTimer = 400.0f;
				}
			}

			handled = true;
		} else if (auto* otherPlayer = runtime_cast<Player>(other)) {
			// Local splitscreen co-op: players physically bump each other apart and can stand on one another. Both
			// must be alive (skip a dead/respawning player) and not warping. Vertical contact is left to the stacking
			// resolver (one player landing on the other), so ApplyPlayerBump only separates side-to-side contact here.
			if (_levelHandler->CanPlayersCollide() && _health > 0 && otherPlayer->_health > 0 &&
				(_currentTransition == nullptr ||
				 (_currentTransition->State != AnimState::TransitionWarpIn && _currentTransition->State != AnimState::TransitionWarpInFreefall &&
				  _currentTransition->State != AnimState::TransitionWarpOut && _currentTransition->State != AnimState::TransitionWarpOutFreefall))) {
				ApplyPlayerBump(*otherPlayer, /*stackingEnabled:*/ true);
				// Resolve the pair only once per frame: returning `true` stops LevelHandler from also dispatching the
				// reverse collision (which would bump both players a second time, since ApplyPlayerBump moves both)
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
		// The heavier rise gravity only lasts for the ascent it was triggered in, so landing arms the next jump
		// with the light one again whether or not the key was ever let go
		_jumpReleased = false;

		if (_activeModifier == Modifier::None && (_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
			_copterFramesLeft = 0.0f;
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
					SetInvulnerability(invulnerableTime, InvulnerableType::Blinking);
					PlayPlayerSfx("HurtSoft"_s);
				} else {
					TakeDamage(1, _speed.X * 0.25f);
				}
			}
		} else if (!_inWater && _activeModifier == Modifier::None) {
			if (_hitFloorTime <= 0.0f && !CanJump()) {
				_hitFloorTime = 30.0f;
				PlaySfx("Land"_s, 0.8f);
				if (PreferencesCache::GamepadRumble >= 2) {
					// "Land" effect is enabled only for Strong preset
					_levelHandler->PlayerExecuteRumble(this, "Land"_s);
				}

				if (Random().NextFloat() < 0.6f) {
					Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y + 20, _renderer.layer() - 2), Explosion::Type::TinyDark);
				}
			}
		} else {
			// Prevent stucking with water/airboard
			SetState(ActorState::CanJump, false);
			if (_speed.Y > 0.0f) {
				_speed.Y = 0.0f;
			}
		}

		_canDoubleJump = true;
		_isFreefall = false;

		SetState(ActorState::IsSolidObject, true);
	}

	void Player::OnHitCeiling(float timeMult)
	{
		// Hitting a ceiling must stop the upward push immediately - otherwise a strong launch (e.g. a vertical
		// spring) keeps re-applying its force and the player sticks to the ceiling. (_speed.Y and _internalForceY
		// are already zeroed by the caller; clear the external force too.)
		if (!_levelHandler->IsReforged() && _externalForce.Y < 0.0f) {
			_externalForce.Y = 0.0f;
		}

		if (_levelHandler->EventMap()->IsHurting(_pos.X, _pos.Y - 4.0f, Direction::Down)) {
			if (!IsInvulnerable() && _sugarRushLeft <= 0.0f) {
				if (_activeShieldTime > 0.0f) {
					DecreaseShieldTime(5.0f * FrameTimer::FramesPerSecond);
					float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
					SetInvulnerability(invulnerableTime, InvulnerableType::Blinking);
					PlayPlayerSfx("HurtSoft"_s);
				} else {
					TakeDamage(1, _speed.X * 0.25f);
				}
			}
		}
	}

	void Player::OnHitWall(float timeMult)
	{
		// Reset speed and show Push animation
		_speed.X = 0.0f;
		_pushFramesLeft = 12.0f;
		_pushContactThisFrame = true;
		_keepRunningTime = 0.0f;

		if (_levelHandler->EventMap()->IsHurting(_pos.X + (_speed.X > 0.0f ? 16.0f : -16.0f), _pos.Y, (_speed.X > 0.0f ? Direction::Left : Direction::Right))) {
			if (!IsInvulnerable() && _sugarRushLeft <= 0.0f) {
				if (_activeShieldTime > 0.0f) {
					DecreaseShieldTime(5.0f * FrameTimer::FramesPerSecond);
					float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
					SetInvulnerability(invulnerableTime, InvulnerableType::Blinking);
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
						if (_levelHandler->EventMap()->GetEventByPosition(IsFacingLeft() ? hitbox2.L : hitbox2.R, hitbox2.B, &wallParams) != EventType::ModifierNoClimb) {
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
							// Gravitation is off for the whole climb, so this one speed carries the player all
							// the way up - the distance is simply the speed times the transition's duration.
							// `TryStandardMovement()` used to add one frame of gravity on top of it right after
							// this callback returned, which made the effective climb speed depend on the frame
							// rate (at 24 FPS barely a third of the 60 Hz rise was left). That tick is gone now,
							// so the 60 Hz amount of it is folded into the constant to keep the climb looking
							// exactly as it did, at any frame rate. This replaces the old `0.4` correction, which
							// only ever applied above 60 FPS.
							//
							// It has to be the plain level gravity and not GetGravityModifier(): climbing a ledge
							// is not an original JJ2 move at all, so the original's much heavier rise gravity has
							// no business shaping it. Taking the modifier made the climb 24% shorter, and 82%
							// shorter once `_jumpReleased` was set - which it is on any approach that involved
							// letting go of jump, i.e. nearly all of them - leaving the player short of the ledge.
							//
							// Clamped so the climb always actually lifts. Gravitation is off for the whole
							// transition, so this one value is the entire rise: a level handler reporting a
							// gravity of 1.4 or more would turn it into a descent and drop the player back
							// below the ledge they just grabbed. Today GetGravity() only ever returns 0.3 or
							// 0.24, so the clamp never binds - it is here because this is an invented move
							// with no original to match, and folding a shared physics value into one has
							// already cost it most of its lift once before.
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
				// Measured: the player and the object travel together at a flat rate, and holding Run makes no
				// difference - walking into a box and dashing into a rock push at exactly the same speed. Note
				// this is a speed, so it must not be scaled by timeMult the way the Reforged line does: that
				// made the push two and a half times faster at 24 FPS than at 60.
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
		std::int32_t sign = ((force.X + force.Y) > std::numeric_limits<float>::epsilon() ? 1 : -1);
		if (std::abs(force.X) > 0.0f) {
			// Reforged pulls the player halfway towards the spring's own centre. The original does not move
			// them at all: read off Diamondus 3's chain, where a blue horizontal spring catches a player
			// falling down a one-tile shaft, its y reads 1428.001, 1428.126, 1428.376 over the launch tick
			// and the two after it - creeping only as gravity rebuilds `ys` from zero.
			//
			// This is invisible to every scenario built out of props, because they all *walk* into a
			// horizontal spring at the height it already sits at, where the midpoint is a no-op. It only
			// shows when something falls onto one, and there it is worth 10 px - enough, on that layout, to
			// decide whether the player clears a wall at tile 12 and escapes the chain or hits it and loops.
			if (_levelHandler->IsReforged()) {
				MoveInstantly(Vector2f(_pos.X, (_pos.Y + pos.Y) * 0.5f), MoveType::Absolute);
			}

			_copterFramesLeft = 0.0f;
			if (!_levelHandler->IsReforged()) {
				// The original simply assigns the spring's speed - no formula, no added external force. The
				// spring already carries the converted figure (see Spring::OnActivatedAsync()).
				_speed.X = force.X;
				_externalForce.X = 0.0f;
			} else {
				_speed.X = (1.0f + std::abs(force.X)) * sign;
				_externalForce.X = force.X * 0.6f;
			}
			_springCooldown = 10.0f;
			SetState(ActorState::CanJump, false);
			// Being thrown by a spring hands the double jump back, exactly like landing does. Non-Reforged
			// only: in Reforged a spring has never refilled it, and handing Spaz an extra air jump per spring
			// there would change level routing that has always been balanced without it.
			if (!_levelHandler->IsReforged()) {
				_canDoubleJump = true;
			}

			_wasActivelyPushing = false;
			_keepRunningTime = (_levelHandler->IsReforged() ? 100.0f : 80.0f);

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
				// As with the horizontal case: the speed is the spring's, verbatim. The applied rise cap then
				// holds the actual climb to 8 px/tick, so a blue spring rises no faster than a red one - it
				// just keeps going for twice as long, which is what makes it reach further.
				_speed.Y = force.Y;
				_externalForce.Y = 0.0f;
				// A spring ascent decays at the HELD rate whatever the jump key is doing - measured at exactly
				// 0.375 over the whole rise, with the probe pressing nothing at all. Leaving `_jumpReleased`
				// alone made the spring inherit the player's last jump instead, and a spring is not a solid
				// object, so descending onto one never calls OnHitFloor() to clear the flag on the way in.
				// A blue spring then rose 399 px instead of 602 - measured, see `jr_spring_fall*` - which is
				// the difference between reaching what is above it and not, from the same spring, decided by
				// something the player did on the way down. The pole launch deliberately does the opposite
				// (see NextPoleStage()), which is why this belongs to the launch source rather than being
				// left to whatever preceded it. `_isSpring` already stops a *later* release from shortening
				// the rise; this is the same intent applied to a stale one.
				_jumpReleased = false;
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

			if (!keepSpeedX) {
				_speed.X = 0.0f;
				_externalForce.X = 0.0f;
				_keepRunningTime = 0.0f;
			}

			if (_inIdleTransition) {
				_inIdleTransition = false;
				CancelTransition();
			}

			if (sign > 0) {
				removeSpecialMove = false;
				if (_activeModifier == Modifier::None) {
					_currentSpecialMove = SpecialMoveType::Buttstomp;
					SetAnimation(AnimState::Buttstomp);
				}
			} else {
				removeSpecialMove = true;
				_isSpring = true;
				// A spring hands the double jump back, exactly like landing does - otherwise a player who had
				// already spent it in mid-air has nothing left for the whole of the (usually long) spring arc.
				// It only becomes usable once the rise is over, see HandleSpecialJump(). Non-Reforged only,
				// for the same reason as the horizontal case above.
				if (!_levelHandler->IsReforged()) {
					_canDoubleJump = true;
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
		Explosion::Create(_levelHandler, Vector3i((std::int32_t)pos.X, (std::int32_t)pos.Y, _renderer.layer() + 2), Explosion::Type::WaterSplash);
		_levelHandler->PlayCommonSfx("WaterSplash"_s, Vector3f(pos.X, pos.Y, 0.0f), inwards ? 0.7f : 1.0f, 0.5f);
	}

	void Player::UpdateAnimation(float timeMult)
	{
		if (!_controllable) {
			return;
		}

		AnimState oldState = _currentAnimation->State;
		AnimState newState;
		if (_inWater) {
			newState = AnimState::Swim;
		} else if (_activeModifier == Modifier::Airboard) {
			newState = AnimState::Airboard;
		} else if (_activeModifier == Modifier::Copter) {
			newState = AnimState::Copter;
		} else if (_activeModifier == Modifier::LizardCopter) {
			newState = AnimState::Hook;
		} else if (_suspendType == SuspendType::SwingingVine) {
			newState = AnimState::Swing;
		} else if (_onPinballPaddleTime > 0.0f) {
			// Standing on a pinball paddle, which the original shows as the sucker tube's curled-up ball -
			// see `_onPinballPaddleTime`. It has to be decided here rather than assigned by the paddle,
			// because everything below would otherwise overwrite it on the very next frame.
			newState = AnimState::Dash | AnimState::Jump;
		} else if (_isLifting || (_beingStoodOn && CanJump() && std::abs(_speed.X) < 1.0f && std::abs(_speed.Y) < 1.0f)) {
			// `_isLifting` is the movement-restricting lift (solid object, or a player in local co-op); `_beingStoodOn`
			// is the cosmetic online case - only show its pose while grounded and still, so a moving (unrestricted)
			// player doesn't freeze in the lift pose
			newState = AnimState::Lift;
		} else if (CanJump() && _isActivelyPushing && _pushFramesLeft > 0.0f && _keepRunningTime <= 0.0f && _fireFramesLeft <= 0.0f &&
			(_pushContactThisFrame || std::abs(_speed.X) <= MaxPushingSpeed)) {
			// The grace timer (_pushFramesLeft) bridges brief frames where the contact probe misses an object that's
			// still being pushed, so the pose doesn't flicker. But only keep it while still genuinely pushing: in
			// contact this step, or still held to the slow push speed. Once the player breaks free and accelerates
			// away, end it at once instead of letting the timer linger the push pose over open ground.
			newState = AnimState::Push;

			if (_inIdleTransition) {
				_inIdleTransition = false;
				CancelTransition();
			}
		} else {
			// Only certain ones don't need to be preserved from earlier state, others should be set as expected
			AnimState composite = (_currentAnimation->State & CompositeAnimMask);

			if (_isActivelyPushing == _wasActivelyPushing || !_levelHandler->IsReforged()) {
				float absSpeedX = std::abs(_speed.X);
				// Threshold must track the actual walk cap, otherwise the higher non-Reforged walk speed would
				// keep triggering the Dash animation while merely walking.
				float dashAnimThreshold = (_levelHandler->IsReforged() ? MaxRunningSpeed : LegacyWalkSpeed);
				if (absSpeedX > dashAnimThreshold) {
					composite |= AnimState::Dash;
				} else if (_keepRunningTime > 0.0f) {
					composite |= AnimState::Run;
				} else if (absSpeedX > (_fireFramesLeft > 0.0f ? 1.0f : 0.0f)) {	// Shooting needs higher threshold to fix pushing into a wall
					composite |= AnimState::Walk;
				}

				if (_inIdleTransition) {
					_inIdleTransition = false;
					CancelTransition();
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
			if (_idleTime > 600.0f) {
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
					// The skid/brake transition isn't part of the original game, so skip it when not Reforged
					if (newState == AnimState::Dash) {
						SetTransition(AnimState::TransitionRunToDash, true);
					} else if (newState == AnimState::Idle && _levelHandler->IsReforged()) {
						_inIdleTransition = true;
						SetTransition(AnimState::TransitionRunToIdle, true, [this]() {
							_inIdleTransition = false;
						});
					}
					break;
				case AnimState::Dash:
					if (newState == AnimState::Idle && _levelHandler->IsReforged()) {
						_inIdleTransition = true;
						SetTransition(AnimState::TransitionDashToIdle, true, [this]() {
							if (_inIdleTransition) {
								SetTransition(AnimState::TransitionRunToIdle, true, [this]() {
									_inIdleTransition = false;
								});
							}
						});
					}
					break;
				case AnimState::Fall:
				case AnimState::Freefall:
					if (newState == AnimState::Idle) {
						SetTransition(AnimState::TransitionFallToIdle, true);
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
					} else if (!_inLedgeTransition && _carryingObject == nullptr && std::abs(_speed.X) < 1.0f && std::abs(_speed.Y) < 1.0f) {
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
	}

	void Player::PushSolidObjects(float timeMult)
	{
		// Ground truth for "pushing this step" - re-established below (and in OnHitWall during the move that follows)
		_pushContactThisFrame = false;

		if (_pushFramesLeft > 0.0f) {
			_pushFramesLeft -= timeMult;
		} else {
			_canPushFurther = false;
		}

		if (CanJump() && _controllable && _controllableExternal && _isActivelyPushing /*&& std::abs(_speed.X) > 0.0f*/) {
			float offset = (IsFacingLeft() ? -4.0f : 4.0f);
			AABBf hitbox = { AABBInner.L + offset, AABBInner.T + 8.0f, AABBInner.R + offset, AABBInner.B - 14.0f };
			TileCollisionParams params = { TileDestructType::None, false };
			ActorBase* collider;
			if (!_levelHandler->IsPositionEmpty(this, hitbox, params, &collider)) {
				if (auto* solidObject = runtime_cast<SolidObjectBase>(collider)) {
					SetState(ActorState::IsSolidObject, false);
					float pushSpeedX = solidObject->Push(_speed.X < 0, timeMult);
					OnPushSolidObject(timeMult, pushSpeedX);
					SetState(ActorState::IsSolidObject, true);
				}
			}
		} else {
			// Lift state: a solid object resting on our head, or another player standing on top of us. The lift
			// animation is shown in both cases (online it propagates to other clients via the normal animation sync),
			// but only the local co-op case immobilizes us, exactly like a solid object - so the player has to jump
			// to get out of it. Online, being stood on is purely cosmetic and never restricts movement.
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

			// Detecting who stands on us by scanning the player list only works where we simulate them: locally
			// (splitscreen) and on the server (all players). On an online client, our own player is predicted and
			// can't see a remote player standing on it, so the server tells us via PlayerPropertyType::BeingStoodOn -
			// in that case leave `_beingStoodOn` alone (it's driven by the packet, not recomputed here).
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
						std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(EventType::Gem, spawnParams, ActorState::None, Vector3i((std::int32_t)(_pos.X + fx * 2.0f), (std::int32_t)(_pos.Y + fy * 4.0f), _renderer.layer() - 10));
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
					SetTransition(AnimState::TransitionButtstompEnd, true);
				}
			} else {
				_controllable = true;
			}
		}

		// Uppercut. Reforged ends it once the rise has decayed, which with its sustained force is what sets
		// the height. The original instead drives for a fixed 30 ticks and is still doing -6.59 when it ends,
		// so waiting for -2 there would overshoot by a long way - it counts the ticks out instead.
		if (_currentSpecialMove == SpecialMoveType::Uppercut && _currentTransition == nullptr && ((_currentAnimation->State & AnimState::Uppercut) == AnimState::Uppercut) &&
			(_levelHandler->IsReforged() ? _speed.Y > -2.0f : _uppercutTimeLeft <= 0.0f)) {
			EndDamagingMove();
		}

		// Sidekick - the dash covers a fixed distance, so what is counted here is the distance itself rather
		// than a duration. `_controllableTimeout` cannot do it: this function only ever sees that a frame
		// stale, since OnUpdateTimers() runs after it. Counting ticks instead still rounded up to whole
		// frames, which on a four-tick kick was a 20% overshoot at 60 FPS and worse at 24. Zeroing the speed
		// lets the existing exit condition below finish the move on this same tick.
		bool sidekickSpent = false;
		// Gated on the move actually being in progress: the budget is persistent state and every other way a
		// kick can end (a wall, an enemy that survives it, a spring, water, a warp, damage) leaves a remainder
		// behind. Counting that remainder down against ordinary running fired the terminal speed fix-up
		// hundreds of pixels later - a dash silently dropping to walking pace, or Lori stopping dead mid-run.
		// EndDamagingMove() clears it, so this only ever sees a live kick.
		if (_currentSpecialMove == SpecialMoveType::Sidekick && _sidekickDistanceLeft > 0.0f) {
			_sidekickDistanceLeft -= std::abs(_pos.X - _frameStartPos.X);
			if (_sidekickDistanceLeft <= 0.0f) {
				_sidekickDistanceLeft = 0.0f;
				_externalForce.X = 0.0f;
				sidekickSpent = true;
				// Lori's kick stops dead - measured, her speed reads 0 on the very next tick. Spaz's does not:
				// his snaps back to the walk cap and the ordinary deceleration coasts him the rest of the way,
				// which is where the last ~65 px of his ~505 px come from.
				if (_playerType == PlayerType::Lori) {
					_speed.X = 0.0f;
				} else {
					_speed.X = std::clamp(_speed.X, -LegacyWalkSpeed, LegacyWalkSpeed);
				}
			}
		}

		if (_currentSpecialMove == SpecialMoveType::Sidekick && _currentTransition == nullptr && (sidekickSpent || _controllable || std::abs(_speed.X) < 0.01f)) {
			EndDamagingMove();
			_controllable = true;
			_controllableTimeout = 0.0f;
			if (_suspendType == SuspendType::None) {
				SetTransition(AnimState::TransitionUppercutEnd, false);
			}
		}

		// Copter Ears
		if (_activeModifier != Modifier::Copter && _activeModifier != Modifier::LizardCopter) {
			// TODO: Is this still needed?
			bool cancelCopter;
			if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
				cancelCopter = (CanJump() || _suspendType != SuspendType::None || _copterFramesLeft <= 0.0f);

				SetCopterFlight(_copterFramesLeft - timeMult, FlightType::Normal);
				_speed.Y = std::min(_speed.Y + _levelHandler->GetGravity() * timeMult,
					(_levelHandler->IsReforged() ? 1.5f : LegacyCopterDescentSpeed));
			} else {
				cancelCopter = ((_currentAnimation->State & AnimState::Fall) == AnimState::Fall && _copterFramesLeft > 0.0f);
			}

			if (cancelCopter) {
				_copterFramesLeft = 0.0f;
				SetAnimation(_currentAnimation->State & ~AnimState::Copter);
				if (!_isAttachedToPole) {
					SetState(ActorState::ApplyGravitation, true);
				}
			}
		}
	}

	void Player::CheckSuspendState(float timeMult)
	{
		if (_suspendTime > 0.0f) {
			_suspendTime -= timeMult;
			return;
		}

		if (_suspendType == SuspendType::SwingingVine || _activeModifier != Modifier::None) {
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
			// Already attached - the plain point check decides when the player slides off the end of a vine
			// and where exactly they hang, so it has to stay exactly as it is
			if (newSuspendState == _suspendType) {
				return;
			}
		} else {
			// The settle loop below parks the player 4 px under the lowest pixel of the vine, so that is where
			// the two of them actually touch. The grab box is anchored to that contact line rather than to the
			// grab point, otherwise a falling player attaches while the vine is still below their hands.
			constexpr float ContactOffset = 4.0f;
			// A falling player catches the vine this much past the contact line, which reads better than
			// snapping the very moment the vine is level with the hands. It applies to falling only - moving
			// the line while rising would just yank the player upwards.
			constexpr float ContactDelay = 4.0f;
			// Vines and hooks spanning only a single tile would be nearly impossible to grab without this
			constexpr float ToleranceX = 14.0f;
			// How much higher than the grab line the vine may be and still be caught
			constexpr float ToleranceUp = 2.0f;
			// The player reaches higher while rising, so a vine that is just out of jump range can still be
			// grabbed, but nobody gets pulled up while standing below one
			constexpr float ToleranceUpRising = 8.0f;

			// The box covers the whole distance travelled this frame as well, so that falling (or jumping)
			// fast cannot tunnel through a vine in between two frames - the grab is then resolved where the
			// vine was actually crossed instead of where the player ended up
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
		if (_inWater) {
			if (_pos.Y >= _levelHandler->GetWaterLevel()) {
				SetState(ActorState::ApplyGravitation, false);

				if (std::abs(_speed.X) > 1.0f || std::abs(_speed.Y) > 1.0f) {
					float angle;
					if (_speed.X == 0.0f) {
						if (IsFacingLeft()) {
							angle = atan2(-_speed.Y, -std::numeric_limits<float>::epsilon());
						} else {
							angle = atan2(_speed.Y, std::numeric_limits<float>::epsilon());
						}
					} else if (_speed.X < 0.0f) {
						angle = atan2(-_speed.Y, -_speed.X);
					} else {
						angle = atan2(_speed.Y, _speed.X);
					}

					if (angle > fPi) {
						angle = angle - fTwoPi;
					}

					_renderer.setRotation(std::clamp(angle, -fPiOver3, fPiOver3));
				}

				// Adjust swimming animation speed
				if (_currentTransition == nullptr) {
					_renderer.AnimDuration = std::max(_currentAnimation->AnimDuration + 1.0f - Vector2f(_speed.X, _speed.Y).Length() * 0.26f, 0.4f);
				}

			} else if (_waterCooldownLeft <= 0.0f) {
				_inWater = false;
				_waterCooldownLeft = 20.0f;

				SetState(ActorState::ApplyGravitation | ActorState::CanJump, true);
				_externalForce.Y = -0.6f;
				_renderer.setRotation(0.0f);

				SetAnimation(AnimState::Jump);

				OnWaterSplash(Vector2f(_pos.X, _levelHandler->GetWaterLevel()), false);
			}
		} else {
			if (_pos.Y >= _levelHandler->GetWaterLevel() && _waterCooldownLeft <= 0.0f) {
				_inWater = true;
				_waterCooldownLeft = 20.0f;
				_flyCheatActive = false;

				if (_activeModifier == Modifier::Copter || _activeModifier == Modifier::Airboard) {
					SetModifier(Modifier::None);
				}

				_controllable = true;
				EndDamagingMove();

				OnWaterSplash(Vector2f(_pos.X, _levelHandler->GetWaterLevel()), true);
			}

			// Adjust walking animation speed
			if (_currentAnimation->State == AnimState::Walk && _currentTransition == nullptr) {
				_renderer.AnimDuration = _currentAnimation->AnimDuration * (1.4f - 0.4f * std::min(std::abs(_speed.X), MaxRunningSpeed) / MaxRunningSpeed);
			}
		}
	}

	void Player::OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock)
	{
		areaWeaponAllowed = true;
		areaWaterBlock = -1;

		auto events = _levelHandler->EventMap();
		if (events == nullptr) {
			return;
		}

		// Tile events are sampled along the whole path travelled this frame, not only where the player ended
		// up: at a low frame rate and high speed the player crosses several tiles in a single step and would
		// otherwise miss every trigger in between - a pole or a tube most noticeably. The step is half a tile,
		// so no tile the path passes through can fall between two samples.
		constexpr float SweepStep = Tiles::TileSet::DefaultTileSize / 2;
		// A teleport (a warp, a respawn, a multiplayer re-sync) is no distance travelled and doesn't reach the
		// sampling at all - it resets the path, so only the destination is left to examine (see
		// ActorBase::ResetPathTracking()). This is only an upper bound on the work a single frame can ask for,
		// well above what the movement itself can produce.
		constexpr std::int32_t MaxSweepSamples = 16;

		Vector2f delta = _pos - _frameStartPos;
		float distance = std::max(std::abs(delta.X), std::abs(delta.Y));
		std::int32_t sampleCount = (distance > SweepStep
			? std::min<std::int32_t>((std::int32_t)(distance / SweepStep) + 1, MaxSweepSamples)
			: 1);

		// The step is deliberately shorter than a tile, so consecutive samples usually land in the same one. Only
		// the sample that first enters a tile may act on its event, otherwise a single frame would run a script
		// callback, show a text or broadcast a water change several times over. The path is a straight line, so
		// a tile it leaves is never entered again and remembering only the previous one is enough.
		Vector2i lastTile = Vector2i(INT32_MIN, INT32_MIN);
		bool statesHandled = false;

		for (std::int32_t i = 1; i <= sampleCount; i++) {
			// The path is walked from just past the frame's start position (the previous frame already handled
			// that point) to exactly the current position
			bool isLastSample = (i == sampleCount);
			Vector2f samplePos = (isLastSample ? _pos : _frameStartPos + delta * ((float)i / sampleCount));

			// Rounded exactly the way Events::EventMap::GetEventByPosition() does, so two samples are treated as
			// the same tile precisely when they would read the same event
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

		// An event that took the player over stops the walk, which for an intermediate sample means the state pass
		// never ran. Those aren't effects that fire but a description of where the player is, and leaving them at
		// their defaults for the frame would drop the ambient light and the water/weapon restrictions of the tile
		// the player is standing in, so they're read at the position it ended up at.
		if (!statesHandled) {
			HandleAreaEventAt(_pos.X, _pos.Y, timeMult, AreaEventPass::States, areaWeaponAllowed, areaWaterBlock);
		}

		// TODO: Implement Slide modifier with JJ2+ parameter

		// Check floating from each corner of an extended hitbox
		// Player should not pass from a single tile wide gap if the columns left or right have
		// float events, so checking for a wider box is necessary.
		constexpr float ExtendedHitbox = 2.0f;

		std::uint8_t* p;
		if (!_isAttachedToPole && (_currentTransition == nullptr || _currentTransition->State != AnimState::TransitionLedgeClimb)) {
			if (_currentSpecialMove != SpecialMoveType::Buttstomp) {
				if ((events->GetEventByPosition(_pos.X, _pos.Y, &p) == EventType::AreaFloatUp) ||
					(events->GetEventByPosition(AABBInner.L - ExtendedHitbox, AABBInner.T - ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
					(events->GetEventByPosition(AABBInner.R + ExtendedHitbox, AABBInner.T - ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
					(events->GetEventByPosition(AABBInner.R + ExtendedHitbox, AABBInner.B + ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
					(events->GetEventByPosition(AABBInner.L - ExtendedHitbox, AABBInner.B + ExtendedHitbox, &p) == EventType::AreaFloatUp)
				) {
					// External force of pinball bumber has higher priority
					if (_externalForceCooldown <= 0.0f || _speed.Y < 0.0f) {
						if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
							_speed.Y = std::max(_speed.Y - _levelHandler->GetGravity() * timeMult * 8.0f, -6.0f);
						} else if (!_levelHandler->IsReforged() && GetState(ActorState::ApplyGravitation)) {
							// The original simply holds the player at a fixed rise speed while they are in
							// the field - not an acceleration, and no force. Measured inside a solid ten-tile
							// column: exactly -8.0 on every one of the 43 ticks inside it, decaying only once
							// the player leaves. Reforged's continuous `-2 * gravity * timeMult` force never
							// assigns a real upward speed and oscillates around zero instead, so riding the
							// test level's diagonal ladder gained 149.8 px against the original's 520.3.
							_speed.Y = -LegacyFloatUpSpeed;
							_externalForce.Y = 0.0f;
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
					// Measured at 0.5 px/tick per unit of strength, against Reforged's 0.7 - so a wind blew
					// the player along 20% too fast. Which way it blows is *not* decided by the event being
					// WIND_LEFT or WIND_RIGHT: the parameter is signed, and the original blows a
					// `MODIFIER_WIND_LEFT` with a positive 8 rightward exactly as our converter reads it.
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
							// A position move like the wind above, but at twice its strength - measured at
							// 1.0 px/tick per unit against Reforged's 0.7
							float factor = (_levelHandler->IsReforged() ? 0.7f : LegacyBeltFactor);
							MoveInstantly(Vector2f((p2 - p1) * factor * timeMult, 0), MoveType::Relative);
						}
						// The accelerating belt is the one of the three that works on the *speed*, and Reforged
						// adds a fixed step per frame with no ceiling - and, because that step is not scaled
						// by `timeMult`, at a rate that depends on the frame rate. It crawled to the
						// original's speed over 45 frames and then sailed past it.
						//
						// The non-Reforged one is not applied here at all: it has to come *after* the brake,
						// so it lives in HandleHorizontalMovement(). See @ref LegacyAccBeltStep.
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

		// The state half of a tile event describes where the player *is*, so it is read only from the tile the
		// player ended the frame in. A sample in between must not latch it, or a player falling past a strip of
		// shallow water would keep that water for the rest of the frame - splashing in and straight back out of
		// something it only flew through - and one crossing a no-fire area mid-frame would stay unable to fire
		// after it had already left.
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
					areaWaterBlock = ((std::int32_t)y / 32) * 32 + p[0];
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
				TakeDamage(INT32_MAX, 0.0f, true);
				return true;
			}
			case EventType::ModifierSetWater: {
				_levelHandler->BroadcastTriggeredEvent(this, EventType::ModifierSetWater, p);
				break;
			}
			case EventType::ModifierLimitCameraView: { // Left, Width
				// Through EventParamsReader, which memcpy()s. The parameters are a byte array starting at an
				// ODD offset inside EventTile (4-byte flags, 2-byte event type, 1-byte active flag, then
				// these), so a `*(std::uint16_t*)` of them is an unaligned halfword load - and on MIPS and
				// SH-4 that is not a slow path but an address error that takes the process down with no
				// chance to log anything. See the AreaEndOfLevel case below, where it was reproducible.
				EventParamsReader eventParams(p);
				std::uint16_t left = eventParams.GetUint16(0);
				std::uint16_t width = eventParams.GetUint16(2);
				_levelHandler->LimitCameraView(this, _pos,
					(left == 0 ? (std::int32_t)(x / Tiles::TileSet::DefaultTileSize) : left) * Tiles::TileSet::DefaultTileSize,
					width * Tiles::TileSet::DefaultTileSize);
				break;
			}
			case EventType::ModifierHPole: {
				// A declined pole (the one just left behind, a character that can't use it) doesn't take the
				// player over, so the rest of the path still gets examined
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
					// A player in Noclip Mode cannot use a tube event with Noclip Only set to false,
					// nor can a player not in Noclip Mode use a tube event with Noclip Only set to true
					break;
				}

				EndDamagingMove();

				SetAnimation(AnimState::Dash | AnimState::Jump);

				_controllable = false;
				SetState(ActorState::CanJump, false);
				// The original keeps gravity **on** through a tube: the speed is re-assigned every tick the
				// player is inside a tile, and the moment they leave, the ascent decays at the ordinary rise
				// gravity. Switching it off for the whole control window made a tube firing straight up rise
				// 222 px against the original's 99.7, and a 30-tile column of them 1151 against 1027.
				if (_levelHandler->IsReforged()) {
					SetState(ActorState::ApplyGravitation, false);
				}

				// The parameters are in the original's units - pixels per 70 Hz tick - so they need the same
				// conversion every other measured speed gets. Both traces *display* 8.0 for a tube set to 8,
				// which is what made this look right at first: 8 px per frame here is 480 px/s against the
				// original's 560, and the travel came out at exactly 6/7 of it.
				float tubeScale = (_levelHandler->IsReforged() ? 1.0f : LegacyFrameRateScale);
				_speed.X = (float)(std::int8_t)p[0] * tubeScale;
				_speed.Y = (float)(std::int8_t)p[1] * tubeScale;

				// The tube snaps the player onto its own tile, which is the tile the event was found in - not
				// necessarily the one the player ended the frame in, if it entered the tube mid-step
				Vector2f pos = Vector2f(x, y);
				if (_speed.X == 0.0f) {
					pos.X = (std::floor(pos.X / 32) * 32) + 16;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				} else if (_speed.Y == 0.0f) {
					pos.Y = (std::floor(pos.Y / 32) * 32) + 8;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				} else if (_inTubeTime <= 0.0f) {
					pos.X = (std::floor(pos.X / 32) * 32) + 16;
					pos.Y = (std::floor(pos.Y / 32) * 32) + 8;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				}

				SetState(ActorState::CollideWithTileset, !becomeNoclip);
				_inTubeTime = (becomeNoclip ? 600.0f : (_levelHandler->IsReforged() ? 10.0f : LegacyTubeControlTime));
				return true;
			}
			case EventType::AreaEndOfLevel: { // ExitType, Fast (No score count, only black screen), TextID, TextOffset, Coins
				if (_levelExiting == LevelExitingState::None) {
					// TODO: Implement Fast parameter
					// memcpy'd through EventParamsReader rather than cast, because EventTile puts these
					// parameters at an odd offset - see ModifierLimitCameraView above. This exact line killed
					// the PSP build every time the player reached a level exit: the unaligned `lhu` raised an
					// address error inside Player::OnUpdate, before even the exit sound was played, so the
					// log always just stopped. PPSSPP never reproduced it - its JIT lowers the load to an x86
					// access, which permits unaligned - only real hardware faults.
					std::uint16_t coinsRequired = EventParamsReader(p).GetUint16(4);
					if (coinsRequired <= _inventory.Coins) {
						_inventory.Coins -= coinsRequired;

						ExitType exitType = (ExitType)p[0];
						if (p[1] != 0) {
							exitType |= ExitType::FastTransition;
						}
						StringView nextLevel;
						if (p[2] != 0) {
							nextLevel = _levelHandler->GetLevelText(p[2], p[3], '|');
						}
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
					_levelHandler->BroadcastTriggeredEvent(this, EventType::AreaCallback, p);
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
				if (_activeModifier == Modifier::Airboard && !IsFlyCheatActive()) {
					SetModifier(Modifier::None);
				}
				break;
			}
			case EventType::AreaRevertMorph: {
				if (_playerType != _playerTypeOriginal) {
					MorphRevert();
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
				// Only the two permanent switches are an effect, the temporary block (0) lasts just as long as
				// the player stands in the area and belongs to the state pass above
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

		return nullptr;
	}

	bool Player::SetPlayerTransition(AnimState state, bool cancellable, bool removeControl, SpecialMoveType specialMove, Function<void()>&& callback)
	{
		if (removeControl) {
			_controllable = false;
			_controllableTimeout = 0.0f;
		}

		_currentSpecialMove = specialMove;
		return SetTransition(state, cancellable, std::move(callback));
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

				// Spawn corpse - pass the effective fur color (bytes 2..5) so the corpse is recolored like the player
				std::uint32_t furColor = GetEffectiveFurColor();
				std::uint8_t playerParams[6] = { (std::uint8_t)_playerType, (std::uint8_t)(IsFacingLeft() ? 1 : 0),
					(std::uint8_t)(furColor & 0xFF), (std::uint8_t)((furColor >> 8) & 0xFF),
					(std::uint8_t)((furColor >> 16) & 0xFF), (std::uint8_t)((furColor >> 24) & 0xFF) };
				std::shared_ptr<PlayerCorpse> corpse = std::make_shared<PlayerCorpse>();
				corpse->OnActivated(ActorActivationDetails(
					_levelHandler,
					Vector3i(_pos.X, _pos.Y, _renderer.layer() - 40),
					playerParams
				));
				_levelHandler->AddActor(corpse);

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
					_renderer.setDrawEnabled(false);

					_invulnerableTime = 0.0f;
					SetState(ActorState::IsInvulnerable, true);
					SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors, false);
				}
			} else {
				_lives = 0;
				_renderer.setDrawEnabled(false);

				_levelHandler->HandleGameOver(this);
			}
		});

		PlayPlayerSfx("Die"_s, 1.3f);
		_levelHandler->PlayerExecuteRumble(this, "Die"_s);
	}

	void Player::SwitchToNextWeapon()
	{
#if defined(WITH_AUDIO)
		if (_weaponSound != nullptr) {
			_weaponSound->stop();
			_weaponSound = nullptr;
		}
#endif

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
#if defined(WITH_AUDIO)
		if (_weaponSound != nullptr) {
			_weaponSound->stop();
			_weaponSound = nullptr;
		}
#endif
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

	void Player::FireWeaponRF()
	{
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

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
			Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() - 2)
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

#if defined(WITH_AUDIO)
		if (_weaponSound == nullptr) {
			PlaySfx("WeaponThunderboltStart"_s, 0.5f);
			_weaponSound = PlaySfx("WeaponThunderbolt"_s, 1.0f);
			if (_weaponSound != nullptr) {
				_weaponSound->setLooping(true);
				_weaponSound->setPitch(Random().FastFloat(1.05f, 1.2f));
				_weaponSound->setLowPass(0.9f);
			}
		}
#endif

		return true;
	}

	bool Player::FireCurrentWeapon(WeaponType weaponType)
	{
		if (_weaponCooldown > 0.0f) {
			return (weaponType != WeaponType::TNT);
		}

		std::uint16_t ammoDecrease = 256;

		switch (weaponType) {
			case WeaponType::Blaster:
				switch (_activeShield) {
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
						PlayPlayerSfx("WeaponBlaster"_s);
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
#if defined(WITH_AUDIO)
					if (_weaponSound == nullptr) {
						_weaponSound = PlaySfx("WeaponToaster"_s, 0.6f);
						if (_weaponSound != nullptr) {
							_weaponSound->setLooping(true);
						}
					}
#endif
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
		if (ammoDecrease > currentAmmo) {
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

	void Player::GetFirePointAndAngle(Vector3i& initialPos, Vector2f& gunspotPos, float& angle)
	{
		if (_currentTransition != nullptr && (_currentTransition->State == AnimState::Spring || _currentTransition->State == AnimState::TransitionShootToIdle)) {
			ForceCancelTransition();
		}

		SetAnimation(_currentAnimation->State | AnimState::Shoot);

		initialPos = Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() - 2);
		gunspotPos = _pos;

		if (_inWater) {
			angle = _renderer.rotation();

			std::int32_t size = (_currentAnimation->Base->FrameDimensions.X / 2);
			gunspotPos.X += (cosf(angle) * size) * (IsFacingLeft() ? -1.0f : 1.0f);
			gunspotPos.Y += (sinf(angle) * size) * (IsFacingLeft() ? -1.0f : 1.0f) - (_currentAnimation->Base->Hotspot.Y - _currentAnimation->Base->Gunspot.Y);
		} else {
			gunspotPos.X += (_currentAnimation->Base->Hotspot.X - _currentAnimation->Base->Gunspot.X) * (IsFacingLeft() ? 1 : -1);
			gunspotPos.Y -= (_currentAnimation->Base->Hotspot.Y - _currentAnimation->Base->Gunspot.Y);

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
				if (CanJump() && std::abs(_speed.X) < 1.0f && std::abs(_speed.Y) < 1.0f) {
					_levelExiting = LevelExitingState::Transition;

					ForceCancelTransition();

					SetPlayerTransition(AnimState::TransitionEndOfLevel, false, true, SpecialMoveType::None, [this]() {
						_renderer.setDrawEnabled(false);
						_levelExiting = LevelExitingState::Ready;
					});
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
						_renderer.setDrawEnabled(false);
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
						_renderer.setDrawEnabled(false);
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

		_renderer.setDrawEnabled(true);

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

		_inventory.WeaponAmmo[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
		_inventoryCheckpoint.WeaponAmmo[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;

		ExitType exitTypeMasked = (exitType & ExitType::TypeMask);
		if (exitTypeMasked == ExitType::Warp || exitTypeMasked == ExitType::Bonus || exitTypeMasked == ExitType::Boss) {
			// Use delayed spawning
			SetState(ActorState::ApplyGravitation, false);
			_renderer.setDrawEnabled(false);
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
				PreloadMetadataAsync(String("Weapon/"_s + WeaponNames[i]));
			}
		}
	}

	PlayerCarryOver Player::PrepareLevelCarryOver()
	{
		PlayerCarryOver carryOver;
		carryOver.Type = _playerType;
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
		dest.WriteValue<std::uint8_t>((std::uint8_t)_playerType);
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

	bool Player::Respawn(Vector2f pos)
	{
		if ((GetState() & (ActorState::IsInvulnerable | ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors)) != ActorState::IsInvulnerable) {
			return false;
		}

		_health = _maxHealth;

		MoveInstantly(pos, MoveType::Absolute | MoveType::Force);

		_controllable = true;
		_renderer.setDrawEnabled(true);
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
			_copterFramesLeft = 0.0f;
			_pushFramesLeft = 0.0f;

			ResetLegacyMovementState();

			// For warping from the water
			_renderer.setRotation(0.0f);

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
			// Don't re-enable gravity if any modifier is active
			if (_activeModifier == Modifier::None) {
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
	}

	void Player::WarpToCheckpoint()
	{
		WarpToPosition(_checkpointPos, WarpFlags::SkipWarpIn);
		_levelHandler->SetAmbientLight(this, _checkpointLight);
	}

	bool Player::InitialPoleStage(bool horizontal, Vector2f eventPos)
	{
		if (_isAttachedToPole || _playerType == PlayerType::Frog || _activeModifier == Modifier::Copter || _activeModifier == Modifier::Airboard) {
			return false;
		}

		// The tile the pole is grabbed at is the one the event was found in, not the one the player happens to
		// have ended the frame in - at a low frame rate and high speed those are one or two tiles apart, and
		// snapping to the latter starts the whole animation past the pole
		std::int32_t x = (std::int32_t)eventPos.X / Tiles::TileSet::DefaultTileSize;
		std::int32_t y = (std::int32_t)eventPos.Y / Tiles::TileSet::DefaultTileSize;

		// Only the exact tile just used is excluded, and only for as long as `_lastPoleTime` runs, which stops
		// the launch re-grabbing the pole it came off.
		//
		// It used to exclude everything within two tiles on both axes, on the assumption that two distinct
		// poles are never that close together. Measured against the original, they can be: a V-Pole event
		// directly above another is a real construction, and the original grabs the second and chains off it
		// - launching -22.25 and then -37.38, for 406.7 px against the 270.6 one pole gives. The old area
		// swallowed the pole one tile up, so the second was silently ignored and a third of the height lost.
		//
		// Side by side is different and needs no exclusion to get right: the original does *not* chain there
		// either, because a launch straight up the column simply never overlaps the neighbouring tile. Both
		// games rise 270 px for that layout, so the geometry decides it rather than any rule.
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
		// Zero is the one entry speed where the comparison matters, and `>= 0` gets it wrong for a horizontal
		// pole: measured, the original launches one *left* when the player arrives with no horizontal speed at
		// all - which is what dropping onto a pole from above does. Together with LegacyHPoleMinLaunch that is
		// what stops such a player re-grabbing the same pole for ever.
		//
		// Reforged keeps `>= 0`. Its launch is a fixed 10 either way, so the direction there is cosmetic and
		// nothing is being matched to; and the vertical pole keeps it too, since a vertical entry of exactly
		// zero has never been measured on either side.
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
		_renderer.setRotation(0.0f);
		_isAttachedToPole = true;
		// Which rise gravity the launch will decay under depends on how the player got here, and it has to be
		// remembered now: `_isSpring` covers only the rising part of a spring launch, and a pole holds the
		// player at zero speed for about 140 ticks, which clears it long before the launch happens.
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
					// Measured: this one multiplies the entry speed and clamps the result, where the vertical
					// pole adds a fixed bonus instead. Entering at a walk gives 12, and anything from a run up
					// hits the ceiling - but the multiply only decides the middle of the range. Fitted on four
					// entry speeds: 0 -> 8 (leftward), 4 -> 12, 9.34 -> 20 and 16 -> 20, the last two clamped.
					//
					// The floor is what the zero case needs. Without it a player who drops onto a pole is
					// launched at nothing, never leaves the tile, and re-grabs it once `_lastPoleTime` runs
					// out - forever. The original does the same run around the test level's spring chain
					// without ever sticking.
					_speed.X = std::clamp(std::abs(lastSpeed) * LegacyHPoleLaunchScale,
						LegacyHPoleMinLaunch, LegacyHPoleMaxLaunch) * sign;
					_externalForce.X = 0.0f;
				}
				SetFacingLeft(!positive);

				_keepRunningTime = 60.0f;

				SetPlayerTransition(AnimState::Dash | AnimState::Jump, true, true, SpecialMoveType::None);
			} else {
				MoveInstantly(Vector2f(0.0f, sign * 16.0f), MoveType::Relative | MoveType::Force);

				// Non-Reforged: stronger vertical launch so spring->pole chains gain ~50% more height
				if (_levelHandler->IsReforged()) {
					_speed.Y = 4.0f * sign + lastSpeed * 1.4f;
					_externalForce.Y = 1.3f * sign;
				} else {
					// Measured: the pole returns the speed it was entered with and adds a fixed bonus on top,
					// which is what makes chained poles and spring-into-pole compound the way they do
					_speed.Y = (LegacyPoleLaunchBonus + std::abs(lastSpeed)) * sign;
					_externalForce.Y = 0.0f;
					// The ascent that follows decays at the *released* rate - measured over 25 consecutive
					// ticks as exactly 0.875 a tick, against the 0.375 a held jump gets - even with jump held.
					// Without it the rise is nearly three times too long, and since a chained pole is entered
					// with whatever survives the previous ascent, the error compounds down a chain: two poles
					// reached 772 px instead of 440.
					//
					// Except off a spring, where the same pole decays at 0.375 instead. Read off the original:
					// ob_vpole, reached by jumping, gives 0.875 a tick; ob_spring_vpole, reached off a
					// spring with no key ever pressed, gives 0.375 for 25+ consecutive ticks. Same pole, same
					// key state, so what decides it is how the player arrived - and forcing the released rate
					// left a spring-fed pole rising 535 px against the original's 804.
					_jumpReleased = !_poleEnteredOnSpring;
				}
			}

			SetState(ActorState::ApplyGravitation, true);
			_isAttachedToPole = false;
			_wasActivelyPushing = false;

			_controllableTimeout = 4.0f;
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
					_airboardSound->setLooping(true);
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

				SetCopterFlight(_flyCheatActive ? 1e6f : (10.0f * FrameTimer::FramesPerSecond),
					_flyCheatActive ? FlightType::Cheat : FlightType::Normal);

#if defined(WITH_AUDIO)
				if (_copterSound == nullptr) {
					_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
					if (_copterSound != nullptr) {
						_copterSound->setLooping(true);
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
				_activeModifierDecor = decor;
				_activeModifierDecor->OnDetach(this);


				SetCopterFlight(3.0f * FrameTimer::FramesPerSecond, FlightType::Normal);
				break;
			}

			default: {
				SetState(ActorState::CollideWithTileset | ActorState::CollideWithTilesetReduced, true);
				SetCopterFlight(0.0f);
				_activeModifier = Modifier::None;

#if defined(WITH_AUDIO)
				if (_copterSound != nullptr) {
					_copterSound->stop();
					_copterSound = nullptr;
				}
				if (_airboardSound != nullptr) {
					_airboardSound->stop();
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
		if (!IsFlyCheatActive()) {
			_copterFramesLeft = 0.0f;
		}
		_pushFramesLeft = 0.0f;
		SetState(ActorState::CanJump, false);
		_isAttachedToPole = false;

		if (_health > 0) {
			_externalForce.X = pushForce;

			if (!_inWater && _activeModifier == Modifier::None) {
				_speed.Y = -6.5f;

				SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects, true);
				SetAnimation(AnimState::Idle);
			}

			SetPlayerTransition(AnimState::Hurt, false, true, SpecialMoveType::None, [this]() {
				_controllable = true;
			});

			float invulnerableTime = _levelHandler->GetHurtInvulnerableTime();
			SetInvulnerability(invulnerableTime, InvulnerableType::Blinking);
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
			_renderer.AnimPaused = true;
			_controllable = false;
			_controllableTimeout = timeLeft;
		} else if (_frozenTimeLeft > 0.0f) {
			_controllable = true;
			_controllableTimeout = 0.0f;
		}

		_frozenTimeLeft = timeLeft;

		return true;
	}

	void Player::SetInvulnerability(float timeLeft, InvulnerableType type)
	{
		if (timeLeft <= 0.0f) {
			if (_invulnerableTime > 0.0f) {
				SetState(ActorState::IsInvulnerable, false);
				_invulnerableTime = 0.0f;
				_shieldSpawnTime = ShieldDisabled;
				_renderer.setDrawEnabled(true);
			}
			return;
		}

		if (type == InvulnerableType::Shielded) {
			if (_invulnerableTime > 0.0f) {
				// If the players is already blinking, show it now
				_renderer.setDrawEnabled(true);
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
			// Cleared here as well as when it runs out, so a move cut short - by a ceiling, water, damage -
			// hands the weightless gravity back instead of carrying it into the fall
			_uppercutTimeLeft = 0.0f;

			if (_externalForce.Y < 0.0f) {
				_externalForce.Y = 0.0f;
			}
		} else if (_currentSpecialMove == SpecialMoveType::Sidekick) {
			CancelTransition();
			_controllable = true;
			_controllableTimeout = 10;

			// Whether the dash stops dead or coasts on is decided where its distance runs out, in
			// CheckEndOfSpecialMoves() - Lori's stops, Spaz's keeps the walk cap and decelerates normally -
			// so no speed is forced here. The remaining distance is dropped, though: it belongs to this kick
			// and a move cut short must not leave it armed to fire during ordinary movement later.
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
		constexpr std::int32_t HealthLimit = 5;

		if (_health >= HealthLimit) {
			return false;
		}

		if (amount < 0) {
			_health = std::max(_maxHealth, HealthLimit);
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
		_gemsPitch++;
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

		// Mirrors the expiration path in OnUpdate() - only revert the renderer if sugar rush is what set it
		if (_renderer.GetRendererType() == ActorRendererType::PartialWhiteMask) {
			_renderer.Initialize(ActorRendererType::Default);
		}
	}

	bool Player::AddAmmo(WeaponType weaponType, std::int16_t count)
	{
		constexpr std::int16_t Multiplier = 256;
		constexpr std::int16_t AmmoLimit = 99 * Multiplier;

		if (weaponType >= WeaponType::Count || _inventory.WeaponAmmo[(std::int32_t)weaponType] < 0 || _inventory.WeaponAmmo[(std::int32_t)weaponType] >= AmmoLimit) {
			return false;
		}

		bool switchTo = (_inventory.WeaponAmmo[(std::int32_t)weaponType] == 0);

		_inventory.WeaponAmmo[(std::int32_t)weaponType] = (std::int16_t)std::min((std::int32_t)_inventory.WeaponAmmo[(std::int32_t)weaponType] + count * Multiplier, (int32_t)AmmoLimit);

		if (switchTo) {
			SetCurrentWeapon(weaponType, SetCurrentWeaponReason::AddAmmo);

			switch (_currentWeapon) {
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

		PlayerType playerTypePrevious = _playerType;

		_playerType = type;

		// Load new metadata, indexed only when recolored (must match the renderer's current palette state)
		bool useIndexed = (GetEffectiveFurColor() != 0);
		RequestMetadata(GetCharacterTraits(type).Metadata, useIndexed);

		// Refresh animation state
		if ((_currentSpecialMove == SpecialMoveType::None) ||
			(_currentSpecialMove == SpecialMoveType::Buttstomp && (type == PlayerType::Jazz || type == PlayerType::Spaz || type == PlayerType::Lori))) {
			AnimState prevAnim = _currentAnimation->State;
			_currentAnimation = nullptr;
			if (!SetAnimation(prevAnim)) {
				if (!SetAnimation(AnimState::Idle)) {
					return false;
				}
			}
		} else {
			_currentAnimation = nullptr;
			if (!SetAnimation(AnimState::Fall)) {
				return false;
			}

			SetState(ActorState::ApplyGravitation, true);
			_controllable = true;

			if (_currentSpecialMove == SpecialMoveType::Uppercut && _externalForce.Y < 0.0f) {
				_externalForce.Y = 0.0f;
			}

			_currentSpecialMove = SpecialMoveType::None;
		}

		// Set transition
		if (type == PlayerType::Frog) {
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
		} else if (playerTypePrevious == PlayerType::Frog) {
			_controllable = false;
			_controllableTimeout = 120.0f;

			SetTransition(AnimState::TransitionFromFrog, false, [this]() {
				_controllable = true;
				_controllableTimeout = 0.0f;
			});
		} else {
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X - 12.0f), (std::int32_t)(_pos.Y - 6.0f), _renderer.layer() + 4), Explosion::Type::SmokeBrown);
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X - 8.0f), (std::int32_t)(_pos.Y + 28.0f), _renderer.layer() + 4), Explosion::Type::SmokeBrown);
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)(_pos.X + 12.0f), (std::int32_t)(_pos.Y + 10.0f), _renderer.layer() + 4), Explosion::Type::SmokeBrown);

			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)(_pos.Y + 12.0f), _renderer.layer() + 6), Explosion::Type::SmokeBrown);
		}

		return true;
	}

	void Player::MorphRevert()
	{
		MorphTo(_playerTypeOriginal);
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
			// The caller's search tests the player's box rather than their centre, so it reaches about 52 px
			// - measurably further than the original, which throws the player at 36 px and not at 48
			if (distanceSqr > LegacyRFBlastReach * LegacyRFBlastReach) {
				return false;
			}
			// A straight speed assignment held for a fixed time, which is what the original does - the throw
			// is the same on the ground as in the air and does not decay while it lasts.
			//
			// The upward kick applies only off the ground. Standing on the floor the original does lift the
			// player, but by an amount that depends on the range: about 10 px parked a tile out (it assigns
			// ys = -4.2488 there) and 0.7 px fired point blank into the wall, which is what happens here
			// already. Two points do not settle that curve, so nothing is applied on the ground rather than
			// trading one measured scenario for the other - see the reference page.
			_speed.X = LegacyRFBlastSpeed * sign;
			_rfBlastLeft = LegacyRFBlastHoldTicks;
			if (!GetState(ActorState::CanJump)) {
				_speed.Y = -LegacyRFBlastRiseSpeed;
				// The original's rise after a blast decays at the released rate even with jump still held,
				// which is 29 px of height - so the blast ends the player's claim on the ascent
				_internalForceY = 0.0f;
				_jumpReleased = true;
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
		// Only chips away while there's a comfortable margin left, so an absorbed hit never drops the shield right
		// before it would have expired anyway
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
			Vector3i((std::int32_t)pos.X, (std::int32_t)pos.Y, _renderer.layer() + 80),
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
			_renderer.setRotation(0.0f);
		}
	}

	void Player::UpdateCarryingObject(ActorBase* actor, SuspendType suspendType)
	{
		DEATH_DEBUG_ASSERT(actor != nullptr);

		if (_carryingObject != nullptr && _carryingObject != actor) {
			return;
		}

		_carryingObject = actor;

		_canDoubleJump = true;

		if (suspendType == SuspendType::SwingingVine) {
			_suspendType = suspendType;
			SetState(ActorState::ApplyGravitation, false);
		} else if (_suspendType == SuspendType::SwingingVine) {
			_suspendType = SuspendType::None;
			SetState(ActorState::ApplyGravitation, true);
			_renderer.setRotation(0.0f);
		}
	}

	bool Player::ApplyPlayerBump(Player& other, bool stackingEnabled)
	{
		// Compute the overlap of the two collision boxes and resolve along the axis of least penetration (minimum
		// translation vector), the standard way to keep two AABBs from interpenetrating
		const AABBf& a = AABBInner;
		const AABBf& b = other.AABBInner;
		float overlapX = std::min(a.R, b.R) - std::max(a.L, b.L);
		float overlapY = std::min(a.B, b.B) - std::max(a.T, b.T);
		if (overlapX <= 0.0f || overlapY <= 0.0f) {
			// Boxes don't actually overlap (e.g., per-pixel collision reported the hit) - nothing to separate
			return false;
		}

		if (overlapY <= overlapX && stackingEnabled) {
			// Vertical overlap is the smaller axis and stacking is enabled - one player is standing/landing on the
			// other. That is resolved by UpdatePlayerStacking (the player below acts as a one-way platform), so don't
			// bump them apart vertically.
			return false;
		}

		// Equal-mass elastic bump along the least-penetration axis, applied only while the two are approaching so it
		// can't compound across frames. This is the "bump apart" behavior (always for side-to-side contact, and for
		// vertical contact too when player stacking is disabled). MoveInstantly checks the tilemap, so a player is
		// never pushed into a wall (it just stays put if blocked).
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
		ActorBase* ground = _levelHandler->FindPlayerToStandOn(this, timeMult);
		if (ground != nullptr) {
			if (snap) {
				// Reposition so our feet rest on the other player's head, re-snapping each frame so we ride its
				// vertical movement. The head is derived from the other player's position plus OUR collision
				// head-offset (players are the same size) rather than its AABB top - a client's RemoteActor uses the
				// sprite box, which is taller and would leave us floating. We sink PlayerStackSinkDepth px into the
				// player below so the two read as one connected stack instead of the upper one floating on its head.
				float groundHead = ground->GetPos().Y + (AABBInner.T - _pos.Y);
				float delta = groundHead - AABBInner.B + PlayerStackSinkDepth;
				if (delta != 0.0f) {
					MoveInstantly(Vector2f(0.0f, delta), MoveType::Relative);
				}
			}
			// Carrying makes CanJump() return true (so we can jump off) and Player::OnUpdatePhysics zeroes our
			// vertical speed so we rest on top instead of falling through.
			UpdateCarryingObject(ground);
			_stackCarrying = true;
		} else if (_stackCarrying) {
			// We were standing on a player and aren't anymore (walked off / jumped) - stop carrying so we fall again.
			// Guarded by _stackCarrying so we never cancel a real solid object we might be standing on.
			CancelCarryingObject();
			_stackCarrying = false;
		}
	}

	bool Player::IsBeingStoodOnByPlayer() const
	{
		// A player B is standing on us when its stacking resolver picked us as the ground (B->_carryingObject == this).
		// This sees only locally-simulated players: all of them in local co-op and on the server, just our own on a
		// client - which is why the cosmetic online animation is driven from the server (see PushSolidObjects).
		for (auto* other : _levelHandler->GetPlayers()) {
			if (other != this && other->_stackCarrying && other->_carryingObject == this) {
				return true;
			}
		}
		return false;
	}
}
