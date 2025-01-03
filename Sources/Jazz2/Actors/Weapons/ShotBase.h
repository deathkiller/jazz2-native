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

		inline int GetStrength() {
			return _strength;
		}

		Player* GetOwner();
		virtual WeaponType GetWeaponType();
		void TriggerRicochet(ActorBase* other);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		std::shared_ptr<ActorBase> _owner;
		float _timeLeft;
		uint8_t _upgrades;
		int _strength;
		ActorBase* _lastRicochet;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		virtual void OnRicochet();

		void TryMovement(float timeMult, Tiles::TileCollisionParams& params);

	private:
		TimeStamp _lastRicochetTime;
	};
}