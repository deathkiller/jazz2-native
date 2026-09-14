#include "AmbientSound.h"
#include "../../ILevelHandler.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	AmbientSound::AmbientSound()
		: _sfx(0), _gain(0.0f), _restartDelay(0.0f)
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

	void AmbientSound::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Common/AmbientSound"_s);
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

	void AmbientSound::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		// A looping ambience that reports itself stopped was stopped by something other than this actor,
		// which starts it once and otherwise only stops it in the destructor. The audio device releases
		// every player when it decides the output has gone away (see
		// AudioDeviceBase::checkForStalledSources()), and nothing would ever start this one again - the
		// region would simply be silent for the rest of the level. Retried about once a second, so a
		// device that is genuinely gone is not asked every frame.
		if (_sound != nullptr && _sound->isStopped()) {
			if (_restartDelay > 0.0f) {
				_restartDelay -= timeMult;
			} else {
				_restartDelay = RestartDelay;
				_sound->play();
			}
		}
	}
}