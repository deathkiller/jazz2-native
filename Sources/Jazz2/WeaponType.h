#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Player weapon type
		
		Identifies one of the player's weapons --- Blaster, Bouncer, Freezer, Seeker, RF, Toaster, TNT, Pepper,
		Electro and Thunderbolt --- with @ref WeaponType::Unknown for an unspecified weapon. The values double as
		indices into the per-player ammo and upgrade arrays, and @ref WeaponType::Count is the number of weapons.
		Carried in @ref PlayerCarryOver between levels.
	*/
	enum class WeaponType : std::uint8_t
	{
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