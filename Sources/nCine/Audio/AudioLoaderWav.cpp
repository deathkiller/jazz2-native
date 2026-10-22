#include "AudioLoaderWav.h"
#include "AudioReaderWav.h"
#include "../../Main.h"

#include <algorithm>
#include <cstring>

#include <Base/Memory.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
	AudioLoaderWav::AudioLoaderWav(std::unique_ptr<Stream> fileHandle)
		: IAudioLoader(Death::move(fileHandle)), _dataOffset(0), _dataSize(0)
	{
		if (!_fileHandle->IsValid()) {
			return;
		}

		RiffHeader header;
		if (_fileHandle->Read(&header, sizeof(RiffHeader)) != sizeof(RiffHeader)) {
			header.chunkId[0] = '\0';
		}

		DEATH_ASSERT(strncmp(header.chunkId, "RIFF", 4) == 0 && strncmp(header.format, "WAVE", 4) == 0, "Invalid WAV signature", );

		// The chunks can be in any order and there can be others among them (e.g. "LIST" or "bext"), and the "fmt " chunk
		// can be longer than the PCM format needs. The samples are in the "data" chunk, the reader starts right at it.
		FormatChunk format = {};
		bool hasFormat = false;
		bool hasData = false;
		ChunkHeader chunk;
		while (_fileHandle->Read(&chunk, sizeof(ChunkHeader)) == sizeof(ChunkHeader)) {
			std::int64_t chunkSize = AsLE(chunk.size);
			if (strncmp(chunk.id, "data", 4) == 0) {
				_dataOffset = _fileHandle->GetPosition();
				_dataSize = chunkSize;
				hasData = true;
				break;
			}
			if (strncmp(chunk.id, "fmt ", 4) == 0 && chunkSize >= std::int64_t(sizeof(FormatChunk))) {
				if (_fileHandle->Read(&format, sizeof(FormatChunk)) != sizeof(FormatChunk)) {
					break;
				}
				hasFormat = true;
				chunkSize -= sizeof(FormatChunk);
			}
			// The chunks are padded to an even size
			if (_fileHandle->Seek(chunkSize + (AsLE(chunk.size) & 1), SeekOrigin::Current) < 0) {
				break;
			}
		}

		DEATH_ASSERT(hasFormat && hasData, "Invalid WAV signature", );
		DEATH_ASSERT(AsLE(format.audioFormat) == 1, "Data is not in PCM format", );

		_bytesPerSample = AsLE(format.bitsPerSample) / 8;
		_numChannels = AsLE(format.numChannels);
		_frequency = AsLE(format.sampleRate);

		DEATH_ASSERT(_numChannels == 1 || _numChannels == 2, ("Unsupported number of channels: {}", _numChannels), );
		DEATH_ASSERT(_bytesPerSample == 1 || _bytesPerSample == 2, ("Unsupported number of bytes per sample: {}", _bytesPerSample), );

		// A file that was cut short (or one that was being recorded) can say more than there is
		std::int64_t fileSize = _fileHandle->GetSize();
		if (fileSize >= 0 && _dataOffset + _dataSize > fileSize) {
			_dataSize = std::max<std::int64_t>(fileSize - _dataOffset, 0);
		}

		_numSamples = std::int32_t(_dataSize / (_numChannels * _bytesPerSample));
		_duration = float(_numSamples) / _frequency;

		LOGD("Duration: {:.2}s, channels: {}, frequency: {} Hz", _duration, _numChannels, _frequency);

		_hasLoaded = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderWav::createReader()
	{
		return std::make_unique<AudioReaderWav>(Death::move(_fileHandle), _bytesPerSample, _dataOffset, _dataSize);
	}
}
