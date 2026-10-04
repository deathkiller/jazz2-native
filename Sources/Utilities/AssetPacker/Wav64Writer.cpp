#include "Wav64Writer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#include <Containers/Array.h>
#include <Core/Logger.h>

extern "C" {
#include "../../Dependencies/vadpcm/codec/vadpcm.h"
#include "../../Dependencies/ulc/ulcEncoder.h"
}

using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Version of the format, the one libdragon's player reads */
		constexpr std::uint8_t FileVersion = 10;

		constexpr std::int32_t VadpcmPredictors = 4;
		constexpr std::int32_t VadpcmBits = 4;
		/** @brief Loops and lengths are aligned to this many frames, the RSP decoder works on pairs of VADPCM frames */
		constexpr std::int32_t VadpcmAlignment = 2 * kVADPCMFrameSampleCount;
		/** @brief Stereo frames are stored in blocks of this many per channel, so that one fill of the mixer's buffer
			reads one block of the file */
		constexpr std::int32_t VadpcmPlanarBlock = 128;
		/** @brief Size of the Huffman contexts, which the header carries (zeroed) even without the Huffman stage */
		constexpr std::int32_t HuffmanContextSize = 3 * 24;

		constexpr std::int32_t UlcBlockSize = 1024;
		/** @brief Quality of `audioconv64`'s default variable-bitrate mode */
		constexpr float UlcQuality = 50.0f;
		/** @brief Blocks between two entries of the seek table */
		constexpr std::int32_t UlcSeekInterval = 8;

		/**
			@brief Inserts @p count frames of the beginning of the loop at its end, moving the loop by as much

			The loop then begins where the format needs it to, and it still plays the same. The copy runs a little
			further, over the first frames of the release tail - those are out of phase with the loop now, and a
			compressed frame straddling the end of the loop would have to encode that jump with the last frames of the
			loop, on one scale factor. Rewriting up to the frame boundary avoids that.
		*/
		void RotateLoopAtEnd(Wav64Audio& audio, std::int32_t count)
		{
			const std::int32_t channels = audio.Channels;
			const std::int32_t loopEnd = (audio.LoopEnd != 0 ? audio.LoopEnd : audio.Frames);
			const std::int32_t loopLength = loopEnd - audio.LoopOffset;
			const std::int32_t tail = audio.Frames - loopEnd;
			if (count <= 0 || loopLength <= 0 || tail < 0) {
				return;
			}

			const std::int32_t end = loopEnd + count;
			std::int32_t over = (end + 3 + kVADPCMFrameSampleCount - 1) / kVADPCMFrameSampleCount * kVADPCMFrameSampleCount - end;
			if (over > tail) {
				over = tail;
			}

			audio.Samples.resize(std::size_t(audio.Frames + count) * channels);
			std::memmove(&audio.Samples[std::size_t(end) * channels], &audio.Samples[std::size_t(loopEnd) * channels],
				std::size_t(tail) * channels * sizeof(std::int16_t));
			for (std::int32_t i = 0; i < count + over; i++) {
				const std::int32_t source = audio.LoopOffset + (i % loopLength);
				for (std::int32_t c = 0; c < channels; c++) {
					audio.Samples[std::size_t(loopEnd + i) * channels + c] = audio.Samples[std::size_t(source) * channels + c];
				}
			}
			audio.Frames += count;
			audio.LoopOffset += count;
			audio.LoopEnd = end;
		}

		/** @brief Index of a frame in the block-planar layout of a VADPCM stream */
		std::int32_t GetPlanarFrameIndex(std::int32_t frame, std::int32_t channel, std::int32_t channels, std::int32_t frameCount)
		{
			if (channels == 1) {
				return frame;
			}
			const std::int32_t block = frame / VadpcmPlanarBlock;
			const std::int32_t offset = frame % VadpcmPlanarBlock;
			const std::int32_t blockCount = (frameCount + VadpcmPlanarBlock - 1) / VadpcmPlanarBlock;
			const std::int32_t blockSize = (block == blockCount - 1 ? frameCount - block * VadpcmPlanarBlock : VadpcmPlanarBlock);
			return block * VadpcmPlanarBlock * channels + channel * blockSize + offset;
		}

		bool WriteVadpcm(BigEndianWriter& out, std::size_t basePosition, std::size_t samplesField, std::size_t stateSizeField,
			Wav64Audio& audio, std::int32_t loopLength, StringView displayName)
		{
			const std::int32_t channels = audio.Channels;

			// The state is 16+4+4 bytes per channel, but the player always allocates both channels
			out.Patch32(stateSizeField, 48);

			// The frame that holds the last samples is completed with what really plays next - the beginning of the
			// loop, or the last sample held - because a frame has a single scale factor, and silence would spend the
			// whole residual range on the jump to zero and flatten the samples next to it
			if (audio.Frames % VadpcmAlignment != 0) {
				const std::int32_t paddedFrames = (audio.Frames + VadpcmAlignment - 1) / VadpcmAlignment * VadpcmAlignment;
				audio.Samples.resize(std::size_t(paddedFrames) * channels);
				for (std::int32_t i = audio.Frames; i < paddedFrames; i++) {
					const std::int32_t source = (loopLength > 0 ? audio.LoopOffset + (i - audio.Frames) % loopLength : audio.Frames - 1);
					for (std::int32_t c = 0; c < channels; c++) {
						audio.Samples[std::size_t(i) * channels + c] = audio.Samples[std::size_t(source) * channels + c];
					}
				}
				audio.Frames = paddedFrames;
			}

			const std::int32_t frameCount = audio.Frames / kVADPCMFrameSampleCount;
			constexpr std::int32_t VectorsPerChannel = VadpcmPredictors * kVADPCMEncodeOrder;
			std::unique_ptr<vadpcm_vector[]> codebook = std::make_unique<vadpcm_vector[]>(std::size_t(VectorsPerChannel) * channels);

			vadpcm_params params = {};
			params.predictor_count = VadpcmPredictors;
			params.dither = kVADPCMDitherRectangular;
			params.min_residual = -(1 << (VadpcmBits - 1));
			params.max_residual = (1 << (VadpcmBits - 1)) - 1;

			// Playback can only start at the beginning of a frame, so every skip point moves to the next one
			SmallVector<std::int32_t, 0> skipPoints;
			skipPoints.append(audio.SkipPoints.begin(), audio.SkipPoints.end());
			if (audio.Looping) {
				skipPoints.push_back(audio.LoopOffset);
			}
			std::sort(skipPoints.begin(), skipPoints.end());
			for (std::int32_t& point : skipPoints) {
				point = (point + kVADPCMFrameSampleCount - 1) / kVADPCMFrameSampleCount * kVADPCMFrameSampleCount;
				if (point < 0 || point >= audio.Frames) {
					LOGE("\"{}\" has an invalid skip point: {}", displayName, point);
					return false;
				}
			}

			const std::int32_t skipCount = std::int32_t(skipPoints.size());
			std::unique_ptr<vadpcm_vector[]> skipStates = std::make_unique<vadpcm_vector[]>(std::size_t(skipCount) * channels + 1);
			// The first three samples decoded at the beginning of the loop, kept after the codebook of each channel
			// so the mixer can interpolate across the loop point
			std::array<std::array<std::int16_t, 3>, 2> loopHead = {};
			const std::int32_t loopStartAligned = (audio.Looping
				? (audio.LoopOffset + kVADPCMFrameSampleCount - 1) / kVADPCMFrameSampleCount * kVADPCMFrameSampleCount
				: -1);

			Array<std::uint8_t> encoded{ValueInit, std::size_t(frameCount) * kVADPCMFrameByteSize * channels};
			Array<std::uint8_t> channelData{ValueInit, std::size_t(frameCount) * kVADPCMFrameByteSize};
			SmallVector<std::int16_t, 0> channelSamples;
			channelSamples.resize(std::size_t(audio.Frames));
			for (std::int32_t i = 0; i < channels; i++) {
				for (std::int32_t j = 0; j < audio.Frames; j++) {
					channelSamples[j] = audio.Samples[std::size_t(j) * channels + i];
				}
				vadpcm_vector* channelCodebook = codebook.get() + std::size_t(VectorsPerChannel) * i;
				vadpcm_error error = vadpcm_encode(&params, channelCodebook, std::size_t(frameCount), channelData.data(), channelSamples.data(), nullptr);
				if (error != kVADPCMErrNone) {
					LOGE("Cannot encode \"{}\" as VADPCM: {}", displayName, vadpcm_error_name(error));
					return false;
				}

				if (skipCount > 0) {
					// The decoder state at the beginning of each skip point's frame, all in one forward pass
					vadpcm_vector state = {};
					std::int32_t currentFrame = 0;
					for (std::int32_t j = 0; j < skipCount; j++) {
						const std::int32_t targetFrame = skipPoints[j] / kVADPCMFrameSampleCount;
						const std::int32_t runFrames = targetFrame - currentFrame;
						if (runFrames > 0) {
							vadpcm_decode(VadpcmPredictors, kVADPCMEncodeOrder, channelCodebook, &state, std::size_t(runFrames),
								channelSamples.data(), channelData.data() + std::size_t(currentFrame) * kVADPCMFrameByteSize);
						}
						skipStates[std::size_t(j) * channels + i] = state;

						if (skipPoints[j] == loopStartAligned && targetFrame < frameCount) {
							vadpcm_vector headState = state;
							std::int16_t head[kVADPCMFrameSampleCount];
							vadpcm_decode(VadpcmPredictors, kVADPCMEncodeOrder, channelCodebook, &headState, 1, head,
								channelData.data() + std::size_t(targetFrame) * kVADPCMFrameByteSize);
							loopHead[i][0] = head[0];
							loopHead[i][1] = head[1];
							loopHead[i][2] = head[2];
						}
						currentFrame = targetFrame;
					}
				}

				for (std::int32_t j = 0; j < frameCount; j++) {
					const std::int32_t index = GetPlanarFrameIndex(j, i, channels, frameCount);
					std::memcpy(&encoded[std::size_t(index) * kVADPCMFrameByteSize], &channelData[std::size_t(j) * kVADPCMFrameByteSize], kVADPCMFrameByteSize);
				}
			}

			std::uint8_t flags = 0;		// Bit 0 would be the Huffman stage
			if (audio.Resident) {
				flags |= 0x02;
			}

			// Per channel: the predictor vectors, the three samples of the loop and padding
			constexpr std::int32_t CodebookStride = VectorsPerChannel * 16 + 8;
			const std::int32_t codebookBytes = CodebookStride * channels;
			out.Write8(VadpcmPredictors);
			out.Write8(kVADPCMEncodeOrder);
			out.Write16(flags);
			out.Write16(std::uint16_t(skipCount));
			out.Write8(VadpcmBits);
			out.Write8(audio.AttackFrames);
			out.Write32(0);		// Huffman table pointer
			out.Write32(skipCount > 0 ? std::uint32_t(codebookBytes) : 0);
			out.Write32(skipCount > 0 ? std::uint32_t(codebookBytes + skipCount * 8) : 0);
			out.WriteZeros(HuffmanContextSize);
			out.Write32(0);		// Padding
			for (std::int32_t c = 0; c < channels; c++) {
				const vadpcm_vector* channelCodebook = codebook.get() + std::size_t(VectorsPerChannel) * c;
				for (std::int32_t i = 0; i < VectorsPerChannel; i++) {
					for (std::int32_t j = 0; j < kVADPCMVectorSampleCount; j++) {
						out.Write16(std::uint16_t(channelCodebook[i].v[j]));
					}
				}
				for (std::int32_t j = 0; j < 3; j++) {
					out.Write16(std::uint16_t(loopHead[c][j]));
				}
				out.Write16(0);
			}
			for (std::int32_t i = 0; i < skipCount; i++) {
				out.Write32(std::uint32_t(skipPoints[i]));
				out.Write32(0);		// Bit position in the Huffman stream
			}
			for (std::int32_t i = 0; i < skipCount; i++) {
				for (std::int32_t c = 0; c < channels; c++) {
					for (std::int32_t j = 0; j < kVADPCMVectorSampleCount; j++) {
						out.Write16(std::uint16_t(skipStates[std::size_t(i) * channels + c].v[j]));
					}
				}
			}

			out.Patch32(samplesField, std::uint32_t(out.GetPosition() - basePosition));
			out.Write(encoded.data(), encoded.size());
			return true;
		}

		bool WriteUlc(BigEndianWriter& out, std::size_t basePosition, std::size_t samplesField, std::size_t stateSizeField,
			const Wav64Audio& audio, StringView displayName)
		{
			const std::int32_t channels = audio.Channels;
			const std::int32_t blockCount = (audio.Frames + UlcBlockSize - 1) / UlcBlockSize + 2;

			ULC_EncoderState_t encoder = {};
			encoder.RateHz = audio.SampleRate;
			encoder.nChan = channels;
			encoder.BlockSize = UlcBlockSize;
			if (ULC_EncoderState_Init(&encoder) <= 0) {
				LOGE("Cannot initialize the ULC encoder for \"{}\"", displayName);
				return false;
			}

			out.Write16(UlcBlockSize);
			const std::size_t maxBlockSizeField = out.Reserve16();
			out.Write32(std::uint32_t(blockCount));
			const std::size_t bitrateField = out.Reserve32();
			const std::size_t seekTableField = out.Reserve32();

			const std::size_t samplesStart = out.GetPosition();
			out.Patch32(samplesField, std::uint32_t(samplesStart - basePosition));

			// The decoder's own state, alignment slack, two temporary blocks per channel and the overlap of each channel
			const std::int32_t decoderStateSize = 24 + 63 + std::int32_t(sizeof(std::int16_t)) * 2 * channels * UlcBlockSize +
				std::int32_t(sizeof(std::int16_t)) * channels * (UlcBlockSize / 2);
			out.Patch32(stateSizeField, std::uint32_t(decoderStateSize));

			SmallVector<std::uint32_t, 0> seekOffsets;
			SmallVector<float, 0> block;
			block.resize(std::size_t(UlcBlockSize) * channels);
			std::uint64_t totalBytes = 0;
			std::int32_t maxBlockSize = 0;
			for (std::int32_t i = 0; i < blockCount; i++) {
				if (i % UlcSeekInterval == 0) {
					seekOffsets.push_back(std::uint32_t(out.GetPosition() - samplesStart));
				}

				std::fill(block.begin(), block.end(), 0.0f);
				const std::int32_t first = i * UlcBlockSize;
				for (std::int32_t c = 0; c < channels; c++) {
					for (std::int32_t k = 0; k < UlcBlockSize && first + k < audio.Frames; k++) {
						block[std::size_t(k) * channels + c] = audio.Samples[std::size_t(first + k) * channels + c] / 32768.0f;
					}
				}

				std::int32_t sizeBits = 0;
				const void* data = ULC_EncodeBlock_VBR(&encoder, block.data(), &sizeBits, UlcQuality);
				const std::int32_t sizeBytes = (sizeBits + 7) / 8;
				out.Write(data, std::size_t(sizeBytes));
				totalBytes += std::uint64_t(sizeBytes);
				maxBlockSize = std::max(maxBlockSize, sizeBytes);
			}

			out.Patch32(seekTableField, std::uint32_t(out.GetPosition() - samplesStart));
			for (std::uint32_t offset : seekOffsets) {
				out.Write32(offset);
			}

			const std::int32_t bitrate = std::int32_t(std::llround(double(totalBytes) * 8.0 * audio.SampleRate / (double(blockCount) * UlcBlockSize)));
			out.Patch16(maxBlockSizeField, std::uint16_t(maxBlockSize));
			out.Patch32(bitrateField, std::uint32_t(bitrate));

			ULC_EncoderState_Destroy(&encoder);
			return true;
		}

		std::uint16_t ReadU16LE(const std::uint8_t* p)
		{
			return std::uint16_t(p[0] | (p[1] << 8));
		}

		std::uint32_t ReadU32LE(const std::uint8_t* p)
		{
			return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
		}

		/** @brief Converts one sample to 16 bits the way dr_wav does, which `audioconv64` reads its input with */
		std::int16_t ConvertSampleToS16(const std::uint8_t* p, std::int32_t bits, bool isFloat)
		{
			if (isFloat) {
				float value;
				if (bits == 64) {
					double d;
					std::memcpy(&d, p, sizeof(d));
					value = float(d);
				} else {
					std::memcpy(&value, p, sizeof(value));
				}
				value = std::clamp(value, -1.0f, 1.0f) + 1.0f;
				return std::int16_t(std::int32_t(value * 32767.5f) - 32768);
			}
			switch (bits) {
				case 8: return std::int16_t((std::int32_t(p[0]) << 8) - 32768);
				case 16: return std::int16_t(ReadU16LE(p));
				case 24: return std::int16_t(ReadU16LE(p + 1));
				default: return std::int16_t(ReadU16LE(p + 2));
			}
		}
	}

	bool Wav64Writer::Write(BigEndianWriter& out, Wav64Audio& audio, Wav64Format format, StringView displayName)
	{
		const std::size_t basePosition = out.GetPosition();
		const std::int32_t channels = audio.Channels;
		if (channels < 1 || channels > 2) {
			LOGE("\"{}\" has {} channels, only mono and stereo can be played", displayName, channels);
			return false;
		}

		// A loop without an end is one that runs to the end of the audio
		if (audio.Looping && audio.LoopEnd == 0) {
			audio.LoopEnd = audio.Frames;
		}
		std::int32_t loopEnd = (audio.Looping ? audio.LoopEnd : 0);
		std::int32_t loopLength = (audio.Looping ? loopEnd - audio.LoopOffset : 0);
		if (loopLength < 0 || loopEnd > audio.Frames) {
			LOGW("\"{}\" has an invalid loop {}..{} (size: {})", displayName, audio.LoopOffset, loopEnd, audio.Frames);
			loopLength = 0;
			loopEnd = 0;
			audio.Looping = false;
			audio.LoopEnd = 0;
		}

		switch (format) {
			case Wav64Format::Raw: {
				if ((loopLength & 1) != 0 && audio.BitsPerSample == 8) {
					// The player copies a loop to RAM in 16-bit units, which an odd length of 8-bit samples would
					// throw out of phase - one sample less is not audible
					audio.LoopOffset++;
					loopLength--;
				}
				break;
			}
			case Wav64Format::Vadpcm: {
				if (audio.Looping && (audio.LoopOffset % VadpcmAlignment) != 0) {
					RotateLoopAtEnd(audio, VadpcmAlignment - (audio.LoopOffset % VadpcmAlignment));
				}
				audio.BitsPerSample = 16;
				break;
			}
			case Wav64Format::Ulc: {
				audio.BitsPerSample = 16;
				// A whole-file loop appends decoded blocks to RAM, so the length is kept on an 8-byte boundary
				const std::int32_t frameBytes = channels * std::int32_t(sizeof(std::int16_t));
				std::int32_t frameAlignment = 1;
				while (((frameAlignment * frameBytes) & 7) != 0) {
					frameAlignment++;
				}
				if (audio.Looping && (audio.LoopOffset % UlcBlockSize) != 0) {
					RotateLoopAtEnd(audio, UlcBlockSize - (audio.LoopOffset % UlcBlockSize));
				}
				audio.Frames -= audio.Frames % frameAlignment;
				audio.Samples.resize(std::size_t(audio.Frames) * channels);
				if (audio.Looping && audio.LoopEnd > audio.Frames) {
					audio.LoopEnd = audio.Frames;
				}
				break;
			}
		}

		// The file stores the loop length, and its end only when something follows it
		loopEnd = (audio.Looping ? (audio.LoopEnd != 0 ? audio.LoopEnd : audio.Frames) : 0);
		loopLength = (audio.Looping ? loopEnd - audio.LoopOffset : 0);
		const std::int32_t loopEndOnDisk = (audio.Looping && loopEnd < audio.Frames ? loopEnd : 0);

		out.Write("WV64", 4);
		out.Write8(FileVersion);
		out.Write8(std::uint8_t(format));
		out.Write8(std::uint8_t(channels));
		out.Write8(std::uint8_t(audio.BitsPerSample));
		out.Write32(std::uint32_t(audio.SampleRate));
		out.Write32(std::uint32_t(audio.Frames));
		out.Write32(std::uint32_t(loopLength));
		out.Write32(std::uint32_t(loopEndOnDisk));
		const std::size_t samplesField = out.Reserve32();
		const std::size_t stateSizeField = out.Reserve32();

		switch (format) {
			case Wav64Format::Raw: {
				out.Patch32(stateSizeField, 0);
				out.Patch32(samplesField, std::uint32_t(out.GetPosition() - basePosition));
				const std::size_t count = std::size_t(audio.Frames) * channels;
				for (std::size_t i = 0; i < count; i++) {
					// An 8-bit sample is the high byte of the 16-bit one, both are signed
					const std::uint16_t value = std::uint16_t(audio.Samples[i]);
					out.Write8(std::uint8_t(value >> 8));
					if (audio.BitsPerSample != 8) {
						out.Write8(std::uint8_t(value));
					}
				}
				return true;
			}
			case Wav64Format::Vadpcm:
				return WriteVadpcm(out, basePosition, samplesField, stateSizeField, audio, loopLength, displayName);
			case Wav64Format::Ulc:
				return WriteUlc(out, basePosition, samplesField, stateSizeField, audio, displayName);
		}
		return false;
	}

	bool Wav64Writer::ReadWave(Stream& s, Wav64Audio& audio, StringView displayName)
	{
		const std::int64_t size = s.GetSize();
		if (size < 12) {
			LOGE("\"{}\" is not a valid WAV file", displayName);
			return false;
		}
		Array<std::uint8_t> file{NoInit, std::size_t(size)};
		if (s.Read(file.data(), size) != size || std::memcmp(&file[0], "RIFF", 4) != 0 || std::memcmp(&file[8], "WAVE", 4) != 0) {
			LOGE("\"{}\" is not a valid WAV file", displayName);
			return false;
		}

		std::int32_t formatTag = 0, channels = 0, bits = 0, sampleRate = 0, blockAlign = 0;
		const std::uint8_t* data = nullptr;
		std::size_t dataSize = 0;
		const std::uint8_t* sampler = nullptr;
		std::size_t samplerSize = 0;
		const std::uint8_t* cue = nullptr;
		std::size_t cueSize = 0;

		std::size_t position = 12;
		while (position + 8 <= file.size()) {
			const std::uint8_t* chunk = &file[position];
			const std::size_t length = std::min<std::size_t>(ReadU32LE(chunk + 4), file.size() - position - 8);
			const std::uint8_t* body = chunk + 8;
			if (std::memcmp(chunk, "fmt ", 4) == 0 && length >= 16) {
				formatTag = ReadU16LE(body);
				channels = ReadU16LE(body + 2);
				sampleRate = std::int32_t(ReadU32LE(body + 4));
				blockAlign = ReadU16LE(body + 12);
				bits = ReadU16LE(body + 14);
				if (formatTag == 0xFFFE && length >= 26) {
					// WAVE_FORMAT_EXTENSIBLE names the real format in the first two bytes of its sub-format GUID
					formatTag = ReadU16LE(body + 24);
				}
			} else if (std::memcmp(chunk, "data", 4) == 0) {
				data = body;
				dataSize = length;
			} else if (std::memcmp(chunk, "smpl", 4) == 0) {
				sampler = body;
				samplerSize = length;
			} else if (std::memcmp(chunk, "cue ", 4) == 0) {
				cue = body;
				cueSize = length;
			}
			position += 8 + length + (length & 1);
		}

		const bool isFloat = (formatTag == 3);
		if ((formatTag != 1 && !isFloat) || data == nullptr || channels <= 0 || sampleRate <= 0 ||
			(isFloat ? (bits != 32 && bits != 64) : (bits != 8 && bits != 16 && bits != 24 && bits != 32))) {
			LOGE("\"{}\" is not a supported WAV file (format {}, {} bits)", displayName, formatTag, bits);
			return false;
		}

		const std::int32_t bytesPerSample = bits / 8;
		const std::int32_t frameBytes = std::max(blockAlign, bytesPerSample * channels);
		audio.Channels = channels;
		audio.BitsPerSample = bits;
		audio.SampleRate = sampleRate;
		audio.Frames = std::int32_t(dataSize / std::size_t(frameBytes));
		audio.Samples.resize(std::size_t(audio.Frames) * channels);
		for (std::int32_t i = 0; i < audio.Frames; i++) {
			for (std::int32_t c = 0; c < channels; c++) {
				audio.Samples[std::size_t(i) * channels + c] = ConvertSampleToS16(data + std::size_t(i) * frameBytes + std::size_t(c) * bytesPerSample, bits, isFloat);
			}
		}

		if (sampler != nullptr && samplerSize >= 36 && ReadU32LE(sampler + 28) > 0 && samplerSize >= 36 + 24) {
			// Only the first loop is used, a loop past its end keeps the samples after it as a release tail
			const std::uint8_t* loop = sampler + 36;
			const std::uint32_t type = ReadU32LE(loop + 4);
			const std::int32_t first = std::int32_t(ReadU32LE(loop + 8));
			const std::int32_t last = std::int32_t(ReadU32LE(loop + 12));
			audio.Looping = true;
			audio.LoopOffset = first;
			audio.LoopEnd = std::min(last + 1, audio.Frames);
			if (type == 1) {
				// A ping-pong loop is unrolled into a forward one, dropping whatever followed the loop
				const std::int32_t loopLength = last - first + 1;
				const std::int32_t keep = audio.LoopEnd;
				SmallVector<std::int16_t, 0> unrolled;
				unrolled.resize(std::size_t(keep + loopLength) * channels);
				std::memcpy(unrolled.data(), audio.Samples.data(), std::size_t(keep) * channels * sizeof(std::int16_t));
				for (std::int32_t i = 0; i < loopLength; i++) {
					for (std::int32_t c = 0; c < channels; c++) {
						unrolled[std::size_t(keep + i) * channels + c] = audio.Samples[std::size_t(last - i) * channels + c];
					}
				}
				audio.Samples = std::move(unrolled);
				audio.Frames = keep + loopLength;
				audio.LoopOffset = keep;
				audio.LoopEnd = audio.Frames;
			} else if (type != 0) {
				LOGW("\"{}\" has a loop of type {}, which is not supported", displayName, type);
			}
		}

		if (cue != nullptr && cueSize >= 4) {
			// Cue points are where playback can be resumed from, which the format calls skip points
			const std::uint32_t count = ReadU32LE(cue);
			for (std::uint32_t i = 0; i < count && 4 + (i + 1) * 24 <= cueSize; i++) {
				const std::int32_t offset = std::int32_t(ReadU32LE(cue + 4 + i * 24 + 20));
				if (offset > 0) {
					audio.SkipPoints.push_back(offset);
				}
			}
		}
		return true;
	}
}
