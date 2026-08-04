#include "TextureLoaderRaw.h"

namespace nCine
{
	TextureLoaderRaw::TextureLoaderRaw(std::int32_t width, std::int32_t height, std::int32_t mipMapCount, PixelFormat format)
		: ITextureLoader()
	{
		_width = width;
		_height = height;
		_mipMapCount = mipMapCount;
		_texFormat = TextureFormat(format);

		std::int32_t numPixels = width * height;
		std::int32_t bytesPerPixel = _texFormat.numChannels();
		for (std::int32_t i = 0; i < _mipMapCount; i++) {
			_dataSize += numPixels * bytesPerPixel;
			numPixels /= 2;
		}

		_hasLoaded = true;
	}
}
