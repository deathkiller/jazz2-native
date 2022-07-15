#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Collectibles
{
	class CollectibleBase : public ActorBase
	{
	public:
		CollectibleBase();

	protected:
		bool _untouched;
		uint32_t _scoreValue;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnHandleCollision(ActorBase* other) override;

		virtual void OnCollect(Player* player);

		void SetFacingDirection();

	private:
		float _phase, _timeLeft;
		float _startingY;

	};
}