#include "EventSpawner.h"

#include "../Actors/Environment/AmbientSound.h"
#include "../Actors/Environment/Bomb.h"
#include "../Actors/Environment/BonusWarp.h"
#include "../Actors/Environment/Checkpoint.h"
#include "../Actors/Environment/EndOfLevel.h"
#include "../Actors/Environment/Eva.h"
#include "../Actors/Environment/Moth.h"
#include "../Actors/Environment/Spring.h"
#include "../Actors/Environment/SteamNote.h"

#include "../Actors/Collectibles/AmmoCollectible.h"
#include "../Actors/Collectibles/CoinCollectible.h"
#include "../Actors/Collectibles/FastFireCollectible.h"
#include "../Actors/Collectibles/FoodCollectible.h"
#include "../Actors/Collectibles/CarrotCollectible.h"
#include "../Actors/Collectibles/GemCollectible.h"
#include "../Actors/Collectibles/OneUpCollectible.h"

#include "../Actors/Enemies/Bat.h"
#include "../Actors/Enemies/Bee.h"
#include "../Actors/Enemies/Caterpillar.h"
#include "../Actors/Enemies/Dragonfly.h"
#include "../Actors/Enemies/LabRat.h"
#include "../Actors/Enemies/MadderHatter.h"
#include "../Actors/Enemies/Sucker.h"
#include "../Actors/Enemies/SuckerFloat.h"
#include "../Actors/Enemies/Turtle.h"
#include "../Actors/Enemies/TurtleShell.h"
#include "../Actors/Enemies/Witch.h"

#include "../Actors/Lighting/FlickerLight.h"
#include "../Actors/Lighting/PulsatingRadialLight.h"
#include "../Actors/Lighting/StaticRadialLight.h"

