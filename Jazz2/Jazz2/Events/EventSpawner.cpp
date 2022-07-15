#include "EventSpawner.h"

#include "../Actors/Environment/BonusWarp.h"
#include "../Actors/Environment/Checkpoint.h"
#include "../Actors/Environment/Spring.h"

#include "../Actors/Collectibles/AmmoCollectible.h"
#include "../Actors/Collectibles/CoinCollectible.h"
#include "../Actors/Collectibles/FoodCollectible.h"

#include "../Actors/Enemies/Bat.h"
#include "../Actors/Enemies/Turtle.h"

using namespace Jazz2::Actors;

namespace Jazz2::Events
{
	EventSpawner::EventSpawner(ILevelHandler* levelHandler)
		:
		_levelHandler(levelHandler)
	{
		RegisterKnownSpawnables();
	}

	void EventSpawner::RegisterKnownSpawnables()
	{
		// Basic
		RegisterSpawnable<Environment::Checkpoint>(EventType::Checkpoint);

		// Area
		/*RegisterSpawnable(EventType::AreaAmbientSound, AmbientSound.Create, AmbientSound.Preload);
		RegisterSpawnable(EventType::AreaAmbientBubbles, AmbientBubbles.Create, AmbientBubbles.Preload);

		// Triggers
		RegisterSpawnable(EventType::TriggerCrate, TriggerCrate.Create, TriggerCrate.Preload);*/

		// Warp
		RegisterSpawnable<Environment::BonusWarp>(EventType::WarpCoinBonus);

		// Lights
		/*RegisterSpawnable(EventType::LightSteady, StaticRadialLight.Create);
		RegisterSpawnable(EventType::LightPulse, PulsatingRadialLight.Create);
		RegisterSpawnable(EventType::LightFlicker, FlickerLight.Create);
		RegisterSpawnable(EventType::LightIlluminate, IlluminateLight.Create);*/

		// Environment
		RegisterSpawnable<Environment::Spring>(EventType::Spring);
		/*RegisterSpawnable(EventType::Bridge, Bridge.Create, Bridge.Preload);
		RegisterSpawnable(EventType::MovingPlatform, MovingPlatform.Create, MovingPlatform.Preload);
		RegisterSpawnable(EventType::SpikeBall, SpikeBall.Create, SpikeBall.Preload);
		RegisterSpawnable(EventType::PushableBox, PushBox.Create, PushBox.Preload);
		RegisterSpawnable(EventType::Eva, Eva.Create, Eva.Preload);
		RegisterSpawnable(EventType::Pole, Pole.Create, Pole.Preload);
		RegisterSpawnable(EventType::SignEOL, SignEol.Create, SignEol.Preload);
		RegisterSpawnable(EventType::Moth, Moth.Create, Moth.Preload);
		RegisterSpawnable(EventType::SteamNote, SteamNote.Create, SteamNote.Preload);
		RegisterSpawnable(EventType::Bomb, Bomb.Create, Bomb.Preload);
		RegisterSpawnable(EventType::PinballBumper, PinballBumper.Create, PinballBumper.Preload);
		RegisterSpawnable(EventType::PinballPaddle, PinballPaddle.Create, PinballPaddle.Preload);*/

		// Enemies
		RegisterSpawnable<Enemies::Turtle>(EventType::EnemyTurtle);
		/*RegisterSpawnable(EventType::EnemyLizard, Lizard.Create, Lizard.Preload);
		RegisterSpawnable(EventType::EnemyLizardFloat, LizardFloat.Create, LizardFloat.Preload);
		RegisterSpawnable(EventType::EnemyDragon, Dragon.Create, Dragon.Preload);
		RegisterSpawnable(EventType::EnemySuckerFloat, SuckerFloat.Create, SuckerFloat.Preload);
		RegisterSpawnable(EventType::EnemySucker, Sucker.Create, Sucker.Preload);
		RegisterSpawnable(EventType::EnemyLabRat, LabRat.Create, LabRat.Preload);
		RegisterSpawnable(EventType::EnemyHelmut, Helmut.Create, Helmut.Preload);*/
		RegisterSpawnable<Enemies::Bat>(EventType::EnemyBat);
		/*RegisterSpawnable(EventType::EnemyFatChick, FatChick.Create, FatChick.Preload);
		RegisterSpawnable(EventType::EnemyFencer, Fencer.Create, Fencer.Preload);
		RegisterSpawnable(EventType::EnemyRapier, Rapier.Create, Rapier.Preload);
		RegisterSpawnable(EventType::EnemySparks, Sparks.Create, Sparks.Preload);
		RegisterSpawnable(EventType::EnemyMonkey, Monkey.Create, Monkey.Preload);
		RegisterSpawnable(EventType::EnemyDemon, Demon.Create, Demon.Preload);
		RegisterSpawnable(EventType::EnemyBee, Bee.Create, Bee.Preload);
		//RegisterSpawnable(EventType::EnemyBeeSwarm, BeeSwarm.Create, BeeSwarm.Preload);
		RegisterSpawnable(EventType::EnemyCaterpillar, Caterpillar.Create, Caterpillar.Preload);
		RegisterSpawnable(EventType::EnemyCrab, Crab.Create, Crab.Preload);
		RegisterSpawnable(EventType::EnemyDoggy, Doggy.Create, Doggy.Preload);
		RegisterSpawnable(EventType::EnemyDragonfly, Dragonfly.Create, Dragonfly.Preload);
		RegisterSpawnable(EventType::EnemyFish, Fish.Create, Fish.Preload);
		RegisterSpawnable(EventType::EnemyMadderHatter, MadderHatter.Create, MadderHatter.Preload);
		RegisterSpawnable(EventType::EnemyRaven, Raven.Create, Raven.Preload);
		RegisterSpawnable(EventType::EnemySkeleton, Skeleton.Create, Skeleton.Preload);
		RegisterSpawnable(EventType::EnemyTurtleTough, TurtleTough.Create, TurtleTough.Preload);
		RegisterSpawnable(EventType::EnemyTurtleTube, TurtleTube.Create, TurtleTube.Preload);
		RegisterSpawnable(EventType::EnemyWitch, Witch.Create, Witch.Preload);

		RegisterSpawnable(EventType::TurtleShell, TurtleShell.Create, TurtleShell.Preload);

		RegisterSpawnable(EventType::BossBilsy, Bilsy.Create, Bilsy.Preload);
		RegisterSpawnable(EventType::BossDevan, Devan.Create, Devan.Preload);
		RegisterSpawnable(EventType::BossDevanRemote, DevanRemote.Create, DevanRemote.Preload);
		RegisterSpawnable(EventType::BossQueen, Queen.Create, Queen.Preload);
		RegisterSpawnable(EventType::BossRobot, Robot.Create, Robot.Preload);
		RegisterSpawnable(EventType::BossTweedle, Tweedle.Create, Tweedle.Preload);
		RegisterSpawnable(EventType::BossUterus, Uterus.Create, Uterus.Preload);
		RegisterSpawnable(EventType::BossTurtleTough, TurtleToughBoss.Create, TurtleToughBoss.Preload);
		RegisterSpawnable(EventType::BossBubba, Bubba.Create, Bubba.Preload);
		RegisterSpawnable(EventType::BossBolly, Bolly.Create, Bolly.Preload);

		// Collectibles
		RegisterSpawnable(EventType::Gem, GemCollectible.Create, GemCollectible.Preload);*/
		RegisterSpawnable<Collectibles::CoinCollectible>(EventType::Coin);
		/*RegisterSpawnable(EventType::Carrot, CarrotCollectible.Create, CarrotCollectible.Preload);
		RegisterSpawnable(EventType::CarrotFly, CarrotFlyCollectible.Create, CarrotFlyCollectible.Preload);
		RegisterSpawnable(EventType::CarrotInvincible, CarrotInvincibleCollectible.Create, CarrotInvincibleCollectible.Preload);
		RegisterSpawnable(EventType::OneUp, OneUpCollectible.Create, OneUpCollectible.Preload);
		RegisterSpawnable(EventType::FastFire, FastFireCollectible.Create, FastFireCollectible.Preload);

		RegisterSpawnable(EventType::CrateAmmo, AmmoCrate.Create, AmmoCrate.Preload);
		RegisterSpawnable(EventType::BarrelAmmo, AmmoBarrel.Create, AmmoBarrel.Preload);
		RegisterSpawnable(EventType::Crate, CrateContainer.Create, CrateContainer.Preload);
		RegisterSpawnable(EventType::Barrel, BarrelContainer.Create, BarrelContainer.Preload);
		RegisterSpawnable(EventType::CrateGem, GemCrate.Create, GemCrate.Preload);
		RegisterSpawnable(EventType::BarrelGem, GemBarrel.Create, GemBarrel.Preload);
		RegisterSpawnable(EventType::GemGiant, GemGiant.Create, GemGiant.Preload);
		RegisterSpawnable(EventType::GemRing, GemRing.Create, GemRing.Preload);

		RegisterSpawnable(EventType::PowerUpMorph, PowerUpMorphMonitor.Create, PowerUpMorphMonitor.Preload);
		RegisterSpawnable(EventType::BirdCage, BirdCage.Create, BirdCage.Preload);

		RegisterSpawnable(EventType::AirboardGenerator, AirboardGenerator.Create, AirboardGenerator.Preload);
		RegisterSpawnable(EventType::Copter, Copter.Create, Copter.Preload);

		RegisterSpawnable(EventType::RollingRock, RollingRock.Create, RollingRock.Preload);
		RegisterSpawnable(EventType::SwingingVine, SwingingVine.Create, SwingingVine.Preload);

		RegisterSpawnable(EventType::PowerUpShield, PowerUpShieldMonitor.Create, PowerUpShieldMonitor.Preload);
		RegisterSpawnable(EventType::Stopwatch, Stopwatch.Create, Stopwatch.Preload);*/

		RegisterSpawnable<Collectibles::AmmoCollectible>(EventType::Ammo);
		//RegisterSpawnable(EventType::PowerUpWeapon, PowerUpWeaponMonitor.Create, PowerUpWeaponMonitor.Preload);
		RegisterSpawnable<Collectibles::FoodCollectible>(EventType::Food);


		// Multiplayer-only remotable actors
		/*RegisterSpawnable(EventType::WeaponBlaster, AmmoBlaster.Create);
		RegisterSpawnable(EventType::WeaponBouncer, AmmoBouncer.Create);
		RegisterSpawnable(EventType::WeaponElectro, AmmoElectro.Create);
		RegisterSpawnable(EventType::WeaponFreezer, AmmoFreezer.Create);
		RegisterSpawnable(EventType::WeaponPepper, AmmoPepper.Create);
		RegisterSpawnable(EventType::WeaponRF, AmmoRF.Create);
		RegisterSpawnable(EventType::WeaponSeeker, AmmoSeeker.Create);
		RegisterSpawnable(EventType::WeaponThunderbolt, AmmoThunderbolt.Create);
		RegisterSpawnable(EventType::WeaponTNT, AmmoTNT.Create);
		RegisterSpawnable(EventType::WeaponToaster, AmmoToaster.Create);*/
	}

