#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Player weapon type */
	enum class WeaponType : std::uint8_t {
		Unknown = UINT8_MAX,		/**< Unspecified */

		Blaster = 0,				/**< Blaster */
		Bouncer,					/**< Bouncer */
		Freezer,					/**< Freezer */
		Seeker,						/**< Seeker */
		RF,							/**< RF */
		Toaster,					/**< Toaster */
		TNT,						/**< TNT */
		Pepper,						/**< Pepper */
		Electro,					/**< Electro */

		Thunderbolt,				/**< Thunderbolt */

		Count						/**< Number of weapons */
	};
}