#include "../Actors/Solid/AmmoBarrel.h"
#include "../Actors/Solid/AmmoCrate.h"
#include "../Actors/Solid/BarrelContainer.h"
#include "../Actors/Solid/Bridge.h"
#include "../Actors/Solid/CrateContainer.h"
#include "../Actors/Solid/GemBarrel.h"
#include "../Actors/Solid/GemCrate.h"
#include "../Actors/Solid/Pole.h"
#include "../Actors/Solid/PowerUpMorphMonitor.h"
#include "../Actors/Solid/PowerUpWeaponMonitor.h"
#include "../Actors/Solid/TriggerCrate.h"

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
		RegisterSpawnable<Environment::AmbientSound>(EventType::AreaAmbientSound);
		//RegisterSpawnable(EventType::AreaAmbientBubbles, AmbientBubbles.Create, AmbientBubbles.Preload);

		// Triggers
		RegisterSpawnable<Solid::TriggerCrate>(EventType::TriggerCrate);

		// Warp
		RegisterSpawnable<Environment::BonusWarp>(EventType::WarpCoinBonus);

		// Lights
		RegisterSpawnable<Lighting::StaticRadialLight>(EventType::LightSteady);
		RegisterSpawnable<Lighting::PulsatingRadialLight>(EventType::LightPulse);
		RegisterSpawnable<Lighting::FlickerLight>(EventType::LightFlicker);
		//RegisterSpawnable(EventType::LightIlluminate, IlluminateLight.Create);

		// Environment
		RegisterSpawnable<Environment::Spring>(EventType::Spring);
		RegisterSpawnable<Solid::Bridge>(EventType::Bridge);
		RegisterSpawnable<Solid::Pole>(EventType::Pole);
		/*RegisterSpawnable(EventType::MovingPlatform, MovingPlatform.Create, MovingPlatform.Preload);
		RegisterSpawnable(EventType::SpikeBall, SpikeBall.Create, SpikeBall.Preload);
		RegisterSpawnable(EventType::PushableBox, PushBox.Create, PushBox.Preload);*/
		RegisterSpawnable<Environment::Eva>(EventType::Eva);
		RegisterSpawnable<Environment::EndOfLevel>(EventType::SignEOL);
		RegisterSpawnable<Environment::Moth>(EventType::Moth);
		RegisterSpawnable<Environment::SteamNote>(EventType::SteamNote);
		RegisterSpawnable<Environment::Bomb>(EventType::Bomb);
		/*RegisterSpawnable(EventType::PinballBumper, PinballBumper.Create, PinballBumper.Preload);
		RegisterSpawnable(EventType::PinballPaddle, PinballPaddle.Create, PinballPaddle.Preload);*/

		// Enemies
		RegisterSpawnable<Enemies::Turtle>(EventType::EnemyTurtle);
		/*RegisterSpawnable(EventType::EnemyLizard, Lizard.Create, Lizard.Preload);
		RegisterSpawnable(EventType::EnemyLizardFloat, LizardFloat.Create, LizardFloat.Preload);
		RegisterSpawnable(EventType::EnemyDragon, Dragon.Create, Dragon.Preload);*/
		RegisterSpawnable<Enemies::SuckerFloat>(EventType::EnemySuckerFloat);
		RegisterSpawnable<Enemies::Sucker>(EventType::EnemySucker);
		RegisterSpawnable<Enemies::LabRat>(EventType::EnemyLabRat);
		//RegisterSpawnable(EventType::EnemyHelmut, Helmut.Create, Helmut.Preload);
		RegisterSpawnable<Enemies::Bat>(EventType::EnemyBat);
		/*RegisterSpawnable(EventType::EnemyFatChick, FatChick.Create, FatChick.Preload);
		RegisterSpawnable(EventType::EnemyFencer, Fencer.Create, Fencer.Preload);
		RegisterSpawnable(EventType::EnemyRapier, Rapier.Create, Rapier.Preload);
		RegisterSpawnable(EventType::EnemySparks, Sparks.Create, Sparks.Preload);
		RegisterSpawnable(EventType::EnemyMonkey, Monkey.Create, Monkey.Preload);
		RegisterSpawnable(EventType::EnemyDemon, Demon.Create, Demon.Preload);*/
		RegisterSpawnable<Enemies::Bee>(EventType::EnemyBee);
		//RegisterSpawnable(EventType::EnemyBeeSwarm, BeeSwarm.Create, BeeSwarm.Preload);
		RegisterSpawnable<Enemies::Caterpillar>(EventType::EnemyCaterpillar);
		/*RegisterSpawnable(EventType::EnemyCrab, Crab.Create, Crab.Preload);
		RegisterSpawnable(EventType::EnemyDoggy, Doggy.Create, Doggy.Preload);*/
		RegisterSpawnable<Enemies::Dragonfly>(EventType::EnemyDragonfly);
		//RegisterSpawnable(EventType::EnemyFish, Fish.Create, Fish.Preload);
		RegisterSpawnable<Enemies::MadderHatter>(EventType::EnemyMadderHatter);
		/*RegisterSpawnable(EventType::EnemyRaven, Raven.Create, Raven.Preload);
		RegisterSpawnable(EventType::EnemySkeleton, Skeleton.Create, Skeleton.Preload);
		RegisterSpawnable(EventType::EnemyTurtleTough, TurtleTough.Create, TurtleTough.Preload);
		RegisterSpawnable(EventType::EnemyTurtleTube, TurtleTube.Create, TurtleTube.Preload);*/
		RegisterSpawnable<Enemies::Witch>(EventType::EnemyWitch);

		RegisterSpawnable<Enemies::TurtleShell>(EventType::TurtleShell);

		/*RegisterSpawnable(EventType::BossBilsy, Bilsy.Create, Bilsy.Preload);
		RegisterSpawnable(EventType::BossDevan, Devan.Create, Devan.Preload);
		RegisterSpawnable(EventType::BossDevanRemote, DevanRemote.Create, DevanRemote.Preload);
		RegisterSpawnable(EventType::BossQueen, Queen.Create, Queen.Preload);
		RegisterSpawnable(EventType::BossRobot, Robot.Create, Robot.Preload);
		RegisterSpawnable(EventType::BossTweedle, Tweedle.Create, Tweedle.Preload);
		RegisterSpawnable(EventType::BossUterus, Uterus.Create, Uterus.Preload);
		RegisterSpawnable(EventType::BossTurtleTough, TurtleToughBoss.Create, TurtleToughBoss.Preload);
		RegisterSpawnable(EventType::BossBubba, Bubba.Create, Bubba.Preload);
		RegisterSpawnable(EventType::BossBolly, Bolly.Create, Bolly.Preload);*/

		// Collectibles
		RegisterSpawnable<Collectibles::GemCollectible>(EventType::Gem);
		RegisterSpawnable<Collectibles::CoinCollectible>(EventType::Coin);
		RegisterSpawnable<Collectibles::CarrotCollectible>(EventType::Carrot);
		/*RegisterSpawnable(EventType::CarrotFly, CarrotFlyCollectible.Create, CarrotFlyCollectible.Preload);
		RegisterSpawnable(EventType::CarrotInvincible, CarrotInvincibleCollectible.Create, CarrotInvincibleCollectible.Preload);*/
		RegisterSpawnable<Collectibles::OneUpCollectible>(EventType::OneUp);
		RegisterSpawnable<Collectibles::FastFireCollectible>(EventType::FastFire);

		RegisterSpawnable<Solid::AmmoCrate>(EventType::CrateAmmo);
		RegisterSpawnable<Solid::AmmoBarrel>(EventType::BarrelAmmo);
		RegisterSpawnable<Solid::CrateContainer>(EventType::Crate);
		RegisterSpawnable<Solid::BarrelContainer>(EventType::Barrel);
		RegisterSpawnable<Solid::GemCrate>(EventType::CrateGem);
		RegisterSpawnable<Solid::GemBarrel>(EventType::BarrelGem);
		/*RegisterSpawnable(EventType::GemGiant, GemGiant.Create, GemGiant.Preload);
		RegisterSpawnable(EventType::GemRing, GemRing.Create, GemRing.Preload);*/

		RegisterSpawnable<Solid::PowerUpMorphMonitor>(EventType::PowerUpMorph);
		/*RegisterSpawnable(EventType::BirdCage, BirdCage.Create, BirdCage.Preload);

		RegisterSpawnable(EventType::AirboardGenerator, AirboardGenerator.Create, AirboardGenerator.Preload);
		RegisterSpawnable(EventType::Copter, Copter.Create, Copter.Preload);

		RegisterSpawnable(EventType::RollingRock, RollingRock.Create, RollingRock.Preload);
		RegisterSpawnable(EventType::SwingingVine, SwingingVine.Create, SwingingVine.Preload);

		RegisterSpawnable(EventType::PowerUpShield, PowerUpShieldMonitor.Create, PowerUpShieldMonitor.Preload);
		RegisterSpawnable(EventType::Stopwatch, Stopwatch.Create, Stopwatch.Preload);*/

		RegisterSpawnable<Collectibles::AmmoCollectible>(EventType::Ammo);
		RegisterSpawnable<Solid::PowerUpWeaponMonitor>(EventType::PowerUpWeapon);
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