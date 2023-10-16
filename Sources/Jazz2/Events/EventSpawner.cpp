#include "EventSpawner.h"

#include "../Actors/Collectibles/AmmoCollectible.h"
#include "../Actors/Collectibles/CarrotCollectible.h"
#include "../Actors/Collectibles/CarrotFlyCollectible.h"
#include "../Actors/Collectibles/CarrotInvincibleCollectible.h"
#include "../Actors/Collectibles/CoinCollectible.h"
#include "../Actors/Collectibles/FastFireCollectible.h"
#include "../Actors/Collectibles/FoodCollectible.h"
#include "../Actors/Collectibles/GemCollectible.h"
#include "../Actors/Collectibles/GemGiant.h"
#include "../Actors/Collectibles/GemRing.h"
#include "../Actors/Collectibles/OneUpCollectible.h"
#include "../Actors/Collectibles/Stopwatch.h"

#include "../Actors/Enemies/Bat.h"
#include "../Actors/Enemies/Bee.h"
#include "../Actors/Enemies/Caterpillar.h"
#include "../Actors/Enemies/Crab.h"
#include "../Actors/Enemies/Demon.h"
#include "../Actors/Enemies/Doggy.h"
#include "../Actors/Enemies/Dragon.h"
#include "../Actors/Enemies/Dragonfly.h"
#include "../Actors/Enemies/FatChick.h"
#include "../Actors/Enemies/Fencer.h"
#include "../Actors/Enemies/Fish.h"
#include "../Actors/Enemies/Helmut.h"
#include "../Actors/Enemies/LabRat.h"
#include "../Actors/Enemies/Lizard.h"
#include "../Actors/Enemies/LizardFloat.h"
#include "../Actors/Enemies/MadderHatter.h"
#include "../Actors/Enemies/Monkey.h"

#include "../Actors/Enemies/Rapier.h"
#include "../Actors/Enemies/Raven.h"
#include "../Actors/Enemies/Skeleton.h"
#include "../Actors/Enemies/Sparks.h"
#include "../Actors/Enemies/Sucker.h"
#include "../Actors/Enemies/SuckerFloat.h"
#include "../Actors/Enemies/Turtle.h"
#include "../Actors/Enemies/TurtleShell.h"
#include "../Actors/Enemies/TurtleTough.h"
#include "../Actors/Enemies/TurtleTube.h"
#include "../Actors/Enemies/Witch.h"

#include "../Actors/Enemies/Bosses/Bilsy.h"
#include "../Actors/Enemies/Bosses/Bolly.h"
#include "../Actors/Enemies/Bosses/Bubba.h"
#include "../Actors/Enemies/Bosses/Devan.h"
#include "../Actors/Enemies/Bosses/DevanRemote.h"
#include "../Actors/Enemies/Bosses/Queen.h"
#include "../Actors/Enemies/Bosses/Robot.h"
#include "../Actors/Enemies/Bosses/TurtleBoss.h"
#include "../Actors/Enemies/Bosses/Uterus.h"

#include "../Actors/Environment/AirboardGenerator.h"
#include "../Actors/Environment/AmbientBubbles.h"
#include "../Actors/Environment/AmbientSound.h"
#include "../Actors/Environment/BirdCage.h"
#include "../Actors/Environment/Bomb.h"
#include "../Actors/Environment/BonusWarp.h"
#include "../Actors/Environment/Checkpoint.h"
#include "../Actors/Environment/Copter.h"
#include "../Actors/Environment/EndOfLevel.h"
#include "../Actors/Environment/Eva.h"
#include "../Actors/Environment/Moth.h"
#include "../Actors/Environment/RollingRock.h"
#include "../Actors/Environment/Spring.h"
#include "../Actors/Environment/SteamNote.h"
#include "../Actors/Environment/SwingingVine.h"

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
#include "../Actors/Solid/MovingPlatform.h"
#include "../Actors/Solid/PinballBumper.h"
#include "../Actors/Solid/PinballPaddle.h"
#include "../Actors/Solid/Pole.h"
#include "../Actors/Solid/PowerUpMorphMonitor.h"
#include "../Actors/Solid/PowerUpShieldMonitor.h"
#include "../Actors/Solid/PowerUpWeaponMonitor.h"
#include "../Actors/Solid/PushableBox.h"
#include "../Actors/Solid/SpikeBall.h"
#include "../Actors/Solid/TriggerCrate.h"

