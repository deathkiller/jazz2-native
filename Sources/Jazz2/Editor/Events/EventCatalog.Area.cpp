#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// ExitType, only the kinds an exit can be (the low 4 bits, ExitType::TypeMask)
		constexpr ParamOption ExitTypeOptions[] = {
			{ N_("Normal"), 1 },
			{ N_("Warp"), 2 },
			{ N_("Bonus"), 3 },
			{ N_("Special"), 4 },
			{ N_("Boss"), 5 }
		};

		// Player::HandleAreaEventAt(), both the state pass (0) and the effect pass (1, 2)
		constexpr ParamOption NoFireOptions[] = {
			{ N_("Only Inside"), 0 },
			{ N_("Enable Firing"), 1 },
			{ N_("Disable Firing"), 2 }
		};

		// WeatherType without WeatherType::OutdoorsOnly
		constexpr ParamOption WeatherOptions[] = {
			{ N_("None"), 0 },
			{ N_("Snow"), 1 },
			{ N_("Flowers"), 2 },
			{ N_("Rain"), 3 },
			{ N_("Leaves"), 4 }
		};

		// AmbientSound::OnActivatedAsync()
		constexpr ParamOption AmbientSoundOptions[] = {
			{ N_("Wind"), 0 },
			{ N_("Fire"), 1 },
			{ N_("Science Noise"), 2 }
		};

		// Mirrored, each force to the left swaps with the one to the right; the arrows are the belt (0), the accelerating
		// belt (1) and the wind (2), the strengths to both sides summed, see Player::OnHandleAreaEvents()
		constexpr EventParamField AreaHForceFields[] = {
			UIntField(N_("Belt Left"), 0, 8, 0, 255)
				.WithDescription(N_("Moves a player standing on the tile to the left, 1 is 1 pixel per tick of the original"))
				.MirroredX(MirrorMode::SwapField, 1).WithDirection(DirectionAxis::X, true, 0),
			UIntField(N_("Belt Right"), 1, 8, 0, 255, 2)
				.WithDescription(N_("Moves a player standing on the tile to the right, 1 is 1 pixel per tick of the original"))
				.MirroredX(MirrorMode::SwapField, 0).WithDirection(DirectionAxis::X, false, 0),
			UIntField(N_("Acc. Belt Left"), 2, 8, 0, 255)
				.WithDescription(N_("Accelerates a player standing on the tile to the left"))
				.MirroredX(MirrorMode::SwapField, 3).WithDirection(DirectionAxis::X, true, 1),
			UIntField(N_("Acc. Belt Right"), 3, 8, 0, 255)
				.WithDescription(N_("Accelerates a player standing on the tile to the right"))
				.MirroredX(MirrorMode::SwapField, 2).WithDirection(DirectionAxis::X, false, 1),
			UIntField(N_("Wind Left"), 4, 8, 0, 255)
				.WithDescription(N_("Blows a player inside the tile to the left, at half the speed of a belt"))
				.MirroredX(MirrorMode::SwapField, 5).WithDirection(DirectionAxis::X, true, 2),
			UIntField(N_("Wind Right"), 5, 8, 0, 255)
				.WithDescription(N_("Blows a player inside the tile to the right, at half the speed of a belt"))
				.MirroredX(MirrorMode::SwapField, 4).WithDirection(DirectionAxis::X, false, 2)
		};

		constexpr EventParamField AreaTextFields[] = {
			TextIdField(N_("Text"), 0),
			UIntField(N_("Part"), 1, 8, 0, 255)
				.WithDescription(N_("Part of the text split by | to show, 0 shows the whole text")),
			BoolField(N_("Vanish"), 2)
				.WithDescription(N_("Removes the event once the text is shown"))
		};

		constexpr EventParamField AreaEndOfLevelFields[] = {
			EnumField(N_("Exit"), 0, 4, ExitTypeOptions, 1),
			BoolField(N_("Frozen"), 0, 6)
				.WithDescription(N_("The player enters the next level frozen")),
			BoolField(N_("Fast"), 1)
				.WithDescription(N_("Changes the level without the fade-out")),
			TextIdField(N_("Next Level"), 2)
				.WithDescription(N_("Text with the name of the next level, 0 goes to the default next level")),
			// Only of a text, see Player::HandleAreaEventAt()
			UIntField(N_("Part"), 3, 8, 0, 255)
				.WithDescription(N_("Part of the text split by | that is the name, 0 is the first one"))
				.ShowIfNot(3, 0),
			UIntField(N_("Coins"), 4, 16, 0, 65535)
				.WithDescription(N_("Coins needed to take the exit, they are spent"))
		};

		constexpr EventParamField AreaCallbackFields[] = {
			UIntField(N_("Function"), 0, 8, 0, 255)
				.WithDescription(N_("Number of the onFunction# function of the level script to call")),
			UIntField(N_("Param"), 1, 8, 0, 255)
				.WithDescription(N_("Passed to the function if it takes a bool or an 8-bit number")),
			BoolField(N_("Vanish"), 2)
				.WithDescription(N_("Disables the function after its first call"))
		};

		constexpr EventParamField AreaActivateBossFields[] = {
			StringField(N_("Music"), 0, 16)
				.WithDescription(N_("Music played during the fight, for example boss1.j2b"))
		};

		constexpr EventParamField AreaNoFireFields[] = {
			EnumField(N_("Mode"), 0, 8, NoFireOptions, 0)
				.WithDescription(N_("Blocks firing only while inside, or allows or blocks it from now on"))
		};

		constexpr EventParamField AreaWaterBlockFields[] = {
			IntField(N_("Surface"), 0, 8, -128, 127)
				.WithUnit(ParamUnit::Pixels)
				.WithDescription(N_("Height of the water surface below the top of the tile, negative is above it"))
		};

		constexpr EventParamField AreaWeatherFields[] = {
			EnumField(N_("Weather"), 0, 7, WeatherOptions, 1)
				.WithDescription(N_("Flowers and leaves are shown as snow")),
			// Nothing to say of no weather
			BoolField(N_("Outdoors Only"), 0, 7)
				.WithDescription(N_("No particles appear inside tiles and all of them vanish when they hit one"))
				.ShowIfNot(0, 0),
			UIntField(N_("Intensity"), 1, 8, 0, 255, 5)
				.WithDescription(N_("Particles spawned each frame"))
				.ShowIfNot(0, 0)
		};

		constexpr EventParamField AreaAmbientSoundFields[] = {
			EnumField(N_("Sound"), 0, 8, AmbientSoundOptions, 0),
			UIntField(N_("Volume"), 1, 8, 0, 255, 255)
				.WithDescription(N_("255 is the full volume"))
		};

		constexpr EventParamField AreaAmbientBubblesFields[] = {
			UIntField(N_("Speed"), 0, 8, 0, 255, 5)
				.WithDescription(N_("Bubbles released every 20 frames"))
		};

		// Crates and zones set triggers, the tiles of trigger areas change by them; warp origins lead to warp targets.
		// The editor links them by lines, see EventPreviews::CollectLinks().
		constexpr const char* TriggerChannel = "trigger";
		constexpr const char* WarpChannel = "warp";

		constexpr EventParamField TriggerCrateFields[] = {
			TriggerIdField(N_("Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Source),
			// Ignored when it toggles, see TriggerCrate::OnActivatedAsync()
			BoolField(N_("Turn On"), 1, 0, true)
				.WithDescription(N_("Turns the trigger on when the crate breaks, otherwise off"))
				.ShowIf(2, 0),
			BoolField(N_("Toggle"), 2)
				.WithDescription(N_("Switches the trigger instead, Turn On is ignored"))
		};

		constexpr EventParamField TriggerAreaFields[] = {
			TriggerIdField(N_("Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Target)
		};

		constexpr EventParamField TriggerZoneFields[] = {
			TriggerIdField(N_("Trigger"), 0)
				.WithLink(TriggerChannel, EventLinkRole::Source),
			BoolField(N_("Turn On"), 1, 0, true)
				.WithDescription(N_("Turns the trigger on, otherwise off"))
		};

		constexpr EventParamField WarpCoinBonusFields[] = {
			WarpIdField(N_("Warp"), 0)
				.WithLink(WarpChannel, EventLinkRole::Source),
			BoolField(N_("Fast"), 1)
				.WithDescription(N_("Moves the player instantly, without the warp animation")),
			// BonusWarp::Activate() counts the lap as WarpOrigin does
			BoolField(N_("Set Lap"), 2)
				.WithDescription(N_("Counts a lap, it's the finish line of a race")),
			UIntField(N_("Coins"), 3, 8, 0, 255, 10)
				.WithDescription(N_("Coins needed, the remaining ones turn into gems"))
		};

		constexpr EventParamField WarpOriginFields[] = {
			WarpIdField(N_("Warp"), 0)
				.WithLink(WarpChannel, EventLinkRole::Source),
			BoolField(N_("Fast"), 1)
				.WithDescription(N_("Moves the player instantly, without the warp animation")),
			BoolField(N_("Set Lap"), 2)
				.WithDescription(N_("Counts a lap, it's the finish line of a race"))
		};

		constexpr EventParamField WarpTargetFields[] = {
			WarpIdField(N_("Warp"), 0)
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
			DescribeEvent(EventType::AreaStopEnemy, "AreaStopEnemy", N_("Stop Enemy"), EventCategory::Area,
				N_("Walking enemies can't enter it."), nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaFloatUp, "AreaFloatUp", N_("Float Up"), EventCategory::Area,
				N_("Lifts players up while they're inside."), nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaHForce, "AreaHForce", N_("Horizontal Force"), EventCategory::Area,
				N_("Conveyor belt for players standing on it, or wind for players inside."), AreaHForceFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaText, "AreaText", N_("Text"), EventCategory::Area,
				N_("Shows a level text."), AreaTextFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaEndOfLevel, "AreaEndOfLevel", N_("End of Level"), EventCategory::Area,
				N_("Ends the level."), AreaEndOfLevelFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaCallback, "AreaCallback", N_("Script Callback"), EventCategory::Area,
				N_("Calls a function of the level script."), AreaCallbackFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaActivateBoss, "AreaActivateBoss", N_("Activate Boss"), EventCategory::Area,
				N_("Starts the fight with the boss of the level."), AreaActivateBossFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaFlyOff, "AreaFlyOff", N_("Fly Off"), EventCategory::Area,
				N_("Takes away the airboard and the flying carrot, and turns a bird back into its character."), nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaRevertMorph, "AreaRevertMorph", N_("Revert Morph"), EventCategory::Area,
				N_("Turns a morphed player back into their character."), nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaMorphToFrog, "AreaMorphToFrog", N_("Morph to Frog"), EventCategory::Area,
				N_("Turns the player into a frog."), nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaNoFire, "AreaNoFire", N_("No Fire"), EventCategory::Area,
				N_("Blocks or allows firing."), AreaNoFireFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaWaterBlock, "AreaWaterBlock", N_("Water Block"), EventCategory::Area,
				N_("Shallow water the player wades in."), AreaWaterBlockFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaWeather, "AreaWeather", N_("Weather"), EventCategory::Area,
				N_("Changes the weather when a player comes near."), AreaWeatherFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::AreaAmbientSound, "AreaAmbientSound", N_("Ambient Sound"), EventCategory::Area,
				N_("Plays a looping sound around the tile."), AreaAmbientSoundFields, nullptr, 0, EventTraits::Object),
			DescribeEvent(EventType::AreaAmbientBubbles, "AreaAmbientBubbles", N_("Ambient Bubbles"), EventCategory::Area,
				N_("Releases rising bubbles."), AreaAmbientBubblesFields, "Common/AmbientBubbles", std::uint32_t(AnimState::Default), EventTraits::Object),

			// Triggers
			DescribeEvent(EventType::TriggerCrate, "TriggerCrate", N_("Trigger Crate"), EventCategory::Trigger,
				N_("Crate that changes a trigger when it breaks."), TriggerCrateFields, "Object/TriggerCrate", std::uint32_t(AnimState::Default), EventTraits::Object),
			DescribeEvent(EventType::TriggerArea, "TriggerArea", N_("Trigger Area"), EventCategory::Trigger,
				N_("Tile that disappears, or shows the second frame of its animation, while the trigger is on."), TriggerAreaFields, nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::TriggerZone, "TriggerZone", N_("Trigger Zone"), EventCategory::Trigger,
				N_("Turns a trigger on or off when a player enters it."), TriggerZoneFields, nullptr, 0, EventTraits::Area),

			// Warp
			DescribeEvent(EventType::WarpCoinBonus, "WarpCoinBonus", N_("Bonus Warp"), EventCategory::Warp,
				N_("Warp that costs coins."), WarpCoinBonusFields, "Object/BonusWarp", 10 /* Bonus10, the default cost */, EventTraits::Object)
				.WithPreviewCases(3, WarpCoinBonusPreview),
			DescribeEvent(EventType::WarpOrigin, "WarpOrigin", N_("Warp Origin"), EventCategory::Warp,
				N_("Warps the player to a warp target with the same identifier."), WarpOriginFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::WarpTarget, "WarpTarget", N_("Warp Target"), EventCategory::Warp,
				N_("Where warps with the same identifier lead, a random one if there are more."), WarpTargetFields, nullptr, 0, EventTraits::None)
		};
	}

	ArrayView<const EventDescriptor> GetAreaEvents()
	{
		return Events;
	}
}

#endif
