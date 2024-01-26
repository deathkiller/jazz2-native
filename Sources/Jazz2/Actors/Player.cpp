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

using namespace Jazz2::Tiles;

namespace Jazz2::Actors
{
	Player::Player()
		:
		_playerIndex(0),
		_playerType(PlayerType::Jazz), _playerTypeOriginal(PlayerType::Jazz),
		_isActivelyPushing(false),
		_wasActivelyPushing(false),
		_controllable(true),
		_controllableExternal(true),
		_controllableTimeout(0.0f),
		_lastExitType(ExitType::None),
		_wasUpPressed(false), _wasDownPressed(false), _wasJumpPressed(false), _wasFirePressed(false),
		_currentSpecialMove(SpecialMoveType::None),
		_isAttachedToPole(false),
		_copterFramesLeft(0.0f), _fireFramesLeft(0.0f), _pushFramesLeft(0.0f), _waterCooldownLeft(0.0f),
		_levelExiting(LevelExitingState::None),
		_isFreefall(false), _inWater(false), _isLifting(false), _isSpring(false),
		_inShallowWater(-1),
		_activeModifier(Modifier::None),
		_externalForceCooldown(0.0f),
		_springCooldown(0.0f),
		_inIdleTransition(false), _inLedgeTransition(false),
		_carryingObject(nullptr),
		_canDoubleJump(true),
		_lives(0), _coins(0), _coinsCheckpoint(0), _foodEaten(0), _foodEatenCheckpoint(0), _score(0),
		_checkpointLight(1.0f),
		_sugarRushLeft(0.0f), _sugarRushStarsTime(0.0f),
		_shieldSpawnTime(ShieldDisabled),
		_gems(0), _gemsCheckpoint(0), _gemsPitch(0),
		_gemsTimer(0.0f),
		_bonusWarpTimer(0.0f),
		_suspendType(SuspendType::None),
		_suspendTime(0.0f),
		_invulnerableTime(0.0f),
		_invulnerableBlinkTime(0.0f),
		_jumpTime(0.0f),
		_idleTime(0.0f),
		_hitFloorTime(0.0f),
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
		if (_copterSound != nullptr) {
			_copterSound->stop();
			_copterSound = nullptr;
		}
		if (_weaponSound != nullptr) {
			_weaponSound->stop();
			_weaponSound = nullptr;
		}
	}

	Task<bool> Player::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_playerTypeOriginal = (PlayerType)details.Params[0];
		_playerType = _playerTypeOriginal;
		_playerIndex = details.Params[1];

		switch (_playerType) {
			case PlayerType::Jazz: async_await RequestMetadataAsync("Interactive/PlayerJazz"_s); break;
			case PlayerType::Spaz: async_await RequestMetadataAsync("Interactive/PlayerSpaz"_s); break;
			case PlayerType::Lori: async_await RequestMetadataAsync("Interactive/PlayerLori"_s); break;
			case PlayerType::Frog: async_await RequestMetadataAsync("Interactive/PlayerFrog"_s); break;
		}

		SetAnimation(AnimState::Fall);

		std::memset(_weaponAmmo, 0, sizeof(_weaponAmmo));
		std::memset(_weaponAmmoCheckpoint, 0, sizeof(_weaponAmmoCheckpoint));
		std::memset(_weaponUpgrades, 0, sizeof(_weaponUpgrades));
		std::memset(_weaponUpgradesCheckpoint, 0, sizeof(_weaponUpgradesCheckpoint));

		_weaponAmmo[(int)WeaponType::Blaster] = UINT16_MAX;
		_weaponAmmoCheckpoint[(int)WeaponType::Blaster] = UINT16_MAX;

		SetState(ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjects |
			ActorState::IsSolidObject | ActorState::ExcludeSimilar, true);

		_health = 5;
		_maxHealth = _health;
		_currentWeapon = WeaponType::Blaster;

		_checkpointPos = Vector2f((float)details.Pos.X, (float)details.Pos.Y);
		_checkpointLight = _levelHandler->GetAmbientLight();
		_trailLastPos = _checkpointPos;

		async_return true;
	}

	bool Player::CanBreakSolidObjects() const
	{
		return (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f);
	}

	bool Player::CanMoveVertically() const
	{
		return (_inWater || _activeModifier != Modifier::None);
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
		if (PreferencesCache::AllowCheats && _levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::ChangeWeapon)) {
			float moveDistance = (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Run) ? 400.0f : 100.0f);
			if (_levelHandler->PlayerActionHit(_playerIndex, PlayerActions::Left)) {
				MoveInstantly(Vector2f(-moveDistance, 0.0f), MoveType::Relative | MoveType::Force);
			}
			if (_levelHandler->PlayerActionHit(_playerIndex, PlayerActions::Right)) {
				MoveInstantly(Vector2f(moveDistance, 0.0f), MoveType::Relative | MoveType::Force);
			}
			if (_levelHandler->PlayerActionHit(_playerIndex, PlayerActions::Up)) {
				MoveInstantly(Vector2f(0.0f, -moveDistance), MoveType::Relative | MoveType::Force);
			}
			if (_levelHandler->PlayerActionHit(_playerIndex, PlayerActions::Down)) {
				MoveInstantly(Vector2f(0.0f, moveDistance), MoveType::Relative | MoveType::Force);
			}
		}
