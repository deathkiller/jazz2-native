#pragma once

#include "../SolidObjectBase.h"
#include "../../PlayerType.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Power-up morph monitor
		
		A floating monitor that, when broken by the player, transforms them into another character, cycling
		between Jazz, Spaz and Lori (or, in the original game, morphing into a bird).
	*/
	class PowerUpMorphMonitor : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		PowerUpMorphMonitor();

		bool OnHandleCollision(ActorBase* other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Destroys the monitor and applies the morph to the specified player */
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