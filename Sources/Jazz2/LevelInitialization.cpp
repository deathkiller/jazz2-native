#include "LevelInitialization.h"

#include <algorithm>
#include <cstring>

namespace Jazz2
{
	LevelInitialization::LevelInitialization()
		: IsLocalSession(true), Difficulty(GameDifficulty::Normal), IsReforged(true), CheatsUsed(false),
			LastExitType(ExitType::None), ElapsedMilliseconds(0), PlayerCarryOvers{}
	{
	}

	LevelInitialization::LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged)
		: LevelName(level), IsLocalSession(true), Difficulty(difficulty), IsReforged(isReforged),
			CheatsUsed(false), LastExitType(ExitType::None), ElapsedMilliseconds(0), PlayerCarryOvers{}
	{
		for (std::int32_t i = 0; i < MaxPlayerCount; i++) {
			PlayerCarryOvers[i].Type = PlayerType::None;
		}
	}

	LevelInitialization::LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, PlayerType playerType)
		: LevelName(level), IsLocalSession(true), Difficulty(difficulty), IsReforged(isReforged),
			CheatsUsed(cheatsUsed), LastExitType(ExitType::None), ElapsedMilliseconds(0), PlayerCarryOvers{}
	{
		PlayerCarryOvers[0].Type = playerType;
		PlayerCarryOvers[0].Lives = DefaultLives;

		for (std::int32_t i = 1; i < MaxPlayerCount; i++) {
			PlayerCarryOvers[i].Type = PlayerType::None;
		}
	}

	LevelInitialization::LevelInitialization(StringView level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, ArrayView<const PlayerType> playerTypes)
		: LevelName(level), IsLocalSession(true), Difficulty(difficulty), IsReforged(isReforged),
			CheatsUsed(cheatsUsed), LastExitType(ExitType::None), ElapsedMilliseconds(0), PlayerCarryOvers{}
	{
		std::int32_t playerCount = std::min((std::int32_t)playerTypes.size(), MaxPlayerCount);
		for (std::int32_t i = 0; i < playerCount; i++) {
			PlayerCarryOvers[i].Type = playerTypes[i];
			PlayerCarryOvers[i].Lives = DefaultLives;
		}

		for (std::int32_t i = playerCount; i < MaxPlayerCount; i++) {
			PlayerCarryOvers[i].Type = PlayerType::None;
		}
	}

	LevelInitialization::LevelInitialization(const LevelInitialization& copy) noexcept
	{
		LevelName = copy.LevelName;
		IsLocalSession = copy.IsLocalSession;
		Difficulty = copy.Difficulty;
		IsReforged = copy.IsReforged;
		CheatsUsed = copy.CheatsUsed;
		LastExitType = copy.LastExitType;
		LastEpisodeName = copy.LastEpisodeName;
		ElapsedMilliseconds = copy.ElapsedMilliseconds;

		std::memcpy(PlayerCarryOvers, copy.PlayerCarryOvers, sizeof(PlayerCarryOvers));
	}

	LevelInitialization::LevelInitialization(LevelInitialization&& move) noexcept
	{
		LevelName = std::move(move.LevelName);
		IsLocalSession = move.IsLocalSession;
		Difficulty = move.Difficulty;
		IsReforged = move.IsReforged;
		CheatsUsed = move.CheatsUsed;
		LastExitType = move.LastExitType;
		LastEpisodeName = std::move(move.LastEpisodeName);
		ElapsedMilliseconds = move.ElapsedMilliseconds;

		std::memcpy(PlayerCarryOvers, move.PlayerCarryOvers, sizeof(PlayerCarryOvers));
	}

	std::int32_t LevelInitialization::GetPlayerCount(const PlayerCarryOver** firstPlayer) const
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
}