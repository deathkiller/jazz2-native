#include "IAudioLoader.h"
#include "AudioLoaderWav.h"
#if defined(WITH_VORBIS)
#	include "AudioLoaderOgg.h"
#endif
#if defined(WITH_OPENMPT)
#	include "AudioLoaderMpt.h"
#endif

#include <IO/FileSystem.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	IAudioLoader::IAudioLoader(std::unique_ptr<Stream> fileHandle)
		: hasLoaded_(false), fileHandle_(std::move(fileHandle)), bytesPerSample_(0), numChannels_(0), frequency_(0), numSamples_(0L), duration_(0.0f)
	{
	}

	/*std::unique_ptr<IAudioLoader> IAudioLoader::createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		// TODO: path cannot be null, otherwise InvalidAudioLoader will be created
		//LOGI("Loading from memory: 0x%lx, %lu bytes", bufferPtr, bufferSize);
		return createLoader(fs::CreateFromMemory(bufferPtr, bufferSize), { });
	}*/

	std::unique_ptr<IAudioLoader> IAudioLoader::createFromFile(const StringView path)
	{
		return createLoader(fs::Open(path, FileAccessMode::Read), path);
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::createFromStream(std::unique_ptr<Stream> fileHandle, const StringView path)
	{
		return createLoader(std::move(fileHandle), path);
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::createLoader(std::unique_ptr<Stream> fileHandle, const StringView path)
	{
		auto extension = fs::GetExtension(path);
		if (extension == "wav"_s) {
			return std::make_unique<AudioLoaderWav>(std::move(fileHandle));
		}
#if defined(WITH_VORBIS)
		if (extension == "ogg"_s) {
			return std::make_unique<AudioLoaderOgg>(std::move(fileHandle));
		}
#endif
#if defined(WITH_OPENMPT)
		if (extension == "it"_s || extension == "j2b"_s || extension == "mo3"_s || extension == "mod"_s || extension == "s3m"_s || extension == "xm"_s) {
			return std::make_unique<AudioLoaderMpt>(std::move(fileHandle));
		}
#endif

		LOGF("Unknown extension: %s", extension.data());
		fileHandle.reset(nullptr);
		return std::make_unique<InvalidAudioLoader>(std::move(fileHandle));
	}
}
