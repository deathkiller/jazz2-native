#include "LevelCheck.h"

#if defined(WITH_EDITOR)

#include "LevelStatistics.h"
#include "../EditorSession.h"
#include "../Events/EventCatalog.h"
#include "../Events/EventPreviews.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../Formats/TileSetFile.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <memory>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// An event of the grid or off it, at the tile it's in
		struct CheckedEvent
		{
			const Formats::EventRecord* Record;
			Vector2i Cell;
		};

		const EventParamField* FindField(const EventDescriptor* descriptor, ParamType type)
		{
			if (descriptor != nullptr) {
				for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
					if (descriptor->Fields[i].Type == type) {
						return &descriptor->Fields[i];
					}
				}
			}
			return nullptr;
		}

		// Identifiers an event uses of a kind, as a warp, a trigger or a rolling rock
		struct IdUse
		{
			std::int32_t Id;
			Vector2i Cell;
		};

		bool HasId(const SmallVectorImpl<IdUse>& uses, std::int32_t id)
		{
			return std::any_of(uses.begin(), uses.end(), [id](const IdUse& use) { return use.Id == id; });
		}
	}

	void CheckLevel(const LevelDocument& document, SmallVectorImpl<LevelProblem>& problems)
	{
		auto add = [&problems](LevelProblemSeverity severity, String text, Vector2i cell = Vector2i(-1, -1)) {
			LevelProblem& problem = problems.emplace_back();
			problem.Severity = severity;
			problem.Text = std::move(text);
			problem.Cell = cell;
		};

		Vector2i gridSize = document.GetEventGridSize();
		SmallVector<CheckedEvent, 0> events;
		events.reserve(document.Events.size() + document.OffGridEvents.size());
		for (const DocumentEvent& event : document.Events) {
			events.push_back(CheckedEvent{ &event.Record, Vector2i(std::int32_t(event.Cell % std::uint32_t(std::max(gridSize.X, 1))),
				std::int32_t(event.Cell / std::uint32_t(std::max(gridSize.X, 1)))) });
		}
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			events.push_back(CheckedEvent{ &event.Event, Vector2i(std::int32_t(event.X / 32), std::int32_t(event.Y / 32)) });
		}

		SmallVector<IdUse, 0> warpOrigins, warpTargets, triggerSetters, triggerTiles, rocks, rockTriggers;
		bool hasMultiplayerStart = false;
		std::int32_t starts[3] = {};

		for (const CheckedEvent& checked : events) {
			const Formats::EventRecord& event = *checked.Record;
			EventType type = EventType(event.Type);
			const EventDescriptor* descriptor = EventCatalog::Find(type);
			if (descriptor == nullptr) {
				add(LevelProblemSeverity::Info, _f("Unknown event {} - the game doesn't know it", event.Type), checked.Cell);
				continue;
			}
			String name = EventPreviews::GetEventName(event.Type);

			// Parameters out of the range the game takes
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				const EventParamField& field = descriptor->Fields[i];
				if ((field.Type != ParamType::UInt && field.Type != ParamType::Int) || field.Max <= field.Min) {
					continue;
				}
				std::int32_t value = EventCatalog::ReadField(event, field);
				if (value < field.Min || value > field.Max) {
					add(LevelProblemSeverity::Warning, _f("{}: {} is {}, it can be from {} to {}", name, _(field.Name), value, field.Min, field.Max), checked.Cell);
				}
			}

			// Texts it shows
			if (const EventParamField* field = FindField(descriptor, ParamType::TextId)) {
				std::int32_t index = EventCatalog::ReadField(event, *field);
				if (index >= std::int32_t(document.Texts.size()) || document.Texts[index].empty()) {
					add(LevelProblemSeverity::Warning, _f("{}: text {} is empty", name, index), checked.Cell);
				}
			}

			switch (type) {
				case EventType::LevelStart:
					for (std::int32_t i = 0; i < 3; i++) {
						if ((event.Params[0] & (1 << i)) != 0) {
							starts[i]++;
						}
					}
					break;
				case EventType::LevelStartMultiplayer:
					hasMultiplayerStart = true;
					break;
				case EventType::WarpTarget:
					warpTargets.push_back(IdUse{ event.Params[0], checked.Cell });
					break;
				case EventType::TriggerArea:
					triggerTiles.push_back(IdUse{ event.Params[0], checked.Cell });
					break;
				case EventType::TriggerCrate:
				case EventType::TriggerZone:
					triggerSetters.push_back(IdUse{ event.Params[0], checked.Cell });
					break;
				case EventType::RollingRock:
					rocks.push_back(IdUse{ event.Params[0], checked.Cell });
					break;
				case EventType::RollingRockTrigger:
					rockTriggers.push_back(IdUse{ event.Params[0], checked.Cell });
					break;
				default:
					if (const EventParamField* field = FindField(descriptor, ParamType::WarpId)) {
						warpOrigins.push_back(IdUse{ EventCatalog::ReadField(event, *field), checked.Cell });
					}
					break;
			}
		}

		// Starts, a character without one starts where Jazz does, Jazz at a multiplayer start (see LevelHandler::SpawnPlayers())
		if (starts[0] == 0) {
			if (hasMultiplayerStart) {
				add(LevelProblemSeverity::Warning, _("Jazz has no start, a multiplayer start is used"));
			} else {
				add(LevelProblemSeverity::Error, _("Jazz has no start, a character without a start can't play the level"));
			}
		}
		static const char* const Characters[] = { "Spaz", "Lori" };
		for (std::int32_t i = 1; i < 3; i++) {
			if (starts[i] == 0) {
				add(LevelProblemSeverity::Info, _f("{} has no start, starts where Jazz does", Characters[i - 1]));
			}
		}
		if ((document.Flags & LevelFlags::IsMultiplayerLevel) == LevelFlags::IsMultiplayerLevel && !hasMultiplayerStart) {
			add(LevelProblemSeverity::Warning, _("The level is for multiplayer, but has no multiplayer start"));
		}

		// Warps lead to a target of their identifier, a warp of coins needs as many coins
		std::int32_t coins = LevelStatistics::Compute(document, Formats::EventRecord::Normal).Coins;
		// A warp takes more tiles, it's told once, at its first tile
		SmallVector<std::int32_t, 8> reportedWarps;
		for (const IdUse& origin : warpOrigins) {
			if (HasId(warpTargets, origin.Id) || std::find(reportedWarps.begin(), reportedWarps.end(), origin.Id) != reportedWarps.end()) {
				continue;
			}
			reportedWarps.push_back(origin.Id);
			std::int32_t tiles = std::int32_t(std::count_if(warpOrigins.begin(), warpOrigins.end(), [&origin](const IdUse& use) { return use.Id == origin.Id; }));
			add(LevelProblemSeverity::Error, tiles > 1
				? _f("Warp {} has no target ({} tiles)", origin.Id, tiles)
				: _f("Warp {} has no target", origin.Id), origin.Cell);
		}
		for (const CheckedEvent& checked : events) {
			EventType type = EventType(checked.Record->Type);
			if (type == EventType::WarpCoinBonus && checked.Record->Params[3] > coins) {
				add(LevelProblemSeverity::Warning, _f("A coin warp needs {} coins, the level has {}", checked.Record->Params[3], coins), checked.Cell);
			}
		}
		for (const IdUse& target : warpTargets) {
			if (!HasId(warpOrigins, target.Id)) {
				add(LevelProblemSeverity::Info, _f("No warp leads to the target of warp {}", target.Id), target.Cell);
			}
		}

		// Triggers change the tiles of their identifier
		for (const IdUse& setter : triggerSetters) {
			if (!HasId(triggerTiles, setter.Id)) {
				add(LevelProblemSeverity::Warning, _f("Trigger {} is set, but no tile changes with it", setter.Id), setter.Cell);
			}
		}
		for (const IdUse& tile : triggerTiles) {
			if (!HasId(triggerSetters, tile.Id)) {
				add(LevelProblemSeverity::Info, _f("Trigger {} changes tiles, but nothing sets it, only a script can", tile.Id), tile.Cell);
			}
		}
		for (const IdUse& rock : rocks) {
			if (!HasId(rockTriggers, rock.Id)) {
				add(LevelProblemSeverity::Warning, _f("Rolling rock {} has no trigger, it never rolls", rock.Id), rock.Cell);
			}
		}
		for (const IdUse& trigger : rockTriggers) {
			if (!HasId(rocks, trigger.Id)) {
				add(LevelProblemSeverity::Warning, _f("Rolling rock trigger {} has no rock", trigger.Id), trigger.Cell);
			}
		}

		// Layers
		std::int32_t skies = 0;
		for (std::size_t l = 0; l < document.Layers.size(); l++) {
			const DocumentLayer& layer = document.Layers[l];
			auto renderer = Tiles::LayerRendererType(layer.Record.RendererType);
			if (renderer == Tiles::LayerRendererType::Sky || renderer == Tiles::LayerRendererType::Circle) {
				skies++;
			} else if (renderer == Tiles::LayerRendererType::XRay) {
				// The last frame of an animated tile is its mask, one of a single frame shows nothing (see LayerRendererType::XRay)
				SmallVector<std::uint16_t, 8> reported;
				for (std::size_t i = 0; i < layer.Tiles.size(); i++) {
					const TileRef& tile = layer.Tiles[i];
					if (!tile.IsAnimated() || tile.Index >= document.Animations.size() || document.Animations[tile.Index].Frames.size() >= 2 ||
						std::find(reported.begin(), reported.end(), tile.Index) != reported.end()) {
						continue;
					}
					reported.push_back(tile.Index);
					String layerName = (layer.Name.empty() ? String(_f("Layer {}", l + 1)) : layer.Name);
					add(LevelProblemSeverity::Warning, _f("{}: x-ray needs animated tiles of 2 frames at least, animated tile {} has fewer, it shows nothing",
						layerName, tile.Index + 1));
				}
			}
		}
		if (skies > 1) {
			add(LevelProblemSeverity::Warning, _f("{} layers are drawn as a sky, only one of them is shown", skies));
		}

		// Errors first, each kind in the order it was found
		std::stable_sort(problems.begin(), problems.end(), [](const LevelProblem& a, const LevelProblem& b) {
			return (a.Severity < b.Severity);
		});
	}

	void CheckReachability(EditorSession& session, SmallVectorImpl<LevelProblem>& problems)
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

		std::int32_t exits = 0, reachedExits = 0, lostItems = 0;
		Vector2i firstExit(-1, -1), firstLostItem(-1, -1);
		for (const DocumentEvent& event : document.Events) {
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Record.Type));
			Vector2i cell(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)));
			if (EventType(event.Record.Type) == EventType::AreaEndOfLevel) {
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
			problem.Text = _("The player can't get to any exit from the start");
			problem.Cell = firstExit;
		}
		if (lostItems > 0) {
			LevelProblem& problem = problems.emplace_back();
			problem.Severity = LevelProblemSeverity::Info;
			// The tracer doesn't know every way, a vine climbed up the side of a wall can be missed
			problem.Text = _fn("{} thing to collect may be out of reach, the way to it wasn't found", "{} things to collect may be out of reach, the way to them wasn't found",
				lostItems, lostItems);
			problem.Cell = firstLostItem;
		}
	}

	void CheckLayerTiles(EditorSession& session, SmallVectorImpl<LevelProblem>& problems)
	{
		const LevelDocument& document = session.GetDocument();
		for (std::int32_t l = 0; l < std::int32_t(document.Layers.size()); l++) {
			const DocumentLayer& layer = document.Layers[l];
			if (Tiles::LayerRendererType(layer.Record.RendererType) != Tiles::LayerRendererType::BlurBehind) {
				continue;
			}
			// A blur behind layer is driven by the channels of true color tiles, a tile in palette colors has none of them
			std::int32_t indexed = 0;
			for (const TileRef& tile : layer.Tiles) {
				if (tile.IsEmpty() || tile.IsAnimated() || tile.Source == TileRef::Raw) {
					continue;
				}
				StringView name;
				std::int32_t index = tile.Index;
				if (tile.Source == TileRef::DefaultTileSet) {
					name = document.Properties.DefaultTileSet;
				} else if (std::size_t(tile.Source) - 1 < document.ExtraTileSets.size()) {
					const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[tile.Source - 1];
					name = extra.Name;
					index += extra.Offset;
				} else {
					continue;
				}
				const Formats::TileSetData* data = session.GetTileSetData(name);
				if (data != nullptr && index < data->TileCount && data->TrueColor[index] == 0) {
					indexed++;
				}
			}
			if (indexed > 0) {
				LevelProblem& problem = problems.emplace_back();
				problem.Severity = LevelProblemSeverity::Warning;
				String layerName = (layer.Name.empty() ? String(_f("Layer {}", l + 1)) : layer.Name);
				problem.Text = _fn("{}: blur behind takes its effect from true color tiles, {} tile is in palette colors and does nothing",
					"{}: blur behind takes its effect from true color tiles, {} tiles are in palette colors and do nothing", indexed, layerName, indexed);
			}
		}
	}

	void ShowCheckLevelDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("Check Level"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();
		auto* summary = content->Add<UiLabel>(StringView(), UiTheme::TextDim);
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 340.0f;
		scroll->Height = 220.0f;

		auto refresh = std::make_shared<Function<void()>>();
		*refresh = [s, summary, scroll]() {
			SmallVector<LevelProblem, 0> problems;
			CheckLevel(s->GetDocument(), problems);
			CheckReachability(*s, problems);
			CheckLayerTiles(*s, problems);
			std::stable_sort(problems.begin(), problems.end(), [](const LevelProblem& a, const LevelProblem& b) {
				return (a.Severity < b.Severity);
			});
			summary->Text = (problems.empty() ? String(_("No problems found")) : _fn("{} problem found", "{} problems found", std::int32_t(problems.size()), std::int32_t(problems.size())));

			auto* rows = scroll->SetContent<UiStack>(UiOrientation::Vertical, 2.0f);
			for (const LevelProblem& problem : problems) {
				auto* row = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				const Colorf& color = (problem.Severity == LevelProblemSeverity::Error ? UiTheme::TextDanger
					: (problem.Severity == LevelProblemSeverity::Warning ? UiTheme::TextAccent : UiTheme::TextDim));
				auto* text = row->Add<UiLabel>(problem.Text, color);
				text->Flex = 1.0f;
				if (problem.Cell.X >= 0) {
					Vector2i cell = problem.Cell;
					auto* show = row->Add<UiButton>(_("Show"), EditorIcon::None, [s, cell]() {
						// The event in the middle of the view, selected
						if (EditorLevelHandler* editHandler = s->GetEditHandler()) {
							EditorCamera& camera = editHandler->GetCamera();
							camera.FlyTo(Vector2f(cell.X * 32.0f + 16.0f, cell.Y * 32.0f + 16.0f), camera.GetScale());
						}
						Vector2i gridSize = s->GetDocument().GetEventGridSize();
						if (cell.X < gridSize.X && cell.Y < gridSize.Y && !LevelDocument::IsEmptyEvent(s->GetDocument().GetEvent(cell.X, cell.Y))) {
							// With the event tool, the selection is seen only with it
							if (!s->IsEditingEvents()) {
								s->GetActions().Execute("tool.event"_s);
							}
							s->GetEventSelection().Set(std::uint32_t(cell.X + cell.Y * gridSize.X));
						}
					});
					show->Style = UiButtonStyle::Flat;
				}
			}
		};
		(*refresh)();

		dialog->AddButton(_("Check Again"), [refresh]() { (*refresh)(); });
		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		// The level is seen behind it, a problem shown in the view stays to be fixed
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
