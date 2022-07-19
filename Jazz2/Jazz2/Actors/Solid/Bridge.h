#pragma once

#include "../../ActorBase.h"

namespace Jazz2::Actors::Solid
{
	enum class BridgeType {
		Rope = 0,
		Stone = 1,
		Vine = 2,
		StoneRed = 3,
		Log = 4,
		Gem = 5,
		Lab = 6,

		Last = Lab
	};

	class Bridge : public ActorBase
	{
	public:
		Bridge();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;

	private:
		float _originalY;
		BridgeType _bridgeType;
		int _bridgeWidth;
		float _heightFactor;
	};
}