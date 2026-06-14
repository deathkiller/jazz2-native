#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Player type
		
		Identifies the playable character --- Jazz, Spaz or Lori --- plus the temporary Frog morph and a @ref
		PlayerType::Spectate mode for multiplayer observers; @ref PlayerType::None is unspecified. Chosen when
		starting a level, carried in @ref LevelInitialization and @ref PlayerCarryOver, and used to select the
		character's metadata and abilities.
	*/
	enum class PlayerType : std::uint8_t
	{
		None,					/**< None/unspecified */

		Jazz,					/**< Jazz */
		Spaz,					/**< Spaz */
		Lori,					/**< Lori */
		Frog,					/**< Frog */

		Spectate = UINT8_MAX	/**< Spectate mode */
	};
}