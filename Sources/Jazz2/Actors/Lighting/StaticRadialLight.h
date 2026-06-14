#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Lighting
{
	/**
		@brief Static radial light
		
		Invisible light-emitter actor placed in a level that contributes a constant circular light to the dynamic
		lighting layer. It has no sprite and disables all collisions; its intensity, brightness and near/far radii
		stay fixed for its lifetime.
	*/
	class StaticRadialLight : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		StaticRadialLight();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details) {}

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