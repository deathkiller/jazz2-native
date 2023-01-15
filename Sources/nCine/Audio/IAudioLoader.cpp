#include "IAudioLoader.h"
#include "AudioLoaderWav.h"
#if defined(WITH_VORBIS)
#	include "AudioLoaderOgg.h"
#endif
#if defined(WITH_OPENMPT)
#	include "AudioLoaderMpt.h"
#endif
#include "../IO/FileSystem.h"

namespace nCine
{
	IAudioLoader::IAudioLoader(std::unique_ptr<IFileStream> fileHandle)
		: hasLoaded_(false), fileHandle_(std::move(fileHandle)), bytesPerSample_(0),
		numChannels_(0), frequency_(0), numSamples_(0L), duration_(0.0f)
	{
		// Warning: Cannot call a virtual `init()` here, in the base constructor
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		LOGI_X("Loading from memory: 0x%lx, %lu bytes", bufferPtr, bufferSize);
		return createLoader(fs::CreateFromMemory(bufferPtr, bufferSize), { });
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::createFromFile(const StringView& filename)
	{
		LOGI_X("Loading from file \"%s\"", filename.data());
		// Creating a handle from IFile static method to detect assets file
		return createLoader(fs::Open(filename, FileAccessMode::Read), filename);
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::createLoader(std::unique_ptr<IFileStream> fileHandle, const StringView& filename)
	{
		auto extension = fs::GetExtension(filename);
		if (extension == "wav"_s) {
			return std::make_unique<AudioLoaderWav>(std::move(fileHandle));
		}
#if defined(WITH_VORBIS)
		if (extension == "ogg"_s) {
			return std::make_unique<AudioLoaderOgg>(std::move(fileHandle));
		}
#endif
#if defined(WITH_OPENMPT)
		if (extension == "j2b"_s || extension == "it"_s || extension == "s3m"_s || extension == "xm"_s || extension == "mo3"_s) {
			return std::make_unique<AudioLoaderMpt>(std::move(fileHandle));
		}
#endif

		LOGF_X("Extension unknown: \"%s\"", extension.data());
		fileHandle.reset(nullptr);
		return std::make_unique<InvalidAudioLoader>(std::move(fileHandle));
	}
}
