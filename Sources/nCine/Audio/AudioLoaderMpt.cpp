#include "AudioLoaderMpt.h"
#include "AudioReaderMpt.h"
#include "IAudioDevice.h"
#include "../ServiceLocator.h"

#if defined(WITH_OPENMPT)

using namespace Death::IO;

namespace nCine
{
	AudioLoaderMpt::AudioLoaderMpt(std::unique_ptr<Stream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		bytesPerSample_ = 2;
		numChannels_ = 2;

#if defined(DEATH_TARGET_PSP)
		// Rendering a module is the most expensive thing the Allegrex is asked to do outside the renderer, and
		// on this platform it happens synchronously on the main thread (there is no decoding thread), so every
		// buffer the stream refills is time the frame does not get. Half the output rate is half that cost,
		// for music that comes out of a pair of centimetre-wide speakers - and the samples inside the original
		// modules were recorded at around this rate to begin with. The device resamples the stream back up to
		// its own 44100 Hz, which costs a fraction of what the module mixer does.
		constexpr std::int32_t PreferredFrequency = 22050;
#else
		constexpr std::int32_t PreferredFrequency = 0;
#endif
		const std::int32_t nativeFrequency = device.nativeFrequency();
		frequency_ = (PreferredFrequency > 0 && PreferredFrequency < nativeFrequency ? PreferredFrequency : nativeFrequency);

		numSamples_ = UINT32_MAX;
		hasLoaded_ = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderMpt::createReader()
	{
		return std::make_unique<AudioReaderMpt>(std::move(fileHandle_), frequency_);
	}
}

#endif
