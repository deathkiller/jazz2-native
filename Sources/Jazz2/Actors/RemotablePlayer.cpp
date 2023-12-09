#include "RemotablePlayer.h"

#if defined(WITH_MULTIPLAYER)

#include "../../nCine/Base/Clock.h"

namespace Jazz2::Actors
{
	RemotablePlayer::RemotablePlayer()
	{
	}

	Task<bool> RemotablePlayer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		async_return async_await Player::OnActivatedAsync(details);
	}

	bool RemotablePlayer::OnPerish(ActorBase* collider)
	{
		return Player::OnPerish(collider);
	}

	void RemotablePlayer::OnUpdate(float timeMult)
	{
		Player::OnUpdate(timeMult);
	}

	void RemotablePlayer::OnWaterSplash(const Vector2f& pos, bool inwards)
	{
		// Already created and broadcasted by the server
	}

	bool RemotablePlayer::FireCurrentWeapon(WeaponType weaponType)
	{
		if (_weaponCooldown > 0.0f) {
			return (weaponType != WeaponType::TNT);
		}

		return true;
	}
}

#endif