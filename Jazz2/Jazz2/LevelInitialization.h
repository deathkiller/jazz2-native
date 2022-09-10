#pragma once

#include <cstring>

#include <Containers/String.h>

using namespace Death::Containers;

namespace Jazz2
{
    enum class GameDifficulty {
        Default,

        Easy,
        Normal,
        Hard,

        Multiplayer
    };

    enum class ExitType {
        None,

        Normal,
        Warp,
        Bonus,
        Special,
        Boss
    };

    enum class WeaponType {
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

    enum class ShieldType {
        None,

        Fire,
        Water,
        Lightning,
        Laser
    };

    enum class PlayerType {
        None,

        Jazz,
        Spaz,
        Lori,
        Frog
    };

    struct PlayerCarryOver {
        static constexpr int WeaponCount = (int)WeaponType::Count;

        PlayerType Type;

        int32_t Lives;
        int32_t Score;
        int32_t FoodEaten;
        WeaponType CurrentWeapon;
        uint16_t Ammo[WeaponCount];
        uint8_t WeaponUpgrades[WeaponCount];
    };

    struct LevelInitialization {
        static constexpr int MaxPlayerCount = 4;
        static constexpr int DefaultLives = 3;

        String LevelName;
        String EpisodeName;

        GameDifficulty Difficulty;
        bool ReduxMode, CheatsUsed;
        ExitType LastExitType;

        PlayerCarryOver PlayerCarryOvers[MaxPlayerCount];

        String LastEpisodeName;

        LevelInitialization()
            : PlayerCarryOvers { }
        {
        }

        LevelInitialization(const String& episode, const String& level, GameDifficulty difficulty, bool reduxMode, bool cheatsUsed, PlayerType playerType)
            : PlayerCarryOvers { }
        {
            LevelName = level;
            EpisodeName = episode;
            Difficulty = difficulty;
            ReduxMode = reduxMode;
            CheatsUsed = cheatsUsed;

            LastExitType = ExitType::None;

            PlayerCarryOvers[0].Type = playerType;
            PlayerCarryOvers[0].Lives = DefaultLives;

            for (int i = 1; i < MaxPlayerCount; i++) {
                PlayerCarryOvers[i].Type = PlayerType::None;
            }
        }

        LevelInitialization(const String& episode, const String& level, GameDifficulty difficulty, bool reduxMode, bool cheatsUsed, const PlayerType* playerTypes, int playerCount)
            : PlayerCarryOvers { }
        {
            LevelName = level;
            EpisodeName = episode;
            Difficulty = difficulty;
            ReduxMode = reduxMode;
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
            ReduxMode = copy.ReduxMode;
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
            ReduxMode = move.ReduxMode;
            CheatsUsed = move.CheatsUsed;
            LastExitType = move.LastExitType;
            LastEpisodeName = std::move(move.LastEpisodeName);

            std::memcpy(PlayerCarryOvers, move.PlayerCarryOvers, sizeof(PlayerCarryOvers));
        }
    };
}