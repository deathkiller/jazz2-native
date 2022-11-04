#include "Copter.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	Copter::Copter()
		:
		_phase(0.0f),
		_state(State::Free)
	{
	}

	Task<bool> Copter::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_state = (details.Params[0] != 0 ? State::Mounted : State::Free);

		SetState(ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Enemy/LizardFloat"_s);

		SetAnimation("Copter"_s);

		_originPos = _pos;

		async_return true;
	}

	void Copter::OnUpdate(float timeMult)
	{
		switch (_state) {
			case State::Free: {
				_phase += timeMult * 0.05f;
				MoveInstantly(_originPos + Vector2f(0.0f, sinf(_phase) * 4.0f), MoveType::Absolute);
				OnUpdateHitbox();
				break;
			}
			case State::Unmounted: {
				ActorBase::OnUpdate(timeMult);

				_speed.Y = _levelHandler->Gravity * -0.5f;
				_renderer.setAlphaF(_renderer.alpha() - 0.004f * timeMult);
				_phase -= timeMult;
				if (_phase <= 0.0f) {
					DecreaseHealth(INT32_MAX);
				}
				break;
			}
		}
	}

	bool Copter::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_state == State::Free || _state == State::Unmounted) {
			if (auto player = dynamic_cast<Player*>(other.get())) {
				if (player->SetModifier(Player::Modifier::LizardCopter, shared_from_this())) {
					_state = State::Mounted;
					return true;
				}
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	void Copter::Unmount(float timeLeft)
	{
		if (_state == State::Mounted) {
			_state = State::Unmounted;
			_phase = timeLeft;

			SetState(ActorState::ApplyGravitation, true);
		}
	}
}