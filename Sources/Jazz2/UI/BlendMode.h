#pragma once

#include "../../Main.h"

namespace Jazz2::UI
{
	/** @brief How a solid rectangle is blended with what's behind it */
	enum class BlendMode
	{
		Normal,		/**< Over what's behind by its alpha */
		Additive,	/**< Added to what's behind */
		Multiply,	/**< What's behind times the color */
		Screen		/**< What's behind lightened by the color */
	};
}
