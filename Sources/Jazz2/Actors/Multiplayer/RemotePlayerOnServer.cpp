#include "RemotePlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/ShotBase.h"
#include "../../Multiplayer/MultiLevelHandler.h"
#include "../../../nCine/Base/Clock.h"

namespace Jazz2::Actors::Multiplayer
{
	RemotePlayerOnServer::RemotePlayerOnServer()
		: _stateBufferPos(0)
	{
	}

	Task<bool> RemotePlayerOnServer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		Clock& c = nCine::clock();
		std::uint64_t now = c.now() * 1000 / c.frequency();
		for (std::int32_t i = 0; i < static_cast<std::int32_t>(arraySize(_stateBuffer)); i++) {
			_stateBuffer[i].Time = now - arraySize(_stateBuffer) + i;
			_stateBuffer[i].Pos = Vector2f(details.Pos.X, details.Pos.Y);
		}

		async_return async_await PlayerOnServer::OnActivatedAsync(details);
	}

	bool RemotePlayerOnServer::OnPerish(ActorBase* collider)
	{
		return PlayerOnServer::OnPerish(collider);
	}

	void RemotePlayerOnServer::OnUpdate(float timeMult)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();
		std::int64_t renderTime = now - ServerDelay;

		std::int32_t nextIdx = _stateBufferPos - 1;
		if (nextIdx < 0) {
			nextIdx += static_cast<std::int32_t>(arraySize(_stateBuffer));
		}

		if (renderTime <= _stateBuffer[nextIdx].Time) {
			std::int32_t prevIdx;
			while (true) {
				prevIdx = nextIdx - 1;
				if (prevIdx < 0) {
					prevIdx += static_cast<std::int32_t>(arraySize(_stateBuffer));
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

	bool RemotePlayerOnServer::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		return PlayerOnServer::OnHandleCollision(other);
	}

	void RemotePlayerOnServer::SyncWithServer(const Vector2f& pos, const Vector2f& speed, bool isVisible, bool isFacingLeft, bool isActivelyPushing)
	{
		Clock& c = nCine::clock();
		std::int64_t now = c.now() * 1000 / c.frequency();

		bool wasVisible = _renderer.isDrawEnabled();
		_renderer.setDrawEnabled(isVisible);
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
				stateBufferPrevPos += static_cast<std::int32_t>(arraySize(_stateBuffer));
			}

			std::int64_t renderTime = now - ServerDelay;

			_stateBuffer[stateBufferPrevPos].Time = renderTime;
			_stateBuffer[stateBufferPrevPos].Pos = pos;
			_stateBuffer[_stateBufferPos].Time = renderTime;
			_stateBuffer[_stateBufferPos].Pos = pos;
		}

		_stateBufferPos++;
		if (_stateBufferPos >= static_cast<std::int32_t>(arraySize(_stateBuffer))) {
			_stateBufferPos = 0;
		}

		// TODO: Set actual pos and speed to the newest value
		_pos = pos;
		_speed = speed;
	}

	void RemotePlayerOnServer::OnHitSpring(const Vector2f& pos, const Vector2f& force, bool keepSpeedX, bool keepSpeedY, bool& removeSpecialMove)
	{
		if (!static_cast<Jazz2::Multiplayer::MultiLevelHandler*>(_levelHandler)->HandlePlayerSpring(this, pos, force, keepSpeedX, keepSpeedY)) {
			return;
		}

		PlayerOnServer::OnHitSpring(pos, force, keepSpeedX, keepSpeedY, removeSpecialMove);
	}

	void RemotePlayerOnServer::WarpToPosition(const Vector2f& pos, WarpFlags flags)
	{
		PlayerOnServer::WarpToPosition(pos, flags);

		static_cast<Jazz2::Multiplayer::MultiLevelHandler*>(_levelHandler)->HandlePlayerBeforeWarp(this, pos, flags);
	}

	bool RemotePlayerOnServer::TakeDamage(std::int32_t amount, float pushForce)
	{
		if (!PlayerOnServer::TakeDamage(amount, pushForce)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MultiLevelHandler*>(_levelHandler)->HandlePlayerTakeDamage(this, amount, pushForce);

		return true;
	}

	bool RemotePlayerOnServer::AddAmmo(WeaponType weaponType, std::int16_t count)
	{
		if (!PlayerOnServer::AddAmmo(weaponType, count)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MultiLevelHandler*>(_levelHandler)->HandlePlayerRefreshAmmo(this, weaponType);

		return true;
	}

	void RemotePlayerOnServer::AddWeaponUpgrade(WeaponType weaponType, std::uint8_t upgrade)
	{
		PlayerOnServer::AddWeaponUpgrade(weaponType, upgrade);

		static_cast<Jazz2::Multiplayer::MultiLevelHandler*>(_levelHandler)->HandlePlayerRefreshWeaponUpgrades(this, weaponType);
	}

	bool RemotePlayerOnServer::FireCurrentWeapon(WeaponType weaponType)
	{
		std::uint16_t prevAmmo = _weaponAmmo[(std::int32_t)weaponType];
		if (!PlayerOnServer::FireCurrentWeapon(weaponType)) {
			return false;
		}

		if (prevAmmo != _weaponAmmo[(std::int32_t)weaponType]) {
			static_cast<Jazz2::Multiplayer::MultiLevelHandler*>(_levelHandler)->HandlePlayerRefreshAmmo(this, weaponType);
		}

		return true;
	}

	void RemotePlayerOnServer::SetCurrentWeapon(WeaponType weaponType)
	{
		PlayerOnServer::SetCurrentWeapon(weaponType);

		static_cast<Jazz2::Multiplayer::MultiLevelHandler*>(_levelHandler)->HandlePlayerWeaponChanged(this);
	}
}

#endif