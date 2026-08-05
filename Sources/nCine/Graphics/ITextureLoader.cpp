#include "ITextureLoader.h"
#include "../../Main.h"

#include "TextureLoaderDds.h"
#include "TextureLoaderPvr.h"
#include "TextureLoaderKtx.h"
#include "TextureLoaderPng.h"
#if defined(WITH_WEBP)
#	include "TextureLoaderWebP.h"
#endif
#if defined(DEATH_TARGET_ANDROID) && defined(RHI_GL_PROFILE_ES)
#	include "TextureLoaderPkm.h"
#endif
#if defined(WITH_QOI)
#	include "TextureLoaderQoi.h"
#endif

#include <IO/FileSystem.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	ITextureLoader::ITextureLoader()
		: _hasLoaded(false), _width(0), _height(0), _headerSize(0), _dataSize(0), _mipMapCount(1)
	{
	}

	ITextureLoader::ITextureLoader(std::unique_ptr<Stream> fileHandle)
		: _hasLoaded(false), _fileHandle(std::move(fileHandle)), _width(0), _height(0), _headerSize(0), _dataSize(0), _mipMapCount(1)
	{
	}

	std::int32_t ITextureLoader::dataSize(std::uint32_t mipMapLevel) const
	{
		std::int32_t dataSize = 0;
		if (_mipMapCount > 1 && std::uint32_t(mipMapLevel) < _mipMapCount) {
			dataSize = _mipDataSizes[mipMapLevel];
		} else if (mipMapLevel == 0) {
			dataSize = _dataSize;
		}
		return dataSize;
	}

	const std::uint8_t* ITextureLoader::pixels(std::uint32_t mipMapLevel) const
	{
		const std::uint8_t* pixels = nullptr;

		if (_pixels != nullptr) {
			if (_mipMapCount > 1 && std::int32_t(mipMapLevel) < _mipMapCount) {
				pixels = _pixels.get() + _mipDataOffsets[mipMapLevel];
			} else if (mipMapLevel == 0) {
				pixels = _pixels.get();
			}
		}

		return pixels;
	}

	/*std::unique_ptr<ITextureLoader> ITextureLoader::createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		// TODO: path cannot be null, otherwise InvalidAudioLoader will be created
		//LOGI("Loading from memory: 0x{:x}, {} bytes", bufferPtr, bufferSize);
		return createLoader(std::make_unique<MemoryStream>(bufferPtr, bufferSize), {});
	}*/

	std::unique_ptr<ITextureLoader> ITextureLoader::createFromFile(const StringView path)
	{
		return createLoader(fs::Open(path, FileAccess::Read), path);
	}

	std::unique_ptr<ITextureLoader> ITextureLoader::createFromStream(std::unique_ptr<Stream> fileHandle, const StringView path)
	{
		return createLoader(std::move(fileHandle), path);
	}

	std::unique_ptr<ITextureLoader> ITextureLoader::createLoader(std::unique_ptr<Stream> fileHandle, const StringView path)
	{
		auto extension = fs::GetExtension(path);
		if (extension == "dds"_s) {
			return std::make_unique<TextureLoaderDds>(std::move(fileHandle));
		}
		if (extension == "pvr"_s) {
			return std::make_unique<TextureLoaderPvr>(std::move(fileHandle));
		}
		if (extension == "ktx"_s) {
			return std::make_unique<TextureLoaderKtx>(std::move(fileHandle));
		}
		if (extension == "png"_s) {
			return std::make_unique<TextureLoaderPng>(std::move(fileHandle));
		}
/*#if defined(WITH_WEBP)
		if (extension == "webp"_s) {
			return std::make_unique<TextureLoaderWebP>(std::move(fileHandle));
		}
#endif*/
#if defined(DEATH_TARGET_ANDROID) && defined(RHI_GL_PROFILE_ES)
		if (extension == "pkm"_s) {
			return std::make_unique<TextureLoaderPkm>(std::move(fileHandle));
		}
#endif
#if defined(WITH_QOI)
		if (extension == "qoi"_s) {
			return std::make_unique<TextureLoaderQoi>(std::move(fileHandle));
		}
#endif

		LOGF("Unknown extension: {}", extension);
		fileHandle.reset(nullptr);
		return std::make_unique<InvalidTextureLoader>(std::move(fileHandle));
	}

	void ITextureLoader::loadPixels(PixelFormat format)
	{
		_texFormat = TextureFormat(format);

		_dataSize = _fileHandle->GetSize() - _headerSize;
		_fileHandle->Seek(_headerSize, SeekOrigin::Current);

		_pixels = std::make_unique<std::uint8_t[]>(_dataSize);
		_fileHandle->Read(_pixels.get(), _dataSize);
	}
}
