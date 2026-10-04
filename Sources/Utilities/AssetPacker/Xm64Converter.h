#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Converts FastTracker II modules (`.xm`) into libdragon's `.xm64`

		This is libdragon's `audioconv64` (`conv_xm64.cpp`, version 13 of the format) reimplemented, so a Nintendo 64
		tree can be made without the libdragon toolchain. Like the original, it loads the module into libxm (the
		player libdragon's is derived from), plays it through once to work out how much of each instrument every
		channel has to keep buffered while it streams from the cartridge, writes the samples as VADPCM `.wav64`
		data (see @ref Wav64Writer) and saves the module's structure with its patterns compressed in libdragon's
		asset format.

		The only intended difference is the compression of the patterns and of the structure: libdragon compresses
		them with LZ4 HC, this uses an LZ4 compressor of its own (the result decodes the same, and keeps to the
		window the player streams the structure through). Everything else - the samples, the buffer sizes and the
		layout - matches `audioconv64` with its default options. The context sizes stored in the file are worked
		out as a 64-bit build of `audioconv64` does on any host, which is what the console's player needs to find.
	*/
	class Xm64Converter
	{
	public:
		Xm64Converter() = delete;

		/**
			@brief Converts a `.xm` file into a `.xm64` file

			@returns `false` if the module cannot be read, or the output cannot be written
		*/
		static bool Convert(StringView sourcePath, StringView targetPath);
	};
}
