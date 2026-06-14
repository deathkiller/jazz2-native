#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Lighting
{
	/** @brief Pulsating radial light */
	class PulsatingRadialLight : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		PulsatingRadialLight();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details) {}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
		float _intensity;
		float _brightness;
		float _radiusNear1;
		float _radiusNear2;
		float _radiusFar;
		float _speed;
		float _phase;
	};
}