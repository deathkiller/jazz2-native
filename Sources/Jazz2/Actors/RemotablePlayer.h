#pragma once

#if defined(WITH_MULTIPLAYER)

#include "Player.h"

namespace Jazz2::Actors
{
	class RemotablePlayer : public Player
	{
	public:
		RemotablePlayer();

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;

		void OnWaterSplash(const Vector2f& pos, bool inwards) override;

		bool FireCurrentWeapon(WeaponType weaponType) override;
	};
}

#endif