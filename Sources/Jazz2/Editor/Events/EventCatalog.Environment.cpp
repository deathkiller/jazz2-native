#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// Lights

		constexpr EventParamField LightAmbientFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Intensity"), 0, 8, 0, 255, 128).WithDescription(NC_("editor", "0 is dark, 255 is fully lit")),
		};

		constexpr EventParamField LightSteadyFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Intensity"), 0, 8, 0, 255, 255).WithDescription(NC_("editor", "How much it lights up the darkness")),
			UIntField(NC_("editor", "Brightness"), 1, 8, 0, 255, 10).WithDescription(NC_("editor", "How much it glows above the normal light")),
			UIntField(NC_("editor", "Near Radius"), 2, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "Fully lit up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
			UIntField(NC_("editor", "Far Radius"), 4, 16, 0, 65535, 100).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "Fades out up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
		};

		constexpr EventParamField LightPulseFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Intensity"), 0, 8, 0, 255, 255).WithDescription(NC_("editor", "How much it lights up the darkness")),
			UIntField(NC_("editor", "Brightness"), 1, 8, 0, 255, 10).WithDescription(NC_("editor", "How much it glows above the normal light")),
			UIntField(NC_("editor", "Near Radius"), 2, 16, 0, 65535, 40).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "Fully lit up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
			// TRANSLATORS: Parameter of a pulsing light in the event properties, a noun, how much its radius grows and shrinks
			UIntField(NC_("editor", "Pulse"), 4, 16, 0, 65535, 20).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "How much the near radius grows and shrinks")),
			// TRANSLATORS: Parameter of a pulsing light in the event properties, a noun, the distance over which it fades out
			UIntField(NC_("editor", "Fade"), 6, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "Fades out over this distance past the near radius")),
			UIntField(NC_("editor", "Speed"), 8, 8, 0, 255, 6).WithDescription(NC_("editor", "How fast it pulses")),
			// TRANSLATORS: Parameter of an event in the event properties, short for synchronization, its starting phase
			UIntField(NC_("editor", "Sync"), 9, 8, 0, 255).WithDescription(NC_("editor", "Starting phase, in quarters of a pulse, 4 is a whole pulse")),
		};

		constexpr EventParamField LightFlickerFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Intensity"), 0, 8, 0, 255, 110).WithDescription(NC_("editor", "How much it lights up the darkness")),
			UIntField(NC_("editor", "Brightness"), 1, 8, 0, 255, 40).WithDescription(NC_("editor", "How much it glows above the normal light")),
			UIntField(NC_("editor", "Near Radius"), 2, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "Fully lit up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
			UIntField(NC_("editor", "Far Radius"), 4, 16, 0, 65535, 110).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "Flickers up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
		};

		// Environment

		// TRANSLATORS: Choices of a parameter of an event (colors of a spring)
		constexpr ParamOption SpringTypes[] = { { NC_("editor", "Red"), 0 }, { NC_("editor", "Green"), 1 }, { NC_("editor", "Blue"), 2 } };
		constexpr ParamOption SpringOrientations[] = {
			// TRANSLATORS: Choices of a parameter of an event (directions a spring launches the player in)
			{ NC_("editor", "Up"), 0 }, { NC_("editor", "Right"), 1 }, { NC_("editor", "Down"), 2 }, { NC_("editor", "Left"), 3 },
			// TRANSLATORS: Choices of a parameter of an event (direction of a spring), sideways away from the wall it's next to, picked automatically, or the opposite way
			{ NC_("editor", "Sideways (auto)"), 4 }, { NC_("editor", "Sideways (auto, reversed)"), 5 }
		};
		// Mirrored, it launches the other way; one turned at runtime turns away from the mirrored wall by itself
		constexpr std::int32_t SpringMirrorX[] = { 1, 3 };
		constexpr std::int32_t SpringMirrorY[] = { 0, 2 };
		constexpr EventParamField SpringFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Type"), 0, 8, SpringTypes),
			EnumField(NC_("editor", "Orientation"), 1, 8, SpringOrientations).WithDescription(NC_("editor", "Where it launches the player, auto turns away from the wall"))
				.MirroredXPairs(SpringMirrorX).MirroredYPairs(SpringMirrorY),
			BoolField(NC_("editor", "Gravity"), 2, 0, true).WithDescription(NC_("editor", "Falls onto the ground below it")),
			BoolField(NC_("editor", "Frozen"), 2, 1).WithDescription(NC_("editor", "Starts frozen, only fire melts it")),
			BoolField(NC_("editor", "Keep Speed X"), 2, 2).WithDescription(NC_("editor", "The player keeps their horizontal speed")),
			BoolField(NC_("editor", "Keep Speed Y"), 2, 3).WithDescription(NC_("editor", "The player keeps their vertical speed")),
			UIntField(NC_("editor", "Delay"), 3, 8, 0, 255).WithUnit(ParamUnit::Frames).WithDescription(NC_("editor", "Time before it can launch again, 0 is 6 frames")),
		};

		constexpr ParamOption BridgeTypes[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of bridges by their material or the theme of the level, Lab is short for laboratory)
			{ NC_("editor", "Rope"), 0 }, { NC_("editor", "Stone"), 1 }, { NC_("editor", "Vine"), 2 }, { NC_("editor", "Red Stone"), 3 }, { NC_("editor", "Log"), 4 }, { NC_("editor", "Gem"), 5 }, { NC_("editor", "Lab"), 6 }
		};
		constexpr EventParamField BridgeFields[] = {
			// From 0, the converter makes a bridge of no width of an original one that has none; stored in steps of 16 px,
			// see Bridge::OnActivatedAsync()
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Width"), 0, 16, 0, 65535, 8).WithUnit(ParamUnit::Pixels).WithScale(16).WithDescription(NC_("editor", "It extends to the right, in steps of 16 px"))
				.WithGizmo(FieldGizmo::WidthRight, 16),
			EnumField(NC_("editor", "Type"), 2, 8, BridgeTypes),
			UIntField(NC_("editor", "Toughness"), 3, 8, 0, 255, 4).WithDescription(NC_("editor", "It sags by the width divided by this, at most 38 px, 0 doesn't sag")),
		};

		constexpr ParamOption PlatformTypes[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of moving platforms, Carrotus is the name of a world, Lab is short for laboratory)
			{ NC_("editor", "Carrotus Fruit"), 1 }, { NC_("editor", "Ball"), 2 }, { NC_("editor", "Carrotus Grass"), 3 }, { NC_("editor", "Lab"), 4 },
			{ NC_("editor", "Sonic"), 5 }, { NC_("editor", "Spike"), 6 }, { NC_("editor", "Spike Ball"), 7 }
		};
		// A platform going round is at `cos, sin` of `sync * 90 - speed * time` degrees, see MovingPlatform::GetPhasePosition().
		// Mirrored, it turns the other way and starts at the mirrored quarter turn: 180 degrees minus it horizontally, minus
		// it vertically. A swinging one hangs from its mount, it isn't mirrored right vertically.
		constexpr std::int32_t ChainSyncMirrorX[] = { 0, 2 };
		constexpr std::int32_t ChainSyncMirrorY[] = { 1, 3 };
		constexpr EventParamField MovingPlatformFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Type"), 0, 8, PlatformTypes, 1),
			// TRANSLATORS: Parameter of an event in the event properties, short for synchronization, its starting phase
			UIntField(NC_("editor", "Sync"), 1, 8, 0, 255).WithDescription(NC_("editor", "Starting position, in quarter turns, 4 is a whole turn"))
				.MirroredXPairs(ChainSyncMirrorX).MirroredYPairs(ChainSyncMirrorY),
			IntField(NC_("editor", "Speed"), 2, 8, -128, 127, 8).WithDescription(NC_("editor", "How fast it turns, negative turns the other way"))
				.MirroredX(MirrorMode::Negate).MirroredY(MirrorMode::Negate),
			UIntField(NC_("editor", "Length"), 3, 8, 0, 255, 4).WithDescription(NC_("editor", "Number of chain links, 12 px each"))
				.WithGizmo(FieldGizmo::LengthDown, 12),
			// TRANSLATORS: Checkbox of an event in the event properties, it swings to and fro on its chain instead of going round
			BoolField(NC_("editor", "Swing"), 4).WithDescription(NC_("editor", "Swings to and fro instead of going round")),
		};

		// MovingPlatform::OnActivatedAsync(), by MovingPlatform::PlatformType
		constexpr EventPreviewCase MovingPlatformPreview[] = {
			{ 1, "MovingPlatform/CarrotusFruit", 0 }, { 2, "MovingPlatform/Ball", 0 }, { 3, "MovingPlatform/CarrotusGrass", 0 },
			{ 4, "MovingPlatform/Lab", 0 }, { 5, "MovingPlatform/Sonic", 0 }, { 6, "MovingPlatform/Spike", 0 },
			{ 7, "MovingPlatform/SpikeBall", 0 }, { EventPreviewCase::AnyValue, "MovingPlatform/CarrotusFruit", 0 }
		};

		// Bridge::OnActivatedAsync(), by Bridge::BridgeType, all pieces are drawn by EventPreviews as long as it is
		constexpr EventPreviewCase BridgePreview[] = {
			{ 0, "Bridge/Rope", std::uint32_t(AnimState::Default), false, 0, -6 }, { 1, "Bridge/Stone", std::uint32_t(AnimState::Default), false, 0, -6 },
			{ 2, "Bridge/Vine", std::uint32_t(AnimState::Default), false, 0, -6 }, { 3, "Bridge/StoneRed", std::uint32_t(AnimState::Default), false, 0, -6 },
			{ 4, "Bridge/Log", std::uint32_t(AnimState::Default), false, 0, -6 }, { 5, "Bridge/Gem", std::uint32_t(AnimState::Default), false, 0, -6 },
			{ 6, "Bridge/Lab", std::uint32_t(AnimState::Default), false, 0, -6 },
			{ EventPreviewCase::AnyValue, "Bridge/Rope", std::uint32_t(AnimState::Default), false, 0, -6 }
		};

		// TRANSLATORS: Choices of a parameter of an event (kinds of pushable boxes)
		constexpr ParamOption PushableBoxTypes[] = { { NC_("editor", "Rock"), 0 }, { NC_("editor", "Crate"), 1 } };
		constexpr EventParamField PushableBoxFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Type"), 0, 8, PushableBoxTypes),
		};
		constexpr EventPreviewCase PushableBoxPreview[] = {
			{ 1, "Object/PushBoxCrate", std::uint32_t(AnimState::Default) },
			{ EventPreviewCase::AnyValue, "Object/PushBoxRock", std::uint32_t(AnimState::Default) }
		};

		constexpr ParamOption PoleThemes[] = {
			// TRANSLATORS: Choices of a parameter of an event (looks of a pole by the theme of the level, Carrotus, Diamondus and Psych are names of worlds)
			{ NC_("editor", "Carrotus"), 0 }, { NC_("editor", "Diamondus"), 1 }, { NC_("editor", "Diamondus Tree"), 2 }, { NC_("editor", "Jungle"), 3 }, { NC_("editor", "Psych"), 4 }
		};
		constexpr EventParamField PoleFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Theme"), 0, 8, PoleThemes),
			IntField(NC_("editor", "Offset X"), 1, 16, -32768, 32767, 14).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "From the centre of the tile"))
				.MirroredX(MirrorMode::Negate).WithGizmo(FieldGizmo::OffsetX),
			// It stands on the ground, it isn't mirrored vertically
			IntField(NC_("editor", "Offset Y"), 3, 16, -32768, 32767, 22).WithUnit(ParamUnit::Pixels).WithDescription(NC_("editor", "From the centre of the tile"))
				.WithGizmo(FieldGizmo::OffsetY),
		};

		constexpr ParamOption SignExitTypes[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of level exits)
			{ NC_("editor", "None"), 0 }, { NC_("editor", "Normal"), 1 }, { NC_("editor", "Warp"), 2 }, { NC_("editor", "Bonus"), 3 }, { NC_("editor", "Secret"), 4 }, { NC_("editor", "Boss"), 5 }
		};
		constexpr EventParamField SignEOLFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Exit Type"), 0, 8, SignExitTypes, 1).WithDescription(NC_("editor", "Only a hint, a secret exit isn't treated as the finish of a race")),
		};

		// TRANSLATORS: Choices of a parameter of an event (colors of a moth)
		constexpr ParamOption MothColors[] = { { NC_("editor", "Pink"), 0 }, { NC_("editor", "Gray"), 1 }, { NC_("editor", "Green"), 2 }, { NC_("editor", "Purple"), 3 } };
		constexpr EventParamField MothFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Color"), 0, 8, MothColors),
		};
		// Moth uses the color as the animation, Moth.res has 4 of them
		constexpr EventPreviewCase MothPreview[] = {
			{ 0, "Object/Moth", 0 }, { 1, "Object/Moth", 1 }, { 2, "Object/Moth", 2 }, { 3, "Object/Moth", 3 }
		};

		// Puffing out to the left of its tile
		constexpr EventPreviewCase SteamNotePreview[] = {
			{ EventPreviewCase::AnyValue, "Object/SteamNote", std::uint32_t(AnimState::Default), false, -10, 0 }
		};

		// TRANSLATORS: Choices of a parameter of an event (looks of a bomb, Float Lizard is the name of an enemy, Xmas is short for Christmas)
		constexpr ParamOption BombLooks[] = { { NC_("editor", "Bomb"), 0 }, { NC_("editor", "Float Lizard"), 1 }, { NC_("editor", "Float Lizard (Xmas)"), 2 } };
		constexpr EventParamField BombFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, how it looks
			EnumField(NC_("editor", "Look"), 0, 8, BombLooks),
			BoolField(NC_("editor", "Facing Left"), 1)
				.MirroredX(MirrorMode::Toggle),
		};

		// TRANSLATORS: Choices of a parameter of an event (looks of a pinball bumper)
		constexpr ParamOption PinballBumperLooks[] = { { NC_("editor", "500 Points"), 0 }, { NC_("editor", "Carrot"), 1 } };
		constexpr EventParamField PinballBumperFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, how it looks
			EnumField(NC_("editor", "Look"), 0, 8, PinballBumperLooks).WithDescription(NC_("editor", "Both give 500 points")),
		};
		// PinballBumper uses the look as the animation, PinballBumper.res has 2 of them
		constexpr EventPreviewCase PinballBumperPreview[] = {
			{ 0, "Object/PinballBumper", 0 }, { 1, "Object/PinballBumper", 1 }
		};

		// TRANSLATORS: Choices of a parameter of an event (side a pinball paddle is mounted on)
		constexpr ParamOption PinballPaddleSides[] = { { NC_("editor", "Left"), 0 }, { NC_("editor", "Right"), 1 } };
		constexpr std::int32_t PinballPaddleMirrorX[] = { 0, 1 };
		constexpr EventParamField PinballPaddleFields[] = {
			// TRANSLATORS: Parameter of a pinball paddle in the event properties, followed by the side it's mounted on, Left or Right
			EnumField(NC_("editor", "Mounted On"), 0, 8, PinballPaddleSides).WithDescription(NC_("editor", "The side it's mounted on, it points to the other one"))
				.MirroredXPairs(PinballPaddleMirrorX),
		};
		// Facing left when it's mounted on the right, see PinballPaddle::OnActivatedAsync()
		constexpr EventPreviewCase PinballPaddlePreview[] = {
			{ 0, "Object/PinballPaddle", std::uint32_t(AnimState::Idle) },
			{ EventPreviewCase::AnyValue, "Object/PinballPaddle", std::uint32_t(AnimState::Idle), true }
		};

		// TRANSLATORS: Choices of a parameter of an event (team colors)
		constexpr ParamOption CtfTeams[] = { { NC_("editor", "Blue"), 0 }, { NC_("editor", "Red"), 1 }, { NC_("editor", "Green"), 2 }, { NC_("editor", "Yellow"), 3 } };
		constexpr EventParamField CtfBaseFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Team"), 0, 8, CtfTeams)
				.AsTeam(),
		};
		// MpLevelHandler::BuildCtfBases() ignores the base of an unknown team, and spawns the others at the left edge of the
		// tile, 8 px below its center
		constexpr EventPreviewCase CtfBasePreview[] = {
			{ 0, "Object/CtfBase", 0, false, -16, 8 }, { 1, "Object/CtfBase", 1, false, -16, 8 },
			{ 2, "Object/CtfBase", 2, false, -16, 8 }, { 3, "Object/CtfBase", 3, false, -16, 8 }
		};

		// TRANSLATORS: Choices of a parameter of an event (birds in a bird cage, Chuck and Birdy are names)
		constexpr ParamOption BirdTypes[] = { { NC_("editor", "Chuck (Red)"), 0 }, { NC_("editor", "Birdy (Yellow)"), 1 } };
		constexpr EventParamField BirdCageFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Bird"), 0, 8, BirdTypes),
			// TRANSLATORS: Checkbox of a bird cage in the event properties, an adjective, it starts already broken open
			BoolField(NC_("editor", "Open"), 1).WithDescription(NC_("editor", "Starts already broken open, without a bird")),
		};

		// As a moving platform, see SpikeBall::GetPhasePosition()
		constexpr EventParamField SpikeBallFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, short for synchronization, its starting phase
			UIntField(NC_("editor", "Sync"), 0, 8, 0, 255).WithDescription(NC_("editor", "Starting position, in quarter turns, 4 is a whole turn"))
				.MirroredXPairs(ChainSyncMirrorX).MirroredYPairs(ChainSyncMirrorY),
			IntField(NC_("editor", "Speed"), 1, 8, -128, 127, 8).WithDescription(NC_("editor", "How fast it turns, negative turns the other way"))
				.MirroredX(MirrorMode::Negate).MirroredY(MirrorMode::Negate),
			UIntField(NC_("editor", "Length"), 2, 8, 0, 255, 4).WithDescription(NC_("editor", "Number of chain links, 12 px each"))
				.WithGizmo(FieldGizmo::LengthDown, 12),
			// TRANSLATORS: Checkbox of an event in the event properties, it swings to and fro on its chain instead of going round
			BoolField(NC_("editor", "Swing"), 3).WithDescription(NC_("editor", "Swings to and fro instead of going round")),
			// TRANSLATORS: Checkbox of a spike ball in the event properties, a verb, it darkens the ball and chain as they turn away
			BoolField(NC_("editor", "Shade"), 4).WithDescription(NC_("editor", "Darkens the ball and chain as they turn away")),
		};

		constexpr EventParamField AirboardGeneratorFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Delay"), 0, 8, 0, 255, 30).WithUnit(ParamUnit::Seconds).WithDescription(NC_("editor", "Time before it gives another airboard")),
		};

		// Triggers set the rocks of their ID rolling, see RollingRock::OnTriggeredEvent()
		constexpr const char* RollingRockChannel = "rollingRock";

		constexpr EventParamField RollingRockFields[] = {
			// TRANSLATORS: Parameter of a rolling rock in the event properties, short for identifier, links it to its triggers
			UIntField(NC_("editor", "ID"), 0, 8, 0, 255).WithDescription(NC_("editor", "A rolling rock trigger with the same ID sets it rolling"))
				.WithLink(RollingRockChannel, EventLinkRole::Target),
			IntField(NC_("editor", "Speed X"), 1, 8, -128, 127).WithDescription(NC_("editor", "Starting speed in px per frame, negative rolls to the left"))
				.MirroredX(MirrorMode::Negate).WithDirection(DirectionAxis::X),
			IntField(NC_("editor", "Speed Y"), 2, 8, -128, 127).WithDescription(NC_("editor", "Starting vertical speed in px per frame"))
				.MirroredY(MirrorMode::Negate).WithDirection(DirectionAxis::Y),
		};

		constexpr EventParamField RollingRockTriggerFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, the identifier of the rolling rocks it sets rolling
			UIntField(NC_("editor", "Rock ID"), 0, 8, 0, 255).WithDescription(NC_("editor", "Sets the rolling rocks with this ID rolling"))
				.WithLink(RollingRockChannel, EventLinkRole::Source),
		};

		constexpr EventDescriptor Events[] = {
			// TRANSLATORS: Name of an event, an area that sets how dark the view is
			DescribeEvent(EventType::LightAmbient, "LightAmbient", NC_("editor", "Ambient Light"), EventCategory::Light,
				NC_("editor", "Sets how dark the view of the player who enters it is."), LightAmbientFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, a light
			DescribeEvent(EventType::LightSteady, "LightSteady", NC_("editor", "Steady Light"), EventCategory::Light,
				NC_("editor", "A light that shines steadily."), LightSteadyFields, nullptr, 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a light
			DescribeEvent(EventType::LightPulse, "LightPulse", NC_("editor", "Pulsing Light"), EventCategory::Light,
				NC_("editor", "A light whose radius pulses."), LightPulseFields, nullptr, 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a light
			DescribeEvent(EventType::LightFlicker, "LightFlicker", NC_("editor", "Flickering Light"), EventCategory::Light,
				NC_("editor", "A light that flickers like a flame."), LightFlickerFields, nullptr, 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a light that lights up the surroundings
			DescribeEvent(EventType::LightIlluminate, "LightIlluminate", NC_("editor", "Illuminate Surroundings"), EventCategory::Light,
				NC_("editor", "Lights up the surroundings, not supported yet."), nullptr, 0, EventTraits::Unused),

			// TRANSLATORS: Name of an event, a spring that launches the player, not the season
			DescribeEvent(EventType::Spring, "Spring", NC_("editor", "Spring"), EventCategory::Environment,
				NC_("editor", "Launches the player in its direction."), SpringFields, "Object/Spring", 0x400 /* Red, up */, EventTraits::Object),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::Bridge, "Bridge", NC_("editor", "Bridge"), EventCategory::Environment,
				NC_("editor", "A bridge that sags under the player."), BridgeFields, "Bridge/Rope", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(1, BridgePreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::MovingPlatform, "MovingPlatform", NC_("editor", "Moving Platform"), EventCategory::Environment,
				NC_("editor", "A platform going round or swinging on a chain."), MovingPlatformFields, "MovingPlatform/CarrotusFruit", 0, EventTraits::Object)
				.WithPreviewCases(0, MovingPlatformPreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::PushableBox, "PushableBox", NC_("editor", "Pushable Box"), EventCategory::Environment,
				NC_("editor", "A heavy rock or crate the player can push."), PushableBoxFields, "Object/PushBoxRock", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, PushableBoxPreview),
			// TRANSLATORS: Name of an event, a character, Eva is a name
			DescribeEvent(EventType::Eva, "Eva", NC_("editor", "Eva"), EventCategory::Environment,
				NC_("editor", "Turns a frog back to normal with a kiss."), "Object/Eva", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a pole that falls over when shot
			DescribeEvent(EventType::Pole, "Pole", NC_("editor", "Pole"), EventCategory::Environment,
				NC_("editor", "A pole that falls over when shot."), PoleFields, "Pole/Carrotus", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a sign marking the exit of a level
			DescribeEvent(EventType::SignEOL, "SignEOL", NC_("editor", "End of Level Sign"), EventCategory::Environment,
				NC_("editor", "A sign marking the exit, the level is ended by an area."), SignEOLFields, "Object/SignEol", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::Moth, "Moth", NC_("editor", "Moth"), EventCategory::Environment,
				NC_("editor", "A moth that flutters off when touched."), MothFields, "Object/Moth", 0, EventTraits::Object)
				.WithPreviewCases(0, MothPreview),
			// TRANSLATORS: Name of an event, a musical note made of steam that keeps puffing out
			DescribeEvent(EventType::SteamNote, "SteamNote", NC_("editor", "Steam Note"), EventCategory::Environment,
				NC_("editor", "A note of steam that keeps puffing out."), "Object/SteamNote", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(-1, SteamNotePreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::Bomb, "Bomb", NC_("editor", "Bomb"), EventCategory::Environment,
				NC_("editor", "A bomb that explodes shortly after it appears."), BombFields, "Object/Bomb", 2, EventTraits::Object),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::PinballBumper, "PinballBumper", NC_("editor", "Pinball Bumper"), EventCategory::Environment,
				NC_("editor", "Bounces the player away and gives 500 points."), PinballBumperFields, "Object/PinballBumper", 0, EventTraits::Object)
				.WithPreviewCases(0, PinballBumperPreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::PinballPaddle, "PinballPaddle", NC_("editor", "Pinball Paddle"), EventCategory::Environment,
				NC_("editor", "Flips the player who lands on it into the air."), PinballPaddleFields, "Object/PinballPaddle", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, PinballPaddlePreview),
			// TRANSLATORS: Name of an event, CTF is short for Capture the Flag
			DescribeEvent(EventType::CtfBase, "CtfBase", NC_("editor", "CTF Base"), EventCategory::Environment,
				NC_("editor", "Base of a team in Capture the Flag."), CtfBaseFields, "Object/CtfBase", 0, EventTraits::Object | EventTraits::MultiplayerOnly)
				.WithPreviewCases(0, CtfBasePreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::BirdCage, "BirdCage", NC_("editor", "Bird Cage"), EventCategory::Environment,
				NC_("editor", "A cage to break open, the bird inside then helps the player."), BirdCageFields, "Object/BirdCageChuck", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a collectible item
			DescribeEvent(EventType::Stopwatch, "Stopwatch", NC_("editor", "Stopwatch"), EventCategory::Collectible,
				NC_("editor", "Adds 10 seconds to the shield of the player."), "Collectible/Stopwatch", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a spiked ball spinning on a chain
			DescribeEvent(EventType::SpikeBall, "SpikeBall", NC_("editor", "Spike Ball"), EventCategory::Environment,
				NC_("editor", "A spiked ball spinning on a chain."), SpikeBallFields, "MovingPlatform/SpikeBall", 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a place that keeps giving the player a flying board
			DescribeEvent(EventType::AirboardGenerator, "AirboardGenerator", NC_("editor", "Airboard Generator"), EventCategory::Environment,
				NC_("editor", "Gives the player an airboard, then comes back after a while."), AirboardGeneratorFields, "Object/Airboard", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, short for helicopter, a propeller the player can grab to fly
			DescribeEvent(EventType::Copter, "Copter", NC_("editor", "Copter"), EventCategory::Environment,
				NC_("editor", "A copter the player can grab to fly for a while."), "Enemy/LizardFloat", 1, EventTraits::Object),
			// TRANSLATORS: Name of an event, a boulder
			DescribeEvent(EventType::RollingRock, "RollingRock", NC_("editor", "Rolling Rock"), EventCategory::Environment,
				NC_("editor", "A boulder that starts rolling when a trigger with its ID is touched."), RollingRockFields, "Object/RollingRock", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an area that sets rolling rocks rolling, trigger is a noun
			DescribeEvent(EventType::RollingRockTrigger, "RollingRockTrigger", NC_("editor", "Rolling Rock Trigger"), EventCategory::Environment,
				NC_("editor", "Sets the rolling rocks with its ID rolling when the player enters it."), RollingRockTriggerFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::SwingingVine, "SwingingVine", NC_("editor", "Swinging Vine"), EventCategory::Environment,
				NC_("editor", "A swinging vine the player can hang on."), "Object/SwingingVine", std::uint32_t(AnimState::Default), EventTraits::Object),
		};
	}

	ArrayView<const EventDescriptor> GetEnvironmentEvents()
	{
		return Events;
	}
}

#endif
