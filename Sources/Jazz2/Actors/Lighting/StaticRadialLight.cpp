#include "StaticRadialLight.h"
#include "../../ILevelHandler.h"

namespace Jazz2::Actors::Lighting
{
	StaticRadialLight::StaticRadialLight()
	{
	}

	Task<bool> StaticRadialLight::OnActivatedAsync(const ActorActivationDetails& details)
	{
		EventParamsReader params(details);
		_intensity = params.GetUint8(0) / 255.0f;
		_brightness = params.GetUint8(1) / 255.0f;
		_radiusNear = (float)params.GetUint16(2);
		_radiusFar = (float)params.GetUint16(4);

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

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