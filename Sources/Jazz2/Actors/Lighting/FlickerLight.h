#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Lighting
{
	/** @brief Flickering light */
	class FlickerLight : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		FlickerLight();

		static void Preload(const ActorActivationDetails& details) {}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct LightPart {
			Vector2f Pos;
			float Radius;
			float Phase;
		};
#endif

		static constexpr std::int32_t LightPartCount = 16;

		float _intensity;
		float _brightness;
		float _radiusNear;
		float _radiusFar;

		float _phase;
		SmallVector<LightPart, LightPartCount> _parts;
	};
}