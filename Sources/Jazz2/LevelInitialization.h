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
	struct PlayerCarryOver
	{
		static constexpr std::int32_t WeaponCount = (std::int32_t)WeaponType::Count;

		PlayerType Type;
		WeaponType CurrentWeapon;
		std::uint8_t Lives;
		std::uint8_t FoodEaten;
		std::int32_t Score;
		std::int32_t Gems[4];
		std::uint16_t Ammo[WeaponCount];
		std::uint8_t WeaponUpgrades[WeaponCount];
	};

	struct LevelInitialization
	{
		static constexpr std::int32_t MaxPlayerCount = 4;
		static constexpr std::int32_t DefaultLives = 3;

		String LevelName;
		String EpisodeName;
		String LastEpisodeName;

		GameDifficulty Difficulty;
		bool IsReforged, CheatsUsed;
		ExitType LastExitType;
		std::uint64_t ElapsedMilliseconds;

		PlayerCarryOver PlayerCarryOvers[MaxPlayerCount];

		LevelInitialization()
			: ElapsedMilliseconds(0), PlayerCarryOvers{}
		{
		}

		LevelInitialization(StringView episode, StringView level, GameDifficulty difficulty, bool isReforged)
			: ElapsedMilliseconds(0), PlayerCarryOvers{}
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
			: ElapsedMilliseconds(0), PlayerCarryOvers{}
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
			: ElapsedMilliseconds(0), PlayerCarryOvers{}
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
			Difficulty = move.Difficulty;
			IsReforged = move.IsReforged;
			CheatsUsed = move.CheatsUsed;
			LastExitType = move.LastExitType;
			LastEpisodeName = std::move(move.LastEpisodeName);
			ElapsedMilliseconds = move.ElapsedMilliseconds;

			std::memcpy(PlayerCarryOvers, move.PlayerCarryOvers, sizeof(PlayerCarryOvers));
		}

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