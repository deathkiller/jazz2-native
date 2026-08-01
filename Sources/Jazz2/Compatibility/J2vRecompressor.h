#pragma once

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	/**
		@brief Rewrites a `.j2v` cinematic so weaker platforms can play it

		The original videos are 640x480, and the decoder has to inflate and then downscale every frame at
		playback time. On the Dreamcast that does not fit in the frame budget, so the video is decoded here,
		downscaled once, and re-encoded into the very same container - which means no new format and no
		decoder changes, only a quarter of the pixels to inflate and copy per frame.

		The container holds four interleaved zlib streams: opcodes, the offset and row parts of
		copy-from-previous-frame runs, and the literal pixels (plus palettes). The re-encoder emits the same
		opcodes, so the result plays anywhere the original does.
	*/
	class J2vRecompressor
	{
	public:
		/** @brief Decodes @p sourcePath, downscales it by @p downscale and writes it to @p targetPath */
		static bool Recompress(StringView sourcePath, StringView targetPath, std::int32_t downscale);
	};
}
