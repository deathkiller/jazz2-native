#include "EventCatalog.h"

#if defined(WITH_EDITOR)

#include "../../AnimState.h"
#include "../../../nCine/I18n.h"

using namespace nCine;

namespace Jazz2::Editor::EventCatalogParts
{
	ArrayView<const EventDescriptor> GetBasicEvents()
	{
		// Built on the first call, after the language is loaded, only what's translated here by _x() is translated, see CatalogText
		// Options and fields of each event, in the order of the events below

		// EventMap::GetSpawnPosition(), bit `PlayerType - 1`, names of characters aren't translated
		static const ParamOption PlayerTypeOptions[] = {
			{ "Jazz", 0x01 },
			{ "Spaz", 0x02 },
			{ "Lori", 0x04 }
		};

		static const EventParamField LevelStartFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			FlagsField(_x("editor", "Players"), 0, 8, PlayerTypeOptions, 0x07)
				.WithDescription(_x("editor", "Characters that start here, a character with no start of its own uses the one of Jazz"))
		};

		// Multiplayer/Teams.h, GetTeamName()
		static const ParamOption TeamOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (teams)
			{ _x("editor", "Blue"), 0 },
			{ _x("editor", "Red"), 1 },
			{ _x("editor", "Green"), 2 },
			{ _x("editor", "Yellow"), 3 }
		};

		static const EventParamField LevelStartMultiplayerFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Team"), 0, 8, TeamOptions, 0)
				.WithDescription(_x("editor", "Team that starts here in team modes, other modes ignore it"))
				.AsTeam()
		};

		// Checkpoint::OnActivatedAsync()
		static const ParamOption CheckpointThemeOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (themes of its look)
			{ _x("editor", "Normal"), 0 },
			{ _x("editor", "Christmas"), 1 }
		};

		static const EventParamField CheckpointFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			EnumField(_x("editor", "Theme"), 0, 8, CheckpointThemeOptions, 0),
			// TRANSLATORS: Parameter of an event in the event properties
			BoolField(_x("editor", "Activated"), 1)
				.WithDescription(_x("editor", "Starts already activated, the game sets it when a player touches the checkpoint"))
		};

		// TileMap::IsPositionEmpty(), bit `1 << WeaponType`, names of weapons aren't translated
		static const ParamOption WeaponOptions[] = {
			{ "Blaster", 0x001 },
			{ "Bouncer", 0x002 },
			{ "Freezer", 0x004 },
			{ "Seeker", 0x008 },
			{ "RF", 0x010 },
			{ "Toaster", 0x020 },
			{ "TNT", 0x040 },
			{ "Pepper", 0x080 },
			{ "Electro", 0x100 },
			{ "Thunderbolt", 0x200 }
		};

		static const EventParamField SceneryDestructFields[] = {
			// All weapons but Freezer, as the converter gives a destructible tile of any weapon
			// TRANSLATORS: Parameter of an event in the event properties
			FlagsField(_x("editor", "Weapons"), 0, 16, WeaponOptions, 0x3FB)
				// TRANSLATORS: Description of a parameter of an event, Freezer is a weapon, names of weapons aren't translated
				.WithDescription(_x("editor", "Weapons that break the tile, a Freezer shot covers it in ice when Freezer isn't chosen"))
		};

		static const EventParamField SceneryDestructSpeedFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Speed"), 0, 8, 0, 255, 8)
				.WithDescription(_x("editor", "Lowest speed of the player that breaks the tile, in pixels per frame"))
		};

		static const EventParamField SceneryCollapseFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Delay"), 0, 16, 0, 65535).WithUnit(ParamUnit::Frames)
				.WithDescription(_x("editor", "Time after a player steps on the tile until it starts to crumble"))
		};

		// Direction, which side of the tile EventMap::IsHurting() is asked about
		static const ParamOption HurtSideOptions[] = {
			// TRANSLATORS: Choices of a parameter of an event (sides of a tile)
			{ _x("editor", "Top"), 0x01 },
			{ _x("editor", "Bottom"), 0x02 },
			{ _x("editor", "Left"), 0x04 },
			{ _x("editor", "Right"), 0x08 }
		};

		// Mirrored, the left side becomes the right one and the top the bottom
		static const std::int32_t HurtSidesMirrorX[] = { 0x04, 0x08 };
		static const std::int32_t HurtSidesMirrorY[] = { 0x01, 0x02 };

		static const EventParamField ModifierHurtFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			FlagsField(_x("editor", "Sides"), 0, 8, HurtSideOptions, 0x01)
				.WithDescription(_x("editor", "Sides of the tile that hurt a player touching them"))
				.MirroredXPairs(HurtSidesMirrorX).MirroredYPairs(HurtSidesMirrorY)
		};

		static const EventParamField ModifierTubeFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			IntField(_x("editor", "X Speed"), 0, 8, -128, 127, 8)
				.WithDescription(_x("editor", "Horizontal speed in pixels per tick of the original (70 ticks per second), positive is to the right"))
				.MirroredX(MirrorMode::Negate).WithDirection(DirectionAxis::X),
			// TRANSLATORS: Parameter of an event in the event properties
			IntField(_x("editor", "Y Speed"), 1, 8, -128, 127)
				.WithDescription(_x("editor", "Vertical speed in pixels per tick of the original (70 ticks per second), positive is down"))
				.MirroredY(MirrorMode::Negate).WithDirection(DirectionAxis::Y),
			// TRANSLATORS: Parameter of a tube event, a checkbox that makes the player go through walls ("noclip")
			BoolField(_x("editor", "Become Noclip"), 4)
				.WithDescription(_x("editor", "The player goes through walls until a tube without it")),
			// TRANSLATORS: Parameter of a tube event, a checkbox for a tube used only by a player going through walls ("noclip")
			BoolField(_x("editor", "Noclip Only"), 5)
				.WithDescription(_x("editor", "Only a player going through walls uses the tube, otherwise only a player who isn't"))
		};

		static const EventParamField ModifierSlideFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Strength"), 0, 8, 0, 3)
				.WithDescription(_x("editor", "How much less the player brakes on the tile, a higher value slides further"))
		};

		static const EventParamField ModifierSetWaterFields[] = {
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Height"), 0, 16, 0, 65535).WithUnit(ParamUnit::Pixels)
				.WithDescription(_x("editor", "New water level, from the top of the level"))
		};

		static const EventParamField ModifierLimitCameraViewFields[] = {
			// TRANSLATORS: Parameter of a limit camera event, the column of the left edge of the camera range
			UIntField(_x("editor", "Left"), 0, 16, 0, 65535).WithUnit(ParamUnit::Tiles)
				.WithDescription(_x("editor", "Column of the left edge, counted from the left of the level, 0 is the column of this event")),
			// TRANSLATORS: Parameter of an event in the event properties
			UIntField(_x("editor", "Width"), 2, 16, 0, 65535).WithUnit(ParamUnit::Tiles)
				.WithDescription(_x("editor", "Width in tiles, 0 reaches to the right edge of the level, if it's narrower than the screen, the camera can go further to the left"))
		};

		static const EventDescriptor Events[] = {
			// Basic
			// TRANSLATORS: Name of an event, where the player starts the level
			DescribeEvent(EventType::LevelStart, "LevelStart", _x("editor", "Level Start"), EventCategory::Basic,
				_x("editor", "Where the player starts the level."), LevelStartFields, nullptr, 0, EventTraits::None),
			// TRANSLATORS: Name of an event, where players start in multiplayer
			DescribeEvent(EventType::LevelStartMultiplayer, "LevelStartMultiplayer", _x("editor", "Multiplayer Start"), EventCategory::Basic,
				_x("editor", "Where players start and respawn in multiplayer, or the start of a level without any other."), LevelStartMultiplayerFields, nullptr, 0, EventTraits::None),
			// TRANSLATORS: Name of an event, a sign that saves the progress
			DescribeEvent(EventType::Checkpoint, "Checkpoint", _x("editor", "Checkpoint"), EventCategory::Basic,
				_x("editor", "Saves the progress when a player touches it."), CheckpointFields, "Object/Checkpoint", std::uint32_t(AnimState::Default), EventTraits::Object),

			// Scenery
			// TRANSLATORS: Name of an event, a tile modifier that makes the tile breakable by weapons
			DescribeEvent(EventType::SceneryDestruct, "SceneryDestruct", _x("editor", "Destructible Scenery"), EventCategory::Scenery,
				_x("editor", "Tile broken by the chosen weapons, it plays its animated tile as it breaks."), SceneryDestructFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that makes the tile breakable by a buttstomp
			DescribeEvent(EventType::SceneryDestructButtstomp, "SceneryDestructButtstomp", _x("editor", "Buttstomp Scenery"), EventCategory::Scenery,
				// TRANSLATORS: Description of an event, TNT is a weapon, names of weapons aren't translated
				_x("editor", "Tile broken by a buttstomp, an uppercut, a sidekick or TNT."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that makes the tile breakable by running into it fast
			DescribeEvent(EventType::SceneryDestructSpeed, "SceneryDestructSpeed", _x("editor", "Speed Scenery"), EventCategory::Scenery,
				_x("editor", "Tile broken by a player moving fast enough."), SceneryDestructSpeedFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that makes the tile crumble when stepped on
			DescribeEvent(EventType::SceneryCollapse, "SceneryCollapse", _x("editor", "Collapsing Scenery"), EventCategory::Scenery,
				_x("editor", "Tile that crumbles away after a player steps on it."), SceneryCollapseFields, nullptr, 0, EventTraits::TileModifier),

			// Modifiers
			// TRANSLATORS: Name of an event, a tile modifier the player hangs from and moves along
			DescribeEvent(EventType::ModifierVine, "ModifierVine", _x("editor", "Vine"), EventCategory::Modifier,
				_x("editor", "Tile the player hangs from and moves along."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier for a platform the player can jump up through
			DescribeEvent(EventType::ModifierOneWay, "ModifierOneWay", _x("editor", "One Way"), EventCategory::Modifier,
				_x("editor", "Tile the player stands on but jumps up through."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event (noun), a tile modifier the player hangs from without moving
			DescribeEvent(EventType::ModifierHook, "ModifierHook", _x("editor", "Hook"), EventCategory::Modifier,
				_x("editor", "Tile the player hangs from without moving."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a pole that spins the player around and flings them sideways
			DescribeEvent(EventType::ModifierHPole, "ModifierHPole", _x("editor", "Horizontal Pole"), EventCategory::Modifier,
				_x("editor", "Pole that spins the player around and flings them sideways."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a pole that spins the player around and flings them up or down
			DescribeEvent(EventType::ModifierVPole, "ModifierVPole", _x("editor", "Vertical Pole"), EventCategory::Modifier,
				_x("editor", "Pole that spins the player around and flings them up or down."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that hurts the player who touches it
			DescribeEvent(EventType::ModifierHurt, "ModifierHurt", _x("editor", "Hurt"), EventCategory::Modifier,
				_x("editor", "Tile whose chosen sides hurt the player."), ModifierHurtFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, an area that drives the player through it like a tube
			DescribeEvent(EventType::ModifierTube, "ModifierTube", _x("editor", "Tube"), EventCategory::Modifier,
				_x("editor", "Drives the player at the given speed while they pass through it."), ModifierTubeFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, a tile modifier that shots bounce off
			DescribeEvent(EventType::ModifierRicochet, "ModifierRicochet", _x("editor", "Ricochet"), EventCategory::Modifier,
				// TRANSLATORS: Description of an event, Blaster is a weapon, names of weapons aren't translated
				_x("editor", "Tile that Blaster shots bounce off."), nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, a tile modifier that makes the player slide
			DescribeEvent(EventType::ModifierSlide, "ModifierSlide", _x("editor", "Slide"), EventCategory::Modifier,
				_x("editor", "Floor the player slides further on, except in Reforged mode."), ModifierSlideFields, nullptr, 0, EventTraits::TileModifier),
			// TRANSLATORS: Name of an event, an area that kills the player
			DescribeEvent(EventType::ModifierDeath, "ModifierDeath", _x("editor", "Death"), EventCategory::Modifier,
				_x("editor", "Kills the player instantly on touch."), nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that changes the water level
			DescribeEvent(EventType::ModifierSetWater, "ModifierSetWater", _x("editor", "Set Water Level"), EventCategory::Modifier,
				_x("editor", "Changes the water level when a player touches it."), ModifierSetWaterFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, an area that limits how far the camera scrolls horizontally
			DescribeEvent(EventType::ModifierLimitCameraView, "ModifierLimitCameraView", _x("editor", "Limit Camera"), EventCategory::Modifier,
				_x("editor", "Limits the camera horizontally when a player touches it."), ModifierLimitCameraViewFields, nullptr, 0, EventTraits::Area),
			// TRANSLATORS: Name of an event, a tile modifier for a ledge the player can't climb onto
			DescribeEvent(EventType::ModifierNoClimb, "ModifierNoClimb", _x("editor", "No Climb"), EventCategory::Modifier,
				_x("editor", "Ledge the player can't climb up onto."), nullptr, 0, EventTraits::TileModifier)
		};

		return Events;
	}
}

#endif
