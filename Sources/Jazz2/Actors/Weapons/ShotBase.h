#pragma once

#include "../ActorBase.h"
#include "../../WeaponType.h"

#include "../../../nCine/Base/TimeStamp.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Weapons
{
	/** @brief Base class of a shot from a player's weapon */
	class ShotBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		ShotBase();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		/** @brief Returns strength (damage) */
		inline std::int32_t GetStrength() {
			return _strength;
		}

		/** @brief Returns owner of the shot */
		Player* GetOwner();
		/** @brief Returns weapon type */
		virtual WeaponType GetWeaponType();
		/** @brief Triggers shot ricochet */
		void TriggerRicochet(ActorBase* other);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		std::shared_ptr<ActorBase> _owner;
		float _timeLeft;
		std::uint8_t _upgrades;
		std::int32_t _strength;
		ActorBase* _lastRicochet;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		/** @brief Called on shot ricochet */
		virtual void OnRicochet();

		void TryMovement(float timeMult, Tiles::TileCollisionParams& params);

	private:
		TimeStamp _lastRicochetTime;
	};
}