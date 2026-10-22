#include "AudioReaderWav.h"
#include "AudioLoaderWav.h"
#include "../../Main.h"

#include <algorithm>
#include <cstring>

#include <Base/Memory.h>
#include <IO/FileStream.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
	AudioReaderWav::AudioReaderWav(std::unique_ptr<Stream> fileHandle, std::int32_t bytesPerSample, std::int64_t dataOffset, std::int64_t dataSize)
		: _fileHandle(Death::move(fileHandle)), _bytesPerSample(bytesPerSample), _dataOffset(dataOffset), _dataSize(dataSize)
	{
		DEATH_ASSERT(_fileHandle->IsValid());
	}

	std::int32_t AudioReaderWav::read(void* buffer, std::int32_t bufferSize) const
	{
		DEATH_ASSERT(buffer);
		DEATH_ASSERT(bufferSize > 0);

		// Whatever follows the samples in the file isn't sound
		std::int64_t bytesLeft = _dataOffset + _dataSize - _fileHandle->GetPosition();
		if (bytesLeft < bufferSize) {
			bufferSize = std::int32_t(std::max<std::int64_t>(bytesLeft, 0));
		}

		std::int32_t bytes = 0;
		std::int32_t bufferSeek = 0;

		while (bufferSize - bufferSeek > 0) {
			// Read up to a buffer's worth of decoded sound data
			bytes = std::int32_t(_fileHandle->Read(static_cast<std::uint8_t*>(buffer) + bufferSeek, bufferSize - bufferSeek));
			if (bytes <= 0) {
				break;
			}
			bufferSeek += bytes;
		}

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
		_fileHandle->Seek(_dataOffset, SeekOrigin::Begin);
	}
}
