#pragma once

#include "AudioReaderMpt.h"

#if defined(WITH_OPENMPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioLoader.h"

namespace nCine
{
	/**
		@brief Audio loader for tracker module formats, backed by `libopenmpt`
	*/
	class AudioLoaderMpt : public IAudioLoader
	{
	public:
		explicit AudioLoaderMpt(std::unique_ptr<Death::IO::Stream> fileHandle);

		AudioLoaderMpt(const AudioLoaderMpt&) = delete;
		AudioLoaderMpt& operator=(const AudioLoaderMpt&) = delete;

		std::unique_ptr<IAudioReader> createReader() override;
	};
}

#endif