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
	// Which bits survive into the next frame's animation state. Bits 16-18 - Freefall, Lift and Spring - are
	// deliberately *outside* it because each is decided fresh every frame, and bit 19, @ref AnimState::RevUp,
	// belongs with them for the same reason. It was inside the mask when the bit was unused, which meant the
	// rev-up pose, once entered, was carried forward for ever: the player launched, ran at dash speed, and
	// went on being drawn winding up on the spot until something else reset the animation outright.
	static constexpr AnimState CompositeAnimMask = (AnimState)0xFFF03F60;
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
		_runActionSeeded(false),
		_revUpCharge(0.0f), _revUpHeldCharge(0.0f), _revUpPresses(0), _revUpArmed(false), _revUpWound(false), _revUpWindowLeft(0.0f), _revUpChargeTime(0.0f), _revUpSparkCooldown(0.0f), _revUpEndLeft(0.0f), _stopPhase(StopPhaseNone), _hookIdleTime(0.0f), _revUpLaunchLeft(0.0f), _revUpPendingSpeed(0.0f), _revUpStarted(false),
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
		_uppercutTimeLeft(0.0f), _jumpReleased(false), _poleEnteredOnSpring(false), _riseFromFloatUp(false), _inFloatUpArea(false),
		_crouchHeldBefore(false),
		_inIdleTransition(false), _inLedgeTransition(false), _stopPhaseChanging(false), _inHookIdleFlavor(false), _canDoubleJump(true),
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
		_hPoleCarry(false),
		_springCarry(false),
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

	bool Player::IsOnLegacyOneWayFloor()
	{
		// A one-way platform does not stop a rising player in either game - both pass straight up through
		// one and both land on it coming down, measured frame for frame on `ow_hop`. What differs is what
		// the *jump* test makes of it: in the original the platform's own mask still answers "is there
		// ground under the feet", so crossing one with jump held launches a fresh jump, and a ladder of
		// them can be climbed by simply holding the key. Measured on `ow_jump` - from the platform at row
		// 33 the original's rise resets to exactly -10 as the feet cross rows 31, 29 and 26, three jumps
		// where this engine took one and stopped four tiles up.
		//
		// This engine cannot answer it the same way, because `ActorState::CanJump` is cleared outright for
		// any actor with `_speed.Y < 0` and is also what picks the ground-bound slope path in
		// TryMoveSubstep() - setting it while rising would have the slope search snap the player back down
		// onto the platform they are passing through. So the question is asked separately, here, and only
		// the jump gate reads it.
		if (_levelHandler->IsReforged() || _speed.Y >= 0.0f || _suspendType != SuspendType::None ||
			(_state & ActorState::ApplyGravitation) != ActorState::ApplyGravitation) {
			return false;
		}

		// The same box asked twice. With `Downwards` a one-way counts as solid and without it as empty, so
		// a surface that is solid only in the first answer is a one-way and nothing else. Asking once would
		// also catch the real floor a jump has just left, whose feet are still inside this box for a frame
		// or two, and re-launch the jump every frame - an unbounded rise from any ordinary standing jump.
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

	bool Player::CanBreakSolidObjects() const
	{
		if (_sugarRushLeft > 0.0f) {
			return true;
		}

		if (_currentSpecialMove == SpecialMoveType::Buttstomp && _currentTransition != nullptr) {
			// Buttstomp is probably in starting transition, do nothing yet
			return false;
		}

		// The other two need the same rule and never had it: a move that has *begun* but not yet driven
		// breaks nothing. Started while standing on a powerup monitor, an uppercut or a sidekick destroyed
		// the monitor on its first tick - so the thing the player was standing on vanished and they fell
		// through the whole wind-up, which is the reported "the player drops to the floor first". Measured on
		// `pu_stand_upper`: the original holds the player at a constant 1943.5 for the entire fourteen-tick
		// wind-up, where we sank 13.3 px before launching.
		//
		// Recognised by the move's own budget rather than by the transition, because the transition does not
		// mean the same thing for each: the budget is armed by the launch callback, so until it is there is
		// no drive. Lori's kick arms hers at the trigger instead of in a callback and so is never caught
		// here - hers goes on breaking whatever it kicks, which is what it should do.
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

		// Inside a float-up column the original applies no gravity at all: it assigns a rise speed and the
		// player travels exactly that far, 8.0 px on every tick measured inside a solid ten-tile column.
		// Ours travelled 7.79 of the original's units, because @ref LegacyFloatUpSpeed is *equal* to the
		// applied rise cap and so leaves no headroom - a launch assigned above the cap has its half-step of
		// gravity clamped back off by @ref _maxRiseSpeed and travels the full amount, and this one cannot.
		// Returning zero here covers both places gravity lands, the velocity-Verlet half-step before the move
		// and the application after it, which is what "no gravity that tick" has to mean.
		if (_inFloatUpArea) {
			return 0.0f;
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
		//
		// A float-up area is the one launch source that answers this from the **live key** rather than from
		// whether the jump was let go during the ascent - there was no jump to let go of. Measured on
		// `fu_col_none`, which presses nothing at all: the original leaves the column decaying at 0.875 and
		// eases to 0.625 for the last pixel per tick, exactly as a released jump does, where reading
		// `_jumpReleased` gave it the *held* 0.375 and overshot the top of the column by 35 px. The flag
		// cannot simply be set instead: a spring and a pole with nothing pressed both measure 0.375, so the
		// sources disagree on what "nothing pressed" means and the rise has to remember which one it came
		// from. See `movement-accuracy-objects-poleascent`.
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
			// The float-up marker describes an ascent, so it lapses the moment there isn't one - otherwise a
			// later jump or spring would inherit a rate that belongs to a column the player has long left
			if (_speed.Y >= 0.0f) {
				_riseFromFloatUp = false;
			}
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

				// Her kick accelerates from a sixth of its peak rather than starting there. The direction comes
				// from which way she is **facing**, not from the sign of the speed the ramp is replacing:
				// anything that stops her mid-kick zeroes `_speed.X`, and `std::copysign` reads positive zero
				// as positive, so the very next tick used to relaunch the whole ramp *rightwards* whatever
				// direction the kick was going. Kicking leftwards into the level's pushable, that threw her
				// 48.2 px backwards in a single step - 2811.3 to 2859.5, where the original stops at 2816.3
				// and stays - which is the reported bounce off a pushable rock. It could only ever show up
				// kicking left, because a rightward kick's sign was already the one copysign invented.
				if (_playerType == PlayerType::Lori) {
					_sidekickTime += timeMult;
					float ramp = LegacyLoriKickRamp * _sidekickTime * _sidekickTime;
					_speed.X = (IsFacingLeft() ? -ramp : ramp);
				}
			} else if (_inTubeTime > 0.0f) {
				// A sucker tube drives the player directly and asks for speeds well above the cap (a level can
				// set 20 or more), so it is exempt here for the same reason the vertical limit is exempt above -
				// otherwise a horizontal tube crawls at less than half the rate the level asked for.
				_maxAppliedSpeedX = std::max(LegacyAppliedSpeedCap, std::abs(_speed.X));
			} else if (std::int32_t accBelt = GetAccBeltStrength()) {
				// An accelerating belt is raised rather than exempt: measured, a strength-8 one moves the
				// original's player 12 px/tick, straight through a cap of 8 - but a *cap* is still what it
				// is, because with Run held the belt reports 18 and 24 while moving only 10 and 16. The
				// ceiling is the belt's own strength plus the walk cap, with the Run bonus deliberately left
				// out of it: Run raises what the belt drives to by twelve (@ref LegacyAccBeltRunBonus) and
				// what it actually moves the player by four. All four measured combinations fit that exactly
				// - 6 and 12 travelled in full because the ceiling is above them, 18 and 24 held to 10 and
				// 16 - and the ramp does too, since travel tracks the speed up to the ceiling rather than
				// starting there. The plain belt and the wind need no ceiling at all, because they move the
				// *position* and never touch the speed.
				float beltCap = std::abs((float)accBelt) * LegacyAccBeltSpeed + LegacyWalkSpeed;
				_maxAppliedSpeedX = std::max(LegacyAppliedSpeedCap, beltCap);
			} else if (_hPoleCarry && _keepRunningTime > 0.0f) {
				// And so is a horizontal pole launch, for its whole 70-tick carry: the original reports 20
				// px/tick and moves 20. This one is fitted to the pole rather than to a rule, because the
				// carry that most resembles it measures the opposite - a horizontal spring reports 32 and
				// still moves 8 - so "a carry ignores the cap" is contradicted by the pair. Both are
				// grounded and both have a direction held, so neither of those separates them either.
				_maxAppliedSpeedX = std::max(LegacyAppliedSpeedCap, std::abs(_speed.X));
			} else {
				// The marker lapses with the carry it describes, so nothing later inherits the exemption
				_hPoleCarry = false;
				_maxAppliedSpeedX = LegacyAppliedSpeedCap;
			}
			// Only the player gets the original's landing allowance - it is measured off the player, and the
			// shared collision code would otherwise hand it to every enemy and pickup in the level as well
			_landingTolerance = LegacyLandingTolerance;
		}

		// The tube's exemption from the hard speed clamps is **not** gated, because the tube is not: a level
		// can ask for 20 or 30 px/tick on either axis and both modes have to deliver it, or the shared speed
		// conversion above is undone by a clamp a moment later. Reforged's defaults are 16 on both axes,
		// which truncates any tube stronger than that.
		//
		// Only limits that already exist are raised, never introduced - `_maxAppliedSpeedX` of 0 means "no
		// cap" and must stay 0 rather than becoming a cap at the current speed. The applied *rise* cap is
		// deliberately left alone: that one is the movement model rather than the tube, and dragging it into
		// Reforged would change every launch, not this ride.
		if (_inTubeTime > 0.0f) {
			_verticalSpeedLimit = std::max(_verticalSpeedLimit, LegacyVerticalSpeedLimit);
			_horizontalSpeedLimit = std::max(_horizontalSpeedLimit, LegacyVerticalSpeedLimit);
			if (_maxAppliedSpeedX > 0.0f) {
				_maxAppliedSpeedX = std::max(_maxAppliedSpeedX, std::abs(_speed.X));
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

		// A one-way platform crossed on the way up with jump held relaunches the jump - see
		// TryLegacyOneWayRejump(), which explains why this is here and not in HandleJump()
		TryLegacyOneWayRejump(timeMult);

		// The original bounds the *top* of the level, and only the position: measured on `sp_pole_loop` it
		// reaches y = 0 and stays there for 69 ticks while `ys` carries on decaying at the ordinary rise
		// gravity - −26.00, −25.625, −25.25 - until it turns positive and the player falls away by itself.
		// The speed is deliberately left alone; zeroing it would end the rise early and drop the player
		// sooner than the original does. Ours had no top bound at all and overshot by 469 px there, which
		// cost 51 ticks coming back down, and it is most of the pinball family's residual as well - a dozen
		// `pb_*` read −13 to −469 where the original reads exactly 0.
		//
		// After the move, for the same reason TryLegacyOneWayRejump() is: the level-bounds block near the
		// top of this function runs *before* it, so clamping there left the frame's own step still to come
		// and the player ended one frame's rise above the bound - 9.3 px at the applied cap, which is
		// exactly what the first attempt measured.
		if (!_levelHandler->IsReforged()) {
			float topBound = float(_levelHandler->GetLevelBounds().Y);
			if (_pos.Y < topBound) {
				MoveInstantly(Vector2f(_pos.X, topBound), MoveType::Absolute | MoveType::Force);
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
		UpdateLoriKickRepeat(timeMult);
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
		if (_springHoldLeft > 0.0f) {
			_springHoldLeft -= timeMult;
			if (_springHoldLeft <= 0.0f) {
				_springHoldLeft = 0.0f;
				// A horizontal spring carries its own launch figure for four ticks and then ends at the same
				// 16 every other carry does - the tube, the accelerating belt and the sidekick all clamp to
				// it, and the spring was the one site that did not. Positions do not move: the applied cap
				// already holds the travel at 8 px a tick either way. What changes is the speed anything else
				// reads. See @ref LegacySpringHoldTicks.
				_speed.X = std::clamp(_speed.X, -LegacyCarryExitRunSpeed, LegacyCarryExitRunSpeed);
			}
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
					!_inIdleTransition && !_isAttachedToPole) {

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
				//
				// With Run held it clamps to 16 instead - see @ref LegacyCarryExitRunSpeed. A tube carries 8
				// px/tick, so that clamp is inert here and the ride simply continues; clamping to the walk cap
				// regardless took this engine from 212.8 px to 166.4 against the original's 222.9, *losing* a
				// fifth of the distance for holding a key that should if anything help.
				//
				// Shared with Reforged like the rest of the tube - see the note where the ride is set up. The
				// two constants keep their `Legacy` names because they are measured figures that the *other*
				// carries (the belt, the sidekick) still use only outside Reforged; it is this site that is
				// unconditional, not the numbers.
				float exitCap = (_isRunPressed ? LegacyCarryExitRunSpeed : LegacyWalkSpeed);
				_speed.X = std::clamp(_speed.X, -exitCap, exitCap);
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
			if (!_runActionSeeded) {
				// A Run key already held when the level starts never produces a rising edge inside it: the level
				// handler refreshes `PressedActionsLast` every frame, warp-in included, so the press is consumed
				// before this runs for the first time and the toggle would stay off until the key was released
				// and hit again. Seeding it from the live key state is what makes a held Run mean "running" at
				// the start of a level the way it does everywhere else.
				// Latched once the key is actually *seen* down rather than on the first frame this runs.
				// Reported as "starting a level with Run held sometimes does not run until you press it
				// again", and the *sometimes* is the tell: on the first frame after a load the input has not
				// necessarily been sampled yet, so seeding there reads "not pressed" for a key that is being
				// held, latches false, and then waits for a rising edge that a held key will never give.
				// Until it latches the flag simply follows the key, which is what an unpressed Run means
				// anyway - so nothing changes for a player who starts the level not holding it.
				_isRunPressed = _levelHandler->PlayerActionPressed(this, PlayerAction::Run);
				_runActionSeeded = _isRunPressed;
			} else if (_levelHandler->PlayerActionHit(this, PlayerAction::Run)) {
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
			// ...but a sidekick can still shoot outside Reforged, and it is this early return that took that
			// away: losing control is what a special move does, and the return sits *before* HandleWeaponFire()
			// rather than after it, so the whole of a kick was silently disarmed. Measured on
			// `sp_lori_side_fire`: the original spends a round at tick 43, four ticks in and still
			// accelerating through the ramp, where we spend none at all. It was reported as a quirk and it is
			// one, but it is the original's behaviour and this was the only thing standing in its way.
			//
			// `_controllableExternal` is still honoured, so whatever takes control away from *outside* the
			// player's own moves - a cutscene, a level exit, a warp - goes on blocking the shot. Only the
			// player's own kick is excused, and only the kick: naming the special move rather than testing
			// `_controllable` keeps the buttstomp above out of it.
			if (!_levelHandler->IsReforged() && _controllableExternal &&
				_currentSpecialMove == SpecialMoveType::Sidekick) {
				HandleWeaponFire(areaWeaponAllowed);
			}
			return;
		}

		if (_inWater || _activeModifier != Modifier::None) {
			HandleWaterAndModifierMovement(timeMult);
		} else {
			// Captured before the crouch can be set this frame - HandleLookupAndCrouch() sets it and
			// HandleJump() reads it immediately after, so nothing downstream could otherwise tell Down and
			// Jump on the same tick from a crouch that was already up. See IsSpecialMoveCrouchReady().
			_crouchHeldBefore = ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch);
			HandleLookupAndCrouch(timeMult, canJumpPrev);
			HandleJump(timeMult);
			// After the jump, so a jump out of a wind-up is the jump rather than a launch, and after the crouch,
			// which reads the same Down key. Not gated on Reforged: this is a mechanic the engine lacks in both
			// modes rather than a difference between them.
			UpdateRevUp(timeMult);
		}

		HandleWeaponFire(areaWeaponAllowed);
	}

	bool Player::IsRevvingUp() const
	{
		return (_revUpWound && _revUpWindowLeft > 0.0f);
	}

	void Player::CancelRevUp()
	{
		// A pending light launch counts as something to cancel even with the charge already spent, or being hit
		// or hitting a wall during the wait would still fire the launch a moment later
		if (_revUpCharge <= 0.0f && !_revUpWound && _revUpLaunchLeft <= 0.0f) {
			return;
		}

		_revUpCharge = 0.0f;
		_revUpHeldCharge = 0.0f;
		_revUpPresses = 0;
		_revUpArmed = false;
		_revUpWound = false;
		_revUpWindowLeft = 0.0f;
		_revUpChargeTime = 0.0f;
		_revUpStarted = false;
		_revUpEndLeft = 0.0f;
		_revUpLaunchLeft = 0.0f;
		_revUpPendingSpeed = 0.0f;
		// The pose runs at a charge-dependent speed, so the ordinary duration has to come back with it or the
		// next animation inherits however fast the wind-up happened to be going
		_renderer.AnimDuration = _currentAnimation->AnimDuration;
	}

	void Player::ApplyRevUpLaunch()
	{
		_speed.X = _revUpPendingSpeed;
		_externalForce.X = 0.0f;
		_keepRunningTime = RevUpKeepRunningTime;
		// Not a spring's carry, so it does not take the spring's refusal of the crouch with it - see
		// @ref _springCarry for why that is left unmeasured rather than assumed
		_springCarry = false;
		// The hold ends in a single-tick snap to the walk cap rather than a decay, which is the dash grace's
		// own behaviour - so the grace is armed to outlast the no-friction window by a hair and deliver it.
		// UpdateDashState() is non-Reforged only, so Reforged decays out of the hold instead.
		_dashGraceLeft = RevUpKeepRunningTime + 2.0f;
		_revUpLaunchLeft = 0.0f;
		_revUpPendingSpeed = 0.0f;
	}

	void Player::UpdateRevUp(float timeMult)
	{
		// A light launch waits at a standstill through its three animation phases before it moves. Counted down
		// before anything else, so the wait cannot be restarted by taps that are still arriving.
		if (_revUpLaunchLeft > 0.0f) {
			float before = _revUpLaunchLeft;
			_revUpLaunchLeft -= timeMult;
			// The start animation waits for the base pose to be in place before it goes in. Fired at the moment
			// the launch is scheduled, the base is still Idle, and UpdateAnimation() changing it to RevUp on the
			// next frame cancels the transition after a single frame - the same trap the wound path hit.
			if (!_revUpStarted && (_currentAnimation->State & AnimState::RevUp) == AnimState::RevUp) {
				_revUpStarted = true;
				SetPlayerTransition(AnimState::TransitionRevUpStart, true, false, SpecialMoveType::None);
			}
			// The end animation goes in once the start animation and the loop after it have had their time. The
			// base pose stays RevUp throughout (see `_revUpEndLeft`), so the loop is what shows in between - the
			// two transitions fired back to back left the player flicking from one pose straight to the other.
			if (before > RevUpEndTime && _revUpLaunchLeft <= RevUpEndTime) {
				SetPlayerTransition(AnimState::TransitionRevUpEnd, true, false, SpecialMoveType::None);
			}
			if (_revUpLaunchLeft <= 0.0f) {
				ApplyRevUpLaunch();
			}
			return;
		}

		// Only on the floor, standing still, with nothing else going on. A direction held is the whole point of
		// *in place*, and it is also how the wind-up is abandoned: start walking and the count is dropped.
		bool eligible = (_controllable && _controllableExternal && CanJump() && !_isLifting &&
			_currentSpecialMove == SpecialMoveType::None && _suspendType == SuspendType::None &&
			_activeModifier == Modifier::None && !_inWater && _dizzyTime <= 0.0f &&
			std::abs(_levelHandler->PlayerHorizontalMovement(this)) <= 0.4f &&
			std::abs(_speed.X) < 1.0f);

		if (!eligible) {
			CancelRevUp();
			return;
		}

		// The rising edge of the *button*, not of `_isRunPressed`: with `ToggleRunAction` the latter flips once
		// per press and would count a press and its release as one tap in one mode and two in the other
		if (_levelHandler->PlayerActionHit(this, PlayerAction::Run)) {
			_revUpCharge += RevUpChargeGainTap;
			_revUpHeldCharge = 0.0f;
			_revUpPresses++;
			_revUpWindowLeft = RevUpTapWindow;
		} else if (_levelHandler->PlayerActionPressed(this, PlayerAction::Run)) {
			// Holding adds too, which is what lets a third tap held a little longer do the work of a fourth -
			// but only up to a cap per press, or one long press would wind the player up on its own
			float gain = std::min(RevUpChargeGainHeld * timeMult, RevUpChargeHeldCap - _revUpHeldCharge);
			if (gain > 0.0f) {
				_revUpCharge += gain;
				_revUpHeldCharge += gain;
			}
			_revUpWindowLeft = RevUpTapWindow;
		} else if (_revUpCharge > 0.0f) {
			_revUpCharge = std::max(_revUpCharge - RevUpChargeDecay * timeMult, 0.0f);
			if (_revUpArmed) {
				_revUpWindowLeft -= timeMult;
			}
		}

		// Two thresholds, because there are two outcomes. Enough charge to be *armed* but not to wind up -
		// about three plain taps - leaves the player standing idle, and letting the key go then plays the start
		// and end animations and sets them running anyway. Only past the second threshold does the wind-up pose
		// itself appear, and from there the launch grows with how long it runs.
		if (_revUpCharge >= RevUpChargeLightThreshold && _revUpPresses >= RevUpPressesRequired) {
			_revUpArmed = true;
		}
		if (!_revUpWound) {
			if (_revUpArmed && _revUpCharge >= RevUpChargeThreshold) {
				_revUpWound = true;
			} else if (!_revUpArmed || _revUpWindowLeft > 0.0f) {
				// Not yet, or still being fed. The charge simply bleeds away; there is nothing to cancel, and a
				// tap arriving before it reaches zero carries on from whatever is left rather than starting over.
				if (_revUpCharge <= 0.0f) {
					CancelRevUp();
				}
				return;
			}
			// Armed but never wound up, and the tapping has stopped - fall through to the launch below, which
			// plays both animations in turn since the wind-up pose was never shown
		}

		// Wound up. Charge follows the time spent revving rather than the tap count - 15 taps over 75 ticks
		// launched at 15.87 where 12 taps over 106 ticks reached the full 16.0. An armed-but-never-wound player
		// accumulates none of it, so their launch comes out at the minimum.
		if (_revUpWound) {
			_revUpChargeTime += timeMult;
		}
		float charge = std::min(_revUpChargeTime / RevUpFullChargeTime, 1.0f);

		if (_revUpWound && _revUpWindowLeft > 0.0f) {
			// The looping pose is chosen in UpdateAnimation(), for the same reason the pinball paddle's is: that
			// runs earlier in the frame and would overwrite anything assigned here, and re-assigning it from
			// here every frame restarts the animation on every frame - which shows as a single frozen frame.
			//
			// The *start* animation is different: it plays once, over the top, on the frame the wind-up begins,
			// which is what a transition is for. Guarded by a flag rather than by comparing animation states,
			// because the looping pose is assigned every frame and would re-trigger it.
			// Fired only once the looping pose is already the base state. Changing the base state cancels
			// whatever transition is playing over it, so firing this on the frame the wind-up begins - before
			// UpdateAnimation() has applied the pose - cut it off after a single frame. Waiting for the pose
			// means there is no state change left to cancel it, and the transition can stay *cancellable*,
			// which matters: a non-cancellable one that outlives its welcome is what left the player stuck in
			// the wind-up animation after launching.
			if (!_revUpStarted && (_currentAnimation->State & AnimState::RevUp) == AnimState::RevUp) {
				_revUpStarted = true;
				SetPlayerTransition(AnimState::TransitionRevUpStart, true, false, SpecialMoveType::None);
			}
			// Measured: the original's wind-up runs at one frame per 7 ticks when it starts and one per 2 at
			// full charge, so it ends up 3.5x faster than it began.
			//
			// Only while no transition is playing, and only while the base really is the wind-up pose. Writing
			// the renderer's duration unconditionally stamps over whatever RefreshAnimation() just set for the
			// *transition* as well, which is what made the end animation crawl - and it kept doing it after the
			// pose had moved on, which froze the whole thing on frame 0.
			if (_currentTransition == nullptr && (_currentAnimation->State & AnimState::RevUp) == AnimState::RevUp) {
				_renderer.AnimDuration = _currentAnimation->AnimDuration / (1.0f + charge * RevUpAnimSpeedUp);
			}

			// Sparks come at the same rate the pose runs at - they are the feet striking the ground, so they
			// have to quicken exactly as the feet do rather than on a schedule of their own
			_revUpSparkCooldown -= timeMult;
			if (_revUpSparkCooldown <= 0.0f) {
				_revUpSparkCooldown = RevUpSparkInterval / (1.0f + charge * RevUpAnimSpeedUp);
				EmitRevUpSparks(charge);
			}
			return;
		}

		// The tapping stopped - launch. The speed is assigned outright and friction suppressed, the way a
		// horizontal spring does it (see OnHitSpring()), and the direction is the one the player is facing:
		// measured, a direction key held at the moment of launch does not steer it, and neither does one
		// pressed during the hold that follows.
		float launchSpeed = lerp(RevUpMinLaunchSpeed, RevUpMaxLaunchSpeed, charge);
		_revUpPendingSpeed = (IsFacingLeft() ? -launchSpeed : launchSpeed);
		if (_revUpWound) {
			// Measured: the full wind-up moves on the very tick its end animation begins
			ApplyRevUpLaunch();
		} else {
			// The light case stands still through all three phases, and moves off exactly as the last one ends
			_revUpLaunchLeft = RevUpStartTime + RevUpLightMidTime + RevUpEndTime;
		}
		_wasActivelyPushing = false;
		_renderer.AnimDuration = _currentAnimation->AnimDuration;

		// Cleared before the transition, so UpdateAnimation() no longer claims the rev-up pose and the launch
		// animation is free to play over whatever the ordinary movement state has become
		bool wasWound = _revUpWound;
		_revUpCharge = 0.0f;
		_revUpHeldCharge = 0.0f;
		_revUpPresses = 0;
		_revUpArmed = false;
		_revUpWound = false;
		_revUpWindowLeft = 0.0f;
		_revUpChargeTime = 0.0f;
		_revUpStarted = false;

		// The base state is held at RevUp for the length of the animations instead of being switched to Dash
		// straight away - see UpdateAnimation(). That is what lets these stay cancellable: the state does not
		// change under them, so nothing cancels them, and because they *are* cancellable they can never leave
		// the player stuck in the wind-up pose if anything unexpected happens.
		if (wasWound) {
			_revUpEndLeft = RevUpEndTime;
			SetPlayerTransition(AnimState::TransitionRevUpEnd, true, false, SpecialMoveType::None);
		} else {
			// Armed but never wound up: nothing has been shown yet, so the whole thing plays now in miniature -
			// the start animation, then the wind-up loop, then the end animation. The base pose is held at RevUp
			// for all three, which is both what lets the loop show in the middle and what stops the base state
			// changing under the transitions and cancelling them. The end animation is fired from the countdown
			// above rather than from a callback here, so the loop gets its own time in between.
			_revUpEndLeft = RevUpStartTime + RevUpLightMidTime + RevUpEndTime;
		}
		PlayPlayerSfx("Jump"_s);
	}

	void Player::EmitRevUpSparks(float charge)
	{
		// Real debris rather than an explosion sprite: the feet are driving against the ground without the
		// player moving, so what comes off them is thrown *backwards* and then falls, which needs a velocity
		// and an acceleration. `Explosion::Create()` has neither - it just plays a sprite where it is put, which
		// is why the first attempt read as a small explosion under the player instead of sparks. Modelled on
		// ElectroShot::CreateParticles(), including the additive blending that makes a spark look hot.
		auto tilemap = _levelHandler->TileMap();
		if (tilemap == nullptr || _metadata == nullptr) {
			return;
		}

		auto* res = _metadata->FindAnimation((AnimState)0x4F000010); // RevUpSpark
		if (res == nullptr || res->Base->TextureDiffuse == nullptr) {
			return;
		}

		auto& resBase = res->Base;
		Vector2i texSize = resBase->TextureDiffuse->GetSize();
		// Away from the direction the player is trying to go
		float back = (IsFacingLeft() ? 1.0f : -1.0f);
		std::int32_t count = 1 + Random().Fast(0, 2 + (std::int32_t)(charge * 2.0f));

		for (std::int32_t i = 0; i < count; i++) {
			// A shallow backwards spray, faster the harder the wind-up. Kept low: sparks struck off the ground
			// travel along it, and a steep launch reads as something being thrown rather than scraped.
			float speed = Random().FastFloat(1.44f, 2.88f) * (1.0f + charge);
			float spread = Random().FastFloat(-0.6f, 0.2f);
			Vector2f dir = Vector2f(back * cosf(spread), -std::abs(sinf(spread)) * 0.5f - Random().FastFloat(0.05f, 0.25f));
			float size = Random().FastFloat(1.5f, 3.0f);

			Tiles::TileMap::DestructibleDebris spark = {};
			// Just behind the heel and a little above the floor, with only a pixel or two of scatter - a wide
			// random spread put them out from under the middle of the player, which looked like the ground
			// itself was sparking rather than the shoe
			spark.Pos = Vector2f(_pos.X + back * (RevUpSparkOffsetBack + Random().FastFloat(0.0f, 2.0f)),
				_pos.Y + 22.0f + RevUpSparkOffsetY);
			spark.Depth = _renderer.layer() - 2;
			spark.Size = Vector2f(size, size);
			spark.Speed = dir * speed;
			// Gravity only - no horizontal damping, or a bounce would have nothing left to carry it along
			spark.Acceleration = Vector2f(0.0f, 0.22f);

			spark.Scale = 1.0f;
			spark.ScaleSpeed = -0.004f;
			spark.Alpha = 1.0f;
			// A spark has to survive its first bounce and still read on the way back down, so it fades slowly -
			// and because the blending is additive, alpha *is* brightness here, so a slow fade is also what
			// keeps it looking solid rather than washed out. Paced to the shorter lifetime below, or they would
			// still be near full brightness at the moment they are removed.
			spark.AlphaSpeed = Random().FastFloat(-0.016f, -0.011f);
			spark.Angle = Random().FastFloat(0.0f, fRadAngle360);
			spark.AngleSpeed = Random().FastFloat(-0.4f, 0.4f);

			spark.Time = 48.0f;
			// Bounces off the floor rather than sinking through it, keeping a little under half its speed -
			// low enough that it settles after two or three hops instead of skittering away
			spark.Elasticity = 0.45f;

			std::int32_t frame = Random().Fast(0, std::max(1, resBase->FrameCount));
			Recti frameRect = resBase->GetFrameRect(frame);
			Vector2i frameOffset = resBase->GetFrameOffset(frame);
			// Without this a trimmed frame is stretched across its whole cell, which is what made the sparks
			// read as vague blobs rather than points of light
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
		// The target is keyed on the direction *held*, not on the speed the player happens to carry. Both
		// readings fit an ordinary run, and `g_dash_rel` separates them: the direction is released while the
		// player is still coasting at 9.6 px/tick and the original's lead collapses to zero anyway. The same
		// rule is what holds the view still through a sidekick, where nothing is pressed at all.
		float movement = _levelHandler->PlayerHorizontalMovement(const_cast<Player*>(this));
		if (std::abs(movement) <= 0.4f) {
			return 0.0f;
		}
		// ...and the player has to actually be going somewhere. Pressed against a wall the original's lead
		// comes back to zero with the direction still held: measured on `an_side_cancel`, it reaches +69.6
		// at the moment of contact and then recedes by exactly 0.997 px a tick, which is the approach's own
		// step cap doing the whole of it - so the target really is zero rather than merely smaller. Ours held
		// the full +119.4 for as long as the key was down.
		//
		// Read as *travel* and not as a speed, because the two disagree exactly here. The original's `xs`
		// reads 0.0000 against the wall, but ours keeps a phantom 0.4989: the collision stops the movement
		// without clearing the speed, and any threshold low enough to leave a genuine crawl alone - the lead
		// is already growing at 0.1831 px/tick on the tick the run starts - would be too low to catch it.
		// What separates them is that one of them is not moving.
		if (std::abs(_pos.X - _frameStartPos.X) < 0.001f) {
			return 0.0f;
		}
		return (movement < 0.0f ? -1.0f : 1.0f) * (IsDashActive() ? LegacyCameraDashLead : LegacyCameraWalkLead);
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

	void Player::ResetRevUpState()
	{
		_revUpCharge = 0.0f;
		_revUpHeldCharge = 0.0f;
		_revUpPresses = 0;
		_revUpArmed = false;
		_revUpWound = false;
		_revUpWindowLeft = 0.0f;
		_revUpChargeTime = 0.0f;
		_revUpSparkCooldown = 0.0f;
		_revUpEndLeft = 0.0f;
		_revUpLaunchLeft = 0.0f;
		_revUpPendingSpeed = 0.0f;
		_revUpStarted = false;
	}

	bool Player::IsSpecialMoveCrouchReady() const
	{
		if ((_currentAnimation->State & AnimState::Crouch) != AnimState::Crouch) {
			return false;
		}
		// Reforged fires the move on the live crouch bit, as it always has; only the measured rule is gated
		if (_levelHandler->IsReforged()) {
			return true;
		}
		// Grounded is asked outright rather than left to the pose. With the crouch now cleared in mid-air
		// (see HandleLookupAndCrouch()) the bit above can no longer be set off the ground, so this is the
		// second of two locks on the same door - but it is the one that states the rule, and the mid-air
		// uppercut it refuses was a 110 px climb out of a hole the player had just fallen into.
		return (_crouchHeldBefore && CanJump());
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
		_riseFromFloatUp = false;
		_inFloatUpArea = false;
		// A pole's carry, and its exemption from the applied cap, end with the life or the warp that took it -
		// and so does a spring's, and the refusal of the crouch that goes with it
		_hPoleCarry = false;
		_springCarry = false;
		// A spring's launch, and anything it armed, belong to the spring that gave them - not to the next
		// life or the far side of a warp. Reusing the game's own reset is what keeps this list from drifting.
		_springLaunchSpeedX = 0.0f;
		_springRebuildAccel = 0.0f;
		_springHoldLeft = 0.0f;
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
						float accelHere = acceleration;
						// A double jump out of a spring launch rebuilds the speed it took, rather than leaving
						// the player to walk it back up - see where this is armed in HandleSpecialJump(). Both
						// the rate and the ceiling differ from ordinary air control: we used to rebuild at the
						// walk acceleration and stop at the walk cap, which is 4, where the original climbs to
						// 16. Only while airborne; landing clears it, and ordinary air control is untouched.
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
						// Run raises the speed the belt drives to, by a flat eight strength rather than by a
						// factor - see @ref LegacyAccBeltRunBonus. Applied to the magnitude so a leftward belt,
						// whose strength is negative, is boosted rather than cancelled.
						std::int32_t strength = accBelt;
						if (_isRunPressed) {
							strength += (accBelt < 0 ? -LegacyAccBeltRunBonus : LegacyAccBeltRunBonus);
						}
						float target = strength * LegacyAccBeltSpeed;
						_speed.X = std::clamp(_speed.X + accBelt * LegacyAccBeltStep * timeMult,
							std::min(target, 0.0f), std::max(target, 0.0f));
					} else if (_wasOnAccBelt) {
						// Leaving one clamps to the walk cap, exactly as leaving a sucker tube does. Measured:
						// the original coasts ~87 px past the end of a belt where keeping the belt speed carries
						// the player 182, and 560 past on a strength-8 one.
						//
						// And, exactly as a sucker tube does, it clamps to 16 rather than to the walk cap while
						// Run is held - see @ref LegacyCarryExitRunSpeed. This is the site that revealed that
						// rule, because it is the only one whose carry is fast enough for the two clamps to
						// differ: both strengths snap from 18 and 24 to exactly 16 and then decay at the dash
						// brake, where the walk-cap clamp cost this engine 12% of the distance.
						float exitCap = (_isRunPressed ? LegacyCarryExitRunSpeed : LegacyWalkSpeed);
						_speed.X = std::clamp(_speed.X, -exitCap, exitCap);
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

			// The run-along carry ends with the same clamp every other one does - see
			// @ref LegacyCarryExitRunSpeed. This was the fifth site and the only one that never had it: the
			// spring hold, the tube, the accelerating belt and the sidekick all clamp, and a pole's carry
			// simply handed its speed back and let ordinary movement take over.
			//
			// It hides while a direction is held, which is why it went unnoticed - `ob_hpole` holds Right and
			// Run throughout, so the player is dashing when the carry ends and the ordinary cap converges on
			// the same 16 the clamp would have given. Let go instead and the two part company: measured on
			// `an_crouch_carry`, the original snaps from **17.70 to 3.88 px/tick** on the tick the carry ends
			// and stops 201 ticks in, where this engine kept the whole 17.8 and was still doing 12 at the end
			// of the scenario, 330 px further on.
			//
			// Reported from play as "a player being run along by a pole or spring should not be able to
			// crouch". The crouch is not refused - the original ducks at 17.70 px/tick there, pose and all -
			// but what it does *next* is stop almost at once, and that is what looks like a refusal.
			if (!_levelHandler->IsReforged() && _keepRunningTime <= 0.0f) {
				float exitCap = (_isRunPressed ? LegacyCarryExitRunSpeed : LegacyWalkSpeed);
				_speed.X = std::clamp(_speed.X, -exitCap, exitCap);
			}
		}

	}

	void Player::TriggerLoriKick()
	{
		_controllable = false;
		_controllableTimeout = 40.0f;
		SetAnimation(AnimState::Uppercut);
		// Her kick has **no wind-up**, which is what separates it from Spaz's. Measured on `sp_lori_side`:
		// the original's speed reads 1.0 on the very tick after the press and ramps quadratically from
		// there, where Spaz stands still for fourteen ticks first. Arming hers from the transition's
		// completion callback, the way his is, handed her his wind-up as well - sixteen ticks of nothing and
		// then a kick that covered the right distance, which is the reported "her wind-up is too long".
		if (_levelHandler->IsReforged()) {
			SetPlayerTransition(AnimState::TransitionUppercutA, true, false, SpecialMoveType::Sidekick, [this]() {
				BeginLoriKick();
			});
		} else {
			// One animation for the whole kick, not three. The original plays her `sidekick.aura` straight
			// through - nine frames at about four ticks each, id 229 for all 35 ticks of the cycle, with the
			// drive in the first four frames and the rest recovery. This engine sliced the same file into
			// `SidekickA` (frames 0-1), `Sidekick` (2+) and `SidekickC` (9+) and played them in sequence,
			// which put the kick pose off the screen after 20 ticks and left her standing in the **crouch**
			// pose for the other 15 of every cycle. `SidekickFull` is the same file with the frame count and
			// rate the original's timing asks for; the three sliced states are untouched, so Reforged - whose
			// own pacing has never been measured - keeps exactly what it had.
			SetPlayerTransition(AnimState::TransitionSidekick, true, false, SpecialMoveType::Sidekick);
			BeginLoriKick();
			// Start to start, so the pause after the kick takes care of itself whatever the kick's own
			// length turns out to be - see LegacyLoriKickPeriod
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
		// Held, not hit: the whole point is that the original repeats without a second press.
		//
		// Grounded is required, and the reason is worth stating because the trace looks at first like it says
		// the opposite. `sp_lori_side_hold` runs off the end of the flat and the original keeps kicking while
		// its `y` climbs 1330 -> 1462 -> 1667 -> 1842, which reads as kicking through the air. It is not: that
		// stretch is the long **slope** at tiles 193-210 and the original is on it the whole way, its
		// animation never once showing a fall. Once the kick stops flying off the floor (see BeginLoriKick())
		// this engine follows the same slope, so the airborne case this guard refuses is a real gap - which
		// is exactly where the original refuses it too, and where the move cannot be triggered in the first
		// place either, since the crouch it needs cannot be entered off the ground.
		// Letting either key go ends the run of kicks, and that is the only thing that disarms the timer.
		if (_playerType != PlayerType::Lori || _levelHandler->IsReforged() ||
			!_levelHandler->PlayerActionPressed(this, PlayerAction::Down) ||
			!_levelHandler->PlayerActionPressed(this, PlayerAction::Jump)) {
			_loriKickRepeatLeft = 0.0f;
			return;
		}
		// Anything else that refuses a kick right now is a *wait*, not a cancel: the timer is left due so
		// the next frame tries again. The kick's own animation is the common case - it can still be a tick or
		// two from finishing when the period elapses, and dropping the repeat there cost every kick after the
		// first, which is what removing the duplicate ending animation exposed.
		// A kick still finishing is **not** a reason to wait: the period is start to start and the animation
		// is the period, so the next one begins as the last wraps. Waiting for the move to end instead put
		// each kick two or three ticks late and the error accumulated - 41, 79, 116, 153 against 41, 76, 111,
		// 146. Any *other* special move does block it.
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
		_externalForce.X = 4.0f * (IsFacingLeft() ? -1.0f : 1.0f);
		_speed.X = (_levelHandler->IsReforged() ? 9.3f : LegacyLoriSidekickSpeed) * (IsFacingLeft() ? -1.0f : 1.0f);
		// The kick's length is counted down where the move ends, so it has to be armed wherever the kick
		// actually begins - which for her is the trigger and for Reforged is the end of the wind-up. Armed
		// at the wrong one of those, a four-tick kick expires during the wind-up and dies on the tick it
		// starts, which is what a ~5 px kick once looked like.
		if (!_levelHandler->IsReforged()) {
			_sidekickDistanceLeft = LegacyLoriSidekickDistance;
			_sidekickTime = 0.0f;
		}
		// Outside Reforged her kick stays **grounded**, which is what makes it follow the floor it is
		// crossing instead of flying off it. Reported as "it should stick to down slopes, but not if there
		// is really a gap", and the trace agrees: `sp_lori_side_hold` kicks across the long slope at tiles
		// 193-210 and the original's animation reads 229 - the kick - for the whole descent, never once
		// falling, its `y` following the incline smoothly from 1330 to 1842. Suspending gravity made ours
		// hover for the kick's own length and then drop between kicks, descending in steps and running 133
		// px behind the original by tick 340 before catching up at the bottom.
		//
		// Both flags matter and they have to agree: TryMoveSubstep() only takes the ground-bound path, the
		// one that carries an actor along a slope, when `CanJump` **and** `ApplyGravitation` are both set.
		// Clearing either put her on the airborne path. Over a real gap nothing has to be special-cased -
		// TryStandardMovement() clears `CanJump` by itself the moment there is space below, so she falls.
		if (_levelHandler->IsReforged()) {
			// As with Spaz, the dash would otherwise leave the player counting as grounded when it ends in
			// mid-air (Lori has copter ears rather than a double jump, so for her that simply means no jump
			// until she lands)
			SetState(ActorState::CanJump, false);
			SetState(ActorState::ApplyGravitation, false);
		}
	}

	void Player::HandleWaterAndModifierMovement(float timeMult)
	{
		// The flying carrot's vertical, all of it measured from hand-played recordings - see
		// LegacyFlyRiseAccel. Up climbs; Down holds a fixed slow descent; otherwise a climb brakes to zero
		// and only then falls, at the much gentler LegacyFlyFallAccel. There is no hovering and no fixed
		// rise speed, which is what this engine had.
		if (!_inWater && _activeModifier == Modifier::Copter && !_levelHandler->IsReforged()) {
			float verticalMovement = _levelHandler->PlayerVerticalMovement(this);
			if (verticalMovement < -0.3f) {
				_speed.Y = std::max(_speed.Y - LegacyFlyRiseAccel * timeMult, -LegacyVerticalSpeedLimit);
			} else if (verticalMovement > 0.3f) {
				// Assigned rather than accelerated towards: the recording snaps from a 3.3125 px/tick fall to
				// this on the tick Down goes down and holds it for 734 ticks without varying
				_speed.Y = LegacyFlyDescentSpeed;
			} else if (_speed.Y < 0.0f) {
				// A climb has the same held/released pair an ordinary jump does, with its own two rates.
				// Clamped at zero rather than allowed to overshoot into the fall, which is what the original
				// does on the crossing tick.
				float brake = (_levelHandler->PlayerActionPressed(this, PlayerAction::Jump)
					? LegacyFlyRiseBrakeHeld : LegacyFlyRiseBrake);
				_speed.Y = std::min(_speed.Y + brake * timeMult, 0.0f);
			} else {
				_speed.Y = std::min(_speed.Y + LegacyFlyFallAccel * timeMult, LegacyFlyTerminalSpeed);
			}
			return;
		}

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

		// The original drops the crouch the instant the player leaves the ground: measured on `an_crouch_gap`,
		// where the player ducks at the lip of a six-tile hole and goes in, its pose is the crouch until the
		// tick the fall starts and the falling pose from that tick on. Nothing here ever cleared the bit - it
		// is cleared when Down is *released*, and Down is still held, so the branch below simply flips to the
		// buttstomp binding (which Down is also bound to by default), finds none of its conditions true and
		// leaves the crouch standing. The player fell the whole 1470 px ducking.
		//
		// That pose is not only cosmetic: @ref IsSpecialMoveCrouchReady() reads the same bit, so a crouch that
		// survives the fall lets an **uppercut** be started in mid-air. Measured on `an_crouch_gap_j`, where
		// Jump is pressed 40 ticks into the drop: the original ignores it completely and goes on falling at 10
		// px/tick, while this engine started the move on the very next tick and climbed 110 px back out of the
		// hole and onto the ledge it had fallen from. Both halves are fixed here, by the bit going away.
		if (!_levelHandler->IsReforged() && !CanJump() && _currentSpecialMove == SpecialMoveType::None &&
			(_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
			SetAnimation(_currentAnimation->State & ~AnimState::Crouch);
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
					// The original does not release into free fall, it *assigns* a speed and lets the ordinary
					// fall gravity take over - see LegacyVineDropSpeed. Released from a standing start this
					// took 48 ticks to reach the floor against the original's 16.
					if (!_levelHandler->IsReforged()) {
						_speed.Y = LegacyVineDropSpeed;
					}
				}
			} else if (_dizzyTime <= 0.0f) {
				// Check also previous CanJump to avoid animation glitches on Springs
				if (canJumpPrev && CanJump()) {
					// Crouching out of a wind-up would show the crouch pose over it and leave the charge running
					// underneath, so Down abandons the wind-up the way Jump does
					CancelRevUp();
					// The original crouches the moment no direction is held on the floor, whatever the player is
					// still carrying - so a run ends in a slide, and the crouched gunspot puts shots out low and
					// moving. Waiting for the speed to reach exactly zero, as Reforged does, means a crouch only
					// begins once the slide is already over, which is the whole difference. HandleHorizontalMovement()
					// runs first and clears the crouch bit whenever a direction *is* held, so both use the same
					// 0.4 deadzone and cannot disagree within a frame.
					// A spring's carry refuses it outright for its whole length - see @ref _springCarry, which
					// also records why a pole is *not* keyed on the same timer even though it refuses too.
					// Down is not *lost* while it is refused: the key is still held, so the crouch engages on
					// the tick the carry ends, which is where the original's does. Measured on the pair that
					// press Down 60 ticks apart in the same carry, `an_crouch_spring` and `an_crouch_spring_l`
					// - the original crouches at tick 126 and 125, so the press does not set the time, the
					// carry does.
					bool canCrouch = (_levelHandler->IsReforged()
						? std::abs(_speed.X) < std::numeric_limits<float>::epsilon()
						: (std::abs(_levelHandler->PlayerHorizontalMovement(this)) <= 0.4f &&
							!(_springCarry && _keepRunningTime > 0.0f)));
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
			// Jumping abandons a wind-up rather than launching out of it - the launch is what letting go of Run
			// is for, and a jump that kept the charge would carry the pose into the air
			CancelRevUp();

			if (!_wasJumpPressed) {
				_wasJumpPressed = true;

				// The copter's one attempt per airtime is spent **here**, on the press itself, and not down in
				// HandleSpecialJump() where it is used - because `_jumpTime` guards that call for ten frames
				// after a jump and the original counts a press made inside that window all the same.
				// `cp_tap55` is the case: its first tap lands at tick 55, ten ticks after the jump, so this
				// engine never saw it at all and happily coptered off the next one at 63 while the original,
				// having spent the attempt on that unseen press, refuses the whole airtime.
				_copterChanceThisPress = (!_levelHandler->IsReforged() && !CanJump() && !_copterChanceUsed);
				if (_copterChanceThisPress) {
					_copterChanceUsed = true;
				}

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

						// Jumping out from under another player (not a solid object). Reforged gets out of the way -
						// bump sideways, so we arc away instead of falling straight back into the lift state. The
						// original does the opposite and *boosts* the player on top, which is how two players reach
						// somewhere neither can alone, so there the rise is handed upwards and this player stays put.
						//
						// NOT MEASURED: the probe drives a single player, so no scenario can reach this on the
						// original's side. The launch handed over is this player's own standing jump speed, on the
						// grounds that a rising body carries what is resting on it - a placeholder with a reason,
						// not a figure read off a trace. See `Docs/MovementAccuracyReference.dox`.
						for (auto* other : _levelHandler->GetPlayers()) {
							if (other != this && other->_stackCarrying && other->_carryingObject == this) {
								if (_levelHandler->IsReforged()) {
									_speed.X = (_pos.X <= other->GetPos().X ? -1.0f : 1.0f) * PlayerBumpMinSeparationSpeed;
								} else {
									other->CancelCarryingObject();
									other->SetState(ActorState::CanJump, false);
									other->_speed.Y = -LegacyJumpSpeed;
									// Which rise gravity the boost decays under has to be stated, not inherited: it is
									// what a launch's height actually depends on, and a stale flag once cost a blue
									// spring a third of its rise. Held rate, so the boost is worth a full jump - a
									// boost that only lifts half of one cannot do the job the mechanic exists for.
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
			}

			if (_suspendType != SuspendType::None) {
				// Drop off hook/vine
				if (IsContinuousJumpAllowed() || _levelHandler->PlayerActionHit(this, PlayerAction::Jump)) {
					if (_suspendType == SuspendType::SwingingVine) {
						CancelCarryingObject();
						_springCooldown = 30.0f;
					} else {
						// No upward nudge on the way off. It was there to get the player clear of the vine so
						// they would not re-grab it immediately, which the detach and cooldown below now do
						// properly - and it put the player 4 px above where the original leaves them. Measured:
						// the original hangs at 1458 and is at 1438 two ticks later, which is a plain -10 jump
						// with nothing added, while ours started its jump from 1452 after settling at 1456.
						//
						// Letting go has to actually let go. Without this the player stays attached, and since
						// continuous jump is on by default this whole branch re-fires on **every frame** the key
						// is held - nudging them 4 px up each time while `_suspendType` still reads Vine. That is
						// the reported "player gets teleported above the vine, catches it from above and carries
						// on jumping up": measured on `ob_vine_low_hold`, y goes 1452 -> 1448 -> 1444 with the
						// suspend state reading 1 the whole way, against 1456 for a tapped jump that settles
						// properly. The cooldown matters as much as the detach, because the grab box reaches
						// above the player and would otherwise catch the same vine again on the next frame.
						// `HandleLookupAndCrouch()`'s Down-side drop already does exactly this.
						_suspendType = SuspendType::None;
						// Short, and measurably so. The original re-grabs a *different* vine three tiles up only
						// **12 ticks** after letting go of the first - measured hanging at y=1458 and then at
						// 1362, one tick each, with the applied rise cap covering the 96 px between them. A 12
						// *frame* cooldown is 14 ticks and swallows that grab entirely, which is why the second
						// vine was being missed. This only has to outlast the few frames it takes to rise clear
						// of the vine just released, which at ~11.7 px a frame is two or three.
						_suspendTime = VineDropCooldown;
						SetState(ActorState::ApplyGravitation, true);
						// Letting go of a vine has to allow the jump that follows in this same call to fire, or
						// the player simply drops off it - the cooldown left over from whatever jump carried them
						// onto the vine is still running, and the standard-jump branch below is gated on it.
						_jumpTime = 0.0f;
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
					BeginStandardJump(timeMult);
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

	void Player::BeginStandardJump(float timeMult)
	{
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
			_riseFromFloatUp = false;
		}
	}

	void Player::TryLegacyOneWayRejump(float timeMult)
	{
		// Called from the *end* of the update, after the move, which is the whole point of it not living in
		// HandleJump() with every other jump. The original evaluates "is there ground under the feet" as part
		// of the move and launches from the position that move ended on; HandleJump() runs before the move and
		// would answer from the previous frame's position, which on `ow_jump` fired each chained launch a
		// frame early and so about 8 px lower - enough that the third one arrived after the key was released
		// and the arc topped out 50 px short of the original's.
		if (!IsOnLegacyOneWayFloor() || _currentSpecialMove != SpecialMoveType::None || !_controllable ||
			_levelHandler->PlayerActionPressed(this, PlayerAction::Down) ||
			!_levelHandler->PlayerActionPressed(this, PlayerAction::Jump)) {
			return;
		}
		// No `_jumpTime` gate. The original's gaps between successive launches on `ow_jump` are 7, 9 and 12
		// ticks, and the cooldown is 10 frames - 11.7 ticks - so honouring it would swallow the first two.
		// A cooldown is not what limits this in the original; the spacing of the platforms is.
		BeginStandardJump(timeMult);
	}

	void Player::HandleSpecialJump(float timeMult)
	{
		// There was a `_fireFramesLeft` gate here, on the reading that the original refuses a special move
		// while the shooting pose is up. **It was wrong, and the control that seemed to prove it was not a
		// control.** `an_shoot_side` and `an_shoot_upper` press Down and Jump on the *same tick*, and
		// `sp_spaz_side_rel` - the scenario used to rule the release out - holds Down from twenty ticks
		// earlier and only releases it later. `sp_side_tap` and `sp_upper_tap` press the two together with
		// no shot at all and give 0 px of travel and 8 px of hop: exactly what the shooting pair gives. The
		// shot changes nothing. What the original wants is the crouch **established before** Jump, and this
		// engine fires the move either way - see the gaps table.

		// A special move also wants the player *stopped*, which the crouch does not: the crouch engages the
		// moment no direction is held and carries whatever speed is left, and that is what makes a run end in
		// a slide. The move the crouch leads to does not follow while that slide is still running. Measured on
		// all three characters, Down held through a dash and Jump four ticks later: `sm` never leaves 0 and the
		// original jumps instead - 8 px up, launching at -13.08 with 12.33 px/tick still under it, which is
		// exactly @ref LegacyJumpSpeed plus the usual speed boost. The buttstomp then comes from Down still
		// being held once that jump has left the ground, and runs its ordinary course: the hold at 0.0625 for
		// about thirty ticks, the descent at 10, the landing, and the crouch again on top of it. This engine
		// fired the full move instead, 201 px of uppercut for Jazz and a 700 px sidekick for Spaz.
		//
		// So all this has to do is hand over to the standard jump and re-arm the Down edge; the existing
		// airborne path produces the stomp by itself. `_wasDownPressed` is what would otherwise swallow it,
		// having been set when the crouch engaged four ticks earlier and never cleared, because Down is still
		// held throughout.
		//
		// The boundary is @ref StopSettleSpeed rather than a literal zero. The original tests its own speed
		// against exactly 0, but ours decays on a 60 Hz curve and reaches 0 on a different frame, so a strict
		// comparison would decide this on frame timing across a band about a pixel per tick wide.
		//
		// The **second** reason a crouched Jump gives an ordinary jump is that the crouch has to be there
		// first: Down and Jump on the same tick never produce the move. Measured by holding both for 8, 16
		// and 24 ticks - the original rises 8.0 px in all three, exactly what a tapped jump gives, and 132.0
		// with Jump held, which is a plain standing jump - against the 201.5 px uppercut it does give once
		// Down is established twenty ticks early. Both refusals land here rather than in the three character
		// branches below, because a refused move still has to *jump*: gating the branches alone left the
		// player doing nothing at all and the three scenarios measured 0 px against the original's 8.
		if (!_levelHandler->IsReforged() && CanJump() &&
			(_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch &&
			(std::abs(_speed.X) >= StopSettleSpeed || !IsSpecialMoveCrouchReady())) {
			_wasDownPressed = false;
			BeginStandardJump(timeMult);
			return;
		}

		switch (_playerType) {
			case PlayerType::Jazz: {
				if (IsSpecialMoveCrouchReady()) {
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
					// Whether the player counts as falling is read at the **start of the frame** outside
					// Reforged, not live - the same rule the double jump needed and for the same reason. The
					// original applies its gravity after this test, so a press landing on the exact apex reads
					// 0.0000 there and is refused; the live value has already crossed zero and accepts it.
					// Measured on `sp_jazz_dj`, whose press lands one tick either side of the apex: the
					// original reads 0.0000, refuses, and falls the rest of the way at the ordinary 0.125
					// gravity, while ours engaged the copter and held 1.0 px/tick for the next eighty ticks -
					// which is the reported "copter descent winds up out of nowhere".
					float copterSpeedY = (_levelHandler->IsReforged() ? _speed.Y : _frameStartSpeedY);
					// There is an **upper** bound on starting it outside Reforged, which this engine had no
					// equivalent of: the original refuses the copter once the fall is past about 2 px/tick,
					// so it can only be started early. Engaging at any speed is what pinned every descent
					// here at 1.0 - see LegacyCopterEngageMaxSpeed. Already flying is exempt, or a tap that
					// arrives once the copter has been going a while would refuse to extend it.
					bool alreadyFlying = ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter);
					// Taken **before** the speed test and deliberately not short-circuited by it: the original spends
					// the attempt on whatever the first press of the airtime lands on, including one made while still
					// rising - which is what refuses `cp_tap55` and `cp_tap60` for their whole airtime even though
					// eleven later presses land squarely in the window.
					bool copterChanceOk = (_levelHandler->IsReforged() || alreadyFlying || CanJump() ||
						_copterChanceThisPress); // spent in HandleJump(), on the press rather than here
					bool copterSpeedAllowed = (copterSpeedY > 0.01f && (_levelHandler->IsReforged() || alreadyFlying ||
						(copterChanceOk && copterSpeedY < LegacyCopterEngageMaxSpeed)));
					if (copterSpeedAllowed && !CanJump() && (_currentAnimation->State & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
						SetState(ActorState::ApplyGravitation, false);
					// Engaging only ever *caps* the descent outside Reforged, it does not set it. A copter
					// started while falling faster than its ceiling drops straight to it - `cp_tap70` engages
					// at 1.250 and reads 1.0078 the next tick - but one started slower keeps what it has and
					// winds up from there at LegacyCopterWindUp. Assigning here made the two cases identical
					// and hid the whole ramp: `cp_tap65` snapped from 0.584 to a full descent in a tick where
					// the original is still at 0.63.
					_speed.Y = (_levelHandler->IsReforged()
						? 1.5f
						: std::min(copterSpeedY, LegacyCopterDescentSpeed));
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
				if (IsSpecialMoveCrouchReady()) {
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
							_riseFromFloatUp = false;
							// ...but a speed a *spring* gave the player is not simply lost. It rebuilds, and at
							// a rate the launch itself sets: measured off all three horizontal springs, the
							// rebuild runs at the launch speed over @ref LegacySpringRebuildTicks. Red launches
							// at 16 and rebuilds at 0.50 px/tick², green at 24 at 0.75 - and blue, at 32, was
							// *predicted* to rebuild at 1.00 before it was measured, and does. The ceiling is a
							// flat @ref LegacyDashSpeed whatever the launch was, so blue reaches it early and
							// stops there rather than climbing to 32.
							if (_springLaunchSpeedX > 0.0f) {
								_springRebuildAccel = _springLaunchSpeedX * LegacyFrameRateScale / LegacySpringRebuildTicks;
								// One rebuild per launch: arming **consumes** the marker. Left set, every later
								// double jump re-armed it, and on a scenario that taps Jump throughout the
								// player simply oscillated back to 16 for the rest of the run - 462 px past the
								// original, where the original ramps up once and then walks at 4. Clearing it
								// on landing instead does not work either: the landing speed *is* the walk cap,
								// so a test against that never fires.
								_springLaunchSpeedX = 0.0f;
							}
						}

						PlayPlayerSfx("DoubleJump"_s);

						SetTransition(AnimState::Spring, false);
					}
				}
				break;
			}
			case PlayerType::Lori: {
				// Unlike Spaz she kicks over and over while Down and Jump are held. The repeat is **timed**
				// rather than driven by pressing again - see LegacyLoriKickPeriod - which is why this used to
				// look right when the keys were worked and did nothing at all when they were simply held.
				if (IsSpecialMoveCrouchReady()) {
					TriggerLoriKick();
				} else {
					// Whether the player counts as falling is read at the **start of the frame** outside
					// Reforged, not live - the same rule the double jump needed and for the same reason. The
					// original applies its gravity after this test, so a press landing on the exact apex reads
					// 0.0000 there and is refused; the live value has already crossed zero and accepts it.
					// Measured on `sp_jazz_dj`, whose press lands one tick either side of the apex: the
					// original reads 0.0000, refuses, and falls the rest of the way at the ordinary 0.125
					// gravity, while ours engaged the copter and held 1.0 px/tick for the next eighty ticks -
					// which is the reported "copter descent winds up out of nowhere".
					float copterSpeedY = (_levelHandler->IsReforged() ? _speed.Y : _frameStartSpeedY);
					// There is an **upper** bound on starting it outside Reforged, which this engine had no
					// equivalent of: the original refuses the copter once the fall is past about 2 px/tick,
					// so it can only be started early. Engaging at any speed is what pinned every descent
					// here at 1.0 - see LegacyCopterEngageMaxSpeed. Already flying is exempt, or a tap that
					// arrives once the copter has been going a while would refuse to extend it.
					bool alreadyFlying = ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter);
					// Taken **before** the speed test and deliberately not short-circuited by it: the original spends
					// the attempt on whatever the first press of the airtime lands on, including one made while still
					// rising - which is what refuses `cp_tap55` and `cp_tap60` for their whole airtime even though
					// eleven later presses land squarely in the window.
					bool copterChanceOk = (_levelHandler->IsReforged() || alreadyFlying || CanJump() ||
						_copterChanceThisPress); // spent in HandleJump(), on the press rather than here
					bool copterSpeedAllowed = (copterSpeedY > 0.01f && (_levelHandler->IsReforged() || alreadyFlying ||
						(copterChanceOk && copterSpeedY < LegacyCopterEngageMaxSpeed)));
					if (copterSpeedAllowed && !CanJump() && (_currentAnimation->State & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
						SetState(ActorState::ApplyGravitation, false);
					// Engaging only ever *caps* the descent outside Reforged, it does not set it. A copter
					// started while falling faster than its ceiling drops straight to it - `cp_tap70` engages
					// at 1.250 and reads 1.0078 the next tick - but one started slower keeps what it has and
					// winds up from there at LegacyCopterWindUp. Assigning here made the two cases identical
					// and hid the whole ramp: `cp_tap65` snapped from 0.584 to a full descent in a tick where
					// the original is still at 0.63.
					_speed.Y = (_levelHandler->IsReforged()
						? 1.5f
						: std::min(copterSpeedY, LegacyCopterDescentSpeed));
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
						GrantInvulnerability(FrameTimer::FramesPerSecond, InvulnerableType::Transient);
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
						GrantInvulnerability(invulnerableTime, InvulnerableType::Blinking);
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
		// The rebuild is an airborne behaviour and ends on landing. The *launch* it was armed from does not:
		// a horizontal spring leaves the player travelling along the ground, so OnHitFloor() runs every frame
		// of the launch and clearing it here unconditionally wiped the marker long before the double jump
		// that needed it - the fix looked inert because it never saw a launch at all. It goes when the player
		// has actually slowed to walking pace, which is when the launch is spent rather than merely grounded.
		if (std::abs(_speed.X) < LegacyWalkSpeed) {
			_springLaunchSpeedX = 0.0f;
		}
		_springRebuildAccel = 0.0f;
		// `_springHoldLeft` is deliberately NOT cleared here, and this is the second time that trap has been
		// walked into: a horizontal spring launch leaves the player on the ground, so OnHitFloor() runs every
		// frame of it and anything cleared here never survives its own four ticks. It is a short self-expiring
		// timer; the reset paths clear it, and nothing else needs to.

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
					GrantInvulnerability(invulnerableTime, InvulnerableType::Blinking);
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
		// The copter's one attempt per airtime comes back with the ground - see LegacyCopterEngageMaxSpeed
		// and where _copterChanceThisPress is set in HandleJump()
		_copterChanceUsed = false;

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
		// Reset speed and show Push animation
		_speed.X = 0.0f;
		_pushFramesLeft = 12.0f;
		_pushContactThisFrame = true;
		_keepRunningTime = 0.0f;
		// A rev-up launch arms the dash grace for its whole 274-frame hold, so that it can end in the same
		// single-tick snap to the walk cap the original does. Running into a wall has to end that too, or the
		// player stands against the wall still counting as dashing for four seconds - which reads exactly like
		// the Run key being stuck down. Only the *extended* grace is cut: an ordinary dash's own 16 ticks is
		// left alone, since nothing here has measured what a wall does to it.
		if (_dashGraceLeft > LegacyDashGraceTicks) {
			_dashGraceLeft = 0.0f;
		}
		// And a wind-up or a pending light launch goes with it - running into a wall is not how either should end
		CancelRevUp();

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
				// Remembered because a double jump out of this launch does not simply lose the speed - it
				// rebuilds it, at a rate this launch sets. See @ref LegacySpringRebuildTicks. Only a spring
				// arms it: a dash's speed, when a double jump takes it, comes back at the ordinary air
				// acceleration instead, so keying this on "was carrying speed" would be wrong.
				_springLaunchSpeedX = std::abs(force.X);
				// The launch is only carried for a few ticks and then clamps to the same figure every other
				// carry ends at - see @ref LegacySpringHoldTicks. Only worth arming when the launch is
				// actually above that: a red spring is at the cap already and clamping it is a no-op.
				_springHoldLeft = (std::abs(force.X) > LegacyCarryExitRunSpeed
					? LegacySpringHoldTicks / LegacyFrameRateScale
					: 0.0f);
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
			// This carry refuses the crouch for its whole length, where a pole's does not - see @ref _springCarry
			_springCarry = true;
			_hPoleCarry = false;

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
				_riseFromFloatUp = false;
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

			// A vertical spring normally throws away whatever horizontal speed the player arrived with. A rev-up
			// launch is the exception: it is a driven run that ignores steering for its whole length, so hitting
			// a spring mid-launch has to send the player up *and* leave them travelling, rather than dropping
			// them straight back down the way they came. Recognised by the extended dash grace the launch arms,
			// the same marker OnHitWall() uses.
			bool revUpLaunch = (_dashGraceLeft > LegacyDashGraceTicks);
			if (!keepSpeedX && !revUpLaunch) {
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
		// Counted down here rather than in UpdateRevUp(), which is skipped in water and under a modifier - a
		// timer that holds a base animation must not be able to get stuck because the code that clears it
		// stopped being called
		if (_revUpEndLeft > 0.0f) {
			_revUpEndLeft -= timeMult;
		}

		// Spaz's double-jump pose lasts exactly as long as the rise in the original, not as long as its own
		// animation: measured on `an_dj_len` it runs 11 ticks and gives way to the falling pose on the tick the
		// vertical speed turns positive, where this engine played all eight frames of `spring` regardless and
		// held the pose for 34. The cadence itself already matches, so the animation is left alone and only cut
		// short. This runs before OnHandleMovement(), so the frame that issues the transition cannot reach it.
		if (!_levelHandler->IsReforged() && _speed.Y >= 0.0f && _currentTransition != nullptr &&
			_currentTransition->State == AnimState::Spring) {
			// The transition is deliberately non-cancellable, so nothing else can end it
			ForceCancelTransition();
		}

		if (!_controllable) {
			// Dropping the phase is not enough: the *transition* is what is on screen, and returning without
			// cancelling it leaves whatever the chain last issued playing for as long as control is gone.
			// Measured on `tb_gap5`, which coasts into a sucker tube: the skid starts while the player is
			// still steering, the tube takes control a few ticks later, and the pose then rode the whole
			// nine-tube crossing - `TransitionDashToIdle` over the entire ride, where the original holds a
			// single pose from the first tube to the last. ApplyStopAnimation(false) is the same cancel the
			// controllable path already does, and it only fires when a stop pose was actually showing, so
			// transitions that belong to something else - an uppercut's wind-up, a warp - are untouched.
			ApplyStopAnimation(false);
			return;
		}

		bool sliding = IsSlidingToHalt();
		// Whether a stopping pose is on screen, which is not the same question: the chain starts from any
		// speed, and its last stage outlives the player reaching zero by about 24 ticks
		bool stopPoseShowing = (sliding && (_stopPhase != StopPhaseNone || std::abs(_speed.X) > 0.0f));

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
		} else if (IsRevvingUp() || _revUpEndLeft > 0.0f) {
			// Decided here rather than assigned from UpdateRevUp(), which runs later in the frame: everything
			// below would overwrite it on the next frame, and re-assigning it from there each frame restarts
			// the animation every frame - which is why the first attempt showed a single frozen frame.
			//
			// It is also held for the length of the end animation, which is what stops the base state changing
			// to Dash underneath that transition and cancelling it.
			newState = AnimState::RevUp;
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

			if (stopPoseShowing) {
				// Sliding to a halt. The base state stays idle for the whole slide and the stop pose is drawn
				// over it as a transition, because SetAnimation() cancels a cancellable transition whenever the
				// base state changes - letting the speed bits decay Dash -> Walk -> Idle underneath it ended the
				// pose part-way through, which is why `an_slide_stop` once showed it for a single tick.
			} else if (_isActivelyPushing == _wasActivelyPushing || !_levelHandler->IsReforged()) {
				float absSpeedX = std::abs(_speed.X);
				// Threshold must track the actual walk cap, otherwise the higher non-Reforged walk speed would
				// keep triggering the Dash animation while merely walking.
				float dashAnimThreshold = (_levelHandler->IsReforged() ? MaxRunningSpeed : LegacyDashAnimSpeed);
				if (absSpeedX > dashAnimThreshold) {
					composite |= AnimState::Dash;
				} else if (!_levelHandler->IsReforged() && absSpeedX > LegacyWalkSpeed) {
					// Speeding up is picked by speed as well, in the same three bands the stop uses: `run` to the
					// walk cap, `dash_start` from there to half the dash cap, `dash` above it. See `an_run_start`.
					// This engine drove the middle one as a transition instead, which plays all eight of its
					// frames whatever the player is doing - 34 ticks, against the 11 the original spends in the
					// band - so the dash pose arrived 47 ticks after the key rather than 23.
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
			// The original's gap is measured from the previous flourish *finishing*, not from it starting:
			// on `bl_right` it waits 141 ticks after one ends whatever that one's length was. Letting the
			// timer run underneath an animation makes the gap depend on how long the animation happened to
			// be instead - it hits the threshold part way through, resets silently because a transition is
			// already playing, and whatever is left over decides the next one. Measured: 93 ticks after a
			// short flourish and 299 after a long one, against the original's flat 141.
			//
			// Non-Reforged only. Reforged's 600 frames is its own figure and its behaviour under a playing
			// transition is left exactly as it was.
			bool holdIdleTimer = (!_levelHandler->IsReforged() && _currentTransition != nullptr);
			// 600 frames is 700 of the original's ticks; it plays one every 140 - see LegacyIdleBoredTime
			if (holdIdleTimer) {
				// Nothing: the player is not standing idle while something is being drawn over them
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
					// Reforged only: outside it the spinning-feet animation is a speed band of its own rather
					// than a transition, and firing it here as well would play all eight frames over the top
					// of the band that is already showing them
					if (newState == AnimState::Dash && _levelHandler->IsReforged()) {
						SetTransition(AnimState::TransitionRunToDash, true);
					}
					break;
				// The stop is no longer driven from here. It used to be a chain fired on the *change* into
				// Idle - dash_stop then run_stop - which cannot reproduce what the original does, because the
				// original picks the pose from the current speed every frame and has three of them. See the
				// speed-tiered block after this switch.
				case AnimState::Dash:
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
		// A transition is what is drawn while one is playing, and its length belongs to it - the rev-up pose
		// learnt that the hard way, by having its duration stamped over from here every frame. Called after
		// ApplyStopAnimation() for the same reason: the stop chain installs a transition of its own and this
		// has to see the frame's final answer, not the one from before it ran.
		if (_levelHandler->IsReforged() || _currentTransition != nullptr || _renderer.FrameCount <= 0) {
			return;
		}

		// Grounded running only. The speed field is set while airborne as well, where it picks which jump or
		// fall sprite to show, and those are not the feet going round - the original plays them at their own
		// rate whatever the player's speed is.
		AnimState state = _currentAnimation->State;
		if ((state & HorizontalAnimMask) == AnimState::Idle || (state & VerticalAnimMask) != AnimState::Idle ||
			(state & AnimState::Freefall) == AnimState::Freefall) {
			return;
		}

		float speedTicks = std::abs(_speed.X) / LegacyFrameRateScale;
		float frameTicks = std::max(LegacyRunAnimMinFrameTicks, LegacyRunAnimFrameTicks - speedTicks);
		float duration = _renderer.FrameCount * frameTicks / LegacyTickRate;
		if (duration == _renderer.AnimDuration) {
			return;
		}

		// Rescaled rather than assigned. Which frame is on screen is `AnimTime / AnimDuration` of the way
		// through the loop, so changing the duration on its own teleports the animation - accelerating from
		// the walk cap to the dash cap shortens the loop fourfold and would jump the feet three frames
		// forward on the tick it happened. Scaling the time by the same factor keeps the phase and changes
		// only the rate, which is what "play this faster" means.
		if (_renderer.AnimDuration > 0.0f) {
			_renderer.AnimTime *= duration / _renderer.AnimDuration;
		}
		_renderer.AnimDuration = duration;
	}

	void Player::UpdateHookIdleAnimation(float timeMult, AnimState newState)
	{
		// Hanging still on a vine alternates `vine_idle` with `vine_idle_flavor`, 70 ticks each, for as long
		// as the player stays there - see LegacyHookIdleCycle. `newState` is exactly Hook only while hanging
		// *and* doing nothing else, so moving along the vine or shooting from it ends the cycle by itself.
		if (newState != AnimState::Hook) {
			_hookIdleTime = 0.0f;
			if (_inHookIdleFlavor) {
				_inHookIdleFlavor = false;
				CancelTransition();
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

	void Player::IssueHookIdleFlavor()
	{
		// Restarted from the finish callback because the flourish is shorter than its half of the cycle and
		// has to go round - the same reason the stopping chain does it, and the same one frame of the static
		// pose avoided by not waiting for the next frame to notice. Cancelling clears the flag first, so the
		// callback can tell its own end from being called off.
		SetTransition(AnimState::TransitionHookIdleFlavor, true, [this]() {
			if (_inHookIdleFlavor) {
				IssueHookIdleFlavor();
			}
		});
	}

	bool Player::IsSlidingToHalt()
	{
		// Whether the player is coasting to a stop rather than steering. The *speed* is deliberately not part
		// of this: the chain below starts from any speed and outlives reaching zero.
		//
		// A rev-up is excluded, and so is anything else *carrying* the player. Both read as "let go and
		// sliding" otherwise, because neither has a directional input of its own - and a carry is precisely
		// the case where the player is not coasting to a stop. The three rev-up timers cover its wind-up and
		// the wait before a light launch moves; `_keepRunningTime` covers the launch itself and every other
		// carry that sets it - a spring, a pole, a wall bounce.
		//
		// Reported from play: a rev-up launch showed the *skid* pose for its whole length instead of the run.
		// The base state was never wrong - UpdateAnimation() already forces @ref AnimState::Run while
		// `_keepRunningTime` is running - but the stop chain is drawn *over* the base animation, so the skid
		// covered it. Anything that forces the run pose has to suppress the stop chain as well, or the two
		// disagree with the transition winning.
		//
		// **A crouch is the third instance of that same shape**, and it was reported the same way: run, let
		// go, then press Down, and the crouch does not appear until the player has genuinely stopped. The
		// crouch state engages immediately and correctly - `sp_slide_*` measure it at 14.5 px/tick, three
		// times the walk cap - but those scenarios press Down on the *same tick* the direction is released,
		// so the chain never starts first and they cannot see this. Press Down a few ticks later and the skid
		// is already up, drawn over the crouch, and it only clears when the speed reaches zero and
		// `StopPhaseHold` cancels the transition. Ducking is a deliberate input rather than coasting to a
		// stop, so it belongs with the carries above.
		if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
			return false;
		}
		// **Shooting is the fourth**, and it was reported the same way again: let go while running, then fire,
		// and the skid plays over the shot instead of giving way to it. Measured on `an_slide_fire`, which is
		// `an_slide_stop` with a shot in it and nothing else held - the two run identically until the fire, and
		// on the tick it lands the original leaves the skid and shows the ordinary movement pose for whatever
		// band the speed is in: `dash_start` at 13.86 px/tick, then `run` at 4.00, then the standing pose. It
		// never returns to the skid. This engine set @ref AnimState::Shoot on the same tick and kept the skid
		// transition drawn over it for the next 40, looping its three frames - which is exactly the "sliding
		// animation repeats" in the report, and the shoot pose was never on screen at all.
		//
		// Keyed on `_fireFramesLeft` rather than on the key, because that is what the shoot pose itself is
		// keyed on: the two now agree by construction, and a shot fired at the very end of a slide suppresses
		// the chain for exactly as long as it is showing.
		if (!_levelHandler->IsReforged() && _fireFramesLeft > 0.0f) {
			return false;
		}
		return (CanJump() && _controllable && _currentSpecialMove == SpecialMoveType::None &&
			_suspendType == SuspendType::None && _activeModifier == Modifier::None && !_inWater &&
			_revUpLaunchLeft <= 0.0f && !IsRevvingUp() && _revUpEndLeft <= 0.0f && _keepRunningTime <= 0.0f &&
			std::abs(_levelHandler->PlayerHorizontalMovement(this)) <= 0.4f);
	}

	void Player::EnterStopPhase(std::int32_t phase)
	{
		// Which animation each phase shows is measured, and it is *not* the order the names suggest: the
		// original skids on `walk_stop`, slides on `dash_stop` and settles on `run_stop`. The names are JJ2's
		// own and describe nothing - they are simply the order the three sit in the animation set, which is
		// how they ended up attached to the wrong parts of the stop when this engine had only two of them.
		AnimState pose;
		switch (phase) {
			case StopPhaseSlide: pose = AnimState::TransitionDashToIdle; break;
			case StopPhaseSettle: pose = AnimState::TransitionRunToIdle; break;
			default: pose = AnimState::TransitionWalkToIdle; break;
		}

		// Issuing the next pose from the finish callback rather than from the next frame's
		// ApplyStopAnimation() is what stops one frame of the standing pose showing through in between.
		// SetTransition() runs the outgoing transition's callback before installing the new one, though, and
		// that must not be taken for the incoming pose having already run out.
		_stopPhaseChanging = true;
		_inIdleTransition = true;
		SetTransition(pose, true, [this]() { OnStopPoseFinished(); });
		_stopPhaseChanging = false;
		_stopPhase = phase;

		if (phase == StopPhaseHold) {
			ParkStopPose();
		}
	}

	void Player::ParkStopPose()
	{
		// Holding the skid on its last frame, which is what the original does when the slide was too slow to
		// have a second pose at all: `tb_right` holds it 3 ticks past its own length and `sl_rf_up` 7, both
		// ending on the tick the player actually stops. Parking the time short of the end rather than letting
		// it run out means the animation never finishes, so nothing has to re-issue it every frame.
		if (_renderer.FrameCount > 0 && _renderer.AnimDuration > 0.0f) {
			_renderer.AnimTime = _renderer.AnimDuration * (_renderer.FrameCount - 0.5f) / _renderer.FrameCount;
		}
	}

	void Player::OnStopPoseFinished()
	{
		// Also reached when the pose is replaced or cancelled, including by something that has nothing to do
		// with stopping - a jump changes the base state, and that cancels whatever is playing over it
		if (_stopPhaseChanging || !_inIdleTransition || !IsSlidingToHalt()) {
			return;
		}

		switch (_stopPhase) {
			case StopPhaseSkid:
				// The one real speed test in the chain. Below it there is no second pose at all - `tb_right`
				// and `sl_rf_up` simply hold the skid until the player stops - which is why a light tap has a
				// much shorter stop than a dash does, without the first pose being any shorter.
				EnterStopPhase(std::abs(_speed.X) >= StopSettleSpeed ? StopPhaseSlide : StopPhaseHold);
				break;
			case StopPhaseSlide:
			case StopPhaseHold:
				// `dash_stop` loops for as long as the speed keeps it, and a transition cannot loop by itself:
				// AnimationLoopMode::Loop still ends it after one cycle
				EnterStopPhase(_stopPhase);
				break;
			case StopPhaseSettle:
				// Standing follows it, and only it
				_stopPhase = StopPhaseNone;
				_inIdleTransition = false;
				break;
		}
	}

	void Player::ApplyStopAnimation(bool sliding)
	{
		if (!sliding) {
			if (_stopPhase != StopPhaseNone) {
				// Moving again, or no longer in a state that can slide at all
				_stopPhase = StopPhaseNone;
				_inIdleTransition = false;
				CancelTransition();
			}
			return;
		}

		float absSpeedX = std::abs(_speed.X);
		switch (_stopPhase) {
			case StopPhaseNone:
				if (absSpeedX > 0.0f) {
					EnterStopPhase(StopPhaseSkid);
				}
				break;
			case StopPhaseSlide:
				if (absSpeedX < StopSettleSpeed) {
					EnterStopPhase(StopPhaseSettle);
				}
				break;
			case StopPhaseHold:
				if (absSpeedX <= 0.0f) {
					_stopPhase = StopPhaseNone;
					_inIdleTransition = false;
					CancelTransition();
				} else {
					ParkStopPose();
				}
				break;
		}
	}

	void Player::PushSolidObjects(float timeMult)
	{
		// Ground truth for "pushing this step" - re-established below (and in OnHitWall during the move that follows)
		_pushContactThisFrame = false;
		// Narrower than the above on purpose: this one means a *kick* met a pushable, and only the push path
		// below sets it. `_pushContactThisFrame` is also raised by OnHitWall, and a kick into a plain wall is
		// not the case that has to be billed for the distance it meant to cover.
		_kickPushedThisFrame = false;

		if (_pushFramesLeft > 0.0f) {
			_pushFramesLeft -= timeMult;
		} else {
			_canPushFurther = false;
		}

		// A sidekick pushes a pushable too, and neither gate below can ever see one: `_controllable` is false
		// for the whole of a special move, and `_isActivelyPushing` wants a direction key that a kick does not
		// use. So we moved the rock not at all where the original creeps it along - 96 px over a run of Lori's
		// repeated kicks and 19 px for a single one of Spaz's, measured against the level's own rock. The rate
		// is the ordinary @ref LegacyPushSpeed for both of them: a kick shoves a rock no faster than a walk
		// does, and the player travels with it at that same crawl rather than at the speed of the kick.
		// Only while the kick still has budget left, which is the same condition that drives it: what remains
		// of a move after that is a recovery the player stands still through, and it does not shove anything.
		// That is the whole difference between the two characters here - Lori spends her budget in twelve ticks
		// and then has twenty-odd of recovery to wait out, so each of her kicks nudges the rock 4.5 px and
		// stops, while Spaz's runs out exactly as his move ends and so pushes the entire time. Without this,
		// hers pushed straight through the recovery and shoved the rock 348 px where the original moves it 96.
		bool kickActive = (!_levelHandler->IsReforged() && _currentSpecialMove == SpecialMoveType::Sidekick);
		bool kickPushing = (kickActive && _sidekickDistanceLeft > 0.0f);
		// And when the drive ends the object has to be told to stop with it - see SolidObjectBase::StopPushing()
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
						// Which way to shove it comes from the facing rather than the sign of the speed: the
						// kick path arrives here with whatever the ramp last assigned, and anything that stops
						// the player mid-kick leaves that at zero, which reads as positive
						float pushSpeedX = solidObject->Push(kickPushing ? IsFacingLeft() : (_speed.X < 0), timeMult);
						OnPushSolidObject(timeMult, pushSpeedX);
						_kickPushedThisFrame = kickPushing;
					}
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
			// A kick held up by a pushable spends its budget on what it *meant* to travel, not on the 0.375
			// px/tick it actually manages - bill it for the crawl and the budget outlasts any plausible push,
			// so a kick that meets a rock simply never ends. It is one rule for both characters, and both
			// halves of it were measured against the same rock: Spaz's 440 px at his capped 8 px/tick is 55
			// ticks, and blocked he drove for 4 of them and then pushed for exactly 51; Lori's spends her
			// 204.75 over the twelve ticks she drives whether that lands on a rock or on thin air, which is
			// why her kick costs the same twelve either way and pushes 4.5 px each time. Hers is billed at a
			// flat rate rather than at her own ramp because the ramp summed frame-wise does not reach her
			// budget in the same elapsed time - see @ref LegacyLoriKickPushDrain.
			float travelled;
			if (_kickPushedThisFrame) {
				travelled = (_playerType == PlayerType::Lori
					? LegacyLoriKickPushDrain
					: LegacyAppliedSpeedCap) * timeMult;
			} else {
				travelled = std::abs(_pos.X - _frameStartPos.X);
			}
			_sidekickDistanceLeft -= travelled;
			if (_sidekickDistanceLeft <= 0.0f) {
				_sidekickDistanceLeft = 0.0f;
				_externalForce.X = 0.0f;
				sidekickSpent = true;
				// Lori's kick stops dead - measured, her speed reads 0 on the very next tick. Spaz's does not:
				// his snaps back to the walk cap and the ordinary deceleration coasts him the rest of the way,
				// which is where the last ~65 px of his ~505 px come from.
				//
				// ...but only with Run *up*, which is how it was measured - every sidekick scenario in the
				// harness pressed none. With Run held the original does not snap at all: measured on
				// `sp_side_run`, the speed leaves the kick at 15.88 and decays at a flat 0.4273 px/tick to
				// zero over forty ticks, which is exactly the ordinary dash brake, and the kick covers 664 px
				// against the 456 the snap gave. With a direction held as well it simply carries on as a dash
				// at 16 for ever (`sp_side_run_dir`), where the snap cost the player a re-acceleration from
				// the walk cap. So the clamp belongs to letting go of Run, not to the kick ending.
				//
				// A kick that runs out with Jump **still held** does not simply hand control back - the
				// original starts a fresh jump on the very next tick. Measured on `sp_spaz_side`, where Down
				// and Jump are both held throughout: `sm` returns to 0 at tick 110 with 15.88 px/tick still
				// under him, and tick 111 launches at −13.939. That is @ref LegacyJumpSpeed plus a quarter of
				// the 15.88 - the ordinary speed boost - so it is a plain jump that happens to take the kick's
				// speed with it, worth 216 px of rise this engine gave none of.
				//
				// It fires **before** the clamp below, and that ordering is the whole of it: the boost reads
				// `_speed.X`, so after the clamp it would be computed from 4 px/tick and the rise would come
				// out at the standing jump's 132 instead. The clamp still runs, and still snaps to 4 - the
				// original's own trace shows 15.88 on the tick the kick ends and 4.00 on the tick it launches,
				// so the jump takes the speed only for the purpose of its boost, not into the air with it.
				//
				// Grounded is asked of the tileset, not of `CanJump()`. The dash clears `ActorState::CanJump`
				// when it arms itself, precisely because it also turns gravity off and nothing would clear the
				// flag again - so by the time the budget runs out the flag reads false whether the kick ended
				// over floor or over a gap, and using it here refused every jump.
				//
				// Lori is excluded, and not because she is refused. Her kick **repeats** off the same held
				// keys (see *Lori's kick does not repeat*), so the tick a jump would use is spent starting the
				// next kick, and the original gives her exactly 0 px of rise in the same setup. The rule is
				// "whatever the move ends into", and for her that is another kick - which is why this is
				// keyed on the character whose kick repeats rather than on anything about jumping.
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
					// The same one clamp every carry ends with - see @ref LegacyCarryExitRunSpeed. Written this
					// way rather than as "skip it while Run is held" because the kick leaves at 15.88 px/tick,
					// so the two are indistinguishable here; the accelerating belt is what told them apart, and
					// stating the shared rule at all three sites keeps them from drifting.
					float exitCap = (_isRunPressed ? LegacyCarryExitRunSpeed : LegacyWalkSpeed);
					_speed.X = std::clamp(_speed.X, -exitCap, exitCap);
				}
			}
		}

		if (_currentSpecialMove == SpecialMoveType::Sidekick && _currentTransition == nullptr && (sidekickSpent || _controllable || std::abs(_speed.X) < 0.01f)) {
			EndDamagingMove();
			_controllable = true;
			_controllableTimeout = 0.0f;
			// Lori's kick is one animation outside Reforged and it has already run its own tail, so a second
			// one on the end is two ticks of a pose the original never shows - it goes straight from the last
			// frame of a kick to the first frame of the next. See AnimState::TransitionSidekick. Spaz keeps
			// his: his kick and its ending are separate animations in both games.
			bool loriOwnEnding = (_playerType == PlayerType::Lori && !_levelHandler->IsReforged());
			if (_suspendType == SuspendType::None && !loriOwnEnding) {
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
				// Gravity is re-asserted off every frame the copter is up, not just where it engages. The
				// engage path turns it off, but a warp turns it back on in DoWarpOut() - and the copter now
				// survives a warp, so without this the ramp below would be carrying a gravity step as well
				// and would climb at 0.133 a tick instead of the measured 1/128.
				if (!_levelHandler->IsReforged()) {
					SetState(ActorState::ApplyGravitation, false);
				}
				// The descent is the copter's **own** acceleration towards its ceiling, not the level's
				// gravity clamped to it - see LegacyCopterWindUp. At the rates an ordinary fall reaches
				// before a copter is usually engaged the two are indistinguishable, because both arrive at
				// the ceiling within a tick or two; it is engaging from *nothing* that separates them, and
				// then the original takes over a hundred ticks to get there while level gravity takes eight.
				_speed.Y = (_levelHandler->IsReforged()
					? std::min(_speed.Y + _levelHandler->GetGravity() * timeMult, 1.5f)
					: std::min(_speed.Y + LegacyCopterWindUp * timeMult, LegacyCopterDescentSpeed));
			} else {
				cancelCopter = ((_currentAnimation->State & AnimState::Fall) == AnimState::Fall && _copterFramesLeft > 0.0f);
			}

			if (cancelCopter) {
				_copterFramesLeft = 0.0f;
				SetAnimation(_currentAnimation->State & ~AnimState::Copter);
				if (!_isAttachedToPole) {
					SetState(ActorState::ApplyGravitation, true);
				}
				// A flight *ending* deliberately does **not** hand the airtime's attempt back. It looked as
				// though it should: `sp_jazz_copter_fwd` shows the original coptering twice, at 76 and 159,
				// and 83 ticks is almost exactly the 70 frames a flight is armed for. Reading the positions
				// rather than the engagement ticks says otherwise - by 149 this engine has *landed*, at
				// y=1328.7, while the original is still airborne at 1277 having jumped again, so the second
				// flight belongs to a trajectory that had already diverged rather than to a rule. Both games
				// engage once, within a tick of each other, before they part company. Landing restores it
				// (see OnHitFloor()), which is what "one attempt per airtime" means and all that is measured.
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

				// The grab box reaches above the player while rising and across the whole distance travelled this
				// frame, so a fast approach can resolve a grab from a position that is no longer inside the
				// suspend region - past the top of the vine. The settle loop below only corrects a player who
				// *is* inside it, so from outside it runs zero times and the lone step back up leaves them high.
				// That is why the hang height moved with the approach speed: measured on the low vine, a tapped
				// jump settled at 1456 and a held one at 1452, where the original hangs at 1458 both times.
				constexpr float MaxSettleSearch = 32.0f;
				float settleSearched = 0.0f;
				while (settleSearched < MaxSettleSearch && tiles->GetTileSuspendState(_pos.X, _pos.Y - 1) == SuspendType::None) {
					MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative | MoveType::Force);
					settleSearched += 1.0f;
				}
				if (tiles->GetTileSuspendState(_pos.X, _pos.Y - 1) == SuspendType::None) {
					// Nothing within reach after all - put the player back rather than leaving them hanging in
					// open air, and let the jump carry on
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

			// Adjust walking animation speed. Reforged's own rule, and now Reforged's alone: outside it
			// UpdateLegacyRunAnimSpeed() sets the rate for all three ground-run animations from the measured
			// one, and this would stamp over its answer for the walk every frame - it runs later in the frame
			// than UpdateAnimation() does. It did exactly that, and the result was not simply "the fix had no
			// effect": the duration reverted here while `AnimTime` kept the value scaled to the other one, so
			// the phase compounded by 46% a frame and the walk ran nine times too fast.
			if (_levelHandler->IsReforged() && _currentAnimation->State == AnimState::Walk && _currentTransition == nullptr) {
				_renderer.AnimDuration = _currentAnimation->AnimDuration * (1.4f - 0.4f * std::min(std::abs(_speed.X), MaxRunningSpeed) / MaxRunningSpeed);
			}
		}
	}

	void Player::OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, std::int32_t& areaWaterBlock)
	{
		areaWeaponAllowed = true;
		areaWaterBlock = -1;
		// Re-sampled below, so leaving a float-up column drops it on the very next frame. It is cleared here
		// rather than where it is read because this runs *after* the move, so what it holds during a move is
		// the previous frame's sample - the same thing that is true of the speed it guards.
		_inFloatUpArea = false;

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
				// The float-up field is a *state* rather than a trigger, but it still has to be looked for
				// along the whole path travelled and not only where the player ended the frame. Measured
				// across four frame rates on `fu_jump_right_rel`, a ladder of separate float tiles: the
				// assignment below fires on 41, 34 and 17 of the sampled rows at 144, 60 and 30 FPS and on
				// **two** at 24, where a frame is 2.92 of the original's ticks and a 12 px/tick fall covers
				// 35 px - more than a tile - between one sample and the next. The climb collapsed from
				// ~495 px to 176.6 with it. The trigger events above are swept for exactly this reason; a
				// state that is *entered* by crossing a tile needs it just as much.
				//
				// One sample finding it is enough, because the effect is an assignment and not an
				// accumulation: the player gets the same speed whether the field was found once or five
				// times in a frame. A stationary player still samples exactly one point, their own, so
				// nothing changes for the case that always worked.
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
							// Which rate the ascent decays at once the player leaves depends on the source, and
							// this is the source - see GetGravityModifier()
							_riseFromFloatUp = true;
							// ...and while still inside, the rate is none at all: the original travels exactly
							// the speed assigned here, with no gravity on the tick. Also GetGravityModifier().
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
				// Not gated on the mode, and none of the four tube figures is any more. The tube is the
				// original's mechanic in both: its speed comes out of the *level*, in the original's units, so
				// it means the same thing whichever movement model is running, and everything measured about
				// how the ride ends turned out to be the mechanic rather than a non-Reforged flavour of it.
				// What Reforged used before was a speed conversion 14% slow, a fixed 10-tick window against
				// the original's 16 re-armed, a snap 7 px high, and no clamp on release at all.
				_speed.X = (float)(std::int8_t)p[0] * LegacyFrameRateScale;
				_speed.Y = (float)(std::int8_t)p[1] * LegacyFrameRateScale;

				// The tube snaps the player onto its own tile, which is the tile the event was found in - not
				// necessarily the one the player ended the frame in, if it entered the tube mid-step
				float tubeSnapY = TubeSnapY;
				Vector2f pos = Vector2f(x, y);
				if (_speed.X == 0.0f) {
					pos.X = (std::floor(pos.X / 32) * 32) + 16;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				} else if (_speed.Y == 0.0f) {
					pos.Y = (std::floor(pos.Y / 32) * 32) + tubeSnapY;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				} else if (_inTubeTime <= 0.0f) {
					pos.X = (std::floor(pos.X / 32) * 32) + 16;
					pos.Y = (std::floor(pos.Y / 32) * 32) + tubeSnapY;
					MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
					OnUpdateHitbox();
				}

				SetState(ActorState::CollideWithTileset, !becomeNoclip);
				_inTubeTime = (becomeNoclip ? 600.0f : TubeControlTime);
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
				// The flying carrot goes too, which is what this event is for and what ends the original's
				// flight - outside Reforged it has no duration of its own to run out. Only the airboard was
				// cancelled here, so a carrot survived the very event meant to take it away.
				if ((_activeModifier == Modifier::Airboard || _activeModifier == Modifier::Copter) && !IsFlyCheatActive()) {
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

		// Measured, the original's shot covers exactly the gap between the player's leading edge and the
		// wall: the flight is a flat 9 ticks plus one per 3 px, and the gaps that fit it - 0.6, 11.7, 20.6
		// and 23.7 px - are edge-to-wall, not centre-to-wall. Ours started at the player's *centre* and was
		// then teleported to the gunspot two frames in (see RFShot), which is further forward again, so at
		// any gap under about 24 px the shot was already at or past the wall before it had flown anywhere -
		// the flight came out flat at 12 ticks where the original grows 13, 16, 17. Starting it at the edge
		// is what makes the measured rule reproducible; RFShot drops the teleport to match.
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
			// A warp does **not** end the copter outside Reforged - measured on `wp_walk`, where the original
			// comes out of the warp still coptering (its own animation 30) and descending at 0.008, 0.016,
			// 0.023, 0.031: the copter's wind-up from the standing start the warp leaves behind. Clearing it
			// here dropped the player into an ordinary fall instead, reaching 2.6 px/tick by the time the
			// original had reached 0.1. It is also the case that makes the wind-up visible at all, since a
			// warp is one of the few things that zeroes vertical speed without landing.
			if (_levelHandler->IsReforged()) {
				_copterFramesLeft = 0.0f;
			}
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
			// Don't re-enable gravity if any modifier is active, or if a copter survived the warp and still
			// owns the descent - see the non-Reforged path below
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

		// Outside Reforged the warp-out animation stops being what control waits on. The original hands it
		// back two ticks after the landing - see @ref LegacyWarpOutControlTime - where waiting for the
		// animation costs 38, which is the reported delay after a warp and is most of a second of standing
		// still. The animation above still plays and its callback still runs; this just gets there first,
		// and everything it does is idempotent. Invulnerability and gravity come back with control rather
		// than after it, because the original is plainly subject to both the moment it starts moving.
		//
		// **After** the transition, not before: SetPlayerTransition() with `removeControl` zeroes
		// `_controllableTimeout` on its way past, so a timeout armed ahead of it is wiped without trace.
		//
		// A frozen exit is left alone: there control is *meant* to stay away, and Freeze() in the callback
		// is what takes it.
		if (!_levelHandler->IsReforged() && (flags & WarpFlags::Freeze) != WarpFlags::Freeze) {
			SetState(ActorState::IsInvulnerable, false);
			// Not if a modifier is running - the same exception the callback makes, and for the same reason -
			// and not if a copter survived the warp, which outside Reforged it now does. The copter owns the
			// descent while it lasts, and handing gravity back here let it accumulate through the warp-out
			// pose, when the Copter animation bit is not up and the clamp below cannot run: the ramp then
			// started from 0.42 px/tick where the original starts from a standstill.
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
		// all - which is what dropping onto a pole from above does. Together with LegacyHPoleLaunchBonus that
		// is what stops such a player re-grabbing the same pole for ever.
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
					// Measured: this one adds a fixed bonus to the entry speed and caps the result, which is
					// the vertical pole's rule with a smaller bonus and a ceiling on top. Five entry speeds
					// fit it exactly; see LegacyHPoleLaunchBonus, which also records the multiply this
					// replaced and the reading that ruled it out.
					//
					// The bonus is also what the zero case needs. Without it a player who drops onto a pole
					// is launched at nothing, never leaves the tile, and re-grabs it once `_lastPoleTime`
					// runs out - forever. The original does the same run around the test level's spring
					// chain without ever sticking.
					_speed.X = std::min(std::abs(lastSpeed) + LegacyHPoleLaunchBonus, LegacyHPoleMaxLaunch) * sign;
					_externalForce.X = 0.0f;
					// ...and unlike every other carry, this one is *travelled* in full rather than being held
					// to the applied cap - see OnUpdatePhysics(). Measured on the launch itself: the original
					// reports 20 px/tick and moves 20, where its horizontal spring reports 32 and moves 8.
					_hPoleCarry = true;
				}
				SetFacingLeft(!positive);

				_keepRunningTime = 60.0f;
				// A pole's carry does **not** refuse the crouch, where a spring's does - see @ref _springCarry
				_springCarry = false;

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

				// Ten seconds in Reforged; the original flies until a Fly Off area or a death, and the
				// recording holds it for 3871 ticks without expiring. See LegacyFlyDuration.
				SetCopterFlight(_flyCheatActive ? 1e6f
						: (_levelHandler->IsReforged() ? (10.0f * FrameTimer::FramesPerSecond) : LegacyFlyDuration),
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
		// Being hit ends a rev-up launch exactly as running into a wall does: the speed is zeroed just above,
		// and without this the no-friction window and the extended dash grace would carry on for their whole
		// four seconds, leaving the player counting as dashing while stood still being hurt. Cancels a wind-up
		// in progress too - `CancelRevUp()` also drops a pending light launch, which must not fire out of a hit.
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

			if (!_inWater && _activeModifier == Modifier::None) {
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

	Player::InvulnerableType Player::GetInvulnerableType() const
	{
		if (_shieldSpawnTime > ShieldDisabled) {
			return InvulnerableType::Shielded;
		}
		// `Transient` is the one that asked for no visual effect at all, which is how it is recorded
		return (_invulnerableBlinkTime >= 0.0f ? InvulnerableType::Blinking : InvulnerableType::Transient);
	}

	void Player::GrantInvulnerability(float timeLeft, InvulnerableType type)
	{
		if (timeLeft <= 0.0f) {
			// A grant of nothing is not a revocation - clearing is SetInvulnerability()'s job
			return;
		}

		if (_invulnerableTime > 0.0f) {
			// A grant never shortens an invulnerability that is still running, nor trades its effect for a
			// weaker one. Both used to happen, and the visible consequence was that `jjgod` - ten minutes of
			// shield - ended the moment the player touched anything granting less: an ordinary carrot is
			// 0.8 s, a full-energy carrot 5 s, the invincibility carrot 30 s, and bouncing off an enemy
			// grants 1 s with no effect at all. Each simply overwrote what was there.
			//
			// The effect and the time are held to that rule separately, because they come apart: a Transient
			// grant landing on a running Blinking one kept the longer time but still cancelled the blink,
			// which left the player invulnerable for seconds with nothing on screen saying so - strictly
			// worse than the overwrite it replaced, and the opposite of what this rule is for.
			if (GetInvulnerableStrength(GetInvulnerableType()) > GetInvulnerableStrength(type)) {
				type = GetInvulnerableType();
			}
			if (timeLeft < _invulnerableTime) {
				timeLeft = _invulnerableTime;
			}
		}

		// Through the virtual, so the resolved values - not the requested ones - are what a server
		// replicates to the peer that owns this player (see RemotePlayerOnServer::SetInvulnerability())
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
			_speed.X = LegacyRFBlastSpeed * sign;
			_rfBlastLeft = LegacyRFBlastHoldTicks;
			if (!GetState(ActorState::CanJump)) {
				_speed.Y = -LegacyRFBlastRiseSpeed;
				// The original's rise after a blast decays at the released rate even with jump still held,
				// which is 29 px of height - so the blast ends the player's claim on the ascent
				_internalForceY = 0.0f;
				_jumpReleased = true;
			} else {
				// Standing on the floor the original lifts the player too, by an amount that **grows with the
				// range** - see @ref LegacyRFBlastLiftSlope for the four points and why the shape is strange.
				// Fitted rather than derived, because the mechanism behind it is not in evidence: nothing here
				// measures the shot's own fall, and this function is given a distance and a side and no
				// geometry to build a radial model out of. A fit of what was measured is still the measured
				// behaviour, and it replaces applying nothing at all.
				_speed.Y = -(LegacyRFBlastLiftBase + LegacyRFBlastLiftSlope * std::sqrt(distanceSqr));
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

