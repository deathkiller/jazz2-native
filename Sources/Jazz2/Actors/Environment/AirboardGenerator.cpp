#include "AirboardGenerator.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Environment
{
	AirboardGenerator::AirboardGenerator()
		:
		_timeLeft(0.0f),
		_active(false)
	{
	}

	Task<bool> AirboardGenerator::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_delay = details.Params[0];
		_active = true;

		async_await RequestMetadataAsync("Object/Airboard"_s);

		SetAnimation(AnimState::Default);

		async_return true;
	}

	void AirboardGenerator::OnUpdate(float timeMult)
	{
		if (!_active) {
			if (_timeLeft <= 0.0f) {
				_active = true;
				_renderer.setDrawEnabled(true);
			}

			_timeLeft -= timeMult;
		}
	}

	bool AirboardGenerator::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			if (_active && player->SetModifier(Player::Modifier::Airboard)) {
				_active = false;
				_renderer.setDrawEnabled(false);

				_timeLeft = _delay * FrameTimer::FramesPerSecond;

				Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() - 2), Explosion::Type::Generator);
			}
			return true;
		}

		return ActorBase::OnHandleCollision(other);
	}
}