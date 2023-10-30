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
	class ShotBase : public ActorBase
	{
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
		std::shared_ptr<ActorBase> _owner;
		float _timeLeft;
		uint8_t _upgrades;
		int _strength;
		ActorBase* _lastRicochet;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		virtual void OnRicochet();

		void TryMovement(float timeMult, Tiles::TileCollisionParams& params);

	private:
		TimeStamp _lastRicochetTime;
	};
}