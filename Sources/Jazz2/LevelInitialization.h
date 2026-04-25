#pragma once

#include "ExitType.h"
#include "GameDifficulty.h"
#include "PlayerType.h"
#include "WeaponType.h"

#include <Containers/ArrayView.h>
#include <Containers/StaticArray.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace Jazz2
{
	/** @brief Player carry over between levels */
	struct PlayerCarryOver
	{
		/** @{ @name Constants */

		/** @brief Number of weapons */
		static constexpr std::int32_t WeaponCount = (std::int32_t)WeaponType::Count;

		/** @} */

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
		StaticArray<4, std::int32_t> Gems;
		/** @brief Weapon ammo */
		StaticArray<WeaponCount, std::uint16_t> Ammo;
		/** @brief Weapon upgrades */
		StaticArray<WeaponCount, std::uint8_t> WeaponUpgrades;
	};

	/** @brief Level initialization parameters */
	struct LevelInitialization
	{
		/** @{ @name Constants */

		/** @brief Maximum player count */
		static constexpr std::int32_t MaxPlayerCount = 4;
		/** @brief Default number of lives */
		static constexpr std::int32_t DefaultLives = 3;

		/** @} */

		/** @brief Level name in `<episode>/<level>` format */
		String LevelName;
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
		StaticArray<MaxPlayerCount, PlayerCarryOver> PlayerCarryOvers;

		LevelInitialization();

		LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged);
		LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, PlayerType playerType);
		LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, ArrayView<const PlayerType> playerTypes);

		LevelInitialization(const LevelInitialization& copy) noexcept;
		LevelInitialization(LevelInitialization&& move) noexcept;

		/** @brief Returns number of assigned players */
		std::int32_t GetPlayerCount(const PlayerCarryOver** firstPlayer = nullptr) const;
	};
}