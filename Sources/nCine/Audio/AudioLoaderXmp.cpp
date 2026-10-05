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
		// Rendering at the device's own mixing rate skips one resampling stage, and it is also what keeps the
		// module mixer's cost in step with the machine: on the PSP that rate is the "Sample Rate" option, whose
		// default of half the hardware's 44100 Hz is there for this mixer above all (see PspAudioDevice), and
		// on the Amiga it follows the performance preset (22050 Hz on the slow tiers)
		_frequency = device.nativeFrequency();

		_numSamples = UINT32_MAX;
		_hasLoaded = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderXmp::createReader()
	{
		return std::make_unique<AudioReaderXmp>(std::move(_fileHandle), _frequency);
	}
}

#endif
