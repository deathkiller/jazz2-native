#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Game difficulty
		
		Selected difficulty of a play session --- easy, normal or hard --- with @ref GameDifficulty::Default standing
		for unspecified. It governs which events are spawned in a level (each event carries the difficulties it applies
		to) and is carried in @ref LevelInitialization for the duration of the session.
	*/
	enum class GameDifficulty : std::uint8_t
	{
		Default,		/**< Default/unspecified */

		Easy,			/**< Easy */
		Normal,			/**< Normal */
		Hard			/**< Hard */
	};
}