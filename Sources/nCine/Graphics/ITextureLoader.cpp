#include "ITextureLoader.h"
#include "../../Common.h"

#include "TextureLoaderDds.h"
#include "TextureLoaderPvr.h"
#include "TextureLoaderKtx.h"
#include "TextureLoaderPng.h"
#if defined(WITH_WEBP)
#	include "TextureLoaderWebP.h"
#endif
#if defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES)
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
		: hasLoaded_(false), width_(0), height_(0), headerSize_(0), dataSize_(0), mipMapCount_(1)
	{
	}

	ITextureLoader::ITextureLoader(std::unique_ptr<Stream> fileHandle)
		: hasLoaded_(false), fileHandle_(std::move(fileHandle)), width_(0), height_(0), headerSize_(0), dataSize_(0), mipMapCount_(1)
	{
	}

	long ITextureLoader::dataSize(unsigned int mipMapLevel) const
	{
		long int dataSize = 0;
		if (mipMapCount_ > 1 && int(mipMapLevel) < mipMapCount_) {
			dataSize = mipDataSizes_[mipMapLevel];
		} else if (mipMapLevel == 0) {
			dataSize = dataSize_;
		}
		return dataSize;
	}

	const GLubyte* ITextureLoader::pixels(unsigned int mipMapLevel) const
	{
		const GLubyte* pixels = nullptr;

		if (pixels_ != nullptr) {
			if (mipMapCount_ > 1 && int(mipMapLevel) < mipMapCount_) {
				pixels = pixels_.get() + mipDataOffsets_[mipMapLevel];
			} else if (mipMapLevel == 0) {
				pixels = pixels_.get();
			}
		}

		return pixels;
	}

	/*std::unique_ptr<ITextureLoader> ITextureLoader::createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		// TODO: path cannot be null, otherwise InvalidAudioLoader will be created
		//LOGI("Loading from memory: 0x%lx, %lu bytes", bufferPtr, bufferSize);
		return createLoader(fs::CreateFromMemory(bufferPtr, bufferSize), { });
	}*/

	std::unique_ptr<ITextureLoader> ITextureLoader::createFromFile(const StringView path)
	{
		//LOGD("Loading from file \"%s\"", path.data());
		return createLoader(fs::Open(path, FileAccess::Read), path);
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
#if defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES)
		if (extension == "pkm"_s) {
			return std::make_unique<TextureLoaderPkm>(std::move(fileHandle));
		}
#endif
#if defined(WITH_QOI)
		if (extension == "qoi"_s) {
			return std::make_unique<TextureLoaderQoi>(std::move(fileHandle));
		}
#endif

		LOGF("Unknown extension: %s", extension.data());
		fileHandle.reset(nullptr);
		return std::make_unique<InvalidTextureLoader>(std::move(fileHandle));
	}

	void ITextureLoader::loadPixels(GLenum internalFormat)
	{
		loadPixels(internalFormat, 0);
	}

	void ITextureLoader::loadPixels(GLenum internalFormat, GLenum type)
	{
		if (type) { // overriding pixel type
			texFormat_ = TextureFormat(internalFormat, type);
		} else {
			texFormat_ = TextureFormat(internalFormat);
		}

		dataSize_ = fileHandle_->GetSize() - headerSize_;
		fileHandle_->Seek(headerSize_, SeekOrigin::Current);

		pixels_ = std::make_unique<unsigned char[]>(dataSize_);
		fileHandle_->Read(pixels_.get(), dataSize_);
	}
}
