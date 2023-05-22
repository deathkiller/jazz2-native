#include "TextureLoaderPkm.h"

#if defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES)

#include <cstring>

using namespace Death::IO;

namespace nCine
{
	TextureLoaderPkm::TextureLoaderPkm(std::unique_ptr<Stream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
		RETURN_ASSERT_MSG(fileHandle_->IsValid(), "File \"%s\" cannot be opened", fileHandle_->GetPath().data());

		PkmHeader header;
		// PKM header is 16 bytes long
		fileHandle_->Read(&header, 16);

		// Checking for the header presence
		if (Stream::Uint32FromBE(header.magicId) == 0x504B4D20) // "PKM 10"
		{
			if (Stream::Uint16FromBE(header.version) != 0x3130) { // "10"
				RETURN_MSG("PKM version not supported: 0x%04x", header.version);
			}
			if (Stream::Uint16FromBE(header.dataType) != 0) {
				RETURN_MSG("PKM data type not supported: 0x%04x", header.dataType);
			}

			headerSize_ = 16;
			width_ = Stream::Uint16FromBE(header.width);
			height_ = Stream::Uint16FromBE(header.height);

			const int extWidth = Stream::Uint16FromBE(header.extendedWidth);
			const int extHeight = Stream::Uint16FromBE(header.extendedHeight);

			LOGI("Header found: w:%d h:%d, xw:%d xh:%d", width_, height_, extWidth, extHeight);
		}

		loadPixels(GL_ETC1_RGB8_OES);
		hasLoaded_ = true;
	}
}

#endif