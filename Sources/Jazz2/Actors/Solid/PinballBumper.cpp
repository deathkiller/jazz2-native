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

		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

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
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 26.0f, [this](ActorBase* actor) {
				if (auto player = dynamic_cast<Player*>(actor)) {
					_cooldown = 10.0f;

					SetTransition(_currentAnimation->State | (AnimState)0x200, true);
					PlaySfx("Hit"_s, 0.8f);

					constexpr float forceMult = 24.0f;
					Vector2f force = (player->GetPos() - _pos).Normalize() * forceMult;
					if (!_levelHandler->IsReforged()) {
						force.Y *= 0.7f;
					}
					player->_speed.X += force.X * 0.4f;
					player->_speed.Y += force.Y * 0.4f;
					player->_externalForce.X += force.X * 0.04f;
					player->_externalForce.Y += force.Y * 0.04f;

					player->_controllable = true;
					player->SetState(ActorState::CanJump, false);
					player->EndDamagingMove();

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