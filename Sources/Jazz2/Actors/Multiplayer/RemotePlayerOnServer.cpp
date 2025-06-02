#include "RemotePlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/ShotBase.h"
#include "../../Multiplayer/MpLevelHandler.h"
#include "../../../nCine/Base/Clock.h"

namespace Jazz2::Actors::Multiplayer
{
	RemotePlayerOnServer::RemotePlayerOnServer(std::shared_ptr<PeerDescriptor> peerDesc)
		: _stateBufferPos(0), Flags(PlayerFlags::None), PressedKeys(0),
			PressedKeysLast(0), UpdatedFrame(0)
	{
		_peerDesc = std::move(peerDesc);
		_peerDesc->Player = this;
	}

	Task<bool> RemotePlayerOnServer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Clock& c = nCine::clock();
		std::uint64_t now = c.now() * 1000 / c.frequency();
		for (std::int32_t i = 0; i < std::int32_t(arraySize(_stateBuffer)); i++) {
			_stateBuffer[i].Time = now - arraySize(_stateBuffer) + i;
			_stateBuffer[i].Pos = Vector2f(details.Pos.X, details.Pos.Y);
		}

		async_return async_await PlayerOnServer::OnActivatedAsync(details);
	}

	void RemotePlayerOnServer::OnUpdate(float timeMult)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();
		std::int64_t renderTime = now - ServerDelay;

		std::int32_t nextIdx = _stateBufferPos - 1;
		if (nextIdx < 0) {
			nextIdx += std::int32_t(arraySize(_stateBuffer));
		}

		if (renderTime <= _stateBuffer[nextIdx].Time) {
			std::int32_t prevIdx;
			while (true) {
				prevIdx = nextIdx - 1;
				if (prevIdx < 0) {
					prevIdx += std::int32_t(arraySize(_stateBuffer));
				}

				if (prevIdx == _stateBufferPos || _stateBuffer[prevIdx].Time <= renderTime) {
					break;
				}

				nextIdx = prevIdx;
			}

			std::int64_t timeRange = (_stateBuffer[nextIdx].Time - _stateBuffer[prevIdx].Time);
			if (timeRange > 0) {
				float lerp = (float)(renderTime - _stateBuffer[prevIdx].Time) / timeRange;
				_displayPos = _stateBuffer[prevIdx].Pos + (_stateBuffer[nextIdx].Pos - _stateBuffer[prevIdx].Pos) * lerp;
			} else {
				_displayPos = _stateBuffer[nextIdx].Pos;
			}
		}

		PlayerOnServer::OnUpdate(timeMult);

		_renderer.setPosition(_displayPos);
	}

	bool RemotePlayerOnServer::IsLedgeClimbAllowed() const
	{
		return (_peerDesc->EnableLedgeClimb && PlayerOnServer::IsLedgeClimbAllowed());
	}

	bool RemotePlayerOnServer::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		// TODO: Remove this override
		return PlayerOnServer::OnHandleCollision(other);
	}

	bool RemotePlayerOnServer::OnLevelChanging(Actors::ActorBase* initiator, ExitType exitType)
	{
		LevelExitingState lastState = _levelExiting;
		bool success = PlayerOnServer::OnLevelChanging(initiator, exitType);

		if (lastState == LevelExitingState::None && _health > 0) {
			// Level changing just started, send the request to the player as WarpIn packet
			static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerLevelChanging(this, exitType);
		}

		return success;
	}

	PlayerCarryOver RemotePlayerOnServer::PrepareLevelCarryOver()
	{
		// Skip all remote players
		return PlayerCarryOver{};
	}

	void RemotePlayerOnServer::SyncWithServer(Vector2f pos, Vector2f speed, bool isVisible, bool isFacingLeft, bool isActivelyPushing)
	{
		if (_health <= 0) {
			// Don't sync dead players to avoid cheating
			return;
		}

		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();

		bool wasVisible = _renderer.isDrawEnabled();
		// Visibility is not synced from the client, becuase it should be already almost in sync on the server
		//_renderer.setDrawEnabled(isVisible);
		SetFacingLeft(isFacingLeft);
		_isActivelyPushing = isActivelyPushing;

		if (wasVisible) {
			// Actor is still visible, enable interpolation
			_stateBuffer[_stateBufferPos].Time = now;
			_stateBuffer[_stateBufferPos].Pos = pos;
		} else {
			// Actor was hidden before, reset state buffer to disable interpolation
			std::int32_t stateBufferPrevPos = _stateBufferPos - 1;
			if (stateBufferPrevPos < 0) {
				stateBufferPrevPos += std::int32_t(arraySize(_stateBuffer));
			}

			std::int64_t renderTime = now - ServerDelay;

			_stateBuffer[stateBufferPrevPos].Time = renderTime;
			_stateBuffer[stateBufferPrevPos].Pos = pos;
			_stateBuffer[_stateBufferPos].Time = renderTime;
			_stateBuffer[_stateBufferPos].Pos = pos;
		}

		_stateBufferPos++;
		if (_stateBufferPos >= std::int32_t(arraySize(_stateBuffer))) {
			_stateBufferPos = 0;
		}

		// TODO: Set actual pos and speed to the newest value
		_pos = pos;
		_speed = speed;
	}

	void RemotePlayerOnServer::ForceResyncWithServer(Vector2f pos, Vector2f speed)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();

		_pos = pos;
		_speed = speed;

		for (std::size_t i = 0; i < arraySize(_stateBuffer); i++) {
			_stateBuffer[i].Time = now;
			_stateBuffer[i].Pos = pos;
		}
	}

	void RemotePlayerOnServer::OnHitSpring(Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY, bool& removeSpecialMove)
	{
		if (!static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSpring(this, pos, force, keepSpeedX, keepSpeedY)) {
			return;
		}

		PlayerOnServer::OnHitSpring(pos, force, keepSpeedX, keepSpeedY, removeSpecialMove);
	}

	void RemotePlayerOnServer::WarpToPosition(Vector2f pos, WarpFlags flags)
	{
		PlayerOnServer::WarpToPosition(pos, flags);

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerBeforeWarp(this, pos, flags);
	}

	bool RemotePlayerOnServer::SetModifier(Modifier modifier, const std::shared_ptr<ActorBase>& decor)
	{
		if (!PlayerOnServer::SetModifier(modifier, decor)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetModifier(this, modifier, decor);

		return true;
	}

	bool RemotePlayerOnServer::Freeze(float timeLeft)
	{
		if (!PlayerOnServer::Freeze(timeLeft)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerFreeze(this, timeLeft);

		return true;
	}

	void RemotePlayerOnServer::SetInvulnerability(float timeLeft, InvulnerableType type)
	{
		PlayerOnServer::SetInvulnerability(timeLeft, type);

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetInvulnerability(this, timeLeft, type);
	}

	void RemotePlayerOnServer::AddScore(std::int32_t amount)
	{
		PlayerOnServer::AddScore(amount);

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetScore(this, _score);
	}

	bool RemotePlayerOnServer::AddHealth(std::int32_t amount)
	{
		if (!PlayerOnServer::AddHealth(amount)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetHealth(this, _health);

		return true;
	}

	bool RemotePlayerOnServer::AddLives(std::int32_t count)
	{
		if (!PlayerOnServer::AddLives(count)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetLives(this, _lives);

		return true;
	}

	bool RemotePlayerOnServer::AddAmmo(WeaponType weaponType, std::int16_t count)
	{
		if (!PlayerOnServer::AddAmmo(weaponType, count)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerRefreshAmmo(this, weaponType);

		return true;
	}

	void RemotePlayerOnServer::AddWeaponUpgrade(WeaponType weaponType, std::uint8_t upgrade)
	{
		PlayerOnServer::AddWeaponUpgrade(weaponType, upgrade);

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerRefreshWeaponUpgrades(this, weaponType);
	}

	bool RemotePlayerOnServer::SetDizzy(float timeLeft)
	{
		bool success = PlayerOnServer::SetDizzy(timeLeft);

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetDizzy(this, timeLeft);

		return success;
	}

	bool RemotePlayerOnServer::SetShield(ShieldType shieldType, float timeLeft)
	{
		if (!PlayerOnServer::SetShield(shieldType, timeLeft)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetShield(this, shieldType, timeLeft);

		return true;
	}

	bool RemotePlayerOnServer::IncreaseShieldTime(float timeLeft)
	{
		if (!PlayerOnServer::IncreaseShieldTime(timeLeft)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetShield(this, _activeShield, _activeShieldTime);

		return true;
	}

	bool RemotePlayerOnServer::FireCurrentWeapon(WeaponType weaponType)
	{
		std::uint16_t prevAmmo = _weaponAmmo[(std::int32_t)weaponType];
		bool success = PlayerOnServer::FireCurrentWeapon(weaponType);

		if (prevAmmo != _weaponAmmo[(std::int32_t)weaponType]) {
			static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerRefreshAmmo(this, weaponType);
		}

		return success;
	}

	void RemotePlayerOnServer::EmitWeaponFlare()
	{
		PlayerOnServer::EmitWeaponFlare();

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerEmitWeaponFlare(this);
	}

	void RemotePlayerOnServer::SetCurrentWeapon(WeaponType weaponType, SetCurrentWeaponReason reason)
	{
		PlayerOnServer::SetCurrentWeapon(weaponType, reason);

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerWeaponChanged(this, reason);
	}
}

#endif