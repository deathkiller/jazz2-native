#include "IAudioLoader.h"
#include "AudioLoaderWav.h"
#ifdef WITH_VORBIS
#include "AudioLoaderOgg.h"
#endif
#include "AudioLoaderMpt.h"
#include "../IO/FileSystem.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	IAudioLoader::IAudioLoader(std::unique_ptr<IFileStream> fileHandle)
		: hasLoaded_(false), fileHandle_(std::move(fileHandle)), bytesPerSample_(0),
		numChannels_(0), frequency_(0), numSamples_(0L), duration_(0.0f)
	{
		// Warning: Cannot call a virtual `init()` here, in the base constructor
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	std::unique_ptr<IAudioLoader> IAudioLoader::createFromMemory(const char* bufferName, const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		LOGI_X("Loading memory file: \"%s\" (0x%lx, %lu bytes)", bufferName, bufferPtr, bufferSize);
		return createLoader(std::move(IFileStream::createFromMemory(bufferName, bufferPtr, bufferSize)), bufferName);
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::createFromFile(const char* filename)
	{
		LOGI_X("Loading file: \"%s\"", filename);
		// Creating a handle from IFile static method to detect assets file
		return createLoader(std::move(IFileStream::createFileHandle(filename)), filename);
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	std::unique_ptr<IAudioLoader> IAudioLoader::createLoader(std::unique_ptr<IFileStream> fileHandle, const char* filename)
	{
		fileHandle->setExitOnFailToOpen(false);

		if (fs::hasExtension(filename, "wav")) {
			return std::make_unique<AudioLoaderWav>(std::move(fileHandle));
		}
#ifdef WITH_VORBIS
		else if (fs::hasExtension(filename, "ogg")) {
			return std::make_unique<AudioLoaderOgg>(std::move(fileHandle));
		}
#endif
		if (fs::hasExtension(filename, "j2b") || fs::hasExtension(filename, "it") || fs::hasExtension(filename, "s3m") ||
			fs::hasExtension(filename, "xm") || fs::hasExtension(filename, "mo3")) {
			return std::make_unique<AudioLoaderMpt>(std::move(fileHandle));
		} else {
			LOGF_X("Extension unknown: \"%s\"", fs::extension(filename));
			fileHandle.reset(nullptr);
			return std::make_unique<InvalidAudioLoader>(std::move(fileHandle));
		}

		return nullptr;
	}

}
