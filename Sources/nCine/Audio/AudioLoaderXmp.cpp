#include "AudioLoaderXmp.h"
#include "AudioReaderXmp.h"
#include "IAudioDevice.h"
#include "../ServiceLocator.h"

#if defined(WITH_XMP)

using namespace Death::IO;

namespace nCine
{
	AudioLoaderXmp::AudioLoaderXmp(std::unique_ptr<Stream> fileHandle)
		: IAudioLoader(Death::move(fileHandle))
	{
		IAudioDevice& device = theServiceLocator().GetAudioDevice();

		_bytesPerSample = 2;
		_numChannels = 2;
		// Render at the device mixing rate to skip a resampling stage (it's lower on slow platforms)
		_frequency = device.GetNativeFrequency();

		_numSamples = UINT32_MAX;
		_hasLoaded = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderXmp::CreateReader()
	{
		return std::make_unique<AudioReaderXmp>(Death::move(_fileHandle), _frequency);
	}
}

#endif
