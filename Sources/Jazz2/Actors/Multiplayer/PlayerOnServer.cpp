#include "PlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/ShotBase.h"
#include "../Weapons/FreezerShot.h"
#include "../../Multiplayer/MpLevelHandler.h"
#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Multiplayer
{
	PlayerOnServer::PlayerOnServer()
		: _lastAttackerTimeout(0.0f)
	{
	}

	void PlayerOnServer::OnUpdate(float timeMult)
	{
		MpPlayer::OnUpdate(timeMult);

		if (_lastAttackerTimeout > 0.0f) {
			_lastAttackerTimeout -= timeMult;
			if (_lastAttackerTimeout <= 0.0f) {
				_lastAttacker = nullptr;
			}
		}
	}

	bool PlayerOnServer::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		// TODO: Check player special move here
		if (auto* weaponOwner = MpLevelHandler::GetWeaponOwner(other.get())) {
			auto* otherPlayerOnServer = static_cast<PlayerOnServer*>(weaponOwner);
			if (_health > 0 && GetPeerDescriptor()->Team != otherPlayerOnServer->GetPeerDescriptor()->Team) {
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
				_lastAttackerTimeout = 300.0f;

				// Decrease remaining shield time by 5 secs
				if (_activeShieldTime > (5.0f * FrameTimer::FramesPerSecond)) {
					_activeShieldTime -= (5.0f * FrameTimer::FramesPerSecond);
				} else if (auto* freezerShot = runtime_cast<Weapons::FreezerShot*>(other.get())) {
					Freeze(3.0f * FrameTimer::FramesPerSecond);
				} else {
					TakeDamage(1, 4.0f * (_pos.X > other->GetPos().X ? 1.0f : -1.0f));
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