#pragma once

#include "../Common.h"

namespace Jazz2
{
	enum class EventType : std::uint16_t
	{
		Empty = 0,

		// Basic
		LevelStart,
		LevelStartMultiplayer,
		Checkpoint,

		// Scenery
		SceneryDestruct,
		SceneryDestructButtstomp,
		SceneryDestructSpeed,
		SceneryCollapse,

		// Modifiers
		ModifierVine,
		ModifierOneWay,
		ModifierHook,
		ModifierHPole,
		ModifierVPole,
		ModifierHurt,
		ModifierTube,
		ModifierRicochet,
		ModifierSlide,
		ModifierDeath,
		ModifierSetWater,
		ModifierLimitCameraView,
		ModifierNoClimb,

		// Area
		AreaStopEnemy,
		AreaFloatUp,
		AreaHForce,
		AreaText,
		AreaEndOfLevel,
		AreaCallback,
		AreaActivateBoss,
		AreaFlyOff,
		AreaRevertMorph,
		AreaMorphToFrog,
		AreaNoFire,
		AreaWaterBlock,
		AreaWeather,
		AreaAmbientSound,
		AreaAmbientBubbles,

		// Triggers
		TriggerCrate,
		TriggerArea,
		TriggerZone,

		// Warp
		WarpCoinBonus,
		WarpOrigin,
		WarpTarget,

		// Lights
		LightAmbient,
		LightSteady,
		LightPulse,
		LightFlicker,
		LightIlluminate,

		// Environment
		Spring,
		Bridge,
		MovingPlatform,
		PushableBox,
		Eva,
		Pole,
		SignEOL,
		Moth,
		SteamNote,
		Bomb,
		PinballBumper,
		PinballPaddle,
		CtfBase,
		BirdCage,
		Stopwatch,
		SpikeBall,
		AirboardGenerator,
		Copter,
		RollingRock,
		RollingRockTrigger,
		SwingingVine,

		// Enemies
		EnemyTurtle,
		EnemyLizard,
		EnemySucker,
		EnemySuckerFloat,
		EnemyLabRat,
		EnemyHelmut,
		EnemyDragon,
		EnemyBat,
		EnemyFatChick,
		EnemyFencer,
		EnemyRapier,
		EnemySparks,
		EnemyMonkey,
		EnemyDemon,
		EnemyBee,
		EnemyBeeSwarm,
		EnemyCaterpillar,
		EnemyCrab,
		EnemyDoggy,
		EnemyDragonfly,
		EnemyFish,
		EnemyMadderHatter,
		EnemyRaven,
		EnemySkeleton,
		EnemyTurtleTough,
		EnemyTurtleTube,
		EnemyWitch,
		EnemyLizardFloat,

		BossTweedle,
		BossBilsy,
		BossDevan,
		BossQueen,
		BossRobot,
		BossUterus,
		BossTurtleTough,
		BossBubba,
		BossDevanRemote,
		BossBolly,

		TurtleShell,

		// Collectibles
		Coin,
		Gem,
		GemGiant,
		GemRing,
		GemStomp,
		Carrot,
		CarrotFly,
		CarrotInvincible,
		OneUp,
		FastFire,
		Food,
		Ammo,
		PowerUpWeapon,

		// Containers
		Crate,
		Barrel,
		CrateAmmo,
		BarrelAmmo,
		CrateGem,
		BarrelGem,
		PowerUpMorph,
		PowerUpShield,

		// End of enumeration
		Count,

		Generator = 0x1000,

		// Multiplayer-only remotable actors
		WeaponBlaster = 0x2001,
		WeaponBouncer = 0x2002,
		WeaponElectro = 0x2003,
		WeaponFreezer = 0x2004,
		WeaponPepper = 0x2005,
		WeaponRF = 0x2006,
		WeaponSeeker = 0x2007,
		WeaponThunderbolt = 0x2008,
		WeaponTNT = 0x2009,
		WeaponToaster = 0x200A,
	};
}