#pragma once

#include "../../Main.h"

#if defined(WITH_QOI) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ITextureLoader.h"

namespace nCine
{
	/// QOI texture loader
	class TextureLoaderQoi : public ITextureLoader
	{
	public:
		explicit TextureLoaderQoi(std::unique_ptr<Death::IO::Stream> fileHandle);
	};
}

#endif