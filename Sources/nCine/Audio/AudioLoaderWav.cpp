#include "AudioLoaderWav.h"
#include "AudioReaderWav.h"
#include "../../Main.h"

#include <cstring>

#include <Base/Memory.h>

using namespace Death::IO;
using namespace Death::Memory;

namespace nCine
{
	AudioLoaderWav::AudioLoaderWav(std::unique_ptr<Stream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
		if (!_fileHandle->IsValid()) {
			return;
		}

		WavHeader header;
		_fileHandle->Read(&header, sizeof(WavHeader));

		DEATH_ASSERT(strncmp(header.chunkId, "RIFF", 4) == 0 && strncmp(header.format, "WAVE", 4) == 0, "Invalid WAV signature", );
		DEATH_ASSERT(strncmp(header.subchunk1Id, "fmt ", 4) == 0, "Invalid WAV signature", );
		DEATH_ASSERT(AsLE(header.audioFormat) == 1, "Data is not in PCM format", );

		_bytesPerSample = AsLE(header.bitsPerSample) / 8;
		_numChannels = AsLE(header.numChannels);
		_frequency = AsLE(header.sampleRate);

		_numSamples = AsLE(header.subchunk2Size) / (_numChannels * _bytesPerSample);
		_duration = float(_numSamples) / _frequency;

		DEATH_ASSERT(_numChannels == 1 || _numChannels == 2, ("Unsupported number of channels: {}", _numChannels), );
		LOGD("Duration: {:.2}s, channels: {}, frequency: {} Hz", _duration, _numChannels, _frequency);

		_hasLoaded = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderWav::createReader()
	{
		return std::make_unique<AudioReaderWav>(std::move(_fileHandle), _bytesPerSample);
	}
}
