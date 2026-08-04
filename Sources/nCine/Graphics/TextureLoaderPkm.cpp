#include "TextureLoaderPkm.h"

#if defined(DEATH_TARGET_ANDROID) && defined(RHI_GL_PROFILE_ES)

#include <cstring>

#include <Base/Memory.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
	TextureLoaderPkm::TextureLoaderPkm(std::unique_ptr<Stream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
		if (!_fileHandle->IsValid()) {
			return;
		}

		PkmHeader header;
		_fileHandle->Read(&header, 16); // PKM header is 16 bytes long

		// Checking for the header presence
		DEATH_ASSERT(AsBE(header.magicId) == 0x504B4D20 /* "PKM 10" */, "Invalid PKM signature", );
		DEATH_ASSERT(AsBE(header.version) == 0x3130 /* "10" */, ("PKM version not supported: 0x{:.4x}", header.version), );
		DEATH_ASSERT(AsBE(header.dataType) == 0, ("PKM data type not supported: 0x{:.4x}", header.dataType), );

		_headerSize = 16;
		_width = AsBE(header.width);
		_height = AsBE(header.height);

		const int extWidth = AsBE(header.extendedWidth);
		const int extHeight = AsBE(header.extendedHeight);

		LOGI("Header found: w:{} h:{}, xw:{} xh:{}", _width, _height, extWidth, extHeight);

		loadPixels(PixelFormat::ETC1);
		_hasLoaded = true;
	}
}

#endif