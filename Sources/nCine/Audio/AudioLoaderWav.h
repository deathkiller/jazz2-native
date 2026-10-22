#pragma once

#include "IAudioLoader.h"

namespace nCine
{
	/**
		@brief Audio loader for the WAVE (`.wav`) format
	*/
	class AudioLoaderWav : public IAudioLoader
	{
	public:
		explicit AudioLoaderWav(std::unique_ptr<Death::IO::Stream> fileHandle);

		AudioLoaderWav(const AudioLoaderWav&) = delete;
		AudioLoaderWav& operator=(const AudioLoaderWav&) = delete;

		std::unique_ptr<IAudioReader> createReader() override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		// Header of a RIFF WAVE file
		struct RiffHeader
		{
			char chunkId[4];
			std::uint32_t chunkSize;
			char format[4];
		};

		// Header of a chunk inside the RIFF WAVE file
		struct ChunkHeader
		{
			char id[4];
			std::uint32_t size;
		};

		// Contents of the "fmt " chunk, the chunk can be longer
		struct FormatChunk
		{
			std::uint16_t audioFormat;
			std::uint16_t numChannels;
			std::uint32_t sampleRate;
			std::uint32_t byteRate;
			std::uint16_t blockAlign;
			std::uint16_t bitsPerSample;
		};
#endif

		std::int64_t _dataOffset;
		std::int64_t _dataSize;
	};
}
