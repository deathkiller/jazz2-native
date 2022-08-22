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
		return createLoader(std::move(fs::CreateFromMemory(bufferName, bufferPtr, bufferSize)), bufferName);
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::createFromFile(const StringView& filename)
	{
		LOGI_X("Loading file: \"%s\"", filename.data());
		// Creating a handle from IFile static method to detect assets file
		return createLoader(std::move(fs::Open(filename, FileAccessMode::Read)), filename);
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	std::unique_ptr<IAudioLoader> IAudioLoader::createLoader(std::unique_ptr<IFileStream> fileHandle, const StringView& filename)
	{
		if (fs::HasExtension(filename, "wav"_s)) {
			return std::make_unique<AudioLoaderWav>(std::move(fileHandle));
		}
#ifdef WITH_VORBIS
		else if (fs::HasExtension(filename, "ogg"_s)) {
			return std::make_unique<AudioLoaderOgg>(std::move(fileHandle));
		}
#endif
#ifdef WITH_OPENMPT
		else if (fs::HasExtension(filename, "j2b"_s) || fs::HasExtension(filename, "it"_s) || fs::HasExtension(filename, "s3m"_s) ||
				 fs::HasExtension(filename, "xm"_s) || fs::HasExtension(filename, "mo3"_s)) {
			return std::make_unique<AudioLoaderMpt>(std::move(fileHandle));
		}
#endif
		else {
			LOGF_X("Extension unknown: \"%s\"", fs::GetExtension(filename).data());
			fileHandle.reset(nullptr);
			return std::make_unique<InvalidAudioLoader>(std::move(fileHandle));
		}

		return nullptr;
	}

}
