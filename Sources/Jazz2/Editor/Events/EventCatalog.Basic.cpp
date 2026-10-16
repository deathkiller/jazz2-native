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
			{ N_("Jazz"), 0x01 },
			{ N_("Spaz"), 0x02 },
			{ N_("Lori"), 0x04 }
		};

		constexpr EventParamField LevelStartFields[] = {
			FlagsField(N_("Players"), 0, 8, PlayerTypeOptions, 0x07)
				.WithDescription(N_("Characters that start here, a character with no start of its own uses the one of Jazz"))
		};

		// Multiplayer/Teams.h, GetTeamName()
		constexpr ParamOption TeamOptions[] = {
			{ N_("Blue"), 0 },
			{ N_("Red"), 1 },
			{ N_("Green"), 2 },
			{ N_("Yellow"), 3 }
		};

		constexpr EventParamField LevelStartMultiplayerFields[] = {
			EnumField(N_("Team"), 0, 8, TeamOptions, 0)
				.WithDescription(N_("Team that starts here in team modes, other modes ignore it"))
		};

		// Checkpoint::OnActivatedAsync()
		constexpr ParamOption CheckpointThemeOptions[] = {
			{ N_("Normal"), 0 },
			{ N_("Christmas"), 1 }
		};

		constexpr EventParamField CheckpointFields[] = {
			EnumField(N_("Theme"), 0, 8, CheckpointThemeOptions, 0),
			BoolField(N_("Activated"), 1)
				.WithDescription(N_("Starts already activated, the game sets it when a player touches the checkpoint"))
		};

		// TileMap::IsPositionEmpty(), bit `1 << WeaponType`
		constexpr ParamOption WeaponOptions[] = {
			{ N_("Blaster"), 0x001 },
			{ N_("Bouncer"), 0x002 },
			{ N_("Freezer"), 0x004 },
			{ N_("Seeker"), 0x008 },
			{ N_("RF"), 0x010 },
			{ N_("Toaster"), 0x020 },
			{ N_("TNT"), 0x040 },
			{ N_("Pepper"), 0x080 },
			{ N_("Electro"), 0x100 },
			{ N_("Thunderbolt"), 0x200 }
		};

		constexpr EventParamField SceneryDestructFields[] = {
			// All weapons but Freezer, as the converter gives a destructible tile of any weapon
			FlagsField(N_("Weapons"), 0, 16, WeaponOptions, 0x3FB)
				.WithDescription(N_("Weapons that break the tile, a Freezer shot covers it in ice when Freezer isn't chosen"))
		};

		constexpr EventParamField SceneryDestructSpeedFields[] = {
			UIntField(N_("Speed"), 0, 8, 0, 255, 8)
				.WithDescription(N_("Lowest speed of the player that breaks the tile, in pixels per frame"))
		};

		constexpr EventParamField SceneryCollapseFields[] = {
			UIntField(N_("Delay"), 0, 16, 0, 65535).WithUnit(ParamUnit::Frames)
				.WithDescription(N_("Time after a player steps on the tile until it starts to crumble"))
		};

		// Direction, which side of the tile EventMap::IsHurting() is asked about
		constexpr ParamOption HurtSideOptions[] = {
			{ N_("Top"), 0x01 },
			{ N_("Bottom"), 0x02 },
			{ N_("Left"), 0x04 },
			{ N_("Right"), 0x08 }
		};

		constexpr EventParamField ModifierHurtFields[] = {
			FlagsField(N_("Sides"), 0, 8, HurtSideOptions, 0x01)
				.WithDescription(N_("Sides of the tile that hurt a player touching them"))
		};

		constexpr EventParamField ModifierTubeFields[] = {
			IntField(N_("X Speed"), 0, 8, -128, 127, 8)
				.WithDescription(N_("Horizontal speed in pixels per tick of the original (70 ticks per second), positive is to the right")),
			IntField(N_("Y Speed"), 1, 8, -128, 127)
				.WithDescription(N_("Vertical speed in pixels per tick of the original (70 ticks per second), positive is down")),
			BoolField(N_("Become Noclip"), 4)
				.WithDescription(N_("The player goes through walls until a tube without it")),
			BoolField(N_("Noclip Only"), 5)
				.WithDescription(N_("Only a player going through walls uses the tube, otherwise only a player who isn't"))
		};

		constexpr EventParamField ModifierSlideFields[] = {
			UIntField(N_("Strength"), 0, 8, 0, 3)
				.WithDescription(N_("How much less the player brakes on the tile, a higher value slides further"))
		};

		constexpr EventParamField ModifierSetWaterFields[] = {
			UIntField(N_("Height"), 0, 16, 0, 65535).WithUnit(ParamUnit::Pixels)
				.WithDescription(N_("New water level, from the top of the level"))
		};

		constexpr EventParamField ModifierLimitCameraViewFields[] = {
			UIntField(N_("Left"), 0, 16, 0, 65535).WithUnit(ParamUnit::Tiles)
				.WithDescription(N_("Column of the left edge, counted from the left of the level, 0 is the column of this event")),
			UIntField(N_("Width"), 2, 16, 0, 65535).WithUnit(ParamUnit::Tiles)
				.WithDescription(N_("Width in tiles, 0 reaches to the right edge of the level, if it's narrower than the screen, the camera can go further to the left"))
		};

		constexpr EventDescriptor Events[] = {
			// Basic
			DescribeEvent(EventType::LevelStart, "LevelStart", N_("Level Start"), EventCategory::Basic,
				N_("Where the player starts the level."), LevelStartFields, nullptr, 0, EventTraits::None),
			DescribeEvent(EventType::LevelStartMultiplayer, "LevelStartMultiplayer", N_("Multiplayer Start"), EventCategory::Basic,
				N_("Where players start and respawn in multiplayer, or the start of a level without any other."), LevelStartMultiplayerFields, nullptr, 0, EventTraits::None),
			DescribeEvent(EventType::Checkpoint, "Checkpoint", N_("Checkpoint"), EventCategory::Basic,
				N_("Saves the progress when a player touches it."), CheckpointFields, "Object/Checkpoint", std::uint32_t(AnimState::Default), EventTraits::Object),

			// Scenery
			DescribeEvent(EventType::SceneryDestruct, "SceneryDestruct", N_("Destructible Scenery"), EventCategory::Scenery,
				N_("Tile broken by the chosen weapons, it plays its animated tile as it breaks."), SceneryDestructFields, nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::SceneryDestructButtstomp, "SceneryDestructButtstomp", N_("Buttstomp Scenery"), EventCategory::Scenery,
				N_("Tile broken by a buttstomp, an uppercut, a sidekick or TNT."), nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::SceneryDestructSpeed, "SceneryDestructSpeed", N_("Speed Scenery"), EventCategory::Scenery,
				N_("Tile broken by a player moving fast enough."), SceneryDestructSpeedFields, nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::SceneryCollapse, "SceneryCollapse", N_("Collapsing Scenery"), EventCategory::Scenery,
				N_("Tile that crumbles away after a player steps on it."), SceneryCollapseFields, nullptr, 0, EventTraits::TileModifier),

			// Modifiers
			DescribeEvent(EventType::ModifierVine, "ModifierVine", N_("Vine"), EventCategory::Modifier,
				N_("Tile the player hangs from and moves along."), nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::ModifierOneWay, "ModifierOneWay", N_("One Way"), EventCategory::Modifier,
				N_("Tile the player stands on but jumps up through."), nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::ModifierHook, "ModifierHook", N_("Hook"), EventCategory::Modifier,
				N_("Tile the player hangs from without moving."), nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::ModifierHPole, "ModifierHPole", N_("Horizontal Pole"), EventCategory::Modifier,
				N_("Pole that spins the player around and flings them sideways."), nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::ModifierVPole, "ModifierVPole", N_("Vertical Pole"), EventCategory::Modifier,
				N_("Pole that spins the player around and flings them up or down."), nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::ModifierHurt, "ModifierHurt", N_("Hurt"), EventCategory::Modifier,
				N_("Tile whose chosen sides hurt the player."), ModifierHurtFields, nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::ModifierTube, "ModifierTube", N_("Tube"), EventCategory::Modifier,
				N_("Drives the player at the given speed while they pass through it."), ModifierTubeFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::ModifierRicochet, "ModifierRicochet", N_("Ricochet"), EventCategory::Modifier,
				N_("Tile that Blaster shots bounce off."), nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::ModifierSlide, "ModifierSlide", N_("Slide"), EventCategory::Modifier,
				N_("Floor the player slides further on, except in Reforged mode."), ModifierSlideFields, nullptr, 0, EventTraits::TileModifier),
			DescribeEvent(EventType::ModifierDeath, "ModifierDeath", N_("Death"), EventCategory::Modifier,
				N_("Kills the player instantly on touch."), nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::ModifierSetWater, "ModifierSetWater", N_("Set Water Level"), EventCategory::Modifier,
				N_("Changes the water level when a player touches it."), ModifierSetWaterFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::ModifierLimitCameraView, "ModifierLimitCameraView", N_("Limit Camera"), EventCategory::Modifier,
				N_("Limits the camera horizontally when a player touches it."), ModifierLimitCameraViewFields, nullptr, 0, EventTraits::Area),
			DescribeEvent(EventType::ModifierNoClimb, "ModifierNoClimb", N_("No Climb"), EventCategory::Modifier,
				N_("Ledge the player can't climb up onto."), nullptr, 0, EventTraits::TileModifier)
		};
	}

	ArrayView<const EventDescriptor> GetBasicEvents()
	{
		return Events;
	}
}

#endif
