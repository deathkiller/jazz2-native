#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Lighting
{
	class PulsatingRadialLight : public ActorBase
	{
	public:
		PulsatingRadialLight();

		static void Preload(const ActorActivationDetails& details) { }

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights);

	private:
		static constexpr float BaseCycleFrames = 700.0f;

		float _intensity;
		float _brightness;
		float _radiusNear1;
		float _radiusNear2;
		float _radiusFar;
		float _speed;
		float _phase;
	};
}