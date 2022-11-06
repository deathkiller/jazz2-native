#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	class Explosion : public ActorBase
	{
	public:
		enum class Type {
			Tiny = 0,
			TinyBlue = 1,
			TinyDark = 2,
			Small = 3,
			SmallDark = 4,
			Large = 5,

			SmokeBrown = 6,
			SmokeGray = 7,
			SmokeWhite = 8,
			SmokePoof = 9,

			WaterSplash = 10,

			Pepper = 11,
			RF = 12,

			Generator = 20,
		};

		Explosion();

		static void Create(ILevelHandler* levelHandler, const Vector3i& pos, Type type);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		void OnAnimationFinished() override;

	private:
		Type _type;
		float _lightBrightness;
		float _lightIntensity;
		float _lightRadiusNear;
		float _lightRadiusFar;
	};
}