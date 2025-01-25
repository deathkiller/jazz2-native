#pragma once

#include "ITextureLoader.h"

namespace nCine {

	/// PNG texture loader
	class TextureLoaderPng : public ITextureLoader
	{
	public:
		explicit TextureLoaderPng(std::unique_ptr<Death::IO::Stream> fileHandle);

	private:
		static std::int32_t ReadInt32BigEndian(const std::unique_ptr<Death::IO::Stream>& s);
		static std::uint8_t UnapplyFilter(std::uint8_t filter, std::uint8_t x, std::uint8_t a, std::uint8_t b, std::uint8_t c);
	};

}

