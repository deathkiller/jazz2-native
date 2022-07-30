#include "Moth.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Environment
{
	Moth::Moth()
		:
		_timer(0.0f),
		_direction(0)
	{
	}

	Task<bool> Moth::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorFlags::CanBeFrozen, false);
		_renderer.setLayer(_renderer.layer() - 20);

		uint16_t theme = *(uint16_t*)&details.Params[0];

		co_await RequestMetadataAsync("Object/Moth"_s);

		switch (theme) {
			default:
			case 0: SetAnimation("Pink"_s); break;
			case 1: SetAnimation("Gray"_s); break;
			case 2: SetAnimation("Green"_s); break;
			case 3: SetAnimation("Purple"_s); break;
		}

		_renderer.AnimPaused = true;

		co_return true;
	}

	void Moth::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_timer > 0.0f) {
			if (GetState(ActorFlags::CanJump)) {
				_timer = 0.0f;
			} else {
				_timer -= timeMult;

				_externalForce.X = std::sinf((100.0f - _timer) / 6.0f) * 4.0f * _direction;
				_externalForce.Y = _timer * _timer * 0.000046f;

				SetFacingLeft(_speed.X < 0.0f);
			}
		} else if (GetState(ActorFlags::CanJump)) {
			_speed.X = 0.0f;
			_externalForce.Y = 0.0f;
			_externalForce.X = 0.0f;

			_renderer.AnimTime = 0.0f;
			_renderer.AnimPaused = true;
		}
	}

	bool Moth::OnHandleCollision(ActorBase* other)
	{
		if (auto player = dynamic_cast<Player*>(other)) {
			if (_timer <= 50.0f) {
				_timer = 100.0f - _timer * 0.2f;

				SetState(ActorFlags::CanJump, false);

				_direction = (Random().NextBool() ? -1 : 1);
				_speed.X = Random().NextFloat(0.0f, -1.4f) * _direction;
				_speed.Y = Random().NextFloat(0.0f, -0.4f);

				_renderer.AnimPaused = false;
			}
			return true;
		}

		return false;
	}
}