#include "AudioLoaderWav.h"
#include "AudioReaderWav.h"
#include "../../Common.h"

#include <cstring>

using namespace Death::IO;

namespace nCine
{
	AudioLoaderWav::AudioLoaderWav(std::unique_ptr<Stream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
		RETURN_ASSERT(fileHandle_->IsValid());

		WavHeader header;
		fileHandle_->Read(&header, sizeof(WavHeader));

		RETURN_ASSERT_MSG(strncmp(header.chunkId, "RIFF", 4) == 0 && strncmp(header.format, "WAVE", 4) == 0, "Invalid WAV signature");
		RETURN_ASSERT_MSG(strncmp(header.subchunk1Id, "fmt ", 4) == 0, "Invalid WAV signature");
		RETURN_ASSERT_MSG(Stream::Uint16FromLE(header.audioFormat) == 1, "Data is not in PCM format");

		bytesPerSample_ = Stream::Uint16FromLE(header.bitsPerSample) / 8;
		numChannels_ = Stream::Uint16FromLE(header.numChannels);
		frequency_ = Stream::Uint32FromLE(header.sampleRate);

		numSamples_ = Stream::Uint32FromLE(header.subchunk2Size) / (numChannels_ * bytesPerSample_);
		duration_ = float(numSamples_) / frequency_;

		RETURN_ASSERT_MSG(numChannels_ == 1 || numChannels_ == 2, "Unsupported number of channels: %d", numChannels_);
		LOGD("Duration: %.2fs, channels: %d, frequency: %dHz", duration_, numChannels_, frequency_);

		hasLoaded_ = true;
	}

	std::unique_ptr<IAudioReader> AudioLoaderWav::createReader()
	{
		return std::make_unique<AudioReaderWav>(std::move(fileHandle_));
	}
}
