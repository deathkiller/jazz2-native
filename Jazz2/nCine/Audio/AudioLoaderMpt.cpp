#include "AudioLoaderMpt.h"
#include "AudioReaderMpt.h"

#ifdef WITH_OPENMPT

namespace nCine
{
	AudioLoaderMpt::AudioLoaderMpt(std::unique_ptr<IFileStream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
		bytesPerSample_ = 2;
		numChannels_ = 2;
		frequency_ = UseNativeFrequency;
		numSamples_ = -1;
		hasLoaded_ = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderMpt::createReader()
	{
		return std::make_unique<AudioReaderMpt>(std::move(fileHandle_));
	}

}

#endif
