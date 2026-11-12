#include "PinballBumper.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../../Events/EventSpawner.h"
#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Solid
{
	PinballBumper::PinballBumper()
		: _cooldown(0.0f), _tickTime(0.0f), _lightIntensity(0.0f), _lightBrightness(0.0f)
	{
	}

	void PinballBumper::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/PinballBumper"_s);
	}

	Task<bool> PinballBumper::OnActivatedAsync(const ActorActivationDetails& details)
	{
		std::uint8_t theme = details.Params[0];
		_isCarrot = (theme == 1);

		SetState(ActorState::CollideWithTileset | ActorState::IsSolidObject | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Object/PinballBumper"_s);

		SetAnimation((AnimState)theme);

		async_return true;
	}

	void PinballBumper::OnUpdate(float timeMult)
	{
		// A frozen bumper still bumps - the freezer only turns it to ice on screen, with the frozen shader and its
		// animation held until it thaws
		UpdateFrozenState(timeMult);

		if (!_levelHandler->IsReforged()) {
			// Checked every tick of the original, a player still overlapping is thrown out again, so it can't get through
			_tickTime += timeMult * Player::LegacyFrameRateScale;
			while (_tickTime >= 1.0f) {
				_tickTime -= 1.0f;
				LegacyTick();
			}
			if (_cooldown > 0.0f) {
				_cooldown -= timeMult;
			}
		} else if (_cooldown <= 0.0f) {
			// Measured against the original by dropping the player past a bumper at a set horizontal offset,
			// leftwards where no second bumper is overhead: it fires at 0 and at 16 px and misses from 48 px
			// out. This engine fired at 48 as well - the player's own hitbox extends whatever is asked for
			// here by about half its width - so the reach is pulled in to match. Vertically both games
			// already agreed, catching the player about 36 px above or below the centre.
			bool carrotPending = false;
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, TriggerRadius, [this, timeMult, &carrotPending](ActorBase* actor) {
				if (auto* player = runtime_cast<Player>(actor)) {
					_cooldown = 16.0f;

					// Not while frozen: the animation is held then, so the hit would sit on its first frame and
					// only play out once the ice is gone
					if (_frozenTimeLeft <= 0.0f) {
						SetTransition(_currentAnimation->State | (AnimState)0x200, true);
					}
					PlaySfx("Hit"_s, 0.8f);

					// A carrot bumper gives out an energy carrot now and then, added once the search is over
					carrotPending |= (_isCarrot && Random().Next(0, 4) == 3);

					// Move the player back
					player->MoveInstantly(-player->_speed * timeMult, MoveType::Relative);

					Vector2f force = (player->GetPos() - _pos).Normalize() * 12.0f;

					// Reset speed if the force acts on the other side
					if (force.X < 0.0f) {
						if (player->_speed.X > 0.0f) player->_speed.X = 0.0f;
						if (player->_externalForce.X > 0.0f) player->_externalForce.X = 0.0f;
					} else if (force.X > 0.0f) {
						if (player->_speed.X < 0.0f) player->_speed.X = 0.0f;
						if (player->_externalForce.X < 0.0f) player->_externalForce.X = 0.0f;
					}
					if (force.Y < 0.0f) {
						if (player->_speed.Y > 0.0f) player->_speed.Y = 0.0f;
						if (player->_externalForce.Y > 0.0f) player->_externalForce.Y = 0.0f;
					} else if (force.Y > 0.0f) {
						if (player->_speed.Y < 0.0f) player->_speed.Y = 0.0f;
						if (player->_externalForce.Y < 0.0f) player->_externalForce.Y = 0.0f;
					}

					player->_speed.X += force.X * 0.4f;
					player->_speed.Y += force.Y * 0.4f;

					if (player->_activeModifier == Player::Modifier::None) {
						if (player->_copterFramesLeft > 0.0f) {
							player->_copterFramesLeft = 1.0f;
						}

						player->_externalForce.X += force.X * 0.04f;
						player->_externalForce.Y += force.Y * 0.04f;
						player->_externalForceCooldown = 10.0f;
						player->_controllable = true;
						player->SetState(ActorState::CanJump, false);
						player->EndDamagingMove();
					}

					// TODO: Check this
					player->AddScore(500);

					// Resync the knockback to the owning client, otherwise remote players aren't affected in multiplayer
					_levelHandler->HandlePlayerPushed(player);
				}
				return true;
			});

			if (carrotPending) {
				std::uint8_t carrotParams[Events::EventSpawner::SpawnParamsSize] = { 0 };
				std::shared_ptr<ActorBase> carrot = _levelHandler->EventSpawner()->SpawnEvent(EventType::Carrot, carrotParams,
					ActorState::None, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 10));
				if (carrot != nullptr) {
					_levelHandler->AddActor(carrot);
				}
			}
		} else {
			_cooldown -= timeMult;
		}

		if (_lightIntensity > 0.0f) {
			_lightIntensity -= timeMult * 0.01f;
			if (_lightIntensity < 0.0f) {
				_lightIntensity = 0.0f;
			}
		}
		if (_lightBrightness > 0.0f) {
			_lightBrightness -= timeMult * 0.02f;

			if (_lightIntensity < 0.0f) {
				_lightIntensity = 0.0f;
			}
		}
	}

	void PinballBumper::LegacyTick()
	{
		bool carrotPending = false;
		// Only players can be bumped, so they are checked directly instead of searching all actors around
		for (auto* player : _levelHandler->GetPlayers()) {
			if ((player->GetState() & (ActorState::CollideWithOtherActors | ActorState::IsDestroyed | ActorState::ForceDisableCollisions)) != ActorState::CollideWithOtherActors) {
				continue;
			}
			float closestX = std::clamp(_pos.X, player->AABB.L, player->AABB.R);
			float closestY = std::clamp(_pos.Y, player->AABB.T, player->AABB.B);
			if ((Vector2f(closestX, closestY) - _pos).SqrLength() >= LegacyBroadRadius * LegacyBroadRadius ||
				player->GetOverlappingPixelCount(this, LegacyMinOverlapPixels) < LegacyMinOverlapPixels) {
				continue;
			}

			// Speed is set to a quarter of the offset every overlapping tick (an eighth while frozen), the bias stands in for the original's origin
			Vector2f offset = player->GetPos() - _pos;
			float scale = (_frozenTimeLeft > 0.0f ? LegacyImpulseScale * 0.5f : LegacyImpulseScale) * Player::LegacyFrameRateScale;
			player->_speed.X = (offset.X + LegacyImpulseBias) * scale;
			player->_speed.Y = (offset.Y + LegacyImpulseBias) * scale;
			// The original's physics caps sideways speed at the walk or run limit on the next tick
			float maxSpeedX = (player->IsDashActive() ? Player::LegacyDashSpeed : Player::LegacyWalkSpeed);
			player->_speed.X = std::clamp(player->_speed.X, -maxSpeedX, maxSpeedX);
			// Rolled into a ball for 35 ticks, which also blocks shooting
			player->_tubeBallPoseLeft = Player::TubeBallPoseTime;
			// Measured in the original: the rise reads the live jump key, like a float-up's (the released rate without it)
			player->_jumpReleased = true;
			player->_riseFromFloatUp = true;
			player->_riseOwnedByJump = false;

			if (player->_activeModifier == Player::Modifier::None) {
				// End a copter properly rather than just shortening it, the way a spring does: Jazz's copter
				// switches gravity off, so a player launched out of one would keep the launch speed for ever
				if (player->_copterFramesLeft > 0.0f) {
					player->_copterFramesLeft = 0.0f;
					player->SetAnimation(player->_currentAnimation->State & ~AnimState::Copter);
				}
				player->SetState(ActorState::ApplyGravitation, true);

				player->_externalForceCooldown = 10.0f;
				player->_controllable = true;
				player->SetState(ActorState::CanJump, false);
				player->EndDamagingMove();
			}
			_levelHandler->HandlePlayerPushed(player);

			// The flash, the sound, the score and the carrot only once per hit
			if (_cooldown <= 0.0f) {
				_cooldown = 16.0f;
				if (_frozenTimeLeft <= 0.0f) {
					SetTransition(_currentAnimation->State | (AnimState)0x200, true);
				}
				PlaySfx("Hit"_s, 0.8f);
				player->AddScore(500);
				carrotPending |= (_isCarrot && Random().Next(0, 4) == 3);
			}
		}

		if (carrotPending) {
			std::uint8_t carrotParams[Events::EventSpawner::SpawnParamsSize] = { 0 };
			std::shared_ptr<ActorBase> carrot = _levelHandler->EventSpawner()->SpawnEvent(EventType::Carrot, carrotParams,
				ActorState::None, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() - 10));
			if (carrot != nullptr) {
				_levelHandler->AddActor(carrot);
			}
		}
	}

	void PinballBumper::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_lightIntensity > 0.0f) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = _lightIntensity;
			light.Brightness = _lightBrightness;
			light.RadiusNear = 24.0f;
			light.RadiusFar = 60.0f;
		}
	}
}