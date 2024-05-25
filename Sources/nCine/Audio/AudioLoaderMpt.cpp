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
		frequency_ = device.nativeFrequency();
		numSamples_ = UINT32_MAX;
		hasLoaded_ = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderMpt::createReader()
	{
		return std::make_unique<AudioReaderMpt>(std::move(fileHandle_), frequency_);
	}
}

#endif
