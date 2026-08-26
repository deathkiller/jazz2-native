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
		// Rendering at the device's own mixing rate skips one resampling stage; on the Amiga that rate
		// already follows the performance preset (22050 Hz on the slow tiers), so the module mixer's
		// cost scales with the machine the same way everything else does
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
