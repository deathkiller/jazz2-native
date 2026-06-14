#pragma once

#include "ActorBase.h"

namespace Jazz2::Actors
{
	/**
		@brief Explosion effects
		
		A transient visual effect actor spawned when something is destroyed, such as a projectile impact,
		an enemy death or a smoke/debris puff. It plays a single animation and then disappears. Covers
		several explosion and smoke variants (see @ref Type).
	*/
	class Explosion : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Explosion effect type */
		enum class Type {
			Tiny,				/**< Tiny explosion */
			TinyBlue,			/**< Tiny blue explosion */
			TinyDark,			/**< Tiny dark explosion */
			Small,				/**< Small explosion */
			SmallDark,			/**< Small dark explosion */
			Large,				/**< Large explosion */

			SmokeBrown,			/**< Brown smoke */
			SmokeGray,			/**< Gray smoke */
			SmokeWhite,			/**< White smoke */
			SmokePoof,			/**< Smoke poof */

			WaterSplash,		/**< Water splash */

			Pepper,				/**< Pepper spray impact */
			RF,					/**< RF missile explosion */
			RFUpgraded,			/**< Upgraded RF missile explosion */

			Generator,			/**< Generator (de)activation effect */

			IceShrapnel,		/**< Ice shrapnel */
		};

		/** @brief Creates a new instance */
		Explosion();

		/** @brief Spawns an explosion effect of a given type at a given position */
		static void Create(ILevelHandler* levelHandler, const Vector3i& pos, Type type, float scale = 1.0f);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		void OnAnimationFinished() override;

	private:
		Type _type;
		float _lightBrightness;
		float _lightIntensity;
		float _lightRadiusNear;
		float _lightRadiusFar;
		float _scale;
		float _time;
	};
}