#pragma once

#include "AudioReaderOgg.h"

#if defined(WITH_VORBIS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioLoader.h"

namespace nCine
{
	/// Ogg Vorbis audio loader
	class AudioLoaderOgg : public IAudioLoader
	{
	public:
		explicit AudioLoaderOgg(std::unique_ptr<Death::IO::Stream> fileHandle);
		~AudioLoaderOgg() override;

		std::unique_ptr<IAudioReader> createReader() override;

	private:
		/// Vorbisfile handle
		OggVorbis_File oggFile_;

		/// Deleted copy constructor
		AudioLoaderOgg(const AudioLoaderOgg&) = delete;
		/// Deleted assignment operator
		AudioLoaderOgg& operator=(const AudioLoaderOgg&) = delete;
	};
}

#endif
