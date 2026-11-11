#include "Copter.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	Copter::Copter()
		: _phase(0.0f), _state(State::Free), _ridden(false)
	{
	}

	Copter::~Copter()
	{
#if defined(WITH_AUDIO)
		if (_noise != nullptr) {
			_noise->Stop();
			_noise = nullptr;
		}
#endif
	}

	Task<bool> Copter::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_state = (details.Params[0] != 0 ? State::Mounted : State::Free);
		SetState(ActorState::CanBeFrozen | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Enemy/LizardFloat"_s);

		SetAnimation((AnimState)1);

		_originPos = _pos;

		async_return true;
	}

	void Copter::OnUpdate(float timeMult)
	{
		switch (_state) {
			case State::Free: {
				_phase += timeMult * 0.05f;
				MoveInstantly(_originPos + Vector2f(0.0f, sinApprox(_phase) * 4.0f), MoveType::Absolute);
				OnUpdateHitbox();
				break;
			}
			case State::Mounted: {
				// The rider ends it, the timer only covers a rider that is gone
				if (_ridden) {
					_phase -= timeMult;
					if (_phase <= 0.0f) {
						DecreaseHealth(INT32_MAX);
					}
				}
				break;
			}
			case State::Unmounted: {
				ActorBase::OnUpdate(timeMult);

				_speed.Y = _levelHandler->GetGravity() * -0.5f;
				_renderer.SetAlphaF(_renderer.GetAlpha() - (0.004f * timeMult));
				_phase -= timeMult;
				if (_phase <= 0.0f) {
					DecreaseHealth(INT32_MAX);
				}
				break;
			}
		}

#if defined(WITH_AUDIO)
		if (_noise != nullptr) {
			float newGain = _noise->GetGain() - (_noiseDec * timeMult);
			if (newGain <= 0.0f) {
				_noise->Stop();
				_noise = nullptr;
			} else {
				_noise->SetGain(newGain);
				_noise->SetPosition(Vector3f(_pos.X, _pos.Y, 0.8f));
			}
		}
#endif
	}

	void Copter::OnSerializeState(Stream& dest)
	{
		ActorBase::OnSerializeState(dest);

		dest.WriteValue<std::uint8_t>((std::uint8_t)((std::uint8_t)_state | (_ridden ? 0x80 : 0x00)));
		dest.WriteValueAsLE<float>(_phase);
	}

	void Copter::OnDeserializeState(Stream& src)
	{
		ActorBase::OnDeserializeState(src);

		// A copter taken by a player is destroyed right away, so there is no rider to restore. The noise
		// of an unmounted copter is only fading out, so it's not started again.
		std::uint8_t state = src.ReadValue<std::uint8_t>();
		_state = (State)(state & 0x7f);
		_phase = src.ReadValueAsLE<float>();
		// The rider is not restored with it, so a ridden one stays hidden until its timer ends it
		_ridden = ((state & 0x80) != 0);
		if (_ridden) {
			_renderer.SetDrawEnabled(false);
			SetState(ActorState::CollideWithOtherActors, false);
		}
	}

	void Copter::OnDetach(ActorBase* parent)
	{
		if (!_ridden) {
			// Measured in the original: it exists for the whole ride, so its generator waits for the ride to end
			_ridden = true;
			_phase = RiddenTimeout;
			SetState(ActorState::CollideWithOtherActors, false);
			return;
		}

		DecreaseHealth(INT32_MAX);
	}

	void Copter::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Enemy/LizardFloat"_s);
	}

	bool Copter::OnHandleCollision(ActorBase* other)
	{
		if (_state == State::Free || _state == State::Unmounted) {
			if (auto* player = runtime_cast<Player>(other)) {
				if (player->GetModifier() == Player::Modifier::None && player->SetModifier(Player::Modifier::LizardCopter, shared_from_this())) {
					_state = State::Mounted;
					_renderer.SetAlphaF(1.0f);

					PlaySfx("CopterPre"_s);
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

#if defined(WITH_AUDIO)
			_noise = PlaySfx("Copter"_s, 0.8f, 0.8f);
			if (_noise != nullptr) {
				_noise->SetLooping(true);
				_noiseDec = _noise->GetGain() * 0.005f;
			}
#endif

			SetState(ActorState::ApplyGravitation, true);
		}
	}
}