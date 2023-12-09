#pragma once

#include "../Common.h"

namespace Jazz2
{
	enum class WeaponType : std::uint8_t {
		Blaster = 0,
		Bouncer,
		Freezer,
		Seeker,
		RF,
		Toaster,
		TNT,
		Pepper,
		Electro,

		Thunderbolt,

		Count,
		Unknown = UINT8_MAX
	};
}