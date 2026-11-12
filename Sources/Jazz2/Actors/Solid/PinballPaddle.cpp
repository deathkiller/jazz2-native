#include "PinballPaddle.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Solid
{
	PinballPaddle::PinballPaddle()
		: _cooldown(0.0f)
	{
	}

	void PinballPaddle::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/PinballPaddle"_s);
	}

	Task<bool> PinballPaddle::OnActivatedAsync(const ActorActivationDetails& details)
	{
		bool facingLeft = (details.Params[0] != 0);

		// Deliberately **not** a solid object, even though the player stands on one. Two reasons, both
		// observed: a solid object overhead makes the player think they are carrying it, which locks them
		// into the lift pose and stops them moving while they are merely standing underneath; and a solid
		// box cannot be sloped, while the original's paddle surface drops a consistent 0.3 px for every
		// pixel away from its mounted end. The paddle carries the player itself in OnUpdate() instead.
		// Nor can it be frozen: it is part of the table, so a freezer shot passes through it like it does through
		// a checkpoint, instead of stopping on it and switching the paddle off.
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation | ActorState::IsSolidObject | ActorState::CanBeFrozen, false);

		async_await RequestMetadataAsync("Object/PinballPaddle"_s);

		SetFacingLeft(facingLeft);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void PinballPaddle::OnUpdate(float timeMult)
	{
		// Nothing is caught while a launch is in flight, it would snap the player straight back down
		if (_cooldown <= 0.0f) {
			// The catch area follows the current frame of the swing, the idle paddle shows the first one
			std::int32_t frame = 0;
			if (_currentTransition != nullptr) {
				frame = std::clamp<std::int32_t>(_renderer.CurrentFrame - _renderer.FirstFrame, 0, arraySize(LegacyFrames) - 1);
			}
			const LegacyFrame& f = LegacyFrames[frame];
			bool mountedLeft = !IsFacingLeft();
			// Measured in JJ2+: the paddle's origin is 15 px into its tile, and our player stands 1 px higher than there
			Vector2f origin = Vector2f(std::floor(_pos.X / 32.0f) * 32.0f + 15.0f, std::floor(_pos.Y / 32.0f) * 32.0f + 14.0f);
			float pivotX = origin.X + (mountedLeft ? -LegacyPivotOffset : LegacyPivotOffset);
			// Only the sprite and 5 px past its tip, nothing behind the pivot
			float tipX = pivotX + (mountedLeft ? f.Width + 5.0f : -(f.Width + 5.0f));
			float top = origin.Y + f.HotspotY - 16.0f;
			float bottom = origin.Y + f.HotspotY + f.Height + 6.0f;

			for (auto* player : _levelHandler->GetPlayers()) {
				Vector2f playerPos = player->GetPos();
				bool insideX = (mountedLeft ? (playerPos.X > pivotX && playerPos.X < tipX) : (playerPos.X < pivotX && playerPos.X > tipX));
				if (!insideX || playerPos.Y <= top || playerPos.Y >= bottom) {
					continue;
				}

				// The surface slopes down from the pivot, 16 px over the paddle's length, and catches from 8 px above it
				float surfaceY = top + std::abs(playerPos.X - pivotX) * 16.0f / 53.0f;
				if (playerPos.Y <= surfaceY - 8.0f) {
					continue;
				}

				// From below too: the player is lifted onto the top
				player->MoveInstantly(Vector2f(0.0f, surfaceY - playerPos.Y), MoveType::Relative);
				player->_speed.Y = 0.0f;

				// Curled into a ball while on it, which also blocks shooting
				player->_onPinballPaddleTime = PaddleHoldTime;

				// It fires only while Jump is held, standing on it does nothing
				if (_cooldown <= 0.0f && _levelHandler->PlayerActionPressed(player, PlayerAction::Jump)) {
					// The original has no cooldown, a weak launch is caught again and fires again while Jump is held
					if (_levelHandler->IsReforged()) {
						_cooldown = 10.0f;
						// The paddle doesn't look at the player while it cools down, the pose mustn't lapse meanwhile
						player->_onPinballPaddleTime = _cooldown + PaddleHoldTime;
					}

					bool newSwing = (_currentTransition == nullptr);
					if (newSwing) {
						SetTransition(AnimState::TransitionActivate, false);
					}

					player->EndDamagingMove();
					// The rise reads the live key, a release remembered from before would cut it short
					player->_jumpReleased = false;

					if (_levelHandler->IsReforged()) {
						PlaySfx("Hit"_s, 0.6f, 0.4f);
						player->_speed.X = 0.0f;
						float mult = (playerPos.X - _pos.X) / _currentAnimation->Base->FrameDimensions.X;
						if (IsFacingLeft()) {
							mult = 1.0f - mult;
						}
						mult = std::clamp(0.2f + mult * 1.6f, 0.4f, 1.0f);

						player->_speed.Y = -1.0f;

						if (player->_activeModifier == Player::Modifier::None) {
							if (player->_copterFramesLeft > 1.0f) {
								player->_copterFramesLeft = 1.0f;
							}

							player->_externalForce.Y -= 1.9f * mult;
							player->_externalForceCooldown = 10.0f;
							player->SetState(ActorState::CanJump, false);
						}
					} else {
						// Measured from the paddle's middle: past it, up by 1 px/tick per px and away by 0.25 + 1/32 per px,
						// behind it a flat 4 up and sideways at half the distance
						// Not mirrored behind the middle, both paddles push the player left there
						float m = std::trunc(playerPos.X - origin.X);
						bool pastMiddle = (mountedLeft ? m > 0.0f : m < 0.0f);
						float speedX, speedY;
						if (pastMiddle) {
							speedX = (mountedLeft ? 0.25f : -0.25f) + m / 32.0f;
							speedY = -std::abs(m);
							if (newSwing) {
								PlaySfx("Hit"_s, 0.6f, 0.4f);
							}
						} else {
							speedX = -std::abs(m) * 0.5f;
							speedY = -LegacyMinLaunch;
						}
						player->_speed.X = speedX * Player::LegacyFrameRateScale;
						player->_speed.Y = speedY * Player::LegacyFrameRateScale;
						// The drift is left alone for a few ticks before the air brake takes it
						player->_brakeHoldLeft = LegacyDriftHoldTicks / Player::LegacyFrameRateScale;
						// Gravity is off for the first ticks of the launch
						player->_springRiseHoldLeft = LegacyRiseHoldTicks / Player::LegacyFrameRateScale;
						player->_tubeBallPoseLeft = Player::TubeBallPoseTime;

						if (player->_activeModifier == Player::Modifier::None) {
							// End a copter properly, Jazz's copter switches gravity off (the ball pose replaces its animation)
							player->_copterFramesLeft = 0.0f;
							player->SetState(ActorState::ApplyGravitation, true);

							player->_externalForceCooldown = 10.0f;
							player->SetState(ActorState::CanJump, false);
						}
					}

					// Resync the paddle launch to the owning client, otherwise remote players aren't affected in multiplayer
					_levelHandler->HandlePlayerPushed(player);
				}
			}
		}

		if (_cooldown > 0.0f) {
			_cooldown -= timeMult;
		}
	}

	void PinballPaddle::OnUpdateHitbox()
	{
		if (_currentAnimation != nullptr) {
			AABBInner = AABBf(
				_pos.X - _currentAnimation->Base->FrameDimensions.X * (IsFacingLeft() ? LengthAway : LengthTowards),
				_pos.Y - _currentAnimation->Base->FrameDimensions.Y * 0.1f,
				_pos.X + _currentAnimation->Base->FrameDimensions.X * (IsFacingLeft() ? LengthTowards : LengthAway),
				_pos.Y + _currentAnimation->Base->FrameDimensions.Y * 0.3f
			);
		}
	}
}