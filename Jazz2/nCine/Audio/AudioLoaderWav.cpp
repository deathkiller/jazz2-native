#include "AudioLoaderWav.h"
#include "AudioReaderWav.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	AudioLoaderWav::AudioLoaderWav(std::unique_ptr<IFileStream> fileHandle)
		: IAudioLoader(std::move(fileHandle))
	{
		//LOGI_X("Loading \"%s\"", fileHandle_->filename());
		fileHandle_->Open(FileAccessMode::Read);
		//RETURN_ASSERT_MSG_X(fileHandle_->isOpened(), "File \"%s\" cannot be opened", fileHandle_->filename());

		WavHeader header;
		fileHandle_->Read(&header, sizeof(WavHeader));

		if (strncmp(header.chunkId, "RIFF", 4) != 0 || strncmp(header.format, "WAVE", 4) != 0) {
			//RETURN_MSG_X("\"%s\" is not a WAV file", fileHandle_->filename());
		}
		if (strncmp(header.subchunk1Id, "fmt ", 4) != 0) {
			//RETURN_MSG_X("\"%s\" is an invalid WAV file", fileHandle_->filename());
		}
		if (IFileStream::int16FromLE(header.audioFormat) != 1) {
			//RETURN_MSG_X("Data in \"%s\" is not in PCM format", fileHandle_->filename());
		}

		bytesPerSample_ = IFileStream::int16FromLE(header.bitsPerSample) / 8;
		numChannels_ = IFileStream::int16FromLE(header.numChannels);
		frequency_ = IFileStream::int32FromLE(header.sampleRate);

		numSamples_ = IFileStream::int32FromLE(header.subchunk2Size) / (numChannels_ * bytesPerSample_);
		duration_ = float(numSamples_) / frequency_;

		//RETURN_ASSERT_MSG_X(numChannels_ == 1 || numChannels_ == 2, "Unsupported number of channels: %d", numChannels_);
		//LOGI_X("duration: %.2fs, channels: %d, frequency: %dHz", duration_, numChannels_, frequency_);

		hasLoaded_ = true;
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	std::unique_ptr<IAudioReader> AudioLoaderWav::createReader()
	{
		return std::make_unique<AudioReaderWav>(std::move(fileHandle_));
	}

}
