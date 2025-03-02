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
		_radiusNear1 = (float)*(std::uint16_t*)&details.Params[2];
		_radiusNear2 = (float)*(std::uint16_t*)&details.Params[4];
		_radiusFar = (float)*(std::uint16_t*)&details.Params[6];
		_speed = details.Params[8] * 0.0072f;
		std::uint8_t sync = details.Params[9];

		_phase = sync * fPiOver2 + _speed * _levelHandler->GetElapsedFrames();

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		async_return true;
	}

	void PulsatingRadialLight::OnUpdate(float timeMult)
	{
		_phase += _speed * timeMult;
	}

	void PulsatingRadialLight::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		auto& light = lights.emplace_back();
		light.Pos = _pos;
		light.Intensity = _intensity;
		light.Brightness = _brightness;
		light.RadiusNear = _radiusNear1 + sinf(_phase) * _radiusNear2;
		light.RadiusFar = light.RadiusNear + _radiusFar;
	}
}