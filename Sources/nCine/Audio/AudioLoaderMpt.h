#pragma once

#include "AudioReaderMpt.h"

#if defined(WITH_OPENMPT)

#include "IAudioLoader.h"

namespace nCine
{
	class AudioLoaderMpt : public IAudioLoader
	{
	public:
		explicit AudioLoaderMpt(std::unique_ptr<Death::IO::Stream> fileHandle);

		std::unique_ptr<IAudioReader> createReader() override;
	};
}

#endif