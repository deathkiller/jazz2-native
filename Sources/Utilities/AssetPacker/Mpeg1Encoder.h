#pragma once

#include <cstdint>
#include <vector>

namespace Jazz2::AssetPacker
{
	/**
		@brief Encodes planar YUV 4:2:0 frames into an MPEG-1 video elementary stream (`.m1v`)

		The Nintendo 64 plays full-motion video with libdragon's port of `pl_mpeg`, which reads a bare MPEG-1 video
		stream. This encoder writes one without any external tool, so it runs wherever the asset packer does,
		including its WebAssembly build. Every decision is made in integer arithmetic, so the same input gives the
		same bytes on every platform and compiler.

		The stream is made of closed groups of pictures, one I-picture followed by P-pictures, with the sequence
		header repeated before each group and the default quantizer matrices. B-pictures are not used: the cinematics
		are animated on twos, so every other frame repeats the previous one and costs next to nothing as a P-picture,
		while B-pictures would only stretch the distance the other P-pictures predict over. A group ends early at a
		scene cut, or runs a little longer to end at one that follows closely.

		The rate control encodes about ten seconds ahead and gives every group the quantizer that would put the
		groups written so far and the ones ahead exactly on target, so the quality stays even across the sequence and
		the target bitrate is met over all of it rather than per group. Those frames are held in memory meanwhile.

		A decoder that only keeps up with a limited bitrate, like the console's, can be given it as a peak: no run of
		pictures then takes more than the buffer plus the peak bitrate over its duration (the leaky bucket of MPEG's
		video buffering verifier), and a picture that would is encoded again with a coarser quantizer. The rate control
		counts such a picture as the bits it would have taken, so what it saves goes to the groups around it.

		Two quirks of libdragon's decoder are taken care of: its RSP path halves negative odd motion vectors the other
		way than the standard when it predicts chroma, so such vectors are only used where both roundings predict the
		same pixels, and it cannot decode the last few bytes of a file, so the stream ends with padding.
	*/
	class Mpeg1Encoder
	{
	public:
		/** @brief Encoding parameters */
		struct Options
		{
			/** @brief Luma size in pixels, both even - it is padded internally to whole macroblocks */
			std::int32_t Width = 0;
			std::int32_t Height = 0;
			/** @brief Frame rate as a fraction, one of the eight rates MPEG-1 defines (23.976, 24, 25, 29.97, 30, 50, 59.94, 60) */
			std::int32_t FrameRateNum = 24;
			std::int32_t FrameRateDen = 1;
			/** @brief Target average bitrate in kbit/s over the whole sequence */
			std::int32_t BitrateKbps = 800;
			/** @brief Pictures in a group, `0` for two seconds' worth - at most 132, as MPEG-1 recommends regular intra refreshes */
			std::int32_t GopLength = 0;
			/** @brief Longest motion vector in pixels (1 to 64) */
			std::int32_t SearchRange = 32;
			/** @brief Quantizer (1 to 31) of every P-picture instead of following @ref BitrateKbps, `0` for rate control */
			std::int32_t FixedQuantizer = 0;
			/** @brief Bitrate in kbit/s the decoder keeps up with, `0` for no limit - never below @ref BitrateKbps */
			std::int32_t PeakBitrateKbps = 0;
			/** @brief How many kbit the decoder may fall behind @ref PeakBitrateKbps before it drops a picture, `0` for half a second of it */
			std::int32_t PeakBufferKbits = 0;
		};

		/** @brief Statistics of the stream written so far */
		struct Statistics
		{
			std::int32_t Frames = 0;
			/** @brief Pictures coded as I-pictures, which is also the number of groups of pictures */
			std::int32_t IntraFrames = 0;
			std::int64_t Bytes = 0;
			/** @brief Sum of the slice quantizers of all pictures, divide by `Frames` and the number of macroblock rows for an average */
			std::int64_t QuantizerSum = 0;
			/** @brief Squared error of the decoded luma against the input, summed over all frames */
			std::uint64_t LumaSquaredError = 0;
		};

		Mpeg1Encoder();
		~Mpeg1Encoder();

		Mpeg1Encoder(const Mpeg1Encoder&) = delete;
		Mpeg1Encoder& operator=(const Mpeg1Encoder&) = delete;

		/**
			@brief Starts a new stream

			@param output	Receives the bytes of the stream as they become available
			@returns `false` if the options cannot be encoded - an odd or too large size, or a frame rate MPEG-1 lacks
		*/
		bool Begin(const Options& options, std::vector<std::uint8_t>& output);

		/**
			@brief Adds one frame

			The planes are limited-range BT.601 (Y 16-235, chroma 16-240), tightly packed: @p y is `Width` by
			`Height`, @p u (Cb) and @p v (Cr) are half of that in both directions. Frames are held for the rate
			control's lookahead, so @p output only grows now and then, by a group of pictures at a time.
		*/
		bool EncodeFrame(const std::uint8_t* y, const std::uint8_t* u, const std::uint8_t* v, std::vector<std::uint8_t>& output);

		/** @brief Encodes the frames still held and finishes the stream with a `sequence_end_code` */
		bool End(std::vector<std::uint8_t>& output);

		/** @brief Returns statistics of the stream written so far */
		const Statistics& GetStatistics() const;

	private:
		struct State;

		State* _state;
	};
}
