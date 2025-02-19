#pragma once

#include <cstdint> // for header
#include "ITextureLoader.h"

namespace nCine
{
	/// KTX texture loader
	class TextureLoaderKtx : public ITextureLoader
	{
	public:
		explicit TextureLoaderKtx(std::unique_ptr<Death::IO::Stream> fileHandle);

	private:
		static const std::int32_t KtxIdentifierLength = 12;
		static std::uint8_t fileIdentifier_[KtxIdentifierLength];

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		/// Header for the KTX format
		struct KtxHeader
		{
			std::uint8_t identifier[KtxIdentifierLength];
			std::uint32_t endianess;
			std::uint32_t glType;
			std::uint32_t glTypeSize;
			std::uint32_t glFormat;
			std::uint32_t glInternalFormat;
			std::uint32_t glBaseInternalFormat;
			std::uint32_t pixelWidth;
			std::uint32_t pixelHeight;
			std::uint32_t pixelDepth;
			std::uint32_t numberOfArrayElements;
			std::uint32_t numberOfFaces;
			std::uint32_t numberOfMipmapLevels;
			std::uint32_t bytesOfKeyValueData;
		};
#endif

		/// Reads the KTX header and fills the corresponding structure
		bool readHeader(KtxHeader& header);
		/// Parses the KTX header to determine its format
		bool parseFormat(const KtxHeader& header);
	};

}
