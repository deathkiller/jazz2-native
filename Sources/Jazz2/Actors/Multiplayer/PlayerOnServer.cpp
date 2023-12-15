#include "PlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/ShotBase.h"
#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Multiplayer
{
	PlayerOnServer::PlayerOnServer()
		: _teamId(0)
	{
	}

	bool PlayerOnServer::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* shotBase = runtime_cast<Weapons::ShotBase*>(other)) {
			std::int32_t strength = shotBase->GetStrength();
			PlayerOnServer* shotOwner = static_cast<PlayerOnServer*>(shotBase->GetOwner());
			// Ignore players in the same team
			if (strength > 0 && _sugarRushLeft <= 0.0f && shotOwner->_teamId != _teamId) {
				// Decrease remaining shield time by 5 secs
				if (_activeShieldTime > (5.0f * FrameTimer::FramesPerSecond)) {
					_activeShieldTime -= (5.0f * FrameTimer::FramesPerSecond);
				} else {
					TakeDamage(strength, 4 * (_pos.X > shotBase->GetPos().X ? 1.0f : -1.0f));
				}
				shotBase->DecreaseHealth(INT32_MAX);
				return true;
			}
		}

		return Player::OnHandleCollision(other);
	}

	std::uint8_t PlayerOnServer::GetTeamId() const
	{
		return _teamId;
	}

	void PlayerOnServer::SetTeamId(std::uint8_t value)
	{
		_teamId = value;
	}
}

#endif