#pragma once

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
	class J2vRecompressor
	{
	public:
		/** @brief Decodes @p sourcePath, downscales it by @p downscale and writes it to @p targetPath */
		static bool Recompress(StringView sourcePath, StringView targetPath, std::int32_t downscale);
	};
}