#endif

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
					_renderer.AnimPaused = true;
					_controllable = false;
					_controllableTimeout = 100.0f;
					_frozenTimeLeft = 100.0f;
				} else {
					_controllable = true;
					// UpdateAnimation() was probably skipped in this step, because _controllable was false, so call it here
					UpdateAnimation(0.0f);
				}
			});

			_renderer.setDrawEnabled(true);
			PlayPlayerSfx("WarpOut"_s);

			_lastExitType = ExitType::None;
		}

		// Process level bounds
		Vector2f lastPos = _pos;
		Recti levelBounds = _levelHandler->LevelBounds();
		if (lastPos.X < levelBounds.X) {
			lastPos.X = (float)levelBounds.X;
			_pos = lastPos;
		} else if (lastPos.X > levelBounds.X + levelBounds.W) {
			lastPos.X = (float)(levelBounds.X + levelBounds.W);
			_pos = lastPos;
		}

		PushSolidObjects(timeMult);

		//ActorBase::OnUpdate(timeMult);
		{
			TileCollisionParams params = { TileDestructType::Collapse, _speed.Y >= 0.0f };
			if (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f) {
				params.DestructType |= TileDestructType::Special;
			}
			if (std::abs(_speed.X) > std::numeric_limits<float>::epsilon() || std::abs(_speed.Y) > std::numeric_limits<float>::epsilon() || _sugarRushLeft > 0.0f) {
				params.DestructType |= TileDestructType::Speed;
				params.Speed = (_sugarRushLeft > 0.0f ? 64.0f : std::max(std::abs(_speed.X), std::abs(_speed.Y)));
			}

			if (timeMult * (std::abs(_speed.X + _externalForce.X) + std::abs(_speed.Y + _externalForce.Y)) > 20.0f) {
				TryStandardMovement(timeMult * 0.5f, params);
				TryStandardMovement(timeMult * 0.5f, params);
			} else {
				TryStandardMovement(timeMult, params);
			}

			if (params.TilesDestroyed > 0) {
				AddScore(params.TilesDestroyed * 50);
			}

			OnUpdateHitbox();

			//UpdateFrozenState(timeMult);
			if (_renderer.AnimPaused) {
				if (_frozenTimeLeft <= 0.0f) {
					_renderer.AnimPaused = false;
					_renderer.Initialize(ActorRendererType::Default);

					for (int i = 0; i < 10; i++) {
						Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 10), Explosion::Type::IceShrapnel);
					}
					
					Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 90), Explosion::Type::SmokeWhite);

					_levelHandler->PlayCommonSfx("IceBreak"_s, Vector3f(_pos.X, _pos.Y, 0.0f));
				} else {
					// Cannot be directly in `ActorBase::HandleFrozenStateChange()` due to bug in `BaseSprite::updateRenderCommand()`,
					// it would be called before `BaseSprite::updateRenderCommand()` but after `SceneNode::transform()`
					_renderer.Initialize(ActorRendererType::FrozenMask);
					_frozenTimeLeft -= timeMult;
				}
			}
		}

		UpdateAnimation(timeMult);
		CheckSuspendState(timeMult);
		CheckEndOfSpecialMoves(timeMult);

		OnHandleWater();

		bool areaWeaponAllowed = true;
		int areaWaterBlock = -1;
		OnHandleAreaEvents(timeMult, areaWeaponAllowed, areaWaterBlock);

		// Force collisions every frame even if player doesn't move
		SetState(ActorState::IsDirty, true);

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
						auto* res = _metadata->FindAnimation((AnimState)536870928); // Shield
						if (res != nullptr) {
							Vector2i texSize = res->Base->TextureDiffuse->size();
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
							debris.Flags = Tiles::TileMap::DebrisFlags::AdditiveBlending;

							tilemap->CreateDebris(debris);
						}
					}
				}
			} else if (_currentTransition == nullptr || _currentTransition->State != AnimState::Hurt) {
				if (_invulnerableBlinkTime > 0.0f) {
					_invulnerableBlinkTime -= timeMult;
				} else {
					_renderer.setDrawEnabled(!_renderer.isDrawEnabled());

					_invulnerableBlinkTime = 3.0f;
				}
			} else {
				_renderer.setDrawEnabled(true);
			}
		}

		// Timers
		if (_controllableTimeout > 0.0f) {
			_controllableTimeout -= timeMult;

			if (_controllableTimeout <= 0.0f) {
				_controllable = true;

				if (_isAttachedToPole) {
					// Something went wrong, detach and try to continue
					// To prevent stucking
					for (int i = -1; i > -6; i--) {
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
						auto* res = _metadata->FindAnimation((AnimState)536870913); // SugarRush
						if (res != nullptr) {
							Vector2i texSize = res->Base->TextureDiffuse->size();
							Vector2i size = res->Base->FrameDimensions;
							Vector2i frameConf = res->Base->FrameConfiguration;
							int frame = res->FrameOffset + Random().Next(0, res->FrameCount);
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
				_renderer.Initialize(ActorRendererType::Outline);
				_weaponWheelState = WeaponWheelState::Visible;
				break;
			case WeaponWheelState::Closing:
				_renderer.Initialize(ActorRendererType::Default);
				_weaponWheelState = WeaponWheelState::Hidden;
				break;
		}

		// Copter
		if (_activeModifier == Modifier::Copter || _activeModifier == Modifier::LizardCopter) {
			_copterFramesLeft -= timeMult;
			if (_copterFramesLeft <= 0.0f) {
				SetModifier(Modifier::None);
			} else if (_activeModifierDecor != nullptr) {
				_activeModifierDecor->MoveInstantly(_pos, MoveType::Absolute | MoveType::Force);
			}
		}

		if (_copterSound != nullptr) {
			if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
				_copterSound->setPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_copterSound->stop();
				_copterSound = nullptr;
			}
		}

		// Trail
		if (PreferencesCache::ShowPlayerTrails) {
			for (int i = 0; i < _trail.size(); i++) {
				auto& part = _trail[i];
				part.Intensity -= timeMult * 0.04f;
				part.Brightness -= timeMult * 0.04f;
				part.RadiusFar -= timeMult * 0.4f;
				if (part.RadiusFar <= 0.0f) {
					arrayRemoveUnordered(_trail, i);
					i--;
				}
			}

			if ((_keepRunningTime > 0.0f || _speed.SqrLength() > (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Run) ? 36.0f : 100.0f)) && _inTubeTime <= 0.0f) {
				constexpr float TrailDivision = 10.0f;
				Vector2f trailDelta = (_pos - _trailLastPos);
				int trailDistance = (int)(trailDelta.Length() / TrailDivision);
				if (trailDistance > 0) {
					trailDelta.Normalize();
					while (trailDistance-- > 0) {
						_trailLastPos += trailDelta * TrailDivision;

						auto& light = arrayAppend(_trail, InPlaceInit);
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

		// Tube
		if (_inTubeTime > 0.0f) {
			_inTubeTime -= timeMult;

			if (_inTubeTime <= 0.0f) {
				_controllable = true;
				SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset, true);
			} else {
				// Skip controls, player is not controllable in tube
				// Weapons are automatically disabled if player is not controllable
				if (_weaponSound != nullptr) {
					_weaponSound->stop();
					_weaponSound = nullptr;
				}

				return;
			}
		}

		// Controls
		// Move
		if (_keepRunningTime <= 0.0f) {
			bool canWalk = (_controllable && _controllableExternal && !_isLifting && _suspendType != SuspendType::SwingingVine &&
				(_playerType != PlayerType::Frog || !_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Fire)));

			float playerMovement = _levelHandler->PlayerHorizontalMovement(_playerIndex);
			float playerMovementVelocity = std::abs(playerMovement);
			if (_currentSpecialMove == SpecialMoveType::Buttstomp) {
				_speed.X = 0.2f * playerMovement;
				if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Run)) {
					_speed.X *= 2.6f;
				}
			} else if (canWalk && playerMovementVelocity > 0.5f) {
				SetAnimation(_currentAnimation->State & ~(AnimState::Lookup | AnimState::Crouch));

				if (_dizzyTime > 0.0f) {
					SetFacingLeft(playerMovement > 0.0f);
				} else {
					SetFacingLeft(playerMovement < 0.0f);
				}

				_isActivelyPushing = _wasActivelyPushing = true;

				float acceleration = (_levelHandler->IsReforged() ? Acceleration : Acceleration * 2.0f);
				if (_dizzyTime > 0.0f || _playerType == PlayerType::Frog) {
					_speed.X = std::clamp(_speed.X + acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxDizzySpeed * playerMovementVelocity, MaxDizzySpeed * playerMovementVelocity);
				} else if (_inShallowWater != -1 && _levelHandler->IsReforged() && _playerType != PlayerType::Lori) {
					// Use lower speed in shallow water if Reforged
					// Also, exclude Lori, because she can't ledge climb or double jump (rescue/01_colon1)
					_speed.X = std::clamp(_speed.X + acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxShallowWaterSpeed * playerMovementVelocity, MaxShallowWaterSpeed * playerMovementVelocity);
				} else {
					if (_suspendType == SuspendType::None && !_inWater && _levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Run)) {
						_speed.X = std::clamp(_speed.X + acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxDashingSpeed * playerMovementVelocity, MaxDashingSpeed * playerMovementVelocity);
					} else if (_suspendType == SuspendType::Vine) {
						if (_wasFirePressed) {
							_speed.X = 0.0f;
						} else {
							// If Run is pressed, player moves faster on vines
							if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Run)) {
								playerMovementVelocity *= 1.6f;
							}
							_speed.X = std::clamp(_speed.X + acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxVineSpeed * playerMovementVelocity, MaxVineSpeed * playerMovementVelocity);
						}
					} else if (_suspendType != SuspendType::Hook) {
						_speed.X = std::clamp(_speed.X + acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxRunningSpeed * playerMovementVelocity, MaxRunningSpeed * playerMovementVelocity);
					}
				}

				if (GetState(ActorState::CanJump)) {
					_wasUpPressed = _wasDownPressed = false;
				}
			} else {
				_speed.X = std::max((std::abs(_speed.X) - Deceleration * timeMult), 0.0f) * (_speed.X < 0.0f ? -1.0f : 1.0f);
				_isActivelyPushing = false;

				float absSpeedX = std::abs(_speed.X);
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

		if (_hitFloorTime > 0.0f) {
			_hitFloorTime -= timeMult;
		}

		if (!_controllable || !_controllableExternal || _currentSpecialMove == SpecialMoveType::Buttstomp) {
			// Weapons are automatically disabled if player is not controllable
			if (_currentWeapon != WeaponType::Thunderbolt || _fireFramesLeft <= 0.0f) {
				if (_weaponSound != nullptr) {
					_weaponSound->stop();
					_weaponSound = nullptr;
				}
			}

			return;
		}

		if (_inWater || _activeModifier != Modifier::None) {
			float playerMovement = _levelHandler->PlayerVerticalMovement(_playerIndex);
			float playerMovementVelocity = std::abs(playerMovement);
			if (playerMovementVelocity > 0.5f) {
				float mult;
				switch (_activeModifier) {
					case Modifier::Airboard: mult = (playerMovement > 0 ? -1.0f : 0.2f); break;
					case Modifier::LizardCopter: mult = (playerMovement > 0 ? -2.0f : 2.0f); break;
					default: mult = (playerMovement > 0 ? -1.0f : 1.0f); break;
				}

				_speed.Y = std::clamp(_speed.Y - Acceleration * timeMult * mult, -MaxRunningSpeed * playerMovementVelocity, MaxRunningSpeed * playerMovementVelocity);
			} else {
				_speed.Y = std::max((std::abs(_speed.Y) - Deceleration * timeMult), 0.0f) * (_speed.Y < 0.0f ? -1.0f : 1.0f);
			}
		} else {
			// Look-up
			if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Up)) {
				if (!_wasUpPressed && _dizzyTime <= 0.0f) {
					if ((GetState(ActorState::CanJump) || (_suspendType != SuspendType::None && _suspendType != SuspendType::SwingingVine)) && !_isLifting && std::abs(_speed.X) < std::numeric_limits<float>::epsilon()) {
						_wasUpPressed = true;

						SetAnimation(AnimState::Lookup | (_currentAnimation->State & AnimState::Hook));
					}
				}
			} else if (_wasUpPressed) {
				_wasUpPressed = false;

				SetAnimation(_currentAnimation->State & ~AnimState::Lookup);
			}

			// Crouch
			if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Down)) {
				if (_suspendType == SuspendType::SwingingVine) {
					// TODO: Swinging vine
				} else if (_suspendType != SuspendType::None) {
					_wasDownPressed = true;

					MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative | MoveType::Force);
					_suspendType = SuspendType::None;
					_suspendTime = 4.0f;

					SetState(ActorState::ApplyGravitation, true);
				} else if (_dizzyTime <= 0.0f) {
					if (GetState(ActorState::CanJump)) {
						if (!_isLifting && std::abs(_speed.X) < std::numeric_limits<float>::epsilon()) {
							_wasDownPressed = true;
							if (_fireFramesLeft > 0.0f) {
								SetAnimation(AnimState::Crouch | AnimState::Shoot);
							} else {
								SetAnimation(AnimState::Crouch);
							}
						}
					} else if (!_wasDownPressed && _playerType != PlayerType::Frog) {
						_wasDownPressed = true;

						_speed.X = 0.0f;
						_speed.Y = 0.0f;
						_internalForceY = 0.0f;
						_externalForce.Y = 0.0f;
						SetState(ActorState::ApplyGravitation, false);
						_currentSpecialMove = SpecialMoveType::Buttstomp;
						SetAnimation(AnimState::Buttstomp);
						SetPlayerTransition(AnimState::TransitionButtstompStart, true, false, SpecialMoveType::Buttstomp, [this]() {
							_speed.Y = 9.0f;
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

			// Jump
			if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Jump)) {
				if (!_wasJumpPressed) {
					_wasJumpPressed = true;

					if (_suspendType == SuspendType::None && _jumpTime <= 0.0f) {
						if (_isLifting && GetState(ActorState::CanJump) && _currentSpecialMove == SpecialMoveType::None) {
							SetState(ActorState::CanJump, false);
							SetAnimation(_currentAnimation->State & (~AnimState::Lookup & ~AnimState::Crouch));
							PlaySfx("Jump"_s);
							_carryingObject = nullptr;

							SetState(ActorState::IsSolidObject | ActorState::CollideWithSolidObjects, false);

							_isLifting = false;
							_controllable = false;
							_jumpTime = 12.0f;

							_speed.Y = -3.0f;
							_internalForceY = -0.88f;

							SetTransition(AnimState::TransitionLiftEnd, false, [this]() {
								_controllable = true;
								SetState(ActorState::CollideWithSolidObjects, true);
							});
						} else {
							switch (_playerType) {
								case PlayerType::Jazz: {
									if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
										_controllable = false;
										SetAnimation(AnimState::Uppercut);
										SetPlayerTransition(AnimState::TransitionUppercutA, true, true, SpecialMoveType::Uppercut, [this]() {
											_externalForce.Y = (_levelHandler->IsReforged() ? -1.4f : -1.2f);
											_speed.Y = -2.0f;
											SetState(ActorState::CanJump, false);
											SetPlayerTransition(AnimState::TransitionUppercutB, true, true, SpecialMoveType::Uppercut);
										});
									} else {
										if (_speed.Y > 0.01f && !GetState(ActorState::CanJump) && (_currentAnimation->State & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
											SetState(ActorState::ApplyGravitation, false);
											_speed.Y = 1.5f;
											_externalForce.Y = 0.0f;
											if ((_currentAnimation->State & AnimState::Copter) != AnimState::Copter) {
												SetAnimation(AnimState::Copter);
											}
											_copterFramesLeft = 70.0f;

											if (_copterSound == nullptr) {
												_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
												if (_copterSound != nullptr) {
													_copterSound->setLooping(true);
												}
											}
										}
									}
									break;
								}
								case PlayerType::Spaz: {
									if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
										_controllable = false;
										SetAnimation(AnimState::Uppercut);
										SetPlayerTransition(AnimState::TransitionUppercutA, true, true, SpecialMoveType::Sidekick, [this]() {
											_externalForce.X = 8.0f * (IsFacingLeft() ? -1.0f : 1.0f);
											_speed.X = 14.4f * (IsFacingLeft() ? -1.0f : 1.0f);
											SetState(ActorState::ApplyGravitation, false);
											SetPlayerTransition(AnimState::TransitionUppercutB, true, true, SpecialMoveType::Sidekick);
										});

										PlayPlayerSfx("Sidekick"_s);
									} else {
										if (!GetState(ActorState::CanJump) && _canDoubleJump) {
											_canDoubleJump = false;
											_isFreefall = false;

											_internalForceY = (_levelHandler->IsReforged() ? -1.15f : -0.88f) - 0.1f * (1.0f - timeMult);
											_speed.Y = -0.6f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f);
											_speed.X = std::clamp(_speed.X * 0.4f, -1.0f, 1.0f);

											PlaySfx("DoubleJump"_s);

											SetTransition(AnimState::Spring, false);
										}
									}
									break;
								}
								case PlayerType::Lori: {
									if ((_currentAnimation->State & AnimState::Crouch) == AnimState::Crouch) {
										_controllable = false;
										SetAnimation(AnimState::Uppercut);
										SetPlayerTransition(AnimState::TransitionUppercutA, true, true, SpecialMoveType::Sidekick, [this]() {
											_externalForce.X = 15.0f * (IsFacingLeft() ? -1.0f : 1.0f);
											_speed.X = 6.0f * (IsFacingLeft() ? -1.0f : 1.0f);
											SetState(ActorState::ApplyGravitation, false);
										});
									} else {
										if (_speed.Y > 0.01f && !GetState(ActorState::CanJump) && (_currentAnimation->State & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
											SetState(ActorState::ApplyGravitation, false);
											_speed.Y = 1.5f;
											_externalForce.Y = 0.0f;
											if ((_currentAnimation->State & AnimState::Copter) != AnimState::Copter) {
												SetAnimation(AnimState::Copter);
											}
											_copterFramesLeft = 70.0f;

											if (_copterSound == nullptr) {
												_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
												if (_copterSound != nullptr) {
													_copterSound->setLooping(true);
												}
											}
										}
									}
									break;
								}
							}
						}
					}
				} else {
					if (_suspendType != SuspendType::None) {
						if (_suspendType == SuspendType::SwingingVine) {
							SetCarryingObject(nullptr);
							_springCooldown = 30.0f;
						} else {
							MoveInstantly(Vector2(0.0f, -4.0f), MoveType::Relative | MoveType::Force);
						}
						SetState(ActorState::CanJump, true);
						_canDoubleJump = true;
					}
					if (!GetState(ActorState::CanJump)) {
						if (_copterFramesLeft > 0.0f) {
							_copterFramesLeft = 70.0f;
						}
					} else if (_currentSpecialMove == SpecialMoveType::None && !_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Down)) {
						SetState(ActorState::CanJump, false);
						_isFreefall = false;
						SetAnimation(_currentAnimation->State & (~AnimState::Lookup & ~AnimState::Crouch));
						if (_jumpTime <= 0.0f) {
							PlaySfx("Jump"_s);
						}
						_jumpTime = 12.0f;
						_carryingObject = nullptr;

						// Gravitation is sometimes off because of active copter, turn it on again
						SetState(ActorState::ApplyGravitation, true);
						SetState(ActorState::IsSolidObject, false);

						if (_levelHandler->IsReforged()) {
							_speed.Y = -3.55f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f);
							_internalForceY = -1.02f - 0.07f * (1.0f - timeMult);
						} else {
							_speed.Y = -8.0f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f);
						}
					}
				}
			} else {
				if (_wasJumpPressed) {
					_wasJumpPressed = false;
					if (_levelHandler->IsReforged()) {
						if (_internalForceY < 0.0f) {
							_internalForceY = 0.0f;
						}
					} else {
						if (_speed.Y < -4.0f) {
							_speed.Y = -4.0f;
						}
					}
				}
			}
		}

		// Fire
		bool weaponInUse = false;
		if (_weaponAllowed && areaWeaponAllowed && _levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Fire)) {
			if (!_isLifting && _suspendType != SuspendType::SwingingVine && (_currentAnimation->State & AnimState::Push) != AnimState::Push && _pushFramesLeft <= 0.0f) {
				if (_playerType == PlayerType::Frog) {
					if (_currentTransition == nullptr && std::abs(_speed.X) < 0.1f && std::abs(_speed.Y) < 0.1f && std::abs(_externalForce.X) < 0.1f && std::abs(_externalForce.Y) < 0.1f) {
						PlaySfx("Tongue"_s, 0.8f);

						_controllable = false;
						_controllableTimeout = 120.0f;

						SetTransition(_currentAnimation->State | AnimState::Shoot, false, [this]() {
							_controllable = true;
							_controllableTimeout = 0.0f;
						});
					}
				} else if (_weaponAmmo[(int)_currentWeapon] != 0) {
					_wasFirePressed = true;

					bool weaponCooledDown = (_weaponCooldown <= 0.0f);
					weaponInUse = FireCurrentWeapon(_currentWeapon);
					if (weaponInUse) {
						if (_currentTransition != nullptr && (_currentTransition->State == AnimState::Spring || _currentTransition->State == AnimState::TransitionShootToIdle)) {
							ForceCancelTransition();
						}

						SetAnimation(_currentAnimation->State | AnimState::Shoot);
						// Rewind the animation, if it should be played only once
						if (weaponCooledDown && _currentAnimation->LoopMode == AnimationLoopMode::Once) {
							_renderer.AnimTime = 0.0f;
						}

						_fireFramesLeft = 20.0f;
					}
				}
			}
		} else if (_wasFirePressed) {
			_wasFirePressed = false;

			_weaponCooldown = 0.0f;
		}

		if (_weaponSound != nullptr) {
			if (weaponInUse) {
				_weaponSound->setPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_weaponSound->stop();
				_weaponSound = nullptr;
			}
		}

		if (_controllable && _controllableExternal && _playerType != PlayerType::Frog) {
			bool isGamepad;
			if (_levelHandler->PlayerActionHit(_playerIndex, PlayerActions::ChangeWeapon, true, isGamepad)) {
				if (!isGamepad || PreferencesCache::WeaponWheel == WeaponWheelStyle::Disabled) {
					SwitchToNextWeapon();
				}
			} else {
				for (std::uint32_t i = 0; i <= (std::uint32_t)PlayerActions::SwitchToThunderbolt - (std::uint32_t)PlayerActions::SwitchToBlaster; i++) {
					if (_levelHandler->PlayerActionHit(_playerIndex, (PlayerActions)(i + (std::uint32_t)PlayerActions::SwitchToBlaster))) {
						SwitchToWeaponByIndex(i);
					}
				}
			}
		}
	}

	bool Player::OnDraw(RenderQueue& renderQueue)
	{
		if (_weaponFlareTime > 0.0f && !_inWater && _currentTransition == nullptr) {
			auto* res = _metadata->FindAnimation((AnimState)536870950); // WeaponFlare
			if (res != nullptr) {
				auto& command = _weaponFlareCommand;
				if (command == nullptr) {
					command = std::make_unique<RenderCommand>();
					command->material().setBlendingEnabled(true);
				}

				if (command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE)) {
					command->material().reserveUniformsDataMemory();
					command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE);
					//command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

					GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
					if (textureUniform && textureUniform->intValue(0) != 0) {
						textureUniform->setIntValue(0); // GL_TEXTURE0
					}
				}

				Vector2i texSize = res->Base->TextureDiffuse->size();
				int curAnimFrame = res->FrameOffset + (_weaponFlareFrame % res->FrameCount);
				int col = curAnimFrame % res->Base->FrameConfiguration.X;
				int row = curAnimFrame / res->Base->FrameConfiguration.X;
				float texScaleX = (float(res->Base->FrameDimensions.X) / float(texSize.X));
				float texBiasX = (float(res->Base->FrameDimensions.X * col) / float(texSize.X));
				float texScaleY = (float(res->Base->FrameDimensions.Y) / float(texSize.Y));
				float texBiasY = (float(res->Base->FrameDimensions.Y * row) / float(texSize.Y));

				float scaleY = std::max(_weaponFlareTime / 8.0f, 0.4f);
				switch (_playerType) {
					case  PlayerType::Jazz: scaleY *= 1.1f; break;
					case  PlayerType::Spaz: scaleY *= 1.6f; break;
					case  PlayerType::Lori: scaleY *= 1.3f; break;
				}

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

				auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
				instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(res->Base->FrameDimensions.X, res->Base->FrameDimensions.Y * scaleY);
				instanceBlock->uniform(Material::ColorUniformName)->setFloatValue(1.0f, 1.0f, 1.0f, 1.8f);

				Matrix4x4f worldMatrix = Matrix4x4f::Translation(gunspotPosX, gunspotPosY, 0.0f);
				if (lookUp) {
					worldMatrix.RotateZ(-fRadAngle90);
				}
				worldMatrix.Translate(res->Base->FrameDimensions.X * -0.5f, res->Base->FrameDimensions.Y * scaleY * -0.5f, 0.0f);
				command->setTransformation(worldMatrix);
				command->setLayer(_renderer.layer() + 2);
				command->material().setTexture(*res->Base->TextureDiffuse.get());

				renderQueue.addCommand(command.get());
			}
		}

		switch (_activeShield) {
			case ShieldType::Fire: {
				auto* res = _metadata->FindAnimation((AnimState)536870929); // ShieldFire
				if (res != nullptr) {
					float frames = _levelHandler->ElapsedFrames();
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
							command = std::make_unique<RenderCommand>();
							command->material().setBlendingEnabled(true);
						}

						if (command->material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::ShieldFire))) {
							command->material().reserveUniformsDataMemory();
							command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
							command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

							GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->intValue(0) != 0) {
								textureUniform->setIntValue(0); // GL_TEXTURE0
							}
						}

						auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
						instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(frames * -0.008f, frames * 0.006f - sinf(frames * 0.006f), -sinf(frames * 0.015f), frames * 0.006f);
						instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(shieldSize, shieldSize);
						instanceBlock->uniform(Material::ColorUniformName)->setFloatValue(2.0f, 2.0f, 0.8f, 0.9f * shieldAlpha);

						command->setTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosX, 0.0f));
						command->setLayer(_renderer.layer() - 4);
						command->material().setTexture(*res->Base->TextureDiffuse.get());

						renderQueue.addCommand(command.get());
					}
					{
						auto& command = _shieldRenderCommands[1];
						if (command == nullptr) {
							command = std::make_unique<RenderCommand>();
							command->material().setBlendingEnabled(true);
						}

						if (command->material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::ShieldFire))) {
							command->material().reserveUniformsDataMemory();
							command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
							command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

							GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->intValue(0) != 0) {
								textureUniform->setIntValue(0); // GL_TEXTURE0
							}
						}

						auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
						instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(frames * 0.006f, sinf(frames * 0.006f), sinf(frames * 0.015f), frames * -0.006f);
						instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(shieldSize, shieldSize);
						instanceBlock->uniform(Material::ColorUniformName)->setFloatValue(2.0f, 2.0f, 1.0f, 1.0f * shieldAlpha);

						command->setTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
						command->setLayer(_renderer.layer() + 4);
						command->material().setTexture(*res->Base->TextureDiffuse.get());

						renderQueue.addCommand(command.get());
					}
				}
				break;
			}
			case ShieldType::Water: {
				auto* res = _metadata->FindAnimation((AnimState)536870930); // ShieldWater
				if (res != nullptr) {
					float frames = _levelHandler->ElapsedFrames();
					float shieldAlpha = std::min(_activeShieldTime * 0.01f, 1.0f);
					float shieldScale = std::min(_activeShieldTime * 0.016f + 0.6f, 1.0f);

					auto& command = _shieldRenderCommands[1];
					if (command == nullptr) {
						command = std::make_unique<RenderCommand>();
						command->material().setBlendingEnabled(true);
					}

					if (command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE)) {
						command->material().reserveUniformsDataMemory();
						command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE);
						command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

						GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
						if (textureUniform && textureUniform->intValue(0) != 0) {
							textureUniform->setIntValue(0); // GL_TEXTURE0
						}
					}

					Vector2i texSize = res->Base->TextureDiffuse->size();
					int curAnimFrame = res->FrameOffset + ((int)(frames * 0.24f) % res->FrameCount);
					int col = curAnimFrame % res->Base->FrameConfiguration.X;
					int row = curAnimFrame / res->Base->FrameConfiguration.X;
					float texScaleX = (float(res->Base->FrameDimensions.X) / float(texSize.X));
					float texBiasX = (float(res->Base->FrameDimensions.X * col) / float(texSize.X));
					float texScaleY = (float(res->Base->FrameDimensions.Y) / float(texSize.Y));
					float texBiasY = (float(res->Base->FrameDimensions.Y * row) / float(texSize.Y));

					float shieldPosX = _pos.X - res->Base->FrameDimensions.X * shieldScale * 0.5f;
					float shieldPosY = _pos.Y - res->Base->FrameDimensions.Y * shieldScale * 0.5f;

					if (!PreferencesCache::UnalignedViewport) {
						shieldPosX = std::floor(shieldPosX);
						shieldPosY = std::floor(shieldPosY);
					}

					auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
					instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
					instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(res->Base->FrameDimensions.X * shieldScale, res->Base->FrameDimensions.Y * shieldScale);
					instanceBlock->uniform(Material::ColorUniformName)->setFloatValue(1.0f, 1.0f, 1.0f, shieldAlpha);

					command->setTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
					command->setLayer(_renderer.layer() + 4);
					command->material().setTexture(*res->Base->TextureDiffuse.get());

					renderQueue.addCommand(command.get());
				}
				break;
			}
			case ShieldType::Lightning: {
				auto* res = _metadata->FindAnimation((AnimState)536870931); // ShieldLightning
				if (res != nullptr) {
					float frames = _levelHandler->ElapsedFrames();
					float shieldAlpha = std::min(_activeShieldTime * 0.01f, 1.0f);
					float shieldScale = std::min(_activeShieldTime * 0.016f + 0.6f, 1.0f);
					float shieldSize = 70.0f * shieldScale + sinf(frames * 0.06f) * 4.0f;

					float shieldPosX = _pos.X - shieldSize * 0.5f;
					float shieldPosY = _pos.Y - shieldSize * 0.5f;

					if (!PreferencesCache::UnalignedViewport) {
						shieldPosX = std::floor(shieldPosX);
						shieldPosY = std::floor(shieldPosY);
					}

					{
						auto& command = _shieldRenderCommands[0];
						if (command == nullptr) {
							command = std::make_unique<RenderCommand>();
							command->material().setBlendingEnabled(true);
						}

						if (command->material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::ShieldLightning))) {
							command->material().reserveUniformsDataMemory();
							command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
							command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

							GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->intValue(0) != 0) {
								textureUniform->setIntValue(0); // GL_TEXTURE0
							}
						}

						auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
						instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(frames * -0.008f, frames * 0.006f - sinf(frames * 0.006f), -sinf(frames * 0.015f), frames * 0.006f);
						instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(shieldSize, shieldSize);
						instanceBlock->uniform(Material::ColorUniformName)->setFloatValue(2.0f, 2.0f, 0.8f, 0.9f * shieldAlpha);

						command->setTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
						command->setLayer(_renderer.layer() - 4);
						command->material().setTexture(*res->Base->TextureDiffuse.get());

						renderQueue.addCommand(command.get());
					}
					{
						auto& command = _shieldRenderCommands[1];
						if (command == nullptr) {
							command = std::make_unique<RenderCommand>();
							command->material().setBlendingEnabled(true);
						}

						if (command->material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::ShieldLightning))) {
							command->material().reserveUniformsDataMemory();
							command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
							command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

							GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
							if (textureUniform && textureUniform->intValue(0) != 0) {
								textureUniform->setIntValue(0); // GL_TEXTURE0
							}
						}

						auto* instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
						instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(frames * 0.006f, sinf(frames * 0.006f), sinf(frames * 0.015f), frames * -0.006f);
						instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(shieldSize, shieldSize);
						instanceBlock->uniform(Material::ColorUniformName)->setFloatValue(2.0f, 2.0f, 1.0f, shieldAlpha);

						command->setTransformation(Matrix4x4f::Translation(shieldPosX, shieldPosY, 0.0f));
						command->setLayer(_renderer.layer() + 4);
						command->material().setTexture(*res->Base->TextureDiffuse.get());

						renderQueue.addCommand(command.get());
					}
				}
				break;
			}
		}

		return ActorBase::OnDraw(renderQueue);
	}

	void Player::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
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

		for (int i = 0; i < (int)_trail.size(); i++) {
			lights.emplace_back(_trail[i]);
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

			// Load original metadata
			switch (_playerType) {
				case PlayerType::Jazz: RequestMetadata("Interactive/PlayerJazz"_s); break;
				case PlayerType::Spaz: RequestMetadata("Interactive/PlayerSpaz"_s); break;
				case PlayerType::Lori: RequestMetadata("Interactive/PlayerLori"_s); break;
				case PlayerType::Frog: RequestMetadata("Interactive/PlayerFrog"_s); break;
			}

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

	bool Player::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		ZoneScoped;

		bool handled = false;
		bool removeSpecialMove = false;
		if (auto* turtleShell = runtime_cast<Enemies::TurtleShell*>(other)) {
			if (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f) {
				other->DecreaseHealth(INT32_MAX, this);

				if ((_currentAnimation->State & AnimState::Buttstomp) == AnimState::Buttstomp) {
					removeSpecialMove = true;
					_speed.Y *= -0.6f;
					SetState(ActorState::CanJump, true);
				}
				return true;
			}
		} else if (auto* enemy = runtime_cast<Enemies::EnemyBase*>(other)) {
			if (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f || _activeShieldTime > 0.0f) {
				if (!enemy->IsInvulnerable()) {
					enemy->DecreaseHealth(4, this);
					handled = true;

					Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 2), Explosion::Type::Small);

					if (_sugarRushLeft > 0.0f) {
						if (!_inWater && GetState(ActorState::CanJump)) {
							_speed.Y = 3;
							SetState(ActorState::CanJump, false);
							_externalForce.Y = -0.6f;
						}
						_speed.Y *= -0.5f;
					}
					if ((_currentAnimation->State & AnimState::Buttstomp) == AnimState::Buttstomp) {
						removeSpecialMove = true;
						_speed.Y *= -0.6f;
						SetState(ActorState::CanJump, true);
					} else if (_currentSpecialMove != SpecialMoveType::None && enemy->GetHealth() >= 0) {
						removeSpecialMove = true;
						_externalForce.X = 0.0f;
						_externalForce.Y = 0.0f;

						if (_currentSpecialMove == SpecialMoveType::Sidekick) {
							_speed.X *= 0.5f;
						}
					}
				}

				// Decrease remaining shield time by 5 secs
				if (_activeShieldTime > (5.0f * FrameTimer::FramesPerSecond)) {
					_activeShieldTime -= (5.0f * FrameTimer::FramesPerSecond);
				}
			} else if (enemy->CanHurtPlayer()) {
				TakeDamage(1, 4 * (_pos.X > enemy->GetPos().X ? 1.0f : -1.0f));
			}
		} else if (auto* spring = runtime_cast<Environment::Spring*>(other)) {
			// Collide only with hitbox here
			if (_controllableExternal && (_currentTransition == nullptr || _currentTransition->State != AnimState::TransitionLedgeClimb) && _springCooldown <= 0.0f && spring->AABBInner.Overlaps(AABBInner)) {
				Vector2f force = spring->Activate();
				OnHitSpring(spring->GetPos(), force, spring->KeepSpeedX, spring->KeepSpeedY, removeSpecialMove);
			}

			handled = true;
		} else if (auto* bonusWarp = runtime_cast<Environment::BonusWarp*>(other)) {
			if (_currentTransition == nullptr || _currentTransitionCancellable) {
				auto cost = bonusWarp->GetCost();
				if (cost <= _coins) {
					_coins -= cost;
					bonusWarp->Activate(this);

					// Convert remaing coins to gems
					_gems += _coins;
					_coins = 0;
				} else if (_bonusWarpTimer <= 0.0f) {
					_levelHandler->ShowCoins(this, _coins);
					PlaySfx("BonusWarpNotEnoughCoins"_s);

					_bonusWarpTimer = 400.0f;
				}
			}

			handled = true;
		}

		if (removeSpecialMove) {
			_controllable = true;
			EndDamagingMove();
		}

		return handled;
	}

	void Player::OnHitFloor(float timeMult)
	{
		if (_levelHandler->TileMap()->IsTileHurting(_pos.X, _pos.Y + 24)) {
			TakeDamage(1, _speed.X * 0.25f);
		} else if (!_inWater && _activeModifier == Modifier::None) {
			if (_hitFloorTime <= 0.0f && !GetState(ActorState::CanJump)) {
				_hitFloorTime = 30.0f;
				PlaySfx("Land"_s, 0.8f);

				if (Random().NextFloat() < 0.6f) {
					Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y + 20, _renderer.layer() - 2), Explosion::Type::TinyDark);
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
		if (_levelHandler->TileMap()->IsTileHurting(_pos.X, _pos.Y - 4.0f)) {
			TakeDamage(1, _speed.X * 0.25f);
		}
	}

	void Player::OnHitWall(float timeMult)
	{
		// Reset speed and show Push animation
		_speed.X = 0.0f;
		_pushFramesLeft = 2.0f;
		_keepRunningTime = 0.0f;

		if (_levelHandler->TileMap()->IsTileHurting(_pos.X + (_speed.X > 0.0f ? 1.0f : -1.0f) * 16.0f, _pos.Y)) {
			TakeDamage(1, _speed.X * 0.25f);
		} else {
			if (PreferencesCache::EnableLedgeClimb && _isActivelyPushing && _suspendType == SuspendType::None && _activeModifier == Modifier::None && !GetState(ActorState::CanJump) &&
				!_inWater && _currentSpecialMove == SpecialMoveType::None && (_currentTransition == nullptr || _currentTransition->State != AnimState::TransitionUppercutEnd) &&
				_speed.Y >= -1.0f && _externalForce.Y >= 0.0f && _copterFramesLeft <= 0.0f && _keepRunningTime <= 0.0f) {

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
							for (int y = 0; y >= -MaxTolerancePixels; y -= 1) {
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
							_speed.Y = -1.4f;
							if (timeMult < 1.0f) {
								_speed.Y += (1.0f - timeMult) * 0.4f;
							}

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
							});
						}
					}

					SetState(ActorState::CollideWithTilesetReduced, true);
				}
			}
		}
	}

	void Player::OnHitSpring(const Vector2f& pos, const Vector2f& force, bool keepSpeedX, bool keepSpeedY, bool& removeSpecialMove)
	{
		std::int32_t sign = ((force.X + force.Y) > std::numeric_limits<float>::epsilon() ? 1 : -1);
		if (std::abs(force.X) > 0.0f) {
			MoveInstantly(Vector2f(_pos.X, (_pos.Y + pos.Y) * 0.5f), MoveType::Absolute);

			removeSpecialMove = true;
			_copterFramesLeft = 0.0f;
			//speedX = force.X;
			_speed.X = (1.0f + std::abs(force.X)) * sign;
			_externalForce.X = force.X * 0.6f;
			_springCooldown = 10.0f;
			SetState(ActorState::CanJump, false);

			_wasActivelyPushing = false;
			_keepRunningTime = 100.0f;

			if (!keepSpeedY) {
				_speed.Y = 0.0f;
				_externalForce.Y = 0.0f;
			}

			if (_inIdleTransition) {
				_inIdleTransition = false;
				CancelTransition();
			}

			SetPlayerTransition(AnimState::Dash | AnimState::Jump, true, false, SpecialMoveType::None);
			_controllableTimeout = 2.0f;
		} else if (std::abs(force.Y) > 0.0f) {
			MoveInstantly(Vector2f(lerp(_pos.X, pos.X, 0.3f), _pos.Y), MoveType::Absolute);

			if (_copterFramesLeft > 0.0f) {
				_copterFramesLeft = 0.0f;
				SetAnimation(_currentAnimation->State & ~AnimState::Copter);
				SetState(ActorState::ApplyGravitation, true);
			}

			_speed.Y = (4.0f + std::abs(force.Y)) * sign;
			if (!GetState(ActorState::ApplyGravitation)) {
				_externalForce.Y = force.Y * 0.14f;
			} else if (_levelHandler->IsReforged()) {
				_externalForce.Y = force.Y;
			} else {
				_externalForce.Y = force.Y * 0.8f;
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
				_currentSpecialMove = SpecialMoveType::Buttstomp;
				SetAnimation(AnimState::Buttstomp);
			} else {
				removeSpecialMove = true;
				_isSpring = true;
			}

			PlaySfx("Spring"_s);
		}
	}

	void Player::OnWaterSplash(const Vector2f& pos, bool inwards)
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
		} else if (_isLifting) {
			newState = AnimState::Lift;
		} else if (GetState(ActorState::CanJump) && _isActivelyPushing && _pushFramesLeft > 0.0f && _keepRunningTime <= 0.0f) {
			newState = AnimState::Push;

			if (_inIdleTransition) {
				_inIdleTransition = false;
				CancelTransition();
			}
		} else {
			// Only certain ones don't need to be preserved from earlier state, others should be set as expected
			AnimState composite = (_currentAnimation->State & (AnimState)0xFFF83F60);

			if (_isActivelyPushing == _wasActivelyPushing) {
				float absSpeedX = std::abs(_speed.X);
				if (absSpeedX > MaxRunningSpeed) {
					composite |= AnimState::Dash;
				} else if (_keepRunningTime > 0.0f) {
					composite |= AnimState::Run;
				} else if (absSpeedX > 0.0f) {
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
				if (GetState(ActorState::CanJump) || _carryingObject != nullptr) {
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
					std::int32_t maxIdx;
					switch (_playerType) {
						case PlayerType::Jazz: maxIdx = 5; break;
						case PlayerType::Spaz: maxIdx = 4; break;
						case PlayerType::Lori: maxIdx = 3; break;
						default: maxIdx = 0; break;
					}
					if (maxIdx > 0) {
						std::int32_t selectedIdx = Random().Fast(0, maxIdx);
						if (SetTransition((AnimState)(536870944 + selectedIdx), true)) {
							PlaySfx(IdleBored[selectedIdx]);
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
					if (newState == AnimState::Idle) {
						_inIdleTransition = true;
						SetTransition(AnimState::TransitionRunToIdle, true, [this]() {
							_inIdleTransition = false;
						});
					} else if (newState == AnimState::Dash) {
						SetTransition(AnimState::TransitionRunToDash, true);
					}
					break;
				case AnimState::Dash:
					if (newState == AnimState::Idle) {
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

							PlaySfx("Ledge"_s);
						}
					}
					break;
			}
		}
	}

	void Player::PushSolidObjects(float timeMult)
	{
		if (_pushFramesLeft > 0.0f) {
			_pushFramesLeft -= timeMult;
		}

		if (GetState(ActorState::CanJump) && _controllable && _controllableExternal && _isActivelyPushing && std::abs(_speed.X) > 0.0f) {
			AABBf hitbox = AABBInner + Vector2f(_speed.X < 0.0f ? -2.0f : 2.0f, 0.0f);
			TileCollisionParams params = { TileDestructType::None, false };
			ActorBase* collider;
			if (!_levelHandler->IsPositionEmpty(this, hitbox, params, &collider)) {
				if (auto* solidObject = runtime_cast<SolidObjectBase*>(collider)) {
					SetState(ActorState::IsSolidObject, false);
					float pushSpeedX = solidObject->Push(_speed.X < 0, timeMult);
					if (std::abs(pushSpeedX) > 0.0f) {
						_speed.X = pushSpeedX * 1.2f * timeMult;
						_pushFramesLeft = 3.0f;
					}
					SetState(ActorState::IsSolidObject, true);
				}
			}
		} else if (GetState(ActorState::IsSolidObject)) {
			AABBf aabb = AABBf(AABBInner.L, AABBInner.T - 20.0f, AABBInner.R, AABBInner.T + 6.0f);
			TileCollisionParams params = { TileDestructType::None, false };
			ActorBase* collider;
			ActorState prevState = GetState();
			SetState(ActorState::CollideWithTileset, false);
			if (!_levelHandler->IsPositionEmpty(this, aabb, params, &collider)) {
				if (auto* solidObject = runtime_cast<SolidObjectBase*>(collider)) {
					if (AABBInner.T >= solidObject->AABBInner.T && !_isLifting && std::abs(_speed.Y) < 1.0f) {
						_isLifting = true;
						SetTransition(AnimState::TransitionLiftStart, true);
					}
				} else {
					_isLifting = false;
				}
			} else {
				_isLifting = false;
			}
			SetState(prevState);
		} else {
			_isLifting = false;
		}
	}

	void Player::CheckEndOfSpecialMoves(float timeMult)
	{
		// Buttstomp
		if (_currentSpecialMove == SpecialMoveType::Buttstomp && (GetState(ActorState::CanJump) || _suspendType != SuspendType::None || _carryingObject != nullptr)) {
			EndDamagingMove();
			if (_suspendType == SuspendType::None && !_isSpring) {
				std::int32_t tx = (std::int32_t)_pos.X / 32;
				std::int32_t ty = ((std::int32_t)_pos.Y + 24) / 32;

				std::uint8_t* eventParams;
				if (_levelHandler->EventMap()->GetEventByPosition(tx, ty, &eventParams) == EventType::GemStomp) {
					_levelHandler->EventMap()->StoreTileEvent(tx, ty, EventType::Empty);

					for (std::int32_t i = 0; i < 8; i++) {
						float fx = Random().NextFloat(-16.0f, 16.0f);
						float fy = Random().NextFloat(-12.0f, 0.2f);

						std::uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] = { };
						std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(EventType::Gem, spawnParams, ActorState::None, Vector3i((int)(_pos.X + fx * 2.0f), (int)(_pos.Y + fy * 4.0f), _renderer.layer() - 10));
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

		// Uppercut
		if (_currentSpecialMove == SpecialMoveType::Uppercut && _currentTransition == nullptr && ((_currentAnimation->State & AnimState::Uppercut) == AnimState::Uppercut) && _speed.Y > -2) {
			EndDamagingMove();
		}

		// Sidekick
		if (_currentSpecialMove == SpecialMoveType::Sidekick && _currentTransition == nullptr && std::abs(_speed.X) < 0.01f) {
			EndDamagingMove();
			_controllable = true;
			if (_suspendType == SuspendType::None) {
				SetTransition(AnimState::TransitionUppercutEnd, false);
			}
		}

		// Copter Ears
		if (_activeModifier != Modifier::Copter && _activeModifier != Modifier::LizardCopter) {
			// TODO: Is this still needed?
			bool cancelCopter;
			if ((_currentAnimation->State & AnimState::Copter) == AnimState::Copter) {
				cancelCopter = (GetState(ActorState::CanJump) || _suspendType != SuspendType::None || _copterFramesLeft <= 0.0f);

				_copterFramesLeft -= timeMult;
				_speed.Y = std::min(_speed.Y + _levelHandler->Gravity * timeMult, 1.5f);
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

		if (_suspendType == SuspendType::SwingingVine) {
			return;
		}

		auto tiles = _levelHandler->TileMap();
		if (tiles == nullptr) {
			return;
		}

		AnimState currentState = _currentAnimation->State;

		SuspendType newSuspendState = tiles->GetTileSuspendState(_pos.X, _pos.Y - 1.0f);

		if (newSuspendState == _suspendType) {
			if (newSuspendState == SuspendType::None) {
				constexpr float ToleranceX = 8.0f;
				constexpr float ToleranceY = 4.0f;

				newSuspendState = tiles->GetTileSuspendState(_pos.X - ToleranceX, _pos.Y - 1.0f);
				if (newSuspendState != SuspendType::Hook) {
					newSuspendState = tiles->GetTileSuspendState(_pos.X + ToleranceX, _pos.Y - 1.0f);
					if (newSuspendState != SuspendType::Hook) {
						// Also try with Y tolerance
						newSuspendState = tiles->GetTileSuspendState(_pos.X, _pos.Y - 1.0f + ToleranceY);
						if (newSuspendState != SuspendType::Hook) {
							newSuspendState = tiles->GetTileSuspendState(_pos.X - ToleranceX, _pos.Y - 1.0f + ToleranceY);
							if (newSuspendState != SuspendType::Hook) {
								newSuspendState = tiles->GetTileSuspendState(_pos.X + ToleranceX, _pos.Y - 1.0f + ToleranceY);
								if (newSuspendState != SuspendType::Hook) {
									return;
								} else {
									MoveInstantly(Vector2f(ToleranceX, ToleranceY), MoveType::Relative | MoveType::Force);
								}
							} else {
								MoveInstantly(Vector2f(-ToleranceX, ToleranceY), MoveType::Relative | MoveType::Force);
							}
						} else {
							MoveInstantly(Vector2f(0.0f, ToleranceY), MoveType::Relative | MoveType::Force);
						}
					} else {
						MoveInstantly(Vector2f(ToleranceX, 0.0f), MoveType::Relative | MoveType::Force);
					}
				} else {
					MoveInstantly(Vector2f(-ToleranceX, 0.0f), MoveType::Relative | MoveType::Force);
				}
			} else {
				return;
			}
		}

		if (newSuspendState != SuspendType::None && _playerType != PlayerType::Frog) {
			if (_currentSpecialMove == SpecialMoveType::None) {
				_suspendType = newSuspendState;
				SetState(ActorState::ApplyGravitation, false);

				if (_speed.Y > 0.0f && newSuspendState == SuspendType::Vine) {
					PlaySfx("HookAttach"_s, 0.8f, 1.2f);
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
			if (_pos.Y >= _levelHandler->WaterLevel()) {
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

				OnWaterSplash(Vector2f(_pos.X, _levelHandler->WaterLevel()), false);
			}
		} else {
			if (_pos.Y >= _levelHandler->WaterLevel() && _waterCooldownLeft <= 0.0f) {
				_inWater = true;
				_waterCooldownLeft = 20.0f;

				_controllable = true;
				EndDamagingMove();

				OnWaterSplash(Vector2f(_pos.X, _levelHandler->WaterLevel()), true);
			}

			// Adjust walking animation speed
			if (_currentAnimation->State == AnimState::Walk && _currentTransition == nullptr) {
				_renderer.AnimDuration = _currentAnimation->AnimDuration * (1.4f - 0.4f * std::min(std::abs(_speed.X), MaxRunningSpeed) / MaxRunningSpeed);
			}
		}
	}

	void Player::OnHandleAreaEvents(float timeMult, bool& areaWeaponAllowed, int& areaWaterBlock)
	{
		areaWeaponAllowed = true;
		areaWaterBlock = -1;

		auto events = _levelHandler->EventMap();
		if (events == nullptr) {
			return;
		}

		uint8_t* p;
		EventType tileEvent = events->GetEventByPosition(_pos.X, _pos.Y, &p);
		switch (tileEvent) {
			case EventType::LightAmbient: { // Intensity, Red, Green, Blue, Flicker
				// TODO: Change only player view, handle splitscreen multiplayer
				_levelHandler->SetAmbientLight(this, p[0] / 255.0f);
				break;
			}
			case EventType::WarpOrigin: { // Warp ID, Fast, Set Lap
				if (_currentTransition == nullptr || _currentTransition->State == (AnimState::Dash | AnimState::Jump) || _currentTransitionCancellable) {
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
					}
				}
				break;
			}
			case EventType::ModifierDeath: {
				DecreaseHealth(INT32_MAX);
				break;
			}
			case EventType::ModifierSetWater: {
				_levelHandler->BroadcastTriggeredEvent(this, EventType::ModifierSetWater, p);
				break;
			}
			case EventType::ModifierLimitCameraView: { // Left, Width
				uint16_t left = *(uint16_t*)&p[0];
				uint16_t width = *(uint16_t*)&p[2];
				_levelHandler->LimitCameraView((left == 0 ? (int)(_pos.X / Tiles::TileSet::DefaultTileSize) : left) * Tiles::TileSet::DefaultTileSize, width * Tiles::TileSet::DefaultTileSize);
				break;
			}
			case EventType::ModifierHPole: {
				InitialPoleStage(true);
				break;
			}
			case EventType::ModifierVPole: {
				InitialPoleStage(false);
				break;
			}
			case EventType::ModifierTube: { // XSpeed, YSpeed, Wait Time, Trig Sample, Become No-clip, No-clip Only
				// TODO: Implement other parameters
				if (p[4] == 0 && p[5] != 0 && GetState(ActorState::CollideWithTileset)) {
					break;
				}

				EndDamagingMove();

				SetAnimation(AnimState::Dash | AnimState::Jump);

				_controllable = false;
				SetState(ActorState::CanJump | ActorState::ApplyGravitation, false);

				_speed.X = (float)(int8_t)p[0];
				_speed.Y = (float)(int8_t)p[1];

				Vector2f pos = _pos;
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

				if (p[4] != 0) { // Become No-clip
					SetState(ActorState::CollideWithTileset, false);
					_inTubeTime = 60.0f;
				} else {
					_inTubeTime = 10.0f;
				}
				break;
			}
			case EventType::AreaEndOfLevel: { // ExitType, Fast (No score count, only black screen), TextID, TextOffset, Coins
				if (_levelExiting == LevelExitingState::None) {
					// TODO: Implement Fast parameter
					uint16_t coinsRequired = *(uint16_t*)&p[4];
					if (coinsRequired <= _coins) {
						_coins -= coinsRequired;

						ExitType exitType = (ExitType)p[0];
						if (p[1] != 0) {
							exitType |= ExitType::FastTransition;
						}
						String nextLevel;
						if (p[2] != 0 && p[3] != 0) {
							nextLevel = _levelHandler->GetLevelText(p[2], p[3], '|');
						}
						_levelHandler->BeginLevelChange(exitType, nextLevel);
					} else if (_bonusWarpTimer <= 0.0f) {
						_levelHandler->ShowCoins(this, _coins);
						PlaySfx("BonusWarpNotEnoughCoins"_s);

						_bonusWarpTimer = 400.0f;
					}
				}
				break;
			}
			case EventType::AreaText: { // Text, TextOffset, Vanish
				uint8_t index = p[1];
				StringView text = _levelHandler->GetLevelText(p[0], index != 0 ? index : -1, '|');
				_levelHandler->ShowLevelText(text);

				if (p[2] != 0) {
					events->StoreTileEvent((int)(_pos.X / 32), (int)(_pos.Y / 32), EventType::Empty);
				}
				break;
			}
			case EventType::AreaCallback: { // Function, Param, Vanish
				_levelHandler->BroadcastTriggeredEvent(this, EventType::AreaCallback, p);
				if (p[2] != 0) {
					events->StoreTileEvent((int)(_pos.X / 32), (int)(_pos.Y / 32), EventType::Empty);
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
				if (_activeModifier == Modifier::Airboard) {
					SetModifier(Modifier::None);
				}
				break;
			}
			case EventType::AreaRevertMorph: {
				if (_playerType != _playerTypeOriginal) {
					MorphRevert();
				}
				break;
			}
			case EventType::AreaMorphToFrog: {
				if (_playerType != PlayerType::Frog) {
					MorphTo(PlayerType::Frog);
				}
				break;
			}
			case EventType::AreaNoFire: {
				switch (p[0]) {
					case 0: areaWeaponAllowed = false; break;
					case 1: _weaponAllowed = true; break;
					case 2: _weaponAllowed = false; break;
				}
				break;
			}
			case EventType::AreaWaterBlock: {
				areaWaterBlock = ((int)_pos.Y / 32) * 32 + p[0];
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

		// TODO: Implement Slide modifier with JJ2+ parameter

		// Check floating from each corner of an extended hitbox
		// Player should not pass from a single tile wide gap if the columns left or right have
		// float events, so checking for a wider box is necessary.
		constexpr float ExtendedHitbox = 2.0f;

		if (_currentTransition == nullptr || _currentTransition->State != AnimState::TransitionLedgeClimb) {
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
							_speed.Y = std::max(_speed.Y - _levelHandler->Gravity * timeMult * 8.0f, -6.0f);
						} else if (GetState(ActorState::ApplyGravitation)) {
							float gravity = _levelHandler->Gravity;
							_externalForce.Y = -2.0f * gravity * timeMult;
							_speed.Y = std::min(gravity * timeMult, _speed.Y);
						} else {
							_speed.Y = std::max(_speed.Y - _levelHandler->Gravity * timeMult, -6.0f);
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
				uint8_t p1 = p[4];
				uint8_t p2 = p[5];
				if ((p2 != 0 || p1 != 0)) {
					MoveInstantly(Vector2f((p2 - p1) * 0.7f * timeMult, 0), MoveType::Relative);
				}
			}

			if (GetState(ActorState::CanJump)) {
				// Floor events
				tileEvent = events->GetEventByPosition(_pos.X, _pos.Y + 32, &p);
				switch (tileEvent) {
					case EventType::AreaHForce: {
						uint8_t p1 = p[0];
						uint8_t p2 = p[1];
						uint8_t p3 = p[2];
						uint8_t p4 = p[3];
						if (p2 != 0 || p1 != 0) {
							MoveInstantly(Vector2f((p2 - p1) * 0.7f * timeMult, 0), MoveType::Relative);
						}
						if (p4 != 0 || p3 != 0) {
							_speed.X += (p4 - p3) * 0.1f;
						}
						break;
					}
				}
			}
		}
	}

	std::shared_ptr<AudioBufferPlayer> Player::PlayPlayerSfx(const StringView identifier, float gain, float pitch)
	{
		auto it = _metadata->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _metadata->Sounds.end()) {
			int idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int)it->second.Buffers.size()) : 0);
			return _levelHandler->PlaySfx(this, identifier, &it->second.Buffers[idx]->Buffer, Vector3f(0.0f, 0.0f, 0.0f), true, gain, pitch);
		} else {
			return nullptr;
		}
	}

	bool Player::SetPlayerTransition(AnimState state, bool cancellable, bool removeControl, SpecialMoveType specialMove, const std::function<void()>& callback)
	{
		if (removeControl) {
			_controllable = false;
			_controllableTimeout = 0.0f;
		}

		_currentSpecialMove = specialMove;
		return SetTransition(state, cancellable, callback);
	}

	bool Player::SetPlayerTransition(AnimState state, bool cancellable, bool removeControl, SpecialMoveType specialMove, std::function<void()>&& callback)
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
		if (_copterSound != nullptr) {
			_copterSound->stop();
			_copterSound = nullptr;
		}
		if (_weaponSound != nullptr) {
			_weaponSound->stop();
			_weaponSound = nullptr;
		}

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
		SetModifier(Modifier::None);
		SetShield(ShieldType::None, 0.0f);

		SetPlayerTransition(AnimState::TransitionDeath, false, true, SpecialMoveType::None, [this]() {
			if (_lives > 1 || _levelHandler->Difficulty() == GameDifficulty::Multiplayer) {
				if (_lives > 1 && _lives < UINT8_MAX) {
					_lives--;
				}

				// Revert food eaten only if Reforged
				if (_levelHandler->IsReforged()) {
					_foodEaten = _foodEatenCheckpoint;
				}

				// Revert coins, gems, ammo and weapon upgrades
				_coins = _coinsCheckpoint;
				_gems = _gemsCheckpoint;

				std::memcpy(_weaponAmmo, _weaponAmmoCheckpoint, sizeof(_weaponAmmo));
				std::memcpy(_weaponUpgrades, _weaponUpgradesCheckpoint, sizeof(_weaponUpgrades));

				// Remove all fast fires and Blaster upgrades
				_weaponUpgrades[(std::int32_t)WeaponType::Blaster] = 0;

				// Reset current weapon to Blaster if player has no ammo on checkpoint
				if (_weaponAmmo[(std::int32_t)_currentWeapon] == 0) {
					SetCurrentWeapon(WeaponType::Blaster);
				}

				if (_sugarRushLeft > 0.0f) {
					_sugarRushLeft = 0.0f;
					_renderer.Initialize(ActorRendererType::Default);
				}

				// Spawn corpse
				std::shared_ptr<PlayerCorpse> corpse = std::make_shared<PlayerCorpse>();
				uint8_t playerParams[2] = { (uint8_t)_playerType, (uint8_t)(IsFacingLeft() ? 1 : 0) };
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
					SetState(ActorState::IsInvulnerable, false);
					SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects, true);
					_controllable = true;

					// Return to the last save point
					MoveInstantly(_checkpointPos, MoveType::Absolute | MoveType::Force);
					_levelHandler->SetAmbientLight(this, _checkpointLight);

					_levelHandler->RollbackToCheckpoint(this);
				} else {
					// Respawn is delayed
					_renderer.setDrawEnabled(false);

					// TODO: Turn off collisions
				}
			} else {
				_renderer.setDrawEnabled(false);

				_levelHandler->HandleGameOver(this);
			}
		});

		PlayPlayerSfx("Die"_s, 1.3f);
	}

	void Player::SwitchToNextWeapon()
	{
		if (_weaponSound != nullptr) {
			_weaponSound->stop();
			_weaponSound = nullptr;
		}

		// Find next available weapon
		WeaponType weaponType = (WeaponType)(((std::int32_t)_currentWeapon + 1) % (std::int32_t)WeaponType::Count);
		for (std::int32_t i = 0; i < (std::int32_t)WeaponType::Count && _weaponAmmo[(std::int32_t)weaponType] == 0; i++) {
			weaponType = (WeaponType)(((std::int32_t)weaponType + 1) % (std::int32_t)WeaponType::Count);
		}
		SetCurrentWeapon(weaponType);

		_weaponCooldown = 1.0f;
	}

	void Player::SwitchToWeaponByIndex(std::uint32_t weaponIndex)
	{
		if (weaponIndex >= (uint32_t)WeaponType::Count || _weaponAmmo[weaponIndex] == 0) {
			PlaySfx("ChangeWeapon"_s);
			return;
		}

		if (_weaponSound != nullptr) {
			_weaponSound->stop();
			_weaponSound = nullptr;
		}

		SetCurrentWeapon((WeaponType)weaponIndex);
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
		uint8_t shotParams[1] = { _weaponUpgrades[(int)weaponType] };
		shot->OnActivated(ActorActivationDetails(
			_levelHandler,
			initialPos,
			shotParams
		));
		shot->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
		_levelHandler->AddActor(shot);

		std::int32_t fastFire = (_weaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		_weaponCooldown = cooldownBase - (fastFire * cooldownUpgrade);

		if (emitFlare) {
			_weaponFlareFrame = (Random().Next() & 0xFFFF);
			_weaponFlareTime = 6.0f;
		}
	}

	void Player::FireWeaponRF()
	{
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		uint8_t shotParams[1] = { _weaponUpgrades[(int)WeaponType::RF] };

		if ((_weaponUpgrades[(int)WeaponType::RF] & 0x1) != 0) {
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
			shot1->OnFire(shared_from_this(), gunspotPos, _speed, angle - 0.22f, IsFacingLeft());
			_levelHandler->AddActor(shot1);

			std::shared_ptr<Weapons::RFShot> shot2 = std::make_shared<Weapons::RFShot>();
			shot2->OnActivated(ActorActivationDetails(
				_levelHandler,
				initialPos,
				shotParams
			));
			shot2->OnFire(shared_from_this(), gunspotPos, _speed, angle + 0.22f, IsFacingLeft());
			_levelHandler->AddActor(shot2);
		}

		_weaponCooldown = 120.0f;
		_weaponFlareFrame = (Random().Next() & 0xFFFF);
		_weaponFlareTime = 6.0f;
	}

	void Player::FireWeaponPepper()
	{
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		uint8_t shotParams[1] = { _weaponUpgrades[(int)WeaponType::Pepper] };

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

		std::int32_t fastFire = (_weaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		_weaponCooldown = 30.0f - (fastFire * 2.7f);
		_weaponFlareFrame = (Random().Next() & 0xFFFF);
		_weaponFlareTime = 6.0f;
	}

	void Player::FireWeaponTNT()
	{
		std::shared_ptr<Weapons::TNT> tnt = std::make_shared<Weapons::TNT>();
		tnt->OnActivated(ActorActivationDetails(
			_levelHandler,
			Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2)
		));
		tnt->OnFire(shared_from_this());
		_levelHandler->AddActor(tnt);

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
		uint8_t shotParams[1] = { _weaponUpgrades[(int)WeaponType::Thunderbolt] };
		shot->OnActivated(ActorActivationDetails(
			_levelHandler,
			initialPos,
			shotParams
		));
		shot->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
		_levelHandler->AddActor(shot);

		_weaponCooldown = 12.0f - (_weaponUpgrades[(int)WeaponType::Blaster] * 0.1f);

		if (!_inWater && (_currentAnimation->State & AnimState::Lookup) != AnimState::Lookup) {
			AddExternalForce(IsFacingLeft() ? 2.0f : -2.0f, 0.0f);
		}

		if (_weaponSound == nullptr) {
			PlaySfx("WeaponThunderboltStart"_s, 0.5f);
			_weaponSound = PlaySfx("WeaponThunderbolt"_s, 1.0f);
			if (_weaponSound != nullptr) {
				_weaponSound->setLooping(true);
				_weaponSound->setPitch(Random().FastFloat(1.05f, 1.2f));
				_weaponSound->setLowPass(0.9f);
			}
		}

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
						FireWeapon<Weapons::BlasterShot, WeaponType::Blaster>(30.0f, 2.7f, true);
						PlaySfx("WeaponBlaster"_s);
						break;
					}
				}
				ammoDecrease = 0;
				break;

			case WeaponType::Bouncer: FireWeapon<Weapons::BouncerShot, WeaponType::Bouncer>(30.0f, 2.7f, true); break;
				case WeaponType::Freezer:
					// TODO: Add upgraded freezer
					FireWeapon<Weapons::FreezerShot, WeaponType::Freezer>(30.0f, 2.7f);
					break;
				case WeaponType::Seeker: FireWeapon<Weapons::SeekerShot, WeaponType::Seeker>(120.0f, 0.0f, true); break;
				case WeaponType::RF: FireWeaponRF(); break;

				case WeaponType::Toaster: {
					if (_inWater) {
						return false;
					}
					FireWeapon<Weapons::ToasterShot, WeaponType::Toaster>(6.0f, 0.0f);
					if (_weaponSound == nullptr) {
						_weaponSound = PlaySfx("WeaponToaster"_s, 0.6f);
						if (_weaponSound != nullptr) {
							_weaponSound->setLooping(true);
						}
					}
					ammoDecrease = 50;
					break;
				}

				case WeaponType::TNT: FireWeaponTNT(); break;
				case WeaponType::Pepper: FireWeaponPepper(); break;
				case WeaponType::Electro: FireWeapon<Weapons::ElectroShot, WeaponType::Electro>(30.0f, 2.7f, true); break;

				case WeaponType::Thunderbolt: {
					if (!FireWeaponThunderbolt()) {
						return false;
					}
					ammoDecrease = ((_weaponUpgrades[(std::int32_t)WeaponType::Thunderbolt] & 0x1) != 0 ? 30 : 50); // Lower ammo consumption with upgrade
					break;
				}

			default:
				return false;
		}

		auto& currentAmmo = _weaponAmmo[(std::int32_t)weaponType];
		if (ammoDecrease > currentAmmo) {
			ammoDecrease = currentAmmo;
		}
		currentAmmo -= ammoDecrease;

		// No ammo, switch weapons
		if (currentAmmo == 0) {
			// Remove upgrade if no ammo left
			_weaponUpgrades[(std::int32_t)weaponType] &= ~0x01;

			SwitchToNextWeapon();
			PlaySfx("ChangeWeapon"_s);
			_weaponCooldown = 20.0f;
		}

		return (weaponType != WeaponType::TNT);
	}

	void Player::SetCurrentWeapon(WeaponType weaponType)
	{
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

	bool Player::OnLevelChanging(ExitType exitType)
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
				if (GetState(ActorState::CanJump) && std::abs(_speed.X) < 1.0f && std::abs(_speed.Y) < 1.0f) {
					_levelExiting = LevelExitingState::Transition;

					ForceCancelTransition();

					SetPlayerTransition(AnimState::TransitionEndOfLevel, false, true, SpecialMoveType::None, [this]() {
						_renderer.setDrawEnabled(false);
						_levelExiting = LevelExitingState::Ready;
					});
					PlayPlayerSfx("EndOfLevel1"_s);

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
					PlayPlayerSfx("WarpIn"_s);

					SetState(ActorState::ApplyGravitation, false);
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					_externalForce.X = 0.0f;
					_externalForce.Y = 0.0f;
					_internalForceY = 0.0f;
				} else {
					// Refresh animation state, because UpdateAnimation() is not called when _controllable is false
					AnimState oldState = _currentAnimation->State;
					AnimState newState = (oldState & (AnimState)0xFFF83F60);
					if (std::abs(_speed.X) > std::numeric_limits<float>::epsilon()) {
						newState |= AnimState::Walk;
					}
					if (!GetState(ActorState::CanJump)) {
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
					PlayPlayerSfx("WarpIn"_s);

					SetState(ActorState::ApplyGravitation, false);
					_speed.X = 0.0f;
					_speed.Y = 0.0f;
					_externalForce.X = 0.0f;
					_externalForce.Y = 0.0f;
					_internalForceY = 0.0f;
				} else {
					// Refresh animation state, because UpdateAnimation() is not called when _controllable is false
					AnimState oldState = _currentAnimation->State;
					AnimState newState = (oldState & (AnimState)0xFFF83F60);
					if (std::abs(_speed.X) > std::numeric_limits<float>::epsilon()) {
						newState |= AnimState::Walk;
					}
					if (!GetState(ActorState::CanJump)) {
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
			PlayPlayerSfx("EndOfLevel"_s);

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
		_foodEaten = (std::int32_t)carryOver.FoodEaten;
		_foodEatenCheckpoint = _foodEaten;
		_currentWeapon = carryOver.CurrentWeapon;

		std::memcpy(_weaponAmmo, carryOver.Ammo, sizeof(_weaponAmmo));
		std::memcpy(_weaponAmmoCheckpoint, carryOver.Ammo, sizeof(_weaponAmmoCheckpoint));
		std::memcpy(_weaponUpgrades, carryOver.WeaponUpgrades, sizeof(_weaponUpgrades));
		std::memcpy(_weaponUpgradesCheckpoint, carryOver.WeaponUpgrades, sizeof(_weaponUpgradesCheckpoint));

		_weaponAmmo[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;
		_weaponAmmoCheckpoint[(std::int32_t)WeaponType::Blaster] = UINT16_MAX;

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
			_renderer.AnimPaused = true;
			_controllable = false;
			_controllableTimeout = 100.0f;
			_frozenTimeLeft = 100.0f;
		}

		// Preload all weapons
		for (std::int32_t i = 0; i < countof(_weaponAmmo); i++) {
			if (_weaponAmmo[i] != 0) {
				PreloadMetadataAsync("Weapon/"_s + WeaponNames[i]);
			}
		}
	}

	PlayerCarryOver Player::PrepareLevelCarryOver()
	{
		PlayerCarryOver carryOver;
		carryOver.Type = _playerType;
		carryOver.Lives = (_lives > UINT8_MAX ? UINT8_MAX : (std::uint8_t)_lives);
		carryOver.Score = _score;
		carryOver.FoodEaten = (_foodEaten > UINT8_MAX ? UINT8_MAX : (std::uint8_t)_foodEaten);
		carryOver.CurrentWeapon = _currentWeapon;

		std::memcpy(carryOver.Ammo, _weaponAmmo, sizeof(_weaponAmmo));
		std::memcpy(carryOver.WeaponUpgrades, _weaponUpgrades, sizeof(_weaponUpgrades));

		return carryOver;
	}

	void Player::InitializeFromStream(ILevelHandler* levelHandler, Stream& src)
	{
		std::uint8_t playerIndex = src.ReadVariableInt32();
		PlayerType playerType = (PlayerType)src.ReadValue<std::uint8_t>();
		PlayerType playerTypeOriginal = (PlayerType)src.ReadValue<std::uint8_t>();
		float checkpointPosX = src.ReadValue<float>();
		float checkpointPosY = src.ReadValue<float>();

		std::uint8_t playerParams[2] = { (std::uint8_t)playerType, (std::uint8_t)playerIndex };
		OnActivated(Actors::ActorActivationDetails(
			levelHandler,
			Vector3i((std::int32_t)checkpointPosX, (std::int32_t)checkpointPosY, ILevelHandler::PlayerZ - playerIndex),
			playerParams
		));

		_playerTypeOriginal = playerTypeOriginal;

		_checkpointLight = src.ReadValue<float>();
		_lives = src.ReadVariableInt32();
		_coins = src.ReadVariableInt32();
		_coinsCheckpoint = _coins;
		_foodEaten = src.ReadVariableInt32();
		_foodEatenCheckpoint = _foodEaten;
		_score = src.ReadVariableInt32();
		_gems = src.ReadVariableInt32();
		_gemsCheckpoint = _gems;

		levelHandler->SetAmbientLight(this, _checkpointLight);

		std::int32_t weaponCount = src.ReadVariableInt32();
		RETURN_ASSERT_MSG(weaponCount == countof(_weaponAmmoCheckpoint), "Weapon count mismatch");
		_currentWeapon = (WeaponType)src.ReadVariableInt32();
		src.Read(_weaponAmmoCheckpoint, sizeof(_weaponAmmoCheckpoint));
		src.Read(_weaponUpgradesCheckpoint, sizeof(_weaponUpgradesCheckpoint));

		std::memcpy(_weaponAmmo, _weaponAmmoCheckpoint, sizeof(_weaponAmmoCheckpoint));
		std::memcpy(_weaponUpgrades, _weaponUpgradesCheckpoint, sizeof(_weaponUpgradesCheckpoint));

		// Reset current weapon to Blaster if player has no ammo on checkpoint
		if (_weaponAmmo[(std::int32_t)_currentWeapon] == 0) {
			SetCurrentWeapon(WeaponType::Blaster);
		}
	}

	void Player::SerializeResumableToStream(Stream& dest)
	{
		dest.WriteVariableInt32(_playerIndex);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_playerType);
		dest.WriteValue<std::uint8_t>((std::uint8_t)_playerTypeOriginal);
		dest.WriteValue<float>(_checkpointPos.X);
		dest.WriteValue<float>(_checkpointPos.Y);
		dest.WriteValue<float>(_checkpointLight);
		dest.WriteVariableInt32(_lives);
		dest.WriteVariableInt32(_coinsCheckpoint);
		dest.WriteVariableInt32(_foodEatenCheckpoint);
		dest.WriteVariableInt32(_score);
		dest.WriteVariableInt32(_gemsCheckpoint);
		dest.WriteVariableInt32(countof(_weaponAmmoCheckpoint));
		dest.WriteVariableInt32((std::int32_t)_currentWeapon);
		dest.Write(_weaponAmmoCheckpoint, sizeof(_weaponAmmoCheckpoint));
		dest.Write(_weaponUpgradesCheckpoint, sizeof(_weaponUpgradesCheckpoint));
	}

	void Player::WarpToPosition(const Vector2f& pos, WarpFlags flags)
	{
		if ((flags & WarpFlags::Fast) == WarpFlags::Fast) {
			Vector2f posPrev = _pos;
			bool hideTrail = (posPrev - pos).Length() > 250.0f;
			MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
			if (hideTrail) {
				_trailLastPos = _pos;
			}
			_levelHandler->HandlePlayerWarped(this, posPrev, true);
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

			// For warping from the water
			_renderer.setRotation(0.0f);

			PlayPlayerSfx("WarpIn"_s);

			SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpInFreefall : AnimState::TransitionWarpIn, false, true, SpecialMoveType::None, [this, flags, pos]() {
				Vector2f posPrev = _pos;
				MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
				_trailLastPos = _pos;
				PlayPlayerSfx("WarpOut"_s);

				_levelHandler->HandlePlayerWarped(this, posPrev, false);

				_isFreefall |= CanFreefall();
				SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpOutFreefall : AnimState::TransitionWarpOut, false, true, SpecialMoveType::None, [this, flags]() {
					SetState(ActorState::IsInvulnerable, false);
					SetState(ActorState::ApplyGravitation, true);

					if ((flags & WarpFlags::Freeze) == WarpFlags::Freeze) {
						_renderer.AnimPaused = true;
						_controllable = false;
						_controllableTimeout = 100.0f;
						_frozenTimeLeft = 100.0f;
					} else {
						_controllable = true;
						// UpdateAnimation() was probably skipped in this step, because _controllable was false, so call it here
						UpdateAnimation(0.0f);
					}
				});
			});
		}
	}

	void Player::InitialPoleStage(bool horizontal)
	{
		if (_isAttachedToPole || _playerType == PlayerType::Frog) {
			return;
		}
		
		std::int32_t x = (std::int32_t)_pos.X / Tiles::TileSet::DefaultTileSize;
		std::int32_t y = (std::int32_t)_pos.Y / Tiles::TileSet::DefaultTileSize;

		if (_lastPoleTime > 0.0f && _lastPolePos.X == x && _lastPolePos.Y == y) {
			return;
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
		bool positive = (activeForce >= 0.0f);

		float tx = x * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2;
		float ty = y * Tiles::TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2;

		if (_levelHandler->IsReforged()) {
			auto* events = _levelHandler->EventMap();
			std::uint8_t* p;
			if (horizontal) {
				if (events->GetEventByPosition(x, (_pos.Y < ty ? y - 1 : y + 1), &p) == EventType::ModifierHPole) {
					ty = _pos.Y;
				}
			} else {
				if (events->GetEventByPosition((_pos.X < tx ? x - 1 : x + 1), y, &p) == EventType::ModifierVPole) {
					tx = _pos.X;
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

		PlaySfx("Pole"_s, 0.8f, 0.6f);
	}

	void Player::NextPoleStage(bool horizontal, bool positive, int stagesLeft, float lastSpeed)
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

			PlaySfx("Pole"_s, 1.0f, 0.6f);
		} else {
			std::int32_t sign = (positive ? 1 : -1);
			if (horizontal) {
				// To prevent stucking
				for (std::int32_t i = -1; i > -6; i--) {
					if (MoveInstantly(Vector2f(_speed.X, (float)i), MoveType::Relative)) {
						break;
					}
				}

				_speed.X = 10 * sign + lastSpeed * 0.2f;
				_externalForce.X = 10.0f * sign;
				SetFacingLeft(!positive);

				_keepRunningTime = 60.0f;

				SetPlayerTransition(AnimState::Dash | AnimState::Jump, true, true, SpecialMoveType::None);
			} else {
				MoveInstantly(Vector2f(0.0f, sign * 16.0f), MoveType::Relative | MoveType::Force);

				_speed.Y = 4.0f * sign + lastSpeed * 1.4f;
				_externalForce.Y = 1.3f * sign;
			}

			SetState(ActorState::ApplyGravitation, true);
			_isAttachedToPole = false;
			_wasActivelyPushing = false;

			_controllableTimeout = 4.0f;
			_lastPoleTime = 10.0f;

			PlaySfx("HookAttach"_s, 0.8f, 1.2f);
		}
	}

	Player::Modifier Player::GetModifier() const
	{
		return _activeModifier;
	}

	bool Player::SetModifier(Modifier modifier, const std::shared_ptr<ActorBase>& decor)
	{
		if (_activeModifier == modifier) {
			return false;
		}

		if (_activeModifierDecor != nullptr) {
			_activeModifierDecor->DecreaseHealth(INT32_MAX);
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

				_copterFramesLeft = 10.0f * FrameTimer::FramesPerSecond;

				if (_copterSound == nullptr) {
					_copterSound = PlaySfx("Copter"_s, 0.6f, 1.5f);
					if (_copterSound != nullptr) {
						_copterSound->setLooping(true);
					}
				}
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

				_copterFramesLeft = 3.0f * FrameTimer::FramesPerSecond;
				break;
			}

			default: {
				_activeModifier = Modifier::None;

				SetState(ActorState::CanJump | ActorState::ApplyGravitation, true);

				SetAnimation(AnimState::Fall);
				break;
			}
		}

		return true;
	}

	bool Player::TakeDamage(std::int32_t amount, float pushForce)
	{
		if (amount <= 0 || GetState(ActorState::IsInvulnerable) || _levelExiting != LevelExitingState::None) {
			return false;
		}

		// Cancel active climbing and copter
		if (_currentTransition != nullptr && _currentTransition->State == AnimState::TransitionLedgeClimb) {
			ForceCancelTransition();
			MoveInstantly(Vector2f(IsFacingLeft() ? 6.0f : -6.0f, 0.0f), MoveType::Relative | MoveType::Force);
		} else if (_activeModifier == Modifier::Copter || _activeModifier == Modifier::LizardCopter) {
			SetModifier(Modifier::None);
		}

		if (_spawnedBird != nullptr) {
			_spawnedBird->FlyAway();
			_spawnedBird = nullptr;
			// Bird acts as extra life
			_health++;
		}

		DecreaseHealth(amount, nullptr);

		_speed.X = 0.0f;
		_internalForceY = 0.0f;
		_fireFramesLeft = 0.0f;
		_copterFramesLeft = 0.0f;
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

			if (_levelHandler->Difficulty() == GameDifficulty::Multiplayer) {
				SetInvulnerability(80.0f, false);
			} else {
				SetInvulnerability(180.0f, false);
			}

			PlaySfx("Hurt"_s);
		} else {
			_externalForce.X = 0.0f;
			_speed.Y = 0.0f;

			PlayPlayerSfx("Die"_s, 1.3f);
		}

		return true;
	}

	void Player::SetInvulnerability(float time, bool withCircleEffect)
	{
		if (time <= 0.0f) {
			if (_invulnerableTime > 0.0f) {
				SetState(ActorState::IsInvulnerable, false);
				_invulnerableTime = 0.0f;
				_shieldSpawnTime = ShieldDisabled;
				_renderer.setDrawEnabled(true);
			}
			return;
		}

		SetState(ActorState::IsInvulnerable, true);
		_invulnerableTime = time;

		if (withCircleEffect) {
			if (_shieldSpawnTime <= ShieldDisabled) {
				_shieldSpawnTime = 1.0f;
			}
		} else {
			_shieldSpawnTime = ShieldDisabled;
		}
	}

	void Player::EndDamagingMove()
	{
		SetState(ActorState::ApplyGravitation, true);
		SetAnimation(_currentAnimation->State & ~(AnimState::Uppercut | AnimState::Buttstomp));

		if (_currentSpecialMove == SpecialMoveType::Uppercut) {
			if (_suspendType == SuspendType::None) {
				SetTransition(AnimState::TransitionUppercutEnd, false);
			}
			_controllable = true;

			if (_externalForce.Y < 0.0f) {
				_externalForce.Y = 0.0f;
			}
		} else if (_currentSpecialMove == SpecialMoveType::Sidekick) {
			CancelTransition();
			_controllable = true;
			_controllableTimeout = 10;
		}

		_currentSpecialMove = SpecialMoveType::None;
	}

	void Player::AddScore(uint32_t amount)
	{
		_score = std::min(_score + amount, 999999999u);
	}

	bool Player::AddHealth(int amount)
	{
		constexpr int HealthLimit = 5;

		if (_health >= HealthLimit) {
			return false;
		}

		if (amount < 0) {
			_health = std::max(_maxHealth, HealthLimit);
			PlaySfx("PickupMaxCarrot"_s);
		} else {
			_health = std::min(_health + amount, HealthLimit);
			if (_maxHealth < _health) {
				_maxHealth = _health;
			}
			PlaySfx("PickupFood"_s);
		}

		return true;
	}

	bool Player::AddLives(int count)
	{
		constexpr std::int32_t LivesLimit = 99;

		if (_lives >= LivesLimit) {
			return false;
		}

		_lives = std::min(_lives + count, LivesLimit);
		PlaySfx("PickupOneUp"_s);
		return true;
	}

	void Player::AddCoins(int count)
	{
		_coins += count;
		_levelHandler->ShowCoins(this, _coins);
		PlaySfx("PickupCoin"_s);
	}

	void Player::AddGems(int count)
	{
		_gems += count;
		_levelHandler->ShowGems(this, _gems);
		PlaySfx("PickupGem"_s, 1.0f, std::min(0.7f + _gemsPitch * 0.05f, 1.3f));

		_gemsTimer = 120.0f;
		_gemsPitch++;
	}

	void Player::ConsumeFood(bool isDrinkable)
	{
		if (isDrinkable) {
			PlaySfx("PickupDrink"_s);
		} else {
			PlaySfx("PickupFood"_s);
		}

		_foodEaten++;
		if (_foodEaten >= 100) {
			_foodEaten = _foodEaten % 100;
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
		_levelHandler->ActivateSugarRush(this);
	}

	bool Player::AddAmmo(WeaponType weaponType, std::int16_t count)
	{
		constexpr std::int16_t Multiplier = 256;
		constexpr std::int16_t AmmoLimit = 99 * Multiplier;

		if (weaponType >= WeaponType::Count || _weaponAmmo[(std::int32_t)weaponType] < 0 || _weaponAmmo[(std::int32_t)weaponType] >= AmmoLimit) {
			return false;
		}

		bool switchTo = (_weaponAmmo[(std::int32_t)weaponType] == 0);

		_weaponAmmo[(std::int32_t)weaponType] = (int16_t)std::min((int32_t)_weaponAmmo[(std::int32_t)weaponType] + count * Multiplier, (int32_t)AmmoLimit);

		if (switchTo) {
			SetCurrentWeapon(weaponType);

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

		PlaySfx("PickupAmmo"_s);
		return true;
	}

	void Player::AddWeaponUpgrade(WeaponType weaponType, std::uint8_t upgrade)
	{
		_weaponUpgrades[(std::int32_t)weaponType] |= upgrade;
	}

	bool Player::AddFastFire(std::int32_t count)
	{
		const std::int32_t FastFireLimit = 9;

		std::int32_t current = (_weaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		if (current >= FastFireLimit) {
			return false;
		}

		current = std::min(current + count, FastFireLimit);

		_weaponUpgrades[(std::int32_t)WeaponType::Blaster] = (std::uint8_t)((_weaponUpgrades[(std::int32_t)WeaponType::Blaster] & 0x1) | (current << 1));

		PlaySfx("PickupAmmo"_s);

		return true;
	}

	void Player::MorphTo(PlayerType type)
	{
		if (_playerType == type) {
			return;
		}

		PlayerType playerTypePrevious = _playerType;

		_playerType = type;

		// Load new metadata
		switch (type) {
			case PlayerType::Jazz: RequestMetadata("Interactive/PlayerJazz"); break;
			case PlayerType::Spaz: RequestMetadata("Interactive/PlayerSpaz"); break;
			case PlayerType::Lori: RequestMetadata("Interactive/PlayerLori"); break;
			case PlayerType::Frog: RequestMetadata("Interactive/PlayerFrog"); break;
		}

		// Refresh animation state
		if ((_currentSpecialMove == SpecialMoveType::None) ||
			(_currentSpecialMove == SpecialMoveType::Buttstomp && (type == PlayerType::Jazz || type == PlayerType::Spaz || type == PlayerType::Lori))) {
			AnimState prevAnim = _currentAnimation->State;
			_currentAnimation = nullptr;
			if (!SetAnimation(prevAnim)) {
				SetAnimation(AnimState::Idle);
			}
		} else {
			_currentAnimation = nullptr;
			SetAnimation(AnimState::Fall);

			SetState(ActorState::ApplyGravitation, true);
			_controllable = true;

			if (_currentSpecialMove == SpecialMoveType::Uppercut && _externalForce.Y < 0.0f) {
				_externalForce.Y = 0.0f;
			}

			_currentSpecialMove = SpecialMoveType::None;
		}

		// Set transition
		if (type == PlayerType::Frog) {
			PlaySfx("Transform");

			_controllable = false;
			_controllableTimeout = 120.0f;

			switch (playerTypePrevious) {
				case PlayerType::Jazz:
					SetTransition((AnimState)0x60000000, false, [this]() {
						_controllable = true;
						_controllableTimeout = 0.0f;
					});
					break;
				case PlayerType::Spaz:
					SetTransition((AnimState)0x60000001, false, [this]() {
						_controllable = true;
						_controllableTimeout = 0.0f;
					});
					break;
				case PlayerType::Lori:
					SetTransition((AnimState)0x60000002, false, [this]() {
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
	}

	void Player::MorphRevert()
	{
		MorphTo(_playerTypeOriginal);
	}

	bool Player::SetDizzyTime(float time)
	{
		bool wasNotDizzy = (_dizzyTime <= 0.0f);
		_dizzyTime = time;
		return wasNotDizzy;
	}

	bool Player::SetShield(ShieldType shieldType, float time)
	{
		_activeShield = shieldType;
		_activeShieldTime = (shieldType != ShieldType::None ? time : 0.0f);
		return true;
	}

	bool Player::IncreaseShieldTime(float time)
	{
		if (_activeShieldTime <= 0.0f) {
			return false;
		}

		_activeShieldTime += time;
		PlaySfx("PickupGem"_s);
		return true;
	}

	bool Player::SpawnBird(uint8_t type, Vector2f pos)
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
		
		_foodEatenCheckpoint = _foodEaten;
		_coinsCheckpoint = _coins;
		_gemsCheckpoint = _gems;

		std::memcpy(_weaponAmmoCheckpoint, _weaponAmmo, sizeof(_weaponAmmo));
		std::memcpy(_weaponUpgradesCheckpoint, _weaponUpgrades, sizeof(_weaponUpgrades));
	}

	void Player::SetCarryingObject(ActorBase* actor, bool resetSpeed, SuspendType suspendType)
	{
		_carryingObject = actor;

		if (resetSpeed) {
			SetState(ActorState::CanJump, true);
			_speed.Y = 0.0f;
			_externalForce.Y = 0.0f;
			_internalForceY = 0.0f;
		}

		if (suspendType == SuspendType::SwingingVine) {
			_suspendType = suspendType;
			SetState(ActorState::ApplyGravitation, false);
		} else if (_suspendType == SuspendType::SwingingVine) {
			_suspendType = SuspendType::None;
			SetState(ActorState::ApplyGravitation, true);
			_renderer.setRotation(0.0f);
		}
	}
}