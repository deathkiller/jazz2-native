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
		_frequency = device.nativeFrequency();

		_numSamples = UINT32_MAX;
		_hasLoaded = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderMpt::createReader()
	{
		return std::make_unique<AudioReaderMpt>(std::move(_fileHandle), _frequency);
	}
}

#endif
