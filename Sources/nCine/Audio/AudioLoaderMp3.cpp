#include "AudioLoaderMp3.h"
#include "AudioReaderMp3.h"

#if defined(WITH_MPG123)

using namespace Death::IO;

namespace nCine
{
	AudioLoaderMp3::AudioLoaderMp3(std::unique_ptr<Stream> fileHandle)
		: IAudioLoader(Death::move(fileHandle)), _handle(nullptr)
	{
		std::int32_t frequency = 0, channels = 0;
		std::int64_t samples = 0;
		_handle = AudioReaderMp3::OpenDecoder(_fileHandle.get(), frequency, channels, samples);
		if (_handle == nullptr) {
			_fileHandle->Dispose();
			return;
		}

		_bytesPerSample = 2;
		_numChannels = channels;
		_frequency = frequency;
		_numSamples = std::int32_t(samples);
		_duration = float(double(samples) / double(frequency));

		_hasLoaded = true;
	}

	AudioLoaderMp3::~AudioLoaderMp3()
	{
		// The reader owns the decoder once it's created
		AudioReaderMp3::CloseDecoder(_handle);
	}

	std::unique_ptr<IAudioReader> AudioLoaderMp3::CreateReader()
	{
		mpg123_handle* handle = _handle;
		_handle = nullptr;
		return std::make_unique<AudioReaderMp3>(Death::move(_fileHandle), handle);
	}
}

#endif
