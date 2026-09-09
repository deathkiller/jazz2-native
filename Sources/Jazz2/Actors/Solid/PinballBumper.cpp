#include "PinballBumper.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Solid
{
	PinballBumper::PinballBumper()
		: _cooldown(0.0f), _lightIntensity(0.0f), _lightBrightness(0.0f)
	{
	}

	void PinballBumper::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/PinballBumper"_s);
	}

	Task<bool> PinballBumper::OnActivatedAsync(const ActorActivationDetails& details)
	{
		std::uint8_t theme = details.Params[0];

		SetState(ActorState::CollideWithTileset | ActorState::IsSolidObject | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Object/PinballBumper"_s);

		SetAnimation((AnimState)theme);

		async_return true;
	}

	void PinballBumper::OnUpdate(float timeMult)
	{
		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		if (_cooldown <= 0.0f) {
			// Measured against the original by dropping the player past a bumper at a set horizontal offset,
			// leftwards where no second bumper is overhead: it fires at 0 and at 16 px and misses from 48 px
			// out. This engine fired at 48 as well - the player's own hitbox extends whatever is asked for
			// here by about half its width - so the reach is pulled in to match. Vertically both games
			// already agreed, catching the player about 36 px above or below the centre.
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, TriggerRadius, [this, timeMult](ActorBase* actor) {
				if (auto* player = runtime_cast<Player>(actor)) {
					_cooldown = 16.0f;

					SetTransition(_currentAnimation->State | (AnimState)0x200, true);
					PlaySfx("Hit"_s, 0.8f);

					// Move the player back
					player->MoveInstantly(-player->_speed * timeMult, MoveType::Relative);

					if (!_levelHandler->IsReforged()) {
						// A straight speed **assignment**, linear in the offset from the bumper's centre and
						// independent of how fast the player arrived. Not a normalised radial impulse: the
						// launch scales with the distance rather than only with the direction. In the
						// original's own units it is exactly
						//
						//     speed = (playerPos - bumperPos + 1) / 4
						//
						// on both axes, which reproduces every hit measured - a drop through the centre
						// caught 36 px up leaves at -8.75; one 16 px to the side caught 28 px up leaves at
						// (-3.75, -6.75); an approach from below caught 47.25 px down is driven back down at
						// +12.0625; and a later bounce at an offset of (0.134, -38.12) leaves at
						// (0.2834, -9.2803). All four to four decimal places.
						//
						// **The `+1` is the whole reason a player ever escapes a bumper.** A dead-centre drop
						// is otherwise a perfect fixed point - zero horizontal, straight back up, forever -
						// and that is what this engine did: `pb_bump_fall` bounced on the spot thirteen times
						// in 800 ticks and was still going. The bias breaks the symmetry, the offset grows
						// with every bounce because the rule is linear in it, and the original's player is
						// thrown clear after about eight.
						Vector2f offset = player->GetPos() - _pos;
						player->_speed.X = (offset.X + LegacyImpulseBias) * LegacyImpulseScale * Player::LegacyFrameRateScale;
						player->_speed.Y = (offset.Y + LegacyImpulseBias) * LegacyImpulseScale * Player::LegacyFrameRateScale;

						if (player->_activeModifier == Player::Modifier::None) {
							// End a copter properly rather than just shortening it, the way a spring does:
							// Jazz's copter switches gravity **off**, and setting `_copterFramesLeft` alone
							// does not turn it back on, so a player launched out of one keeps the launch
							// speed indefinitely instead of arcing.
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

						player->AddScore(500);
						_levelHandler->HandlePlayerPushed(player);
						return true;
					}

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