#pragma once

#include "IAudioLoader.h"

namespace nCine
{
	class AudioLoaderMpt : public IAudioLoader
	{
	public:
		explicit AudioLoaderMpt(std::unique_ptr<IFileStream> fileHandle);

		std::unique_ptr<IAudioReader> createReader() override;
	};

}
