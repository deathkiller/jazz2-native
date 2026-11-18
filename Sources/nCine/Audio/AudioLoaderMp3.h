#pragma once

#include "AudioReaderMp3.h"

#if defined(WITH_MPG123) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioLoader.h"

namespace nCine
{
	/**
		@brief Audio loader for the MPEG audio format (MP3), backed by `libmpg123`
	*/
	class AudioLoaderMp3 : public IAudioLoader
	{
	public:
		explicit AudioLoaderMp3(std::unique_ptr<Death::IO::Stream> fileHandle);
		~AudioLoaderMp3() override;

		AudioLoaderMp3(const AudioLoaderMp3&) = delete;
		AudioLoaderMp3& operator=(const AudioLoaderMp3&) = delete;

		std::unique_ptr<IAudioReader> CreateReader() override;

	private:
		/** @brief Decoder handle, owned by the reader once created */
		mpg123_handle* _handle;
	};
}

#endif
