#include "AudioLoaderMpt.h"
#include "AudioReaderMpt.h"

namespace nCine
{
	AudioLoaderMpt::AudioLoaderMpt(std::unique_ptr<IFileStream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
		// TODO
		bytesPerSample_ = 2;
		numChannels_ = 2;
		frequency_ = AudioReaderMpt::SampleRate;
		numSamples_ = -1;
		hasLoaded_ = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderMpt::createReader()
	{
		return std::make_unique<AudioReaderMpt>(std::move(fileHandle_));
	}

}
