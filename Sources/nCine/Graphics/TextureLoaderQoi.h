#pragma once

#include "ITextureLoader.h"

namespace nCine
{
	class TextureLoaderQoi : public ITextureLoader
	{
	public:
		explicit TextureLoaderQoi(std::unique_ptr<IFileStream> fileHandle);
	};
}
