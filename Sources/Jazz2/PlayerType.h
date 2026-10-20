#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Player type
		
		Identifies the playable character --- Jazz, Spaz or Lori --- plus the temporary Frog and two Bird morphs and
		a @ref PlayerType::Spectate mode for multiplayer observers; @ref PlayerType::None is unspecified. Chosen when
		starting a level, carried in @ref LevelInitialization and @ref PlayerCarryOver, and used to select the
		character's metadata and abilities. Neither bird is ever carried over to the next level or into a save, the
		original character is.
	*/
	enum class PlayerType : std::uint8_t
	{
		None,					/**< None/unspecified */

		Jazz,					/**< Jazz */
		Spaz,					/**< Spaz */
		Lori,					/**< Lori */
		Frog,					/**< Frog */
		Bird,					/**< Yellow bird that attacks with its beak */
		Bird2,					/**< Red bird that shoots instead */

		Spectate = UINT8_MAX	/**< Spectate mode */
	};
}