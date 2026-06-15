#pragma once

#include "../Main.h"

namespace Jazz2
{
	/**
		@brief Event type
		
		Identifies every kind of event or object that can be placed in a level or spawned at runtime --- player and
		checkpoint markers, destructible/modifier scenery tiles, trigger and warp markers, lights, environment objects,
		enemies and bosses, collectibles and containers. Values below @ref EventType::Count are the well-known events
		stored in the event map; @ref EventType::Generator marks a generator that repeatedly spawns another event, and
		the `0x2000`-range values are projectiles/objects remotable in multiplayer. The event spawner maps each value
		to the actor it instantiates.
	*/
	enum class EventType : std::uint16_t
	{
		Empty = 0,							/**< No event */

		// Basic
		LevelStart,							/**< Player spawn point */
		LevelStartMultiplayer,				/**< Multiplayer player spawn point */
		Checkpoint,							/**< Checkpoint */

		// Scenery
		SceneryDestruct,					/**< Tile destructible by weapon fire */
		SceneryDestructButtstomp,			/**< Tile destructible by buttstomp */
		SceneryDestructSpeed,				/**< Tile destructible by running at high speed */
		SceneryCollapse,					/**< Tile that collapses after being stepped on */

		// Modifiers
		ModifierVine,						/**< Vine the player can climb */
		ModifierOneWay,						/**< One-way platform tile */
		ModifierHook,						/**< Hook the player can hang on */
		ModifierHPole,						/**< Horizontal pole */
		ModifierVPole,						/**< Vertical pole */
		ModifierHurt,						/**< Tile that hurts the player */
		ModifierTube,						/**< Tube that transports the player */
		ModifierRicochet,					/**< Tile that ricochets projectiles */
		ModifierSlide,						/**< Tile the player slides on */
		ModifierDeath,						/**< Tile that instantly kills the player */
		ModifierSetWater,					/**< Sets the water level */
		ModifierLimitCameraView,			/**< Limits the camera view */
		ModifierNoClimb,					/**< Tile the player cannot climb */

		// Area
		AreaStopEnemy,						/**< Area that stops enemies */
		AreaFloatUp,						/**< Area that makes the player float up */
		AreaHForce,							/**< Area that applies a horizontal force */
		AreaText,							/**< Area that shows a text */
		AreaEndOfLevel,						/**< Area that ends the level */
		AreaCallback,						/**< Area that invokes a script callback */
		AreaActivateBoss,					/**< Area that activates a boss */
		AreaFlyOff,							/**< Area that disables the airboard */
		AreaRevertMorph,					/**< Area that reverts player morph */
		AreaMorphToFrog,					/**< Area that morphs the player into a frog */
		AreaNoFire,							/**< Area that disables firing */
		AreaWaterBlock,						/**< Area that blocks water */
		AreaWeather,						/**< Area that sets the weather */
		AreaAmbientSound,					/**< Area that plays an ambient sound */
		AreaAmbientBubbles,					/**< Area that emits ambient bubbles */

		// Triggers
		TriggerCrate,						/**< Crate that toggles a trigger */
		TriggerArea,						/**< Area that sets a trigger */
		TriggerZone,						/**< Zone that toggles a trigger */

		// Warp
		WarpCoinBonus,						/**< Warp to a coin bonus */
		WarpOrigin,							/**< Warp origin */
		WarpTarget,							/**< Warp target */

		// Lights
		LightAmbient,						/**< Ambient light */
		LightSteady,						/**< Steady light */
		LightPulse,							/**< Pulsing light */
		LightFlicker,						/**< Flickering light */
		LightIlluminate,					/**< Illuminating light */

		// Environment
		Spring,								/**< Spring */
		Bridge,								/**< Bridge */
		MovingPlatform,						/**< Moving platform */
		PushableBox,						/**< Pushable box */
		Eva,								/**< Eva (kissable character) */
		Pole,								/**< Pole */
		SignEOL,							/**< End-of-level sign */
		Moth,								/**< Moth */
		SteamNote,							/**< Steam note */
		Bomb,								/**< Bomb */
		PinballBumper,						/**< Pinball bumper */
		PinballPaddle,						/**< Pinball paddle */
		CtfBase,							/**< Capture the flag base */
		BirdCage,							/**< Bird cage */
		Stopwatch,							/**< Stopwatch */
		SpikeBall,							/**< Spike ball */
		AirboardGenerator,					/**< Airboard generator */
		Copter,								/**< Copter */
		RollingRock,						/**< Rolling rock */
		RollingRockTrigger,					/**< Rolling rock trigger */
		SwingingVine,						/**< Swinging vine */

