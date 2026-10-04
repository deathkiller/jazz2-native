#pragma once

#include "BigEndianWriter.h"

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/** @brief Audio to be written as a `.wav64`, always as 16-bit samples */
	struct Wav64Audio
	{
		/** @brief Interleaved samples, @ref Frames times @ref Channels */
		SmallVector<std::int16_t, 0> Samples;
		std::int32_t Frames = 0;
		std::int32_t Channels = 1;
		/** @brief Sample size of the source data, 8 or 16 - only an uncompressed file keeps 8-bit samples */
		std::int32_t BitsPerSample = 16;
		std::int32_t SampleRate = 0;
		bool Looping = false;
		/** @brief First frame of the loop */
		std::int32_t LoopOffset = 0;
		/** @brief Frame the loop ends at (exclusive), 0 for the end of the audio */
		std::int32_t LoopEnd = 0;
		/** @brief Frames playback can be started from besides the beginning (VADPCM only) */
		SmallVector<std::int32_t, 0> SkipPoints;
		/** @brief Compressed frames kept in RAM for a fast note-on (VADPCM only, used by `.xm64` samples) */
		std::uint8_t AttackFrames = 0;
		/** @brief Whether the whole waveform is preloaded instead of streamed (VADPCM only) */
		bool Resident = false;
	};

	/** @brief Compression of the samples of a `.wav64` */
	enum class Wav64Format
	{
		Raw = 0,
		/** @brief 4-bit VADPCM, which the mixer decodes on the RSP */
		Vadpcm = 1,
		/** @brief ULC, a low-complexity MDCT codec for long recordings */
		Ulc = 2
	};

	/**
		@brief Writes libdragon's `.wav64` audio files

		This is libdragon's `audioconv64` (`conv_wav64.cpp`, version 10 of the format) reimplemented on top of the same
		codecs, so a Nintendo 64 tree can be made without the libdragon toolchain - which is what the web build
		of the tool relies on. The output is meant to match `audioconv64` byte for byte for the options this tool
		uses: VADPCM without the Huffman stage (`--wav-compress vadpcm,huffman=false`), and ULC in its default
		variable-bitrate mode (`--wav-compress ulc`).
	*/
	class Wav64Writer
	{
	public:
		Wav64Writer() = delete;

		/**
			@brief Writes @p audio at the end of @p out

			Offsets inside the file are relative to where it begins, so it can be part of a larger file, as the
			samples of a `.xm64` module are. The audio is adjusted the way the format needs - the loop of a VADPCM
			or ULC file is rotated to begin on a block boundary and the audio is padded to whole blocks - and the
			adjusted lengths are left in @p audio for the caller to pick up.
		*/
		static bool Write(BigEndianWriter& out, Wav64Audio& audio, Wav64Format format, StringView displayName);

		/**
			@brief Reads a RIFF WAVE file as `audioconv64` does

			PCM of 8 to 32 bits and floating-point data are converted to 16-bit samples, the first loop of a `smpl`
			chunk becomes the loop and `cue` points become skip points.
		*/
		static bool ReadWave(Death::IO::Stream& s, Wav64Audio& audio, StringView displayName);
	};
}
