#pragma once

#include "../../Main.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Converts the game's Galaxy Music System modules (`.j2b`) into FastTracker II modules (`.xm`)

		The Nintendo 64 plays music through libdragon's XM player, which streams the instruments from the
		cartridge and costs next to no RAM - but it only reads XM. The game's own soundtrack is in the Galaxy
		Music System format (`RIFF AM`, and the older `RIFF AMFF`, zlib-compressed behind a `MUSE` header),
		which is close enough to XM to translate rather than pre-render: its effect set is the MOD/XM one, and
		the shipped modules use neither envelopes nor multi-sample instruments.

		The one thing XM cannot express directly is a channel's default panning. The Galaxy format keeps a pan
		position per channel that a note does not change, while XM resets the panning to the sample's own on
		every note that names an instrument. So the playback order is simulated to know the panning each note
		is heard at, and each instrument is written once per panning it is played at - within a size budget,
		beyond which the nearest variant is used and a panning command is added where the row has room for it.

		The semantics follow libopenmpt's loader (`load_j2b.cpp`), which is what the game plays the originals
		with on every other platform.
	*/
	class ModuleConverter
	{
	public:
		ModuleConverter() = delete;

		/**
			@brief Converts a `.j2b` file into a `.xm` file

			@param sourcePath	The original module
			@param targetPath	The module to write
			@returns `false` if the source cannot be read or is not a Galaxy Music System module
		*/
		static bool ConvertJ2bToXm(StringView sourcePath, StringView targetPath);
	};
}
