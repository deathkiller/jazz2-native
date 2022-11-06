#pragma once

#include "ITextureLoader.h"

namespace nCine
{
	/// A texture loader used to specify a raw format when loading texels
	class TextureLoaderRaw : public ITextureLoader
	{
	public:
		TextureLoaderRaw(int width, int height, int mipMapCount, GLenum internalFormat);
	};
}
