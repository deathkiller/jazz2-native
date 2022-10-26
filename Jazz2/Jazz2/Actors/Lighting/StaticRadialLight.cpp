#include "StaticRadialLight.h"
#include "../../ILevelHandler.h"

namespace Jazz2::Actors::Lighting
{
	StaticRadialLight::StaticRadialLight()
	{
	}

	Task<bool> StaticRadialLight::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_intensity = details.Params[0] / 255.0f;
		_brightness = details.Params[1] / 255.0f;
		_radiusNear = (float)*(uint16_t*)&details.Params[2];
		_radiusFar = (float)*(uint16_t*)&details.Params[4];

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		async_return true;
	}

	void StaticRadialLight::OnUpdate(float timeMult)
	{
		// Nothing to do...
	}

	void StaticRadialLight::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = _intensity;
		light.Brightness = _brightness;
		light.RadiusNear = _radiusNear;
		light.RadiusFar = _radiusFar;
	}
}