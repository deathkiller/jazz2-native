#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	class PinballBumper : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		PinballBumper();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
		float _cooldown;
		float _lightIntensity;
		float _lightBrightness;
	};
}