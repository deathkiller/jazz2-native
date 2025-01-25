#pragma once

#include "ITextureLoader.h"

namespace nCine
{
	/// Texture loader used to specify a raw format when loading texels
	class TextureLoaderRaw : public ITextureLoader
	{
	public:
		TextureLoaderRaw(std::int32_t width, std::int32_t height, std::int32_t mipMapCount, GLenum internalFormat);
	};
}
