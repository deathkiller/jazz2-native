#include "PlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/ShotBase.h"
#include "../../Multiplayer/MpLevelHandler.h"
#include "../../../nCine/Base/FrameTimer.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::Actors::Multiplayer
{
	std::shared_ptr<PeerDescriptor> MpPlayer::GetPeerDescriptor()
	{
		return _peerDesc;
	}

	std::shared_ptr<const PeerDescriptor> MpPlayer::GetPeerDescriptor() const
	{
		return _peerDesc;
	}

	PlayerOnServer::PlayerOnServer()
	{
	}

	bool PlayerOnServer::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		// TODO: Check player special move here
		if (auto* weaponOwner = MpLevelHandler::GetWeaponOwner(other.get())) {
			auto* otherPlayerOnServer = static_cast<PlayerOnServer*>(weaponOwner);
			if (GetPeerDescriptor()->Team != otherPlayerOnServer->GetPeerDescriptor()->Team) {
				bool otherIsPlayer = false;
				if (auto* anotherPlayer = runtime_cast<PlayerOnServer*>(other)) {
					bool isAttacking = IsAttacking();
					if (!isAttacking && !anotherPlayer->IsAttacking()) {
						return true;
					}
					if (isAttacking) {
						return false;
					}
					otherIsPlayer = true;
				}

				_lastAttacker = otherPlayerOnServer->shared_from_this();

				// Decrease remaining shield time by 5 secs
				if (_activeShieldTime > (5.0f * FrameTimer::FramesPerSecond)) {
					_activeShieldTime -= (5.0f * FrameTimer::FramesPerSecond);
				} else {
					TakeDamage(1, 4 * (_pos.X > other->GetPos().X ? 1.0f : -1.0f));
				}
				if (!otherIsPlayer) {
					other->DecreaseHealth(INT32_MAX);
				}
				return true;
			}
		}

		return Player::OnHandleCollision(other);
	}

	bool PlayerOnServer::CanCauseDamage(ActorBase* collider)
	{
		if (_health > 0) {
			if (auto* weaponOwner = MpLevelHandler::GetWeaponOwner(collider)) {
				return (static_cast<PlayerOnServer*>(weaponOwner)->GetPeerDescriptor()->Team != GetPeerDescriptor()->Team);
			}
		}

		return false;
	}

	bool PlayerOnServer::TakeDamage(std::int32_t amount, float pushForce)
	{
		if (!MpPlayer::TakeDamage(amount, pushForce)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerTakeDamage(this, amount, pushForce);

		return true;
	}

	bool PlayerOnServer::IsAttacking() const
	{
		if (_currentSpecialMove == SpecialMoveType::Buttstomp && _currentTransition != nullptr && _sugarRushLeft <= 0.0f) {
			return false;
		}

		return (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f);
	}
}

#endif