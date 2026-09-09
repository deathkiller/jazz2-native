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
		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation | ActorState::IsSolidObject, false);

		async_await RequestMetadataAsync("Object/PinballPaddle"_s);

		SetFacingLeft(facingLeft);
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	void PinballPaddle::OnUpdate(float timeMult)
	{
		if (_frozenTimeLeft > 0.0f) {
			return;
		}

		// Nothing is carried while a launch is in flight. The player is still well inside the catch band on
		// the frame after one - they have moved about eight pixels - so carrying them there would snap them
		// straight back down onto the surface and undo the launch entirely. The cooldown is long enough for
		// them to clear it.
		if (_cooldown <= 0.0f) {
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, CatchRadius, [this](ActorBase* actor) {
				if (auto* player = runtime_cast<Player>(actor)) {
					Vector2f playerPos = player->GetPos();

					// Which point of the paddle the player is over, measured from its mounted end
					float mountX = _pos.X + (IsFacingLeft() ? LegacyMountOffset : -LegacyMountOffset);
					float distance = std::abs(playerPos.X - mountX);
					if (distance > LegacyMaxDistance) {
						// Past the far end - the original lets the player fall straight by from 64 px out
						return true;
					}

					// The surface slopes **down** away from the mount, a consistent 0.3 px per px: measured
					// resting heights of 571.2, 573.6, 576.1, 578.5, 580.9 and 583.3 at 16 to 56 px out, and
					// 0.6 lower than that on the mirrored paddle. At the mount it is the paddle's own centre.
					//
					// Carried here rather than left to a solid object, because an axis-aligned box cannot be
					// sloped at all - and because a solid object *above* the player makes them think they are
					// carrying it, which pins them in the lift pose and stops them moving while they are only
					// standing underneath one.
					float surfaceY = _pos.Y + LegacySlope * std::max(0.0f, distance - LegacyMountOffset);
					float feet = player->AABBInner.B;
					if (feet < surfaceY - CatchAbove || feet > surfaceY + CatchBelow) {
						return true;
					}

					// Put them on it, and take the fall out. Rising into the underside lands here too, which
					// is what the original does - driven up from below it does not block the player at the
					// bottom but lifts them out onto the top and parks them there.
					// Both directions, not just the fall: driven up from below the original parks the player on
					// the surface and keeps them there while its own `ys` column still reads -17 and falling,
					// the same reported-speed-against-travel decoupling the buttstomp drift shows. Zeroing is
					// the practical form of that - leaving the upward speed alone sent the player straight on
					// through, 9 px a tick, after being placed correctly.
					player->MoveInstantly(Vector2f(0.0f, surfaceY - feet), MoveType::Relative);
					player->_speed.Y = 0.0f;

					// The pose, and it is not a launch effect: the original has the player in the sucker
					// tube's curled-up ball for as long as they are *standing* on a paddle, showing animation
					// 58 several ticks before the jump press that fires it. Re-armed every frame the paddle
					// can see them - see `Player::_onPinballPaddleTime`, which is what UpdateAnimation()
					// reads. An earlier attempt set the animation from here directly and it never survived a
					// frame.
					player->_onPinballPaddleTime = PaddleHoldTime;

					// A paddle is scenery until the player asks it to fire. Measured against the original:
					// dropped onto one with nothing pressed, the player lands at tick 53 and stays exactly
					// where they landed for the remaining 746 ticks of the scenario. Both paddles behave that
					// way, and this engine did the opposite - it fired on contact and again every time the
					// player came back down, which made it a trampoline that never stops.
					//
					// The key has to be **held**, not newly hit, which is what the original does: with jump
					// down to the end `pb_pad_hold` fires eight times, because the key is still down each
					// time the player lands back on it, while `pb_pad_h20` - the same key let go after
					// twenty ticks - fires exactly once. Not gated on Reforged: the original has no
					// self-firing paddle in either mode and a paddle that launches whatever the player does
					// is a bug rather than a design difference.
					if (_cooldown <= 0.0f && _levelHandler->PlayerActionPressed(player, PlayerAction::Jump)) {
						_cooldown = 10.0f;

						SetTransition(AnimState::TransitionActivate, false);
						PlaySfx("Hit"_s, 0.6f, 0.4f);

						player->EndDamagingMove();

						// The rise that follows is governed by `_jumpReleased`, and a paddle only ever fires
						// while the key is **held** - so it is false by definition at this instant and has to
						// say so. Without this the flag stays set from any earlier release, because only an
						// ordinary jump clears it, and every later launch off a paddle climbs at the heavy
						// released rate: the player gets one tall launch, then short ones until they touch
						// the floor and jump to reset it.
						//
						// Letting go *after* the launch still shortens it, which is correct - measured, the
						// original rises 290.9 px when the key is released four ticks in against 580.9 px
						// held, and this engine 272.0 against 597.3. What was wrong was that a release stayed
						// remembered. The original picks the rate from the live key state; ours from a sticky
						// flag, which is the same mismatch the float-up exit gap describes.
						player->_jumpReleased = false;

						if (_levelHandler->IsReforged()) {
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
							// A straight speed assignment, **linear** in the distance from the mounted end, with
							// a floor near the mount. Measured over both paddles eight pixels at a time, in the
							// original's own units:
							//
							//   distance | 24 | 32 | 40 | 48 | 56 | 64
							//   right    | -7 |-15 |-23 |-31 |-39 | off the end
							//   left     | -9 |-17 |-25 |-33 |-41 | off the end
							//
							// Exactly eight per eight pixels, so one per pixel, and -4 nearer in than that.
							// The two paddles come out a constant two apart, which is a mirrored pivot landing
							// on a different pixel rather than a different rule - this splits the difference
							// and is within one unit of either. What it replaces was a curve that saturated
							// almost immediately: `clamp(0.2 + mult * 1.6, 0.4, 1)` reached its ceiling a few
							// pixels out, so every offset from 24 to 56 launched at the same -3.54.
							float mountX = _pos.X + (IsFacingLeft() ? LegacyMountOffset : -LegacyMountOffset);
							float distance = std::abs(playerPos.X - mountX);
							float launch = std::max(LegacyMinLaunch, distance - LegacyLaunchOffset);
							player->_speed.Y = -launch * Player::LegacyFrameRateScale;

							// The launch also carries the player a little further *away* from the mount, and
							// that is not a detail - it is the whole reason a held key pumps. Each bounce
							// lands further out, the rule above is steeper there, and so the next one is
							// stronger: measured over a held press the original's launches run -4, -7.1,
							// -9.7, -12.9, -17.0, -22.2, -29.2 while the player walks from 16 px out to 46,
							// which is exactly what `distance - 17` gives at each of those positions. Without
							// the drift the player stays where they landed and this engine fired a flat -4
							// twenty-six times over without ever leaving the paddle.
							//
							// Linear too, and it splits the two paddles the same way: measured -0.4688 to
							// -1.4688 on the right one and +0.5313 to +1.5313 on the left, over 24 to 56 px,
							// which is one thirty-second of a pixel per pixel either side of `(d - 8) / 32`.
							float drift = std::max(LegacyMinDrift, (distance - LegacyDriftOffset) * LegacyDriftScale);
							player->_speed.X = (IsFacingLeft() ? -drift : drift) * Player::LegacyFrameRateScale;

							if (player->_activeModifier == Player::Modifier::None) {
								// End a copter properly rather than just shortening it, which is what a spring
								// does. Jazz's copter switches gravity **off** and holds a constant descent,
								// and merely setting `_copterFramesLeft` does not turn it back on - so a
								// player who coptered on the way down and was then launched kept the launch
								// speed for ever. Measured before this: `ys` frozen at -37.33 for 426 ticks
								// and 2836 px above the top of the level, still climbing.
								if (player->_copterFramesLeft > 0.0f) {
									player->_copterFramesLeft = 0.0f;
									player->SetAnimation(player->_currentAnimation->State & ~AnimState::Copter);
								}
								player->SetState(ActorState::ApplyGravitation, true);

								player->_externalForceCooldown = 10.0f;
								player->SetState(ActorState::CanJump, false);
							}
						}

						// TODO: Check this
						player->AddScore(500);

						// Resync the paddle launch to the owning client, otherwise remote players aren't affected in multiplayer
						_levelHandler->HandlePlayerPushed(player);
					}
				}
				return true;
			});
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