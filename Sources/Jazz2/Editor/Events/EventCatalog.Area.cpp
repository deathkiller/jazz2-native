#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"
#include "../../../nCine/I18n.h"

using namespace nCine;

namespace Jazz2::Editor::EventCatalogParts
{
	ArrayView<const EventDescriptor> GetAreaEvents()
	{
		// Built on the first call, after the language is loaded, only what's translated here by _x() is translated, see CatalogText
		// ExitType, only the kinds an exit can be (the low 4 bits, ExitType::TypeMask)
		static const ParamOption ExitTypeOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of level exits)
			{ _x("editor", "Normal"), 1 },
			{ _x("editor", "Warp"), 2 },
			{ _x("editor", "Bonus"), 3 },
			{ _x("editor", "Special"), 4 },
			{ _x("editor", "Boss"), 5 }
		};

		// Player::HandleAreaEventAt(), both the state pass (0) and the effect pass (1, 2)
		static const ParamOption NoFireOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (whether firing is blocked only inside the area, or allowed or blocked from then on)
			{ _x("editor", "Only Inside"), 0 },
			{ _x("editor", "Enable Firing"), 1 },
			{ _x("editor", "Disable Firing"), 2 }
		};

		// WeatherType without WeatherType::OutdoorsOnly
		static const ParamOption WeatherOptions[] = {
			// TRANSLATORS: Choice of a parameter of an event, no weather
			{ _x("editor", "None"), 0 },
			// Kinds of weather aren't translated
			{ "Snow", 1 },
			{ "Flowers", 2 },
			{ "Rain", 3 },
			{ "Leaves", 4 }
		};

		// AmbientSound::OnActivatedAsync(), kinds of ambient sounds aren't translated
		static const ParamOption AmbientSoundOptions[] = {
			{ "Wind", 0 },
			{ "Fire", 1 },
			{ "Science Noise", 2 }
		};

		// Mirrored, each force to the left swaps with the one to the right; the arrows are the belt (0), the accelerating
		// belt (1) and the wind (2), the strengths to both sides summed, see Player::OnHandleAreaEvents()
		static const EventParamField AreaHForceFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, the speed of a conveyor belt moving to the left
			UIntField(_x("editor", "Belt Left"), 0, 8, 0, 255)
				.WithDescription(_x("editor", "Moves a player standing on the tile to the left, 1 is 1 pixel per tick of the original"))
				.MirroredX(MirrorMode::SwapField, 1).WithDirection(DirectionAxis::X, true, 0),
			// TRANSLATORS: Parameter of an event in the event properties, the speed of a conveyor belt moving to the right
			UIntField(_x("editor", "Belt Right"), 1, 8, 0, 255, 2)
				.WithDescription(_x("editor", "Moves a player standing on the tile to the right, 1 is 1 pixel per tick of the original"))
				.MirroredX(MirrorMode::SwapField, 0).WithDirection(DirectionAxis::X, false, 0),
			// TRANSLATORS: Parameter of an event in the event properties, short for a conveyor belt accelerating to the left
			UIntField(_x("editor", "Acc. Belt Left"), 2, 8, 0, 255)
				.WithDescription(_x("editor", "Accelerates a player standing on the tile to the left"))
				.MirroredX(MirrorMode::SwapField, 3).WithDirection(DirectionAxis::X, true, 1),
			// TRANSLATORS: Parameter of an event in the event properties, short for a conveyor belt accelerating to the right
			UIntField(_x("editor", "Acc. Belt Right"), 3, 8, 0, 255)
				.WithDescription(_x("editor", "Accelerates a player standing on the tile to the right"))
				.MirroredX(MirrorMode::SwapField, 2).WithDirection(DirectionAxis::X, false, 1),
			// TRANSLATORS: Parameter of an event in the event properties, the strength of wind blowing to the left
			UIntField(_x("editor", "Wind Left"), 4, 8, 0, 255)
				.WithDescription(_x("editor", "Blows a player inside the tile to the left, at half the speed of a belt"))
				.MirroredX(MirrorMode::SwapField, 5).WithDirection(DirectionAxis::X, true, 2),
			// TRANSLATORS: Parameter of an event in the event properties, the strength of wind blowing to the right
			UIntField(_x("editor", "Wind Right"), 5, 8, 0, 255)
				.WithDescription(_x("editor", "Blows a player inside the tile to the right, at half the speed of a belt"))
				.MirroredX(MirrorMode::SwapField, 4).WithDirection(DirectionAxis::X, false, 2)
		};

		static const EventParamField AreaTextFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			TextIdField(_x("editor", "Text"), 0),
			// TRANSLATORS: Parameter of an event in the event properties, the number of a part of a level text split by |
			UIntField(_x("editor", "Part"), 1, 8, 0, 255)
				.WithDescription(_x("editor", "Part of the text split by | to show, 0 shows the whole text")),
			// TRANSLATORS: Checkbox of an event in the event properties, a verb, the event works only once
			BoolField(_x("editor", "Vanish"), 2)
				.WithDescription(_x("editor", "Removes the event once the text is shown"))
		};

		static const EventParamField AreaEndOfLevelFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the kind of the level exit
			EnumField(_x("editor", "Exit"), 0, 4, ExitTypeOptions, 1),
			BoolField(_x("editor", "Frozen"), 0, 6)
				.WithDescription(_x("editor", "The player enters the next level frozen")),
			// TRANSLATORS: Checkbox of an event in the event properties, an adjective, without the usual fade-out or animation
			BoolField(_x("editor", "Fast"), 1)
				.WithDescription(_x("editor", "Changes the level without the fade-out")),
			TextIdField(_x("editor", "Next Level"), 2)
				.WithDescription(_x("editor", "Text with the name of the next level, 0 goes to the default next level")),
			// Only of a text, see Player::HandleAreaEventAt()
			// TRANSLATORS: Parameter of an event in the event properties, the number of a part of a level text split by |
			UIntField(_x("editor", "Part"), 3, 8, 0, 255)
				.WithDescription(_x("editor", "Part of the text split by | that is the name, 0 is the first one"))
				.ShowIfNot(3, 0),
			UIntField(_x("editor", "Coins"), 4, 16, 0, 65535)
				.WithDescription(_x("editor", "Coins needed to take the exit, they are spent"))
		};

		static const EventParamField AreaCallbackFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, the number of a function of the level script
			UIntField(_x("editor", "Function"), 0, 8, 0, 255)
				.WithDescription(_x("editor", "Number of the onFunction# function of the level script to call")),
			// TRANSLATORS: Parameter of an event in the event properties, short for parameter, a value passed to a function of the level script
			UIntField(_x("editor", "Param"), 1, 8, 0, 255)
				.WithDescription(_x("editor", "Passed to the function if it takes a bool or an 8-bit number")),
			// TRANSLATORS: Checkbox of an event in the event properties, a verb, the event works only once
			BoolField(_x("editor", "Vanish"), 2)
				.WithDescription(_x("editor", "Disables the function after its first call"))
		};

		static const EventParamField AreaActivateBossFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			StringField(_x("editor", "Music"), 0, 16)
				.WithDescription(_x("editor", "Music played during the fight, for example boss1.j2b"))
		};

		static const EventParamField AreaNoFireFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Mode"), 0, 8, NoFireOptions, 0)
				.WithDescription(_x("editor", "Blocks firing only while inside, or allows or blocks it from now on"))
		};

		static const EventParamField AreaWaterBlockFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, the height of the water surface
			IntField(_x("editor", "Surface"), 0, 8, -128, 127)
				.WithUnit(ParamUnit::Pixels)
				.WithDescription(_x("editor", "Height of the water surface below the top of the tile, negative is above it"))
		};

		static const EventParamField AreaWeatherFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Weather"), 0, 7, WeatherOptions, 1)
				.WithDescription(_x("editor", "Flowers and leaves are shown as snow")),
			// Nothing to say of no weather
			BoolField(_x("editor", "Outdoors Only"), 0, 7)
				.WithDescription(_x("editor", "No particles appear inside tiles and all of them vanish when they hit one"))
				.ShowIfNot(0, 0),
			UIntField(_x("editor", "Intensity"), 1, 8, 0, 255, 5)
				.WithDescription(_x("editor", "Particles spawned each frame"))
				.ShowIfNot(0, 0)
		};

		static const EventParamField AreaAmbientSoundFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Sound"), 0, 8, AmbientSoundOptions, 0),
			UIntField(_x("editor", "Volume"), 1, 8, 0, 255, 255)
				.WithDescription(_x("editor", "255 is the full volume"))
		};

		static const EventParamField AreaAmbientBubblesFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Speed"), 0, 8, 0, 255, 5)
				.WithDescription(_x("editor", "Bubbles released every 20 frames"))
		};

		// Crates and zones set triggers, the tiles of trigger areas change by them; warp origins lead to warp targets.
		// The editor links them by lines, see EventPreviews::CollectLinks().
		constexpr const char* TriggerChannel = "Trigger";
		constexpr const char* WarpChannel = "Warp";

		static const EventParamField TriggerCrateFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number of the trigger it changes or that changes it
			TriggerIdField(_x("editor", "Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Source),
			// Ignored when it toggles, see TriggerCrate::OnActivatedAsync()
			// TRANSLATORS: Checkbox of an event in the event properties, it turns the trigger on, otherwise off
			BoolField(_x("editor", "Turn On"), 1, 0, true)
				.WithDescription(_x("editor", "Turns the trigger on when the crate breaks, otherwise off"))
				.ShowIf(2, 0),
			// TRANSLATORS: Checkbox of an event in the event properties, a verb, it switches the trigger between on and off
			BoolField(_x("editor", "Toggle"), 2)
				.WithDescription(_x("editor", "Switches the trigger instead, Turn On is ignored"))
		};

		static const EventParamField TriggerAreaFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number of the trigger it changes or that changes it
			TriggerIdField(_x("editor", "Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Target)
		};

		static const EventParamField TriggerZoneFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number of the trigger it changes or that changes it
			TriggerIdField(_x("editor", "Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Source),
			// TRANSLATORS: Checkbox of an event in the event properties, it turns the trigger on, otherwise off
			BoolField(_x("editor", "Turn On"), 1, 0, true)
				.WithDescription(_x("editor", "Turns the trigger on, otherwise off"))
		};

		static const EventParamField WarpCoinBonusFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number that links warps with their targets
			WarpIdField(_x("editor", "Warp"), 0)
				.WithLink(WarpChannel, EventLinkRole::Source),
			// TRANSLATORS: Checkbox of an event in the event properties, an adjective, without the usual fade-out or animation
			BoolField(_x("editor", "Fast"), 1)
				.WithDescription(_x("editor", "Moves the player instantly, without the warp animation")),
			// BonusWarp::Activate() counts the lap as WarpOrigin does
			// TRANSLATORS: Checkbox of an event in the event properties, it counts a lap of a race
			BoolField(_x("editor", "Set Lap"), 2)
				.WithDescription(_x("editor", "Counts a lap, it's the finish line of a race")),
			UIntField(_x("editor", "Coins"), 3, 8, 0, 255, 10)
				.WithDescription(_x("editor", "Coins needed, the remaining ones turn into gems"))
		};

		static const EventParamField WarpOriginFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number that links warps with their targets
			WarpIdField(_x("editor", "Warp"), 0)
				.WithLink(WarpChannel, EventLinkRole::Source),
			// TRANSLATORS: Checkbox of an event in the event properties, an adjective, without the usual fade-out or animation
			BoolField(_x("editor", "Fast"), 1)
				.WithDescription(_x("editor", "Moves the player instantly, without the warp animation")),
			// TRANSLATORS: Checkbox of an event in the event properties, it counts a lap of a race
			BoolField(_x("editor", "Set Lap"), 2)
				.WithDescription(_x("editor", "Counts a lap, it's the finish line of a race"))
		};

		static const EventParamField WarpTargetFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number that links warps with their targets
			WarpIdField(_x("editor", "Warp"), 0)
				.WithLink(WarpChannel, EventLinkRole::Target)
		};

		// BonusWarp has a sign only for these costs, anything else shows the plain one
		static const EventPreviewCase WarpCoinBonusPreview[] = {
			{ 10, "Object/BonusWarp", 10 },
			{ 20, "Object/BonusWarp", 20 },
			{ 50, "Object/BonusWarp", 50 },
			{ 100, "Object/BonusWarp", 100 },
			{ EventPreviewCase::AnyValue, "Object/BonusWarp", std::uint32_t(AnimState::Default) }
		};

		static const EventDescriptor Events[] = {
			// Area
			// TRANSLATORS: Name of an event, an area that walking enemies can't enter
			DescribeEvent(EventType::AreaStopEnemy, "AreaStopEnemy", _x("editor", "Stop Enemy"), EventCategory::Area,
				_x("editor", "Walking enemies can't enter it."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that lifts players up
			DescribeEvent(EventType::AreaFloatUp, "AreaFloatUp", _x("editor", "Float Up"), EventCategory::Area,
				_x("editor", "Lifts players up while they're inside."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area with a conveyor belt or wind
			DescribeEvent(EventType::AreaHForce, "AreaHForce", _x("editor", "Horizontal Force"), EventCategory::Area,
				_x("editor", "Conveyor belt for players standing on it, or wind for players inside."), AreaHForceFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that shows a level text
			DescribeEvent(EventType::AreaText, "AreaText", _x("editor", "Text"), EventCategory::Area,
				_x("editor", "Shows a level text."), AreaTextFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that ends the level
			DescribeEvent(EventType::AreaEndOfLevel, "AreaEndOfLevel", _x("editor", "End of Level"), EventCategory::Area,
				_x("editor", "Ends the level."), AreaEndOfLevelFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that calls a function of the level script
			DescribeEvent(EventType::AreaCallback, "AreaCallback", _x("editor", "Script Callback"), EventCategory::Area,
				_x("editor", "Calls a function of the level script."), AreaCallbackFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that starts the boss fight
			DescribeEvent(EventType::AreaActivateBoss, "AreaActivateBoss", _x("editor", "Activate Boss"), EventCategory::Area,
				_x("editor", "Starts the fight with the boss of the level."), AreaActivateBossFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that ends flying of the player
			DescribeEvent(EventType::AreaFlyOff, "AreaFlyOff", _x("editor", "Fly Off"), EventCategory::Area,
				_x("editor", "Takes away the airboard and the flying carrot, and turns a bird back into its character."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that turns a morphed player back into their character
			DescribeEvent(EventType::AreaRevertMorph, "AreaRevertMorph", _x("editor", "Revert Morph"), EventCategory::Area,
				_x("editor", "Turns a morphed player back into their character."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that turns the player into a frog
			DescribeEvent(EventType::AreaMorphToFrog, "AreaMorphToFrog", _x("editor", "Morph to Frog"), EventCategory::Area,
				_x("editor", "Turns the player into a frog."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that blocks or allows firing
			DescribeEvent(EventType::AreaNoFire, "AreaNoFire", _x("editor", "No Fire"), EventCategory::Area,
				_x("editor", "Blocks or allows firing."), AreaNoFireFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area of shallow water, block is a noun
			DescribeEvent(EventType::AreaWaterBlock, "AreaWaterBlock", _x("editor", "Water Block"), EventCategory::Area,
				_x("editor", "Shallow water the player wades in."), AreaWaterBlockFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that changes the weather
			DescribeEvent(EventType::AreaWeather, "AreaWeather", _x("editor", "Weather"), EventCategory::Area,
				_x("editor", "Changes the weather when a player comes near."), AreaWeatherFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, a looping sound in the level
			DescribeEvent(EventType::AreaAmbientSound, "AreaAmbientSound", _x("editor", "Ambient Sound"), EventCategory::Area,
				_x("editor", "Plays a looping sound around the tile."), AreaAmbientSoundFields, nullptr, 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a source of rising bubbles in the level
			DescribeEvent(EventType::AreaAmbientBubbles, "AreaAmbientBubbles", _x("editor", "Ambient Bubbles"), EventCategory::Area,
				_x("editor", "Releases rising bubbles."), AreaAmbientBubblesFields, "Common/AmbientBubbles", std::uint32_t(AnimState::Default), EventTraits::Object),

			// Triggers
			// TRANSLATORS: Name of an event, a crate that changes a trigger, trigger is a noun
			DescribeEvent(EventType::TriggerCrate, "TriggerCrate", _x("editor", "Trigger Crate"), EventCategory::Trigger,
				_x("editor", "Crate that changes a trigger when it breaks."), TriggerCrateFields, "Object/TriggerCrate", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a tile modifier that changes the tile by a trigger, trigger is a noun
			DescribeEvent(EventType::TriggerArea, "TriggerArea", _x("editor", "Trigger Area"), EventCategory::Trigger,
				_x("editor", "Tile that disappears, or shows the second frame of its animation, while the trigger is on."), TriggerAreaFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, an area that turns a trigger on or off, trigger is a noun
			DescribeEvent(EventType::TriggerZone, "TriggerZone", _x("editor", "Trigger Zone"), EventCategory::Trigger,
				_x("editor", "Turns a trigger on or off when a player enters it."), TriggerZoneFields, nullptr, 0, EventTraits::Area),

			// Warp
			// TRANSLATORS: Name of an event, a warp that costs coins
			DescribeEvent(EventType::WarpCoinBonus, "WarpCoinBonus", _x("editor", "Bonus Warp"), EventCategory::Warp,
				_x("editor", "Warp that costs coins."), WarpCoinBonusFields, "Object/BonusWarp", 10 /* Bonus10, the default cost */, EventTraits::Object)
				.WithPreviewCases(3, WarpCoinBonusPreview),
			// TRANSLATORS: Name of an event, an area where a warp starts
			DescribeEvent(EventType::WarpOrigin, "WarpOrigin", _x("editor", "Warp Origin"), EventCategory::Warp,
				_x("editor", "Warps the player to a warp target with the same identifier."), WarpOriginFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, the place a warp leads to
			DescribeEvent(EventType::WarpTarget, "WarpTarget", _x("editor", "Warp Target"), EventCategory::Warp,
				_x("editor", "Where warps with the same identifier lead, a random one if there are more."), WarpTargetFields, nullptr, 0, EventTraits::None)
		};

		return Events;
	}
}

#endif
