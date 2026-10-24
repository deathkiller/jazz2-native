#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"
#include "../../../nCine/I18n.h"

using namespace nCine;

namespace Jazz2::Editor::EventCatalogParts
{
	ArrayView<const EventDescriptor> GetEnemyEvents()
	{
		// Built on the first call, after the language is loaded, only what's translated here by _x() is translated, see CatalogText
		// Turtle, Lizard and Floating Lizard read the theme from the first byte
		// TRANSLATORS: Choices of a parameter of an event (themes of its look)
		static const ParamOption XmasThemes[] = { { _x("editor", "Normal"), 0 }, { _x("editor", "Christmas"), 1 } };

		static const EventParamField TurtleFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Theme"), 0, 8, XmasThemes)
				.WithDescription(_x("editor", "Also changes the shell it leaves behind")),
		};
		static const EventPreviewCase TurtlePreview[] = {
			{ 1, "Enemy/TurtleXmas", std::uint32_t(AnimState::Walk) },
			{ EventPreviewCase::AnyValue, "Enemy/Turtle", std::uint32_t(AnimState::Walk) }
		};

		static const EventParamField LizardFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Theme"), 0, 8, XmasThemes),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(_x("editor", "Falling"), 1)
				.WithDescription(_x("editor", "Starts as a lizard that lost its copter, it isn't placed on the ground")),
			// Lizard::OnActivatedAsync() reads it only of a falling one
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(_x("editor", "Facing left"), 2)
				.WithDescription(_x("editor", "Used only when falling, otherwise the direction is random"))
				.ShowIf(1, 1).MirroredX(MirrorMode::Toggle),
		};

		static const ParamOption SuckerDirections[] = {
			// TRANSLATORS: Choices of a parameter of an event (directions, or none)
			{ _x("editor", "None"), 0 }, { _x("editor", "Up"), 1 }, { _x("editor", "Down"), 2 }, { _x("editor", "Left"), 4 }, { _x("editor", "Right"), 8 }
		};
		// Mirrored, it was popped off the other way
		static const std::int32_t SuckerMirrorX[] = { 4, 8 };
		static const std::int32_t SuckerMirrorY[] = { 1, 2 };
		static const EventParamField SuckerFields[] = {
			// TRANSLATORS: Parameter of a sucker enemy, the direction it was popped off a floating sucker in
			EnumField(_x("editor", "Pop direction"), 0, 8, SuckerDirections)
				.WithDescription(_x("editor", "Starts deflating as if popped off a floating sucker in this direction, None for a normal sucker"))
				.MirroredXPairs(SuckerMirrorX).MirroredYPairs(SuckerMirrorY),
		};

		static const EventParamField MonkeyFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(_x("editor", "Walking"), 0, 0, true)
				.WithDescription(_x("editor", "Walks around, otherwise it stands in place")),
		};
		static const EventPreviewCase MonkeyPreview[] = {
			{ 0, "Enemy/Monkey", std::uint32_t(AnimState::Jump) },
			{ EventPreviewCase::AnyValue, "Enemy/Monkey", std::uint32_t(AnimState::Walk) }
		};

		// TRANSLATORS: Choices of a parameter of an event (themes of its look)
		static const ParamOption DoggyThemes[] = { { _x("editor", "Dog"), 0 }, { _x("editor", "Cat"), 1 } };
		static const EventParamField DoggyFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Theme"), 0, 8, DoggyThemes),
		};
		static const EventPreviewCase DoggyPreview[] = {
			{ 1, "Enemy/Cat", std::uint32_t(AnimState::Walk) },
			{ EventPreviewCase::AnyValue, "Enemy/Doggy", std::uint32_t(AnimState::Walk) }
		};

		static const EventParamField LizardFloatFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Theme"), 0, 8, XmasThemes),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Copter duration"), 1, 8, 0, 255)
				.WithUnit(ParamUnit::Seconds)
				.WithDescription(_x("editor", "How long the copter stays after the lizard is hit, so a player can use it; 0 destroys it at once")),
		};
		// Its copter is drawn with it, see EventPreviews::GetExtraParts()
		static const EventPreviewCase LizardFloatPreview[] = {
			{ 1, "Enemy/LizardFloatXmas", std::uint32_t(AnimState::Idle) },
			{ EventPreviewCase::AnyValue, "Enemy/LizardFloat", std::uint32_t(AnimState::Idle) }
		};

		// TRANSLATORS: Choices of a parameter of an event (themes of its look)
		static const ParamOption BilsyThemes[] = { { _x("editor", "Normal"), 0 }, { _x("editor", "Christmas"), 1 } };
		static const EventParamField BilsyFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Theme"), 0, 8, BilsyThemes),
			// TRANSLATORS: Parameter of a boss event, the level text shown at the end of the fight
			TextIdField(_x("editor", "End text"), 1)
				.WithDescription(_x("editor", "Level text shown when the boss is defeated")),
		};
		static const EventPreviewCase BilsyPreview[] = {
			{ 1, "Boss/BilsyXmas", std::uint32_t(AnimState::Idle) },
			{ EventPreviewCase::AnyValue, "Boss/Bilsy", std::uint32_t(AnimState::Idle) }
		};

		// Facing or placed as the objects turn or move themselves when they're spawned
		static const EventPreviewCase DragonPreview[] = { { EventPreviewCase::AnyValue, "Enemy/Dragon", std::uint32_t(AnimState::Idle), false, 0, 8 } };
		static const EventPreviewCase FencerPreview[] = { { EventPreviewCase::AnyValue, "Enemy/Fencer", std::uint32_t(AnimState::Idle), true } };
		static const EventPreviewCase CaterpillarPreview[] = { { EventPreviewCase::AnyValue, "Enemy/Caterpillar", std::uint32_t(AnimState::Idle), true } };
		static const EventPreviewCase SparksPreview[] = { { EventPreviewCase::AnyValue, "Enemy/Sparks", std::uint32_t(AnimState::Idle), true } };
		static const EventPreviewCase DevanRemotePreview[] = { { EventPreviewCase::AnyValue, "Boss/DevanRemote", std::uint32_t(AnimState::Idle), true } };
		static const EventPreviewCase RobotPreview[] = { { EventPreviewCase::AnyValue, "Boss/Robot", std::uint32_t(AnimState::Idle), true } };
		static const EventPreviewCase TurtleBossPreview[] = { { EventPreviewCase::AnyValue, "Boss/TurtleBoss", std::uint32_t(AnimState::Idle), true } };
		static const EventPreviewCase QueenPreview[] = { { EventPreviewCase::AnyValue, "Boss/Queen", std::uint32_t(AnimState::Idle), false, 0, -8 } };

		// Bosses that read only the end text from the second byte
		static const EventParamField BossFields[] = {
			// TRANSLATORS: Parameter of a boss event, the level text shown at the end of the fight
			TextIdField(_x("editor", "End text"), 1)
				.WithDescription(_x("editor", "Level text shown when the boss is defeated")),
		};

		static const EventParamField DevanRemoteFields[] = {
			// TRANSLATORS: Parameter of a boss event, the level text shown at the start of the fight
			TextIdField(_x("editor", "Intro text"), 1)
				.WithDescription(_x("editor", "Level text shown when the boss is activated")),
			// TRANSLATORS: Parameter of a boss event, the level text shown at the end of the fight
			TextIdField(_x("editor", "End text"), 2)
				.WithDescription(_x("editor", "Level text shown when the robot is destroyed")),
		};

		// The speed of a launched shell is stored as floats in bytes 0 to 7, they can't be edited here
		// TRANSLATORS: Choices of a parameter of an event (themes of its look)
		static const ParamOption TurtleShellThemes[] = { { _x("editor", "Normal"), 0 }, { _x("editor", "Christmas"), 1 }, { "Turtle Boss", 2 } };
		static const EventParamField TurtleShellFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Theme"), 8, 8, TurtleShellThemes),
		};
		static const EventPreviewCase TurtleShellPreview[] = {
			{ 1, "Enemy/TurtleShellXmas", std::uint32_t(AnimState::Default) },
			{ 2, "Boss/TurtleBossShell", std::uint32_t(AnimState::Default) },
			{ EventPreviewCase::AnyValue, "Enemy/TurtleShell", std::uint32_t(AnimState::Default) }
		};

		// Names of enemies and bosses aren't translated
		static const EventDescriptor Events[] = {
			DescribeEvent(EventType::EnemyTurtle, "EnemyTurtle", "Turtle", EventCategory::Enemy,
				_x("editor", "Walks back and forth, leaves its shell behind when defeated."),
				TurtleFields, "Enemy/Turtle", std::uint32_t(AnimState::Walk), EventTraits::Object)
				.WithPreviewCases(0, TurtlePreview),
			DescribeEvent(EventType::EnemyLizard, "EnemyLizard", "Lizard", EventCategory::Enemy,
				_x("editor", "Walks back and forth."),
				LizardFields, "Enemy/Lizard", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemySucker, "EnemySucker", "Sucker", EventCategory::Enemy,
				_x("editor", "Walks back and forth."),
				SuckerFields, "Enemy/Sucker", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemySuckerFloat, "EnemySuckerFloat", "Floating Sucker", EventCategory::Enemy,
				_x("editor", "Floats in a small circle, drops a sucker when hit."),
				"Enemy/SuckerFloat", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyLabRat, "EnemyLabRat", "Lab Rat", EventCategory::Enemy,
				_x("editor", "Walks around and leaps at nearby players."),
				"Enemy/LabRat", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyHelmut, "EnemyHelmut", "Helmut", EventCategory::Enemy,
				_x("editor", "Walks back and forth."),
				"Enemy/Helmut", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyDragon, "EnemyDragon", "Dragon", EventCategory::Enemy,
				_x("editor", "Breathes fire at nearby players."),
				"Enemy/Dragon", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, DragonPreview),
			DescribeEvent(EventType::EnemyBat, "EnemyBat", "Bat", EventCategory::Enemy,
				_x("editor", "Hangs in place and flies at nearby players."),
				"Enemy/Bat", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyFatChick, "EnemyFatChick", "Fat Chick", EventCategory::Enemy,
				_x("editor", "Walks around and attacks nearby players."),
				"Enemy/FatChick", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyFencer, "EnemyFencer", "Fencer", EventCategory::Enemy,
				_x("editor", "Jumps towards and away from nearby players."),
				"Enemy/Fencer", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, FencerPreview),
			DescribeEvent(EventType::EnemyRapier, "EnemyRapier", "Rapier", EventCategory::Enemy,
				_x("editor", "Floats around and flies at nearby players."),
				"Enemy/Rapier", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemySparks, "EnemySparks", "Sparks", EventCategory::Enemy,
				_x("editor", "Flies through walls towards nearby players."),
				"Enemy/Sparks", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, SparksPreview),
			DescribeEvent(EventType::EnemyMonkey, "EnemyMonkey", "Monkey", EventCategory::Enemy,
				_x("editor", "Throws bananas at nearby players."),
				MonkeyFields, "Enemy/Monkey", std::uint32_t(AnimState::Walk), EventTraits::Object)
				.WithPreviewCases(0, MonkeyPreview),
			DescribeEvent(EventType::EnemyDemon, "EnemyDemon", "Demon", EventCategory::Enemy,
				_x("editor", "Walks and attacks nearby players."),
				"Enemy/Demon", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyBee, "EnemyBee", "Bee", EventCategory::Enemy,
				_x("editor", "Flies at nearby players and returns to its place."),
				"Enemy/Bee", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyBeeSwarm, "EnemyBeeSwarm", "Bee Swarm", EventCategory::Enemy,
				_x("editor", "Swarm of bees, not implemented by the game."),
				"Enemy/Bee", std::uint32_t(AnimState::Idle), EventTraits::Unused),
			DescribeEvent(EventType::EnemyCaterpillar, "EnemyCaterpillar", "Caterpillar", EventCategory::Enemy,
				_x("editor", "Blows smoke rings that make players dizzy, it can't be destroyed."),
				"Enemy/Caterpillar", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, CaterpillarPreview),
			DescribeEvent(EventType::EnemyCrab, "EnemyCrab", "Crab", EventCategory::Enemy,
				_x("editor", "Walks back and forth."),
				"Enemy/Crab", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyDoggy, "EnemyDoggy", "Doggy", EventCategory::Enemy,
				_x("editor", "Walks back and forth, charges when hit."),
				DoggyFields, "Enemy/Doggy", std::uint32_t(AnimState::Walk), EventTraits::Object)
				.WithPreviewCases(0, DoggyPreview),
			DescribeEvent(EventType::EnemyDragonfly, "EnemyDragonfly", "Dragonfly", EventCategory::Enemy,
				_x("editor", "Flies around and attacks nearby players."),
				"Enemy/Dragonfly", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyFish, "EnemyFish", "Fish", EventCategory::Enemy,
				_x("editor", "Swims in water and attacks nearby players."),
				"Enemy/Fish", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyMadderHatter, "EnemyMadderHatter", "Madder Hatter", EventCategory::Enemy,
				_x("editor", "Walks around and spits at nearby players."),
				"Enemy/MadderHatter", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyRaven, "EnemyRaven", "Raven", EventCategory::Enemy,
				_x("editor", "Flies at nearby players."),
				"Enemy/Raven", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemySkeleton, "EnemySkeleton", "Skeleton", EventCategory::Enemy,
				_x("editor", "Walks back and forth."),
				"Enemy/Skeleton", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyTurtleTough, "EnemyTurtleTough", "Tough Turtle", EventCategory::Enemy,
				_x("editor", "Walks back and forth, takes more hits."),
				"Enemy/TurtleTough", std::uint32_t(AnimState::Walk), EventTraits::Object),
			DescribeEvent(EventType::EnemyTurtleTube, "EnemyTurtleTube", "Tube Turtle", EventCategory::Enemy,
				_x("editor", "Floats on the water surface in a tube."),
				"Enemy/TurtleTube", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyWitch, "EnemyWitch", "Witch", EventCategory::Enemy,
				_x("editor", "Flies at players and casts spells that turn them into frogs."),
				"Enemy/Witch", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::EnemyLizardFloat, "EnemyLizardFloat", "Floating Lizard", EventCategory::Enemy,
				_x("editor", "Flies on a copter and drops bombs, falls as a lizard when hit."),
				LizardFloatFields, "Enemy/LizardFloat", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, LizardFloatPreview),

			DescribeEvent(EventType::BossTweedle, "BossTweedle", "Tweedle", EventCategory::Boss,
				_x("editor", "Not implemented by the game."),
				"Boss/Tweedle", std::uint32_t(AnimState::Idle), EventTraits::Unused),
			DescribeEvent(EventType::BossBilsy, "BossBilsy", "Bilsy", EventCategory::Boss,
				_x("editor", "Teleports around and throws fireballs."),
				BilsyFields, "Boss/Bilsy", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, BilsyPreview),
			DescribeEvent(EventType::BossDevan, "BossDevan", "Devan", EventCategory::Boss,
				_x("editor", "Shoots at players, then turns into a flying demon."),
				BossFields, "Boss/Devan", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossQueen, "BossQueen", "Queen", EventCategory::Boss,
				_x("editor", "Can't be hurt, hits push her back until she falls off her ledge."),
				BossFields, "Boss/Queen", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, QueenPreview),
			DescribeEvent(EventType::BossRobot, "BossRobot", "Robot", EventCategory::Boss,
				// TRANSLATORS: Description of a boss event, Devan Remote is the name of another boss, names of bosses aren't translated
				_x("editor", "Hidden until a Devan Remote boss activates it."),
				"Boss/Robot", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, RobotPreview),
			DescribeEvent(EventType::BossUterus, "BossUterus", "Uterus", EventCategory::Boss,
				_x("editor", "Protected by shields, spawns crabs."),
				BossFields, "Boss/Uterus", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossTurtleTough, "BossTurtleTough", "Turtle Boss", EventCategory::Boss,
				_x("editor", "Walks towards players and throws its mace."),
				BossFields, "Boss/TurtleBoss", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, TurtleBossPreview),
			DescribeEvent(EventType::BossBubba, "BossBubba", "Bubba", EventCategory::Boss,
				_x("editor", "Spits fireballs and spins like a tornado."),
				BossFields, "Boss/Bubba", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::BossDevanRemote, "BossDevanRemote", "Devan Remote", EventCategory::Boss,
				// TRANSLATORS: Description of a boss event, Devan and Robot are names of bosses, names of bosses aren't translated
				_x("editor", "Devan controlling the Robot boss, which has to be placed in the level too."),
				DevanRemoteFields, "Boss/DevanRemote", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(-1, DevanRemotePreview),
			DescribeEvent(EventType::BossBolly, "BossBolly", "Bolly", EventCategory::Boss,
				_x("editor", "Flies after players with a spiked chain and fires rockets."),
				BossFields, "Boss/Bolly", std::uint32_t(AnimState::Idle), EventTraits::Object),

			DescribeEvent(EventType::TurtleShell, "TurtleShell", "Turtle Shell", EventCategory::Enemy,
				_x("editor", "Slides when shot and hurts enemies it hits."),
				TurtleShellFields, "Enemy/TurtleShell", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, TurtleShellPreview),
		};

		return Events;
	}
}

#endif
