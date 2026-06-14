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
#endif

	public:
		/** @{ @name Constants */

		/** @brief Size of the RIFF WAVE header in bytes */
		static const std::uint32_t HeaderSize = sizeof(WavHeader);

		/** @} */
	};
}
