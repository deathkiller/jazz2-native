#pragma once

#include "AudioReaderOgg.h"

#if defined(WITH_VORBIS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioLoader.h"

namespace nCine
{
	/// Ogg Vorbis audio loader using `libvorbis` library
	class AudioLoaderOgg : public IAudioLoader
	{
	public:
		explicit AudioLoaderOgg(std::unique_ptr<Death::IO::Stream> fileHandle);
		~AudioLoaderOgg() override;

		AudioLoaderOgg(const AudioLoaderOgg&) = delete;
		AudioLoaderOgg& operator=(const AudioLoaderOgg&) = delete;

		std::unique_ptr<IAudioReader> createReader() override;

	private:
		/// Vorbisfile handle
		OggVorbis_File oggFile_;
	};
}

#endif
