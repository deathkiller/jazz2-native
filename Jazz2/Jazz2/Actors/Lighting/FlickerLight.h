#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Lighting
{
	class FlickerLight : public ActorBase
	{
	public:
		FlickerLight();

		static void Preload(const ActorActivationDetails& details) { }

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights);

	private:
		float _intensity;
		float _brightness;
		float _radiusNear;
		float _radiusFar;

		float _phase;
	};
}