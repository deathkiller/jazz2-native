#include "LevelCheck.h"

#if defined(WITH_EDITOR)

#include "LevelStatistics.h"
#include "ShellCommon.h"
#include "ShellPanels.h"
#include "../Tools/EditorContext.h"
#include "../Document/CommandHistory.h"
#include "../Document/LevelDocument.h"
#include "../Files/EditorSettings.h"
#include "../Input/ActionRegistry.h"
#include "../Tools/EventTools.h"
#include "../Document/Commands.h"
#include "../Document/DocumentChanges.h"
#include "../Events/EventCatalog.h"
#include "../Events/EventPreviews.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// An event of the grid or off it, at the tile it's in, with its index if it's off the grid
		struct CheckedEvent
		{
			const Formats::EventRecord* Record;
			Vector2i Cell;
			std::int32_t OffGrid;
		};

		// Identifiers an event uses of a kind, as a warp, a trigger or a rolling rock
		struct IdUse
		{
			std::int32_t Id;
			Vector2i Cell;
			std::int32_t OffGrid;
		};

		bool HasId(const SmallVectorImpl<IdUse>& uses, std::int32_t id)
		{
			return std::any_of(uses.begin(), uses.end(), [id](const IdUse& use) { return use.Id == id; });
		}

		// Problems of the same kind are told once, with how many there are and where the first one is, a level can have
		// hundreds of tiles of a trigger area or the same parameter wrong in each copy of an event
		struct ProblemGroup
		{
			std::uint64_t Key;
			std::int32_t Count;
			Vector2i Cell;
			std::int32_t OffGrid;
			std::int32_t Value;
			std::uint16_t Type;
		};

		ProblemGroup& FindGroup(SmallVectorImpl<ProblemGroup>& groups, std::uint64_t key, const CheckedEvent& at, std::int32_t value = 0)
		{
			for (ProblemGroup& group : groups) {
				if (group.Key == key) {
					group.Count++;
					return group;
				}
			}
			return groups.emplace_back(ProblemGroup{ key, 1, at.Cell, at.OffGrid, value, at.Record->Type });
		}
	}

	void CheckLevel(const LevelDocument& document, SmallVectorImpl<LevelProblem>& problems)
	{
		auto add = [&problems](LevelProblemSeverity severity, String text, Vector2i cell = Vector2i(-1, -1), std::int32_t offGrid = -1, std::int32_t layer = -1) -> LevelProblem& {
			LevelProblem& problem = problems.emplace_back();
			problem.Severity = severity;
			problem.Text = Death::move(text);
			problem.Cell = cell;
			problem.OffGrid = offGrid;
			problem.Layer = layer;
			return problem;
		};

		Vector2i gridSize = document.GetEventGridSize();
		SmallVector<CheckedEvent, 0> events;
		events.reserve(document.Events.size() + document.OffGridEvents.size());
		std::uint32_t gridWidth = std::uint32_t(std::max<std::int32_t>(gridSize.X, 1));
		for (const DocumentEvent& event : document.Events) {
			events.push_back(CheckedEvent{ &event.Record, Vector2i(std::int32_t(event.Cell % gridWidth), std::int32_t(event.Cell / gridWidth)), -1 });
		}
		for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
			const Formats::OffGridEventRecord& event = document.OffGridEvents[i];
			events.push_back(CheckedEvent{ &event.Event, Vector2i(std::int32_t(event.X / 32), std::int32_t(event.Y / 32)), std::int32_t(i) });
		}

		SmallVector<IdUse, 0> warpOrigins, warpTargets, triggerSetters, triggerTiles, rocks, rockTriggers;
		SmallVector<IdUse, 0> multiplayerStarts, ctfBases;
		// Race waypoints by group and order, see MpLevelHandler::BuildTrackCheckpoints()
		struct Waypoint
		{
			std::uint8_t Group;
			std::uint8_t Order;
			Vector2i Cell;
			std::int32_t OffGrid;
		};
		SmallVector<Waypoint, 0> waypoints;
		SmallVector<ProblemGroup, 0> unknownEvents, outOfRange, missingTexts;
		bool hasMultiplayerStart = false;
		std::int32_t starts[3] = {};
		Vector2i firstStartCell[3] = { Vector2i(-1, -1), Vector2i(-1, -1), Vector2i(-1, -1) };
		std::int32_t multiplayerOnlyEvents = 0;
		CheckedEvent firstMultiplayerOnly{};

		for (const CheckedEvent& checked : events) {
			const Formats::EventRecord& event = *checked.Record;
			EventType type = EventType(event.Type);
			if (type == EventType::Empty) {
				// A tile without an event that keeps only its flags, nothing the game would spawn
				continue;
			}
			const EventDescriptor* descriptor = EventCatalog::Find(type);
			if (descriptor == nullptr) {
				FindGroup(unknownEvents, event.Type, checked);
				continue;
			}
			if ((event.Flags & Formats::EventRecord::MultiplayerOnly) != 0 || (descriptor->Traits & EventTraits::MultiplayerOnly) == EventTraits::MultiplayerOnly) {
				if (multiplayerOnlyEvents++ == 0) {
					firstMultiplayerOnly = checked;
				}
			}
			switch (type) {
				case EventType::LevelStartMultiplayer:
					multiplayerStarts.push_back(IdUse{ event.Params[0], checked.Cell, checked.OffGrid });
					break;
				case EventType::CtfBase:
					ctfBases.push_back(IdUse{ event.Params[0], checked.Cell, checked.OffGrid });
					break;
				case EventType::RaceCheckpoint:
					waypoints.push_back(Waypoint{ event.Params[1], event.Params[0], checked.Cell, checked.OffGrid });
					break;
				default:
					break;
			}

			// Parameters out of the range the game takes, the first value is told
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				const EventParamField& field = descriptor->Fields[i];
				if ((field.Type != ParamType::UInt && field.Type != ParamType::Int) || field.Max <= field.Min) {
					continue;
				}
				std::int32_t value = EventCatalog::ReadField(event, field);
				if (value < field.Min || value > field.Max) {
					FindGroup(outOfRange, (std::uint64_t(event.Type) << 8) | i, checked, value);
				}
			}

			// Texts it shows
			if (const EventParamField* field = FindEventField(descriptor, ParamType::TextId)) {
				std::int32_t index = EventCatalog::ReadField(event, *field);
				if (index < 0 || index >= std::int32_t(document.Texts.size()) || document.Texts[index].empty()) {
					FindGroup(missingTexts, std::uint64_t(std::uint32_t(index)), checked, index);
				}
			}

			IdUse use{ event.Params[0], checked.Cell, checked.OffGrid };
			switch (type) {
				case EventType::LevelStart:
					for (std::int32_t i = 0; i < 3; i++) {
						if ((event.Params[0] & (1 << i)) != 0) {
							if (starts[i]++ == 0) {
								firstStartCell[i] = checked.Cell;
							}
						}
					}
					break;
				case EventType::LevelStartMultiplayer:
					hasMultiplayerStart = true;
					break;
				case EventType::WarpTarget:
					warpTargets.push_back(use);
					break;
				case EventType::TriggerArea:
					triggerTiles.push_back(use);
					break;
				case EventType::TriggerCrate:
				case EventType::TriggerZone:
					triggerSetters.push_back(use);
					break;
				case EventType::RollingRock:
					rocks.push_back(use);
					break;
				case EventType::RollingRockTrigger:
					rockTriggers.push_back(use);
					break;
				default:
					if (const EventParamField* field = FindEventField(descriptor, ParamType::WarpId)) {
						use.Id = EventCatalog::ReadField(event, *field);
						warpOrigins.push_back(use);
					}
					break;
			}
		}

		for (const ProblemGroup& group : unknownEvents) {
			add(LevelProblemSeverity::Info, group.Count > 1
				// TRANSLATORS: In the Check Level dialog, the first {} is the number of the event type, the second how many times it's in the level
				? _fnx("editor", "Unknown event {} - the game doesn't know it ({} time)", "Unknown event {} - the game doesn't know it ({} times)", group.Count, group.Type, group.Count)
				// TRANSLATORS: In the Check Level dialog, {} is the number of the event type
				: _fx("editor", "Unknown event {} - the game doesn't know it", group.Type), group.Cell, group.OffGrid);
		}
		for (const ProblemGroup& group : outOfRange) {
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(group.Type));
			std::uint8_t fieldIndex = std::uint8_t(group.Key & 0xFF);
			if (descriptor == nullptr || fieldIndex >= descriptor->FieldCount) {
				continue;
			}
			const EventParamField& field = descriptor->Fields[fieldIndex];
			String name = EventPreviews::GetEventName(group.Type);
			add(LevelProblemSeverity::Warning, group.Count > 1
				// TRANSLATORS: In the Check Level dialog, the name of the event, the name of its parameter, the lowest and the highest value it can have, and the number of events
				? _fnx("editor", "{}: {} is out of its range from {} to {} in {} event", "{}: {} is out of its range from {} to {} in {} events", group.Count,
					name, field.Name, field.Min, field.Max, group.Count)
				// TRANSLATORS: In the Check Level dialog, the name of the event, the name of its parameter, its value, and the lowest and the highest value it can have
				: _fx("editor", "{}: {} is {}, it can be from {} to {}", name, field.Name, group.Value, field.Min, field.Max), group.Cell, group.OffGrid);
		}
		for (const ProblemGroup& group : missingTexts) {
			bool exists = (group.Value >= 0 && group.Value < std::int32_t(document.Texts.size()));
			String text;
			if (group.Count > 1) {
				// TRANSLATORS: In the Check Level dialog, the number of a text of the level and the number of events that show it
				text = (exists ? _fnx("editor", "Text {} is empty, {} event shows it", "Text {} is empty, {} events show it", group.Count, group.Value, group.Count)
					// TRANSLATORS: In the Check Level dialog, the number of a text of the level and the number of events that show it
					: _fnx("editor", "Text {} doesn't exist, {} event shows it", "Text {} doesn't exist, {} events show it", group.Count, group.Value, group.Count));
			} else {
				String name = EventPreviews::GetEventName(group.Type);
				// TRANSLATORS: In the Check Level dialog, the name of the event and the number of the text of the level it shows
				text = (exists ? _fx("editor", "{}: text {} is empty", name, group.Value) : _fx("editor", "{}: text {} doesn't exist", name, group.Value));
			}
			add(LevelProblemSeverity::Warning, Death::move(text), group.Cell, group.OffGrid);
		}

		// Starts, a character without one starts where Jazz does, Jazz at a multiplayer start (see LevelHandler::SpawnPlayers())
		bool isMultiplayer = (document.Flags & LevelFlags::IsMultiplayerLevel) == LevelFlags::IsMultiplayerLevel;
		if (starts[0] == 0) {
			if (hasMultiplayerStart) {
				add(LevelProblemSeverity::Warning, _x("editor", "Jazz has no start, a multiplayer start is used"));
			} else {
				LevelProblem& problem = add(LevelProblemSeverity::Error, _x("editor", "Jazz has no start, a character without a start can't play the level"));
				problem.Fix = LevelProblemFix::AddStart;
				problem.FixValue = 0x01;
			}
		}
		static const char* const Characters[] = { "Jazz", "Spaz", "Lori" };
		for (std::int32_t i = 1; i < 3; i++) {
			if (starts[i] == 0 && !isMultiplayer) {
				// TRANSLATORS: In the Check Level dialog, {} is the name of a character, Spaz or Lori
				LevelProblem& problem = add(LevelProblemSeverity::Info, _fx("editor", "{} has no start, starts where Jazz does", Characters[i]));
				problem.Fix = LevelProblemFix::AddStart;
				problem.FixValue = (1 << i);
			}
		}
		// One of more starts of the same character is picked at random, see EventMap::GetSpawnPosition()
		for (std::int32_t i = 0; i < 3; i++) {
			if (starts[i] > 1) {
				// TRANSLATORS: In the Check Level dialog, the name of a character (Jazz, Spaz or Lori) and the number of its starts
				add(LevelProblemSeverity::Info, _fx("editor", "{} has {} starts, one of them is picked at random", Characters[i], starts[i]), firstStartCell[i]);
			}
		}

		// Multiplayer, see MpLevelHandler
		if (isMultiplayer && !hasMultiplayerStart) {
			LevelProblem& problem = add(LevelProblemSeverity::Warning, _x("editor", "The level is for multiplayer, but has no multiplayer start"));
			problem.Fix = LevelProblemFix::AddMultiplayerStart;
		} else if (isMultiplayer && multiplayerStarts.size() < 4) {
			add(LevelProblemSeverity::Warning, _fnx("editor", "Only {} multiplayer start, more players start on top of each other",
				"Only {} multiplayer starts, more players start on top of each other", std::int32_t(multiplayerStarts.size()), std::int32_t(multiplayerStarts.size())),
				multiplayerStarts[0].Cell, multiplayerStarts[0].OffGrid);
		}
		if (!isMultiplayer && multiplayerOnlyEvents > 0) {
			add(LevelProblemSeverity::Info, _fnx("editor", "{} event is only in multiplayer, but the level isn't for multiplayer",
				"{} events are only in multiplayer, but the level isn't for multiplayer", multiplayerOnlyEvents, multiplayerOnlyEvents),
				firstMultiplayerOnly.Cell, firstMultiplayerOnly.OffGrid);
		}

		// The flag of a team in Capture the Flag is at its base, the first one found, a team without a base has it at its start
		// TRANSLATORS: Names of the teams in multiplayer, put into the problems in the Check Level dialog
		const StringView TeamNames[] = { _x("editor", "Blue"), _x("editor", "Red"), _x("editor", "Green"), _x("editor", "Yellow") };
		// As the level is saved, the flag follows the events
		if ((document.GetSavedFlags() & LevelFlags::HasCaptureTheFlag) == LevelFlags::HasCaptureTheFlag) {
			for (std::int32_t team = 0; team < 4; team++) {
				std::int32_t bases = std::int32_t(std::count_if(ctfBases.begin(), ctfBases.end(), [team](const IdUse& use) { return use.Id == team; }));
				bool hasStart = HasId(multiplayerStarts, team);
				if (bases == 0 && team < 2) {
					// TRANSLATORS: In the Check Level dialog, {} is the name of the team (Blue, Red, Green or Yellow)
					LevelProblem& problem = add(LevelProblemSeverity::Warning, _fx("editor", "The team {} has no CTF base, its flag is at its start", TeamNames[team]));
					problem.Fix = LevelProblemFix::AddCtfBase;
					problem.FixValue = team;
				} else if (bases > 1) {
					auto it = std::find_if(ctfBases.begin(), ctfBases.end(), [team](const IdUse& use) { return use.Id == team; });
					// TRANSLATORS: In the Check Level dialog, the name of the team (Blue, Red, Green or Yellow) and the number of its bases
					add(LevelProblemSeverity::Warning, _fx("editor", "The team {} has {} CTF bases, only the first one is used", TeamNames[team], bases), it->Cell, it->OffGrid);
				}
				if (bases > 0 && !hasStart) {
					auto it = std::find_if(ctfBases.begin(), ctfBases.end(), [team](const IdUse& use) { return use.Id == team; });
					// TRANSLATORS: In the Check Level dialog, {} is the name of the team (Blue, Red, Green or Yellow)
					LevelProblem& problem = add(LevelProblemSeverity::Warning, _fx("editor", "The team {} has a CTF base, but no multiplayer start of its own", TeamNames[team]),
						it->Cell, it->OffGrid);
					problem.Fix = LevelProblemFix::AddMultiplayerStart;
					problem.FixValue = team;
				}
			}
			for (const IdUse& base : ctfBases) {
				if (base.Id < 0 || base.Id >= 4) {
					// TRANSLATORS: In the Check Level dialog, {} is the number of the team the base is set to
					add(LevelProblemSeverity::Warning, _fx("editor", "A CTF base is of team {}, there are only 4 teams", base.Id), base.Cell, base.OffGrid);
					break;
				}
			}
		}

		// A race counts laps at a warp with "Set Lap", the waypoints show the track and rank the players by them
		if ((document.GetSavedFlags() & LevelFlags::HasLaps) == LevelFlags::HasLaps) {
			if (waypoints.empty()) {
				add(LevelProblemSeverity::Info, _x("editor", "The race has no waypoints, the track is traced from the level for the minimap and the ranking"));
			}
		}
		if (!waypoints.empty()) {
			std::stable_sort(waypoints.begin(), waypoints.end(), [](const Waypoint& a, const Waypoint& b) {
				return (a.Group != b.Group ? a.Group < b.Group : a.Order < b.Order);
			});
			for (std::size_t i = 0; i < waypoints.size(); ) {
				std::size_t j = i;
				while (j < waypoints.size() && waypoints[j].Group == waypoints[i].Group && waypoints[j].Order == waypoints[i].Order) {
					j++;
				}
				// Those of the same order and group are merged into their center, which is far from them if they're apart
				std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
				for (std::size_t k = i; k < j; k++) {
					minX = std::min<std::int32_t>(minX, waypoints[k].Cell.X);
					minY = std::min<std::int32_t>(minY, waypoints[k].Cell.Y);
					maxX = std::max<std::int32_t>(maxX, waypoints[k].Cell.X);
					maxY = std::max<std::int32_t>(maxY, waypoints[k].Cell.Y);
				}
				if (j - i > 1 && (maxX - minX > 4 || maxY - minY > 4)) {
					add(LevelProblemSeverity::Info, _fx("editor", "{} race waypoints of group {} have order {}, they're merged into their center, which is far from them",
						std::int32_t(j - i), waypoints[i].Group, waypoints[i].Order), waypoints[i].Cell, waypoints[i].OffGrid);
				}
				// A gap in the order of a group isn't wrong, but often a waypoint forgotten
				if (j < waypoints.size() && waypoints[j].Group == waypoints[i].Group && waypoints[j].Order > waypoints[i].Order + 1) {
					add(LevelProblemSeverity::Info, _fx("editor", "Race waypoints of group {} go from order {} to {}, those between are missing",
						waypoints[i].Group, waypoints[i].Order, waypoints[j].Order), waypoints[j].Cell, waypoints[j].OffGrid);
				}
				i = j;
			}
		}

		// Uses of an identifier without a counterpart, told once for each identifier with how many there are, at the first
		auto addUnmatched = [&add](const SmallVectorImpl<IdUse>& uses, const SmallVectorImpl<IdUse>& counterparts, LevelProblemSeverity severity,
			Function<String(std::int32_t, std::int32_t)>&& makeText, LevelProblemFix fix = LevelProblemFix::None, LevelProblemFix alternativeFix = LevelProblemFix::None) {
			SmallVector<std::int32_t, 8> reported;
			for (const IdUse& use : uses) {
				if (HasId(counterparts, use.Id) || std::find(reported.begin(), reported.end(), use.Id) != reported.end()) {
					continue;
				}
				reported.push_back(use.Id);
				std::int32_t count = std::int32_t(std::count_if(uses.begin(), uses.end(), [&use](const IdUse& other) { return other.Id == use.Id; }));
				LevelProblem& problem = add(severity, makeText(use.Id, count), use.Cell, use.OffGrid);
				problem.Fix = fix;
				problem.AlternativeFix = alternativeFix;
				problem.FixValue = use.Id;
			}
		};

		// Warps lead to a target of their identifier, a warp of coins needs as many coins, a warp takes more tiles
		addUnmatched(warpOrigins, warpTargets, LevelProblemSeverity::Error, [](std::int32_t id, std::int32_t count) {
			return (count > 1 ? _fx("editor", "Warp {} has no target ({} tiles)", id, count) : _fx("editor", "Warp {} has no target", id));
		}, LevelProblemFix::AddWarpTarget, LevelProblemFix::RemoveWarp);
		std::int32_t coins = LevelStatistics::Compute(document, Formats::EventRecord::Normal).Coins;
		SmallVector<ProblemGroup, 0> coinWarps;
		for (const CheckedEvent& checked : events) {
			EventType type = EventType(checked.Record->Type);
			if (type == EventType::WarpCoinBonus && checked.Record->Params[3] > coins) {
				FindGroup(coinWarps, checked.Record->Params[3], checked, checked.Record->Params[3]);
			}
		}
		for (const ProblemGroup& group : coinWarps) {
			add(LevelProblemSeverity::Warning, _fx("editor", "A coin warp needs {} coins, the level has {}", group.Value, coins), group.Cell, group.OffGrid);
		}
		addUnmatched(warpTargets, warpOrigins, LevelProblemSeverity::Info, [](std::int32_t id, std::int32_t count) {
			return _fx("editor", "No warp leads to the target of warp {}", id);
		});

		// Triggers change the tiles of their identifier
		addUnmatched(triggerSetters, triggerTiles, LevelProblemSeverity::Warning, [](std::int32_t id, std::int32_t count) {
			return (count > 1 ? _fnx("editor", "Trigger {} is set by {} event, but no tile changes with it", "Trigger {} is set by {} events, but no tile changes with it", count, id, count)
				: _fx("editor", "Trigger {} is set, but no tile changes with it", id));
		});
		addUnmatched(triggerTiles, triggerSetters, LevelProblemSeverity::Info, [](std::int32_t id, std::int32_t count) {
			return _fnx("editor", "Trigger {} changes {} tile, but nothing sets it, only a script can", "Trigger {} changes {} tiles, but nothing sets it, only a script can", count, id, count);
		});
		addUnmatched(rocks, rockTriggers, LevelProblemSeverity::Warning, [](std::int32_t id, std::int32_t count) {
			return _fx("editor", "Rolling rock {} has no trigger, it never rolls", id);
		});
		addUnmatched(rockTriggers, rocks, LevelProblemSeverity::Warning, [](std::int32_t id, std::int32_t count) {
			return (count > 1 ? _fnx("editor", "Rolling rock trigger {} has no rock ({} tile)", "Rolling rock trigger {} has no rock ({} tiles)", count, id, count)
				: _fx("editor", "Rolling rock trigger {} has no rock", id));
		});

		// Layers
		std::int32_t skies = 0, firstSky = -1;
		std::int32_t reflections = 0, firstReflection = -1;
		SmallVector<LevelProblem, 4> layerProblems;
		for (std::size_t l = 0; l < document.Layers.size(); l++) {
			const DocumentLayer& layer = document.Layers[l];
			auto renderer = Tiles::LayerRendererType(layer.Record.RendererType);
			if (layer.IsSpriteLayer()) {
				// It's drawn as tiles by any other renderer
			} else if (Tiles::LayerDescription::IsTexturedBackground(renderer)) {
				if (skies == 0) {
					firstSky = std::int32_t(l);
				}
				skies++;
			} else if (renderer == Tiles::LayerRendererType::Reflection) {
				if (reflections == 0) {
					firstReflection = std::int32_t(l);
				}
				reflections++;
			}
			// What makes the layer drawn wrong, the same as its properties show (see CheckLayerDrawing()), named by it; the
			// layers that keep others from being drawn are told once below
			layerProblems.clear();
			CheckLayerDrawing(document, std::int32_t(l), false, layerProblems);
			for (LevelProblem& problem : layerProblems) {
				// TRANSLATORS: A problem of a layer in the Check Level dialog, the name of the layer and the problem
				add(problem.Severity, _fx("editor", "{}: {}", GetLayerTitle(document, std::int32_t(l)), problem.Text), Vector2i(-1, -1), -1, std::int32_t(l));
			}
		}
		if (skies > 1) {
			add(LevelProblemSeverity::Warning, _fx("editor", "{} layers are drawn as a sky, a tunnel, a wave or a cylinder, only one of them is shown", skies), Vector2i(-1, -1), -1, firstSky);
		}
		if (reflections > 1) {
			add(LevelProblemSeverity::Warning, _fx("editor", "{} layers are drawn as a reflection, only the last one is shown", reflections), Vector2i(-1, -1), -1, firstReflection);
		}

		// Errors first, each kind in the order it was found
		std::stable_sort(problems.begin(), problems.end(), [](const LevelProblem& a, const LevelProblem& b) {
			return (a.Severity < b.Severity);
		});
	}

	void CheckReachability(IEditorContext& session, SmallVectorImpl<LevelProblem>& problems)
	{
		SmallVector<std::int32_t, 0> distances;
		if (!session.ComputeReachability(distances)) {
			return;
		}
		const LevelDocument& document = session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		// The tracer reaches the tiles the player stands on or lands on, the air above them is where a jump gets to,
		// so a tile counts if one of those is a little below it
		auto reached = [&distances, gridSize](std::uint32_t cell) {
			std::int32_t x = std::int32_t(cell % std::uint32_t(gridSize.X)), y = std::int32_t(cell / std::uint32_t(gridSize.X));
			for (std::int32_t dy = 0; dy <= 4; dy++) {
				for (std::int32_t dx = -2; dx <= 2; dx++) {
					std::int32_t nx = x + dx, ny = y + dy;
					if (nx >= 0 && ny >= 0 && nx < gridSize.X && ny < gridSize.Y && distances[std::size_t(ny) * std::size_t(gridSize.X) + std::size_t(nx)] >= 0) {
						return true;
					}
				}
			}
			return false;
		};

		std::int32_t exits = 0, reachedExits = 0, lostItems = 0, lostWaypoints = 0;
		Vector2i firstExit(-1, -1), firstLostItem(-1, -1), firstLostWaypoint(-1, -1);
		for (const DocumentEvent& event : document.Events) {
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Record.Type));
			Vector2i cell(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)));
			if (EventType(event.Record.Type) == EventType::RaceCheckpoint) {
				if (!reached(event.Cell)) {
					if (lostWaypoints++ == 0) {
						firstLostWaypoint = cell;
					}
				}
			} else if (EventType(event.Record.Type) == EventType::AreaEndOfLevel) {
				exits++;
				if (reached(event.Cell)) {
					reachedExits++;
				} else if (firstExit.X < 0) {
					firstExit = cell;
				}
			} else if (descriptor != nullptr && descriptor->Category == EventCategory::Collectible && (event.Record.Flags & Formats::EventRecord::MultiplayerOnly) == 0) {
				if (!reached(event.Cell)) {
					lostItems++;
					if (firstLostItem.X < 0) {
						firstLostItem = cell;
					}
				}
			}
		}
		if (exits > 0 && reachedExits == 0) {
			LevelProblem& problem = problems.emplace_back();
			problem.Severity = LevelProblemSeverity::Error;
			problem.Text = _x("editor", "The player can't get to any exit from the start");
			problem.Cell = firstExit;
		}
		if (lostItems > 0) {
			LevelProblem& problem = problems.emplace_back();
			problem.Severity = LevelProblemSeverity::Info;
			// The tracer doesn't know every way, a vine climbed up the side of a wall can be missed
			problem.Text = _fnx("editor", "{} thing to collect may be out of reach, the way to it wasn't found", "{} things to collect may be out of reach, the way to them wasn't found",
				lostItems, lostItems);
			problem.Cell = firstLostItem;
		}
		if (lostWaypoints > 0) {
			LevelProblem& problem = problems.emplace_back();
			problem.Severity = LevelProblemSeverity::Warning;
			problem.Text = _fnx("editor", "{} race waypoint may be out of reach, the track would lead there", "{} race waypoints may be out of reach, the track would lead there",
				lostWaypoints, lostWaypoints);
			problem.Cell = firstLostWaypoint;
		}
	}

	namespace
	{
		// A palette-shifted layer moves only the colors of the tiles in palette colors - and the game draws a whole tile set in
		// true color as soon as one of its tiles is (see Tiles::LayerRendererType::PaletteShift)
		void CheckPaletteShiftTiles(IEditorContext& session, const DocumentLayer& layer, std::int32_t l, SmallVectorImpl<LevelProblem>& problems)
		{
			const LevelDocument& document = session.GetDocument();
			// Whether each source is all in palette colors, looked up once, not for every tile
			bool sourceIndexed[256];
			bool sourceKnown[256] = {};
			std::int32_t shifted = 0, unchanged = 0;
			for (const TileRef& tile : layer.Tiles) {
				if (tile.IsEmpty() || tile.IsAnimated() || tile.Source == TileRef::Raw) {
					continue;
				}
				if (tile.Source != TileRef::DefaultTileSet && std::size_t(tile.Source) - 1 >= document.ExtraTileSets.size()) {
					continue;
				}
				if (!sourceKnown[tile.Source]) {
					sourceKnown[tile.Source] = true;
					const Formats::TileSetData* data = session.GetTileSetData(tile.Source == TileRef::DefaultTileSet
						? StringView(document.Properties.DefaultTileSet) : StringView(document.ExtraTileSets[tile.Source - 1].Name));
					sourceIndexed[tile.Source] = true;
					if (data != nullptr) {
						// An extra tile set is loaded only with the part of its file the level takes
						std::int32_t first = 0, last = data->TileCount;
						if (tile.Source != TileRef::DefaultTileSet) {
							const Formats::ExtraTileSetRecord& record = document.ExtraTileSets[tile.Source - 1];
							first = std::min<std::int32_t>(record.Offset, data->TileCount);
							last = std::min<std::int32_t>(std::int32_t(record.Offset) + std::int32_t(record.Count), data->TileCount);
						}
						for (std::int32_t i = first; i < last; i++) {
							if (data->TrueColor[i] != 0) {
								sourceIndexed[tile.Source] = false;
								break;
							}
						}
					}
				}
				(sourceIndexed[tile.Source] ? shifted : unchanged)++;
			}
			if (shifted == 0 && unchanged > 0) {
				LevelProblem& problem = problems.emplace_back();
				problem.Severity = LevelProblemSeverity::Warning;
				// TRANSLATORS: In the Check Level dialog, {} is the name of the layer
				problem.Text = _fx("editor", "{}: palette shift changes only tiles in palette colors, all tiles of the layer come from tile sets with true color tiles and stay as they are",
					GetLayerTitle(document, l));
				problem.Layer = l;
			}
		}
	}

	void CheckLayerTiles(IEditorContext& session, SmallVectorImpl<LevelProblem>& problems)
	{
		const LevelDocument& document = session.GetDocument();
		for (std::int32_t l = 0; l < std::int32_t(document.Layers.size()); l++) {
			const DocumentLayer& layer = document.Layers[l];
			if (Tiles::LayerRendererType(layer.Record.RendererType) == Tiles::LayerRendererType::PaletteShift) {
				CheckPaletteShiftTiles(session, layer, l, problems);
				continue;
			}
			if (Tiles::LayerRendererType(layer.Record.RendererType) != Tiles::LayerRendererType::BlurBehind) {
				continue;
			}
			// A blur behind layer is driven by the channels of true color tiles, a tile in palette colors has none of them
			std::int32_t indexed = CountPaletteColorTiles(session, l);
			if (indexed > 0) {
				LevelProblem& problem = problems.emplace_back();
				problem.Severity = LevelProblemSeverity::Warning;
				// TRANSLATORS: In the Check Level dialog, the name of the layer and the number of its tiles in palette colors
				problem.Text = _fnx("editor", "{}: blur behind takes its effect from true color tiles, {} tile is in palette colors and does nothing",
					"{}: blur behind takes its effect from true color tiles, {} tiles are in palette colors and do nothing", indexed, GetLayerTitle(document, l), indexed);
				problem.Layer = l;
			}
		}
	}

	std::int32_t CountPaletteColorTiles(IEditorContext& session, std::int32_t layerIndex)
	{
		const LevelDocument& document = session.GetDocument();
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return 0;
		}
		// The tile set of each source is looked up once, not for every tile
		const Formats::TileSetData* sources[256];
		bool sourceKnown[256] = {};
		std::int32_t indexed = 0;
		for (const TileRef& tile : document.Layers[layerIndex].Tiles) {
			if (tile.IsEmpty() || tile.IsAnimated() || tile.Source == TileRef::Raw) {
				continue;
			}
			std::int32_t index = tile.Index;
			if (tile.Source != TileRef::DefaultTileSet) {
				if (std::size_t(tile.Source) - 1 >= document.ExtraTileSets.size()) {
					continue;
				}
				index += document.ExtraTileSets[tile.Source - 1].Offset;
			}
			if (!sourceKnown[tile.Source]) {
				sourceKnown[tile.Source] = true;
				sources[tile.Source] = session.GetTileSetData(tile.Source == TileRef::DefaultTileSet
					? StringView(document.Properties.DefaultTileSet) : StringView(document.ExtraTileSets[tile.Source - 1].Name));
			}
			const Formats::TileSetData* data = sources[tile.Source];
			if (data != nullptr && index < data->TileCount && data->TrueColor[index] == 0) {
				indexed++;
			}
		}
		return indexed;
	}

	void CheckLayerDrawing(const LevelDocument& document, std::int32_t layerIndex, bool aboutOthers, SmallVectorImpl<LevelProblem>& problems)
	{
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}
		auto add = [&problems, layerIndex](LevelProblemSeverity severity, String text) {
			LevelProblem& problem = problems.emplace_back();
			problem.Severity = severity;
			problem.Text = Death::move(text);
			problem.Layer = layerIndex;
		};

		const DocumentLayer& layer = document.Layers[layerIndex];
		const std::uint8_t* color = layer.Record.Color;
		auto renderer = Tiles::LayerRendererType(layer.Record.RendererType);
		if (layer.IsSpriteLayer() && !Tiles::LayerDescription::IsSpriteLayerRenderer(renderer)) {
			// The sprite layer is drawn as ordinary tiles by any other renderer (see Tiles::LayerDescription::FromRecord())
			renderer = Tiles::LayerRendererType::Default;
		}

		// Whether a later layer of the same kind is the one that's drawn, there is only one of each (see Tiles::LayerRendererType)
		auto hasLaterLayer = [&document, layerIndex](bool (*isKind)(Tiles::LayerRendererType)) {
			for (std::size_t l = std::size_t(layerIndex) + 1; l < document.Layers.size(); l++) {
				if (!document.Layers[l].IsSpriteLayer() && isKind(Tiles::LayerRendererType(document.Layers[l].Record.RendererType))) {
					return true;
				}
			}
			return false;
		};

		switch (renderer) {
			case Tiles::LayerRendererType::Default:
				if (color[3] == 0) {
					add(LevelProblemSeverity::Info, layer.IsSpriteLayer()
						? String(_x("editor", "Alpha is 0, it isn't drawn, but its tiles are still solid"))
						: String(_x("editor", "Alpha is 0, it isn't drawn")));
				} else if (std::max({ color[0], color[1], color[2] }) < 24) {
					// Easily left from a sky, a tunnel or a cylinder, whose horizon the color was
					add(LevelProblemSeverity::Warning, _x("editor", "Its color is nearly black, it multiplies the tiles, so they're drawn black"));
				}
				break;
			case Tiles::LayerRendererType::Solid:
			case Tiles::LayerRendererType::PaletteShift:
				if (color[3] == 0) {
					add(LevelProblemSeverity::Info, _x("editor", "Alpha is 0, it isn't drawn"));
				}
				break;
			case Tiles::LayerRendererType::Tinted: {
				// The alpha of the palette entry it's tinted with is the opacity of the layer too (see TileMap::ResolveTintColor())
				const std::uint32_t* palettes = ContentResolver::Get().GetPalettes();
				const std::uint32_t entry = (palettes != nullptr ? palettes[color[0]] : 0xFFFFFFFFu);
				if (color[3] == 0) {
					add(LevelProblemSeverity::Info, _x("editor", "Strength is 0, it isn't drawn"));
				} else if ((entry >> 24) == 0) {
					add(LevelProblemSeverity::Warning, _fx("editor", "Palette entry {} is transparent, so the layer isn't drawn", color[0]));
				}
				break;
			}
			case Tiles::LayerRendererType::SingleColor:
			case Tiles::LayerRendererType::ChromaKey:
				if (color[3] == 0) {
					add(LevelProblemSeverity::Info, _x("editor", "Alpha is 0, it isn't drawn"));
				}
				break;
			case Tiles::LayerRendererType::XRay: {
				// The last frame of an animated tile is its mask, one of a single frame shows nothing (see LayerRendererType::XRay)
				SmallVector<std::uint16_t, 8> reported;
				for (const TileRef& tile : layer.Tiles) {
					if (!tile.IsAnimated() || tile.Index >= document.Animations.size() || document.Animations[tile.Index].Frames.size() >= 2 ||
						std::find(reported.begin(), reported.end(), tile.Index) != reported.end()) {
						continue;
					}
					reported.push_back(tile.Index);
					add(LevelProblemSeverity::Warning, _fx("editor", "X-ray needs animated tiles of 2 frames at least, animated tile {} has fewer, it shows nothing", tile.Index + 1));
				}
				break;
			}
			case Tiles::LayerRendererType::BlurBehind:
				if (color[3] == 0) {
					add(LevelProblemSeverity::Info, _x("editor", "Alpha is 0, it does nothing"));
				}
				break;
			case Tiles::LayerRendererType::Sky:
			case Tiles::LayerRendererType::Tunnel:
			case Tiles::LayerRendererType::Wave:
			case Tiles::LayerRendererType::Cylinder: {
				// The texture is the layer of 8x8 tiles, or an image of its own (see TileMap::DrawLayer())
				if (layer.Texture.empty()) {
					if (layer.GetWidth() != 8 || layer.GetHeight() != 8) {
						add(LevelProblemSeverity::Warning, _x("editor", "Drawn as ordinary tiles - a textured background needs a layer of 8 x 8 tiles or an image of its own"));
					} else {
						bool hasTiles = false;
						for (std::int32_t y = 0; y < 8 && !hasTiles; y++) {
							for (std::int32_t x = 0; x < 8 && !hasTiles; x++) {
								hasTiles = !layer.GetTile(x, y).IsEmpty();
							}
						}
						if (!hasTiles) {
							add(LevelProblemSeverity::Warning, _x("editor", "All its tiles are empty, so it's drawn black"));
						}
					}
				}
				if (renderer == Tiles::LayerRendererType::Wave && color[0] == 0 && color[1] == 0) {
					add(LevelProblemSeverity::Info, _x("editor", "Both wavelengths are 0, it has no waves"));
				}
				if (aboutOthers && hasLaterLayer(&Tiles::LayerDescription::IsTexturedBackground)) {
					add(LevelProblemSeverity::Warning, _x("editor", "A later layer is a textured background too - only the last one is drawn right, this one shows its texture"));
				}
				break;
			}
			case Tiles::LayerRendererType::Reflection: {
				// The horizon is a fraction of the view, or of the layer (see TileMap::DrawReflection()) - at the top of the layer
				// it mirrors what is above the layer
				bool horizonInLayer = ((layer.Record.Textured.Flags & Formats::TexturedLayerParams::InnerLayer) != 0);
				if (layer.Record.Textured.FadeY >= 1.0f) {
					add(LevelProblemSeverity::Warning, _x("editor", "The horizon is at the bottom, the reflection shows nothing"));
				} else if (layer.Record.Textured.FadeY <= 0.0f && !horizonInLayer) {
					add(LevelProblemSeverity::Warning, _x("editor", "The horizon is at the top, there's nothing to mirror, so it's drawn black"));
				}
				// Its ripples are the 8x8 tiles at its top left, unless it has an image of its own, see Tiles::LayerRendererType::Reflection
				bool hasTiles = !layer.Texture.empty();
				for (std::int32_t y = 0; y < std::min<std::int32_t>(layer.GetHeight(), 8) && !hasTiles; y++) {
					for (std::int32_t x = 0; x < std::min<std::int32_t>(layer.GetWidth(), 8) && !hasTiles; x++) {
						hasTiles = !layer.GetTile(x, y).IsEmpty();
					}
				}
				if (!hasTiles && color[2] != 0) {
					add(LevelProblemSeverity::Info, _x("editor", "No tiles at its top left to ripple what it mirrors, it's mirrored as it is"));
				}
				if (aboutOthers && hasLaterLayer([](Tiles::LayerRendererType type) { return type == Tiles::LayerRendererType::Reflection; })) {
					add(LevelProblemSeverity::Warning, _x("editor", "A later layer is a reflection too - only the last one is drawn, this one isn't"));
				}
				break;
			}
			default:
				break;
		}
	}

	void CheckLevelSurroundings(IEditorContext& session, SmallVectorImpl<LevelProblem>& problems)
	{
		const LevelDocument& document = session.GetDocument();
		auto add = [&problems](LevelProblemSeverity severity, String text, Vector2i cell = Vector2i(-1, -1), std::int32_t offGrid = -1) {
			LevelProblem& problem = problems.emplace_back();
			problem.Severity = severity;
			problem.Text = Death::move(text);
			problem.Cell = cell;
			problem.OffGrid = offGrid;
		};

		// Objects inside walls, a destructible or a trigger tile can be meant to hide them, the shown level tells
		Vector2i gridSize = document.GetEventGridSize();
		Tiles::TileMap* tileMap = session.GetTileMap();
		if (tileMap != nullptr && gridSize.X > 0 && gridSize.Y > 0) {
			auto isSolid = [tileMap, gridSize](Vector2i cell) {
				return (cell.X >= 0 && cell.Y >= 0 && cell.X < gridSize.X && cell.Y < gridSize.Y && !tileMap->IsTileEmpty(cell.X, cell.Y) &&
					!tileMap->IsTilePartiallySolid(cell.X, cell.Y) && !tileMap->IsTileDestructible(cell.X, cell.Y) && !tileMap->IsTileTrigger(cell.X, cell.Y) &&
					!tileMap->IsTileOneWay(cell.X, cell.Y));
			};
			SmallVector<ProblemGroup, 0> embedded, embeddedTargets;
			auto check = [&](const Formats::EventRecord& event, Vector2i cell, std::int32_t offGrid) {
				const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
				if (descriptor == nullptr || !isSolid(cell)) {
					return;
				}
				CheckedEvent checked{ &event, cell, offGrid };
				if (EventType(event.Type) == EventType::WarpTarget) {
					FindGroup(embeddedTargets, 0, checked);
				} else if ((descriptor->Traits & EventTraits::Object) == EventTraits::Object) {
					FindGroup(embedded, event.Type, checked);
				}
			};
			for (const DocumentEvent& event : document.Events) {
				check(event.Record, Vector2i(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X))), -1);
			}
			for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
				const Formats::OffGridEventRecord& event = document.OffGridEvents[i];
				check(event.Event, Vector2i(event.X / 32, event.Y / 32), std::int32_t(i));
			}
			for (const ProblemGroup& group : embeddedTargets) {
				add(LevelProblemSeverity::Warning, _fnx("editor", "{} warp target is inside a solid tile, the player gets stuck there", "{} warp targets are inside solid tiles, the player gets stuck there",
					group.Count, group.Count), group.Cell, group.OffGrid);
			}
			for (const ProblemGroup& group : embedded) {
				// TRANSLATORS: In the Check Level dialog, the name of the event and how many of them are inside solid tiles
				add(LevelProblemSeverity::Info, _fnx("editor", "{}: {} is inside a solid tile", "{}: {} are inside solid tiles", group.Count,
					EventPreviews::GetEventName(group.Type), group.Count), group.Cell, group.OffGrid);
			}
		}

		// The levels that follow, as a level of the same episode is written without the episode, see ShowLevelPropertiesDialog()
		SmallVector<UiFileRoot, 4> roots;
		session.GetFileRoots(false, roots);
		StringView episode = session.GetLevelName().partition('/')[0];
		auto levelExists = [&roots, episode](StringView name) {
			String levelName = (name.contains('/') || episode.empty() ? String(name) : String(episode + "/"_s + name));
			for (const UiFileRoot& root : roots) {
				if (fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(root.Path, String(levelName + ".j2l"_s))))) {
					return true;
				}
			}
			return false;
		};
		struct LevelLink { String Formats::LevelProperties::* Member; String Label; };
		static const LevelLink Links[] = {
			// TRANSLATORS: Links of the level to the levels that follow it, put before the name of a level that isn't found in the Check Level dialog
			{ &Formats::LevelProperties::NextLevel, _x("editor", "Next level") },
			{ &Formats::LevelProperties::SecretLevel, _x("editor", "Secret level") },
			{ &Formats::LevelProperties::BonusLevel, _x("editor", "Bonus level") }
		};
		for (const LevelLink& link : Links) {
			StringView name = document.Properties.*link.Member;
			// The end of the episode and the credits have names of their own, as the original names of them
			String lowerName = StringUtils::lowercase(name);
			if (name.empty() || name.hasPrefix(':') || lowerName == "end"_s || lowerName == "endepis"_s || lowerName == "ending"_s || roots.empty()) {
				continue;
			}
			if (!levelExists(name)) {
				// TRANSLATORS: In the Check Level dialog, the kind of the link (Next level, Secret level or Bonus level) and the name of the level it leads to
				add(LevelProblemSeverity::Warning, _fx("editor", "{} \"{}\" isn't found", link.Label, name));
			}
		}

		// Music, as ContentResolver::GetMusic() looks for it
		StringView music = document.Properties.Music;
		if (!music.empty()) {
			auto& resolver = ContentResolver::Get();
			bool found = false;
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				found = fs::IsReadableFile(fs::CombinePath({ userContent, "Music"_s, music }));
			}
			if (!found) {
				found = fs::IsReadableFile(fs::CombinePath({ resolver.GetContentPath(), "Music"_s, music }));
			}
			if (!found && !resolver.GetSourcePath().empty()) {
				found = fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), music)));
			}
			if (!found) {
				add(LevelProblemSeverity::Warning, _fx("editor", "Music \"{}\" isn't found, the level is played without it", music));
			}
		}

		// The scripting API refers to tiles by IDs below a limit, see EditorSession::LoadEventExtensions() for the script
		std::int32_t usedTileIds = std::max<std::int32_t>(document.GetStaticTileCount(), 0) + std::int32_t(document.Animations.size());
		if (usedTileIds > LevelDocument::ScriptTileIdLimit) {
			StringView filePath = session.GetFilePath();
			String scriptPath;
			if (!filePath.empty()) {
				scriptPath = fs::CombinePath(fs::GetDirectoryName(filePath), String(fs::GetFileNameWithoutExtension(filePath) + ".j2as"_s));
			}
			if ((scriptPath.empty() || !fs::IsReadableFile(scriptPath)) && !session.GetLevelName().empty()) {
				scriptPath = ContentResolver::Get().FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(session.GetLevelName()) + ".j2as"_s)));
			}
			if (!scriptPath.empty() && fs::IsReadableFile(scriptPath)) {
				add(LevelProblemSeverity::Warning, _fx("editor", "The level has a script, which can refer to the first {} tile IDs only, the level uses {}",
					LevelDocument::ScriptTileIdLimit, usedTileIds));
			}
		}
	}

	String GetLevelProblemFixLabel(LevelProblemFix fix)
	{
		switch (fix) {
			// TRANSLATORS: Button next to a problem in the Check Level dialog, removes the warp that has no target
			case LevelProblemFix::RemoveWarp: return _x("editor", "Remove");
			case LevelProblemFix::AddWarpTarget:
			case LevelProblemFix::AddStart:
			case LevelProblemFix::AddMultiplayerStart:
			// TRANSLATORS: Button next to a problem in the Check Level dialog, places the missing event (a warp target, a start or a CTF base)
			case LevelProblemFix::AddCtfBase: return _x("editor", "Add");
			default: return {};
		}
	}

	bool FixLevelProblem(IEditorContext& session, LevelProblemFix fix, std::int32_t value)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		Vector2i gridSize = document.GetEventGridSize();
		if (fix == LevelProblemFix::None || spriteLayer < 0 || gridSize.X <= 0 || gridSize.Y <= 0) {
			return false;
		}
		if (!session.IsLayerEditable(spriteLayer)) {
			session.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
			return false;
		}

		if (fix == LevelProblemFix::RemoveWarp) {
			// All tiles of the warp, on the grid and off it
			auto isWarp = [value](const Formats::EventRecord& event) {
				if (EventType(event.Type) == EventType::WarpTarget) {
					return false;
				}
				const EventParamField* field = FindEventField(EventCatalog::Find(EventType(event.Type)), ParamType::WarpId);
				return (field != nullptr && EventCatalog::ReadField(event, *field) == value);
			};
			// TRANSLATORS: Name of an undo step in the history, a warp removed from the Check Level dialog
			auto command = std::make_unique<SetEventsCommand>(document, _x("editor", "Remove Warp"));
			for (const DocumentEvent& event : document.Events) {
				if (isWarp(event.Record)) {
					command->Set(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)), LevelDocument::GetEmptyEvent());
				}
			}
			command->Seal();
			SmallVector<Formats::OffGridEventRecord, 0> offGrid;
			bool offGridChanged = false;
			for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
				if (isWarp(event.Event)) {
					offGridChanged = true;
				} else {
					offGrid.push_back(event);
				}
			}
			if (command->IsEmpty() && !offGridChanged) {
				return false;
			}
			CommandHistory& history = session.GetHistory();
			history.BeginTransaction(_x("editor", "Remove Warp"));
			if (!command->IsEmpty()) {
				session.Execute(Death::move(command));
			}
			if (offGridChanged) {
				session.GetEventSelection().Clear();
				session.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(offGrid), _x("editor", "Remove Warp")));
			}
			history.EndTransaction();
			session.ShowMessage(_fx("editor", "Warp {} removed", value));
			return true;
		}

		// What is placed, with its parameter set
		EventType type;
		ParamType paramType;
		String name;
		switch (fix) {
			// TRANSLATORS: Names of undo steps in the history, an event placed from the Check Level dialog
			case LevelProblemFix::AddWarpTarget: type = EventType::WarpTarget; paramType = ParamType::WarpId; name = _x("editor", "Add Warp Target"); break;
			case LevelProblemFix::AddStart: type = EventType::LevelStart; paramType = ParamType::Flags; name = _x("editor", "Add Start"); break;
			case LevelProblemFix::AddMultiplayerStart: type = EventType::LevelStartMultiplayer; paramType = ParamType::Enum; name = _x("editor", "Add Multiplayer Start"); break;
			case LevelProblemFix::AddCtfBase: type = EventType::CtfBase; paramType = ParamType::Enum; name = _x("editor", "Add CTF Base"); break;
			default: return false;
		}
		Formats::EventRecord record = EventCatalog::MakeDefault(type);
		if (const EventParamField* field = FindEventField(EventCatalog::Find(type), paramType)) {
			EventCatalog::WriteField(record, *field, value);
		}

		// The empty tile nearest to the middle of the view, in rings around it
		Vector2i center(gridSize.X / 2, gridSize.Y / 2);
		if (EditorLevelHandler* editHandler = session.GetEditHandler()) {
			Vector2f world = editHandler->GetCamera().GetCenter();
			center = Vector2i(std::clamp<std::int32_t>(std::int32_t(world.X / 32.0f), 0, gridSize.X - 1),
				std::clamp<std::int32_t>(std::int32_t(world.Y / 32.0f), 0, gridSize.Y - 1));
		}
		Vector2i found(-1, -1);
		constexpr std::int32_t MaxRadius = 64;
		for (std::int32_t radius = 0; radius <= MaxRadius && found.X < 0; radius++) {
			for (std::int32_t y = center.Y - radius; y <= center.Y + radius && found.X < 0; y++) {
				for (std::int32_t x = center.X - radius; x <= center.X + radius; x++) {
					bool onRing = (std::abs(x - center.X) == radius || std::abs(y - center.Y) == radius);
					if (onRing && x >= 0 && y >= 0 && x < gridSize.X && y < gridSize.Y && LevelDocument::IsEmptyEvent(document.GetEvent(x, y))) {
						found = Vector2i(x, y);
						break;
					}
				}
			}
		}
		if (found.X < 0) {
			session.ShowMessage(_x("editor", "There's no empty tile around the middle of the view"));
			return false;
		}
		auto command = std::make_unique<SetEventsCommand>(document, name);
		command->Set(found.X, found.Y, record);
		command->Seal();
		session.Execute(Death::move(command));

		// Selected, so it can be moved where it belongs
		if (!session.IsEditingEvents()) {
			session.GetActions().Execute(ActionIds::ToolEvent);
		}
		session.GetEventSelection().Set(std::uint32_t(found.X + found.Y * gridSize.X));
		// TRANSLATORS: Status message after an event was placed from the Check Level dialog, {} is the name of the event
		session.ShowMessage(_fx("editor", "{} placed in the middle of the view", EventPreviews::GetEventName(std::uint16_t(type))));
		return true;
	}

	namespace
	{
		// Moves the view to what a problem is about and selects it
		void ShowProblem(IEditorContext& session, const LevelProblem& problem)
		{
			LevelDocument& document = session.GetDocument();
			if (problem.Layer >= 0) {
				if (problem.Layer < std::int32_t(document.Layers.size())) {
					session.SetCurrentLayer(problem.Layer);
					// TRANSLATORS: Status message after a problem of a layer was shown from the Check Level dialog, {} is the name of the layer
					session.ShowMessage(_fx("editor", "Editing {}", GetLayerTitle(document, problem.Layer)));
				}
				return;
			}

			Vector2f target(problem.Cell.X * 32.0f + 16.0f, problem.Cell.Y * 32.0f + 16.0f);
			bool offGrid = (problem.OffGrid >= 0 && problem.OffGrid < std::int32_t(document.OffGridEvents.size()));
			if (offGrid) {
				target = Vector2f(float(document.OffGridEvents[problem.OffGrid].X), float(document.OffGridEvents[problem.OffGrid].Y));
			}
			if (EditorLevelHandler* editHandler = session.GetEditHandler()) {
				EditorCamera& camera = editHandler->GetCamera();
				camera.FlyTo(target, camera.GetScale());
			}

			// With the event tool, the selection is seen only with it
			Vector2i gridSize = document.GetEventGridSize();
			bool onGrid = (!offGrid && problem.Cell.X >= 0 && problem.Cell.Y >= 0 && problem.Cell.X < gridSize.X && problem.Cell.Y < gridSize.Y &&
				!LevelDocument::IsEmptyEvent(document.GetEvent(problem.Cell.X, problem.Cell.Y)));
			if ((offGrid || onGrid) && !session.IsEditingEvents()) {
				session.GetActions().Execute(ActionIds::ToolEvent);
			}
			if (offGrid) {
				session.GetEventSelection().SetOffGrid(problem.OffGrid);
			} else if (onGrid) {
				session.GetEventSelection().Set(std::uint32_t(problem.Cell.X + problem.Cell.Y * gridSize.X));
			}
		}

		struct CheckState
		{
			// Where the player can get takes loading the level again, it's found only when asked for, the rest after each change
			SmallVector<LevelProblem, 0> Reachability;
			bool ReachabilityStale = false;
			bool Dirty = false;
			float Wait = 0.0f;
			// Problems found of each severity, shown on the filters
			std::int32_t Counts[3] = {};
			UiButton* FilterButtons[3] = {};
			std::shared_ptr<Function<void(const DocumentChanges&)>> Observer;
		};
	}

	void ShowCheckLevelDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		// Asked for again, the level is checked again
		if (surface == nullptr || ReuseSingleDialog(session, "checkLevel")) {
			return;
		}

		IEditorContext* s = &session;
		// TRANSLATORS: Title of the dialog that lists the problems found in the level
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Check Level"));
		UiDialog* dialogPtr = dialog.get();
		// As large as the screen allows for long problems and many of them, the list scrolls, the texts don't make it
		// wider, they're cut off with the whole of them in their tooltips
		dialog->MaxScreenHeightFraction = 0.8f;
		dialog->MinScreenWidthFraction = 0.7f;
		auto* content = dialog->GetContent();
		auto* summary = content->Add<UiLabel>(StringView(), UiTheme::TextDim);
		summary->Width = 1.0f;
		// Which problems are shown, by how bad they are, remembered
		auto* filters = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		// What's left between the filters and the buttons, which stay
		auto* scroll = content->Add<UiScrollView>();
		scroll->Flex = 1.0f;

		auto state = std::make_shared<CheckState>();
		auto show = std::make_shared<Function<void()>>();
		*show = [s, state, summary, scroll]() {
			SmallVector<LevelProblem, 0> problems;
			CheckLevel(s->GetDocument(), problems);
			CheckLayerTiles(*s, problems);
			CheckLevelSurroundings(*s, problems);
			for (const LevelProblem& problem : state->Reachability) {
				problems.push_back(problem);
			}
			// Descriptions of events from event packs or the script of the level that couldn't be used
			for (const EventExtensionError& error : EventCatalog::GetExtensionErrors()) {
				LevelProblem& problem = problems.emplace_back();
				problem.Severity = LevelProblemSeverity::Warning;
				// TRANSLATORS: In the Check Level dialog, the name of an event pack or a script file and an error found in it
				problem.Text = _fx("editor", "{}: {}", fs::GetFileName(error.Source), error.Message);
			}
			std::stable_sort(problems.begin(), problems.end(), [](const LevelProblem& a, const LevelProblem& b) {
				return (a.Severity < b.Severity);
			});
			for (std::int32_t i = 0; i < 3; i++) {
				state->Counts[i] = 0;
			}
			for (const LevelProblem& problem : problems) {
				state->Counts[std::int32_t(problem.Severity)]++;
			}
			// TRANSLATORS: Toggles in the Check Level dialog that show or hide the problems of each kind, plural nouns
			const StringView FilterLabels[] = { _x("editor", "Errors"), _x("editor", "Warnings"), _x("editor", "Information") };
			for (std::int32_t i = 0; i < 3; i++) {
				if (state->FilterButtons[i] != nullptr) {
					// TRANSLATORS: Toggle in the Check Level dialog, the kind of problems (Errors, Warnings or Information) and how many were found
					state->FilterButtons[i]->Text = _fx("editor", "{} ({})", FilterLabels[i], state->Counts[i]);
				}
			}
			String text = (problems.empty() ? String(_x("editor", "No problems found")) : _fnx("editor", "{} problem found", "{} problems found", std::int32_t(problems.size()), std::int32_t(problems.size())));
			if (state->ReachabilityStale) {
				// TRANSLATORS: In the Check Level dialog after the number of problems found, the reachable places weren't checked since the level changed
				text += " - "_s + _x("editor", "where the player can get is from before the last change, check again");
			}
			summary->Tooltip = text;
			summary->Text = Death::move(text);

			std::int32_t filter = s->GetSettings().CheckLevelFilter;
			auto* rows = scroll->SetContent<UiStack>(UiOrientation::Vertical, 2.0f);
			std::int32_t shown = 0;
			for (const LevelProblem& problem : problems) {
				if ((filter & (1 << std::int32_t(problem.Severity))) == 0) {
					continue;
				}
				shown++;
				auto* row = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				const Colorf& color = (problem.Severity == LevelProblemSeverity::Error ? UiTheme::TextDanger
					: (problem.Severity == LevelProblemSeverity::Warning ? UiTheme::TextAccent : UiTheme::TextDim));
				auto* label = row->Add<UiLabel>(problem.Text, color);
				label->Width = 1.0f;
				label->Flex = 1.0f;
				label->Tooltip = problem.Text;
				if (problem.Cell.X >= 0 || problem.OffGrid >= 0 || problem.Layer >= 0) {
					// TRANSLATORS: Button next to a problem in the Check Level dialog, moves the view to it
					auto* button = row->Add<UiButton>(_x("editor", "Show"), EditorIcon::None, [s, problem]() {
						ShowProblem(*s, problem);
					});
					button->Style = UiButtonStyle::Flat;
				}
				// Fixes that are obvious, the level is checked again right after
				std::weak_ptr<CheckState> weakState = state;
				for (LevelProblemFix fix : { problem.Fix, problem.AlternativeFix }) {
					if (fix == LevelProblemFix::None) {
						continue;
					}
					std::int32_t value = problem.FixValue;
					auto* button = row->Add<UiButton>(GetLevelProblemFixLabel(fix), EditorIcon::None, [s, fix, value, weakState]() {
						if (FixLevelProblem(*s, fix, value)) {
							if (std::shared_ptr<CheckState> state = weakState.lock()) {
								state->Dirty = true;
								state->Wait = 0.0f;
							}
						}
					});
					button->Style = UiButtonStyle::Flat;
					button->Tooltip = (fix == LevelProblemFix::RemoveWarp ? _x("editor", "Removes the warp, all of its tiles") : _x("editor", "Places it at the empty tile nearest to the middle of the view"));
				}
			}
			if (shown == 0 && !problems.empty()) {
				rows->Add<UiLabel>(_x("editor", "The problems found are filtered out"), UiTheme::TextDisabled);
			}
		};
		// Filters by how bad a problem is
		for (std::int32_t i = 0; i < 3; i++) {
			auto* button = filters->Add<UiButton>(StringView(), EditorIcon::None, [s, i, show]() {
				EditorSettings& settings = s->GetSettings();
				settings.CheckLevelFilter ^= (1 << i);
				settings.Save();
				(*show)();
			});
			button->Style = UiButtonStyle::Flat;
			button->IsCheckedFunc = [s, i]() { return (s->GetSettings().CheckLevelFilter & (1 << i)) != 0; };
			button->Tooltip = _x("editor", "Shows or hides these problems");
			state->FilterButtons[i] = button;
		}
		// Where the player can get is found with the events as the game has them, so the level is loaded again first
		auto checkAll = [s, state, show]() {
			s->ReloadRuntime();
			state->Reachability.clear();
			CheckReachability(*s, state->Reachability);
			state->ReachabilityStale = false;
			state->Dirty = false;
			(*show)();
		};
		checkAll();

		// The rest follows the changes of the level, once they stop for a moment
		std::weak_ptr<CheckState> weakState = state;
		state->Observer = std::make_shared<Function<void(const DocumentChanges&)>>([weakState](const DocumentChanges& changes) {
			if (std::shared_ptr<CheckState> state = weakState.lock()) {
				constexpr float WaitFrames = 30.0f;
				state->Dirty = true;
				state->Wait = WaitFrames;
				if (changes.HasAny(DocumentChangeKind::Tiles | DocumentChangeKind::Events | DocumentChangeKind::LayerStructure |
					DocumentChangeKind::LevelProperties | DocumentChangeKind::TileOverrides | DocumentChangeKind::TileSets)) {
					state->ReachabilityStale = true;
				}
			}
		});
		session.AddChangeObserver(state->Observer);
		content->Add<UiUpdateWatcher>([state, show](float timeMult) {
			if (state->Dirty) {
				state->Wait -= timeMult;
				if (state->Wait <= 0.0f) {
					state->Dirty = false;
					(*show)();
				}
			}
		});

		// TRANSLATORS: Button in the Check Level dialog
		dialog->AddButton(_x("editor", "Check Again"), checkAll);
		// Closed by its cross or Escape, checking again isn't what they do
		dialog->OnCancel = [dialogPtr]() { dialogPtr->Close(); };

		// The level is seen behind it, a problem shown in the view stays to be fixed
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		ShowSingleDialog(session, "checkLevel", Death::move(dialog), Death::move(options), checkAll);
	}
}

#endif
