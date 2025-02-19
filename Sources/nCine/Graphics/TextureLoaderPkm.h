#pragma once

#include "../../Main.h"

#if (defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ITextureLoader.h"

namespace nCine
{
	/// PKM texture loader
	class TextureLoaderPkm : public ITextureLoader
	{
	public:
		explicit TextureLoaderPkm(std::unique_ptr<Death::IO::Stream> fileHandle);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		/// Header for the PKM header
		/*! The extended width and height are the dimensions rounded up to a multiple of 4.
		 *  The total data size in bytes is (extendedWidth / 4) * (extendedHeight / 4) * 8
		 */
		struct PkmHeader
		{
			std::uint32_t magicId;
			std::uint16_t version;
			std::uint16_t dataType;
			std::uint16_t extendedWidth;
			std::uint16_t extendedHeight;
			std::uint16_t width;
			std::uint16_t height;
		};
#endif
	};
}

#endif