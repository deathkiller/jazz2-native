#include "TextureLoaderQoi.h"

#if defined(WITH_QOI)

#define QOI_IMPLEMENTATION
#define QOI_DECODE_ONLY
#define QOI_NO_STDIO
#include <qoi.h>

using namespace Death::IO;

namespace nCine
{
	TextureLoaderQoi::TextureLoaderQoi(std::unique_ptr<Stream> fileHandle)
		: ITextureLoader(std::move(fileHandle))
	{
		if (!_fileHandle->IsValid()) {
			return;
		}

		auto fileSize = _fileHandle->GetSize();
		if (fileSize < QOI_HEADER_SIZE || fileSize > 64 * 1024 * 1024) {
			// 64 MB file size limit, files are usually smaller than 1MB
			return;
		}

		auto buffer = std::make_unique<char[]>(fileSize);
		_fileHandle->Read(buffer.get(), fileSize);

		qoi_desc desc = { };
		void* data = qoi_decode(buffer.get(), fileSize, &desc, 4);
		if (data == nullptr) {
			return;
		}

		int imageSize = desc.width * desc.height * desc.channels;
		_pixels = std::make_unique<std::uint8_t[]>(imageSize);
		// TODO: remove this additional copy
		memcpy(_pixels.get(), data, imageSize);
		QOI_FREE(data);

		_width = desc.width;
		_height = desc.height;
		_mipMapCount = 1;
		_texFormat = TextureFormat(PixelFormat::RGBA8);

		_hasLoaded = true;
	}
}

#endif