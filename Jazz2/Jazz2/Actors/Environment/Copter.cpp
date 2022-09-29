#include "Copter.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	Copter::Copter()
		:
		_phase(0.0f),
		_state(StateFree)
	{
	}

	Task<bool> Copter::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_state = (details.Params[0] != 0 ? StateMounted : StateFree);

		SetState(ActorState::ApplyGravitation, false);

		co_await RequestMetadataAsync("Enemy/LizardFloat"_s);

		SetAnimation("Copter"_s);

		_originPos = _pos;

		co_return true;
	}

	void Copter::OnUpdate(float timeMult)
	{
		if (_state == StateFree) {
			_phase += timeMult * 0.05f;
			MoveInstantly(_originPos + Vector2f(0.0f, sinf(_phase) * 4.0f), MoveType::Absolute);
			OnUpdateHitbox();
		} else if (_state == StateUnmounted) {
			ActorBase::OnUpdate(timeMult);

			_speed.Y = _levelHandler->Gravity * -0.5f;
			_renderer.setAlphaF(_renderer.alpha() - 0.007f * timeMult);
			_phase -= timeMult;
			if (_phase <= 0.0f) {
				DecreaseHealth(INT32_MAX);
			}
		}
	}

	bool Copter::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_state != StateMounted) {
			if (auto player = dynamic_cast<Player*>(other.get())) {
				if (player->SetModifier(Player::Modifier::LizardCopter)) {
					DecreaseHealth(INT32_MAX);
					return true;
				}
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	void Copter::Unmount()
	{
		if (_state == StateMounted) {
			_state = StateUnmounted;
			_phase = 120.0f;

			SetState(ActorState::ApplyGravitation, true);
		}
	}
}