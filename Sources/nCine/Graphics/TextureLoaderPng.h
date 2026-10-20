#pragma once

#include "ITextureLoader.h"

namespace nCine {

	/**
		@brief Texture loader for the Portable Network Graphics (`.png`) format

		Decoded by @ref PngCodec::DecodeRgba() into 8-bit RGBA, whatever the image holds.
	*/
	class TextureLoaderPng : public ITextureLoader
	{
	public:
		explicit TextureLoaderPng(std::unique_ptr<Death::IO::Stream> fileHandle);
	};

}

