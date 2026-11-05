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

	/** @brief What a character does when jump is pressed in the air, see @ref CharacterProfile */
	enum class CharacterAirJump : std::int32_t
	{
		None,			/**< Nothing */
		Copter,			/**< Helicopter ears */
		DoubleJump		/**< Another jump in the air */
	};

	/** @brief What a character does when jump is pressed while crouching, see @ref CharacterProfile */
	enum class CharacterGroundJump : std::int32_t
	{
		Crouch,			/**< Keeps crouching */
		Jump,			/**< Ordinary jump */
		Jazz,			/**< Uppercut */
		Spaz,			/**< Sidekick */
		Lori			/**< Lori's kick */
	};

	/**
		@brief Abilities of a character

		Shared by every player of the same @ref PlayerType. The level handler may override the defaults, see
		@ref ILevelHandler::GetCharacterProfile(). Speeds are in pixels per tick of the original game.
	*/
	struct CharacterProfile
	{
		/** @brief What the character does when jump is pressed in the air */
		CharacterAirJump AirJump;
		/** @brief What the character does when jump is pressed while crouching */
		CharacterGroundJump GroundJump;
		/** @brief Most double jumps in one airtime */
		std::int32_t DoubleJumpCountMax;
		/** @brief Horizontal speed of a double jump, in the direction the player faces */
		float DoubleJumpSpeedX;
		/** @brief Vertical speed of a double jump */
		float DoubleJumpSpeedY;
		/** @brief Most ticks of copter flight in one airtime */
		std::int32_t CopterDurationMax;
		/** @brief Horizontal speed of copter flight, in the direction the player faces */
		float CopterSpeedX;
		/** @brief Vertical speed of copter flight, negative rises */
		float CopterSpeedY;
		/** @brief Whether the attack of the bird hurts enemies */
		bool CanHurt;
		/** @brief Whether the character can run */
		bool CanRun;
		/** @brief Whether morph monitors turn players into the character */
		bool MorphBoxCycle;

		/** @brief Returns the default abilities of the character */
		static constexpr CharacterProfile GetDefault(PlayerType type) {
			return {
				(type == PlayerType::Spaz ? CharacterAirJump::DoubleJump
					: (type == PlayerType::Jazz || type == PlayerType::Lori ? CharacterAirJump::Copter : CharacterAirJump::None)),
				(type == PlayerType::Jazz ? CharacterGroundJump::Jazz
					: (type == PlayerType::Spaz ? CharacterGroundJump::Spaz
					: (type == PlayerType::Lori ? CharacterGroundJump::Lori : CharacterGroundJump::Crouch))),
				// Only Spaz has a double jump of his own, the others have none until a script sets both, as in JJ2+
				(type == PlayerType::Spaz ? 1 : 0), 0.0f, (type == PlayerType::Spaz ? -8.0f : 0.0f), UnlimitedCopterDuration, 0.0f, 1.0f, false, true,
				(type == PlayerType::Jazz || type == PlayerType::Spaz || type == PlayerType::Lori)
			};
		}

		/** @brief @ref CopterDurationMax with no limit */
		static constexpr std::int32_t UnlimitedCopterDuration = 9999999;
	};
}