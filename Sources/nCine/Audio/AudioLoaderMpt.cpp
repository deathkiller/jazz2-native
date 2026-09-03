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

		_bytesPerSample = 2;
		_numChannels = 2;

#if defined(DEATH_TARGET_PSP)
		// Rendering a module is the most expensive thing the Allegrex is asked to do outside the renderer, and
		// it costs more than the idle time the decoding thread has to hide it in, so the main thread ends up
		// waiting on the part it cannot overlap. Half the output rate is half that cost,
		// for music that comes out of a pair of centimetre-wide speakers - and the samples inside the original
		// modules were recorded at around this rate to begin with. The device resamples the stream back up to
		// its own 44100 Hz, which costs a fraction of what the module mixer does.
		constexpr std::int32_t PreferredFrequency = 22050;
#else
		constexpr std::int32_t PreferredFrequency = 0;
#endif
		const std::int32_t nativeFrequency = device.nativeFrequency();
		_frequency = (PreferredFrequency > 0 && PreferredFrequency < nativeFrequency ? PreferredFrequency : nativeFrequency);

		_numSamples = UINT32_MAX;
		_hasLoaded = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderMpt::createReader()
	{
		return std::make_unique<AudioReaderMpt>(std::move(_fileHandle), _frequency);
	}
}

#endif
