#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Level flags
		
		Per-level boolean properties stored as single-bit flags --- whether the level has a pit (and whether that pit
		kills instantly), uses a custom palette, is hidden, is a multiplayer level, has laps or capture-the-flag,
		should use a vertical splitscreen, or defines multiplayer spawn points. Read from the level file and used by
		the level handler to configure the session. Supports a bitwise combination of its member values.
	*/
	enum class LevelFlags : std::uint16_t
	{
		None = 0,								/**< None */
		HasPit = 0x01,							/**< Level has a pit */
		HasPitInstantDeath = 0x02,				/**< Level has a pit with instant death */
		UseLevelPalette = 0x04,					/**< Level has custom palette */
		IsHidden = 0x08,						/**< Level is hidden */
		IsMultiplayerLevel = 0x10,				/**< Level is for multiplayer */
		HasLaps = 0x20,							/**< Level has laps */
		HasCaptureTheFlag = 0x40,				/**< Level has capture the flag */
		HasVerticalSplitscreen = 0x80,			/**< Vertical splitscreen should be used */
		HasMultiplayerSpawnPoints = 0x100		/**< Level has multiplayer spawn points */
	};

	DEATH_ENUM_FLAGS(LevelFlags);
}