#pragma once

#include "../../Common.h"

#if defined(WITH_QOI)

#include "ITextureLoader.h"

namespace nCine
{
	class TextureLoaderQoi : public ITextureLoader
	{
	public:
		explicit TextureLoaderQoi(std::unique_ptr<IFileStream> fileHandle);
	};
}

#endif