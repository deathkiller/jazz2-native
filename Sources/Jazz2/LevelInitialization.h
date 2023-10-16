#pragma once

#include "ExitType.h"
#include "GameDifficulty.h"
#include "PlayerType.h"
#include "WeaponType.h"

#include <cstring>

#include <Containers/String.h>

using namespace Death::Containers;

namespace Jazz2
{
	struct PlayerCarryOver {
		static constexpr int WeaponCount = (int)WeaponType::Count;

		PlayerType Type;
		WeaponType CurrentWeapon;
		uint8_t Lives;
		uint8_t FoodEaten;
		int32_t Score;
		uint16_t Ammo[WeaponCount];
		uint8_t WeaponUpgrades[WeaponCount];
	};

	struct LevelInitialization {
		static constexpr int MaxPlayerCount = 4;
		static constexpr int DefaultLives = 3;

		String LevelName;
		String EpisodeName;
		String LastEpisodeName;

		GameDifficulty Difficulty;
		bool IsReforged, CheatsUsed;
		ExitType LastExitType;

		PlayerCarryOver PlayerCarryOvers[MaxPlayerCount];

		LevelInitialization()
			: PlayerCarryOvers{}
		{
		}

		LevelInitialization(const StringView& episode, const StringView& level, GameDifficulty difficulty, bool isReforged)
			: PlayerCarryOvers{}
		{
			LevelName = level;
			EpisodeName = episode;
			Difficulty = difficulty;
			IsReforged = isReforged;
			CheatsUsed = false;

			LastExitType = ExitType::None;

			for (int i = 0; i < MaxPlayerCount; i++) {
				PlayerCarryOvers[i].Type = PlayerType::None;
			}
		}

		LevelInitialization(const StringView& episode, const StringView& level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, PlayerType playerType)
			: PlayerCarryOvers{}
		{
			LevelName = level;
			EpisodeName = episode;
			Difficulty = difficulty;
			IsReforged = isReforged;
			CheatsUsed = cheatsUsed;

			LastExitType = ExitType::None;

			PlayerCarryOvers[0].Type = playerType;
			PlayerCarryOvers[0].Lives = DefaultLives;

			for (int i = 1; i < MaxPlayerCount; i++) {
				PlayerCarryOvers[i].Type = PlayerType::None;
			}
		}

		LevelInitialization(const StringView& episode, const StringView& level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, const PlayerType* playerTypes, int playerCount)
			: PlayerCarryOvers{}
		{
			LevelName = level;
			EpisodeName = episode;
			Difficulty = difficulty;
			IsReforged = isReforged;
			CheatsUsed = cheatsUsed;

			LastExitType = ExitType::None;

			for (int i = 0; i < playerCount; i++) {
				PlayerCarryOvers[i].Type = playerTypes[i];
				PlayerCarryOvers[i].Lives = DefaultLives;
			}

			for (int i = playerCount; i < MaxPlayerCount; i++) {
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

			std::memcpy(PlayerCarryOvers, move.PlayerCarryOvers, sizeof(PlayerCarryOvers));
		}
	};
}