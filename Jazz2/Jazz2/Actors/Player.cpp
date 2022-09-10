#include "Player.h"
#include "../ILevelHandler.h"
#include "../Events/EventMap.h"
#include "../Tiles/TileMap.h"
#include "../PreferencesCache.h"
#include "SolidObjectBase.h"
#include "Explosion.h"
#include "PlayerCorpse.h"
#include "Environment/BonusWarp.h"
#include "Environment/Spring.h"
#include "Enemies/EnemyBase.h"
#include "Enemies/TurtleShell.h"

#include "Weapons/BlasterShot.h"
#include "Weapons/BouncerShot.h"
#include "Weapons/FreezerShot.h"
#include "Weapons/RFShot.h"
#include "Weapons/SeekerShot.h"
#include "Weapons/ToasterShot.h"
#include "Weapons/TNT.h"
#include "Weapons/Thunderbolt.h"

#include "../../nCine/Base/Random.h"
#include "../../nCine/Base/FrameTimer.h"

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
		_wasUpPressed(false), _wasDownPressed(false), _wasJumpPressed(false), _wasFirePressed(false),
		_currentSpecialMove(SpecialMoveType::None),
		_isAttachedToPole(false),
		_copterFramesLeft(0.0f), _fireFramesLeft(0.0f), _pushFramesLeft(0.0f), _waterCooldownLeft(0.0f),
		_levelExiting(LevelExitingState::None),
		_isFreefall(false), _inWater(false), _isLifting(false), _isSpring(false),
		_inShallowWater(-1),
		_activeModifier(Modifier::None),
		_springCooldown(0.0f),
		_inIdleTransition(false), _inLedgeTransition(false),
		_canDoubleJump(true),
		_lives(0), _coins(0), _foodEaten(0), _score(0),
		_checkpointLight(1.0f),
		_sugarRushLeft(0.0f), _sugarRushStarsTime(0.0f),
		_gems(0), _gemsPitch(0),
		_gemsTimer(0.0f),
		_bonusWarpTimer(0.0f),
		_suspendType(SuspendType::None),
		_suspendTime(0.0f),
		_invulnerableTime(0.0f),
		_invulnerableBlinkTime(0.0f),
		_jumpTime(0.0f),
		_idleTime(0.0f),
		_keepRunningTime(0.0f),
		_lastPoleTime(0.0f),
		_inTubeTime(0.0f),
		_dizzyTime(0.0f),
		_weaponAllowed(true)
	{
	}

	Player::~Player()
	{
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
			case PlayerType::Jazz:
				co_await RequestMetadataAsync("Interactive/PlayerJazz"_s);
				break;
			case PlayerType::Spaz:
				co_await RequestMetadataAsync("Interactive/PlayerSpaz"_s);
				break;
			case PlayerType::Lori:
				co_await RequestMetadataAsync("Interactive/PlayerLori"_s);
				break;
			case PlayerType::Frog:
				co_await RequestMetadataAsync("Interactive/PlayerFrog"_s);
				break;
		}

		SetAnimation(AnimState::Fall);

		std::memset(_weaponAmmo, 0, sizeof(_weaponAmmo));
		std::memset(_weaponUpgrades, 0, sizeof(_weaponUpgrades));

		_weaponAmmo[(int)WeaponType::Blaster] = UINT16_MAX;

		SetState(ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjects | ActorState::IsSolidObject, true);

		_health = 5;
		_maxHealth = _health;
		_currentWeapon = WeaponType::Blaster;

		_checkpointPos = Vector2f((float)details.Pos.X, (float)details.Pos.Y);
		_checkpointLight = _levelHandler->GetAmbientLight();

		co_return true;
	}

	bool Player::CanBreakSolidObjects() const
	{
		return (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f);
	}

	bool Player::CanMoveVertically() const
	{
		return (_inWater || _activeModifier != Modifier::None);
	}

	bool Player::OnTileDeactivate(int tx1, int ty1, int tx2, int ty2)
	{
		// Player can never be deactivated
		return false;
	}

	void Player::OnUpdate(float timeMult)
	{
#if _DEBUG
		if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::SwitchWeapon)) {
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

			if (timeMult * (std::abs(_speed.X + _externalForce.X) + std::abs(_speed.Y - _externalForce.Y)) > 20.0f) {
				TryStandardMovement(timeMult * 0.5f, params);
				TryStandardMovement(timeMult * 0.5f, params);
			} else {
				TryStandardMovement(timeMult, params);
			}

			if (params.TilesDestroyed > 0) {
				AddScore(params.TilesDestroyed * 50);
			}

			OnUpdateHitbox();

			if (_renderer.AnimPaused) {
				if (_frozenTimeLeft <= 0.0f) {
					_renderer.AnimPaused = false;
				} else {
					_frozenTimeLeft -= timeMult;
				}
			}
		}

		//FollowCarryingPlatform();
		UpdateAnimation(timeMult);
		CheckSuspendState(timeMult);
		CheckEndOfSpecialMoves(timeMult);

		OnHandleWater();

		bool areaWeaponAllowed = true;
		int areaWaterBlock = -1;
		OnHandleAreaEvents(timeMult, areaWeaponAllowed, areaWaterBlock);

		// Invulnerability
		if (_invulnerableTime > 0.0f) {
			_invulnerableTime -= timeMult;

			if (_invulnerableTime <= 0.0f) {
				SetState(ActorState::IsInvulnerable, false);
				_renderer.setDrawEnabled(true);

#if !SERVER
				/*if (currentCircleEffectRenderer != null) {
					SetCircleEffect(false);
				}*/
#endif

#if MULTIPLAYER && SERVER
				((LevelHandler)levelHandler).OnPlayerSetInvulnerability(this, 0f, false);
#endif
			}
#if !SERVER
			else if (_currentTransitionState != AnimState::Hurt /*&& _currentCircleEffectRenderer == nullptr*/) {
				if (_invulnerableBlinkTime > 0.0f) {
					_invulnerableBlinkTime -= timeMult;
				} else {
					_renderer.setDrawEnabled(!_renderer.isDrawEnabled());

					_invulnerableBlinkTime = 3.0f;
				}
			}
#endif
			else {
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
				if ((_currentAnimationState & (AnimState::Walk | AnimState::Run | AnimState::Dash | AnimState::Crouch | AnimState::Buttstomp | AnimState::Swim | AnimState::Airboard | AnimState::Lift | AnimState::Spring)) == AnimState::Idle &&
					_currentTransitionState != AnimState::TransitionRunToIdle &&
					_currentTransitionState != AnimState::TransitionDashToIdle &&
					!_isAttachedToPole) {

					if ((_currentAnimationState & AnimState::Hook) == AnimState::Hook) {
						SetTransition(AnimState::TransitionHookShootToHook, false);
					} else if ((_currentAnimationState & AnimState::Copter) == AnimState::Copter) {
						SetAnimation(AnimState::Copter);
						SetTransition(AnimState::TransitionCopterShootToCopter, false);
					} else if ((_currentAnimationState & AnimState::Fall) == AnimState::Fall) {
						SetTransition(AnimState::TransitionFallShootToFall, false);
					} else {
						SetTransition(AnimState::TransitionShootToIdle, false);
					}
				}
			}
		}

		// Shield
		/*if (_shieldTime > 0.0f) {
			_shieldTime -= timeMult;

			if (_shieldTime <= 0.0f) {
				SetShield(ShieldType::None, 0.0f);
			}
		}*/

		// Dizziness
		if (_dizzyTime > 0.0f) {
			_dizzyTime -= timeMult;
		}

		// Sugar Rush
		if (_sugarRushLeft > 0.0f) {
			_sugarRushLeft -= timeMult;

			if (_sugarRushLeft > 0.0f) {
				if (_sugarRushStarsTime > 0.0f) {
					_sugarRushStarsTime -= timeMult;
				} else {
					_sugarRushStarsTime = Random().FastFloat(2.0f, 8.0f);

					auto tilemap = _levelHandler->TileMap();
					if (tilemap != nullptr) {
						auto it = _metadata->Graphics.find(String::nullTerminatedView("SugarRush"_s));
						if (it != _metadata->Graphics.end()) {
							Vector2i texSize = it->second.Base->TextureDiffuse->size();
							Vector2i size = it->second.Base->FrameDimensions;
							Vector2i frameConf = it->second.Base->FrameConfiguration;
							int frame = it->second.FrameOffset + Random().Next(0, it->second.FrameCount);
							float speedX = Random().FastFloat(-4.0f, 4.0f);

							Tiles::TileMap::DestructibleDebris debris = { };
							debris.Pos = _pos;
							debris.Depth = _renderer.layer() - 2;
							debris.Size = Vector2f(size.X, size.Y);
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

							debris.DiffuseTexture = it->second.Base->TextureDiffuse.get();

							tilemap->CreateDebris(debris);
						}
					}
				}
			} else {
				_renderer.Initialize(ActorRendererType::Default);
			}
		}

		// Copter
		if (_activeModifier != Modifier::None) {
			if (_activeModifier == Modifier::Copter || _activeModifier == Modifier::LizardCopter) {
				_copterFramesLeft -= timeMult;
				if (_copterFramesLeft <= 0.0f) {
					SetModifier(Modifier::None);
				}
			}
		}

		if (_copterSound != nullptr) {
			if ((_currentAnimationState & AnimState::Copter) == AnimState::Copter) {
				_copterSound->setPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			} else {
				_copterSound->stop();
				_copterSound = nullptr;
			}
		}

		// Shallow Water
		if (areaWaterBlock != -1) {
			if (_inShallowWater == -1) {
				Explosion::Create(_levelHandler, Vector3i((int)_pos.X, areaWaterBlock, _renderer.layer() + 2), Explosion::Type::WaterSplash);
				_levelHandler->PlayCommonSfx("WaterSplash"_s, Vector3f(_pos.X, areaWaterBlock, 0.0f), 0.7f, 0.5f);
			}

			_inShallowWater = areaWaterBlock;
		} else if (_inShallowWater != -1) {
			Explosion::Create(_levelHandler, Vector3i((int)_pos.X, _inShallowWater, _renderer.layer() + 2), Explosion::Type::WaterSplash);
			_levelHandler->PlayCommonSfx("WaterSplash"_s, Vector3f(_pos.X, _inShallowWater, 0.0f), 1.0f, 0.5f);

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
			if (canWalk && playerMovementVelocity > 0.5f) {
				SetAnimation(_currentAnimationState & ~(AnimState::Lookup | AnimState::Crouch));

				if (_dizzyTime > 0.0f) {
					SetFacingLeft(playerMovement > 0.0f);
				} else {
					SetFacingLeft(playerMovement < 0.0f);
				}

				_isActivelyPushing = _wasActivelyPushing = true;

				if (_dizzyTime > 0.0f || _playerType == PlayerType::Frog) {
					_speed.X = std::clamp(_speed.X + Acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxDizzySpeed * playerMovementVelocity, MaxDizzySpeed * playerMovementVelocity);
				} else if (_inShallowWater != -1 && _levelHandler->ReduxMode() && _playerType != PlayerType::Lori) {
					// Use lower speed in shallow water if Redux Mode is enabled
					// Also, exclude Lori, because she can't ledge climb or double jump (rescue/01_colon1)
					_speed.X = std::clamp(_speed.X + Acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxShallowWaterSpeed * playerMovementVelocity, MaxShallowWaterSpeed * playerMovementVelocity);
				} else {
					if (_suspendType == SuspendType::None && !_inWater && _levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Run)) {
						_speed.X = std::clamp(_speed.X + Acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxDashingSpeed * playerMovementVelocity, MaxDashingSpeed * playerMovementVelocity);
					} else if (_suspendType == SuspendType::Vine) {
						if (_wasFirePressed) {
							_speed.X = 0.0f;
						} else {
							// If Run is pressed, player moves faster on vines
							if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Run)) {
								playerMovementVelocity *= 1.6f;
							}
							_speed.X = std::clamp(_speed.X + Acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxVineSpeed * playerMovementVelocity, MaxVineSpeed * playerMovementVelocity);
						}
					} else if (_suspendType != SuspendType::Hook) {
						_speed.X = std::clamp(_speed.X + Acceleration * timeMult * (IsFacingLeft() ? -1 : 1), -MaxRunningSpeed * playerMovementVelocity, MaxRunningSpeed * playerMovementVelocity);
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

		if (!_controllable || !_controllableExternal) {
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

						SetAnimation(AnimState::Lookup | (_currentAnimationState & AnimState::Hook));
					}
				}
			} else if (_wasUpPressed) {
				_wasUpPressed = false;

				SetAnimation(_currentAnimationState & ~AnimState::Lookup);
			}

			// Crouch
			if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Down)) {
				if (_suspendType == SuspendType::SwingingVine) {
					// TODO
				} else if (_suspendType != SuspendType::None) {
					_wasDownPressed = true;

					MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative | MoveType::Force);
					_suspendType = SuspendType::None;
					_suspendTime = 4.0f;

					SetState(ActorState::ApplyGravitation, true);
				} else if (!_wasDownPressed && _dizzyTime <= 0.0f) {
					if (GetState(ActorState::CanJump)) {
						if (!_isLifting && std::abs(_speed.X) < std::numeric_limits<float>::epsilon()) {
							_wasDownPressed = true;

							SetAnimation(AnimState::Crouch);
						}
					} else if (_playerType != PlayerType::Frog) {
						_wasDownPressed = true;

						_controllable = false;
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

				SetAnimation(_currentAnimationState & ~AnimState::Crouch);
			}

			// Jump
			if (_levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Jump)) {
				if (!_wasJumpPressed) {
					_wasJumpPressed = true;

					if (_suspendType == SuspendType::None && _jumpTime <= 0.0f) {
						if (_isLifting && GetState(ActorState::CanJump) && _currentSpecialMove == SpecialMoveType::None) {
							SetState(ActorState::CanJump, false);
							SetAnimation(_currentAnimationState & (~AnimState::Lookup & ~AnimState::Crouch));
							PlaySfx("Jump"_s);
							//_carryingObject = null;

							SetState(ActorState::IsSolidObject | ActorState::CollideWithSolidObjects, false);

							_isLifting = false;
							_controllable = false;
							_jumpTime = 12.0f;

							_speed.Y = -3.0f;
							_internalForceY = 0.88f;

							SetTransition(AnimState::TransitionLiftEnd, false, [this]() {
								_controllable = true;
								SetState(ActorState::CollideWithSolidObjects, true);
							});
						} else {
							switch (_playerType) {
								case PlayerType::Jazz: {
									if ((_currentAnimationState & AnimState::Crouch) == AnimState::Crouch) {
										_controllable = false;
										SetAnimation(AnimState::Uppercut);
										SetPlayerTransition(AnimState::TransitionUppercutA, true, true, SpecialMoveType::Uppercut, [this]() {
											_externalForce.Y = 1.4f;
											_speed.Y = -2.0f;
											SetState(ActorState::CanJump, false);
											SetPlayerTransition(AnimState::TransitionUppercutB, true, true, SpecialMoveType::Uppercut);
										});
									} else {
										if (_speed.Y > 0.01f && !GetState(ActorState::CanJump) && (_currentAnimationState & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
											SetState(ActorState::ApplyGravitation, false);
											_speed.Y = 1.5f;
											if ((_currentAnimationState & AnimState::Copter) != AnimState::Copter) {
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
									if ((_currentAnimationState & AnimState::Crouch) == AnimState::Crouch) {
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

											_internalForceY = 1.15f;
											_speed.Y = -0.6f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f);
											_speed.X *= 0.4f;

											PlaySfx("DoubleJump"_s);

											SetTransition(AnimState::Spring, false);
										}
									}
									break;
								}
								case PlayerType::Lori: {
									if ((_currentAnimationState & AnimState::Crouch) == AnimState::Crouch) {
										_controllable = false;
										SetAnimation(AnimState::Uppercut);
										SetPlayerTransition(AnimState::TransitionUppercutA, true, true, SpecialMoveType::Sidekick, [this]() {
											_externalForce.X = 15.0f * (IsFacingLeft() ? -1.0f : 1.0f);
											_speed.X = 6.0f * (IsFacingLeft() ? -1.0f : 1.0f);
											SetState(ActorState::ApplyGravitation, false);
										});
									} else {
										if (_speed.Y > 0.01f && !GetState(ActorState::CanJump) && (_currentAnimationState & (AnimState::Fall | AnimState::Copter)) != AnimState::Idle) {
											SetState(ActorState::ApplyGravitation, false);
											_speed.Y = 1.5f;
											if ((_currentAnimationState & AnimState::Copter) != AnimState::Copter) {
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
							_suspendType = SuspendType::None;
							//_currentVine = nullptr;
							SetState(ActorState::ApplyGravitation, true);
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
						SetAnimation(_currentAnimationState & (~AnimState::Lookup & ~AnimState::Crouch));
						if (_jumpTime <= 0.0f) {
							PlaySfx("Jump"_s);
						}
						_jumpTime = 12.0f;
						//_carryingObject = nullptr;

						// Gravitation is sometimes off because of active copter, turn it on again
						SetState(ActorState::ApplyGravitation, true);

						SetState(ActorState::IsSolidObject, false);

						_internalForceY = 1.02f + (1.0f - timeMult) * 0.07f;
						_speed.Y = -3.55f - std::max(0.0f, (std::abs(_speed.X) - 4.0f) * 0.3f);
					}
				}
			} else {
				if (!_wasJumpPressed) {
					if (_internalForceY > 0.0f) {
						_internalForceY = 0.0f;
					}
				} else {
					_wasJumpPressed = false;
				}
			}
		}

		// Fire
		bool weaponInUse = false;
		if (_weaponAllowed && areaWeaponAllowed && _levelHandler->PlayerActionPressed(_playerIndex, PlayerActions::Fire)) {
			if (!_isLifting && _suspendType != SuspendType::SwingingVine && (_currentAnimationState & AnimState::Push) != AnimState::Push && _pushFramesLeft <= 0.0f) {
				if (_playerType == PlayerType::Frog) {
					if (_currentTransitionState == AnimState::Idle && std::abs(_speed.X) < 0.1f && std::abs(_speed.Y) < 0.1f && std::abs(_externalForce.X) < 0.1f && std::abs(_externalForce.Y) < 0.1f) {
						PlaySfx("Tongue"_s, 0.8f);

						_controllable = false;
						_controllableTimeout = 120.0f;

						SetTransition(_currentAnimationState | AnimState::Shoot, false, [this]() {
							_controllable = true;
							_controllableTimeout = 0.0f;
						});
					}
				} else if (_weaponAmmo[(int)_currentWeapon] != 0) {
					_wasFirePressed = true;

					bool weaponCooledDown = (_weaponCooldown <= 0.0f);
					weaponInUse = FireCurrentWeapon(_currentWeapon);
					if (weaponInUse) {
						if (_currentTransitionState == AnimState::Spring || _currentTransitionState == AnimState::TransitionShootToIdle) {
							ForceCancelTransition();
						}

						SetAnimation(_currentAnimationState | AnimState::Shoot);
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
			if (_levelHandler->PlayerActionHit(_playerIndex, PlayerActions::SwitchWeapon, true, isGamepad)) {
				if (!isGamepad || !PreferencesCache::EnableWeaponWheel) {
					SwitchToNextWeapon();
				}
			}
		}
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
	}

	bool Player::OnPerish(ActorBase* collider)
	{
		if (_currentTransitionState == AnimState::TransitionDeath) {
			return false;
		}

		SetState(ActorState::IsInvulnerable, true);

		ForceCancelTransition();

		if (_playerType == PlayerType::Frog) {
			_playerType = _playerTypeOriginal;

			// Load original metadata
			switch (_playerType) {
				case PlayerType::Jazz:
					RequestMetadata("Interactive/PlayerJazz"_s);
					break;
				case PlayerType::Spaz:
					RequestMetadata("Interactive/PlayerSpaz"_s);
					break;
				case PlayerType::Lori:
					RequestMetadata("Interactive/PlayerLori"_s);
					break;
				case PlayerType::Frog:
					RequestMetadata("Interactive/PlayerFrog"_s);
					break;
			}

			// Refresh animation state
			_currentSpecialMove = SpecialMoveType::None;
			_currentAnimation = nullptr;
			SetAnimation(_currentAnimationState);

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
		AABBInner = AABBf(_pos.X - 11.0f, _pos.Y + 8.0f - 18.0f, _pos.X + 11.0f, _pos.Y + 8.0f + 12.0f);
	}

	bool Player::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		bool handled = false;
		bool removeSpecialMove = false;
		if (auto turtleShell = dynamic_cast<Enemies::TurtleShell*>(other.get())) {
			if (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f) {
				other->DecreaseHealth(INT32_MAX, this);

				if ((_currentAnimationState & AnimState::Buttstomp) == AnimState::Buttstomp) {
					removeSpecialMove = true;
					_speed.Y *= -0.6f;
					SetState(ActorState::CanJump, true);
				}
				return true;
			}
		} else if (auto enemy = dynamic_cast<Enemies::EnemyBase*>(other.get())) {
			if (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f /*|| _shieldTime > 0.0f*/) {
				if (!enemy->IsInvulnerable()) {
					enemy->DecreaseHealth(4, this);
					handled = true;

					Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 2), Explosion::Type::Small);

					if (_sugarRushLeft > 0.0f) {
						if (GetState(ActorState::CanJump)) {
							_speed.Y = 3;
							SetState(ActorState::CanJump, false);
							_externalForce.Y = 0.6f;
						}
						_speed.Y *= -0.5f;
					}
					if ((_currentAnimationState & AnimState::Buttstomp) == AnimState::Buttstomp) {
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
				/*if (_shieldTime > 0.0f) {
					_shieldTime = std::max(1.0f, _shieldTime - 5.0f * FrameTimer::FramesPerSecond);
				}*/
			} else if (enemy->CanHurtPlayer()) {
				TakeDamage(1, 4 * (_pos.X > enemy->GetPos().X ? 1 : -1));
			}
		} else if (auto spring = dynamic_cast<Environment::Spring*>(other.get())) {
			// Collide only with hitbox
			if (_controllableExternal && _currentTransitionState != AnimState::TransitionLedgeClimb && _springCooldown <= 0.0f && spring->AABBInner.Overlaps(AABBInner)) {
				Vector2 force = spring->Activate();
				int sign = ((force.X + force.Y) > std::numeric_limits<float>::epsilon() ? 1 : -1);
				if (std::abs(force.X) > 0.0f) {
					removeSpecialMove = true;
					_copterFramesLeft = 0.0f;
					//speedX = force.X;
					_speed.X = (1 + std::abs(force.X)) * sign;
					_externalForce.X = force.X * 0.6f;
					_springCooldown = 3.0f;
					SetState(ActorState::CanJump, false);

					_wasActivelyPushing = false;

					_keepRunningTime = 100.0f;

					if (!spring->KeepSpeedY) {
						_speed.Y = 0.0f;
						_externalForce.Y = 0.0f;
					}

					SetPlayerTransition(AnimState::Dash | AnimState::Jump, true, false, SpecialMoveType::None);
					_controllableTimeout = 2.0f;
				} else if (std::abs(force.Y) > 0.0f) {
					_copterFramesLeft = 0.0f;
					_speed.Y = (4 + std::abs(force.Y)) * sign;
					_externalForce.Y = -force.Y;
					_springCooldown = 3.0f;
					SetState(ActorState::CanJump, false);

					if (!spring->KeepSpeedX) {
						_speed.X = 0.0f;
						_externalForce.X = 0.0f;
						_keepRunningTime = 0.0f;
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

			handled = true;
		} else if (auto bonusWarp = dynamic_cast<Environment::BonusWarp*>(other.get())) {
			if (_currentTransitionState == AnimState::Idle || _currentTransitionCancellable) {
				auto cost = bonusWarp->GetCost();
				if (cost <= _coins) {
					_coins -= cost;
					bonusWarp->Activate(this);

					// Convert remaing coins to gems
					_gems += _coins;
					_coins = 0;
				} else if (_bonusWarpTimer <= 0.0f) {
					_levelHandler->ShowCoins(_coins);
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
		if (_levelHandler->EventMap()->IsHurting(_pos.X, _pos.Y + 24)) {
			TakeDamage(1, _speed.X * 0.25f);
		} else if (!_inWater && _activeModifier == Modifier::None) {
			if (!GetState(ActorState::CanJump)) {
				PlaySfx("Land"_s, 0.8f);

				if (Random().NextFloat() < 0.6f) {
					Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y + 20.0f, _renderer.layer() - 2), Explosion::Type::TinyDark);
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
		if (_levelHandler->EventMap()->IsHurting(_pos.X, _pos.Y - 4.0f)) {
			TakeDamage(1, _speed.X * 0.25f);
		}
	}

	void Player::OnHitWall(float timeMult)
	{
		// Reset speed and show Push animation
		_speed.X = 0.0f;
		_pushFramesLeft = 2.0f;
		_keepRunningTime = 0.0f;

		if (_levelHandler->EventMap()->IsHurting(_pos.X + (_speed.X > 0.0f ? 1.0f : -1.0f) * 16.0f, _pos.Y)) {
			TakeDamage(1, _speed.X * 0.25f);
		} else {

			if (PreferencesCache::EnableLedgeClimb && _isActivelyPushing && _suspendType == SuspendType::None && _activeModifier == Modifier::None && !GetState(ActorState::CanJump) &&
				_currentSpecialMove == SpecialMoveType::None && _currentTransitionState != AnimState::TransitionUppercutEnd &&
				_speed.Y >= -1.0f && _externalForce.Y <= 0.0f && _copterFramesLeft <= 0.0f && _keepRunningTime <= 0.0f) {

				// Character supports ledge climbing
				AnimationCandidate candidates[AnimationCandidatesCount];
				int count = FindAnimationCandidates(AnimState::TransitionLedgeClimb, candidates);
				bool canClimb = (count > 0);

				if (canClimb) {
					const int MaxTolerancePixels = 6;

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
								_speed.Y += (1.0f - timeMult) * 0.2f;
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

	void Player::UpdateAnimation(float timeMult)
	{
		if (!_controllable) {
			return;
		}

		float posX = _pos.X;

		AnimState oldState = _currentAnimationState;
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
		} else {
			// Only certain ones don't need to be preserved from earlier state, others should be set as expected
			AnimState composite = (_currentAnimationState & (AnimState)0xFFF83F60);

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
					CancelTransition();
				}
			}

			if (_fireFramesLeft > 0.0f) {
				composite |= AnimState::Shoot;
			}

			if (_suspendType != SuspendType::None) {
				composite |= AnimState::Hook;
			} else {
				if (GetState(ActorState::CanJump)) {
					// Grounded, no vertical speed
					if (_dizzyTime > 0.0f) {
						composite |= AnimState::Dizzy;
					}
				} else if (_speed.Y < -std::numeric_limits<float>::epsilon()) {
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

				if (_currentTransitionState == AnimState::Idle) {
					SetPlayerTransition(AnimState::TransitionIdleBored, true, false, SpecialMoveType::None);
				}
			} else {
				_idleTime += timeMult;
			}
		} else {
			_idleTime = 0.0f;
		}

		SetAnimation(newState);

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
						_inIdleTransition = false;
						SetTransition(AnimState::TransitionRunToIdle, true);
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
				} else if (!_inLedgeTransition) {
					AABBf aabbL = AABBf(AABBInner.L + 2, AABBInner.B - 10, AABBInner.L + 4, AABBInner.B + 28);
					AABBf aabbR = AABBf(AABBInner.R - 4, AABBInner.B - 10, AABBInner.R - 2, AABBInner.B + 28);
					TileCollisionParams params = { TileDestructType::None, true };
					if (IsFacingLeft()
						? (_levelHandler->IsPositionEmpty(this, aabbL, params) && !_levelHandler->IsPositionEmpty(this, aabbR, params))
						: (!_levelHandler->IsPositionEmpty(this, aabbL, params) && _levelHandler->IsPositionEmpty(this, aabbR, params))) {

						_inLedgeTransition = true;
						// TODO: Spaz's and Lori's animation should be continual
						SetTransition(AnimState::TransitionLedge, true);

						PlaySfx("Ledge"_s);
					}
				} else if (newState != AnimState::Idle) {
					_inLedgeTransition = false;

					if (_currentTransitionState == AnimState::TransitionLedge) {
						CancelTransition();
					}
				}
				break;
		}
	}

	void Player::PushSolidObjects(float timeMult)
	{
		if (_pushFramesLeft > 0.0f) {
			_pushFramesLeft -= timeMult;
		}

		if (GetState(ActorState::CanJump) && _controllable && _controllableExternal && _isActivelyPushing && std::abs(_speed.X) > std::numeric_limits<float>::epsilon()) {
			AABBf hitbox = AABBInner + Vector2f(_speed.X < 0.0f ? -2.0f : 2.0f, 0.0f);
			TileCollisionParams params = { TileDestructType::None, false };
			ActorBase* collider;
			if (!_levelHandler->IsPositionEmpty(this, hitbox, params, &collider)) {
				if (auto solidObject = dynamic_cast<SolidObjectBase*>(collider)) {
					SetState(ActorState::IsSolidObject, false);
					if (solidObject->Push(_speed.X < 0, timeMult)) {
						_pushFramesLeft = 3.0f;
					}
					SetState(ActorState::IsSolidObject, true);
				}
			}
		} else if (GetState(ActorState::IsSolidObject)) {
			AABBf aabb = AABBInner + Vector2f(0.0f, -2.0f);
			TileCollisionParams params = { TileDestructType::None, false };
			ActorBase* collider;
			if (!_levelHandler->IsPositionEmpty(this, aabb, params, &collider)) {
				if (auto solidObject = dynamic_cast<SolidObjectBase*>(collider)) {
					if (AABBInner.T >= solidObject->AABBInner.T && !_isLifting) {
						_isLifting = true;

						SetTransition(AnimState::TransitionLiftStart, true);
					}
				} else {
					_isLifting = false;
				}
			} else {
				_isLifting = false;
			}
		} else {
			_isLifting = false;
		}
	}

	void Player::CheckEndOfSpecialMoves(float timeMult)
	{
		// Buttstomp
		if (_currentSpecialMove == SpecialMoveType::Buttstomp && (GetState(ActorState::CanJump) || _suspendType != SuspendType::None)) {
			EndDamagingMove();
			if (_suspendType == SuspendType::None && !_isSpring) {
				int tx = (int)_pos.X / 32;
				int ty = ((int)_pos.Y + 24) / 32;

				uint8_t* eventParams;
				if (_levelHandler->EventMap()->GetEventByPosition(tx, ty, &eventParams) == EventType::GemStomp) {
					_levelHandler->EventMap()->StoreTileEvent(tx, ty, EventType::Empty);

					for (int i = 0; i < 8; i++) {
						float fx = Random().NextFloat(-16.0f, 16.0f);
						float fy = Random().NextFloat(-12.0f, 0.2f);

						uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] = { };
						std::shared_ptr<ActorBase> actor = _levelHandler->EventSpawner()->SpawnEvent(EventType::Gem, spawnParams, ActorState::None, Vector3i((int)(_pos.X + fx * 2.0f), (int)(_pos.Y + fy * 4.0f), _renderer.layer() - 10));
						if (actor != nullptr) {
							actor->AddExternalForce(fx, fy);
							_levelHandler->AddActor(actor);
						}
					}
				}

				SetTransition(AnimState::TransitionButtstompEnd, false, [this]() {
					_controllable = true;
				});
			} else {
				_controllable = true;
			}
		}

		// Uppercut
		if (_currentSpecialMove == SpecialMoveType::Uppercut && _currentTransitionState == AnimState::Idle && ((_currentAnimationState & AnimState::Uppercut) == AnimState::Uppercut) && _speed.Y > -2) {
			EndDamagingMove();
		}

		// Sidekick
		if (_currentSpecialMove == SpecialMoveType::Sidekick && _currentTransitionState == AnimState::Idle && std::abs(_speed.X) < 0.01f) {
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
			if ((_currentAnimationState & AnimState::Copter) == AnimState::Copter) {
				cancelCopter = (GetState(ActorState::CanJump) || _suspendType != SuspendType::None || _copterFramesLeft <= 0.0f);

				_copterFramesLeft -= timeMult;
			} else {
				cancelCopter = ((_currentAnimationState & AnimState::Fall) == AnimState::Fall && _copterFramesLeft > 0.0f);
			}

			if (cancelCopter) {
				_copterFramesLeft = 0.0f;
				SetAnimation(_currentAnimationState & ~AnimState::Copter);
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

		AnimState currentState = _currentAnimationState;

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
					_speed.X = _externalForce.X = 0.0f;
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
				if (_currentTransitionState == AnimState::Idle) {
					_renderer.AnimDuration = std::max(_currentAnimation->FrameDuration + 1.0f - Vector2f(_speed.X, _speed.Y).Length() * 0.26f, 0.4f);
				}

			} else if (_waterCooldownLeft <= 0.0f) {
				_inWater = false;
				_waterCooldownLeft = 20.0f;

				SetState(ActorState::ApplyGravitation | ActorState::CanJump, true);
				_externalForce.Y = 0.45f;
				_renderer.setRotation(0.0f);

				SetAnimation(AnimState::Jump);

				float y = _levelHandler->WaterLevel();
				Explosion::Create(_levelHandler, Vector3i((int)_pos.X, y, _renderer.layer() + 2), Explosion::Type::WaterSplash);
				_levelHandler->PlayCommonSfx("WaterSplash"_s, Vector3f(_pos.X, y, 0.0f), 1.0f, 0.5f);
			}
		} else {
			if (_pos.Y >= _levelHandler->WaterLevel() && _waterCooldownLeft <= 0.0f) {
				_inWater = true;
				_waterCooldownLeft = 20.0f;

				_controllable = true;
				EndDamagingMove();

				float y = _levelHandler->WaterLevel();
				Explosion::Create(_levelHandler, Vector3i((int)_pos.X, y, _renderer.layer() + 2), Explosion::Type::WaterSplash);
				_levelHandler->PlayCommonSfx("WaterSplash"_s, Vector3f(_pos.X, y, 0.0f), 0.7f, 0.5f);
			}

			// Adjust walking animation speed
			if (_currentAnimationState == AnimState::Walk && _currentTransitionState == AnimState::Idle) {
				_renderer.AnimDuration = _currentAnimation->FrameDuration * (1.6f - 0.6f * std::min(std::abs(_speed.X), MaxRunningSpeed) / MaxRunningSpeed);
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
				_levelHandler->SetAmbientLight(p[0] / 255.0f);
				break;
			}
			case EventType::WarpOrigin: { // Warp ID, Fast, Set Lap
				if (_currentTransitionState == AnimState::Idle || _currentTransitionState == (AnimState::Dash | AnimState::Jump) || _currentTransitionCancellable) {
					Vector2f c = events->GetWarpTarget(p[0]);
					if (c.X >= 0.0f && c.Y >= 0.0f) {
						WarpToPosition(c, p[1] != 0);

#if MULTIPLAYER && SERVER
						if (p[2] != 0) {
							((LevelHandler)levelHandler).OnPlayerIncrementLaps(this);
						}
#endif
					}
				}
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
			case EventType::ModifierTube: { // XSpeed, YSpeed, Wait Time, Trig Sample, Become Noclip, Noclip Only
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

						String nextLevel;
						if (p[4] != 0) {
							nextLevel = _levelHandler->GetLevelText(p[2], p[3], '|');
						}
						_levelHandler->BeginLevelChange((ExitType)p[0], nextLevel);
					} else if (_bonusWarpTimer <= 0.0f) {
						_levelHandler->ShowCoins(_coins);
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
				// TODO: Call function #{p[0]}(sender, p[1]); implement level extensions
				if (p[2] != 0) {
					events->StoreTileEvent((int)(_pos.X / 32), (int)(_pos.Y / 32), EventType::Empty);
				}
				break;
			}
			case EventType::AreaActivateBoss: { // Music
				_levelHandler->BroadcastTriggeredEvent(tileEvent, p);

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
					MorphRevent();
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
			case EventType::TriggerZone: { // Trigger ID, Turn On, Switch
				auto tiles = _levelHandler->TileMap();
				if (tiles != nullptr) {
					// TODO: Implement Switch parameter
					tiles->SetTrigger(p[0], p[1] != 0);
				}
				break;
			}

			case EventType::ModifierDeath: {
				DecreaseHealth(INT32_MAX);
				break;
			}
			case EventType::ModifierSetWater: { // Height, Instant, Lighting
				// TODO: Implement Instant (non-instant transition), Lighting
				_levelHandler->SetWaterLevel(*(uint16_t*)&p[0]);
				break;
			}
			case EventType::ModifierLimitCameraView: { // Left, Width
				uint16_t left = *(uint16_t*)&p[0];
				uint16_t width = *(uint16_t*)&p[2];
				_levelHandler->LimitCameraView((left == 0 ? (int)(_pos.X / Tiles::TileSet::DefaultTileSize) : left) * Tiles::TileSet::DefaultTileSize, width * Tiles::TileSet::DefaultTileSize);
				break;
			}

			case EventType::RollingRockTrigger: { // Rock ID
				//_levelHandler->BroadcastTriggeredEvent(tileEvent, p);
				break;
			}

			case EventType::AreaWaterBlock: {
				areaWaterBlock = ((int)_pos.Y / 32) * 32 + p[0];
				break;
			}
		}

		// TODO: Implement Slide modifier with JJ2+ parameter

		// Check floating from each corner of an extended hitbox
		// Player should not pass from a single tile wide gap if the columns left or right have
		// float events, so checking for a wider box is necessary.
		const float ExtendedHitbox = 2.0f;

		if (_currentTransitionState != AnimState::TransitionLedgeClimb) {
			if (_currentSpecialMove != SpecialMoveType::Buttstomp) {
				if ((events->GetEventByPosition(_pos.X, _pos.Y, &p) == EventType::AreaFloatUp) ||
					(events->GetEventByPosition(AABBInner.L - ExtendedHitbox, AABBInner.T - ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
					(events->GetEventByPosition(AABBInner.R + ExtendedHitbox, AABBInner.T - ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
					(events->GetEventByPosition(AABBInner.R + ExtendedHitbox, AABBInner.B + ExtendedHitbox, &p) == EventType::AreaFloatUp) ||
					(events->GetEventByPosition(AABBInner.L - ExtendedHitbox, AABBInner.B + ExtendedHitbox, &p) == EventType::AreaFloatUp)
				) {
					if (GetState(ActorState::ApplyGravitation)) {
						float gravity = _levelHandler->Gravity;
						_externalForce.Y = gravity * 2.0f * timeMult;
						_speed.Y = std::min(gravity * timeMult, _speed.Y);
					} else {
						_speed.Y = std::max(_speed.Y - _levelHandler->Gravity * timeMult, -6.0f);
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

			//
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

	std::shared_ptr<AudioBufferPlayer> Player::PlayPlayerSfx(const StringView& identifier, float gain, float pitch)
	{
		auto it = _metadata->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _metadata->Sounds.end()) {
			int idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int)it->second.Buffers.size()) : 0);
			return _levelHandler->PlaySfx(it->second.Buffers[idx].get(), Vector3f(0.0f, 0.0f, 0.0f), true, gain, pitch);
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

	bool Player::CanFreefall()
	{
		AABBf aabb = AABBf(_pos.X - 14, _pos.Y + 8 - 12, _pos.X + 14, _pos.Y + 8 + 12 + 100);
		TileCollisionParams params = { TileDestructType::None, true };
		return _levelHandler->IsPositionEmpty(this, aabb, params);
	}

	void Player::OnPerishInner()
	{
		SetPlayerTransition(AnimState::TransitionDeath, false, true, SpecialMoveType::None, [this]() {
			if (_lives > 1 || _levelHandler->Difficulty() == GameDifficulty::Multiplayer) {
				if (_lives > 1) {
					_lives--;
				}

				// Remove fast fires
				_weaponUpgrades[(int)WeaponType::Blaster] = (uint8_t)(_weaponUpgrades[(int)WeaponType::Blaster] & 0x1);

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
				_controllable = true;
				_inShallowWater = -1;
				_invulnerableTime = 0.0f;
				SetModifier(Modifier::None);

				// Spawn corpse
				std::shared_ptr<PlayerCorpse> corpse = std::make_shared<PlayerCorpse>();
				uint8_t playerParams[2] = { (uint8_t)_playerType, (uint8_t)(IsFacingLeft() ? 1 : 0) };
				corpse->OnActivated({
					.LevelHandler = _levelHandler,
					.Pos = Vector3i(_pos.X, _pos.Y, _renderer.layer() - 10),
					.Params = playerParams
				});
				_levelHandler->AddActor(corpse);

				SetAnimation(AnimState::Idle);

				if (_levelHandler->HandlePlayerDied(shared_from_this())) {
					// Reset health
					_health = _maxHealth;

					// Player can be respawned immediately
					SetState(ActorState::IsInvulnerable, false);
					SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects, true);

					// Return to the last save point
					MoveInstantly(_checkpointPos, MoveType::Absolute | MoveType::Force);
					_levelHandler->SetAmbientLight(_checkpointLight);
					_levelHandler->LimitCameraView(0, 0);
					_levelHandler->WarpCameraToTarget(shared_from_this());

					_levelHandler->RollbackToCheckpoint();

				} else {
					// Respawn is delayed
					_controllable = false;
					_renderer.setDrawEnabled(false);

					// TODO: Turn off collisions
				}
			} else {
				_controllable = false;
				_invulnerableTime = 0.0f;
				_renderer.setDrawEnabled(false);

				_levelHandler->HandleGameOver();
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
		_currentWeapon = (WeaponType)(((int)_currentWeapon + 1) % (int)WeaponType::Count);

		for (int i = 0; i < (int)WeaponType::Count && _weaponAmmo[(int)_currentWeapon] == 0; i++) {
			_currentWeapon = (WeaponType)(((int)_currentWeapon + 1) % (int)WeaponType::Count);
		}

		_weaponCooldown = 1.0f;
	}

	void Player::SwitchToWeaponByIndex(int weaponIndex)
	{
		if (weaponIndex >= (int)WeaponType::Count || _weaponAmmo[weaponIndex] == 0) {
			return;
		}

		if (_weaponSound != nullptr) {
			_weaponSound->stop();
			_weaponSound = nullptr;
		}

		_currentWeapon = (WeaponType)weaponIndex;
		_weaponCooldown = 1.0f;
	}

	template<typename T, WeaponType weaponType>
	void Player::FireWeapon(float cooldownBase, float cooldownUpgrade)
	{
		// NOTE: cooldownBase and cooldownUpgrade cannot be template parameters in Emscripten
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		std::shared_ptr<T> shot = std::make_shared<T>();
		uint8_t shotParams[1] = { _weaponUpgrades[(int)weaponType] };
		shot->OnActivated({
			.LevelHandler = _levelHandler,
			.Pos = initialPos,
			.Params = shotParams
		});
		shot->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
		_levelHandler->AddActor(shot);

		_weaponCooldown = cooldownBase - (_weaponUpgrades[(int)WeaponType::Blaster] * cooldownUpgrade);
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
			shot1->OnActivated({
				.LevelHandler = _levelHandler,
				.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2),
				.Params = shotParams
			});
			shot1->OnFire(shared_from_this(), gunspotPos, _speed, angle - 0.3f, IsFacingLeft());
			_levelHandler->AddActor(shot1);

			std::shared_ptr<Weapons::RFShot> shot2 = std::make_shared<Weapons::RFShot>();
			shot2->OnActivated({
				.LevelHandler = _levelHandler,
				.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2),
				.Params = shotParams
			});
			shot2->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
			_levelHandler->AddActor(shot2);

			std::shared_ptr<Weapons::RFShot> shot3 = std::make_shared<Weapons::RFShot>();
			shot3->OnActivated({
				.LevelHandler = _levelHandler,
				.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2),
				.Params = shotParams
			});
			shot3->OnFire(shared_from_this(), gunspotPos, _speed, angle + 0.3f, IsFacingLeft());
			_levelHandler->AddActor(shot3);
		} else {
			std::shared_ptr<Weapons::RFShot> shot1 = std::make_shared<Weapons::RFShot>();
			shot1->OnActivated({
				.LevelHandler = _levelHandler,
				.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2),
				.Params = shotParams
			});
			shot1->OnFire(shared_from_this(), gunspotPos, _speed, angle - 0.22f, IsFacingLeft());
			_levelHandler->AddActor(shot1);

			std::shared_ptr<Weapons::RFShot> shot2 = std::make_shared<Weapons::RFShot>();
			shot2->OnActivated({
				.LevelHandler = _levelHandler,
				.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2),
				.Params = shotParams
			});
			shot2->OnFire(shared_from_this(), gunspotPos, _speed, angle + 0.22f, IsFacingLeft());
			_levelHandler->AddActor(shot2);
		}

		_weaponCooldown = 100.0f - (_weaponUpgrades[(int)WeaponType::Blaster] * 1.0f);
	}

	void Player::FireWeaponTNT()
	{
		std::shared_ptr<Weapons::TNT> tnt = std::make_shared<Weapons::TNT>();
		tnt->OnActivated({
			.LevelHandler = _levelHandler,
			.Pos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2)
		});
		tnt->OnFire(shared_from_this());
		_levelHandler->AddActor(tnt);

		_weaponCooldown = 30.0f;
	}

	bool Player::FireWeaponThunderbolt()
	{
		if (_isActivelyPushing || _inWater || _isAttachedToPole || std::abs(_speed.X) > 1.0f || std::abs(_speed.Y) > 1.0f ||
			!(GetState(ActorState::CanJump) || _activeModifier != Modifier::None || _suspendType == SuspendType::Vine || _suspendType == SuspendType::Hook)) {
			return false;
		}

		// NOTE: cooldownBase and cooldownUpgrade cannot be template parameters in Emscripten
		Vector3i initialPos;
		Vector2f gunspotPos;
		float angle;
		GetFirePointAndAngle(initialPos, gunspotPos, angle);

		std::shared_ptr<Weapons::Thunderbolt> shot = std::make_shared<Weapons::Thunderbolt>();
		uint8_t shotParams[1] = { _weaponUpgrades[(int)WeaponType::Thunderbolt] };
		shot->OnActivated({
			.LevelHandler = _levelHandler,
			.Pos = initialPos,
			.Params = shotParams
		});
		shot->OnFire(shared_from_this(), gunspotPos, _speed, angle, IsFacingLeft());
		_levelHandler->AddActor(shot);

		_weaponCooldown = 12.0f - (_weaponUpgrades[(int)WeaponType::Blaster] * 0.1f);
		_controllable = false;
		_controllableTimeout = _weaponCooldown;

		if (_weaponSound == nullptr) {
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

		uint16_t ammoDecrease = 256;

		switch (weaponType) {
			case WeaponType::Blaster:
				FireWeapon<Weapons::BlasterShot, WeaponType::Blaster>(40.0f, 1.0f);
				PlaySfx("WeaponBlaster"_s);
				ammoDecrease = 0;
				break;

			case WeaponType::Bouncer: FireWeapon<Weapons::BouncerShot, WeaponType::Bouncer>(32.0f, 0.85f); break;
				case WeaponType::Freezer:
					// TODO: Add upgraded freezer
					FireWeapon<Weapons::FreezerShot, WeaponType::Freezer>(46.0f, 0.8f);
					break;
				case WeaponType::Seeker: FireWeapon<Weapons::SeekerShot, WeaponType::Seeker>(100.0f, 1.0f); break;
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
				//case WeaponType::Pepper: FireWeaponPepper(); break;
				//case WeaponType::Electro: FireWeaponElectro(); break;

				case WeaponType::Thunderbolt: {
					if (!FireWeaponThunderbolt()) {
						return false;
					}
					ammoDecrease = ((_weaponUpgrades[(int)WeaponType::Thunderbolt] & 0x1) != 0 ? 30 : 50); // Lower ammo consumption with upgrade
					break;
				}

			default:
				return false;
		}

		auto& currentAmmo = _weaponAmmo[(int)weaponType];
		if (ammoDecrease > currentAmmo) {
			ammoDecrease = currentAmmo;
		}
		currentAmmo -= ammoDecrease;

		// No ammo, switch weapons
		if (currentAmmo == 0) {
			SwitchToNextWeapon();
			PlaySfx("ChangeWeapon"_s);
			_weaponCooldown = 20.0f;
		}

		return (weaponType != WeaponType::TNT);
	}

	void Player::GetFirePointAndAngle(Vector3i& initialPos, Vector2f& gunspotPos, float& angle)
	{
		initialPos = Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2);
		gunspotPos = _pos;

		if (_inWater) {
			angle = _renderer.rotation();

			int size = (_currentAnimation->Base->FrameDimensions.X / 2);
			gunspotPos.X += (cosf(angle) * size) * (IsFacingLeft() ? -1.0f : 1.0f);
			gunspotPos.Y += (sinf(angle) * size) * (IsFacingLeft() ? -1.0f : 1.0f) - (_currentAnimation->Base->Hotspot.Y - _currentAnimation->Base->Gunspot.Y);
		} else {
			gunspotPos.X += (_currentAnimation->Base->Hotspot.X - _currentAnimation->Base->Gunspot.X) * (IsFacingLeft() ? 1 : -1);
			gunspotPos.Y -= (_currentAnimation->Base->Hotspot.Y - _currentAnimation->Base->Gunspot.Y);

			if ((_currentAnimationState & AnimState::Lookup) == AnimState::Lookup) {
				initialPos.X = (int)gunspotPos.X;
				angle = (IsFacingLeft() ? fRadAngle90 : fRadAngle270);
			} else {
				initialPos.Y = (int)gunspotPos.Y;
				angle = 0.0f;
			}
		}
	}

	bool Player::OnLevelChanging(ExitType exitType)
	{
		// TODO: Birds
		/*if (_activeBird != nullptr) {
			_activeBird->FlyAway();
			_activeBird = nullptr;
		}*/

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
				}
				return false;
			}

			case LevelExitingState::WaitingForWarp: {
				if (_lastPoleTime <= 0.0f) {
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
				}
				return false;
			}

			case LevelExitingState::Transition:
				return false;

			case LevelExitingState::Ready:
				return true;
		}

		PlayPlayerSfx("EndOfLevel"_s);

		if (_suspendType != SuspendType::None) {
			MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative | MoveType::Force);
			_suspendType = SuspendType::None;
			_suspendTime = 60.0f;
		}

		_controllable = false;
		SetFacingLeft(false);
		SetState(ActorState::IsInvulnerable | ActorState::ApplyGravitation, true);
		_fireFramesLeft = 0.0f;
		_copterFramesLeft = 0.0f;
		_pushFramesLeft = 0.0f;
		_invulnerableTime = 0.0f;

		if (exitType == ExitType::Warp || exitType == ExitType::Bonus || exitType == ExitType::Boss || _inWater) {
			_levelExiting = LevelExitingState::WaitingForWarp;

			// Re-used for waiting timeout
			_lastPoleTime = 100.0f;
		} else {
			_levelExiting = LevelExitingState::Waiting;

			// Re-used for waiting timeout
			_lastPoleTime = 300.0f;
		}

		return false;
	}

	void Player::ReceiveLevelCarryOver(ExitType exitType, const PlayerCarryOver& carryOver)
	{
		_lives = carryOver.Lives;
		_score = carryOver.Score;
		_foodEaten = carryOver.FoodEaten;
		_currentWeapon = carryOver.CurrentWeapon;

		std::memcpy(_weaponAmmo, carryOver.Ammo, sizeof(_weaponAmmo));
		std::memcpy(_weaponUpgrades, carryOver.WeaponUpgrades, sizeof(_weaponUpgrades));

		_weaponAmmo[(int)WeaponType::Blaster] = UINT16_MAX;

		if (exitType == ExitType::Warp || exitType == ExitType::Bonus) {
			PlayPlayerSfx("WarpOut"_s);

			SetState(ActorState::ApplyGravitation, false);

			_isFreefall = CanFreefall();
			SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpOutFreefall : AnimState::TransitionWarpOut, false, true, SpecialMoveType::None, [this]() {
				SetState(ActorState::IsInvulnerable, false);
				SetState(ActorState::ApplyGravitation, true);
				_controllable = true;
			});

			//attachedHud?.BeginFadeIn(false);
		} else {
			//attachedHud?.BeginFadeIn(true);
		}

		// Preload all weapons
		for (int i = 0; i < _countof(_weaponAmmo); i++) {
			if (_weaponAmmo[i] != 0) {
				PreloadMetadataAsync("Weapon/"_s + WeaponNames[i]);
			}
		}
	}

	PlayerCarryOver Player::PrepareLevelCarryOver()
	{
		PlayerCarryOver carryOver;
		carryOver.Type = _playerType;
		carryOver.Lives = _lives;
		carryOver.Score = _score;
		carryOver.FoodEaten = _foodEaten;
		carryOver.CurrentWeapon = _currentWeapon;

		std::memcpy(carryOver.Ammo, _weaponAmmo, sizeof(_weaponAmmo));
		std::memcpy(carryOver.WeaponUpgrades, _weaponUpgrades, sizeof(_weaponUpgrades));

		return carryOver;
	}

	void Player::WarpToPosition(Vector2f pos, bool fast)
	{
		if (fast) {
			Vector2f posOld = _pos;
			MoveInstantly(pos, MoveType::Absolute | MoveType::Force);

			if (Vector2f(posOld.X - pos.X, posOld.Y - pos.Y).Length() > 250.0f) {
				_levelHandler->WarpCameraToTarget(shared_from_this());
			}
		} else {
			EndDamagingMove();
			SetState(ActorState::IsInvulnerable, true);
			SetState(ActorState::ApplyGravitation, false);

			SetAnimation(_currentAnimationState & ~(AnimState::Uppercut | AnimState::Buttstomp));

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

			SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpInFreefall : AnimState::TransitionWarpIn, false, true, SpecialMoveType::None, [this, pos]() {
				Vector2f posOld = _pos;
				MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
				PlayPlayerSfx("WarpOut"_s);

				if (Vector2f(posOld.X - pos.X, posOld.Y - pos.Y).Length() > 250) {
					_levelHandler->WarpCameraToTarget(shared_from_this());
				}

				_isFreefall |= CanFreefall();
				SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpOutFreefall : AnimState::TransitionWarpOut, false, true, SpecialMoveType::None, [this]() {
					SetState(ActorState::IsInvulnerable, false);
					SetState(ActorState::ApplyGravitation, true);
					_controllable = true;
				});
			});
		}
	}

	void Player::InitialPoleStage(bool horizontal)
	{
		if (_isAttachedToPole || _playerType == PlayerType::Frog) {
			return;
		}
		
		int x = (int)_pos.X / Tiles::TileSet::DefaultTileSize;
		int y = (int)_pos.Y / Tiles::TileSet::DefaultTileSize;

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

		auto events = _levelHandler->EventMap();
		uint8_t* p;
		if (horizontal) {
			if (events->GetEventByPosition(x, (_pos.Y < ty ? y - 1 : y + 1), &p) == EventType::ModifierHPole) {
				ty = _pos.Y;
			}
		} else {
			if (events->GetEventByPosition((_pos.X < tx ? x - 1 : x + 1), y, &p) == EventType::ModifierVPole) {
				tx = _pos.X;
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
		_isAttachedToPole = true;
		_inIdleTransition = false;

		_keepRunningTime = 0.0f;
		_pushFramesLeft = 0.0f;
		_fireFramesLeft = 0.0f;
		_copterFramesLeft = 0.0f;

		SetAnimation(_currentAnimationState & ~(AnimState::Uppercut /*| AnimState::Sidekick*/ | AnimState::Buttstomp));

		AnimState poleAnim = (horizontal ? AnimState::TransitionPoleHSlow : AnimState::TransitionPoleVSlow);
		SetPlayerTransition(poleAnim, false, true, SpecialMoveType::None, [this, horizontal, positive, lastSpeed]() {
			NextPoleStage(horizontal, positive, 2, lastSpeed);
		});

		_controllableTimeout = 80.0f;

		PlaySfx("Pole"_s, 0.8f, 0.6f);
	}

	void Player::NextPoleStage(bool horizontal, bool positive, int stagesLeft, float lastSpeed)
	{
		if (stagesLeft > 0) {
			AnimState poleAnim = (horizontal ? AnimState::TransitionPoleH : AnimState::TransitionPoleV);
			SetPlayerTransition(poleAnim, false, true, SpecialMoveType::None, [this, horizontal, positive, stagesLeft, lastSpeed]() {
				NextPoleStage(horizontal, positive, stagesLeft - 1, lastSpeed);
			});

			_inIdleTransition = false;
			_controllableTimeout = 80.0f;

			PlaySfx("Pole"_s, 1.0f, 0.6f);
		} else {
			int sign = (positive ? 1 : -1);
			if (horizontal) {
				// To prevent stucking
				for (int i = -1; i > -6; i--) {
					if (MoveInstantly(Vector2f(_speed.X, i), MoveType::Relative)) {
						break;
					}
				}

				_speed.X = 10 * sign + lastSpeed * 0.2f;
				_externalForce.X = 10 * sign;
				SetFacingLeft(!positive);

				_keepRunningTime = 60.0f;

				SetPlayerTransition(AnimState::Dash | AnimState::Jump, true, true, SpecialMoveType::None);
			} else {
				MoveInstantly(Vector2f(0, sign * 16), MoveType::Relative | MoveType::Force);

				_speed.Y = 4 * sign + lastSpeed * 1.4f;
				_externalForce.Y = (-1.3f * sign);
			}

			SetState(ActorState::ApplyGravitation, true);
			_isAttachedToPole = false;
			_wasActivelyPushing = false;
			_inIdleTransition = false;

			_controllableTimeout = 4.0f;
			_lastPoleTime = 10.0f;

			PlaySfx("HookAttach"_s, 0.8f, 1.2f);
		}
	}

	bool Player::SetModifier(Modifier modifier)
	{
		if (_activeModifier == modifier) {
			return false;
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

				_activeModifier = Modifier::Copter;

				_copterFramesLeft = 350.0f;
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

				_copterFramesLeft = 150.0f;

				/*CopterDecor copter = new CopterDecor();
				copter.OnActivated(new ActorActivationDetails {
					LevelHandler = levelHandler
				});
				copter.Parent = this;*/
				break;
			}

			default: {
				_activeModifier = Modifier::None;

				/*CopterDecor copterDecor = GetFirstChild<CopterDecor>();
				if (copterDecor != null) {
					copterDecor.DecreaseHealth(int.MaxValue);
				}*/

				SetState(ActorState::CanJump | ActorState::ApplyGravitation, true);

				SetAnimation(AnimState::Fall);
				break;
			}
		}

#if MULTIPLAYER && SERVER
		((LevelHandler)levelHandler).OnPlayerSetModifier(this, modifier);
#endif

		return true;
	}

	bool Player::TakeDamage(int amount, float pushForce)
	{
		if (GetState(ActorState::IsInvulnerable) || _levelExiting != LevelExitingState::None) {
			return false;
		}

		// Cancel active climbing
		if (_currentTransitionState == AnimState::TransitionLedgeClimb) {
			ForceCancelTransition();

			MoveInstantly(Vector2f(IsFacingLeft() ? 6.0f : -6.0f, 0.0f), MoveType::Relative | MoveType::Force);
		}

		DecreaseHealth(amount, nullptr);

		_speed.X = 0.0f;
		_internalForceY = 0.0f;
		_fireFramesLeft = 0.0f;
		_copterFramesLeft = 0.0f;
		_pushFramesLeft = 0.0f;
		SetState(ActorState::CanJump, false);
		_isAttachedToPole = false;

		// TODO: Birds
		/*if (activeBird != null) {
			activeBird.FlyAway();
			activeBird = null;
		}*/

		if (_health > 0) {
			_externalForce.X = pushForce;

			if (!_inWater && _activeModifier == Modifier::None) {
				_speed.Y = -6.5f;

				SetState(ActorState::ApplyGravitation | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects, true);
				SetAnimation(AnimState::Idle);
			} else {
				_speed.Y = -1.0f;
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

#if MULTIPLAYER && SERVER
		((LevelHandler)levelHandler).OnPlayerTakeDamage(this, pushForce);
#endif
		return true;
	}

	void Player::SetInvulnerability(float time, bool withCircleEffect)
	{
		if (time <= 0.0f) {
			if (_invulnerableTime > 0.0f) {
				SetState(ActorState::IsInvulnerable, false);
				_invulnerableTime = 0.0f;
				_renderer.setDrawEnabled(true);

				// TODO: Circle effect
				//SetCircleEffect(false);

#if MULTIPLAYER && SERVER
				((LevelHandler)levelHandler).OnPlayerSetInvulnerability(this, 0f, false);
#endif
			}
			return;
		}

		SetState(ActorState::IsInvulnerable, true);
		_invulnerableTime = time;

		if (withCircleEffect) {
			// TODO: Circle effect
			//SetCircleEffect(true);
		}

#if MULTIPLAYER && SERVER
		((LevelHandler)levelHandler).OnPlayerSetInvulnerability(this, time, withCircleEffect);
#endif
	}

	void Player::EndDamagingMove()
	{
		SetState(ActorState::ApplyGravitation, true);
		SetAnimation(_currentAnimationState & ~(AnimState::Uppercut | AnimState::Buttstomp));

		if (_currentSpecialMove == SpecialMoveType::Uppercut) {
			if (_suspendType == SuspendType::None) {
				SetTransition(AnimState::TransitionUppercutEnd, false);
			}
			_controllable = true;

			if (_externalForce.Y > 0.0f) {
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
		const int HealthLimit = 5;

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

	void Player::AddLives(int count)
	{
		_lives += count;

		PlaySfx("PickupOneUp"_s);
	}

	void Player::AddCoins(int count)
	{
		_coins += count;
		_levelHandler->ShowCoins(_coins);
		PlaySfx("PickupCoin"_s);
	}

	void Player::AddGems(int count)
	{
		_gems += count;
		_levelHandler->ShowGems(_gems);
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
			if (_sugarRushLeft <= 0.0f) {
				_sugarRushLeft = 1300.0f;
				_renderer.Initialize(ActorRendererType::PartialWhiteMask);
				_levelHandler->ActivateSugarRush();
			}
		}
	}

	bool Player::AddAmmo(WeaponType weaponType, int16_t count)
	{
		constexpr int16_t Multiplier = 256;
		constexpr int16_t AmmoLimit = 99 * Multiplier;

		if (weaponType >= WeaponType::Count || _weaponAmmo[(int)weaponType] < 0 || _weaponAmmo[(int)weaponType] >= AmmoLimit) {
			return false;
		}

		bool switchTo = (_weaponAmmo[(int)weaponType] == 0);

		_weaponAmmo[(int)weaponType] = std::min((int16_t)(_weaponAmmo[(int)weaponType] + count * Multiplier), AmmoLimit);

		if (switchTo) {
			_currentWeapon = weaponType;

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

	void Player::AddWeaponUpgrade(WeaponType weaponType, uint8_t upgrade)
	{
		_weaponUpgrades[(int)weaponType] |= upgrade;
	}

	bool Player::AddFastFire(int count)
	{
		const int FastFireLimit = 9;

		int current = (_weaponUpgrades[(int)WeaponType::Blaster] >> 1);
		if (current >= FastFireLimit) {
			return false;
		}

		current = std::min(current + count, FastFireLimit);

		_weaponUpgrades[(int)WeaponType::Blaster] = (uint8_t)((_weaponUpgrades[(int)WeaponType::Blaster] & 0x1) | (current << 1));

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
			case PlayerType::Jazz:
				RequestMetadata("Interactive/PlayerJazz");
				break;
			case PlayerType::Spaz:
				RequestMetadata("Interactive/PlayerSpaz");
				break;
			case PlayerType::Lori:
				RequestMetadata("Interactive/PlayerLori");
				break;
			case PlayerType::Frog:
				RequestMetadata("Interactive/PlayerFrog");
				break;
		}

		// Refresh animation state
		if ((_currentSpecialMove == SpecialMoveType::None) ||
			(_currentSpecialMove == SpecialMoveType::Buttstomp && (type == PlayerType::Jazz || type == PlayerType::Spaz || type == PlayerType::Lori))) {
			_currentAnimation = nullptr;
			SetAnimation(_currentAnimationState);
		} else {
			_currentAnimation = nullptr;
			SetAnimation(AnimState::Fall);

			SetState(ActorState::ApplyGravitation, true);
			_controllable = true;

			if (_currentSpecialMove == SpecialMoveType::Uppercut && _externalForce.Y > 0.0f) {
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
			Explosion::Create(_levelHandler, Vector3i((int)(_pos.X - 12.0f), (int)(_pos.Y - 6.0f), _renderer.layer() + 4), Explosion::Type::SmokeBrown);
			Explosion::Create(_levelHandler, Vector3i((int)(_pos.X - 8.0f), (int)(_pos.Y + 28.0f), _renderer.layer() + 4), Explosion::Type::SmokeBrown);
			Explosion::Create(_levelHandler, Vector3i((int)(_pos.X + 12.0f), (int)(_pos.Y + 10.0f), _renderer.layer() + 4), Explosion::Type::SmokeBrown);

			Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)(_pos.Y + 12.0f), _renderer.layer() + 6), Explosion::Type::SmokeBrown);
		}
	}

	void Player::MorphRevent()
	{
		MorphTo(_playerTypeOriginal);
	}

	bool Player::SetDizzyTime(float time)
	{
		bool wasNotDizzy = (_dizzyTime <= 0.0f);
		_dizzyTime = time;
		return wasNotDizzy;
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
	}
}