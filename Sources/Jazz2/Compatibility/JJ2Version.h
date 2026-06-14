#pragma once

#include "../../Main.h"

namespace Jazz2::Compatibility
{
	/**
		@brief Version of the original game, supports a bitwise combination of its member values
		
		Identifies which release of the original game a file being imported originates from (base game,
		The Secret Files, Holiday Hare or Christmas Chronicles), together with flags marking the JJ2+
		extension and the shareware demo. Detected during conversion and used to drive
		version-specific import behavior.
	*/
	enum class JJ2Version : std::uint16_t
	{
		Unknown = 0x0000,					/**< Unknown version */

		BaseGame = 0x0001,					/**< Jazz Jackrabbit 2 base game */
		TSF = 0x0002,						/**< The Secret Files expansion */
		HH = 0x0004,						/**< Holiday Hare */
		CC = 0x0008,						/**< Christmas Chronicles */

		PlusExtension = 0x0100,				/**< JJ2+ extension is present */
		SharewareDemo = 0x0200,				/**< Shareware demo version */

		All = 0xffff						/**< All versions */
	};

	DEATH_ENUM_FLAGS(JJ2Version);
}