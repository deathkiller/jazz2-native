#pragma once

#include "../../ActorBase.h"
#include "../../LevelInitialization.h"

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

		bool OnHandleCollision(ActorBase* other) override;

		float GetStrength() {
			return _strength;
		}

		Player* GetOwner();
		virtual WeaponType GetWeaponType();

	protected:
		std::shared_ptr<ActorBase> _owner;
		float _timeLeft;
		bool _firedUp;
		uint8_t _upgrades;
		int _strength;
		ActorBase* _lastRicochet;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

		virtual void OnRicochet();
		void CheckCollisions(float timeMult);
		void TryMovement(float timeMult);

	private:
		int _lastRicochetFrame;

	};
}