		// Enemies
		EnemyTurtle,						/**< Turtle enemy */
		EnemyLizard,						/**< Lizard enemy */
		EnemySucker,						/**< Sucker enemy */
		EnemySuckerFloat,					/**< Floating sucker enemy */
		EnemyLabRat,						/**< Lab rat enemy */
		EnemyHelmut,						/**< Helmut enemy */
		EnemyDragon,						/**< Dragon enemy */
		EnemyBat,							/**< Bat enemy */
		EnemyFatChick,						/**< Fat chick enemy */
		EnemyFencer,						/**< Fencer enemy */
		EnemyRapier,						/**< Rapier enemy */
		EnemySparks,						/**< Sparks enemy */
		EnemyMonkey,						/**< Monkey enemy */
		EnemyDemon,							/**< Demon enemy */
		EnemyBee,							/**< Bee enemy */
		EnemyBeeSwarm,						/**< Bee swarm enemy */
		EnemyCaterpillar,					/**< Caterpillar enemy */
		EnemyCrab,							/**< Crab enemy */
		EnemyDoggy,							/**< Doggy enemy */
		EnemyDragonfly,						/**< Dragonfly enemy */
		EnemyFish,							/**< Fish enemy */
		EnemyMadderHatter,					/**< Madder Hatter enemy */
		EnemyRaven,							/**< Raven enemy */
		EnemySkeleton,						/**< Skeleton enemy */
		EnemyTurtleTough,					/**< Tough turtle enemy */
		EnemyTurtleTube,					/**< Tube turtle enemy */
		EnemyWitch,							/**< Witch enemy */
		EnemyLizardFloat,					/**< Floating lizard enemy */

		BossTweedle,						/**< Tweedle boss */
		BossBilsy,							/**< Bilsy boss */
		BossDevan,							/**< Devan boss */
		BossQueen,							/**< Queen boss */
		BossRobot,							/**< Robot boss */
		BossUterus,							/**< Uterus boss */
		BossTurtleTough,					/**< Tough turtle boss */
		BossBubba,							/**< Bubba boss */
		BossDevanRemote,					/**< Devan remote boss */
		BossBolly,							/**< Bolly boss */

		TurtleShell,						/**< Turtle shell */

		// Collectibles
		Coin,								/**< Coin */
		Gem,								/**< Gem */
		GemGiant,							/**< Giant gem */
		GemRing,							/**< Gem ring */
		GemStomp,							/**< Gem released by buttstomp */
		Carrot,								/**< Carrot (restores health) */
		CarrotFly,							/**< Flying carrot (grants copter) */
		CarrotInvincible,					/**< Invincibility carrot */
		OneUp,								/**< Extra life */
		FastFire,							/**< Fast fire power-up */
		Food,								/**< Food (contributes to sugar rush) */
		Ammo,								/**< Weapon ammo */
		PowerUpWeapon,						/**< Weapon power-up */

		// Containers
		Crate,								/**< Crate */
		Barrel,								/**< Barrel */
		CrateAmmo,							/**< Crate containing ammo */
		BarrelAmmo,							/**< Barrel containing ammo */
		CrateGem,							/**< Crate containing gems */
		BarrelGem,							/**< Barrel containing gems */
		PowerUpMorph,						/**< Morph power-up monitor */
		PowerUpShield,						/**< Shield power-up monitor */

		RaceCheckpoint,						/**< Ordered race-track waypoint (passive minimap/positioning marker, JJ2+ Text event with TextID >= 100) */

		// End of enumeration
		Count,								/**< Number of well-known events */

		Generator = 0x1000,					/**< Generator (spawns another event repeatedly) */

		// Multiplayer-only remotable actors
		WeaponBlaster = 0x2001,				/**< Remotable Blaster projectile */
		WeaponBouncer = 0x2002,				/**< Remotable Bouncer projectile */
		WeaponElectro = 0x2003,				/**< Remotable Electro projectile */
		WeaponFreezer = 0x2004,				/**< Remotable Freezer projectile */
		WeaponPepper = 0x2005,				/**< Remotable Pepper projectile */
		WeaponRF = 0x2006,					/**< Remotable RF projectile */
		WeaponSeeker = 0x2007,				/**< Remotable Seeker projectile */
		WeaponThunderbolt = 0x2008,			/**< Remotable Thunderbolt projectile */
		WeaponTNT = 0x2009,					/**< Remotable TNT */
		WeaponToaster = 0x200A,				/**< Remotable Toaster projectile */
	};
}