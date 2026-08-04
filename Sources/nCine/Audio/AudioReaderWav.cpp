#include "AudioReaderWav.h"
#include "AudioLoaderWav.h"
#include "../../Main.h"

#include <cstring>

#include <Base/Memory.h>
#include <IO/FileStream.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
	AudioReaderWav::AudioReaderWav(std::unique_ptr<Stream> fileHandle, std::int32_t bytesPerSample)
		: _fileHandle(std::move(fileHandle)), _bytesPerSample(bytesPerSample)
	{
		DEATH_ASSERT(_fileHandle->IsValid());
	}

	std::int32_t AudioReaderWav::read(void* buffer, std::int32_t bufferSize) const
	{
		DEATH_ASSERT(buffer);
		DEATH_ASSERT(bufferSize > 0);

		std::int32_t bytes = 0;
		std::int32_t bufferSeek = 0;

		do {
			// Read up to a buffer's worth of decoded sound data
			bytes = _fileHandle->Read(static_cast<std::uint8_t*>(buffer) + bufferSeek, bufferSize - bufferSeek);
			bufferSeek += bytes;
		} while (bytes > 0 && bufferSize - bufferSeek > 0);

#if defined(DEATH_TARGET_BIG_ENDIAN)
		// The samples are stored little-endian in the file, but the buffer formats of IAudioDevice are
		// native-endian, so 16-bit data has to be swapped here (8-bit data is unaffected)
		if (_bytesPerSample == 2) {
			std::uint16_t* samples = static_cast<std::uint16_t*>(buffer);
			for (std::int32_t i = 0, n = bufferSeek / 2; i < n; i++) {
				samples[i] = SwapBytes(samples[i]);
			}
		}
#endif

		return bufferSeek;
	}

	void AudioReaderWav::rewind() const
	{
		_fileHandle->Seek(AudioLoaderWav::HeaderSize, SeekOrigin::Begin);
	}
}
