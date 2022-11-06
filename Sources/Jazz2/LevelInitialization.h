#pragma once

#include <cstring>

#include <Containers/String.h>

using namespace Death::Containers;

namespace Jazz2
{
	enum class GameDifficulty : uint8_t {
		Default,

		Easy,
		Normal,
		Hard,

		Multiplayer
	};

	enum class ExitType : uint8_t {
		None,

		Normal,
		Warp,
		Bonus,
		Special,
		Boss,

		TypeMask = 0x0f,
		FastTransition = 0x80
	};

	DEFINE_ENUM_OPERATORS(ExitType);

	enum class WeaponType : uint8_t {
		Blaster = 0,
		Bouncer,
		Freezer,
		Seeker,
		RF,
		Toaster,
		TNT,
		Pepper,
		Electro,

		Thunderbolt,

		Count,
		Unknown = UINT8_MAX
	};

	enum class ShieldType : uint8_t {
		None,

		Fire,
		Water,
		Lightning,
		Laser
	};

	enum class PlayerType : uint8_t {
		None,

		Jazz,
		Spaz,
		Lori,
		Frog
	};

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
			: PlayerCarryOvers { }
		{
		}

		LevelInitialization(const StringView& episode, const StringView& level, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, PlayerType playerType)
			: PlayerCarryOvers { }
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
			: PlayerCarryOvers { }
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