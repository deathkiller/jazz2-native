#pragma once

#include "AudioReaderXmp.h"

#if defined(WITH_XMP) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "IAudioLoader.h"

namespace nCine
{
	/**
		@brief Audio loader for tracker module formats, backed by `libxmp` (see @ref AudioReaderXmp)
	*/
	class AudioLoaderXmp : public IAudioLoader
	{
	public:
		explicit AudioLoaderXmp(std::unique_ptr<Death::IO::Stream> fileHandle);

		AudioLoaderXmp(const AudioLoaderXmp&) = delete;
		AudioLoaderXmp& operator=(const AudioLoaderXmp&) = delete;

		std::unique_ptr<IAudioReader> createReader() override;
	};
}

#endif
