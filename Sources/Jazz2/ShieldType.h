#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Shield type
		
		Kind of temporary protective shield a player can carry --- fire, water, lightning or laser --- or @ref
		ShieldType::None when unshielded. Each type has its own visual effect (e.g. the fire and lightning shield
		shaders) and behaviour. @ref ShieldType::Count is the number of supported shield types.
	*/
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