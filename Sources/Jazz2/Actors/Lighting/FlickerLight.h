#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Lighting
{
	class FlickerLight : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		FlickerLight();

		static void Preload(const ActorActivationDetails& details) { }

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
		struct LightPart {
			Vector2f Pos;
			float Radius;
			float Phase;
		};

		static constexpr int LightPartCount = 16;

		float _intensity;
		float _brightness;
		float _radiusNear;
		float _radiusFar;

		float _phase;
		SmallVector<LightPart, LightPartCount> _parts;
	};
}