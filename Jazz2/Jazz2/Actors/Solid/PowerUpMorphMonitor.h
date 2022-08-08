#pragma once

#include "../SolidObjectBase.h"

namespace Jazz2::Actors::Solid
{
	class PowerUpMorphMonitor : public SolidObjectBase
	{
	public:
		PowerUpMorphMonitor();

		static void Preload(const ActorActivationDetails& details);

		void DestroyAndApplyToPlayer(Player* player);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool OnPerish(ActorBase* collider) override;

	private:
		enum class MorphType {
			Swap2,
			Swap3,
			ToBird
		};

		MorphType _morphType;

		std::optional<PlayerType> GetTargetType(PlayerType currentType);
	};
}