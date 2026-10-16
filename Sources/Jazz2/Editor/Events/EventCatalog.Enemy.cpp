#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// Turtle, Lizard and Floating Lizard read the theme from the first byte
		constexpr ParamOption XmasThemes[] = { { N_("Normal"), 0 }, { N_("Christmas"), 1 } };

		constexpr EventParamField TurtleFields[] = {
			EnumField(N_("Theme"), 0, 8, XmasThemes)
				.WithDescription(N_("Also changes the shell it leaves behind")),
		};

		constexpr EventParamField LizardFields[] = {
			EnumField(N_("Theme"), 0, 8, XmasThemes),
			BoolField(N_("Falling"), 1)
				.WithDescription(N_("Starts as a lizard that lost its copter, it isn't placed on the ground")),
			BoolField(N_("Facing left"), 2)
				.WithDescription(N_("Used only when falling, otherwise the direction is random")),
		};

		constexpr ParamOption SuckerDirections[] = {
			{ N_("None"), 0 }, { N_("Up"), 1 }, { N_("Down"), 2 }, { N_("Left"), 4 }, { N_("Right"), 8 }
		};
		constexpr EventParamField SuckerFields[] = {
			EnumField(N_("Pop direction"), 0, 8, SuckerDirections)
				.WithDescription(N_("Starts deflating as if popped off a floating sucker in this direction, None for a normal sucker")),
		};

		constexpr EventParamField MonkeyFields[] = {
			BoolField(N_("Walking"), 0, 0, true)
				.WithDescription(N_("Walks around, otherwise it stands in place")),
		};

		constexpr ParamOption DoggyThemes[] = { { N_("Dog"), 0 }, { N_("Cat"), 1 } };
		constexpr EventParamField DoggyFields[] = {
			EnumField(N_("Theme"), 0, 8, DoggyThemes),
		};

		constexpr EventParamField LizardFloatFields[] = {
			EnumField(N_("Theme"), 0, 8, XmasThemes),
			UIntField(N_("Copter duration"), 1, 8, 0, 255)
				.WithUnit(ParamUnit::Seconds)
				.WithDescription(N_("How long the copter stays after the lizard is hit, so a player can use it; 0 destroys it at once")),
		};

		constexpr ParamOption BilsyThemes[] = { { N_("Normal"), 0 }, { N_("Christmas"), 1 } };
		constexpr EventParamField BilsyFields[] = {
			EnumField(N_("Theme"), 0, 8, BilsyThemes),
			TextIdField(N_("End text"), 1)
				.WithDescription(N_("Level text shown when the boss is defeated")),
		};

		// Bosses that read only the end text from the second byte
		constexpr EventParamField BossFields[] = {
			TextIdField(N_("End text"), 1)
				.WithDescription(N_("Level text shown when the boss is defeated")),
		};

		constexpr EventParamField DevanRemoteFields[] = {
			TextIdField(N_("Intro text"), 1)
				.WithDescription(N_("Level text shown when the boss is activated")),
			TextIdField(N_("End text"), 2)
				.WithDescription(N_("Level text shown when the robot is destroyed")),
		};

		// The speed of a launched shell is stored as floats in bytes 0 to 7, they can't be edited here
		constexpr ParamOption TurtleShellThemes[] = { { N_("Normal"), 0 }, { N_("Christmas"), 1 }, { N_("Turtle Boss"), 2 } };
		constexpr EventParamField TurtleShellFields[] = {
			EnumField(N_("Theme"), 8, 8, TurtleShellThemes),
		};

		constexpr EventDescriptor Events[] = {
			DescribeEvent(EventType::EnemyTurtle, "EnemyTurtle", N_("Turtle"), EventCategory::Enemy,
				N_("Walks back and forth, leaves its shell behind when defeated."),
				TurtleFields, "Enemy/Turtle", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyLizard, "EnemyLizard", N_("Lizard"), EventCategory::Enemy,
				N_("Walks back and forth."),
				LizardFields, "Enemy/Lizard", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemySucker, "EnemySucker", N_("Sucker"), EventCategory::Enemy,
				N_("Walks back and forth."),
				SuckerFields, "Enemy/Sucker", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemySuckerFloat, "EnemySuckerFloat", N_("Floating Sucker"), EventCategory::Enemy,
				N_("Floats in a small circle, drops a sucker when hit."),
				"Enemy/SuckerFloat", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyLabRat, "EnemyLabRat", N_("Lab Rat"), EventCategory::Enemy,
				N_("Walks around and leaps at nearby players."),
				"Enemy/LabRat", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyHelmut, "EnemyHelmut", N_("Helmut"), EventCategory::Enemy,
				N_("Walks back and forth."),
				"Enemy/Helmut", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyDragon, "EnemyDragon", N_("Dragon"), EventCategory::Enemy,
				N_("Breathes fire at nearby players."),
				"Enemy/Dragon", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyBat, "EnemyBat", N_("Bat"), EventCategory::Enemy,
				N_("Hangs in place and flies at nearby players."),
				"Enemy/Bat", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyFatChick, "EnemyFatChick", N_("Fat Chick"), EventCategory::Enemy,
				N_("Walks around and attacks nearby players."),
				"Enemy/FatChick", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyFencer, "EnemyFencer", N_("Fencer"), EventCategory::Enemy,
				N_("Jumps towards and away from nearby players."),
				"Enemy/Fencer", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyRapier, "EnemyRapier", N_("Rapier"), EventCategory::Enemy,
				N_("Floats around and flies at nearby players."),
				"Enemy/Rapier", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemySparks, "EnemySparks", N_("Sparks"), EventCategory::Enemy,
				N_("Flies through walls towards nearby players."),
				"Enemy/Sparks", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyMonkey, "EnemyMonkey", N_("Monkey"), EventCategory::Enemy,
				N_("Throws bananas at nearby players."),
				MonkeyFields, "Enemy/Monkey", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyDemon, "EnemyDemon", N_("Demon"), EventCategory::Enemy,
				N_("Walks and attacks nearby players."),
				"Enemy/Demon", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyBee, "EnemyBee", N_("Bee"), EventCategory::Enemy,
				N_("Flies at nearby players and returns to its place."),
				"Enemy/Bee", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyBeeSwarm, "EnemyBeeSwarm", N_("Bee Swarm"), EventCategory::Enemy,
				N_("Swarm of bees, not implemented by the game."),
				"Enemy/Bee", std::uint32_t(AnimState::Idle), EventTraits::Unused),
			DescribeEvent(EventType::EnemyCaterpillar, "EnemyCaterpillar", N_("Caterpillar"), EventCategory::Enemy,
				N_("Blows smoke rings that make players dizzy, it can't be destroyed."),
				"Enemy/Caterpillar", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyCrab, "EnemyCrab", N_("Crab"), EventCategory::Enemy,
				N_("Walks back and forth."),
				"Enemy/Crab", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyDoggy, "EnemyDoggy", N_("Doggy"), EventCategory::Enemy,
				N_("Walks back and forth, charges when hit."),
				DoggyFields, "Enemy/Doggy", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyDragonfly, "EnemyDragonfly", N_("Dragonfly"), EventCategory::Enemy,
				N_("Flies around and attacks nearby players."),
				"Enemy/Dragonfly", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyFish, "EnemyFish", N_("Fish"), EventCategory::Enemy,
				N_("Swims in water and attacks nearby players."),
				"Enemy/Fish", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyMadderHatter, "EnemyMadderHatter", N_("Madder Hatter"), EventCategory::Enemy,
				N_("Walks around and spits at nearby players."),
				"Enemy/MadderHatter", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyRaven, "EnemyRaven", N_("Raven"), EventCategory::Enemy,
				N_("Flies at nearby players."),
				"Enemy/Raven", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemySkeleton, "EnemySkeleton", N_("Skeleton"), EventCategory::Enemy,
				N_("Walks back and forth."),
				"Enemy/Skeleton", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyTurtleTough, "EnemyTurtleTough", N_("Tough Turtle"), EventCategory::Enemy,
				N_("Walks back and forth, takes more hits."),
				"Enemy/TurtleTough", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyTurtleTube, "EnemyTurtleTube", N_("Tube Turtle"), EventCategory::Enemy,
				N_("Floats on the water surface in a tube."),
				"Enemy/TurtleTube", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyWitch, "EnemyWitch", N_("Witch"), EventCategory::Enemy,
				N_("Flies at players and casts spells that turn them into frogs."),
				"Enemy/Witch", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyLizardFloat, "EnemyLizardFloat", N_("Floating Lizard"), EventCategory::Enemy,
				N_("Flies on a copter and drops bombs, falls as a lizard when hit."),
				LizardFloatFields, "Enemy/LizardFloat", std::uint32_t(AnimState::Idle), EventTraits::Object),

			DescribeEvent(EventType::BossTweedle, "BossTweedle", N_("Tweedle"), EventCategory::Boss,
				N_("Not implemented by the game."),
				"Boss/Tweedle", std::uint32_t(AnimState::Idle), EventTraits::Unused),
			DescribeEvent(EventType::BossBilsy, "BossBilsy", N_("Bilsy"), EventCategory::Boss,
				N_("Teleports around and throws fireballs."),
				BilsyFields, "Boss/Bilsy", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossDevan, "BossDevan", N_("Devan"), EventCategory::Boss,
				N_("Shoots at players, then turns into a flying demon."),
				BossFields, "Boss/Devan", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossQueen, "BossQueen", N_("Queen"), EventCategory::Boss,
				N_("Can't be hurt, hits push her back until she falls off her ledge."),
				BossFields, "Boss/Queen", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossRobot, "BossRobot", N_("Robot"), EventCategory::Boss,
				N_("Hidden until a Devan Remote boss activates it."),
				"Boss/Robot", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossUterus, "BossUterus", N_("Uterus"), EventCategory::Boss,
				N_("Protected by shields, spawns crabs."),
				BossFields, "Boss/Uterus", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossTurtleTough, "BossTurtleTough", N_("Turtle Boss"), EventCategory::Boss,
				N_("Walks towards players and throws its mace."),
				BossFields, "Boss/TurtleBoss", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossBubba, "BossBubba", N_("Bubba"), EventCategory::Boss,
				N_("Spits fireballs and spins like a tornado."),
				BossFields, "Boss/Bubba", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossDevanRemote, "BossDevanRemote", N_("Devan Remote"), EventCategory::Boss,
				N_("Devan controlling the Robot boss, which has to be placed in the level too."),
				DevanRemoteFields, "Boss/DevanRemote", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossBolly, "BossBolly", N_("Bolly"), EventCategory::Boss,
				N_("Flies after players with a spiked chain and fires rockets."),
				BossFields, "Boss/Bolly", std::uint32_t(AnimState::Idle), EventTraits::Object),

			DescribeEvent(EventType::TurtleShell, "TurtleShell", N_("Turtle Shell"), EventCategory::Enemy,
				N_("Slides when shot and hurts enemies it hits."),
				TurtleShellFields, "Enemy/TurtleShell", std::uint32_t(AnimState::Default), EventTraits::Object),
		};
	}

	ArrayView<const EventDescriptor> GetEnemyEvents()
	{
		return Events;
	}
}

#endif
