#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Type tag written into the header of each file the game produces

		Kept separate from @ref ContentResolver so the offline converters can identify what they write without
		pulling in the resource cache (and, through it, the renderer). @ref ContentResolver exposes the same
		values under its own names for the rest of the codebase.
	*/
	struct ContentFileType
	{
		static constexpr std::uint8_t Level = 1;
		static constexpr std::uint8_t Episode = 2;
		static constexpr std::uint8_t CacheIndex = 3;
		static constexpr std::uint8_t Config = 4;
		static constexpr std::uint8_t State = 5;
		static constexpr std::uint8_t SfxList = 6;
		static constexpr std::uint8_t Highscores = 7;
		static constexpr std::uint8_t Video = 8;
		static constexpr std::uint8_t Font = 9;
	};
}
