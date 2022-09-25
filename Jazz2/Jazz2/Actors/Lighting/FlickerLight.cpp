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
		// TODO: Add noise
		_intensity = details.Params[0] / 255.0f;
		_brightness = details.Params[1] / 255.0f;
		_radiusNear = (float)*(uint16_t*)&details.Params[2];
		_radiusFar = (float)*(uint16_t*)&details.Params[4];
		_phase = 1.0;

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		co_return true;
	}

	void FlickerLight::OnUpdate(float timeMult)
	{
		_phase = lerp(_phase, Random().FastFloat(), 0.04f * timeMult);
	}

	void FlickerLight::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = _intensity * _phase;
		light.Brightness = _brightness;
		light.RadiusNear = _radiusNear;
		light.RadiusFar = lerp(_radiusNear, _radiusFar, _phase);
	}
}