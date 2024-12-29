#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Shield type */
	enum class ShieldType : uint8_t
	{
		None,

		Fire,
		Water,
		Lightning,
		Laser,

		Count
	};
}