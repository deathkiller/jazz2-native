#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Game difficulty */
	enum class GameDifficulty : std::uint8_t
	{
		Default,		/**< Default/unspecified */

		Easy,			/**< Easy */
		Normal,			/**< Normal */
		Hard			/**< Hard */
	};
}