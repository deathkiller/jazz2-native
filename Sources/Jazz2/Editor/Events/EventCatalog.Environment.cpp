#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// Lights

		constexpr EventParamField LightAmbientFields[] = {
			UIntField(N_("Intensity"), 0, 8, 0, 255, 128).WithDescription(N_("0 is dark, 255 is fully lit")),
		};

		constexpr EventParamField LightSteadyFields[] = {
			UIntField(N_("Intensity"), 0, 8, 0, 255, 255).WithDescription(N_("How much it lights up the darkness")),
			UIntField(N_("Brightness"), 1, 8, 0, 255, 10).WithDescription(N_("How much it glows above the normal light")),
			UIntField(N_("Near Radius"), 2, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(N_("Fully lit up to this distance")),
			UIntField(N_("Far Radius"), 4, 16, 0, 65535, 100).WithUnit(ParamUnit::Pixels).WithDescription(N_("Fades out up to this distance")),
		};

		constexpr EventParamField LightPulseFields[] = {
			UIntField(N_("Intensity"), 0, 8, 0, 255, 255).WithDescription(N_("How much it lights up the darkness")),
			UIntField(N_("Brightness"), 1, 8, 0, 255, 10).WithDescription(N_("How much it glows above the normal light")),
			UIntField(N_("Near Radius"), 2, 16, 0, 65535, 40).WithUnit(ParamUnit::Pixels).WithDescription(N_("Fully lit up to this distance")),
			UIntField(N_("Pulse"), 4, 16, 0, 65535, 20).WithUnit(ParamUnit::Pixels).WithDescription(N_("How much the near radius grows and shrinks")),
			UIntField(N_("Fade"), 6, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(N_("Fades out over this distance past the near radius")),
			UIntField(N_("Speed"), 8, 8, 0, 255, 6).WithDescription(N_("How fast it pulses")),
			UIntField(N_("Sync"), 9, 8, 0, 255).WithDescription(N_("Starting phase, in quarters of a pulse, 4 is a whole pulse")),
		};

		constexpr EventParamField LightFlickerFields[] = {
			UIntField(N_("Intensity"), 0, 8, 0, 255, 110).WithDescription(N_("How much it lights up the darkness")),
			UIntField(N_("Brightness"), 1, 8, 0, 255, 40).WithDescription(N_("How much it glows above the normal light")),
			UIntField(N_("Near Radius"), 2, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(N_("Fully lit up to this distance")),
			UIntField(N_("Far Radius"), 4, 16, 0, 65535, 110).WithUnit(ParamUnit::Pixels).WithDescription(N_("Flickers up to this distance")),
		};

		// Environment

		constexpr ParamOption SpringTypes[] = { { N_("Red"), 0 }, { N_("Green"), 1 }, { N_("Blue"), 2 } };
		constexpr ParamOption SpringOrientations[] = {
			{ N_("Up"), 0 }, { N_("Right"), 1 }, { N_("Down"), 2 }, { N_("Left"), 3 },
			{ N_("Sideways (auto)"), 4 }, { N_("Sideways (auto, reversed)"), 5 }
		};
		constexpr EventParamField SpringFields[] = {
			EnumField(N_("Type"), 0, 8, SpringTypes),
			EnumField(N_("Orientation"), 1, 8, SpringOrientations).WithDescription(N_("Where it launches the player, auto turns away from the wall")),
			BoolField(N_("Gravity"), 2, 0, true).WithDescription(N_("Falls onto the ground below it")),
			BoolField(N_("Frozen"), 2, 1).WithDescription(N_("Starts frozen, only fire melts it")),
			BoolField(N_("Keep Speed X"), 2, 2).WithDescription(N_("The player keeps their horizontal speed")),
			BoolField(N_("Keep Speed Y"), 2, 3).WithDescription(N_("The player keeps their vertical speed")),
			UIntField(N_("Delay"), 3, 8, 0, 255).WithUnit(ParamUnit::Frames).WithDescription(N_("Time before it can launch again, 0 is 6 frames")),
		};

		constexpr ParamOption BridgeTypes[] = {
			{ N_("Rope"), 0 }, { N_("Stone"), 1 }, { N_("Vine"), 2 }, { N_("Red Stone"), 3 }, { N_("Log"), 4 }, { N_("Gem"), 5 }, { N_("Lab"), 6 }
		};
		constexpr EventParamField BridgeFields[] = {
			UIntField(N_("Width"), 0, 16, 1, 255, 8).WithDescription(N_("In steps of 16 px, it extends to the right")),
			EnumField(N_("Type"), 2, 8, BridgeTypes),
			UIntField(N_("Toughness"), 3, 8, 0, 255, 4).WithDescription(N_("It sags by the width divided by this, at most 38 px, 0 doesn't sag")),
		};

		constexpr ParamOption PlatformTypes[] = {
			{ N_("Carrotus Fruit"), 1 }, { N_("Ball"), 2 }, { N_("Carrotus Grass"), 3 }, { N_("Lab"), 4 },
			{ N_("Sonic"), 5 }, { N_("Spike"), 6 }, { N_("Spike Ball"), 7 }
		};
		constexpr EventParamField MovingPlatformFields[] = {
			EnumField(N_("Type"), 0, 8, PlatformTypes, 1),
			UIntField(N_("Sync"), 1, 8, 0, 255).WithDescription(N_("Starting position, in quarter turns, 4 is a whole turn")),
			IntField(N_("Speed"), 2, 8, -128, 127, 8).WithDescription(N_("How fast it turns, negative turns the other way")),
			UIntField(N_("Length"), 3, 8, 0, 255, 4).WithDescription(N_("Number of chain links, 12 px each")),
			BoolField(N_("Swing"), 4).WithDescription(N_("Swings to and fro instead of going round")),
		};

		constexpr ParamOption PushableBoxTypes[] = { { N_("Rock"), 0 }, { N_("Crate"), 1 } };
		constexpr EventParamField PushableBoxFields[] = {
			EnumField(N_("Type"), 0, 8, PushableBoxTypes),
		};

		constexpr ParamOption PoleThemes[] = {
			{ N_("Carrotus"), 0 }, { N_("Diamondus"), 1 }, { N_("Diamondus Tree"), 2 }, { N_("Jungle"), 3 }, { N_("Psych"), 4 }
		};
		constexpr EventParamField PoleFields[] = {
			EnumField(N_("Theme"), 0, 8, PoleThemes),
			IntField(N_("Offset X"), 1, 16, -32768, 32767, 14).WithUnit(ParamUnit::Pixels).WithDescription(N_("From the centre of the tile")),
			IntField(N_("Offset Y"), 3, 16, -32768, 32767, 22).WithUnit(ParamUnit::Pixels).WithDescription(N_("From the centre of the tile")),
		};

		constexpr ParamOption SignExitTypes[] = {
			{ N_("None"), 0 }, { N_("Normal"), 1 }, { N_("Warp"), 2 }, { N_("Bonus"), 3 }, { N_("Secret"), 4 }, { N_("Boss"), 5 }
		};
		constexpr EventParamField SignEOLFields[] = {
			EnumField(N_("Exit Type"), 0, 8, SignExitTypes, 1).WithDescription(N_("Only a hint, a secret exit isn't treated as the finish of a race")),
		};

		constexpr ParamOption MothColors[] = { { N_("Pink"), 0 }, { N_("Gray"), 1 }, { N_("Green"), 2 }, { N_("Purple"), 3 } };
		constexpr EventParamField MothFields[] = {
			EnumField(N_("Color"), 0, 8, MothColors),
		};

		constexpr ParamOption BombLooks[] = { { N_("Bomb"), 0 }, { N_("Float Lizard"), 1 }, { N_("Float Lizard (Xmas)"), 2 } };
		constexpr EventParamField BombFields[] = {
			EnumField(N_("Look"), 0, 8, BombLooks),
			BoolField(N_("Facing Left"), 1),
		};

		constexpr ParamOption PinballBumperLooks[] = { { N_("500 Points"), 0 }, { N_("Carrot"), 1 } };
		constexpr EventParamField PinballBumperFields[] = {
			EnumField(N_("Look"), 0, 8, PinballBumperLooks).WithDescription(N_("Both give 500 points")),
		};

		constexpr ParamOption PinballPaddleSides[] = { { N_("Left"), 0 }, { N_("Right"), 1 } };
		constexpr EventParamField PinballPaddleFields[] = {
			EnumField(N_("Mounted On"), 0, 8, PinballPaddleSides).WithDescription(N_("The side it's mounted on, it points to the other one")),
		};

		constexpr ParamOption CtfTeams[] = { { N_("Blue"), 0 }, { N_("Red"), 1 }, { N_("Green"), 2 }, { N_("Yellow"), 3 } };
		constexpr EventParamField CtfBaseFields[] = {
			EnumField(N_("Team"), 0, 8, CtfTeams),
		};

		constexpr ParamOption BirdTypes[] = { { N_("Chuck (Red)"), 0 }, { N_("Birdy (Yellow)"), 1 } };
		constexpr EventParamField BirdCageFields[] = {
			EnumField(N_("Bird"), 0, 8, BirdTypes),
			BoolField(N_("Open"), 1).WithDescription(N_("Starts already broken open, without a bird")),
		};

		constexpr EventParamField SpikeBallFields[] = {
			UIntField(N_("Sync"), 0, 8, 0, 255).WithDescription(N_("Starting position, in quarter turns, 4 is a whole turn")),
			IntField(N_("Speed"), 1, 8, -128, 127, 8).WithDescription(N_("How fast it turns, negative turns the other way")),
			UIntField(N_("Length"), 2, 8, 0, 255, 4).WithDescription(N_("Number of chain links, 12 px each")),
			BoolField(N_("Swing"), 3).WithDescription(N_("Swings to and fro instead of going round")),
			BoolField(N_("Shade"), 4).WithDescription(N_("Darkens the ball and chain as they turn away")),
		};

		constexpr EventParamField AirboardGeneratorFields[] = {
			UIntField(N_("Delay"), 0, 8, 0, 255, 30).WithUnit(ParamUnit::Seconds).WithDescription(N_("Time before it gives another airboard")),
		};

		constexpr EventParamField RollingRockFields[] = {
			UIntField(N_("ID"), 0, 8, 0, 255).WithDescription(N_("A rolling rock trigger with the same ID sets it rolling")),
			IntField(N_("Speed X"), 1, 8, -128, 127).WithDescription(N_("Starting speed in px per frame, negative rolls to the left")),
			IntField(N_("Speed Y"), 2, 8, -128, 127).WithDescription(N_("Starting vertical speed in px per frame")),
		};

		constexpr EventParamField RollingRockTriggerFields[] = {
			UIntField(N_("Rock ID"), 0, 8, 0, 255).WithDescription(N_("Sets the rolling rocks with this ID rolling")),
		};

		constexpr EventDescriptor Events[] = {
			DescribeEvent(EventType::LightAmbient, "LightAmbient", N_("Ambient Light"), EventCategory::Light,
				N_("Sets how dark the view of the player who enters it is."), LightAmbientFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::LightSteady, "LightSteady", N_("Steady Light"), EventCategory::Light,
				N_("A light that shines steadily."), LightSteadyFields, nullptr, 0, EventTraits::Object),
			DescribeEvent(EventType::LightPulse, "LightPulse", N_("Pulsing Light"), EventCategory::Light,
				N_("A light whose radius pulses."), LightPulseFields, nullptr, 0, EventTraits::Object),
			DescribeEvent(EventType::LightFlicker, "LightFlicker", N_("Flickering Light"), EventCategory::Light,
				N_("A light that flickers like a flame."), LightFlickerFields, nullptr, 0, EventTraits::Object),
			DescribeEvent(EventType::LightIlluminate, "LightIlluminate", N_("Illuminate Surroundings"), EventCategory::Light,
				N_("Lights up the surroundings, not supported yet."), nullptr, 0, EventTraits::Unused),

			DescribeEvent(EventType::Spring, "Spring", N_("Spring"), EventCategory::Environment,
				N_("Launches the player in its direction."), SpringFields, "Object/Spring", 0x400 /* Red, up */, EventTraits::Object),
			DescribeEvent(EventType::Bridge, "Bridge", N_("Bridge"), EventCategory::Environment,
				N_("A bridge that sags under the player."), BridgeFields, "Bridge/Rope", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::MovingPlatform, "MovingPlatform", N_("Moving Platform"), EventCategory::Environment,
				N_("A platform going round or swinging on a chain."), MovingPlatformFields, "MovingPlatform/CarrotusFruit", 0, EventTraits::Object),
			DescribeEvent(EventType::PushableBox, "PushableBox", N_("Pushable Box"), EventCategory::Environment,
				N_("A heavy rock or crate the player can push."), PushableBoxFields, "Object/PushBoxRock", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Eva, "Eva", N_("Eva"), EventCategory::Environment,
				N_("Turns a frog back to normal with a kiss."), "Object/Eva", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::Pole, "Pole", N_("Pole"), EventCategory::Environment,
				N_("A pole that falls over when shot."), PoleFields, "Pole/Carrotus", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::SignEOL, "SignEOL", N_("End of Level Sign"), EventCategory::Environment,
				N_("A sign marking the exit, the level is ended by an area."), SignEOLFields, "Object/SignEol", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Moth, "Moth", N_("Moth"), EventCategory::Environment,
				N_("A moth that flutters off when touched."), MothFields, "Object/Moth", 0, EventTraits::Object),
			DescribeEvent(EventType::SteamNote, "SteamNote", N_("Steam Note"), EventCategory::Environment,
				N_("A note of steam that keeps puffing out."), "Object/SteamNote", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Bomb, "Bomb", N_("Bomb"), EventCategory::Environment,
				N_("A bomb that explodes shortly after it appears."), BombFields, "Object/Bomb", 2, EventTraits::Object),
			DescribeEvent(EventType::PinballBumper, "PinballBumper", N_("Pinball Bumper"), EventCategory::Environment,
				N_("Bounces the player away and gives 500 points."), PinballBumperFields, "Object/PinballBumper", 0, EventTraits::Object),
			DescribeEvent(EventType::PinballPaddle, "PinballPaddle", N_("Pinball Paddle"), EventCategory::Environment,
				N_("Flips the player who lands on it into the air."), PinballPaddleFields, "Object/PinballPaddle", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::CtfBase, "CtfBase", N_("CTF Base"), EventCategory::Environment,
				N_("Base of a team in Capture the Flag."), CtfBaseFields, "Object/CtfBase", 0, EventTraits::Object | EventTraits::MultiplayerOnly),
			DescribeEvent(EventType::BirdCage, "BirdCage", N_("Bird Cage"), EventCategory::Environment,
				N_("A cage to break open, the bird inside then helps the player."), BirdCageFields, "Object/BirdCageChuck", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::Stopwatch, "Stopwatch", N_("Stopwatch"), EventCategory::Collectible,
				N_("Adds 10 seconds to the shield of the player."), "Collectible/Stopwatch", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::SpikeBall, "SpikeBall", N_("Spike Ball"), EventCategory::Environment,
				N_("A spiked ball spinning on a chain."), SpikeBallFields, "MovingPlatform/SpikeBall", 0, EventTraits::Object),
			DescribeEvent(EventType::AirboardGenerator, "AirboardGenerator", N_("Airboard Generator"), EventCategory::Environment,
				N_("Gives the player an airboard, then comes back after a while."), AirboardGeneratorFields, "Object/Airboard", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Copter, "Copter", N_("Copter"), EventCategory::Environment,
				N_("A copter the player can grab to fly for a while."), "Enemy/LizardFloat", 1, EventTraits::Object),
			DescribeEvent(EventType::RollingRock, "RollingRock", N_("Rolling Rock"), EventCategory::Environment,
				N_("A boulder that starts rolling when a trigger with its ID is touched."), RollingRockFields, "Object/RollingRock", std::uint32_t(AnimState::Idle), EventTraits::Object),
			DescribeEvent(EventType::RollingRockTrigger, "RollingRockTrigger", N_("Rolling Rock Trigger"), EventCategory::Environment,
				N_("Sets the rolling rocks with its ID rolling when the player enters it."), RollingRockTriggerFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::SwingingVine, "SwingingVine", N_("Swinging Vine"), EventCategory::Environment,
				N_("A swinging vine the player can hang on."), "Object/SwingingVine", std::uint32_t(AnimState::Default), EventTraits::Object),
		};
	}

	ArrayView<const EventDescriptor> GetEnvironmentEvents()
	{
		return Events;
	}
}

#endif