	void EventSpawner::RegisterSpawnable(EventType type, CreateFunction create, PreloadFunction preload)
	{
		_spawnableEvents[type] = { create, preload };
	}

	template<typename T>
	void EventSpawner::RegisterSpawnable(EventType type)
	{
		_spawnableEvents[type] = { [](const ActorActivationDetails& details) -> std::shared_ptr<ActorBase> {
			std::shared_ptr<ActorBase> actor = std::make_shared<T>();
			actor->OnActivated(details);
			return actor;
		}, T::Preload };
	}

	void EventSpawner::PreloadEvent(EventType type, uint8_t* spawnParams)
	{
		auto it = _spawnableEvents.find(type);
		if (it == _spawnableEvents.end() || it->second.PreloadFunction == nullptr) {
			return;
		}

		ActorActivationDetails details;
		details.LevelHandler = _levelHandler;
		details.Params = spawnParams;
		it->second.PreloadFunction(details);
	}

	std::shared_ptr<ActorBase> EventSpawner::SpawnEvent(EventType type, uint8_t* spawnParams, ActorFlags flags, int x, int y, int z)
	{
		return SpawnEvent(type, spawnParams, flags, Vector3i(x * 32 + 16, y * 32 + 16, (int)z));
	}

	std::shared_ptr<ActorBase> EventSpawner::SpawnEvent(EventType type, uint8_t* spawnParams, ActorFlags flags, const Vector3i& pos)
	{
		auto it = _spawnableEvents.find(type);
		if (it == _spawnableEvents.end() || it->second.CreateFunction == nullptr) {
			return nullptr;
		}

		ActorActivationDetails details;
		details.LevelHandler = _levelHandler;
		details.Params = spawnParams;
		details.Pos = pos;
		details.Flags = flags;
		return it->second.CreateFunction(details);
	}

}