#pragma once

#include <string>

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
        Special
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
        Unknown = UINT16_MAX
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
        int16_t Ammo[WeaponCount];
        uint8_t WeaponUpgrades[WeaponCount];
        int32_t Score;
        int32_t FoodEaten;
        WeaponType CurrentWeapon;
    };

    struct LevelInitialization {
        static constexpr int MaxPlayerCount = 4;
        static constexpr int DefaultLives = 3;

        std::string LevelName;
        std::string EpisodeName;

        GameDifficulty Difficulty;
        bool ReduxMode, CheatsUsed;
        ExitType ExitType;

        PlayerCarryOver PlayerCarryOvers[MaxPlayerCount];

        std::string LastEpisodeName;

        LevelInitialization()
            : PlayerCarryOvers { }
        {
        }

        LevelInitialization(const std::string& episode, const std::string& level, GameDifficulty difficulty, bool reduxMode, bool cheatsUsed, PlayerType playerType)
            : PlayerCarryOvers { }
        {
            LevelName = level;
            EpisodeName = episode;
            Difficulty = difficulty;
            ReduxMode = reduxMode;
            CheatsUsed = cheatsUsed;

            ExitType = ExitType::None;

            PlayerCarryOvers[0].Type = playerType;
            PlayerCarryOvers[0].Lives = DefaultLives;

            for (int i = 1; i < MaxPlayerCount; i++) {
                PlayerCarryOvers[i].Type = PlayerType::None;
            }
        }

        LevelInitialization(const std::string& episode, const std::string& level, GameDifficulty difficulty, bool reduxMode, bool cheatsUsed, const PlayerType* playerTypes, int playerCount)
            : PlayerCarryOvers { }
        {
            LevelName = level;
            EpisodeName = episode;
            Difficulty = difficulty;
            ReduxMode = reduxMode;
            CheatsUsed = cheatsUsed;

            ExitType = ExitType::None;

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
            ExitType = copy.ExitType;
            LastEpisodeName = copy.LastEpisodeName;

            memcpy(PlayerCarryOvers, copy.PlayerCarryOvers, sizeof(PlayerCarryOvers));
        }
    };
}