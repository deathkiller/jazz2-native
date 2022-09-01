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

		uint8_t sfx = details.Params[0];
		float gain = 0.2f * (details.Params[1] / 255.0f);

		CollisionFlags = CollisionFlags::ForceDisableCollisions;

		co_await RequestMetadataAsync("Common/AmbientSound"_s);

		switch (sfx) {
			case 0: _sound = PlaySfx("AmbientWind"_s, gain); break;
			case 1: _sound = PlaySfx("AmbientFire"_s, gain); break;
			case 2: _sound = PlaySfx("AmbientScienceNoise"_s, gain); break;
		}

		// TODO: Fade-in
		if (_sound != nullptr) {
			_sound->setLooping(true);
		}

		co_return true;
	}

	void AmbientSound::OnUpdate(float timeMult)
	{
		// Nothing to do...
	}
}