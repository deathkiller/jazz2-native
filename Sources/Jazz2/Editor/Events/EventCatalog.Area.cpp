#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// ExitType, only the kinds an exit can be (the low 4 bits, ExitType::TypeMask)
		constexpr ParamOption ExitTypeOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of level exits)
			{ NC_("editor", "Normal"), 1 },
			{ NC_("editor", "Warp"), 2 },
			{ NC_("editor", "Bonus"), 3 },
			{ NC_("editor", "Special"), 4 },
			{ NC_("editor", "Boss"), 5 }
		};

		// Player::HandleAreaEventAt(), both the state pass (0) and the effect pass (1, 2)
		constexpr ParamOption NoFireOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (whether firing is blocked only inside the area, or allowed or blocked from then on)
			{ NC_("editor", "Only Inside"), 0 },
			{ NC_("editor", "Enable Firing"), 1 },
			{ NC_("editor", "Disable Firing"), 2 }
		};

		// WeatherType without WeatherType::OutdoorsOnly
		constexpr ParamOption WeatherOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of weather)
			{ NC_("editor", "None"), 0 },
			{ NC_("editor", "Snow"), 1 },
			{ NC_("editor", "Flowers"), 2 },
			{ NC_("editor", "Rain"), 3 },
			// TRANSLATORS: Choice of a kind of weather, falling leaves of trees, a noun
			{ NC_("editor", "Leaves"), 4 }
		};

		// AmbientSound::OnActivatedAsync()
		constexpr ParamOption AmbientSoundOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (kinds of ambient sounds)
			{ NC_("editor", "Wind"), 0 },
			{ NC_("editor", "Fire"), 1 },
			// TRANSLATORS: Choice of an ambient sound, the hum of laboratory machines
			{ NC_("editor", "Science Noise"), 2 }
		};

		// Mirrored, each force to the left swaps with the one to the right; the arrows are the belt (0), the accelerating
		// belt (1) and the wind (2), the strengths to both sides summed, see Player::OnHandleAreaEvents()
		constexpr EventParamField AreaHForceFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, the speed of a conveyor belt moving to the left
			UIntField(NC_("editor", "Belt Left"), 0, 8, 0, 255)
				.WithDescription(NC_("editor", "Moves a player standing on the tile to the left, 1 is 1 pixel per tick of the original"))
				.MirroredX(MirrorMode::SwapField, 1).WithDirection(DirectionAxis::X, true, 0),
			// TRANSLATORS: Parameter of an event in the event properties, the speed of a conveyor belt moving to the right
			UIntField(NC_("editor", "Belt Right"), 1, 8, 0, 255, 2)
				.WithDescription(NC_("editor", "Moves a player standing on the tile to the right, 1 is 1 pixel per tick of the original"))
				.MirroredX(MirrorMode::SwapField, 0).WithDirection(DirectionAxis::X, false, 0),
			// TRANSLATORS: Parameter of an event in the event properties, short for a conveyor belt accelerating to the left
			UIntField(NC_("editor", "Acc. Belt Left"), 2, 8, 0, 255)
				.WithDescription(NC_("editor", "Accelerates a player standing on the tile to the left"))
				.MirroredX(MirrorMode::SwapField, 3).WithDirection(DirectionAxis::X, true, 1),
			// TRANSLATORS: Parameter of an event in the event properties, short for a conveyor belt accelerating to the right
			UIntField(NC_("editor", "Acc. Belt Right"), 3, 8, 0, 255)
				.WithDescription(NC_("editor", "Accelerates a player standing on the tile to the right"))
				.MirroredX(MirrorMode::SwapField, 2).WithDirection(DirectionAxis::X, false, 1),
			// TRANSLATORS: Parameter of an event in the event properties, the strength of wind blowing to the left
			UIntField(NC_("editor", "Wind Left"), 4, 8, 0, 255)
				.WithDescription(NC_("editor", "Blows a player inside the tile to the left, at half the speed of a belt"))
				.MirroredX(MirrorMode::SwapField, 5).WithDirection(DirectionAxis::X, true, 2),
			// TRANSLATORS: Parameter of an event in the event properties, the strength of wind blowing to the right
			UIntField(NC_("editor", "Wind Right"), 5, 8, 0, 255)
				.WithDescription(NC_("editor", "Blows a player inside the tile to the right, at half the speed of a belt"))
				.MirroredX(MirrorMode::SwapField, 4).WithDirection(DirectionAxis::X, false, 2)
		};

		constexpr EventParamField AreaTextFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			TextIdField(NC_("editor", "Text"), 0),
			// TRANSLATORS: Parameter of an event in the event properties, the number of a part of a level text split by |
			UIntField(NC_("editor", "Part"), 1, 8, 0, 255)
				.WithDescription(NC_("editor", "Part of the text split by | to show, 0 shows the whole text")),
			// TRANSLATORS: Checkbox of an event in the event properties, a verb, the event works only once
			BoolField(NC_("editor", "Vanish"), 2)
				.WithDescription(NC_("editor", "Removes the event once the text is shown"))
		};

		constexpr EventParamField AreaEndOfLevelFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the kind of the level exit
			EnumField(NC_("editor", "Exit"), 0, 4, ExitTypeOptions, 1),
			BoolField(NC_("editor", "Frozen"), 0, 6)
				.WithDescription(NC_("editor", "The player enters the next level frozen")),
			// TRANSLATORS: Checkbox of an event in the event properties, an adjective, without the usual fade-out or animation
			BoolField(NC_("editor", "Fast"), 1)
				.WithDescription(NC_("editor", "Changes the level without the fade-out")),
			TextIdField(NC_("editor", "Next Level"), 2)
				.WithDescription(NC_("editor", "Text with the name of the next level, 0 goes to the default next level")),
			// Only of a text, see Player::HandleAreaEventAt()
			// TRANSLATORS: Parameter of an event in the event properties, the number of a part of a level text split by |
			UIntField(NC_("editor", "Part"), 3, 8, 0, 255)
				.WithDescription(NC_("editor", "Part of the text split by | that is the name, 0 is the first one"))
				.ShowIfNot(3, 0),
			UIntField(NC_("editor", "Coins"), 4, 16, 0, 65535)
				.WithDescription(NC_("editor", "Coins needed to take the exit, they are spent"))
		};

		constexpr EventParamField AreaCallbackFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, the number of a function of the level script
			UIntField(NC_("editor", "Function"), 0, 8, 0, 255)
				.WithDescription(NC_("editor", "Number of the onFunction# function of the level script to call")),
			// TRANSLATORS: Parameter of an event in the event properties, short for parameter, a value passed to a function of the level script
			UIntField(NC_("editor", "Param"), 1, 8, 0, 255)
				.WithDescription(NC_("editor", "Passed to the function if it takes a bool or an 8-bit number")),
			// TRANSLATORS: Checkbox of an event in the event properties, a verb, the event works only once
			BoolField(NC_("editor", "Vanish"), 2)
				.WithDescription(NC_("editor", "Disables the function after its first call"))
		};

		constexpr EventParamField AreaActivateBossFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			StringField(NC_("editor", "Music"), 0, 16)
				.WithDescription(NC_("editor", "Music played during the fight, for example boss1.j2b"))
		};

		constexpr EventParamField AreaNoFireFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Mode"), 0, 8, NoFireOptions, 0)
				.WithDescription(NC_("editor", "Blocks firing only while inside, or allows or blocks it from now on"))
		};

		constexpr EventParamField AreaWaterBlockFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, the height of the water surface
			IntField(NC_("editor", "Surface"), 0, 8, -128, 127)
				.WithUnit(ParamUnit::Pixels)
				.WithDescription(NC_("editor", "Height of the water surface below the top of the tile, negative is above it"))
		};

		constexpr EventParamField AreaWeatherFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Weather"), 0, 7, WeatherOptions, 1)
				.WithDescription(NC_("editor", "Flowers and leaves are shown as snow")),
			// Nothing to say of no weather
			BoolField(NC_("editor", "Outdoors Only"), 0, 7)
				.WithDescription(NC_("editor", "No particles appear inside tiles and all of them vanish when they hit one"))
				.ShowIfNot(0, 0),
			UIntField(NC_("editor", "Intensity"), 1, 8, 0, 255, 5)
				.WithDescription(NC_("editor", "Particles spawned each frame"))
				.ShowIfNot(0, 0)
		};

		constexpr EventParamField AreaAmbientSoundFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Sound"), 0, 8, AmbientSoundOptions, 0),
			UIntField(NC_("editor", "Volume"), 1, 8, 0, 255, 255)
				.WithDescription(NC_("editor", "255 is the full volume"))
		};

		constexpr EventParamField AreaAmbientBubblesFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Speed"), 0, 8, 0, 255, 5)
				.WithDescription(NC_("editor", "Bubbles released every 20 frames"))
		};

		// Crates and zones set triggers, the tiles of trigger areas change by them; warp origins lead to warp targets.
		// The editor links them by lines, see EventPreviews::CollectLinks().
		constexpr const char* TriggerChannel = "trigger";
		constexpr const char* WarpChannel = "warp";

		constexpr EventParamField TriggerCrateFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number of the trigger it changes or that changes it
			TriggerIdField(NC_("editor", "Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Source),
			// Ignored when it toggles, see TriggerCrate::OnActivatedAsync()
			// TRANSLATORS: Checkbox of an event in the event properties, it turns the trigger on, otherwise off
			BoolField(NC_("editor", "Turn On"), 1, 0, true)
				.WithDescription(NC_("editor", "Turns the trigger on when the crate breaks, otherwise off"))
				.ShowIf(2, 0),
			// TRANSLATORS: Checkbox of an event in the event properties, a verb, it switches the trigger between on and off
			BoolField(NC_("editor", "Toggle"), 2)
				.WithDescription(NC_("editor", "Switches the trigger instead, Turn On is ignored"))
		};

		constexpr EventParamField TriggerAreaFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number of the trigger it changes or that changes it
			TriggerIdField(NC_("editor", "Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Target)
		};

		constexpr EventParamField TriggerZoneFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number of the trigger it changes or that changes it
			TriggerIdField(NC_("editor", "Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Source),
			// TRANSLATORS: Checkbox of an event in the event properties, it turns the trigger on, otherwise off
			BoolField(NC_("editor", "Turn On"), 1, 0, true)
				.WithDescription(NC_("editor", "Turns the trigger on, otherwise off"))
		};

		constexpr EventParamField WarpCoinBonusFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number that links warps with their targets
			WarpIdField(NC_("editor", "Warp"), 0)
				.WithLink(WarpChannel, EventLinkRole::Source),
			// TRANSLATORS: Checkbox of an event in the event properties, an adjective, without the usual fade-out or animation
			BoolField(NC_("editor", "Fast"), 1)
				.WithDescription(NC_("editor", "Moves the player instantly, without the warp animation")),
			// BonusWarp::Activate() counts the lap as WarpOrigin does
			// TRANSLATORS: Checkbox of an event in the event properties, it counts a lap of a race
			BoolField(NC_("editor", "Set Lap"), 2)
				.WithDescription(NC_("editor", "Counts a lap, it's the finish line of a race")),
			UIntField(NC_("editor", "Coins"), 3, 8, 0, 255, 10)
				.WithDescription(NC_("editor", "Coins needed, the remaining ones turn into gems"))
		};

		constexpr EventParamField WarpOriginFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number that links warps with their targets
			WarpIdField(NC_("editor", "Warp"), 0)
				.WithLink(WarpChannel, EventLinkRole::Source),
			// TRANSLATORS: Checkbox of an event in the event properties, an adjective, without the usual fade-out or animation
			BoolField(NC_("editor", "Fast"), 1)
				.WithDescription(NC_("editor", "Moves the player instantly, without the warp animation")),
			// TRANSLATORS: Checkbox of an event in the event properties, it counts a lap of a race
			BoolField(NC_("editor", "Set Lap"), 2)
				.WithDescription(NC_("editor", "Counts a lap, it's the finish line of a race"))
		};

		constexpr EventParamField WarpTargetFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties, a noun, the number that links warps with their targets
			WarpIdField(NC_("editor", "Warp"), 0)
				.WithLink(WarpChannel, EventLinkRole::Target)
		};

		// BonusWarp has a sign only for these costs, anything else shows the plain one
		constexpr EventPreviewCase WarpCoinBonusPreview[] = {
			{ 10, "Object/BonusWarp", 10 },
			{ 20, "Object/BonusWarp", 20 },
			{ 50, "Object/BonusWarp", 50 },
			{ 100, "Object/BonusWarp", 100 },
			{ EventPreviewCase::AnyValue, "Object/BonusWarp", std::uint32_t(AnimState::Default) }
		};

		constexpr EventDescriptor Events[] = {
			// Area
			// TRANSLATORS: Name of an event, an area that walking enemies can't enter
			DescribeEvent(EventType::AreaStopEnemy, "AreaStopEnemy", NC_("editor", "Stop Enemy"), EventCategory::Area,
				NC_("editor", "Walking enemies can't enter it."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that lifts players up
			DescribeEvent(EventType::AreaFloatUp, "AreaFloatUp", NC_("editor", "Float Up"), EventCategory::Area,
				NC_("editor", "Lifts players up while they're inside."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area with a conveyor belt or wind
			DescribeEvent(EventType::AreaHForce, "AreaHForce", NC_("editor", "Horizontal Force"), EventCategory::Area,
				NC_("editor", "Conveyor belt for players standing on it, or wind for players inside."), AreaHForceFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that shows a level text
			DescribeEvent(EventType::AreaText, "AreaText", NC_("editor", "Text"), EventCategory::Area,
				NC_("editor", "Shows a level text."), AreaTextFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that ends the level
			DescribeEvent(EventType::AreaEndOfLevel, "AreaEndOfLevel", NC_("editor", "End of Level"), EventCategory::Area,
				NC_("editor", "Ends the level."), AreaEndOfLevelFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that calls a function of the level script
			DescribeEvent(EventType::AreaCallback, "AreaCallback", NC_("editor", "Script Callback"), EventCategory::Area,
				NC_("editor", "Calls a function of the level script."), AreaCallbackFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that starts the boss fight
			DescribeEvent(EventType::AreaActivateBoss, "AreaActivateBoss", NC_("editor", "Activate Boss"), EventCategory::Area,
				NC_("editor", "Starts the fight with the boss of the level."), AreaActivateBossFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that ends flying of the player
			DescribeEvent(EventType::AreaFlyOff, "AreaFlyOff", NC_("editor", "Fly Off"), EventCategory::Area,
				NC_("editor", "Takes away the airboard and the flying carrot, and turns a bird back into its character."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that turns a morphed player back into their character
			DescribeEvent(EventType::AreaRevertMorph, "AreaRevertMorph", NC_("editor", "Revert Morph"), EventCategory::Area,
				NC_("editor", "Turns a morphed player back into their character."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that turns the player into a frog
			DescribeEvent(EventType::AreaMorphToFrog, "AreaMorphToFrog", NC_("editor", "Morph to Frog"), EventCategory::Area,
				NC_("editor", "Turns the player into a frog."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that blocks or allows firing
			DescribeEvent(EventType::AreaNoFire, "AreaNoFire", NC_("editor", "No Fire"), EventCategory::Area,
				NC_("editor", "Blocks or allows firing."), AreaNoFireFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area of shallow water, block is a noun
			DescribeEvent(EventType::AreaWaterBlock, "AreaWaterBlock", NC_("editor", "Water Block"), EventCategory::Area,
				NC_("editor", "Shallow water the player wades in."), AreaWaterBlockFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that changes the weather
			DescribeEvent(EventType::AreaWeather, "AreaWeather", NC_("editor", "Weather"), EventCategory::Area,
				NC_("editor", "Changes the weather when a player comes near."), AreaWeatherFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, a looping sound in the level
			DescribeEvent(EventType::AreaAmbientSound, "AreaAmbientSound", NC_("editor", "Ambient Sound"), EventCategory::Area,
				NC_("editor", "Plays a looping sound around the tile."), AreaAmbientSoundFields, nullptr, 0, EventTraits::Object),
			// TRANSLATORS: Name of an event, a source of rising bubbles in the level
			DescribeEvent(EventType::AreaAmbientBubbles, "AreaAmbientBubbles", NC_("editor", "Ambient Bubbles"), EventCategory::Area,
				NC_("editor", "Releases rising bubbles."), AreaAmbientBubblesFields, "Common/AmbientBubbles", std::uint32_t(AnimState::Default), EventTraits::Object),

			// Triggers
			// TRANSLATORS: Name of an event, a crate that changes a trigger, trigger is a noun
			DescribeEvent(EventType::TriggerCrate, "TriggerCrate", NC_("editor", "Trigger Crate"), EventCategory::Trigger,
				NC_("editor", "Crate that changes a trigger when it breaks."), TriggerCrateFields, "Object/TriggerCrate", std::uint32_t(AnimState::Default), EventTraits::Object),
			// TRANSLATORS: Name of an event, a tile modifier that changes the tile by a trigger, trigger is a noun
			DescribeEvent(EventType::TriggerArea, "TriggerArea", NC_("editor", "Trigger Area"), EventCategory::Trigger,
				NC_("editor", "Tile that disappears, or shows the second frame of its animation, while the trigger is on."), TriggerAreaFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, an area that turns a trigger on or off, trigger is a noun
			DescribeEvent(EventType::TriggerZone, "TriggerZone", NC_("editor", "Trigger Zone"), EventCategory::Trigger,
				NC_("editor", "Turns a trigger on or off when a player enters it."), TriggerZoneFields, nullptr, 0, EventTraits::Area),

			// Warp
			// TRANSLATORS: Name of an event, a warp that costs coins
			DescribeEvent(EventType::WarpCoinBonus, "WarpCoinBonus", NC_("editor", "Bonus Warp"), EventCategory::Warp,
				NC_("editor", "Warp that costs coins."), WarpCoinBonusFields, "Object/BonusWarp", 10 /* Bonus10, the default cost */, EventTraits::Object)
				.WithPreviewCases(3, WarpCoinBonusPreview),
			// TRANSLATORS: Name of an event, an area where a warp starts
			DescribeEvent(EventType::WarpOrigin, "WarpOrigin", NC_("editor", "Warp Origin"), EventCategory::Warp,
				NC_("editor", "Warps the player to a warp target with the same identifier."), WarpOriginFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, the place a warp leads to
			DescribeEvent(EventType::WarpTarget, "WarpTarget", NC_("editor", "Warp Target"), EventCategory::Warp,
				NC_("editor", "Where warps with the same identifier lead, a random one if there are more."), WarpTargetFields, nullptr, 0, EventTraits::None)
		};
	}

	ArrayView<const EventDescriptor> GetAreaEvents()
	{
		return Events;
	}
}

#endif
