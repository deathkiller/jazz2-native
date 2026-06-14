#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Exit type
		
		Describes how the player left a level when a level change begins. The low bits (extracted with
		@ref ExitType::TypeMask) select the kind of exit --- normal, warp, bonus, special or boss --- and the high
		bits are independent modifiers: @ref ExitType::Frozen spawns the player frozen in the next level and
		@ref ExitType::FastTransition skips the fade transitions. Carried in @ref LevelInitialization to the next
		level. Supports a bitwise combination of its member values.
	*/
	enum class ExitType : std::uint8_t
	{
		None,						/**< Unspecified */

		Normal,						/**< Standard exit */
		Warp,						/**< Warp exit */
		Bonus,						/**< Bonus exit */
		Special,					/**< Special exit */
		Boss,						/**< Exit after killing a boxx */

		TypeMask = 0x0f,			/**< Mask for exit type */
		Frozen = 0x40,				/**< Player should spawn frozen in the next level */
		FastTransition = 0x80		/**< Animated fade transitions should be skipped */
	};

	DEATH_ENUM_FLAGS(ExitType);
}