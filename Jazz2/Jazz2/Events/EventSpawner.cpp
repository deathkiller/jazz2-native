#include "EventSpawner.h"

#include "../Actors/Environment/AirboardGenerator.h"
#include "../Actors/Environment/AmbientSound.h"
#include "../Actors/Environment/BirdCage.h"
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
#include "../Actors/Collectibles/GemGiant.h"
#include "../Actors/Collectibles/OneUpCollectible.h"

#include "../Actors/Enemies/Bat.h"
#include "../Actors/Enemies/Bee.h"
#include "../Actors/Enemies/Bubba.h"
#include "../Actors/Enemies/Caterpillar.h"
#include "../Actors/Enemies/Crab.h"
#include "../Actors/Enemies/Demon.h"
#include "../Actors/Enemies/Doggy.h"
#include "../Actors/Enemies/Dragonfly.h"
#include "../Actors/Enemies/FatChick.h"
#include "../Actors/Enemies/Fencer.h"
#include "../Actors/Enemies/Helmut.h"
#include "../Actors/Enemies/LabRat.h"
#include "../Actors/Enemies/Lizard.h"
#include "../Actors/Enemies/MadderHatter.h"
#include "../Actors/Enemies/Queen.h"
#include "../Actors/Enemies/Raven.h"
#include "../Actors/Enemies/Sucker.h"
#include "../Actors/Enemies/SuckerFloat.h"
#include "../Actors/Enemies/Turtle.h"
#include "../Actors/Enemies/TurtleShell.h"
#include "../Actors/Enemies/TurtleTough.h"
#include "../Actors/Enemies/TurtleTube.h"
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
#include "../Actors/Solid/PowerUpShieldMonitor.h"
#include "../Actors/Solid/PowerUpWeaponMonitor.h"
#include "../Actors/Solid/PushableBox.h"
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
		RegisterSpawnable(EventType::SpikeBall, SpikeBall.Create, SpikeBall.Preload);*/
		RegisterSpawnable<Solid::PushableBox>(EventType::PushableBox);
		RegisterSpawnable<Environment::Eva>(EventType::Eva);
		RegisterSpawnable<Environment::EndOfLevel>(EventType::SignEOL);
		RegisterSpawnable<Environment::Moth>(EventType::Moth);
		RegisterSpawnable<Environment::SteamNote>(EventType::SteamNote);
		RegisterSpawnable<Environment::Bomb>(EventType::Bomb);
		/*RegisterSpawnable(EventType::PinballBumper, PinballBumper.Create, PinballBumper.Preload);
		RegisterSpawnable(EventType::PinballPaddle, PinballPaddle.Create, PinballPaddle.Preload);*/

		// Enemies
		RegisterSpawnable<Enemies::Turtle>(EventType::EnemyTurtle);
		RegisterSpawnable<Enemies::Lizard>(EventType::EnemyLizard);
		//RegisterSpawnable(EventType::EnemyLizardFloat, LizardFloat.Create, LizardFloat.Preload);
		//RegisterSpawnable(EventType::EnemyDragon, Dragon.Create, Dragon.Preload);
		RegisterSpawnable<Enemies::SuckerFloat>(EventType::EnemySuckerFloat);
		RegisterSpawnable<Enemies::Sucker>(EventType::EnemySucker);
		RegisterSpawnable<Enemies::LabRat>(EventType::EnemyLabRat);
		RegisterSpawnable<Enemies::Helmut>(EventType::EnemyHelmut);
		RegisterSpawnable<Enemies::Bat>(EventType::EnemyBat);
		RegisterSpawnable<Enemies::FatChick>(EventType::EnemyFatChick);
		RegisterSpawnable<Enemies::Fencer>(EventType::EnemyFencer);
		//RegisterSpawnable(EventType::EnemyRapier, Rapier.Create, Rapier.Preload);
		//RegisterSpawnable(EventType::EnemySparks, Sparks.Create, Sparks.Preload);
		//RegisterSpawnable(EventType::EnemyMonkey, Monkey.Create, Monkey.Preload);
		RegisterSpawnable<Enemies::Demon>(EventType::EnemyDemon);
		RegisterSpawnable<Enemies::Bee>(EventType::EnemyBee);
		//RegisterSpawnable(EventType::EnemyBeeSwarm, BeeSwarm.Create, BeeSwarm.Preload);
		RegisterSpawnable<Enemies::Caterpillar>(EventType::EnemyCaterpillar);
		RegisterSpawnable<Enemies::Crab>(EventType::EnemyCrab);
		RegisterSpawnable<Enemies::Doggy>(EventType::EnemyDoggy);
		RegisterSpawnable<Enemies::Dragonfly>(EventType::EnemyDragonfly);
		//RegisterSpawnable(EventType::EnemyFish, Fish.Create, Fish.Preload);
		RegisterSpawnable<Enemies::MadderHatter>(EventType::EnemyMadderHatter);
		RegisterSpawnable<Enemies::Raven>(EventType::EnemyRaven);
		//RegisterSpawnable(EventType::EnemySkeleton, Skeleton.Create, Skeleton.Preload);
		RegisterSpawnable<Enemies::TurtleTough>(EventType::EnemyTurtleTough);
		RegisterSpawnable<Enemies::TurtleTube>(EventType::EnemyTurtleTube);
		RegisterSpawnable<Enemies::Witch>(EventType::EnemyWitch);

		RegisterSpawnable<Enemies::TurtleShell>(EventType::TurtleShell);

		/*RegisterSpawnable(EventType::BossBilsy, Bilsy.Create, Bilsy.Preload);
		RegisterSpawnable(EventType::BossDevan, Devan.Create, Devan.Preload);
		RegisterSpawnable(EventType::BossDevanRemote, DevanRemote.Create, DevanRemote.Preload);*/
		RegisterSpawnable<Enemies::Queen>(EventType::BossQueen);
		/*RegisterSpawnable(EventType::BossRobot, Robot.Create, Robot.Preload);
		RegisterSpawnable(EventType::BossTweedle, Tweedle.Create, Tweedle.Preload);
		RegisterSpawnable(EventType::BossUterus, Uterus.Create, Uterus.Preload);
		RegisterSpawnable(EventType::BossTurtleTough, TurtleToughBoss.Create, TurtleToughBoss.Preload);*/
		RegisterSpawnable<Enemies::Bubba>(EventType::BossBubba);
		//RegisterSpawnable(EventType::BossBolly, Bolly.Create, Bolly.Preload);

		// Collectibles
		RegisterSpawnable<Collectibles::GemCollectible>(EventType::Gem);
		RegisterSpawnable<Collectibles::GemGiant>(EventType::GemGiant);
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
		//RegisterSpawnable(EventType::GemRing, GemRing.Create, GemRing.Preload);

		RegisterSpawnable<Solid::PowerUpMorphMonitor>(EventType::PowerUpMorph);
		RegisterSpawnable<Environment::BirdCage>(EventType::BirdCage);

		RegisterSpawnable<Environment::AirboardGenerator>(EventType::AirboardGenerator);
		//RegisterSpawnable(EventType::Copter, Copter.Create, Copter.Preload);

		/*RegisterSpawnable(EventType::RollingRock, RollingRock.Create, RollingRock.Preload);
		RegisterSpawnable(EventType::SwingingVine, SwingingVine.Create, SwingingVine.Preload);*/

		RegisterSpawnable<Solid::PowerUpShieldMonitor>(EventType::PowerUpShield);
		//RegisterSpawnable(EventType::Stopwatch, Stopwatch.Create, Stopwatch.Preload);*/

		RegisterSpawnable<Collectibles::AmmoCollectible>(EventType::Ammo);
		RegisterSpawnable<Solid::PowerUpWeaponMonitor>(EventType::PowerUpWeapon);
		RegisterSpawnable<Collectibles::FoodCollectible>(EventType::Food);
	}

	void EventSpawner::RegisterSpawnable(EventType type, CreateDelegate create, PreloadDelegate preload)
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

	std::shared_ptr<ActorBase> EventSpawner::SpawnEvent(EventType type, uint8_t* spawnParams, ActorState flags, int x, int y, int z)
	{
		return SpawnEvent(type, spawnParams, flags, Vector3i(x * 32 + 16, y * 32 + 16, (int)z));
	}

	std::shared_ptr<ActorBase> EventSpawner::SpawnEvent(EventType type, uint8_t* spawnParams, ActorState flags, const Vector3i& pos)
	{
		auto it = _spawnableEvents.find(type);
		if (it == _spawnableEvents.end() || it->second.CreateFunction == nullptr) {
			return nullptr;
		}

		ActorActivationDetails details;
		details.LevelHandler = _levelHandler;
		details.Params = spawnParams;
		details.Pos = pos;
		details.State = flags;
		return it->second.CreateFunction(details);
	}

}