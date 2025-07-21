#include "AudioReaderWav.h"
#include "AudioLoaderWav.h"
#include "../../Main.h"

#include <cstring>

#include <IO/FileStream.h>

using namespace Death::IO;

namespace nCine
{
	AudioReaderWav::AudioReaderWav(std::unique_ptr<Stream> fileHandle)
		: fileHandle_(std::move(fileHandle))
	{
		DEATH_ASSERT(fileHandle_->IsValid());
	}

	std::int32_t AudioReaderWav::read(void* buffer, std::int32_t bufferSize) const
	{
		DEATH_ASSERT(buffer);
		DEATH_ASSERT(bufferSize > 0);

		std::int32_t bytes = 0;
		std::int32_t bufferSeek = 0;

		do {
			// Read up to a buffer's worth of decoded sound data
			bytes = fileHandle_->Read(buffer, bufferSize);
			bufferSeek += bytes;
		} while (bytes > 0 && bufferSize - bufferSeek > 0);

		return bufferSeek;
	}

	void AudioReaderWav::rewind() const
	{
		fileHandle_->Seek(AudioLoaderWav::HeaderSize, SeekOrigin::Begin);
	}
}
