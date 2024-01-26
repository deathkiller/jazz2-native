#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Lighting
{
	class StaticRadialLight : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		StaticRadialLight();

		static void Preload(const ActorActivationDetails& details) { }

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
		float _intensity;
		float _brightness;
		float _radiusNear;
		float _radiusFar;
	};
}