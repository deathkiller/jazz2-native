#pragma once

#include "AudioReaderOgg.h"

#if defined(WITH_VORBIS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioLoader.h"

namespace nCine
{
	/**
		@brief Audio loader for the Ogg Vorbis format, backed by `libvorbis`
	*/
	class AudioLoaderOgg : public IAudioLoader
	{
	public:
		explicit AudioLoaderOgg(std::unique_ptr<Death::IO::Stream> fileHandle);
		~AudioLoaderOgg() override;

		AudioLoaderOgg(const AudioLoaderOgg&) = delete;
		AudioLoaderOgg& operator=(const AudioLoaderOgg&) = delete;

		std::unique_ptr<IAudioReader> createReader() override;

	private:
		/** @brief Vorbisfile decoder handle */
		OggVorbis_File oggFile_;
	};
}

#endif
