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
		if (!fileHandle_->IsValid()) {
			return;
		}

		WavHeader header;
		fileHandle_->Read(&header, sizeof(WavHeader));

		DEATH_ASSERT(strncmp(header.chunkId, "RIFF", 4) == 0 && strncmp(header.format, "WAVE", 4) == 0, "Invalid WAV signature", );
		DEATH_ASSERT(strncmp(header.subchunk1Id, "fmt ", 4) == 0, "Invalid WAV signature", );
		DEATH_ASSERT(AsLE(header.audioFormat) == 1, "Data is not in PCM format", );

		bytesPerSample_ = AsLE(header.bitsPerSample) / 8;
		numChannels_ = AsLE(header.numChannels);
		frequency_ = AsLE(header.sampleRate);

		numSamples_ = AsLE(header.subchunk2Size) / (numChannels_ * bytesPerSample_);
		duration_ = float(numSamples_) / frequency_;

		DEATH_ASSERT(numChannels_ == 1 || numChannels_ == 2, ("Unsupported number of channels: {}", numChannels_), );
		LOGD("Duration: {:.2}s, channels: {}, frequency: {} Hz", duration_, numChannels_, frequency_);

		hasLoaded_ = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderWav::createReader()
	{
		return std::make_unique<AudioReaderWav>(std::move(fileHandle_));
	}
}
