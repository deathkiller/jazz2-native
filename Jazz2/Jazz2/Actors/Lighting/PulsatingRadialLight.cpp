#include "PulsatingRadialLight.h"
#include "../../ILevelHandler.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Lighting
{
	PulsatingRadialLight::PulsatingRadialLight()
	{
	}

	Task<bool> PulsatingRadialLight::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_intensity = details.Params[0] / 255.0f;
		_brightness = details.Params[1] / 255.0f;
		_radiusNear1 = (float)*(uint16_t*)&details.Params[2];
		_radiusNear2 = (float)*(uint16_t*)&details.Params[4];
		_radiusFar = (float)*(uint16_t*)&details.Params[6];
		_speed = details.Params[8] * 0.6f;
		uint8_t sync = details.Params[9];

		_phase = fmodf(BaseCycleFrames - ((float)(fmodf(theApplication().numFrames() * FrameTimer::FramesPerSecond, BaseCycleFrames)) + sync * 175), BaseCycleFrames);

		CollisionFlags = CollisionFlags::ForceDisableCollisions;

		co_return true;
	}

	void PulsatingRadialLight::OnUpdate(float timeMult)
	{
		_phase = fmodf(_phase + _speed * timeMult, BaseCycleFrames);
	}

	void PulsatingRadialLight::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = _intensity;
		light.Brightness = _brightness;
		light.RadiusNear = _radiusNear1 + sinf(fTwoPi * _phase / BaseCycleFrames) * _radiusNear2;
		light.RadiusFar = light.RadiusNear + _radiusFar;
	}
}