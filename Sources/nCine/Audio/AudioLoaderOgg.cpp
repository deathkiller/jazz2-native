#include "AudioLoaderOgg.h"
#include "AudioReaderOgg.h"

#if defined(WITH_VORBIS)

namespace nCine
{
	namespace
	{
		size_t fileRead(void* ptr, size_t size, size_t nmemb, void* datasource)
		{
			IFileStream* file = static_cast<IFileStream*>(datasource);
			return file->Read(ptr, size * nmemb);
		}

		int fileSeek(void* datasource, ogg_int64_t offset, int whence)
		{
			IFileStream* file = static_cast<IFileStream*>(datasource);
			return file->Seek(offset, (SeekOrigin)whence);
		}

		int fileClose(void* datasource)
		{
			IFileStream* file = static_cast<IFileStream*>(datasource);
			file->Close();
			return 0;
		}

		long fileTell(void* datasource)
		{
			IFileStream* file = static_cast<IFileStream*>(datasource);
			return file->GetPosition();
		}

		const ov_callbacks fileCallbacks = { fileRead, fileSeek, fileClose, fileTell };
	}

	AudioLoaderOgg::AudioLoaderOgg(std::unique_ptr<IFileStream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
		LOGV_X("Loading \"%s\"", fileHandle_->GetFileName().data());

#if defined(DEATH_TARGET_ANDROID)
		if (fileHandle_->GetType() == IFileStream::FileType::Asset) {
			fileHandle_->Open(FileAccessMode::Read | FileAccessMode::FileDescriptor);
		} else
#endif
			fileHandle_->Open(FileAccessMode::Read);

#if defined(WITH_VORBIS_DYNAMIC)
		if (!AudioReaderOgg::TryLoadLibrary()) {
			fileHandle_->Close();
			return;
		}

		int result = AudioReaderOgg::_ov_open_callbacks(fileHandle_.get(), &oggFile_, nullptr, 0, fileCallbacks);
#else
		int result = ov_open_callbacks(fileHandle_.get(), &oggFile_, nullptr, 0, fileCallbacks);
#endif
		if (result != 0) {
			LOGE_X("Cannot open \"%s\" with ov_open_callbacks()", fileHandle_->GetFileName().data());
			fileHandle_->Close();
			return;
		}

		// Get some information about the Ogg file
#if defined(WITH_VORBIS_DYNAMIC)
		const vorbis_info* info = AudioReaderOgg::_ov_info(&oggFile_, -1);
#else
		const vorbis_info* info = ov_info(&oggFile_, -1);
#endif

		bytesPerSample_ = 2; // Ogg is always 16 bits
		numChannels_ = info->channels;
		frequency_ = info->rate;

#if defined(WITH_VORBIS_DYNAMIC)
		numSamples_ = static_cast<unsigned long int>(AudioReaderOgg::_ov_pcm_total(&oggFile_, -1));
		duration_ = float(AudioReaderOgg::_ov_time_total(&oggFile_, -1));
#else
		numSamples_ = static_cast<unsigned long int>(ov_pcm_total(&oggFile_, -1));
		duration_ = float(ov_time_total(&oggFile_, -1));
#endif

		RETURN_ASSERT_MSG_X(numChannels_ == 1 || numChannels_ == 2, "Unsupported number of channels: %d", numChannels_);
		LOGV_X("Duration: %.2fs, channels: %d, frequency: %dHz", duration_, numChannels_, frequency_);

		hasLoaded_ = true;
	}

	AudioLoaderOgg::~AudioLoaderOgg()
	{
		// Checking if the ownership of the `IFileStream` pointer has been transferred to a reader
		if (fileHandle_ != nullptr) {
#if defined(WITH_VORBIS_DYNAMIC)
			AudioReaderOgg::_ov_clear(&oggFile_);
#else
			ov_clear(&oggFile_);
#endif
		}
	}

	std::unique_ptr<IAudioReader> AudioLoaderOgg::createReader()
	{
		return std::make_unique<AudioReaderOgg>(std::move(fileHandle_), oggFile_);
	}
}

#endif