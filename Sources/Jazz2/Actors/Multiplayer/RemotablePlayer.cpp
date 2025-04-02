#include "RemotablePlayer.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Multiplayer/MpLevelHandler.h"

#include "../../../nCine/Base/Clock.h"

namespace Jazz2::Actors::Multiplayer
{
	RemotablePlayer::RemotablePlayer(std::shared_ptr<PeerDescriptor> peerDesc)
		: ChangingWeaponFromServer(false), RespawnPending(false), _warpPending(false)
	{
		_peerDesc = std::move(peerDesc);
		_peerDesc->Player = this;
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

		if (_levelExiting != LevelExitingState::None) {
			OnLevelChanging(nullptr, ExitType::None);
		}
	}

	void RemotablePlayer::OnWaterSplash(Vector2f pos, bool inwards)
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

	void RemotablePlayer::SetCurrentWeapon(WeaponType weaponType)
	{
		if (_currentWeapon == weaponType) {
			return;
		}

		Player::SetCurrentWeapon(weaponType);

		if (!ChangingWeaponFromServer) {
			static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerWeaponChanged(this);
		}
	}

	void RemotablePlayer::WarpIn(ExitType exitType)
	{
		if (exitType != (ExitType)0xFF) {
			OnLevelChanging(this, exitType);
			return;
		}

		EndDamagingMove();
		SetState(ActorState::IsInvulnerable, true);
		SetState(ActorState::ApplyGravitation, false);

		SetAnimation(_currentAnimation->State & ~(AnimState::Uppercut | AnimState::Buttstomp));

		_speed.X = 0.0f;
		_speed.Y = 0.0f;
		_externalForce.X = 0.0f;
		_externalForce.Y = 0.0f;
		_internalForceY = 0.0f;
		_fireFramesLeft = 0.0f;
		_copterFramesLeft = 0.0f;
		_pushFramesLeft = 0.0f;
		_warpPending = true;

		// For warping from the water
		_renderer.setRotation(0.0f);

		PlayPlayerSfx("WarpIn"_s);

		SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpInFreefall : AnimState::TransitionWarpIn, false, true, SpecialMoveType::None, [this]() {
			if (_warpPending) {
				_renderer.setDrawEnabled(false);
			}
		});
	}

	void RemotablePlayer::MoveRemotely(Vector2f pos, Vector2f speed)
	{
		Vector2f posPrev = _pos;
		MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
		_speed = speed;

		if (_warpPending) {
			_warpPending = false;
			_trailLastPos = _pos;
			PlayPlayerSfx("WarpOut"_s);

			_levelHandler->HandlePlayerWarped(this, posPrev, WarpFlags::Default);

			_renderer.setDrawEnabled(true);
			_isFreefall |= CanFreefall();
			SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpOutFreefall : AnimState::TransitionWarpOut, false, true, SpecialMoveType::None, [this]() {
				SetState(ActorState::IsInvulnerable, false);
				SetState(ActorState::ApplyGravitation, true);
				_controllable = true;
			});
		} else {
			_levelHandler->HandlePlayerWarped(this, posPrev, WarpFlags::Fast);
		}
	}

	bool RemotablePlayer::Respawn(Vector2f pos)
	{
		bool success = MpPlayer::Respawn(pos);
		if (!success) {
			// Player didn't have enough time to die completely, respawn it when HandlePlayerDied() will be called
			RespawnPending = true;
		}

		return true;
	}
}

#endif