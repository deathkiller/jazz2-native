#pragma once

#include "ITextureLoader.h"

namespace nCine
{
	/**
		@brief Texture loader that describes an empty raw-format texture
		
		Does not read any file; it just records the width, height, MIP count and internal format so
		raw texels can later be uploaded into the resulting texture.
	*/
	class TextureLoaderRaw : public ITextureLoader
	{
	public:
		TextureLoaderRaw(std::int32_t width, std::int32_t height, std::int32_t mipMapCount, PixelFormat format);
	};
}
