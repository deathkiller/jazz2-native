#include "ITextureLoader.h"
#include "TextureLoaderDds.h"
#include "TextureLoaderPvr.h"
#include "TextureLoaderKtx.h"
//#ifdef WITH_PNG
#include "TextureLoaderPng.h"
//#endif
#ifdef WITH_WEBP
#include "TextureLoaderWebP.h"
#endif
#ifdef WITH_OPENGLES
#include "TextureLoaderPkm.h"
#endif
#include "TextureLoaderQoi.h"

#include "../IO/FileSystem.h"

namespace nCine {

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

	std::unique_ptr<ITextureLoader> ITextureLoader::createFromMemory(const char* bufferName, const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		LOGI_X("Loading memory file: \"%s\" (0x%lx, %lu bytes)", bufferName, bufferPtr, bufferSize);
		return createLoader(std::move(IFileStream::createFromMemory(bufferName, bufferPtr, bufferSize)), bufferName);
	}

	std::unique_ptr<ITextureLoader> ITextureLoader::createFromFile(const char* filename)
	{
		LOGI_X("Loading file: \"%s\"", filename);
		// Creating a handle from IFile static method to detect assets file
		return createLoader(std::move(IFileStream::createFileHandle(filename)), filename);
	}

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	std::unique_ptr<ITextureLoader> ITextureLoader::createLoader(std::unique_ptr<IFileStream> fileHandle, const char* filename)
	{
		fileHandle->setExitOnFailToOpen(false);

		if (fs::hasExtension(filename, "dds"))
			return std::make_unique<TextureLoaderDds>(std::move(fileHandle));
		else if (fs::hasExtension(filename, "pvr"))
			return std::make_unique<TextureLoaderPvr>(std::move(fileHandle));
		else if (fs::hasExtension(filename, "ktx"))
			return std::make_unique<TextureLoaderKtx>(std::move(fileHandle));
	//#ifdef WITH_PNG
		else if (fs::hasExtension(filename, "png"))
			return std::make_unique<TextureLoaderPng>(std::move(fileHandle));
	//#endif
	/*#ifdef WITH_WEBP
		else if (fs::hasExtension(filename, "webp"))
			return std::make_unique<TextureLoaderWebP>(std::move(fileHandle));
	#endif*/
	#ifdef __ANDROID__
		else if (fs::hasExtension(filename, "pkm"))
			return std::make_unique<TextureLoaderPkm>(std::move(fileHandle));
	#endif
		else if (fs::hasExtension(filename, "qoi")) {
			return std::make_unique<TextureLoaderQoi>(std::move(fileHandle));
		} else {
			LOGF_X("Extension unknown: \"%s\"", fs::extension(filename));
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
		LOGI_X("Loading \"%s\"", fileHandle_->filename());
		if (type) // overriding pixel type
			texFormat_ = TextureFormat(internalFormat, type);
		else
			texFormat_ = TextureFormat(internalFormat);

		// If the file has not been already opened by a header reader method
		if (!fileHandle_->isOpened())
			fileHandle_->Open(FileAccessMode::Read);

		dataSize_ = fileHandle_->GetSize() - headerSize_;
		fileHandle_->Seek(headerSize_, SeekOrigin::Current);

		pixels_ = std::make_unique<unsigned char[]>(dataSize_);
		fileHandle_->Read(pixels_.get(), dataSize_);
	}

}
