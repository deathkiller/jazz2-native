#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Collectibles
{
	/** @brief Base class of a collectible object */
	class CollectibleBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		CollectibleBase();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	protected:
		/** @{ @name Constants */

		static constexpr std::int32_t IlluminateLightCount = 20;

		/** @} */

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		struct IlluminateLight {
			float Intensity;
			float Distance;
			float Phase;
			float Speed;
		};

		bool _untouched;
		std::int32_t _scoreValue;
		float _timeLeft;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

		/** @brief Called when the collectible is collected */
		virtual void OnCollect(Player* player);

		/** @brief Sets facing direction */
		void SetFacingDirection(bool inverse = false);

	private:
		float _phase;
		float _startingY;
		SmallVector<IlluminateLight, 0> _illuminateLights;
	};
}