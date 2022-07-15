#pragma once

//#include <png.h>
#include "ITextureLoader.h"

namespace nCine {

	/// PNG texture loader
	class TextureLoaderPng : public ITextureLoader
	{
	public:
		explicit TextureLoaderPng(std::unique_ptr<IFileStream> fileHandle);

	private:
		static int ReadInt32BigEndian(const std::unique_ptr<IFileStream>& s);
		static uint8_t UnapplyFilter(uint8_t filter, uint8_t x, uint8_t a, uint8_t b, uint8_t c);
	};

}

