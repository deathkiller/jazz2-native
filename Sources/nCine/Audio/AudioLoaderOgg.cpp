#include "AudioLoaderOgg.h"
#include "AudioReaderOgg.h"

#if defined(WITH_VORBIS)

using namespace Death::IO;

namespace nCine
{
	namespace
	{
		size_t fileRead(void* ptr, size_t size, size_t nmemb, void* datasource)
		{
			Stream* file = static_cast<Stream*>(datasource);
			std::int64_t bytesRead = file->Read(ptr, size * nmemb);
			return bytesRead > 0 ? (size_t)bytesRead : 0;
		}

		int fileSeek(void* datasource, ogg_int64_t offset, int whence)
		{
			Stream* file = static_cast<Stream*>(datasource);
			return file->Seek(offset, (SeekOrigin)whence) >= 0 ? 0 : -1;
		}

		int fileClose(void* datasource)
		{
			Stream* file = static_cast<Stream*>(datasource);
			file->Dispose();
			return 0;
		}

		long fileTell(void* datasource)
		{
			Stream* file = static_cast<Stream*>(datasource);
			return (long)file->GetPosition();
		}

		const ov_callbacks fileCallbacks = { fileRead, fileSeek, fileClose, fileTell };
	}

	AudioLoaderOgg::AudioLoaderOgg(std::unique_ptr<Stream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
#if defined(WITH_VORBIS_DYNAMIC)
		if (!AudioReaderOgg::TryLoadLibrary()) {
			_fileHandle->Dispose();
			return;
		}

		int result = AudioReaderOgg::_ov_open_callbacks(_fileHandle.get(), &_oggFile, nullptr, 0, fileCallbacks);
#else
		int result = ov_open_callbacks(_fileHandle.get(), &_oggFile, nullptr, 0, fileCallbacks);
#endif
		if (result != 0) {
			LOGE("ov_open_callbacks() failed with error {}", result);
			_fileHandle->Dispose();
			return;
		}

		// Get some information about the Ogg file
#if defined(WITH_VORBIS_DYNAMIC)
		const vorbis_info* info = AudioReaderOgg::_ov_info(&_oggFile, -1);
#else
		const vorbis_info* info = ov_info(&_oggFile, -1);
#endif

		_bytesPerSample = 2; // Ogg is always 16 bits
		_numChannels = info->channels;
		_frequency = info->rate;

#if defined(WITH_VORBIS_DYNAMIC)
		_numSamples = static_cast<unsigned long int>(AudioReaderOgg::_ov_pcm_total(&_oggFile, -1));
		_duration = float(AudioReaderOgg::_ov_time_total(&_oggFile, -1));
#else
		_numSamples = static_cast<unsigned long int>(ov_pcm_total(&_oggFile, -1));
		_duration = float(ov_time_total(&_oggFile, -1));
#endif

		DEATH_ASSERT(_numChannels == 1 || _numChannels == 2, ("Unsupported number of channels: {}", _numChannels), );
		LOGD("Duration: {:.2}s, channels: {}, frequency: {} Hz", _duration, _numChannels, _frequency);

		_hasLoaded = true;
	}

	AudioLoaderOgg::~AudioLoaderOgg()
	{
		// Checking if the ownership of the `Stream` pointer has been transferred to a reader
		if (_fileHandle != nullptr) {
#if defined(WITH_VORBIS_DYNAMIC)
			AudioReaderOgg::_ov_clear(&_oggFile);
#else
			ov_clear(&_oggFile);
#endif
		}
	}

	std::unique_ptr<IAudioReader> AudioLoaderOgg::createReader()
	{
		return std::make_unique<AudioReaderOgg>(std::move(_fileHandle), _oggFile);
	}
}

#endif