#include "AmbientSound.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	AmbientSound::AmbientSound()
	{
	}

	AmbientSound::~AmbientSound()
	{
		// TODO: Fade-out
		if (_sound != nullptr) {
			_sound->stop();
			_sound = nullptr;
		}
	}

	Task<bool> AmbientSound::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// TODO: Implement Fade:1|Sine:1

		_sfx = details.Params[0];
		_gain = 0.2f * (details.Params[1] / 255.0f);

		SetState(ActorState::ForceDisableCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithSolidObjects | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Common/AmbientSound"_s);
		
		switch (_sfx) {
			case 0: _sound = PlaySfx("AmbientWind"_s, _gain); break;
			case 1: _sound = PlaySfx("AmbientFire"_s, _gain); break;
			case 2: _sound = PlaySfx("AmbientScienceNoise"_s, _gain); break;
		}

		// TODO: Fade-in
		if (_sound != nullptr) {
			_sound->setLooping(true);
		}

		async_return true;
	}
}