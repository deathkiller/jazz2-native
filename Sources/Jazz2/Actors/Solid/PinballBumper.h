#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	/**
	 * @brief Pinball bumper
	 *
	 * A round bumper found in pinball-themed levels that flashes and forcefully knocks the player away in the
	 * direction they struck it, awarding score, much like the bumpers on a real pinball table.
	 */
	class PinballBumper : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		PinballBumper();

		/** @brief Preloads all assets required by this actor */
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