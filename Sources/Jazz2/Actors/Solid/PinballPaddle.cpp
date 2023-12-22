#include "PinballPaddle.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Solid
{
	PinballPaddle::PinballPaddle()
		:
		_cooldown(0.0f)
	{
	}

	void PinballPaddle::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/PinballPaddle"_s);
	}

	Task<bool> PinballPaddle::OnActivatedAsync(const ActorActivationDetails& details)
	{
		bool facingLeft = (details.Params[0] != 0);

		SetState(ActorState::CollideWithTileset | ActorState::IsSolidObject | ActorState::ApplyGravitation, false);

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

		if (_cooldown <= 0.0f) {
			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 30.0f, [this](ActorBase* actor) {
				if (auto* player = runtime_cast<Player*>(actor)) {
					Vector2f playerPos = player->GetPos();
					if (playerPos.Y > _pos.Y - 26.0f && playerPos.Y < _pos.Y + 10.0f) {
						_cooldown = 10.0f;

						SetTransition(AnimState::TransitionActivate, false);
						PlaySfx("Hit"_s, 0.6f, 0.4f);

						float mult = (playerPos.X - _pos.X) / _currentAnimation->Base->FrameDimensions.X;
						if (IsFacingLeft()) {
							mult = 1.0f - mult;
						}
						mult = std::clamp(0.2f + mult * 1.6f, 0.4f, 1.0f);

						float forceY = 1.9f * mult;
						if (!_levelHandler->IsReforged()) {
							forceY *= 0.85f;
						}

						player->_speed.X = 0.0f;
						player->_speed.Y = (_levelHandler->IsReforged() ? -1.0f : -0.7f);

						if (player->_activeModifier == Player::Modifier::None) {
							if (player->_copterFramesLeft > 1.0f) {
								player->_copterFramesLeft = 1.0f;
							}

							player->_externalForce.Y -= forceY;
							player->_externalForceCooldown = 10.0f;
							player->_controllable = true;
							player->SetState(ActorState::CanJump, false);
							player->EndDamagingMove();
						}

						// TODO: Check this
						player->AddScore(500);
					}
				}
				return true;
			});
		} else {
			_cooldown -= timeMult;
		}
	}

	void PinballPaddle::OnUpdateHitbox()
	{
		if (_currentAnimation != nullptr) {
			AABBInner = AABBf(
				_pos.X - _currentAnimation->Base->FrameDimensions.X * (IsFacingLeft() ? 0.7f : 0.3f),
				_pos.Y - _currentAnimation->Base->FrameDimensions.Y * 0.1f,
				_pos.X + _currentAnimation->Base->FrameDimensions.X * (IsFacingLeft() ? 0.3f : 0.7f),
				_pos.Y + _currentAnimation->Base->FrameDimensions.Y * 0.3f
			);
		}
	}
}