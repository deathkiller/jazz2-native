#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// Turtle, Lizard and Floating Lizard read the theme from the first byte
		// TRANSLATORS: Choices of a parameter of an event (themes of its look)
		constexpr ParamOption XmasThemes[] = { { NC_("editor", "Normal"), 0 }, { NC_("editor", "Christmas"), 1 } };

		constexpr EventParamField TurtleFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Theme"), 0, 8, XmasThemes)
				.WithDescription(NC_("editor", "Also changes the shell it leaves behind")),
		};
		constexpr EventPreviewCase TurtlePreview[] = {
			{ 1, "Enemy/TurtleXmas", std::uint32_t(AnimState::Walk) },
			{ EventPreviewCase::AnyValue, "Enemy/Turtle", std::uint32_t(AnimState::Walk) }
		};

		constexpr EventParamField LizardFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Theme"), 0, 8, XmasThemes),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(NC_("editor", "Falling"), 1)
				.WithDescription(NC_("editor", "Starts as a lizard that lost its copter, it isn't placed on the ground")),
			// Lizard::OnActivatedAsync() reads it only of a falling one
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(NC_("editor", "Facing left"), 2)
				.WithDescription(NC_("editor", "Used only when falling, otherwise the direction is random"))
				.ShowIf(1, 1).MirroredX(MirrorMode::Toggle),
		};

		constexpr ParamOption SuckerDirections[] = {
			// TRANSLATORS: Choices of a parameter of an event (directions, or none)
			{ NC_("editor", "None"), 0 }, { NC_("editor", "Up"), 1 }, { NC_("editor", "Down"), 2 }, { NC_("editor", "Left"), 4 }, { NC_("editor", "Right"), 8 }
		};
		// Mirrored, it was popped off the other way
		constexpr std::int32_t SuckerMirrorX[] = { 4, 8 };
		constexpr std::int32_t SuckerMirrorY[] = { 1, 2 };
		constexpr EventParamField SuckerFields[] = {
			// TRANSLATORS: Parameter of a sucker enemy, the direction it was popped off a floating sucker in
			EnumField(NC_("editor", "Pop direction"), 0, 8, SuckerDirections)
				.WithDescription(NC_("editor", "Starts deflating as if popped off a floating sucker in this direction, None for a normal sucker"))
				.MirroredXPairs(SuckerMirrorX).MirroredYPairs(SuckerMirrorY),
		};

		constexpr EventParamField MonkeyFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(NC_("editor", "Walking"), 0, 0, true)
				.WithDescription(NC_("editor", "Walks around, otherwise it stands in place")),
		};
		constexpr EventPreviewCase MonkeyPreview[] = {
			{ 0, "Enemy/Monkey", std::uint32_t(AnimState::Jump) },
			{ EventPreviewCase::AnyValue, "Enemy/Monkey", std::uint32_t(AnimState::Walk) }
		};

		// TRANSLATORS: Choices of a parameter of an event (themes of its look)
		constexpr ParamOption DoggyThemes[] = { { NC_("editor", "Dog"), 0 }, { NC_("editor", "Cat"), 1 } };
		constexpr EventParamField DoggyFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Theme"), 0, 8, DoggyThemes),
		};
		constexpr EventPreviewCase DoggyPreview[] = {
			{ 1, "Enemy/Cat", std::uint32_t(AnimState::Walk) },
			{ EventPreviewCase::AnyValue, "Enemy/Doggy", std::uint32_t(AnimState::Walk) }
		};

		constexpr EventParamField LizardFloatFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Theme"), 0, 8, XmasThemes),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Copter duration"), 1, 8, 0, 255)
				.WithUnit(ParamUnit::Seconds)
				.WithDescription(NC_("editor", "How long the copter stays after the lizard is hit, so a player can use it; 0 destroys it at once")),
		};
		// Its copter is drawn with it, see EventPreviews::GetExtraParts()
		constexpr EventPreviewCase LizardFloatPreview[] = {
			{ 1, "Enemy/LizardFloatXmas", std::uint32_t(AnimState::Idle) },
			{ EventPreviewCase::AnyValue, "Enemy/LizardFloat", std::uint32_t(AnimState::Idle) }
		};

		// TRANSLATORS: Choices of a parameter of an event (themes of its look)
		constexpr ParamOption BilsyThemes[] = { { NC_("editor", "Normal"), 0 }, { NC_("editor", "Christmas"), 1 } };
		constexpr EventParamField BilsyFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Theme"), 0, 8, BilsyThemes),
			// TRANSLATORS: Parameter of a boss event, the level text shown at the end of the fight
			TextIdField(NC_("editor", "End text"), 1)
				.WithDescription(NC_("editor", "Level text shown when the boss is defeated")),
		};
		constexpr EventPreviewCase BilsyPreview[] = {
			{ 1, "Boss/BilsyXmas", std::uint32_t(AnimState::Idle) },
			{ EventPreviewCase::AnyValue, "Boss/Bilsy", std::uint32_t(AnimState::Idle) }
		};

		// Facing or placed as the objects turn or move themselves when they're spawned
		constexpr EventPreviewCase DragonPreview[] = { { EventPreviewCase::AnyValue, "Enemy/Dragon", std::uint32_t(AnimState::Idle), false, 0, 8 } };
		constexpr EventPreviewCase FencerPreview[] = { { EventPreviewCase::AnyValue, "Enemy/Fencer", std::uint32_t(AnimState::Idle), true } };
		constexpr EventPreviewCase CaterpillarPreview[] = { { EventPreviewCase::AnyValue, "Enemy/Caterpillar", std::uint32_t(AnimState::Idle), true } };
		constexpr EventPreviewCase SparksPreview[] = { { EventPreviewCase::AnyValue, "Enemy/Sparks", std::uint32_t(AnimState::Idle), true } };
		constexpr EventPreviewCase DevanRemotePreview[] = { { EventPreviewCase::AnyValue, "Boss/DevanRemote", std::uint32_t(AnimState::Idle), true } };
		constexpr EventPreviewCase RobotPreview[] = { { EventPreviewCase::AnyValue, "Boss/Robot", std::uint32_t(AnimState::Idle), true } };
		constexpr EventPreviewCase TurtleBossPreview[] = { { EventPreviewCase::AnyValue, "Boss/TurtleBoss", std::uint32_t(AnimState::Idle), true } };
		constexpr EventPreviewCase QueenPreview[] = { { EventPreviewCase::AnyValue, "Boss/Queen", std::uint32_t(AnimState::Idle), false, 0, -8 } };

		// Bosses that read only the end text from the second byte
		constexpr EventParamField BossFields[] = {
			// TRANSLATORS: Parameter of a boss event, the level text shown at the end of the fight
			TextIdField(NC_("editor", "End text"), 1)
				.WithDescription(NC_("editor", "Level text shown when the boss is defeated")),
		};

		constexpr EventParamField DevanRemoteFields[] = {
			// TRANSLATORS: Parameter of a boss event, the level text shown at the start of the fight
			TextIdField(NC_("editor", "Intro text"), 1)
				.WithDescription(NC_("editor", "Level text shown when the boss is activated")),
			// TRANSLATORS: Parameter of a boss event, the level text shown at the end of the fight
			TextIdField(NC_("editor", "End text"), 2)
				.WithDescription(NC_("editor", "Level text shown when the robot is destroyed")),
		};

		// The speed of a launched shell is stored as floats in bytes 0 to 7, they can't be edited here
		// TRANSLATORS: Choices of a parameter of an event (themes of its look)
		constexpr ParamOption TurtleShellThemes[] = { { NC_("editor", "Normal"), 0 }, { NC_("editor", "Christmas"), 1 }, { NC_("editor", "Turtle Boss"), 2 } };
		constexpr EventParamField TurtleShellFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Theme"), 8, 8, TurtleShellThemes),
		};
		constexpr EventPreviewCase TurtleShellPreview[] = {
			{ 1, "Enemy/TurtleShellXmas", std::uint32_t(AnimState::Default) },
			{ 2, "Boss/TurtleBossShell", std::uint32_t(AnimState::Default) },
			{ EventPreviewCase::AnyValue, "Enemy/TurtleShell", std::uint32_t(AnimState::Default) }
		};

		constexpr EventDescriptor Events[] = {
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyTurtle, "EnemyTurtle", NC_("editor", "Turtle"), EventCategory::Enemy,
				NC_("editor", "Walks back and forth, leaves its shell behind when defeated."),
				TurtleFields, "Enemy/Turtle", std::uint32_t(AnimState::Walk), EventTraits::Object)
				.WithPreviewCases(0, TurtlePreview),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyLizard, "EnemyLizard", NC_("editor", "Lizard"), EventCategory::Enemy,
				NC_("editor", "Walks back and forth."),
				LizardFields, "Enemy/Lizard", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemySucker, "EnemySucker", NC_("editor", "Sucker"), EventCategory::Enemy,
				NC_("editor", "Walks back and forth."),
				SuckerFields, "Enemy/Sucker", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, a flying enemy that drops a sucker
			DescribeEvent(EventType::EnemySuckerFloat, "EnemySuckerFloat", NC_("editor", "Floating Sucker"), EventCategory::Enemy,
				NC_("editor", "Floats in a small circle, drops a sucker when hit."),
				"Enemy/SuckerFloat", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyLabRat, "EnemyLabRat", NC_("editor", "Lab Rat"), EventCategory::Enemy,
				NC_("editor", "Walks around and leaps at nearby players."),
				"Enemy/LabRat", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy with a proper name
			DescribeEvent(EventType::EnemyHelmut, "EnemyHelmut", NC_("editor", "Helmut"), EventCategory::Enemy,
				NC_("editor", "Walks back and forth."),
				"Enemy/Helmut", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyDragon, "EnemyDragon", NC_("editor", "Dragon"), EventCategory::Enemy,
				NC_("editor", "Breathes fire at nearby players."),
				"Enemy/Dragon", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, DragonPreview),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyBat, "EnemyBat", NC_("editor", "Bat"), EventCategory::Enemy,
				NC_("editor", "Hangs in place and flies at nearby players."),
				"Enemy/Bat", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy, a big woman
			DescribeEvent(EventType::EnemyFatChick, "EnemyFatChick", NC_("editor", "Fat Chick"), EventCategory::Enemy,
				NC_("editor", "Walks around and attacks nearby players."),
				"Enemy/FatChick", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy that fights with a sword
			DescribeEvent(EventType::EnemyFencer, "EnemyFencer", NC_("editor", "Fencer"), EventCategory::Enemy,
				NC_("editor", "Jumps towards and away from nearby players."),
				"Enemy/Fencer", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, FencerPreview),
			// TRANSLATORS: Name of an event, a flying enemy
			DescribeEvent(EventType::EnemyRapier, "EnemyRapier", NC_("editor", "Rapier"), EventCategory::Enemy,
				NC_("editor", "Floats around and flies at nearby players."),
				"Enemy/Rapier", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a flying enemy
			DescribeEvent(EventType::EnemySparks, "EnemySparks", NC_("editor", "Sparks"), EventCategory::Enemy,
				NC_("editor", "Flies through walls towards nearby players."),
				"Enemy/Sparks", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, SparksPreview),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyMonkey, "EnemyMonkey", NC_("editor", "Monkey"), EventCategory::Enemy,
				NC_("editor", "Throws bananas at nearby players."),
				MonkeyFields, "Enemy/Monkey", std::uint32_t(AnimState::Walk), EventTraits::Object)
				.WithPreviewCases(0, MonkeyPreview),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyDemon, "EnemyDemon", NC_("editor", "Demon"), EventCategory::Enemy,
				NC_("editor", "Walks and attacks nearby players."),
				"Enemy/Demon", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyBee, "EnemyBee", NC_("editor", "Bee"), EventCategory::Enemy,
				NC_("editor", "Flies at nearby players and returns to its place."),
				"Enemy/Bee", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyBeeSwarm, "EnemyBeeSwarm", NC_("editor", "Bee Swarm"), EventCategory::Enemy,
				NC_("editor", "Swarm of bees, not implemented by the game."),
				"Enemy/Bee", std::uint32_t(AnimState::Idle), EventTraits::Unused),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyCaterpillar, "EnemyCaterpillar", NC_("editor", "Caterpillar"), EventCategory::Enemy,
				NC_("editor", "Blows smoke rings that make players dizzy, it can't be destroyed."),
				"Enemy/Caterpillar", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, CaterpillarPreview),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyCrab, "EnemyCrab", NC_("editor", "Crab"), EventCategory::Enemy,
				NC_("editor", "Walks back and forth."),
				"Enemy/Crab", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy, a dog
			DescribeEvent(EventType::EnemyDoggy, "EnemyDoggy", NC_("editor", "Doggy"), EventCategory::Enemy,
				NC_("editor", "Walks back and forth, charges when hit."),
				DoggyFields, "Enemy/Doggy", std::uint32_t(AnimState::Walk), EventTraits::Object)
				.WithPreviewCases(0, DoggyPreview),
			// TRANSLATORS: Name of an event, an enemy, the insect
			DescribeEvent(EventType::EnemyDragonfly, "EnemyDragonfly", NC_("editor", "Dragonfly"), EventCategory::Enemy,
				NC_("editor", "Flies around and attacks nearby players."),
				"Enemy/Dragonfly", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyFish, "EnemyFish", NC_("editor", "Fish"), EventCategory::Enemy,
				NC_("editor", "Swims in water and attacks nearby players."),
				"Enemy/Fish", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy, a pun on the Mad Hatter
			DescribeEvent(EventType::EnemyMadderHatter, "EnemyMadderHatter", NC_("editor", "Madder Hatter"), EventCategory::Enemy,
				NC_("editor", "Walks around and spits at nearby players."),
				"Enemy/MadderHatter", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyRaven, "EnemyRaven", NC_("editor", "Raven"), EventCategory::Enemy,
				NC_("editor", "Flies at nearby players."),
				"Enemy/Raven", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemySkeleton, "EnemySkeleton", NC_("editor", "Skeleton"), EventCategory::Enemy,
				NC_("editor", "Walks back and forth."),
				"Enemy/Skeleton", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy, a stronger turtle
			DescribeEvent(EventType::EnemyTurtleTough, "EnemyTurtleTough", NC_("editor", "Tough Turtle"), EventCategory::Enemy,
				NC_("editor", "Walks back and forth, takes more hits."),
				"Enemy/TurtleTough", std::uint32_t(AnimState::Walk), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy, a turtle floating on the water in a swim ring
			DescribeEvent(EventType::EnemyTurtleTube, "EnemyTurtleTube", NC_("editor", "Tube Turtle"), EventCategory::Enemy,
				NC_("editor", "Floats on the water surface in a tube."),
				"Enemy/TurtleTube", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy
			DescribeEvent(EventType::EnemyWitch, "EnemyWitch", NC_("editor", "Witch"), EventCategory::Enemy,
				NC_("editor", "Flies at players and casts spells that turn them into frogs."),
				"Enemy/Witch", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an enemy, a lizard flying on a copter
			DescribeEvent(EventType::EnemyLizardFloat, "EnemyLizardFloat", NC_("editor", "Floating Lizard"), EventCategory::Enemy,
				NC_("editor", "Flies on a copter and drops bombs, falls as a lizard when hit."),
				LizardFloatFields, "Enemy/LizardFloat", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, LizardFloatPreview),

			// TRANSLATORS: Name of an event, a boss with a proper name
			DescribeEvent(EventType::BossTweedle, "BossTweedle", NC_("editor", "Tweedle"), EventCategory::Boss,
				NC_("editor", "Not implemented by the game."),
				"Boss/Tweedle", std::uint32_t(AnimState::Idle), EventTraits::Unused),
			// TRANSLATORS: Name of an event, a boss with a proper name
			DescribeEvent(EventType::BossBilsy, "BossBilsy", NC_("editor", "Bilsy"), EventCategory::Boss,
				NC_("editor", "Teleports around and throws fireballs."),
				BilsyFields, "Boss/Bilsy", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, BilsyPreview),
			// TRANSLATORS: Name of an event, a boss with a proper name
			DescribeEvent(EventType::BossDevan, "BossDevan", NC_("editor", "Devan"), EventCategory::Boss,
				NC_("editor", "Shoots at players, then turns into a flying demon."),
				BossFields, "Boss/Devan", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a boss
			DescribeEvent(EventType::BossQueen, "BossQueen", NC_("editor", "Queen"), EventCategory::Boss,
				NC_("editor", "Can't be hurt, hits push her back until she falls off her ledge."),
				BossFields, "Boss/Queen", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, QueenPreview),
			// TRANSLATORS: Name of an event, a boss
			DescribeEvent(EventType::BossRobot, "BossRobot", NC_("editor", "Robot"), EventCategory::Boss,
				NC_("editor", "Hidden until a Devan Remote boss activates it."),
				"Boss/Robot", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, RobotPreview),
			// TRANSLATORS: Name of an event, a boss
			DescribeEvent(EventType::BossUterus, "BossUterus", NC_("editor", "Uterus"), EventCategory::Boss,
				NC_("editor", "Protected by shields, spawns crabs."),
				BossFields, "Boss/Uterus", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a boss, a big turtle
			DescribeEvent(EventType::BossTurtleTough, "BossTurtleTough", NC_("editor", "Turtle Boss"), EventCategory::Boss,
				NC_("editor", "Walks towards players and throws its mace."),
				BossFields, "Boss/TurtleBoss", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, TurtleBossPreview),
			// TRANSLATORS: Name of an event, a boss with a proper name
			DescribeEvent(EventType::BossBubba, "BossBubba", NC_("editor", "Bubba"), EventCategory::Boss,
				NC_("editor", "Spits fireballs and spins like a tornado."),
				BossFields, "Boss/Bubba", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a boss, Devan controlling the Robot boss by a remote control
			DescribeEvent(EventType::BossDevanRemote, "BossDevanRemote", NC_("editor", "Devan Remote"), EventCategory::Boss,
				NC_("editor", "Devan controlling the Robot boss, which has to be placed in the level too."),
				DevanRemoteFields, "Boss/DevanRemote", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, DevanRemotePreview),
			// TRANSLATORS: Name of an event, a boss with a proper name
			DescribeEvent(EventType::BossBolly, "BossBolly", NC_("editor", "Bolly"), EventCategory::Boss,
				NC_("editor", "Flies after players with a spiked chain and fires rockets."),
				BossFields, "Boss/Bolly", std::uint32_t(AnimState::Idle), EventTraits::Object),

			// TRANSLATORS: Name of an event, the shell a turtle enemy leaves behind
			DescribeEvent(EventType::TurtleShell, "TurtleShell", NC_("editor", "Turtle Shell"), EventCategory::Enemy,
				NC_("editor", "Slides when shot and hurts enemies it hits."),
				TurtleShellFields, "Enemy/TurtleShell", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, TurtleShellPreview),
		};
	}

	ArrayView<const EventDescriptor> GetEnemyEvents()
	{
		return Events;
	}
}

#endif
