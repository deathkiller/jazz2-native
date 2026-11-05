#include "IAudioLoader.h"

#if defined(WITH_AUDIO)
#	include "AudioLoaderWav.h"
#	if defined(WITH_VORBIS)
#		include "AudioLoaderOgg.h"
#	endif
#	if defined(WITH_OPENMPT)
#		include "AudioLoaderMpt.h"
#	elif defined(WITH_XMP)
#		include "AudioLoaderXmp.h"
#	endif
#endif

#include <IO/FileSystem.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	IAudioLoader::IAudioLoader(std::unique_ptr<Stream> fileHandle)
		: _hasLoaded(false), _fileHandle(Death::move(fileHandle)), _bytesPerSample(0), _numChannels(0),
			_frequency(0), _numSamples(0L), _duration(0.0f)
	{
	}

	/*std::unique_ptr<IAudioLoader> IAudioLoader::createFromMemory(const unsigned char* bufferPtr, unsigned long int bufferSize)
	{
		// TODO: path cannot be null, otherwise InvalidAudioLoader will be created
		//LOGI("Loading from memory: 0x{:x}, {} bytes", bufferPtr, bufferSize);
		return CreateLoader(std::make_unique<MemoryStream>(bufferPtr, bufferSize), {});
	}*/

	std::unique_ptr<IAudioLoader> IAudioLoader::CreateFromFile(const StringView path)
	{
		return CreateLoader(fs::Open(path, FileAccess::Read), path);
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::CreateFromStream(std::unique_ptr<Stream> fileHandle, StringView path)
	{
		return CreateLoader(Death::move(fileHandle), path);
	}

	std::unique_ptr<IAudioLoader> IAudioLoader::CreateLoader(std::unique_ptr<Stream> fileHandle, StringView path)
	{
#if defined(WITH_AUDIO)
		auto extension = fs::GetExtension(path);
		if (extension == "wav"_s) {
			return std::make_unique<AudioLoaderWav>(Death::move(fileHandle));
		}
#	if defined(WITH_VORBIS)
		if (extension == "ogg"_s) {
			return std::make_unique<AudioLoaderOgg>(Death::move(fileHandle));
		}
#	endif
#	if defined(WITH_OPENMPT)
		if (extension == "it"_s || extension == "j2b"_s || extension == "mo3"_s || extension == "mod"_s || extension == "s3m"_s || extension == "xm"_s) {
			return std::make_unique<AudioLoaderMpt>(Death::move(fileHandle));
		}
#	elif defined(WITH_XMP)
		// libxmp for platforms where libopenmpt is too slow (no `.mo3`, libxmp can't parse it)
		if (extension == "it"_s || extension == "j2b"_s || extension == "mod"_s || extension == "s3m"_s || extension == "xm"_s) {
			return std::make_unique<AudioLoaderXmp>(Death::move(fileHandle));
		}
#	endif

		LOGE("Audio file with extension \".{}\" is not supported", extension);
#endif
		return std::make_unique<InvalidAudioLoader>();
	}
}
