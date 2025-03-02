#pragma once

#include "ExitType.h"
#include "GameDifficulty.h"
#include "PlayerType.h"
#include "WeaponType.h"

#include <Containers/ArrayView.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace Jazz2
{
	/** @brief Player carry over between levels */
	struct PlayerCarryOver
	{
		/** @brief Number of weapons */
		static constexpr std::int32_t WeaponCount = (std::int32_t)WeaponType::Count;

		/** @brief Player type */
		PlayerType Type;
		/** @brief Current weapon type */
		WeaponType CurrentWeapon;
		/** @brief Lives */
		std::uint8_t Lives;
		/** @brief Food eaten */
		std::uint8_t FoodEaten;
		/** @brief Score */
		std::int32_t Score;
		/** @brief Gems collected */
		std::int32_t Gems[4];
		/** @brief Weapon ammo */
		std::uint16_t Ammo[WeaponCount];
		/** @brief Weapon upgrades */
		std::uint8_t WeaponUpgrades[WeaponCount];
	};

	/** @brief Level initialization parameters */
	struct LevelInitialization
	{
		/** @brief Maximum player count */
		static constexpr std::int32_t MaxPlayerCount = 4;
		/** @brief Default number of lives */
		static constexpr std::int32_t DefaultLives = 3;

		/** @brief Level name */
		String LevelName;
		/** @brief Episode name */
		String EpisodeName;
		/** @brief Last episode name */
		String LastEpisodeName;

		/** @brief Whether the session is local (not online) */
		bool IsLocalSession;
		/** @brief Difficulty */
		GameDifficulty Difficulty;
		/** @brief Whether reforged gameplay is enabled */
		bool IsReforged;
		/** @brief Whether cheats were used */
		bool CheatsUsed;
		/** @brief Last exit type */
		ExitType LastExitType;
		/** @brief Elapsed milliseconds (gameplay time) */
		std::uint64_t ElapsedMilliseconds;

		/** @brief Player carry over descriptions */
		PlayerCarryOver PlayerCarryOvers[MaxPlayerCount];

		LevelInitialization();

		LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged);
		LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, PlayerType playerType);
		LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, ArrayView<const PlayerType> playerTypes);

		LevelInitialization(const LevelInitialization& copy) noexcept;
		LevelInitialization(LevelInitialization&& move) noexcept;

		/** @brief Returns number of assigned players */
		std::int32_t GetPlayerCount(const PlayerCarryOver** firstPlayer = nullptr) const;
	};
}