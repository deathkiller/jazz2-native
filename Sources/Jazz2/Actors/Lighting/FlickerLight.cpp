#include "FlickerLight.h"
#include "../../ILevelHandler.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Lighting
{
	FlickerLight::FlickerLight()
	{
	}

	Task<bool> FlickerLight::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_intensity = details.Params[0] / 255.0f;
		_brightness = details.Params[1] / 255.0f;
		_radiusNear = (float)*(uint16_t*)&details.Params[2];
		_radiusFar = (float)*(uint16_t*)&details.Params[4];
		_phase = 0.6f;

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		for (int i = 0; i < LightPartCount; i++) {
			float radius = Random().FastFloat(_radiusFar * 0.5f, _radiusFar * 0.8f);
			float angle = Random().FastFloat(0.0f, fTwoPi);
			float distance = Random().FastFloat(0.0f, _radiusFar * 0.6f);

			LightPart& part = _parts.emplace_back();
			part.Radius = radius;
			part.Pos = Vector2f(_pos.X + cosf(angle) * distance, _pos.Y + sinf(angle) * distance);
			part.Phase = (float)i / LightPartCount;
		}

		async_return true;
	}

	void FlickerLight::OnUpdate(float timeMult)
	{
		_phase = lerp(_phase, Random().FastFloat(), 0.04f * timeMult);

		for (auto& part : _parts) {
			part.Phase += timeMult * 0.02f;
			if (part.Phase >= 1.0f) {
				float radius = Random().FastFloat(_radiusFar * 0.5f, _radiusFar * 0.8f);
				float angle = Random().FastFloat(0.0f, fTwoPi);
				float distance = Random().FastFloat(0.0f, _radiusFar * 0.6f);

				part.Radius = radius;
				part.Pos = Vector2f(_pos.X + cosf(angle) * distance, _pos.Y + sinf(angle) * distance);
				part.Phase = 0.0f;
			}
		}
	}

	void FlickerLight::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = _intensity * _phase * 0.2f;
		light.Brightness = _brightness * 0.4f;
		light.RadiusNear = _radiusNear;
		light.RadiusFar = lerp(_radiusNear, _radiusFar, _phase);

		for (auto& part : _parts) {
			float phase = sinf(part.Phase * fPi);

			auto& light2 = lights.emplace_back();
			light2.Pos = part.Pos;
			light2.Intensity = _intensity * phase * 0.6f;
			light2.Brightness = _brightness  * phase * 0.4f;
			light2.RadiusNear = 0.0f;
			light2.RadiusFar = part.Radius;
		}
	}
}