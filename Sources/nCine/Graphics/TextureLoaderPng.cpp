#include "TextureLoaderPng.h"
#include "PngCodec.h"

using namespace Death::IO;

namespace nCine
{
	TextureLoaderPng::TextureLoaderPng(std::unique_ptr<Stream> fileHandle)
		: ITextureLoader(Death::move(fileHandle))
	{
		if (!_fileHandle->IsValid()) {
			return;
		}

		// Always 8-bit RGBA, an indexed image with the colors of its palette, gray and 16-bit images too. It's inflated a
		// row at a time straight into the pixels of the texture, only the data of the file is held besides them.
		// No texture of the game comes near 8192 × 8192 and many devices can't create one that large, so a damaged or
		// hostile file doesn't get to allocate more than that
		PngDecodeOptions options;
		options.MaxPixels = std::int64_t(8192) * 8192;
		PngRgbaImage image;
		PngError error = PngCodec::DecodeRgba(*_fileHandle, image, options);
		if (error != PngError::None) {
			LOGE("PNG file cannot be loaded: {}", PngCodec::GetErrorText(error));
			return;
		}

		_width = image.Width;
		_height = image.Height;
		_pixels = Death::move(image.Pixels);
		_mipMapCount = 1;
		_texFormat = TextureFormat(PixelFormat::RGBA8);
		_hasLoaded = true;
	}
}
