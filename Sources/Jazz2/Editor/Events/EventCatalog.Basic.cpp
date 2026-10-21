#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"

namespace Jazz2::Editor::EventCatalogParts
{
	namespace
	{
		// Options and fields of each event, in the order of the events below

		// EventMap::GetSpawnPosition(), bit `PlayerType - 1`
		constexpr ParamOption PlayerTypeOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (characters)
			{ NC_("editor", "Jazz"), 0x01 },
			{ NC_("editor", "Spaz"), 0x02 },
			{ NC_("editor", "Lori"), 0x04 }
		};

		constexpr EventParamField LevelStartFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			FlagsField(NC_("editor", "Players"), 0, 8, PlayerTypeOptions, 0x07)
				.WithDescription(NC_("editor", "Characters that start here, a character with no start of its own uses the one of Jazz"))
		};

		// Multiplayer/Teams.h, GetTeamName()
		constexpr ParamOption TeamOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (teams)
			{ NC_("editor", "Blue"), 0 },
			{ NC_("editor", "Red"), 1 },
			{ NC_("editor", "Green"), 2 },
			{ NC_("editor", "Yellow"), 3 }
		};

		constexpr EventParamField LevelStartMultiplayerFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Team"), 0, 8, TeamOptions, 0)
				.WithDescription(NC_("editor", "Team that starts here in team modes, other modes ignore it"))
				.AsTeam()
		};

		// Checkpoint::OnActivatedAsync()
		constexpr ParamOption CheckpointThemeOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (themes of its look)
			{ NC_("editor", "Normal"), 0 },
			{ NC_("editor", "Christmas"), 1 }
		};

		constexpr EventParamField CheckpointFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(NC_("editor", "Theme"), 0, 8, CheckpointThemeOptions, 0),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(NC_("editor", "Activated"), 1)
				.WithDescription(NC_("editor", "Starts already activated, the game sets it when a player touches the checkpoint"))
		};

		// TileMap::IsPositionEmpty(), bit `1 << WeaponType`
		constexpr ParamOption WeaponOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (weapons)
			{ NC_("editor", "Blaster"), 0x001 },
			{ NC_("editor", "Bouncer"), 0x002 },
			{ NC_("editor", "Freezer"), 0x004 },
			{ NC_("editor", "Seeker"), 0x008 },
			{ NC_("editor", "RF"), 0x010 },
			{ NC_("editor", "Toaster"), 0x020 },
			{ NC_("editor", "TNT"), 0x040 },
			{ NC_("editor", "Pepper"), 0x080 },
			{ NC_("editor", "Electro"), 0x100 },
			{ NC_("editor", "Thunderbolt"), 0x200 }
		};

		constexpr EventParamField SceneryDestructFields[] = {
			// All weapons but Freezer, as the converter gives a destructible tile of any weapon
			// TRANSLATORS: Parameter of an event in the event properties
			FlagsField(NC_("editor", "Weapons"), 0, 16, WeaponOptions, 0x3FB)
				.WithDescription(NC_("editor", "Weapons that break the tile, a Freezer shot covers it in ice when Freezer isn't chosen"))
		};

		constexpr EventParamField SceneryDestructSpeedFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Speed"), 0, 8, 0, 255, 8)
				.WithDescription(NC_("editor", "Lowest speed of the player that breaks the tile, in pixels per frame"))
		};

		constexpr EventParamField SceneryCollapseFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Delay"), 0, 16, 0, 65535).WithUnit(ParamUnit::Frames)
				.WithDescription(NC_("editor", "Time after a player steps on the tile until it starts to crumble"))
		};

		// Direction, which side of the tile EventMap::IsHurting() is asked about
		constexpr ParamOption HurtSideOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (sides of a tile)
			{ NC_("editor", "Top"), 0x01 },
			{ NC_("editor", "Bottom"), 0x02 },
			{ NC_("editor", "Left"), 0x04 },
			{ NC_("editor", "Right"), 0x08 }
		};

		// Mirrored, the left side becomes the right one and the top the bottom
		constexpr std::int32_t HurtSidesMirrorX[] = { 0x04, 0x08 };
		constexpr std::int32_t HurtSidesMirrorY[] = { 0x01, 0x02 };

		constexpr EventParamField ModifierHurtFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			FlagsField(NC_("editor", "Sides"), 0, 8, HurtSideOptions, 0x01)
				.WithDescription(NC_("editor", "Sides of the tile that hurt a player touching them"))
				.MirroredXPairs(HurtSidesMirrorX).MirroredYPairs(HurtSidesMirrorY)
		};

		constexpr EventParamField ModifierTubeFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			IntField(NC_("editor", "X Speed"), 0, 8, -128, 127, 8)
				.WithDescription(NC_("editor", "Horizontal speed in pixels per tick of the original (70 ticks per second), positive is to the right"))
				.MirroredX(MirrorMode::Negate).WithDirection(DirectionAxis::X),
			// TRANSLATORS: Parameter of an event in the event properties
			IntField(NC_("editor", "Y Speed"), 1, 8, -128, 127)
				.WithDescription(NC_("editor", "Vertical speed in pixels per tick of the original (70 ticks per second), positive is down"))
				.MirroredY(MirrorMode::Negate).WithDirection(DirectionAxis::Y),
			// TRANSLATORS: Parameter of a tube event, a checkbox that makes the player go through walls ("noclip")
			BoolField(NC_("editor", "Become Noclip"), 4)
				.WithDescription(NC_("editor", "The player goes through walls until a tube without it")),
			// TRANSLATORS: Parameter of a tube event, a checkbox for a tube used only by a player going through walls ("noclip")
			BoolField(NC_("editor", "Noclip Only"), 5)
				.WithDescription(NC_("editor", "Only a player going through walls uses the tube, otherwise only a player who isn't"))
		};

		constexpr EventParamField ModifierSlideFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Strength"), 0, 8, 0, 3)
				.WithDescription(NC_("editor", "How much less the player brakes on the tile, a higher value slides further"))
		};

		constexpr EventParamField ModifierSetWaterFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Height"), 0, 16, 0, 65535).WithUnit(ParamUnit::Pixels)
				.WithDescription(NC_("editor", "New water level, from the top of the level"))
		};

		constexpr EventParamField ModifierLimitCameraViewFields[] = {
			// TRANSLATORS: Parameter of a limit camera event, the column of the left edge of the camera range
			UIntField(NC_("editor", "Left"), 0, 16, 0, 65535).WithUnit(ParamUnit::Tiles)
				.WithDescription(NC_("editor", "Column of the left edge, counted from the left of the level, 0 is the column of this event")),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(NC_("editor", "Width"), 2, 16, 0, 65535).WithUnit(ParamUnit::Tiles)
				.WithDescription(NC_("editor", "Width in tiles, 0 reaches to the right edge of the level, if it's narrower than the screen, the camera can go further to the left"))
		};

		constexpr EventDescriptor Events[] = {
			// Basic
			// TRANSLATORS: Name of an event, where the player starts the level
			DescribeEvent(EventType::LevelStart, "LevelStart", NC_("editor", "Level Start"), EventCategory::Basic,
				NC_("editor", "Where the player starts the level."), LevelStartFields, nullptr, 0, EventTraits::None),
			// TRANSLATORS: Name of an event, where players start in multiplayer
			DescribeEvent(EventType::LevelStartMultiplayer, "LevelStartMultiplayer", NC_("editor", "Multiplayer Start"), EventCategory::Basic,
				NC_("editor", "Where players start and respawn in multiplayer, or the start of a level without any other."), LevelStartMultiplayerFields, nullptr, 0, EventTraits::None),
			// TRANSLATORS: Name of an event, a sign that saves the progress
			DescribeEvent(EventType::Checkpoint, "Checkpoint", NC_("editor", "Checkpoint"), EventCategory::Basic,
				NC_("editor", "Saves the progress when a player touches it."), CheckpointFields, "Object/Checkpoint", std::uint32_t(AnimState::Default), EventTraits::Object),

			// Scenery
			// TRANSLATORS: Name of an event, a tile modifier that makes the tile breakable by weapons
			DescribeEvent(EventType::SceneryDestruct, "SceneryDestruct", NC_("editor", "Destructible Scenery"), EventCategory::Scenery,
				NC_("editor", "Tile broken by the chosen weapons, it plays its animated tile as it breaks."), SceneryDestructFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that makes the tile breakable by a buttstomp
			DescribeEvent(EventType::SceneryDestructButtstomp, "SceneryDestructButtstomp", NC_("editor", "Buttstomp Scenery"), EventCategory::Scenery,
				NC_("editor", "Tile broken by a buttstomp, an uppercut, a sidekick or TNT."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that makes the tile breakable by running into it fast
			DescribeEvent(EventType::SceneryDestructSpeed, "SceneryDestructSpeed", NC_("editor", "Speed Scenery"), EventCategory::Scenery,
				NC_("editor", "Tile broken by a player moving fast enough."), SceneryDestructSpeedFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that makes the tile crumble when stepped on
			DescribeEvent(EventType::SceneryCollapse, "SceneryCollapse", NC_("editor", "Collapsing Scenery"), EventCategory::Scenery,
				NC_("editor", "Tile that crumbles away after a player steps on it."), SceneryCollapseFields, nullptr, 0, EventTraits::TileModifier),

			// Modifiers
			// TRANSLATORS: Name of an event, a tile modifier the player hangs from and moves along
			DescribeEvent(EventType::ModifierVine, "ModifierVine", NC_("editor", "Vine"), EventCategory::Modifier,
				NC_("editor", "Tile the player hangs from and moves along."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier for a platform the player can jump up through
			DescribeEvent(EventType::ModifierOneWay, "ModifierOneWay", NC_("editor", "One Way"), EventCategory::Modifier,
				NC_("editor", "Tile the player stands on but jumps up through."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event (noun), a tile modifier the player hangs from without moving
			DescribeEvent(EventType::ModifierHook, "ModifierHook", NC_("editor", "Hook"), EventCategory::Modifier,
				NC_("editor", "Tile the player hangs from without moving."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a pole that spins the player around and flings them sideways
			DescribeEvent(EventType::ModifierHPole, "ModifierHPole", NC_("editor", "Horizontal Pole"), EventCategory::Modifier,
				NC_("editor", "Pole that spins the player around and flings them sideways."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a pole that spins the player around and flings them up or down
			DescribeEvent(EventType::ModifierVPole, "ModifierVPole", NC_("editor", "Vertical Pole"), EventCategory::Modifier,
				NC_("editor", "Pole that spins the player around and flings them up or down."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that hurts the player who touches it
			DescribeEvent(EventType::ModifierHurt, "ModifierHurt", NC_("editor", "Hurt"), EventCategory::Modifier,
				NC_("editor", "Tile whose chosen sides hurt the player."), ModifierHurtFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, an area that drives the player through it like a tube
			DescribeEvent(EventType::ModifierTube, "ModifierTube", NC_("editor", "Tube"), EventCategory::Modifier,
				NC_("editor", "Drives the player at the given speed while they pass through it."), ModifierTubeFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, a tile modifier that shots bounce off
			DescribeEvent(EventType::ModifierRicochet, "ModifierRicochet", NC_("editor", "Ricochet"), EventCategory::Modifier,
				NC_("editor", "Tile that Blaster shots bounce off."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that makes the player slide
			DescribeEvent(EventType::ModifierSlide, "ModifierSlide", NC_("editor", "Slide"), EventCategory::Modifier,
				NC_("editor", "Floor the player slides further on, except in Reforged mode."), ModifierSlideFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, an area that kills the player
			DescribeEvent(EventType::ModifierDeath, "ModifierDeath", NC_("editor", "Death"), EventCategory::Modifier,
				NC_("editor", "Kills the player instantly on touch."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that changes the water level
			DescribeEvent(EventType::ModifierSetWater, "ModifierSetWater", NC_("editor", "Set Water Level"), EventCategory::Modifier,
				NC_("editor", "Changes the water level when a player touches it."), ModifierSetWaterFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that limits how far the camera scrolls horizontally
			DescribeEvent(EventType::ModifierLimitCameraView, "ModifierLimitCameraView", NC_("editor", "Limit Camera"), EventCategory::Modifier,
				NC_("editor", "Limits the camera horizontally when a player touches it."), ModifierLimitCameraViewFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, a tile modifier for a ledge the player can't climb onto
			DescribeEvent(EventType::ModifierNoClimb, "ModifierNoClimb", NC_("editor", "No Climb"), EventCategory::Modifier,
				NC_("editor", "Ledge the player can't climb up onto."), nullptr, 0, EventTraits::TileModifier)
		};
	}

	ArrayView<const EventDescriptor> GetBasicEvents()
	{
		return Events;
	}
}

#endif
