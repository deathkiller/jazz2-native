#include "StaticRadialLight.h"
#include "../../ILevelHandler.h"

namespace Jazz2::Actors::Lighting
{
	StaticRadialLight::StaticRadialLight()
	{
	}

	Task<bool> StaticRadialLight::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_intensity = (float)*(uint16_t*)&details.Params[0] / 255.0f;
		_brightness = (float)*(uint16_t*)&details.Params[2] / 255.0f;
		_radiusNear = (float)*(uint16_t*)&details.Params[4];
		_radiusFar = (float)*(uint16_t*)&details.Params[6];

		CollisionFlags = CollisionFlags::ForceDisableCollisions;

		co_return true;
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