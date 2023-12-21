#include "PinballBumper.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Solid
{
	PinballBumper::PinballBumper()
		:
		_cooldown(0.0f),
		_lightIntensity(0.0f),
		_lightBrightness(0.0f)
	{
	}

	void PinballBumper::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/PinballBumper"_s);
	}

	Task<bool> PinballBumper::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];

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
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 16.0f, [this, timeMult](ActorBase* actor) {
				if (auto* player = runtime_cast<Player*>(actor)) {
					_cooldown = 16.0f;

					SetTransition(_currentAnimation->State | (AnimState)0x200, true);
					PlaySfx("Hit"_s, 0.8f);

					constexpr float forceMult = 12.0f;
					Vector2f force = (player->GetPos() - _pos).Normalize() * forceMult;
					if (!_levelHandler->IsReforged()) {
						force.Y *= 0.9f;
					}

					// Move the player back
					player->MoveInstantly(-player->_speed * timeMult, MoveType::Relative);

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