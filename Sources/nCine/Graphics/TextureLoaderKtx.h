#pragma once

#include <cstdint> // for header
#include "ITextureLoader.h"

namespace nCine
{
	/**
		@brief Texture loader for the Khronos Texture (`.ktx`) format
	*/
	class TextureLoaderKtx : public ITextureLoader
	{
	public:
		explicit TextureLoaderKtx(std::unique_ptr<Death::IO::Stream> fileHandle);

	private:
		static const std::int32_t KtxIdentifierLength = 12;
		static std::uint8_t fileIdentifier_[KtxIdentifierLength];

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		/** @brief Header of a KTX file (field codes are the graphics-API enum values stored by KTX v1.1) */
		struct KtxHeader
		{
			std::uint8_t identifier[KtxIdentifierLength];
			std::uint32_t endianess;
			std::uint32_t dataType;
			std::uint32_t dataTypeSize;
			std::uint32_t format;
			std::uint32_t internalFormat;
			std::uint32_t baseInternalFormat;
			std::uint32_t pixelWidth;
			std::uint32_t pixelHeight;
			std::uint32_t pixelDepth;
			std::uint32_t numberOfArrayElements;
			std::uint32_t numberOfFaces;
			std::uint32_t numberOfMipmapLevels;
			std::uint32_t bytesOfKeyValueData;
		};
#endif

		/** @brief Reads the KTX header and fills the corresponding structure */
		bool readHeader(KtxHeader& header);
		/** @brief Parses the KTX header to determine the texture format */
		bool parseFormat(const KtxHeader& header);
	};

}
