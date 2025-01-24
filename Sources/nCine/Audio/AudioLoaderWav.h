#pragma once

#include "IAudioLoader.h"

namespace nCine
{
	/// WAVE audio loader
	class AudioLoaderWav : public IAudioLoader
	{
	public:
		explicit AudioLoaderWav(std::unique_ptr<Death::IO::Stream> fileHandle);

		AudioLoaderWav(const AudioLoaderWav&) = delete;
		AudioLoaderWav& operator=(const AudioLoaderWav&) = delete;

		std::unique_ptr<IAudioReader> createReader() override;

	private:
		/// Header for the RIFF WAVE format
		struct WavHeader
		{
			char chunkId[4];
			std::uint32_t chunkSize;
			char format[4];

			char subchunk1Id[4];
			std::uint32_t subchunk1Size;
			std::uint16_t audioFormat;
			std::uint16_t numChannels;
			std::uint32_t sampleRate;
			std::uint32_t byteRate;
			std::uint16_t blockAlign;
			std::uint16_t bitsPerSample;

			char subchunk2Id[4];
			std::uint32_t subchunk2Size;
		};

	public:
		static const std::uint32_t HeaderSize = sizeof(WavHeader);
	};
}