using namespace Jazz2::Actors;

namespace Jazz2::Events
{
	EventSpawner::EventSpawner(ILevelHandler* levelHandler)
		: _levelHandler(levelHandler)
	{
		RegisterKnownSpawnables();
	}

	void EventSpawner::RegisterKnownSpawnables()
	{
		// Basic
		RegisterSpawnable<Environment::Checkpoint>(EventType::Checkpoint);

		// Area
		RegisterSpawnable<Environment::AmbientSound>(EventType::AreaAmbientSound);
		RegisterSpawnable<Environment::AmbientBubbles>(EventType::AreaAmbientBubbles);

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
		RegisterSpawnable<Solid::MovingPlatform>(EventType::MovingPlatform);
		RegisterSpawnable<Solid::SpikeBall>(EventType::SpikeBall);
		RegisterSpawnable<Solid::PushableBox>(EventType::PushableBox);
		RegisterSpawnable<Environment::Eva>(EventType::Eva);
		RegisterSpawnable<Environment::EndOfLevel>(EventType::SignEOL);
		RegisterSpawnable<Environment::Moth>(EventType::Moth);
		RegisterSpawnable<Environment::SteamNote>(EventType::SteamNote);
		RegisterSpawnable<Environment::Bomb>(EventType::Bomb);
		RegisterSpawnable<Solid::PinballBumper>(EventType::PinballBumper);
		RegisterSpawnable<Solid::PinballPaddle>(EventType::PinballPaddle);
		RegisterSpawnable<Environment::AirboardGenerator>(EventType::AirboardGenerator);
		RegisterSpawnable<Environment::Copter>(EventType::Copter);
		RegisterSpawnable<Environment::RollingRock>(EventType::RollingRock);
		RegisterSpawnable<Environment::SwingingVine>(EventType::SwingingVine);

		// Enemies
		RegisterSpawnable<Enemies::Turtle>(EventType::EnemyTurtle);
		RegisterSpawnable<Enemies::Lizard>(EventType::EnemyLizard);
		RegisterSpawnable<Enemies::LizardFloat>(EventType::EnemyLizardFloat);
		RegisterSpawnable<Enemies::Dragon>(EventType::EnemyDragon);
		RegisterSpawnable<Enemies::SuckerFloat>(EventType::EnemySuckerFloat);
		RegisterSpawnable<Enemies::Sucker>(EventType::EnemySucker);
		RegisterSpawnable<Enemies::LabRat>(EventType::EnemyLabRat);
		RegisterSpawnable<Enemies::Helmut>(EventType::EnemyHelmut);
		RegisterSpawnable<Enemies::Bat>(EventType::EnemyBat);
		RegisterSpawnable<Enemies::FatChick>(EventType::EnemyFatChick);
		RegisterSpawnable<Enemies::Fencer>(EventType::EnemyFencer);
		RegisterSpawnable<Enemies::Rapier>(EventType::EnemyRapier);
		RegisterSpawnable<Enemies::Sparks>(EventType::EnemySparks);
		RegisterSpawnable<Enemies::Monkey>(EventType::EnemyMonkey);
		RegisterSpawnable<Enemies::Demon>(EventType::EnemyDemon);
		RegisterSpawnable<Enemies::Bee>(EventType::EnemyBee);
		//RegisterSpawnable(EventType::EnemyBeeSwarm, BeeSwarm.Create, BeeSwarm.Preload);
		RegisterSpawnable<Enemies::Caterpillar>(EventType::EnemyCaterpillar);
		RegisterSpawnable<Enemies::Crab>(EventType::EnemyCrab);
		RegisterSpawnable<Enemies::Doggy>(EventType::EnemyDoggy);
		RegisterSpawnable<Enemies::Dragonfly>(EventType::EnemyDragonfly);
		RegisterSpawnable<Enemies::Fish>(EventType::EnemyFish);
		RegisterSpawnable<Enemies::MadderHatter>(EventType::EnemyMadderHatter);
		RegisterSpawnable<Enemies::Raven>(EventType::EnemyRaven);
		RegisterSpawnable<Enemies::Skeleton>(EventType::EnemySkeleton);
		RegisterSpawnable<Enemies::TurtleTough>(EventType::EnemyTurtleTough);
		RegisterSpawnable<Enemies::TurtleTube>(EventType::EnemyTurtleTube);
		RegisterSpawnable<Enemies::Witch>(EventType::EnemyWitch);
		RegisterSpawnable<Enemies::TurtleShell>(EventType::TurtleShell);

		RegisterSpawnable<Bosses::Bilsy>(EventType::BossBilsy);
		RegisterSpawnable<Bosses::Devan>(EventType::BossDevan);
		RegisterSpawnable<Bosses::DevanRemote>(EventType::BossDevanRemote);
		RegisterSpawnable<Bosses::Queen>(EventType::BossQueen);
		RegisterSpawnable<Bosses::Robot>(EventType::BossRobot);
		//RegisterSpawnable(EventType::BossTweedle, Tweedle.Create, Tweedle.Preload);
		RegisterSpawnable<Bosses::Uterus>(EventType::BossUterus);
		RegisterSpawnable<Bosses::TurtleBoss>(EventType::BossTurtleTough);
		RegisterSpawnable<Bosses::Bubba>(EventType::BossBubba);
		RegisterSpawnable<Bosses::Bolly>(EventType::BossBolly);

		// Collectibles
		RegisterSpawnable<Collectibles::AmmoCollectible>(EventType::Ammo);
		RegisterSpawnable<Collectibles::FoodCollectible>(EventType::Food);
		RegisterSpawnable<Collectibles::GemCollectible>(EventType::Gem);
		RegisterSpawnable<Collectibles::GemGiant>(EventType::GemGiant);
		RegisterSpawnable<Collectibles::GemRing>(EventType::GemRing);
		RegisterSpawnable<Collectibles::CoinCollectible>(EventType::Coin);
		RegisterSpawnable<Collectibles::CarrotCollectible>(EventType::Carrot);
		RegisterSpawnable<Collectibles::CarrotFlyCollectible>(EventType::CarrotFly);
		RegisterSpawnable<Collectibles::CarrotInvincibleCollectible>(EventType::CarrotInvincible);
		RegisterSpawnable<Collectibles::OneUpCollectible>(EventType::OneUp);
		RegisterSpawnable<Collectibles::FastFireCollectible>(EventType::FastFire);
		RegisterSpawnable<Collectibles::Stopwatch>(EventType::Stopwatch);

		RegisterSpawnable<Solid::AmmoCrate>(EventType::CrateAmmo);
		RegisterSpawnable<Solid::AmmoBarrel>(EventType::BarrelAmmo);
		RegisterSpawnable<Solid::CrateContainer>(EventType::Crate);
		RegisterSpawnable<Solid::BarrelContainer>(EventType::Barrel);
		RegisterSpawnable<Solid::GemCrate>(EventType::CrateGem);
		RegisterSpawnable<Solid::GemBarrel>(EventType::BarrelGem);

		RegisterSpawnable<Solid::PowerUpMorphMonitor>(EventType::PowerUpMorph);
		RegisterSpawnable<Solid::PowerUpShieldMonitor>(EventType::PowerUpShield);
		RegisterSpawnable<Solid::PowerUpWeaponMonitor>(EventType::PowerUpWeapon);
		RegisterSpawnable<Environment::BirdCage>(EventType::BirdCage);
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

	void EventSpawner::PreloadEvent(EventType type, std::uint8_t* spawnParams)
	{
		auto it = _spawnableEvents.find(type);
		if (it == _spawnableEvents.end() || it->second.PreloadFunction == nullptr) {
			return;
		}

		it->second.PreloadFunction(ActorActivationDetails(_levelHandler, {}, spawnParams));
	}

	std::shared_ptr<ActorBase> EventSpawner::SpawnEvent(EventType type, std::uint8_t* spawnParams, ActorState flags, std::int32_t x, std::int32_t y, std::int32_t z)
	{
		return SpawnEvent(type, spawnParams, flags, Vector3i(x * 32 + 16, y * 32 + 16, (int)z));
	}

	std::shared_ptr<ActorBase> EventSpawner::SpawnEvent(EventType type, std::uint8_t* spawnParams, ActorState flags, const Vector3i& pos)
	{
		auto it = _spawnableEvents.find(type);
		if (it == _spawnableEvents.end() || it->second.CreateFunction == nullptr) {
			return nullptr;
		}

		ActorActivationDetails details(_levelHandler, pos, spawnParams);
		details.State = flags;
		details.Type = type;
		return it->second.CreateFunction(details);
	}
}