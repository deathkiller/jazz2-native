#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"
#include "../../../nCine/I18n.h"

using namespace nCine;

namespace Jazz2::Editor::EventCatalogParts
{
	ArrayView<const EventDescriptor> GetEnvironmentEvents()
	{
		// Built on the first call, after the language is loaded, only what's translated here by _x() is translated, see CatalogText
		// Lights

		static const EventParamField LightAmbientFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Intensity"), 0, 8, 0, 255, 128).WithDescription(_x("editor", "0 is dark, 255 is fully lit")),
		};

		static const EventParamField LightSteadyFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Intensity"), 0, 8, 0, 255, 255).WithDescription(_x("editor", "How much it lights up the darkness")),
			UIntField(_x("editor", "Brightness"), 1, 8, 0, 255, 10).WithDescription(_x("editor", "How much it glows above the normal light")),
			UIntField(_x("editor", "Near Radius"), 2, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "Fully lit up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
			UIntField(_x("editor", "Far Radius"), 4, 16, 0, 65535, 100).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "Fades out up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
		};

		static const EventParamField LightPulseFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Intensity"), 0, 8, 0, 255, 255).WithDescription(_x("editor", "How much it lights up the darkness")),
			UIntField(_x("editor", "Brightness"), 1, 8, 0, 255, 10).WithDescription(_x("editor", "How much it glows above the normal light")),
			UIntField(_x("editor", "Near Radius"), 2, 16, 0, 65535, 40).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "Fully lit up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
			// TRANSLATORS: Parameter of a pulsing light in the event properties, a noun, how much its radius grows and shrinks
			UIntField(_x("editor", "Pulse"), 4, 16, 0, 65535, 20).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "How much the near radius grows and shrinks")),
			// TRANSLATORS: Parameter of a pulsing light in the event properties, a noun, the distance over which it fades out
			UIntField(_x("editor", "Fade"), 6, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "Fades out over this distance past the near radius")),
			UIntField(_x("editor", "Speed"), 8, 8, 0, 255, 6).WithDescription(_x("editor", "How fast it pulses")),
			// TRANSLATORS: Parameter of an event in the event properties, short for synchronization, its starting phase
			UIntField(_x("editor", "Sync"), 9, 8, 0, 255).WithDescription(_x("editor", "Starting phase, in quarters of a pulse, 4 is a whole pulse")),
		};

		static const EventParamField LightFlickerFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Intensity"), 0, 8, 0, 255, 110).WithDescription(_x("editor", "How much it lights up the darkness")),
			UIntField(_x("editor", "Brightness"), 1, 8, 0, 255, 40).WithDescription(_x("editor", "How much it glows above the normal light")),
			UIntField(_x("editor", "Near Radius"), 2, 16, 0, 65535, 60).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "Fully lit up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
			UIntField(_x("editor", "Far Radius"), 4, 16, 0, 65535, 110).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "Flickers up to this distance"))
				.WithGizmo(FieldGizmo::Radius),
		};

		// Environment

		// TRANSLATORS: Choices of a parameter of an event (colors of a spring)
		static const ParamOption SpringTypes[] = { { _x("editor", "Red"), 0 }, { _x("editor", "Green"), 1 }, { _x("editor", "Blue"), 2 } };
		static const ParamOption SpringOrientations[] = {
			// TRANSLATORS: Choices of a parameter of an event (directions a spring launches the player in)
			{ _x("editor", "Up"), 0 }, { _x("editor", "Right"), 1 }, { _x("editor", "Down"), 2 }, { _x("editor", "Left"), 3 },
			// TRANSLATORS: Choices of a parameter of an event (direction of a spring), sideways away from the wall it's next to, picked automatically, or the opposite way
			{ _x("editor", "Sideways (auto)"), 4 }, { _x("editor", "Sideways (auto, reversed)"), 5 }
		};
		// Mirrored, it launches the other way; one turned at runtime turns away from the mirrored wall by itself
		static const std::int32_t SpringMirrorX[] = { 1, 3 };
		static const std::int32_t SpringMirrorY[] = { 0, 2 };
		static const EventParamField SpringFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Type"), 0, 8, SpringTypes),
			EnumField(_x("editor", "Orientation"), 1, 8, SpringOrientations).WithDescription(_x("editor", "Where it launches the player, auto turns away from the wall"))
				.MirroredXPairs(SpringMirrorX).MirroredYPairs(SpringMirrorY),
			BoolField(_x("editor", "Gravity"), 2, 0, true).WithDescription(_x("editor", "Falls onto the ground below it")),
			BoolField(_x("editor", "Frozen"), 2, 1).WithDescription(_x("editor", "Starts frozen, only fire melts it")),
			BoolField(_x("editor", "Keep Speed X"), 2, 2).WithDescription(_x("editor", "The player keeps their horizontal speed")),
			BoolField(_x("editor", "Keep Speed Y"), 2, 3).WithDescription(_x("editor", "The player keeps their vertical speed")),
			UIntField(_x("editor", "Delay"), 3, 8, 0, 255).WithUnit(ParamUnit::Frames).WithDescription(_x("editor", "Time before it can launch again, 0 is 6 frames")),
		};

		// Kinds of bridges aren't translated
		static const ParamOption BridgeTypes[] = {
			{ "Rope", 0 }, { "Stone", 1 }, { "Vine", 2 }, { "Red Stone", 3 }, { "Log", 4 }, { "Gem", 5 }, { "Lab", 6 }
		};
		static const EventParamField BridgeFields[] = {
			// From 0, the converter makes a bridge of no width of an original one that has none; stored in steps of 16 px,
			// see Bridge::OnActivatedAsync()
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Width"), 0, 16, 0, 65535, 8).WithUnit(ParamUnit::Pixels).WithScale(16).WithDescription(_x("editor", "It extends to the right, in steps of 16 px"))
				.WithGizmo(FieldGizmo::WidthRight, 16),
			EnumField(_x("editor", "Type"), 2, 8, BridgeTypes),
			UIntField(_x("editor", "Toughness"), 3, 8, 0, 255, 4).WithDescription(_x("editor", "It sags by the width divided by this, at most 38 px, 0 doesn't sag")),
		};

		// Kinds of moving platforms aren't translated
		static const ParamOption PlatformTypes[] = {
			{ "Carrotus Fruit", 1 }, { "Ball", 2 }, { "Carrotus Grass", 3 }, { "Lab", 4 },
			{ "Sonic", 5 }, { "Spike", 6 }, { "Spike Ball", 7 }
		};
		// A platform going round is at `cos, sin` of `sync * 90 - speed * time` degrees, see MovingPlatform::GetPhasePosition().
		// Mirrored, it turns the other way and starts at the mirrored quarter turn: 180 degrees minus it horizontally, minus
		// it vertically. A swinging one hangs from its mount, it isn't mirrored right vertically.
		static const std::int32_t ChainSyncMirrorX[] = { 0, 2 };
		static const std::int32_t ChainSyncMirrorY[] = { 1, 3 };
		static const EventParamField MovingPlatformFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Type"), 0, 8, PlatformTypes, 1),
			// TRANSLATORS: Parameter of an event in the event properties, short for synchronization, its starting phase
			UIntField(_x("editor", "Sync"), 1, 8, 0, 255).WithDescription(_x("editor", "Starting position, in quarter turns, 4 is a whole turn"))
				.MirroredXPairs(ChainSyncMirrorX).MirroredYPairs(ChainSyncMirrorY),
			IntField(_x("editor", "Speed"), 2, 8, -128, 127, 8).WithDescription(_x("editor", "How fast it turns, negative turns the other way"))
				.MirroredX(MirrorMode::Negate).MirroredY(MirrorMode::Negate),
			UIntField(_x("editor", "Length"), 3, 8, 0, 255, 4).WithDescription(_x("editor", "Number of chain links, 25 px each"))
				.WithGizmo(FieldGizmo::LengthDown, 25),
			// TRANSLATORS: Checkbox of an event in the event properties, it swings to and fro on its chain instead of going round
			BoolField(_x("editor", "Swing"), 4).WithDescription(_x("editor", "Swings to and fro instead of going round")),
		};

		// MovingPlatform::OnActivatedAsync(), by MovingPlatform::PlatformType
		static const EventPreviewCase MovingPlatformPreview[] = {
			{ 1, "MovingPlatform/CarrotusFruit", 0 }, { 2, "MovingPlatform/Ball", 0 }, { 3, "MovingPlatform/CarrotusGrass", 0 },
			{ 4, "MovingPlatform/Lab", 0 }, { 5, "MovingPlatform/Sonic", 0 }, { 6, "MovingPlatform/Spike", 0 },
			{ 7, "MovingPlatform/SpikeBall", 0 }, { EventPreviewCase::AnyValue, "MovingPlatform/CarrotusFruit", 0 }
		};

		// Bridge::OnActivatedAsync(), by Bridge::BridgeType, all pieces are drawn by EventPreviews as long as it is
		static const EventPreviewCase BridgePreview[] = {
			{ 0, "Bridge/Rope", std::uint32_t(AnimState::Default), false, 0, -6 }, { 1, "Bridge/Stone", std::uint32_t(AnimState::Default), false, 0, -6 },
			{ 2, "Bridge/Vine", std::uint32_t(AnimState::Default), false, 0, -6 }, { 3, "Bridge/StoneRed", std::uint32_t(AnimState::Default), false, 0, -6 },
			{ 4, "Bridge/Log", std::uint32_t(AnimState::Default), false, 0, -6 }, { 5, "Bridge/Gem", std::uint32_t(AnimState::Default), false, 0, -6 },
			{ 6, "Bridge/Lab", std::uint32_t(AnimState::Default), false, 0, -6 },
			{ EventPreviewCase::AnyValue, "Bridge/Rope", std::uint32_t(AnimState::Default), false, 0, -6 }
		};

		// TRANSLATORS: Choices of a parameter of an event (kinds of pushable boxes)
		static const ParamOption PushableBoxTypes[] = { { _x("editor", "Rock"), 0 }, { _x("editor", "Crate"), 1 } };
		static const EventParamField PushableBoxFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Type"), 0, 8, PushableBoxTypes),
		};
		static const EventPreviewCase PushableBoxPreview[] = {
			{ 1, "Object/PushBoxCrate", std::uint32_t(AnimState::Default) },
			{ EventPreviewCase::AnyValue, "Object/PushBoxRock", std::uint32_t(AnimState::Default) }
		};

		// Looks of poles by the theme of the level aren't translated
		static const ParamOption PoleThemes[] = {
			{ "Carrotus", 0 }, { "Diamondus", 1 }, { "Diamondus Tree", 2 }, { "Jungle", 3 }, { "Psych", 4 }
		};
		static const EventParamField PoleFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Theme"), 0, 8, PoleThemes),
			IntField(_x("editor", "Offset X"), 1, 16, -32768, 32767, 14).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "From the centre of the tile"))
				.MirroredX(MirrorMode::Negate).WithGizmo(FieldGizmo::OffsetX),
			// It stands on the ground, it isn't mirrored vertically
			IntField(_x("editor", "Offset Y"), 3, 16, -32768, 32767, 22).WithUnit(ParamUnit::Pixels).WithDescription(_x("editor", "From the centre of the tile"))
				.WithGizmo(FieldGizmo::OffsetY),
		};

		static const ParamOption SignExitTypes[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of level exits)
			{ _x("editor", "None"), 0 }, { _x("editor", "Normal"), 1 }, { _x("editor", "Warp"), 2 }, { _x("editor", "Bonus"), 3 }, { _x("editor", "Secret"), 4 }, { _x("editor", "Boss"), 5 }
		};
		static const EventParamField SignEOLFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Exit Type"), 0, 8, SignExitTypes, 1).WithDescription(_x("editor", "Only a hint, a secret exit isn't treated as the finish of a race")),
		};

		// TRANSLATORS: Choices of a parameter of an event (colors of a moth)
		static const ParamOption MothColors[] = { { _x("editor", "Pink"), 0 }, { _x("editor", "Gray"), 1 }, { _x("editor", "Green"), 2 }, { _x("editor", "Purple"), 3 } };
		static const EventParamField MothFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Color"), 0, 8, MothColors),
		};
		// Moth uses the color as the animation, Moth.res has 4 of them
		static const EventPreviewCase MothPreview[] = {
			{ 0, "Object/Moth", 0 }, { 1, "Object/Moth", 1 }, { 2, "Object/Moth", 2 }, { 3, "Object/Moth", 3 }
		};

		// Puffing out to the left of its tile
		static const EventPreviewCase SteamNotePreview[] = {
			{ EventPreviewCase::AnyValue, "Object/SteamNote", std::uint32_t(AnimState::Default), false, -10, 0 }
		};

		// Names of enemies aren't translated
		// TRANSLATORS: Choice of a parameter of an event, a bomb looks as a bomb, the other choices are names of enemies
		static const ParamOption BombLooks[] = { { _x("editor", "Bomb"), 0 }, { "Float Lizard", 1 }, { "Float Lizard (Xmas)", 2 } };
		static const EventParamField BombFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, how it looks
			EnumField(_x("editor", "Look"), 0, 8, BombLooks),
			BoolField(_x("editor", "Facing Left"), 1)
				.MirroredX(MirrorMode::Toggle),
		};

		// TRANSLATORS: Choices of a parameter of an event (looks of a pinball bumper)
		static const ParamOption PinballBumperLooks[] = { { _x("editor", "500 Points"), 0 }, { _x("editor", "Carrot"), 1 } };
		static const EventParamField PinballBumperFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, how it looks
			EnumField(_x("editor", "Look"), 0, 8, PinballBumperLooks).WithDescription(_x("editor", "Both give 500 points")),
		};
		// PinballBumper uses the look as the animation, PinballBumper.res has 2 of them
		static const EventPreviewCase PinballBumperPreview[] = {
			{ 0, "Object/PinballBumper", 0 }, { 1, "Object/PinballBumper", 1 }
		};

		// TRANSLATORS: Choices of a parameter of an event (side a pinball paddle is mounted on)
		static const ParamOption PinballPaddleSides[] = { { _x("editor", "Left"), 0 }, { _x("editor", "Right"), 1 } };
		static const std::int32_t PinballPaddleMirrorX[] = { 0, 1 };
		static const EventParamField PinballPaddleFields[] = {
			// TRANSLATORS: Parameter of a pinball paddle in the event properties, followed by the side it's mounted on, Left or Right
			EnumField(_x("editor", "Mounted On"), 0, 8, PinballPaddleSides).WithDescription(_x("editor", "The side it's mounted on, it points to the other one"))
				.MirroredXPairs(PinballPaddleMirrorX),
		};
		// Facing left when it's mounted on the right, see PinballPaddle::OnActivatedAsync()
		static const EventPreviewCase PinballPaddlePreview[] = {
			{ 0, "Object/PinballPaddle", std::uint32_t(AnimState::Idle) },
			{ EventPreviewCase::AnyValue, "Object/PinballPaddle", std::uint32_t(AnimState::Idle), true }
		};

		// TRANSLATORS: Choices of a parameter of an event (team colors)
		static const ParamOption CtfTeams[] = { { _x("editor", "Blue"), 0 }, { _x("editor", "Red"), 1 }, { _x("editor", "Green"), 2 }, { _x("editor", "Yellow"), 3 } };
		static const EventParamField CtfBaseFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Team"), 0, 8, CtfTeams)
				.AsTeam(),
		};
		// MpLevelHandler::BuildCtfBases() ignores the base of an unknown team, and spawns the others at the left edge of the
		// tile, 8 px below its center
		static const EventPreviewCase CtfBasePreview[] = {
			{ 0, "Object/CtfBase", 0, false, -16, 8 }, { 1, "Object/CtfBase", 1, false, -16, 8 },
			{ 2, "Object/CtfBase", 2, false, -16, 8 }, { 3, "Object/CtfBase", 3, false, -16, 8 }
		};

		// Names of birds aren't translated
		static const ParamOption BirdTypes[] = { { "Chuck (Red)", 0 }, { "Birdy (Yellow)", 1 } };
		static const EventParamField BirdCageFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Bird"), 0, 8, BirdTypes),
			// TRANSLATORS: Checkbox of a bird cage in the event properties, an adjective, it starts already broken open
			BoolField(_x("editor", "Open"), 1).WithDescription(_x("editor", "Starts already broken open, without a bird")),
		};

		// As a moving platform, see SpikeBall::GetPhasePosition()
		static const EventParamField SpikeBallFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, short for synchronization, its starting phase
			UIntField(_x("editor", "Sync"), 0, 8, 0, 255).WithDescription(_x("editor", "Starting position, in quarter turns, 4 is a whole turn"))
				.MirroredXPairs(ChainSyncMirrorX).MirroredYPairs(ChainSyncMirrorY),
			IntField(_x("editor", "Speed"), 1, 8, -128, 127, 8).WithDescription(_x("editor", "How fast it turns, negative turns the other way"))
				.MirroredX(MirrorMode::Negate).MirroredY(MirrorMode::Negate),
			UIntField(_x("editor", "Length"), 2, 8, 0, 255, 4).WithDescription(_x("editor", "Number of chain links, 12 px each"))
				.WithGizmo(FieldGizmo::LengthDown, 12),
			// TRANSLATORS: Checkbox of an event in the event properties, it swings to and fro on its chain instead of going round
			BoolField(_x("editor", "Swing"), 3).WithDescription(_x("editor", "Swings to and fro instead of going round")),
			// TRANSLATORS: Checkbox of a spike ball in the event properties, a verb, it darkens the ball and chain as they turn away
			BoolField(_x("editor", "Shade"), 4).WithDescription(_x("editor", "Darkens the ball and chain as they turn away")),
		};

		static const EventParamField AirboardGeneratorFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Delay"), 0, 8, 0, 255, 30).WithUnit(ParamUnit::Seconds).WithDescription(_x("editor", "Time before it gives another airboard")),
		};

		// Triggers set the rocks of their ID rolling, see RollingRock::OnTriggeredEvent()
		constexpr const char* RollingRockChannel = "RollingRock";

		static const EventParamField RollingRockFields[] = {
			// TRANSLATORS: Parameter of a rolling rock in the event properties, short for identifier, links it to its triggers
			UIntField(_x("editor", "ID"), 0, 8, 0, 255).WithDescription(_x("editor", "A rolling rock trigger with the same ID sets it rolling"))
				.WithLink(RollingRockChannel, EventLinkRole::Target),
			IntField(_x("editor", "Speed X"), 1, 8, -128, 127).WithDescription(_x("editor", "Starting speed in px per frame, negative rolls to the left"))
				.MirroredX(MirrorMode::Negate).WithDirection(DirectionAxis::X),
			IntField(_x("editor", "Speed Y"), 2, 8, -128, 127).WithDescription(_x("editor", "Starting vertical speed in px per frame"))
				.MirroredY(MirrorMode::Negate).WithDirection(DirectionAxis::Y),
		};

		static const EventParamField RollingRockTriggerFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, the identifier of the rolling rocks it sets rolling
			UIntField(_x("editor", "Rock ID"), 0, 8, 0, 255).WithDescription(_x("editor", "Sets the rolling rocks with this ID rolling"))
				.WithLink(RollingRockChannel, EventLinkRole::Source),
		};

		static const EventDescriptor Events[] = {
			// TRANSLATORS: Name of an event, an area that sets how dark the view is
			DescribeEvent(EventType::LightAmbient, "LightAmbient", _x("editor", "Ambient Light"), EventCategory::Light,
				_x("editor", "Sets how dark the view of the player who enters it is."), LightAmbientFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, a light
			DescribeEvent(EventType::LightSteady, "LightSteady", _x("editor", "Steady Light"), EventCategory::Light,
				_x("editor", "A light that shines steadily."), LightSteadyFields, nullptr, 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a light
			DescribeEvent(EventType::LightPulse, "LightPulse", _x("editor", "Pulsing Light"), EventCategory::Light,
				_x("editor", "A light whose radius pulses."), LightPulseFields, nullptr, 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a light
			DescribeEvent(EventType::LightFlicker, "LightFlicker", _x("editor", "Flickering Light"), EventCategory::Light,
				_x("editor", "A light that flickers like a flame."), LightFlickerFields, nullptr, 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a light that lights up the surroundings
			DescribeEvent(EventType::LightIlluminate, "LightIlluminate", _x("editor", "Illuminate Surroundings"), EventCategory::Light,
				_x("editor", "Lights up the surroundings, not supported yet."), nullptr, 0, EventTraits::Unused),

			// TRANSLATORS: Name of an event, a spring that launches the player, not the season
			DescribeEvent(EventType::Spring, "Spring", _x("editor", "Spring"), EventCategory::Environment,
				_x("editor", "Launches the player in its direction."), SpringFields, "Object/Spring", 0x400 /* Red, up */, EventTraits::Object),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::Bridge, "Bridge", _x("editor", "Bridge"), EventCategory::Environment,
				_x("editor", "A bridge that sags under the player."), BridgeFields, "Bridge/Rope", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(1, BridgePreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::MovingPlatform, "MovingPlatform", _x("editor", "Moving Platform"), EventCategory::Environment,
				_x("editor", "A platform going round or swinging on a chain."), MovingPlatformFields, "MovingPlatform/CarrotusFruit", 0, EventTraits::Object)
				.WithPreviewCases(0, MovingPlatformPreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::PushableBox, "PushableBox", _x("editor", "Pushable Box"), EventCategory::Environment,
				_x("editor", "A heavy rock or crate the player can push."), PushableBoxFields, "Object/PushBoxRock", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(0, PushableBoxPreview),
			DescribeEvent(EventType::Eva, "Eva", "Eva", EventCategory::Environment,
				_x("editor", "Turns a frog back to normal with a kiss."), "Object/Eva", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a pole that falls over when shot
			DescribeEvent(EventType::Pole, "Pole", _x("editor", "Pole"), EventCategory::Environment,
				_x("editor", "A pole that falls over when shot."), PoleFields, "Pole/Carrotus", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a sign marking the exit of a level
			DescribeEvent(EventType::SignEOL, "SignEOL", _x("editor", "End of Level Sign"), EventCategory::Environment,
				_x("editor", "A sign marking the exit, the level is ended by an area."), SignEOLFields, "Object/SignEol", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::Moth, "Moth", "Moth", EventCategory::Environment,
				_x("editor", "A moth that flutters off when touched."), MothFields, "Object/Moth", 0, EventTraits::Object)
				.WithPreviewCases(0, MothPreview),
			// TRANSLATORS: Name of an event, a musical note made of steam that keeps puffing out
			DescribeEvent(EventType::SteamNote, "SteamNote", _x("editor", "Steam Note"), EventCategory::Environment,
				_x("editor", "A note of steam that keeps puffing out."), "Object/SteamNote", std::uint32_t(AnimState::Default), EventTraits::Object)
				.WithPreviewCases(-1, SteamNotePreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::Bomb, "Bomb", _x("editor", "Bomb"), EventCategory::Environment,
				_x("editor", "A bomb that explodes shortly after it appears."), BombFields, "Object/Bomb", 2, EventTraits::Object),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::PinballBumper, "PinballBumper", _x("editor", "Pinball Bumper"), EventCategory::Environment,
				_x("editor", "Bounces the player away and gives 500 points."), PinballBumperFields, "Object/PinballBumper", 0, EventTraits::Object)
				.WithPreviewCases(0, PinballBumperPreview),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::PinballPaddle, "PinballPaddle", _x("editor", "Pinball Paddle"), EventCategory::Environment,
				_x("editor", "Flips the player who lands on it into the air."), PinballPaddleFields, "Object/PinballPaddle", std::uint32_t(AnimState::Idle), EventTraits::Object)
				.WithPreviewCases(0, PinballPaddlePreview),
			// TRANSLATORS: Name of an event, CTF is short for Capture the Flag
			DescribeEvent(EventType::CtfBase, "CtfBase", _x("editor", "CTF Base"), EventCategory::Environment,
				_x("editor", "Base of a team in Capture the Flag."), CtfBaseFields, "Object/CtfBase", 0, EventTraits::Object | EventTraits::MultiplayerOnly)
				.WithPreviewCases(0, CtfBasePreview),
			DescribeEvent(EventType::BirdCage, "BirdCage", "Bird Cage", EventCategory::Environment,
				_x("editor", "A cage to break open, the bird inside then helps the player."), BirdCageFields, "Object/BirdCageChuck", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, a collectible item
			DescribeEvent(EventType::Stopwatch, "Stopwatch", _x("editor", "Stopwatch"), EventCategory::Collectible,
				_x("editor", "Adds 10 seconds to the shield of the player."), "Collectible/Stopwatch", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a spiked ball spinning on a chain
			DescribeEvent(EventType::SpikeBall, "SpikeBall", _x("editor", "Spike Ball"), EventCategory::Environment,
				_x("editor", "A spiked ball spinning on a chain."), SpikeBallFields, "MovingPlatform/SpikeBall", 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a place that keeps giving the player a flying board
			DescribeEvent(EventType::AirboardGenerator, "AirboardGenerator", _x("editor", "Airboard Generator"), EventCategory::Environment,
				_x("editor", "Gives the player an airboard, then comes back after a while."), AirboardGeneratorFields, "Object/Airboard", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, short for helicopter, a propeller the player can grab to fly
			DescribeEvent(EventType::Copter, "Copter", _x("editor", "Copter"), EventCategory::Environment,
				_x("editor", "A copter the player can grab to fly for a while."), "Enemy/LizardFloat", 1, EventTraits::Object),
			// TRANSLATORS: Name of an event, a boulder
			DescribeEvent(EventType::RollingRock, "RollingRock", _x("editor", "Rolling Rock"), EventCategory::Environment,
				_x("editor", "A boulder that starts rolling when a trigger with its ID is touched."), RollingRockFields, "Object/RollingRock", std::uint32_t(AnimState::Idle), EventTraits::Object),
			// TRANSLATORS: Name of an event, an area that sets rolling rocks rolling, trigger is a noun
			DescribeEvent(EventType::RollingRockTrigger, "RollingRockTrigger", _x("editor", "Rolling Rock Trigger"), EventCategory::Environment,
				_x("editor", "Sets the rolling rocks with its ID rolling when the player enters it."), RollingRockTriggerFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an object in the level
			DescribeEvent(EventType::SwingingVine, "SwingingVine", _x("editor", "Swinging Vine"), EventCategory::Environment,
				_x("editor", "A swinging vine the player can hang on."), "Object/SwingingVine", std::uint32_t(AnimState::Default), EventTraits::Object),
		};

		return Events;
	}
}

#endif
