#include "IFileStream.h"
#include "MemoryFile.h"
#include "StandardFile.h"

#ifdef DEATH_TARGET_ANDROID
#	include <cstring>
#	include "AssetFile.h"
#endif

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	IFileStream::IFileStream(const String& filename)
		: type_(FileType::Base), filename_(filename), fileDescriptor_(-1), filePointer_(nullptr),
		shouldCloseOnDestruction_(true), shouldExitOnFailToOpen_(true), fileSize_(0)
	{
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	bool IFileStream::isOpened() const
	{
		if (fileDescriptor_ >= 0 || filePointer_ != nullptr)
			return true;
		else
			return false;
	}

	std::unique_ptr<IFileStream> IFileStream::createFromMemory(const char* bufferName, unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		ASSERT(bufferName);
		ASSERT(bufferPtr);
		ASSERT(bufferSize > 0);
		return std::make_unique<MemoryFile>(bufferName, bufferPtr, bufferSize);
	}

	std::unique_ptr<IFileStream> IFileStream::createFromMemory(const char* bufferName, const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		ASSERT(bufferName);
		ASSERT(bufferPtr);
		ASSERT(bufferSize > 0);
		return std::make_unique<MemoryFile>(bufferName, bufferPtr, bufferSize);
	}

	std::unique_ptr<IFileStream> IFileStream::createFromMemory(unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		ASSERT(bufferPtr);
		ASSERT(bufferSize > 0);
		return std::make_unique<MemoryFile>(bufferPtr, bufferSize);
	}

	std::unique_ptr<IFileStream> IFileStream::createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		ASSERT(bufferPtr);
		ASSERT(bufferSize > 0);
		return std::make_unique<MemoryFile>(bufferPtr, bufferSize);
	}

	std::unique_ptr<IFileStream> IFileStream::createFileHandle(const String& filename)
	{
		ASSERT(filename);
#ifdef DEATH_TARGET_ANDROID
		const char* assetFilename = AssetFile::assetPath(filename);
		if (assetFilename)
			return std::make_unique<AssetFile>(assetFilename);
		else
#endif
			return std::make_unique<StandardFile>(filename);
	}

}
