#pragma once

#include "../Common.h"

namespace Jazz2
{
    enum class EventType : uint16_t
    {
        Empty = 0x0000,

        Generator = 0x1000,

        // Basic
        LevelStart = 0x0100,
        LevelStartMultiplayer = 0x0101,
        Checkpoint = 0x005A,

        // Scenery
        SceneryDestruct = 0x0116,
        SceneryDestructButtstomp = 0x0117,
        SceneryDestructSpeed = 0x0118,
        SceneryCollapse = 0x0119,

        // Modifiers
        ModifierVine = 0x0110,
        ModifierOneWay = 0x0111,
        ModifierHook = 0x0112,
        ModifierHPole = 0x0113,
        ModifierVPole = 0x0114,
        ModifierHurt = 0x0115,
        ModifierTube = 0x0144,
        ModifierRicochet = 0x0145,
        ModifierSlide = 0x0147,
        ModifierDeath = 0x0148,
        ModifierSetWater = 0x0149,
        ModifierLimitCameraView = 0x014B,
        ModifierNoClimb = 0x014C,

        // Area
        AreaStopEnemy = 0x0143,
        AreaFloatUp = 0x0146,
        AreaHForce = 0x0401,
        AreaText = 0x014A,
        AreaEndOfLevel = 0x0108,
        AreaCallback = 0x0109,
        AreaActivateBoss = 0x010A,
        AreaFlyOff = 0x010B,
        AreaRevertMorph = 0x010C,
        AreaMorphToFrog = 0x0130,
        AreaNoFire = 0x0131,
        AreaWaterBlock = 0x0132,
        AreaWeather = 0x0510,
        AreaAmbientSound = 0x0511,
        AreaAmbientBubbles = 0x0512,

        // Triggers
        TriggerCrate = 0x0060,
        TriggerArea = 0x011A,
        TriggerZone = 0x011B,

        // Warp
        WarpCoinBonus = 0x010D,
        WarpOrigin = 0x010E,
        WarpTarget = 0x010F,

        // Lights
        LightSet = 0x0120,
        LightReset = 0x0124,
        LightSteady = 0x0121,
        LightPulse = 0x0122,
        LightFlicker = 0x0123,
        LightIlluminate = 0x0125,

        // Environment
        Spring = 0x00C0,
        Bridge = 0x00C3,
        MovingPlatform = 0x00C4,
        PushableBox = 0x00C5,
        Eva = 0x0500,
        Pole = 0x0501,
        SignEOL = 0x0502,
        Moth = 0x0503,
        SteamNote = 0x0504,
        Bomb = 0x0505,
        PinballBumper = 0x0506,
        PinballPaddle = 0x0507,
        CtfBase = 0x0508,

        // Enemies
        EnemyTurtle = 0x0180,
        EnemyLizard = 0x0185,
        EnemySucker = 0x018C,
        EnemySuckerFloat = 0x018D,
        EnemyLabRat = 0x018B,
        EnemyHelmut = 0x018E,
        EnemyDragon = 0x018F,
        EnemyBat = 0x0190,
        EnemyFatChick = 0x0191,
        EnemyFencer = 0x0192,
        EnemyRapier = 0x0193,
        EnemySparks = 0x0194,
        EnemyMonkey = 0x0195,
        EnemyDemon = 0x0196,
        EnemyBee = 0x0197,
        EnemyBeeSwarm = 0x0198,
        EnemyCaterpillar = 0x0199,
        EnemyCrab = 0x019A,
        EnemyDoggy = 0x019B,
        EnemyDragonfly = 0x019C,
        EnemyFish = 0x019D,
        EnemyMadderHatter = 0x019E,
        EnemyRaven = 0x019F,
        EnemySkeleton = 0x0200,
        EnemyTurtleTough = 0x0201,
        EnemyTurtleTube = 0x0202,
        EnemyWitch = 0x0204,
        EnemyLizardFloat = 0x0205,

        BossTweedle = 0x0203,
        BossBilsy = 0x0210,
        BossDevan = 0x0211,
        BossQueen = 0x0212,
        BossRobot = 0x0213,
        BossUterus = 0x0214,
        BossTurtleTough = 0x0215,
        BossBubba = 0x0216,
        BossDevanRemote = 0x0217,
        BossBolly = 0x0218,

        TurtleShell = 0x0182,

        // Collectibles
        Coin = 0x0048,
        Gem = 0x0040,
        GemGiant = 0x0041,
        GemRing = 0x0042,
        GemStomp = 0x0043,
        Carrot = 0x0050,
        CarrotFly = 0x0051,
        CarrotInvincible = 0x0052,
        OneUp = 0x0055,
        FastFire = 0x0001,

        // Weapons
        Ammo = 0x0002,
        PowerUpWeapon = 0x0003,

        // Food
        Food = 0x0004,

        // Containers
        Crate = 0x0061,
        Barrel = 0x0062,
        CrateAmmo = 0x0063,
        BarrelAmmo = 0x0064,
        CrateGem = 0x0065,
        BarrelGem = 0x0066,

        PowerUpMorph = 0x0067,
        BirdCage = 0x0068,

        AirboardGenerator = 0x0069,

        PowerUpShield = 0x006A,
        Stopwatch = 0x006B,
        SpikeBall = 0x006C,

        Copter = 0x006D,

        RollingRock = 0x006E,
        RollingRockTrigger = 0x006F,
        SwingingVine = 0x0070,

        // Multiplayer-only remotable actors
        WeaponBlaster = 0x0601,
        WeaponBouncer = 0x0602,
        WeaponElectro = 0x0603,
        WeaponFreezer = 0x0604,
        WeaponPepper = 0x0605,
        WeaponRF = 0x0606,
        WeaponSeeker = 0x0607,
        WeaponThunderbolt = 0x0608,
        WeaponTNT = 0x0609,
        WeaponToaster = 0x060A,
    };
}