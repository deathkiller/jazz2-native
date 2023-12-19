#include "RemotablePlayer.h"

#if defined(WITH_MULTIPLAYER)

#include "../../ILevelHandler.h"

#include "../../../nCine/Base/Clock.h"

namespace Jazz2::Actors::Multiplayer
{
	RemotablePlayer::RemotablePlayer()
		: _teamId(0), _warpPending(false)
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

	std::uint8_t RemotablePlayer::GetTeamId() const
	{
		return _teamId;
	}

	void RemotablePlayer::SetTeamId(std::uint8_t value)
	{
		_teamId = value;
	}

	void RemotablePlayer::WarpIn()
	{
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

	void RemotablePlayer::MoveRemotely(const Vector2f& pos, const Vector2f& speed)
	{
		Vector2f posPrev = _pos;
		MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
		_speed = speed;

		if (_warpPending) {
			_warpPending = false;
			_trailLastPos = _pos;
			PlayPlayerSfx("WarpOut"_s);

			_levelHandler->HandlePlayerWarped(this, posPrev, false);

			_renderer.setDrawEnabled(true);
			_isFreefall |= CanFreefall();
			SetPlayerTransition(_isFreefall ? AnimState::TransitionWarpOutFreefall : AnimState::TransitionWarpOut, false, true, SpecialMoveType::None, [this]() {
				SetState(ActorState::IsInvulnerable, false);
				SetState(ActorState::ApplyGravitation, true);
				_controllable = true;
			});
		} else {
			_levelHandler->HandlePlayerWarped(this, posPrev, true);
		}
	}
}

#endif