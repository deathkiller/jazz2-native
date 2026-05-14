#pragma once

#include "../Main.h"

namespace Jazz2
{
	/** @brief Player type */
	enum class PlayerType : std::uint8_t
	{
		None,					/**< None/unspecified */

		Jazz,					/**< Jazz */
		Spaz,					/**< Spaz */
		Lori,					/**< Lori */
		Frog,					/**< Frog */
		Bird,					/**< Bird */
		BirdYellow,			/**< Bird (Yellow) */

		Spectate = UINT8_MAX	/**< Spectate mode */
	};

	/** @brief Returns next type in base morph cycle (Jazz -> Spaz -> Lori -> Jazz) */
	inline constexpr PlayerType GetNextBaseMorphType(PlayerType currentType)
	{
		switch (currentType) {
			case PlayerType::Spaz: return PlayerType::Lori;
			case PlayerType::Lori: return PlayerType::Jazz;
			default: return PlayerType::Spaz;
		}
	}

	/** @brief Returns `true` if the type is one of Bird morph variants */
	inline constexpr bool IsBirdMorphType(PlayerType playerType)
	{
		return (playerType == PlayerType::Bird || playerType == PlayerType::BirdYellow);
	}

	/** @brief Returns next type in cheat morph cycle (Jazz -> Spaz -> Lori -> Frog -> Bird -> BirdYellow -> Jazz) */
	inline constexpr PlayerType GetNextCheatMorphType(PlayerType currentType)
	{
		switch (currentType) {
			case PlayerType::Jazz: return PlayerType::Spaz;
			case PlayerType::Spaz: return PlayerType::Lori;
			case PlayerType::Lori: return PlayerType::Frog;
			case PlayerType::Frog: return PlayerType::Bird;
			case PlayerType::Bird: return PlayerType::BirdYellow;
			default: return PlayerType::Jazz;
		}
	}
}