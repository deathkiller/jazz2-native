#pragma once

#include "../SolidObjectBase.h"
#include "../../PlayerType.h"

namespace Jazz2::Actors::Solid
{
	class PowerUpMorphMonitor : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		PowerUpMorphMonitor();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details);

		void DestroyAndApplyToPlayer(Player* player);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdateHitbox() override;
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