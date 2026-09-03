#include "AudioLoaderXmp.h"
#include "AudioReaderXmp.h"
#include "IAudioDevice.h"
#include "../ServiceLocator.h"

#if defined(WITH_XMP)

using namespace Death::IO;

namespace nCine
{
	AudioLoaderXmp::AudioLoaderXmp(std::unique_ptr<Stream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		_bytesPerSample = 2;
		_numChannels = 2;
#if defined(DEATH_TARGET_PSP)
		// Rendering a module is the most expensive thing the Allegrex is asked to do outside the renderer:
		// measured on `prince/02_castle1n`, mixing this at the device's 44100 Hz cost around 420 ms of every
		// second - more CPU than the whole renderer, and more than the idle time the decoding thread has to
		// hide it in, so the main thread ended up waiting on the decode it could not overlap. The mixer's
		// cost scales with the output rate, so half the rate is half of that, for music that comes out of a
		// pair of centimetre-wide speakers - and the samples inside the original modules were recorded at
		// around this rate to begin with. The device resamples the stream back up to its own 44100 Hz, which
		// costs a fraction of what the mixer does. Kept in step with AudioLoaderMpt, which caps the same way.
		constexpr std::int32_t PreferredFrequency = 22050;
#else
		// Rendering at the device's own mixing rate skips one resampling stage; on the Amiga that rate
		// already follows the performance preset (22050 Hz on the slow tiers), so the module mixer's
		// cost scales with the machine the same way everything else does
		constexpr std::int32_t PreferredFrequency = 0;
#endif
		const std::int32_t nativeFrequency = device.nativeFrequency();
		_frequency = (PreferredFrequency > 0 && PreferredFrequency < nativeFrequency ? PreferredFrequency : nativeFrequency);

		_numSamples = UINT32_MAX;
		_hasLoaded = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderXmp::createReader()
	{
		return std::make_unique<AudioReaderXmp>(std::move(_fileHandle), _frequency);
	}
}

#endif
