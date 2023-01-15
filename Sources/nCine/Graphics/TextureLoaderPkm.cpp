#include "TextureLoaderPkm.h"

#if defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES)

#include <cstring>

namespace nCine
{
	TextureLoaderPkm::TextureLoaderPkm(std::unique_ptr<IFileStream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
		RETURN_ASSERT_MSG_X(fileHandle_->IsOpened(), "File \"%s\" cannot be opened", fileHandle_->GetFileName().data());

		PkmHeader header;
		// PKM header is 16 bytes long
		fileHandle_->Read(&header, 16);

		// Checking for the header presence
		if (IFileStream::int32FromBE(header.magicId) == 0x504B4D20) // "PKM 10"
		{
			if (IFileStream::int16FromBE(header.version) != 0x3130) { // "10"
				RETURN_MSG_X("PKM version not supported: 0x%04x", header.version);
			}
			if (IFileStream::int16FromBE(header.dataType) != 0) {
				RETURN_MSG_X("PKM data type not supported: 0x%04x", header.dataType);
			}

			headerSize_ = 16;
			width_ = IFileStream::int16FromBE(header.width);
			height_ = IFileStream::int16FromBE(header.height);

			const int extWidth = IFileStream::int16FromBE(header.extendedWidth);
			const int extHeight = IFileStream::int16FromBE(header.extendedHeight);

			LOGI_X("Header found: w:%d h:%d, xw:%d xh:%d", width_, height_, extWidth, extHeight);
		}

		loadPixels(GL_ETC1_RGB8_OES);
		hasLoaded_ = true;
	}
}

#endif