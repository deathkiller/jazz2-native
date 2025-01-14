#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Shield type */
	enum class ShieldType : std::uint8_t
	{
		None,			/**< No shield */

		Fire,			/**< Fire shield */
		Water,			/**< Water shield */
		Lightning,		/**< Lightning shield */
		Laser,			/**< Laser shield */

		Count			/**< Count of supported shield types */
	};
}