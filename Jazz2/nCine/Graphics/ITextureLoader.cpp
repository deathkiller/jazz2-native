#include "ITextureLoader.h"
#include "../../Common.h"

#include "TextureLoaderDds.h"
#include "TextureLoaderPvr.h"
#include "TextureLoaderKtx.h"
//#ifdef WITH_PNG
#include "TextureLoaderPng.h"
//#endif
#ifdef WITH_WEBP
#	include "TextureLoaderWebP.h"
#endif
#ifdef WITH_OPENGLES
#	include "TextureLoaderPkm.h"
#endif
#include "TextureLoaderQoi.h"

#include "../IO/FileSystem.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	ITextureLoader::ITextureLoader()
		: hasLoaded_(false), width_(0), height_(0),
		headerSize_(0), dataSize_(0), mipMapCount_(1)
	{
	}

	ITextureLoader::ITextureLoader(std::unique_ptr<IFileStream> fileHandle)
		: hasLoaded_(false), fileHandle_(std::move(fileHandle)),
		width_(0), height_(0), headerSize_(0), dataSize_(0), mipMapCount_(1)
	{
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	long ITextureLoader::dataSize(unsigned int mipMapLevel) const
	{
		long int dataSize = 0;

		if (mipMapCount_ > 1 && int(mipMapLevel) < mipMapCount_)
			dataSize = mipDataSizes_[mipMapLevel];
		else if (mipMapLevel == 0)
			dataSize = dataSize_;

		return dataSize;
	}

	const GLubyte* ITextureLoader::pixels(unsigned int mipMapLevel) const
	{
		const GLubyte* pixels = nullptr;

		if (pixels_ != nullptr) {
			if (mipMapCount_ > 1 && int(mipMapLevel) < mipMapCount_)
				pixels = pixels_.get() + mipDataOffsets_[mipMapLevel];
			else if (mipMapLevel == 0)
				pixels = pixels_.get();
		}

		return pixels;
	}

	std::unique_ptr<ITextureLoader> ITextureLoader::createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		LOGI_X("Loading from memory: 0x%lx, %lu bytes", bufferPtr, bufferSize);
		return createLoader(std::move(fs::CreateFromMemory(bufferPtr, bufferSize)), { });
	}

	std::unique_ptr<ITextureLoader> ITextureLoader::createFromFile(const StringView& filename)
	{
		LOGI_X("Loading from file \"%s\"", filename.data());
		// Creating a handle from IFile static method to detect assets file
		return createLoader(std::move(fs::Open(filename, FileAccessMode::Read)), filename);
	}

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	std::unique_ptr<ITextureLoader> ITextureLoader::createLoader(std::unique_ptr<IFileStream> fileHandle, const StringView& filename)
	{
		auto extension = fs::GetExtension(filename);
		if (extension == "dds"_s)
			return std::make_unique<TextureLoaderDds>(std::move(fileHandle));
		else if (extension == "pvr"_s)
			return std::make_unique<TextureLoaderPvr>(std::move(fileHandle));
		else if (extension == "ktx"_s)
			return std::make_unique<TextureLoaderKtx>(std::move(fileHandle));
	//#ifdef WITH_PNG
		else if (extension == "png"_s)
			return std::make_unique<TextureLoaderPng>(std::move(fileHandle));
	//#endif
	/*#ifdef WITH_WEBP
		else if (extension == "webp"_s)
			return std::make_unique<TextureLoaderWebP>(std::move(fileHandle));
	#endif*/
	#ifdef DEATH_TARGET_ANDROID
		else if (extension == "pkm"_s)
			return std::make_unique<TextureLoaderPkm>(std::move(fileHandle));
	#endif
		else if (extension == "qoi"_s) {
			return std::make_unique<TextureLoaderQoi>(std::move(fileHandle));
		} else {
			LOGF_X("Extension unknown: \"%s\"", extension.data());
			fileHandle.reset(nullptr);
			return std::make_unique<InvalidTextureLoader>(std::move(fileHandle));
		}
	}

	void ITextureLoader::loadPixels(GLenum internalFormat)
	{
		loadPixels(internalFormat, 0);
	}

	void ITextureLoader::loadPixels(GLenum internalFormat, GLenum type)
	{
		LOGI_X("Loading \"%s\"", fileHandle_->GetFilename());
		if (type) // overriding pixel type
			texFormat_ = TextureFormat(internalFormat, type);
		else
			texFormat_ = TextureFormat(internalFormat);

		// If the file has not been already opened by a header reader method
		if (!fileHandle_->IsOpened()) {
			fileHandle_->Open(FileAccessMode::Read, false);
		}

		dataSize_ = fileHandle_->GetSize() - headerSize_;
		fileHandle_->Seek(headerSize_, SeekOrigin::Current);

		pixels_ = std::make_unique<unsigned char[]>(dataSize_);
		fileHandle_->Read(pixels_.get(), dataSize_);
	}

}
