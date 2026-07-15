#include "TextureLoaderRaw.h"

namespace nCine
{
	TextureLoaderRaw::TextureLoaderRaw(std::int32_t width, std::int32_t height, std::int32_t mipMapCount, PixelFormat format)
		: ITextureLoader()
	{
		width_ = width;
		height_ = height;
		mipMapCount_ = mipMapCount;
		texFormat_ = TextureFormat(format);

		std::int32_t numPixels = width * height;
		std::int32_t bytesPerPixel = texFormat_.numChannels();
		for (std::int32_t i = 0; i < mipMapCount_; i++) {
			dataSize_ += numPixels * bytesPerPixel;
			numPixels /= 2;
		}

		hasLoaded_ = true;
	}
}
