#include "Moth.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Environment
{
	Moth::Moth()
		: _timer(0.0f), _direction(0)
	{
	}

	Task<bool> Moth::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CanBeFrozen, false);
		_renderer.setLayer(_renderer.layer() + 20);

		uint8_t theme = details.Params[0];

		async_await RequestMetadataAsync("Object/Moth"_s);
		SetAnimation((AnimState)theme);

		_renderer.AnimPaused = true;

		async_return true;
	}

	void Moth::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_timer > 0.0f) {
			if (GetState(ActorState::CanJump)) {
				_timer = 0.0f;
			} else {
				_timer -= timeMult;

				_externalForce.X = lerpByTime(_externalForce.X, sinf((100.0f - _timer) / 6.0f) * 4.0f * _direction, 0.6f, timeMult);
				_externalForce.Y = lerpByTime(_externalForce.Y, -0.00005f * _timer * _timer, 0.6f, timeMult);

				SetFacingLeft(_speed.X < 0.0f);
			}
		} else if (GetState(ActorState::CanJump)) {
			_speed.X = 0.0f;
			_externalForce.Y = 0.0f;
			_externalForce.X = 0.0f;

			_renderer.AnimTime = 0.0f;
			_renderer.AnimPaused = true;
		} else if (GetState(ActorState::ApplyGravitation)) {
			_externalForce.Y = _levelHandler->Gravity * -0.8f;
		}
	}

	bool Moth::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			if (_timer <= 50.0f) {
				_timer = 100.0f - _timer * 0.2f;

				SetState(ActorState::CanJump, false);

				_direction = (Random().NextBool() ? -1 : 1);
				_speed.X = Random().NextFloat(-1.4f, 0.0f) * _direction;
				_speed.Y = Random().NextFloat(-0.4f, 0.0f);

				_renderer.AnimPaused = false;
			}
			return true;
		}

		return false;
	}
}