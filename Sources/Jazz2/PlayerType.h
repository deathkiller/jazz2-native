#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Player type */
	enum class PlayerType : std::uint8_t
	{
		None,		/**< None/unspecified */

		Jazz,		/**< Jazz */
		Spaz,		/**< Spaz */
		Lori,		/**< Lori */
		Frog		/**< Frog */
	};
}