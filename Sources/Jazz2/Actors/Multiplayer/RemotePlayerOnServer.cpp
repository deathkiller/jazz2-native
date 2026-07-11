#include "RemotePlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/ShotBase.h"
#include "../../Multiplayer/MpLevelHandler.h"

namespace Jazz2::Actors::Multiplayer
{
	RemotePlayerOnServer::RemotePlayerOnServer(std::shared_ptr<PeerDescriptor> peerDesc)
		: Flags(PlayerFlags::None), PressedKeys(0),
			PressedKeysLast(0), UpdatedFrame(0)
	{
		_peerDesc = std::move(peerDesc);
		_peerDesc->Player = this;
		// Use the peer's chosen fur color (the base Player ctor defaulted it to the local/host PreferencesCache value)
		_furColor = _peerDesc->FurColor;
	}

	Task<bool> RemotePlayerOnServer::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_stateBuffer.Reset(Vector2f(details.Pos.X, details.Pos.Y), StateInterpolationBuffer::Now());
		// Seed the interpolated display position too, so a hitch before the first OnUpdate interpolation doesn't
		// render the player at the level origin (0, 0)
		_displayPos = Vector2f(details.Pos.X, details.Pos.Y);

		async_return async_await PlayerOnServer::OnActivatedAsync(details);
	}

	void RemotePlayerOnServer::OnUpdate(float timeMult)
	{
		std::int64_t renderTime = StateInterpolationBuffer::Now() - StateInterpolationBuffer::ServerDelay;
		_stateBuffer.Sample(renderTime, _displayPos);

		// Ground this server-side shadow on the player it stands on (if any) so it doesn't apply gravity and play a
		// falling animation - its position comes from the owning client, so don't reposition it (snap = false).
		UpdatePlayerStacking(timeMult, /*snap:*/ false);

		PlayerOnServer::OnUpdate(timeMult);

		_renderer.setPosition(_displayPos);

		// The owning client predicts this player locally and can't tell when another player stands on it, so push the
		// server-authoritative "being stood on" state to it (only on change) to drive its cosmetic lift animation.
		if (_beingStoodOn != _beingStoodOnLastSent) {
			_beingStoodOnLastSent = _beingStoodOn;
			static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerSetBeingStoodOn(this, _beingStoodOn);
		}
	}

	bool RemotePlayerOnServer::IsContinuousJumpAllowed() const
	{
		return (Flags & PlayerFlags::EnableContinuousJump) == PlayerFlags::EnableContinuousJump;
	}

	bool RemotePlayerOnServer::IsLedgeClimbAllowed() const
	{
		return (_peerDesc->EnableLedgeClimb && PlayerOnServer::IsLedgeClimbAllowed());
	}

	bool RemotePlayerOnServer::OnHandleCollision(ActorBase* other)
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

	void RemotePlayerOnServer::SyncWithServer(Vector2f pos, Vector2f speed, PlayerFlags flags)
	{
		if (_health <= 0) {
			// Don't sync dead players to avoid cheating
			return;
		}

		Flags = flags;

		bool wasVisible = _renderer.isDrawEnabled();
		// Visibility is not synced from the client, because it should be already almost in sync on the server
		//_renderer.setDrawEnabled((flags & PlayerFlags::IsVisible) == PlayerFlags::IsVisible);
		SetFacingLeft((flags & PlayerFlags::IsFacingLeft) == PlayerFlags::IsFacingLeft);
		_isActivelyPushing = (flags & PlayerFlags::IsActivelyPushing) == PlayerFlags::IsActivelyPushing;

		_stateBuffer.Push(pos, StateInterpolationBuffer::Now(), wasVisible);

		// TODO: Set actual pos and speed to the newest value
		_pos = pos;
		_speed = speed;
	}

	void RemotePlayerOnServer::ForceResyncWithServer(Vector2f pos, Vector2f speed)
	{
		_pos = pos;
		_speed = speed;

		_stateBuffer.Reset(pos, StateInterpolationBuffer::Now());
	}

	void RemotePlayerOnServer::OnPushSolidObject(float timeMult, float pushSpeedX)
	{
		if (!static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerPush(this, pushSpeedX)) {
			return;
		}

		PlayerOnServer::OnPushSolidObject(timeMult, pushSpeedX);
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

	bool RemotePlayerOnServer::FireCurrentWeapon(WeaponType weaponType)
	{
		std::uint16_t prevAmmo = _inventory.WeaponAmmo[(std::int32_t)weaponType];
		bool success = PlayerOnServer::FireCurrentWeapon(weaponType);

		if (prevAmmo != _inventory.WeaponAmmo[(std::int32_t)weaponType]) {
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