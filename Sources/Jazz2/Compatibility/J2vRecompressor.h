#pragma once

#include <Containers/Function.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	/**
		@brief Rewrites a `.j2v` cinematic so weaker platforms can play it

		The original videos are 640x480 in four interleaved zlib streams (opcodes, the offset and row parts
		of copy-from-previous-frame runs, and the literal pixels plus palettes), and the decoder has to
		inflate and then downscale every frame at playback time. On the Dreamcast that does not fit in the
		frame budget, so the video is decoded here, downscaled once, and re-encoded into the game's own
		@ref VideoFormat container, whose skip/literal/run commands decode with plain `memcpy`/`memset`
		and no inflation at all. The player detects the format by its signature and accepts both.
	*/
	/** @brief Properties of an original `.j2v` cinematic */
	struct J2vVideoInfo
	{
		std::int32_t Width = 0;
		std::int32_t Height = 0;
		std::int32_t FrameCount = 0;
		/** @brief Duration of one frame in milliseconds */
		std::uint16_t FrameDelay = 0;
		std::int64_t FileSize = 0;
	};

	class J2vRecompressor
	{
	public:
		/** @brief Decodes @p sourcePath, downscales it by @p downscale and writes it to @p targetPath */
		static bool Recompress(StringView sourcePath, StringView targetPath, std::int32_t downscale);

		/**
			@brief Decodes every frame of an original cinematic

			@p onFrame receives each frame as palette indices at the video's full resolution together with the
			palette in effect (256 entries of R, G, B, X), whether the palette changed with this frame (always
			for the first one), and returns `false` to stop. The frame buffer is reused for the next frame.
		*/
		static bool DecodeFrames(StringView sourcePath, Function<bool(const J2vVideoInfo& info, std::int32_t frameIndex,
			const std::uint8_t* indices, const std::uint8_t* palette, bool paletteChanged)>&& onFrame, J2vVideoInfo* info = nullptr);
	};
}
