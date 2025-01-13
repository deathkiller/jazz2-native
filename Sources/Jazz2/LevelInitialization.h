#pragma once

#include "ExitType.h"
#include "GameDifficulty.h"
#include "PlayerType.h"
#include "WeaponType.h"

#include <algorithm>
#include <cstring>

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

	/** @brief Level initialization */
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

		LevelInitialization()
			: IsLocalSession(true), ElapsedMilliseconds(0), PlayerCarryOvers{}
		{
		}

		LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged)
			: IsLocalSession(true), ElapsedMilliseconds(0), PlayerCarryOvers{}
		{
			LevelName = level;
			EpisodeName = episode;
			Difficulty = difficulty;
			IsReforged = isReforged;
			CheatsUsed = false;

			LastExitType = ExitType::None;

			for (std::int32_t i = 0; i < MaxPlayerCount; i++) {
				PlayerCarryOvers[i].Type = PlayerType::None;
			}
		}

		LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, PlayerType playerType)
			: IsLocalSession(true), ElapsedMilliseconds(0), PlayerCarryOvers{}
		{
			LevelName = level;
			EpisodeName = episode;
			Difficulty = difficulty;
			IsReforged = isReforged;
			CheatsUsed = cheatsUsed;

			LastExitType = ExitType::None;

			PlayerCarryOvers[0].Type = playerType;
			PlayerCarryOvers[0].Lives = DefaultLives;

			for (std::int32_t i = 1; i < MaxPlayerCount; i++) {
				PlayerCarryOvers[i].Type = PlayerType::None;
			}
		}

		LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, ArrayView<const PlayerType> playerTypes)
			: IsLocalSession(true), ElapsedMilliseconds(0), PlayerCarryOvers{}
		{
			LevelName = level;
			EpisodeName = episode;
			Difficulty = difficulty;
			IsReforged = isReforged;
			CheatsUsed = cheatsUsed;

			LastExitType = ExitType::None;

			std::int32_t playerCount = std::min((std::int32_t)playerTypes.size(), MaxPlayerCount);
			for (std::int32_t i = 0; i < playerCount; i++) {
				PlayerCarryOvers[i].Type = playerTypes[i];
				PlayerCarryOvers[i].Lives = DefaultLives;
			}

			for (std::int32_t i = playerCount; i < MaxPlayerCount; i++) {
				PlayerCarryOvers[i].Type = PlayerType::None;
			}
		}

		LevelInitialization(const LevelInitialization& copy)
		{
			LevelName = copy.LevelName;
			EpisodeName = copy.EpisodeName;
			IsLocalSession = copy.IsLocalSession;
			Difficulty = copy.Difficulty;
			IsReforged = copy.IsReforged;
			CheatsUsed = copy.CheatsUsed;
			LastExitType = copy.LastExitType;
			LastEpisodeName = copy.LastEpisodeName;
			ElapsedMilliseconds = copy.ElapsedMilliseconds;

			std::memcpy(PlayerCarryOvers, copy.PlayerCarryOvers, sizeof(PlayerCarryOvers));
		}

		LevelInitialization(LevelInitialization&& move)
		{
			LevelName = std::move(move.LevelName);
			EpisodeName = std::move(move.EpisodeName);
			IsLocalSession = move.IsLocalSession;
			Difficulty = move.Difficulty;
			IsReforged = move.IsReforged;
			CheatsUsed = move.CheatsUsed;
			LastExitType = move.LastExitType;
			LastEpisodeName = std::move(move.LastEpisodeName);
			ElapsedMilliseconds = move.ElapsedMilliseconds;

			std::memcpy(PlayerCarryOvers, move.PlayerCarryOvers, sizeof(PlayerCarryOvers));
		}

		/** @brief Returns number of assigned players */
		std::int32_t GetPlayerCount(const PlayerCarryOver** firstPlayer = nullptr) const
		{
			if (firstPlayer != nullptr) {
				*firstPlayer = nullptr;
			}

			std::int32_t playerCount = 0;
			for (std::int32_t i = playerCount; i < MaxPlayerCount; i++) {
				if (PlayerCarryOvers[i].Type != PlayerType::None) {
					playerCount++;
					if (firstPlayer != nullptr && *firstPlayer == nullptr) {
						*firstPlayer = &PlayerCarryOvers[i];
					}
				}
			}

			return playerCount;
		}
	};
}