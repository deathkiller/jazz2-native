#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Collectibles
{
	class CollectibleBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		CollectibleBase();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	protected:
		static constexpr int IlluminateLightCount = 20;

		struct IlluminateLight {
			float Intensity;
			float Distance;
			float Phase;
			float Speed;
		};

		bool _untouched;
		uint32_t _scoreValue;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

		virtual void OnCollect(Player* player);

		void SetFacingDirection();

	private:
		float _phase, _timeLeft;
		float _startingY;
		SmallVector<IlluminateLight, 0> _illuminateLights;
	};
}