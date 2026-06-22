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
	/**
		@brief Player carry over between levels
		
		Snapshot of a single player's persistent progress (type, current weapon, lives, food, score, gems, ammo and
		weapon upgrades) handed from a finished level to the next one through @ref LevelInitialization.
	*/
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

	/**
		@brief Level initialization parameters
		
		Describes which level to start and under what conditions --- target level name, difficulty, session/reforged
		flags, last exit type, elapsed time and the per-player @ref PlayerCarryOver entries. Passed to the root
		controller to change level and consumed by the level handler when spawning players.
	*/
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
		/** @brief Game mode of a local splitscreen multiplayer session (value of @ref Multiplayer::MpGameMode); @ref Multiplayer::MpGameMode::Unknown (the default) marks a plain single-player local session that uses the base level handler, anything else selects the local @ref Multiplayer::MpLevelHandler running that mode */
		std::uint8_t LocalMultiplayerGameMode;
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

		/** @brief Creates a new instance */
		LevelInitialization();

		/**
		 * @brief Creates a new instance for a given level
		 *
		 * @param level			Level name in `<episode>/<level>` format
		 * @param difficulty	Difficulty
		 * @param isReforged	Whether reforged gameplay is enabled
		 */
		LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged);
		/**
		 * @brief Creates a new instance for a given level and a single player
		 *
		 * @param level			Level name in `<episode>/<level>` format
		 * @param difficulty	Difficulty
		 * @param isReforged	Whether reforged gameplay is enabled
		 * @param cheatsUsed	Whether cheats were used
		 * @param playerType	Type of the player
		 */
		LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, PlayerType playerType);
		/**
		 * @brief Creates a new instance for a given level and multiple players
		 *
		 * @param level			Level name in `<episode>/<level>` format
		 * @param difficulty	Difficulty
		 * @param isReforged	Whether reforged gameplay is enabled
		 * @param cheatsUsed	Whether cheats were used
		 * @param playerTypes	Types of the players
		 */
		LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, ArrayView<const PlayerType> playerTypes);

		/** @brief Copy constructor */
		LevelInitialization(const LevelInitialization& copy) noexcept;
		/** @brief Move constructor */
		LevelInitialization(LevelInitialization&& move) noexcept;

		/** @brief Returns number of assigned players */
		std::int32_t GetPlayerCount(const PlayerCarryOver** firstPlayer = nullptr) const;
	};
}