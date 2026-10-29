#include "EditorDebugConsole.h"

#if defined(WITH_EDITOR) && defined(DEATH_DEBUG)

#include "DocumentFiles.h"
#include "EditorInputRouter.h"
#include "PlayOverlays.h"
#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../Events/EventCatalog.h"
#include "../Imaging/LevelThumbnail.h"
#include "../Imaging/PngCodec.h"
#include "../Imaging/TileImages.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../Runtime/PlayTestController.h"
#include "../Runtime/TileLod.h"
#include "../Shell/FxTileGenerator.h"
#include "../Shell/LevelCheck.h"
#include "../Shell/PaletteDialog.h"
#include "../Shell/TileSetApplyDialog.h"
#include "../Shell/TileSetImportDialog.h"
#include "../Shell/TileSetsDialog.h"
#include "../Smart/TilePrefabs.h"
#include "../Tools/EditorTool.h"
#include "../UI/UiKeyboard.h"
#include "../UI/UiRadialMenu.h"
#include "../UI/UiSurface.h"
#include "../UI/UiTheme.h"
#include "../../ContentResolver.h"
#include "../../LevelHandler.h"
#include "../../PreferencesCache.h"
#include "../../Actors/Player.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/FrameStatistics.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// What a debug command did, a command that only tells whether it worked isn't logged
		enum class DebugResult
		{
			Failed,
			Done,
			Quiet
		};

		DebugResult QuietResult(bool succeeded)
		{
			return (succeeded ? DebugResult::Quiet : DebugResult::Failed);
		}
	}

	// The words of a debug command, the first one is the command itself, so an index is the same as on the line
	struct EditorDebugConsole::Args
	{
		StringView Line;
		StringView Verb;
		SmallVector<StringView, 8> Values;

		std::size_t size() const {
			return Values.size();
		}

		StringView operator[](std::size_t index) const {
			return Values[index];
		}

		// The word at an index as a number, the default if there are fewer words
		float Float(std::size_t index, float defaultValue) const {
			if (index >= Values.size()) {
				return defaultValue;
			}
			String value = Values[index];
			return float(std::strtod(value.data(), nullptr));
		}
	};

	// A debug command, it's run only with at least so many words after it, otherwise its usage is logged
	struct EditorDebugConsole::Command
	{
		StringView Verb;
		std::int32_t MinArgs;
		StringView Usage;
		Function<DebugResult(const Args&)> Handler;
	};

	EditorDebugConsole::EditorDebugConsole(EditorSession& session, DocumentFiles& files, EditorInputRouter& input, TileLod& tileLod)
		: _session(session), _files(files), _input(input), _tileLod(tileLod)
	{
	}

	EditorDebugConsole::~EditorDebugConsole()
	{
	}

	void EditorDebugConsole::ReadStartupCommands()
	{
		const AppConfiguration& config = theApplication().GetAppConfiguration();
		for (std::int32_t i = 0; i + 1 < config.argc(); i++) {
			if (config.argv(i) == "/console"_s) {
				_startupCommands.emplace_back(config.argv(i + 1));
				i++;
			}
		}
	}

	void EditorDebugConsole::Update()
	{
		// Run from the second frame, once the view was initialized, /wait <frames> delays the rest
		if DEATH_UNLIKELY(!_startupCommands.empty() && ++_frame >= 2) {
			if (_waitFrames > 0) {
				_waitFrames--;
			} else {
				while (!_startupCommands.empty()) {
					String command = Death::move(_startupCommands.front());
					_startupCommands.erase(_startupCommands.begin());
					if (command.hasPrefix("/wait "_s)) {
						String frames = command.exceptPrefix(6);
						_waitFrames = std::int32_t(std::strtol(frames.data(), nullptr, 10));
						break;
					}
					if (command == "/quit"_s) {
						theApplication().Quit();
						break;
					}
					if (!Execute(command)) {
						LOGW("Unknown editor command: {}", command);
					}
				}
			}
		}
		UpdatePerf();
	}

	bool EditorDebugConsole::Execute(StringView line)
	{
		if (_session.GetEditHandler() == nullptr) {
			return false;
		}

		Args args;
		args.Line = line;
		for (StringView arg : line.trimmed().split(' ')) {
			if (!arg.empty()) {
				args.Values.push_back(arg);
			}
		}
		if (args.Values.empty()) {
			return false;
		}
		args.Verb = args.Values[0];

		SmallVector<Command, 0> commands;
		AddViewCommands(commands);
		AddEditCommands(commands);
		AddEventCommands(commands);
		AddFileCommands(commands);
		AddPlayTestCommands(commands);
		AddInputCommands(commands);
		AddTileSetCommands(commands);
		AddDiagnosticCommands(commands);

		for (Command& command : commands) {
			if (command.Verb != args.Verb) {
				continue;
			}
			if (args.size() - 1 < std::size_t(command.MinArgs)) {
				LOGW("Editor command needs more: {}", command.Usage);
				return false;
			}
			DebugResult result = command.Handler(args);
			if (result == DebugResult::Done) {
				LOGI("Editor command: {}", line);
			}
			return (result != DebugResult::Failed);
		}

		String verbs;
		for (const Command& command : commands) {
			if (!verbs.empty()) {
				verbs += " "_s;
			}
			verbs += command.Verb;
		}
		// The startup commands also have /wait and /quit, see Update()
		LOGW("Editor commands are: {} /wait /quit", verbs);
		return false;
	}

	void EditorDebugConsole::AddViewCommands(SmallVectorImpl<Command>& commands)
	{
		commands.push_back({ "/zoom"_s, 0, "/zoom [scale]"_s, [this](const Args& args) {
			EditorCamera& camera = _session.GetEditHandler()->GetCamera();
			Vector2f viewCenter = Vector2f(float(camera.GetViewSize().X), float(camera.GetViewSize().Y)) * 0.5f;
			camera.ZoomTo(viewCenter, args.Float(1, camera.GetGameplayScale()));
			return DebugResult::Done;
		} });
		commands.push_back({ "/jump"_s, 0, "/jump <x> <y> [scale]"_s, [this](const Args& args) {
			EditorCamera& camera = _session.GetEditHandler()->GetCamera();
			camera.JumpTo(Vector2f(args.Float(1, 0.0f), args.Float(2, 0.0f)), args.Float(3, camera.GetScale()));
			return DebugResult::Done;
		} });
		commands.push_back({ "/fly"_s, 0, "/fly <x> <y> [scale]"_s, [this](const Args& args) {
			EditorCamera& camera = _session.GetEditHandler()->GetCamera();
			camera.FlyTo(Vector2f(args.Float(1, 0.0f), args.Float(2, 0.0f)), args.Float(3, camera.GetScale()));
			return DebugResult::Done;
		} });
		commands.push_back({ "/fit"_s, 0, "/fit"_s, [this](const Args&) {
			EditorLevelHandler* editHandler = _session.GetEditHandler();
			Vector2i levelBounds = editHandler->TileMap()->GetLevelBounds();
			editHandler->GetCamera().Fit(Rectf(0.0f, 0.0f, float(levelBounds.X), float(levelBounds.Y)), false);
			return DebugResult::Done;
		} });
		commands.push_back({ "/view"_s, 3, "/view <x|c> <y|c> <percent>"_s, [this](const Args& args) {
			// The zoom as the status bar shows it, relative to how large the game shows the level, `c c` for the middle of the level
			EditorLevelHandler* editHandler = _session.GetEditHandler();
			EditorCamera& camera = editHandler->GetCamera();
			Vector2f center(args.Float(1, 0.0f), args.Float(2, 0.0f));
			if (args[1] == "c"_s) {
				Vector2i levelBounds = editHandler->TileMap()->GetLevelBounds();
				center = Vector2f(float(levelBounds.X), float(levelBounds.Y)) * 0.5f;
			}
			camera.JumpTo(center, args.Float(3, 100.0f) * 0.01f * camera.GetGameplayScale());
			return DebugResult::Done;
		} });
		commands.push_back({ "/drift"_s, 2, "/drift <dx> <dy> [zoom] [frames]"_s, [this](const Args& args) {
			// The view moves by so many screen pixels every frame and zooms out by the factor, back and forth every so many
			// frames, `/drift 0 0` stops it
			_drift = Vector2f(args.Float(1, 0.0f), args.Float(2, 0.0f));
			_driftZoom = args.Float(3, 1.0f);
			_driftPeriod = std::max<std::int32_t>(std::int32_t(args.Float(4, 30.0f)), 1);
			_driftFrame = 0;
			return DebugResult::Done;
		} });
		commands.push_back({ "/lod"_s, 1, "/lod stats|on|off|auto"_s, [this](const Args& args) {
			if (args[1] == "stats"_s) {
				// The resolution of the images and the memory they take
				EditorLevelHandler* editHandler = _session.GetEditHandler();
				std::size_t drawnBytes = 0;
				std::int32_t drawn = _tileLod.GetDrawnChunks(&drawnBytes);
				LOGI("LOD: {} pixels for a tile ({} wanted), scale {:.3f}, {} images drawn ({:.1f} MB), {:.1f} MB of images in all",
					_tileLod.GetTilePixels(), _tileLod.GetWantedTilePixels(), editHandler != nullptr ? editHandler->GetCamera().GetScale() : 0.0f,
					drawn, double(drawnBytes) / (1024.0 * 1024.0), double(_tileLod.GetMemoryUsage()) / (1024.0 * 1024.0));
			} else {
				// The images of the layers from far away forced on or off, or by the zoom
				_lodMode = (args[1] == "on"_s ? 1 : (args[1] == "off"_s ? 0 : -1));
			}
			return DebugResult::Done;
		} });
	}

	void EditorDebugConsole::AddEditCommands(SmallVectorImpl<Command>& commands)
	{
		commands.push_back({ "/paint"_s, 4, "/paint <layer> <x> <y> <[source:]tile> [width] [height]"_s, [this](const Args& args) {
			LevelDocument& document = _session.GetDocument();
			std::int32_t layer = std::int32_t(args.Float(1, 0.0f));
			std::int32_t x = std::int32_t(args.Float(2, 0.0f));
			std::int32_t y = std::int32_t(args.Float(3, 0.0f));
			TileRef tile = { std::uint16_t(args.Float(4, 0.0f)), TileRef::DefaultTileSet, 0 };
			auto sourceAndTile = args[4].partition(':');
			if (!sourceAndTile[2].empty()) {
				tile.Source = std::uint8_t(std::strtol(String(sourceAndTile[0]).data(), nullptr, 10));
				tile.Index = std::uint16_t(std::strtol(String(sourceAndTile[2]).data(), nullptr, 10));
			}
			std::int32_t width = std::int32_t(args.Float(5, 1.0f));
			std::int32_t height = std::int32_t(args.Float(6, 1.0f));
			if (layer < 0 || layer >= std::int32_t(document.Layers.size())) {
				return DebugResult::Failed;
			}
			// TRANSLATORS: Name of an undo step in the history, after tiles were painted
			auto paint = std::make_unique<SetTilesCommand>(document, layer, _x("editor", "Paint"));
			for (std::int32_t j = 0; j < height; j++) {
				for (std::int32_t i = 0; i < width; i++) {
					paint->Set(x + i, y + j, tile);
				}
			}
			_session.Execute(Death::move(paint));
			return DebugResult::Done;
		} });
		commands.push_back({ "/stamp"_s, 0, "/stamp"_s, [this](const Args&) {
			// The brush, as a block grabbed by B makes it, and the cell under the pointer
			const TileStamp& stamp = _session.GetStamp();
			std::int32_t present = 0, empty = 0, events = 0;
			for (std::size_t i = 0; i < stamp.Tiles.size() && i < stamp.Present.size(); i++) {
				if (stamp.Present[i]) {
					present++;
					empty += (stamp.Tiles[i].IsEmpty() ? 1 : 0);
				}
			}
			for (const Formats::EventRecord& event : stamp.Events) {
				events += (LevelDocument::IsEmptyEvent(event) ? 0 : 1);
			}
			ToolPointer hover;
			bool hoverValid = _session.GetHoverPointer(hover);
			LOGI("Tool {}, picker {}", std::int32_t(_session.GetTools().GetChosenType()), _session.GetPickerPanel() == PickerPanel::Events ? "events" : "tiles");
			LOGI("Stamp {}x{}: {} tiles ({} empty), {} events of {}, {} off the grid, empty transparent {}, block grab {}, masks {}, pointer {} at {}, {}", stamp.Width, stamp.Height,
				present, empty, events, std::int32_t(stamp.Events.size()), std::int32_t(stamp.OffGridEvents.size()), stamp.EmptyIsTransparent ? "yes" : "no",
				_session.GetBlockGrab().IsActive() ? "marked" : "none", _session.GetMaskMode(),
				hoverValid ? "over the view" : "off the view",
				hover.Cell.X, hover.Cell.Y);
			return DebugResult::Done;
		} });
		// Not through the actions of Undo and Redo, those end what the pointer is in the middle of and do nothing without
		// a step to undo, a test that presses, undoes and releases checks what the history does meanwhile
		commands.push_back({ "/undo"_s, 0, "/undo"_s, [this](const Args&) {
			_session.Undo();
			return DebugResult::Done;
		} });
		commands.push_back({ "/redo"_s, 0, "/redo"_s, [this](const Args&) {
			_session.Redo();
			return DebugResult::Done;
		} });
		commands.push_back({ "/hide"_s, 1, "/hide <layer>"_s, [this](const Args& args) {
			_session.ToggleLayerFlag(std::int32_t(args.Float(1, 0.0f)), DocumentLayer::EditorHidden);
			return DebugResult::Done;
		} });
		commands.push_back({ "/layer"_s, 1, "/layer <layer>"_s, [this](const Args& args) {
			_session.SetCurrentLayer(std::int32_t(args.Float(1, 0.0f)));
			return DebugResult::Done;
		} });
		commands.push_back({ "/renderer"_s, 1, "/renderer <type> [color [alpha]]"_s, [this](const Args& args) {
			// Changes the renderer of the current layer, as an undoable step, "/renderer <type> [red [alpha]]" sets the color too
			// (the palette index of a tinted layer, the shift of a palette-shifted one, the tint opacity of a reflection)
			LevelDocument& document = _session.GetDocument();
			std::int32_t currentLayer = _session.GetCurrentLayer();
			if (currentLayer >= 0 && currentLayer < std::int32_t(document.Layers.size())) {
				const DocumentLayer& layer = document.Layers[currentLayer];
				Formats::LayerRecord record = layer.Record;
				std::uint8_t type = std::uint8_t(args.Float(1, 0.0f));
				constexpr std::uint8_t Reflection = std::uint8_t(Tiles::LayerRendererType::Reflection);
				if (type == Reflection && record.RendererType != Reflection) {
					// As the inspector starts one (see InspectorPanel), untinted and rippled a little
					record.Color[0] = 0;
					record.Color[1] = 0;
					record.Color[2] = 16;
					record.Color[3] = 0;
				}
				record.RendererType = type;
				if (args.size() >= 3) {
					record.Color[0] = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(args.Float(2, 0.0f)), 0, 255));
					if (record.RendererType == std::uint8_t(Tiles::LayerRendererType::PaletteShift)) {
						record.Color[1] = 255;
						record.Color[2] = 255;
					}
				}
				if (args.size() >= 4) {
					record.Color[3] = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(args.Float(3, 255.0f)), 0, 255));
				}
				// TRANSLATORS: Name of an undo step in the history, after properties of a layer were changed
				_session.Execute(std::make_unique<SetLayerPropertiesCommand>(document, currentLayer, record, layer.Name, _x("editor", "Layer properties")));
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/textured"_s, 2, "/textured <x> <y> [fade [flags]]"_s, [this](const Args& args) {
			// The parameters of a tunnel, a cylinder or a reflection of the current layer its color has no room for (see
			// Formats::TexturedLayerParams), the position in percent of the view, as an undoable step
			LevelDocument& document = _session.GetDocument();
			std::int32_t currentLayer = _session.GetCurrentLayer();
			if (currentLayer >= 0 && currentLayer < std::int32_t(document.Layers.size())) {
				const DocumentLayer& layer = document.Layers[currentLayer];
				Formats::LayerRecord record = layer.Record;
				record.Textured.FadeX = std::clamp<float>(args.Float(1, 50.0f), 0.0f, 100.0f) / 100.0f;
				record.Textured.FadeY = std::clamp<float>(args.Float(2, 50.0f), 0.0f, 100.0f) / 100.0f;
				if (args.size() >= 4) {
					record.Textured.Fade = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(args.Float(3, 192.0f)), 0, 255));
				}
				if (args.size() >= 5) {
					record.Textured.Flags = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(args.Float(4, 0.0f)), 0, 255));
				}
				_session.Execute(std::make_unique<SetLayerPropertiesCommand>(document, currentLayer, record, layer.Name, _x("editor", "Layer properties")));
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/innerspeeds"_s, 4, "/innerspeeds <x> <y> <auto x> <auto y>"_s, [this](const Args& args) {
			// How the texture of a reflection drawn only where the current layer is moves inside it (see
			// Formats::TexturedLayerParams::InnerSpeedX), as an undoable step
			LevelDocument& document = _session.GetDocument();
			std::int32_t currentLayer = _session.GetCurrentLayer();
			if (currentLayer >= 0 && currentLayer < std::int32_t(document.Layers.size())) {
				const DocumentLayer& layer = document.Layers[currentLayer];
				Formats::LayerRecord record = layer.Record;
				record.Textured.InnerSpeedX = args.Float(1, 0.0f);
				record.Textured.InnerSpeedY = args.Float(2, 0.0f);
				record.Textured.InnerAutoSpeedX = args.Float(3, 0.0f);
				record.Textured.InnerAutoSpeedY = args.Float(4, 0.0f);
				_session.Execute(std::make_unique<SetLayerPropertiesCommand>(document, currentLayer, record, layer.Name, _x("editor", "Layer properties")));
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/animframe"_s, 2, "/animframe <animation> <frames>"_s, [this](const Args& args) {
			// Frame time of an animated tile in frames
			LevelDocument& document = _session.GetDocument();
			std::int32_t index = std::int32_t(args.Float(1, 0.0f));
			if (index < 0 || index >= std::int32_t(document.Animations.size())) {
				return DebugResult::Failed;
			}
			DocumentAnimation animation = document.Animations[index];
			animation.Timing.FrameDuration16 = std::uint16_t(args.Float(2, 1.0f) * 16.0f);
			_session.Execute(std::make_unique<SetAnimationCommand>(document, index, Death::move(animation), "Animation timing"_s), true);
			return DebugResult::Done;
		} });
		commands.push_back({ "/links"_s, 0, "/links [next] [secret] [bonus]"_s, [this](const Args& args) {
			// "-" for none, as a step of the history as Level Properties changes them, then logged
			LevelDocument& document = _session.GetDocument();
			if (args.size() >= 2) {
				Formats::LevelProperties properties = document.Properties;
				String* links[] = { &properties.NextLevel, &properties.SecretLevel, &properties.BonusLevel };
				for (std::size_t i = 0; i < 3 && i + 1 < args.size(); i++) {
					*links[i] = (args[i + 1] == "-"_s ? String() : String(args[i + 1]));
				}
				// TRANSLATORS: Name of an undo step in the history, after properties of the level were changed
				_session.Execute(std::make_unique<SetLevelPropertiesCommand>(document, document.Flags, properties, _x("editor", "Level properties")));
			}
			LOGI("Links of \"{}\": next \"{}\", secret \"{}\", bonus \"{}\"", _files.GetFilePath(), document.Properties.NextLevel,
				document.Properties.SecretLevel, document.Properties.BonusLevel);
			return DebugResult::Done;
		} });
		commands.push_back({ "/saveprefab"_s, 1, "/saveprefab <name>"_s, [this](const Args& args) {
			// The brush as a prefab of the default tile set
			LevelDocument& document = _session.GetDocument();
			SmallVector<TilePrefab, 0> prefabs;
			TilePrefab::Load(document.Properties.DefaultTileSet, prefabs);
			prefabs.push_back(TilePrefab::FromStamp(document, _session.GetStamp(), args[1]));
			return QuietResult(TilePrefab::Save(document.Properties.DefaultTileSet, prefabs));
		} });
	}

	void EditorDebugConsole::AddEventCommands(SmallVectorImpl<Command>& commands)
	{
		commands.push_back({ "/brush"_s, 1, "/brush <event key>"_s, [this](const Args& args) {
			// Event to place by its key, as the catalog names it
			const EventDescriptor* descriptor = EventCatalog::FindByKey(args[1]);
			if (descriptor == nullptr) {
				return DebugResult::Failed;
			}
			_session.SetEventBrush(EventStamp::FromEvent(EventCatalog::MakeDefault(descriptor->Type)));
			return DebugResult::Done;
		} });
		commands.push_back({ "/selectevent"_s, 2, "/selectevent <x> <y> [add]"_s, [this](const Args& args) {
			Vector2i gridSize = _session.GetDocument().GetEventGridSize();
			EventSelection& selection = _session.GetEventSelection();
			std::int32_t x = std::int32_t(args.Float(1, 0.0f)), y = std::int32_t(args.Float(2, 0.0f));
			if (x < 0 || y < 0 || x >= gridSize.X || y >= gridSize.Y) {
				return DebugResult::Failed;
			}
			if (args.size() >= 4 && args[3] == "add"_s) {
				selection.Add(std::uint32_t(x + y * gridSize.X));
			} else {
				selection.Set(std::uint32_t(x + y * gridSize.X));
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/placeevent"_s, 3, "/placeevent <event key> <x> <y> [bytes...]"_s, [this](const Args& args) {
			// An event of the catalog with its default parameters, the bytes after the position replace the first bytes
			// of the parameters
			LevelDocument& document = _session.GetDocument();
			const EventDescriptor* descriptor = EventCatalog::FindByKey(args[1]);
			Vector2i gridSize = document.GetEventGridSize();
			std::int32_t x = std::int32_t(args.Float(2, -1.0f)), y = std::int32_t(args.Float(3, -1.0f));
			if (descriptor == nullptr || x < 0 || y < 0 || x >= gridSize.X || y >= gridSize.Y) {
				return DebugResult::Failed;
			}
			Formats::EventRecord event = EventCatalog::MakeDefault(descriptor->Type);
			for (std::size_t i = 4; i < args.size() && i - 4 < sizeof(event.Params); i++) {
				event.Params[i - 4] = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(args.Float(i, 0.0f)), 0, 255));
			}
			// TRANSLATORS: Name of an undo step in the history, after events were placed
			auto place = std::make_unique<SetEventsCommand>(document, _x("editor", "Place Events"));
			place->Set(x, y, event);
			_session.Execute(Death::move(place));
			return DebugResult::Done;
		} });
		commands.push_back({ "/placeoffgrid"_s, 3, "/placeoffgrid <event key> <x> <y> [delay]"_s, [this](const Args& args) {
			// An off-grid event at a position in pixels, the editor can't place them otherwise, with a delay it's a generator
			// of the event
			LevelDocument& document = _session.GetDocument();
			const EventDescriptor* descriptor = EventCatalog::FindByKey(args[1]);
			if (descriptor == nullptr) {
				return DebugResult::Failed;
			}
			SmallVector<Formats::OffGridEventRecord, 0> events;
			for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
				events.push_back(event);
			}
			Formats::OffGridEventRecord& record = events.emplace_back();
			record.X = std::uint32_t(std::max(args.Float(2, 0.0f), 0.0f));
			record.Y = std::uint32_t(std::max(args.Float(3, 0.0f), 0.0f));
			record.Event = EventCatalog::MakeDefault(descriptor->Type);
			if (args.size() >= 5) {
				record.Event.Flags |= Formats::EventRecord::Generator;
				record.Event.GeneratorDelay = std::uint8_t(std::clamp(args.Float(4, 0.0f), 0.0f, 255.0f));
			}
			_session.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), _x("editor", "Place Events")));
			return DebugResult::Done;
		} });
		commands.push_back({ "/events"_s, 0, "/events"_s, [this](const Args&) {
			const LevelDocument& document = _session.GetDocument();
			EventSelection& selection = _session.GetEventSelection();
			LOGI("{} events, {} selected, event tool mode {}", document.Events.size(), selection.GetTotalCount(), std::int32_t(_session.GetTools().GetSettings().EventMode));
			for (const EventStamp::Item& item : _session.GetEventBrush().Items) {
				LOGI("Brush event at {}, {}: type {}, params {} {} {} {}", item.Offset.X, item.Offset.Y, item.Event.Type,
					item.Event.Params[0], item.Event.Params[1], item.Event.Params[2], item.Event.Params[3]);
			}
			Vector2i gridSize = document.GetEventGridSize();
			for (std::uint32_t cell : selection.GetCells()) {
				const Formats::EventRecord& event = document.GetEvent(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
				LOGI("Selected event at {}, {}: type {}, flags 0x{:x}, params {} {} {} {}", cell % std::uint32_t(gridSize.X), cell / std::uint32_t(gridSize.X),
					event.Type, event.Flags, event.Params[0], event.Params[1], event.Params[2], event.Params[3]);
			}
			for (std::int32_t index : selection.GetOffGrid()) {
				if (index < std::int32_t(document.OffGridEvents.size())) {
					const Formats::OffGridEventRecord& record = document.OffGridEvents[index];
					LOGI("Selected off-grid event {} at {}, {} px: type {}, flags 0x{:x}", index, record.X, record.Y, record.Event.Type, record.Event.Flags);
				}
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/findevents"_s, 1, "/findevents <event key>"_s, [this](const Args& args) {
			// Where the events of a kind of the catalog are, at most 20 of them
			const LevelDocument& document = _session.GetDocument();
			const EventDescriptor* descriptor = EventCatalog::FindByKey(args[1]);
			if (descriptor == nullptr) {
				return DebugResult::Failed;
			}
			Vector2i gridSize = document.GetEventGridSize();
			std::int32_t found = 0;
			for (std::int32_t y = 0; y < gridSize.Y && found < 20; y++) {
				for (std::int32_t x = 0; x < gridSize.X && found < 20; x++) {
					const Formats::EventRecord& event = document.GetEvent(x, y);
					if (EventType(event.Type) == descriptor->Type) {
						LOGI("Event {} at {}, {}: params {} {} {} {}", args[1], x, y, event.Params[0], event.Params[1], event.Params[2], event.Params[3]);
						found++;
					}
				}
			}
			return DebugResult::Done;
		} });
	}

	void EditorDebugConsole::AddFileCommands(SmallVectorImpl<Command>& commands)
	{
		// Not through the actions of Save and Export, those ask for the path in a dialog
		auto saveTo = [this](const Args& args) {
			String path = args.Line.trimmed().exceptPrefix(args.Verb.size()).trimmed();
			return QuietResult(_files.SaveTo(path, args.Verb == "/export"_s));
		};
		commands.push_back({ "/save"_s, 1, "/save <path>"_s, saveTo });
		commands.push_back({ "/export"_s, 1, "/export <path>"_s, saveTo });
		commands.push_back({ "/exportplayonly"_s, 1, "/exportplayonly <path>"_s, [this](const Args& args) {
			// As Export with "Only to be played" checked
			String path = args.Line.trimmed().exceptPrefix(args.Verb.size()).trimmed();
			_files.SetExportNotEditable(true);
			bool saved = _files.SaveTo(path, true);
			_files.SetExportNotEditable(false);
			return QuietResult(saved);
		} });
		commands.push_back({ "/autosave"_s, 0, "/autosave"_s, [this](const Args&) {
			// Written at once, not on a thread, so it can be checked right after
			return QuietResult(_files.GetAutosave().SaveNow(_session.GetDocument(), _session.GetHistory(), true));
		} });
		commands.push_back({ "/restorecopy"_s, 1, "/restorecopy <path>"_s, [this](const Args& args) {
			// Restores a recovery copy with its history, as the recovery copies dialog does
			return QuietResult(_files.RestoreRecoveryCopy(args[1], _files.GetFilePath(), _files.GetLevelName()));
		} });
		commands.push_back({ "/preview"_s, 1, "/preview <image.png>"_s, [this](const Args& args) {
			// The preview kept with the level when it's saved
			SmallVector<std::uint8_t, 0> preview;
			LevelThumbnail::MakePreview(_session.GetDocument(), _session.GetTileMap(), _session.GetTileSetDataCache(), preview);
			SmallVector<std::uint8_t, 0> rgba;
			Vector2i size;
			if (!Formats::LevelPreview::Decode(preview, rgba, size)) {
				return DebugResult::Failed;
			}
			PngImage image;
			image.Create(size.X, size.Y, false);
			std::memcpy(image.Pixels.data(), rgba.data(), rgba.size());
			auto s = fs::Open(args[1], FileAccess::Write);
			return QuietResult(s->IsValid() && PngCodec::Encode(*s, image));
		} });
		commands.push_back({ "/exportsprite"_s, 2, "/exportsprite <sprite.aura> <image.png>"_s, [](const Args& args) {
			// The whole sprite sheet in the colors of the sprites, to draw icons after
			ContentResolver::SpriteSheet sheet;
			if (!ContentResolver::Get().ReadSpriteSheet(args[1], sheet) || sheet.Pixels == nullptr) {
				return DebugResult::Failed;
			}
			const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
			PngImage image;
			image.Create(sheet.Size.X, sheet.Size.Y, false);
			std::int32_t channels = sheet.ChannelCount;
			for (std::int32_t i = 0; i < sheet.Size.X * sheet.Size.Y; i++) {
				const std::uint8_t* src = &sheet.Pixels[std::size_t(i) * channels];
				std::uint32_t color;
				if (channels == 4) {
					color = std::uint32_t(src[0]) | (std::uint32_t(src[1]) << 8) | (std::uint32_t(src[2]) << 16) | (std::uint32_t(src[3]) << 24);
				} else {
					std::uint32_t alpha = (channels == 2 ? src[1] : (src[0] != 0 ? 255 : 0));
					color = (palette[src[0]] & 0x00FFFFFFu) | (alpha << 24);
				}
				std::memcpy(&image.Pixels[std::size_t(i) * 4], &color, 4);
			}
			auto s = fs::Open(args[2], FileAccess::Write);
			return QuietResult(s->IsValid() && PngCodec::Encode(*s, image));
		} });
	}

	void EditorDebugConsole::AddPlayTestCommands(SmallVectorImpl<Command>& commands)
	{
		commands.push_back({ "/exitlevel"_s, 0, "/exitlevel"_s, [this](const Args&) {
			PlayTestController& playTest = _session.GetPlayTest();
			if (playTest.GetPlayTest() == nullptr || !playTest.IsInGame()) {
				return DebugResult::Failed;
			}
			playTest.GetPlayTest()->GetLevelHandler().BeginLevelChange(nullptr, ExitType::Normal);
			return DebugResult::Done;
		} });
		commands.push_back({ "/stopplay"_s, 0, "/stopplay [keep]"_s, [this](const Args& args) {
			// Back to the editor, `/stopplay keep` keeps the play-test to resume
			_session.StopPlayTest(args.size() >= 2 && args[1] == "keep"_s);
			return DebugResult::Done;
		} });
		commands.push_back({ "/teleport"_s, 2, "/teleport <x> <y>"_s, [this](const Args& args) {
			// The first player of the level played moved to a position in the level
			LevelHandler* gameHandler = _session.GetPlayTest().GetGameHandler();
			PlayTestDebug* debug = (gameHandler != nullptr ? gameHandler->GetPlayTestDebug() : nullptr);
			if (debug == nullptr || !debug->TeleportPlayer(0, Vector2f(args.Float(1, 0.0f), args.Float(2, 0.0f)))) {
				return DebugResult::Failed;
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/step"_s, 0, "/step"_s, [this](const Args&) {
			// A single update of the level played, it stands still then
			_session.GetPlayTest().StepFrame();
			return DebugResult::Done;
		} });
		commands.push_back({ "/paths"_s, 0, "/paths"_s, [this](const Args&) {
			// What is kept of the paths of the play-tests
			const PlayPathHistory& history = _session.GetPlayTest().GetPathHistory();
			LOGI("Play-test paths: {}, best finished in {:.0f} frames, {} deaths", std::int32_t(history.GetPaths().size()), history.GetBestExitTime(), history.GetTotalDeaths());
			return DebugResult::Done;
		} });
		commands.push_back({ "/reach"_s, 0, "/reach [character] [multiplayer starts]"_s, [this](const Args& args) {
			// Where the player can get, logged
			std::int32_t character = std::int32_t(args.Float(1, 0.0f));
			bool fromMpStarts = (args.Float(2, 0.0f) != 0.0f);
			SmallVector<std::int32_t, 0> distances;
			_session.ReloadRuntime();
			if (!_session.GetPlayOverlays().ComputeReachability(distances, character, fromMpStarts)) {
				return DebugResult::Failed;
			}
			std::int32_t reached = 0;
			for (std::int32_t distance : distances) {
				reached += (distance >= 0 ? 1 : 0);
			}
			LOGI("Reachable tiles: {}", reached);
			return DebugResult::Done;
		} });
		commands.push_back({ "/playerstate"_s, 0, "/playerstate [score lives]"_s, [this](const Args& args) {
			// Score and lives added to the player of the play-test, and what it has then logged
			PlayTestController& playTest = _session.GetPlayTest();
			if (Actors::Player* player = (playTest.GetPlayTest() != nullptr ? playTest.GetPlayTest()->GetPlayer() : nullptr)) {
				if (args.size() >= 3) {
					player->AddScore(std::int32_t(args.Float(1, 0.0f)));
					player->AddLives(std::int32_t(args.Float(2, 0.0f)));
				}
				LOGI("Play-test player: score {}, lives {}, health {}", player->GetScore(), player->GetLives(), player->GetHealth());
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/playring"_s, 1, "/playring <progress>"_s, [this](const Args& args) {
			// Holds the transition into the level being played at a point, so it can be looked at, or the one back into
			// the editor after a play-test
			PlayTestController& playTest = _session.GetPlayTest();
			if (playTest.IsInGame()) {
				playTest.GetTransition().Start(_session.GetEditHandler()->GetFrameTexture(), playTest.GetPlayTest() != nullptr ? playTest.GetPlayTest()->GetFrameTexture() : nullptr, _session.GetAccentColor());
			} else {
				IPlayTest* played = (playTest.GetClosingPlayTest() != nullptr ? playTest.GetClosingPlayTest() : playTest.GetPlayTest());
				if (!playTest.IsEditing() || played == nullptr || played->GetFrameTexture() == nullptr) {
					return DebugResult::Failed;
				}
				playTest.GetTransition().Start(played->GetFrameTexture(), nullptr, _session.GetAccentColor());
			}
			playTest.GetTransition().SetProgress(args.Float(1, 0.5f), true);
			return DebugResult::Done;
		} });
	}

	void EditorDebugConsole::AddInputCommands(SmallVectorImpl<Command>& commands)
	{
		commands.push_back({ "/action"_s, 1, "/action <action ID>"_s, [this](const Args& args) {
			return QuietResult(_session.GetActions().Execute(args[1]));
		} });
		auto click = [this](const Args& args) {
			// Through the same path as the mouse, relative to the size of the window if the values are fractions
			Vector2f pos(args.Float(1, 0.0f), args.Float(2, 0.0f));
			Vector2i size = _session.GetViewSize();
			if (pos.X < 1.0f && pos.Y < 1.0f) {
				pos = Vector2f(pos.X * size.X, pos.Y * size.Y);
			}
			MouseEvent event;
			event.x = std::int32_t(pos.X);
			event.y = std::int32_t(pos.Y);
			event.button = (args.Verb == "/rclick"_s ? MouseButton::Right : MouseButton::Left);
			_session.OnMouseDown(event);
			_session.OnMouseUp(event);
			return DebugResult::Done;
		};
		commands.push_back({ "/click"_s, 2, "/click <x> <y>"_s, click });
		commands.push_back({ "/rclick"_s, 2, "/rclick <x> <y>"_s, click });
		commands.push_back({ "/drag"_s, 4, "/drag <x1> <y1> <x2> <y2>"_s, [this](const Args& args) {
			Vector2f from(args.Float(1, 0.0f), args.Float(2, 0.0f));
			Vector2f to(args.Float(3, 0.0f), args.Float(4, 0.0f));
			MouseEvent event;
			event.button = MouseButton::Left;
			event.x = std::int32_t(from.X);
			event.y = std::int32_t(from.Y);
			_session.OnMouseDown(event);
			struct DebugMouseState : MouseState {
				bool isButtonDown(MouseButton button) const override { return button == MouseButton::Left; }
			} state;
			constexpr std::int32_t Steps = 16;
			for (std::int32_t i = 1; i <= Steps; i++) {
				state.x = std::int32_t(from.X + (to.X - from.X) * i / Steps);
				state.y = std::int32_t(from.Y + (to.Y - from.Y) * i / Steps);
				_session.OnMouseMove(state);
			}
			event.x = std::int32_t(to.X);
			event.y = std::int32_t(to.Y);
			_session.OnMouseUp(event);
			return DebugResult::Done;
		} });
		auto pressOrRelease = [this](const Args& args) {
			// A drag over several frames, with /move between, so what animates while it's held can be seen
			MouseEvent event;
			event.button = MouseButton::Left;
			event.x = std::int32_t(args.Float(1, 0.0f));
			event.y = std::int32_t(args.Float(2, 0.0f));
			if (args.Verb == "/press"_s) {
				_session.OnMouseDown(event);
			} else {
				_session.OnMouseUp(event);
			}
			return DebugResult::Done;
		};
		commands.push_back({ "/press"_s, 2, "/press <x> <y>"_s, pressOrRelease });
		commands.push_back({ "/release"_s, 2, "/release <x> <y>"_s, pressOrRelease });
		commands.push_back({ "/move"_s, 2, "/move <x> <y>"_s, [this](const Args& args) {
			struct DebugMouseState : MouseState {
				bool isButtonDown(MouseButton button) const override { return button == MouseButton::Left; }
			} state;
			state.x = std::int32_t(args.Float(1, 0.0f));
			state.y = std::int32_t(args.Float(2, 0.0f));
			_session.OnMouseMove(state);
			return DebugResult::Done;
		} });
		commands.push_back({ "/hover"_s, 2, "/hover <x> <y>"_s, [this](const Args& args) {
			struct DebugMouseState : MouseState {
				bool isButtonDown(MouseButton button) const override { return false; }
			} state;
			state.x = std::int32_t(args.Float(1, 0.0f));
			state.y = std::int32_t(args.Float(2, 0.0f));
			_session.OnMouseMove(state);
			return DebugResult::Done;
		} });
		commands.push_back({ "/wheel"_s, 3, "/wheel <x> <y> <delta>"_s, [this](const Args& args) {
			// Scrolls at a position, as the mouse would
			struct DebugMouseState : MouseState {
				bool isButtonDown(MouseButton button) const override { return false; }
			} state;
			state.x = std::int32_t(args.Float(1, 0.0f));
			state.y = std::int32_t(args.Float(2, 0.0f));
			_session.OnMouseMove(state);
			ScrollEvent event;
			event.x = 0.0f;
			event.y = args.Float(3, 0.0f);
			_session.OnMouseWheel(event);
			return DebugResult::Done;
		} });
		auto keyEvent = [this](const Args& args) {
			KeyboardEvent event;
			StringView key = args[1];
			struct KeyName { const char* Name; Keys Key; };
			static const KeyName KeyNames[] = {
				{ "enter", Keys::Return }, { "escape", Keys::Escape }, { "delete", Keys::Delete }, { "f5", Keys::F5 },
				{ "left", Keys::Left }, { "right", Keys::Right }, { "up", Keys::Up }, { "down", Keys::Down },
				{ "space", Keys::Space }, { "ctrl", Keys::LCtrl }, { "shift", Keys::LShift }, { "alt", Keys::LAlt },
			};
			event.sym = Keys::Unknown;
			for (const KeyName& name : KeyNames) {
				if (key == StringView(name.Name)) {
					event.sym = name.Key;
				}
			}
			// A letter for itself, as B grabbing a block or M held showing the masks
			if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') {
				event.sym = Keys(std::int32_t(Keys::A) + (key[0] - 'a'));
			}
			if (event.sym == Keys::Unknown) {
				return DebugResult::Failed;
			}
			// The modifier keys held, as a real key event has them
			std::int32_t modifiers = _input.GetModifiers();
			std::int32_t modifier = (event.sym == Keys::LCtrl ? KeyMod::Ctrl : (event.sym == Keys::LShift ? KeyMod::Shift : (event.sym == Keys::LAlt ? KeyMod::Alt : 0)));
			event.mod = (args.Verb == "/keyup"_s ? (modifiers & ~modifier) : (modifiers | modifier));
			if (args.Verb != "/keyup"_s) {
				_session.OnKeyPressed(event);
			}
			if (args.Verb != "/keydown"_s) {
				_session.OnKeyReleased(event);
			}
			return DebugResult::Done;
		};
		commands.push_back({ "/key"_s, 1, "/key enter|escape|delete|f5|left|right|up|down|space|ctrl|shift|alt|<a-z>"_s, keyEvent });
		commands.push_back({ "/keydown"_s, 1, "/keydown <key>, as /key"_s, keyEvent });
		commands.push_back({ "/keyup"_s, 1, "/keyup <key>, as /key"_s, keyEvent });
		commands.push_back({ "/type"_s, 1, "/type <text>, a space written as +"_s, [this](const Args& args) {
			// Typed into what has the focus character by character, as the keyboard does
			for (char c : args[1]) {
				TextInputEvent event;
				event.text[0] = (c == '+' ? ' ' : c);
				event.length = 1;
				_session.OnTextInput(event);
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/touch"_s, 1, "/touch down|pointerdown|move|pointerup|up <id> <x> <y> [<id> <x> <y> ...]"_s, [this](const Args& args) {
			// In pixels of the screen, the index of the finger that came or went is the last one given
			TouchEvent event;
			StringView type = args[1];
			event.type = (type == "down"_s ? TouchEventType::Down : type == "pointerdown"_s ? TouchEventType::PointerDown :
				type == "pointerup"_s ? TouchEventType::PointerUp : type == "up"_s ? TouchEventType::Up : TouchEventType::Move);
			Vector2f screen = _session.GetSurface()->GetScreenSize();
			for (std::size_t i = 2; i + 2 < args.size() + 0 && event.count < TouchEvent::MaxPointers; i += 3) {
				TouchEvent::Pointer& pointer = event.pointers[event.count++];
				pointer.id = std::int32_t(args.Float(i, 0.0f));
				pointer.x = args.Float(i + 1, 0.0f) / screen.X;
				pointer.y = args.Float(i + 2, 0.0f) / screen.Y;
				pointer.pressure = 1.0f;
			}
			event.actionIndex = std::int32_t(event.count) - 1;
			_session.OnTouchEvent(event);
			return DebugResult::Done;
		} });
		commands.push_back({ "/gamepad"_s, 2, "/gamepad <x> <y>"_s, [this](const Args& args) {
			// As if a gamepad was used, with its cursor at a position
			_input.SetInputModality(InputModality::Gamepad);
			_input.SetGamepadCursor(Vector2f(args.Float(1, 0.0f), args.Float(2, 0.0f)));
			_input.PointerMove(_input.GetGamepadCursor());
			return DebugResult::Done;
		} });
		commands.push_back({ "/radial"_s, 0, "/radial [x y]"_s, [this](const Args& args) {
			// `/radial` shows the ring of tools at the gamepad cursor, `/radial x y` then points the stick at one
			if (_input.GetRadialMenu() == nullptr) {
				_input.ShowToolMenu(_input.GetGamepadCursor());
			} else if (args.size() >= 3) {
				_input.GetRadialMenu()->PointTo(Vector2f(args.Float(1, 0.0f), args.Float(2, 0.0f)));
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/focus"_s, 0, "/focus [back]"_s, [this](const Args& args) {
			// Moves the keyboard focus as Tab does, "/focus back" as Shift+Tab
			UiSurface* surface = _session.GetSurface();
			if (surface == nullptr) {
				return DebugResult::Failed;
			}
			return QuietResult(surface->MoveFocus(args.size() >= 2 && args[1] == "back"_s));
		} });
		commands.push_back({ "/keyboard"_s, 0, "/keyboard"_s, [this](const Args&) {
			// The keyboard of the editor, as where the system has none
			UiOnScreenKeyboard::Show(*_session.GetSurface());
			return DebugResult::Done;
		} });
		commands.push_back({ "/theme"_s, 1, "/theme default|contrast [still] [readable]"_s, [](const Args& args) {
			// Not saved
			UiThemeKind kind = (args[1] == "contrast"_s ? UiThemeKind::HighContrast : UiThemeKind::Default);
			bool reducedMotion = false, readableFont = false;
			for (std::size_t i = 2; i < args.size(); i++) {
				reducedMotion |= (args[i] == "still"_s);
				readableFont |= (args[i] == "readable"_s);
			}
			UiTheme::Apply(kind, reducedMotion, readableFont);
			return DebugResult::Done;
		} });
		commands.push_back({ "/safearea"_s, 4, "/safearea <left> <top> <right> <bottom>, in percent"_s, [](const Args& args) {
			// As the Safe Area of the graphics options sets it, not saved unless the game saves its options
			for (std::int32_t i = 0; i < std::int32_t(SafeAreaEdge::Count); i++) {
				PreferencesCache::SafeArea[i] = std::uint16_t(std::clamp(args.Float(i + 1, 0.0f) * 10.0f, 0.0f, float(PreferencesCache::MaxSafeArea)));
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/keymap"_s, 1, "/keymap default|jcs|mlle"_s, [this](const Args& args) {
			// Saved as choosing it in the dialog does
			ActionRegistry& actions = _session.GetActions();
			KeymapPreset preset = (args[1] == "jcs"_s ? KeymapPreset::Jcs : (args[1] == "mlle"_s ? KeymapPreset::Mlle : KeymapPreset::Default));
			actions.ApplyPreset(preset);
			return QuietResult(actions.SaveOverrides(IEditorContext::GetHotkeysPath()));
		} });
	}

	void EditorDebugConsole::AddTileSetCommands(SmallVectorImpl<Command>& commands)
	{
		commands.push_back({ "/exporttileset"_s, 1, "/exporttileset <image.png>"_s, [this](const Args& args) {
			// The default tile set as images, without the dialog
			const Formats::TileSetData* tileSet = _session.GetTileSetData(_session.GetDocument().Properties.DefaultTileSet);
			if (tileSet == nullptr) {
				return DebugResult::Failed;
			}
			PngImage sheet = MakeTileSetSheet(*tileSet, ContentResolver::Get().GetPalettes().data(), 10);
			PngImage masks = MakeTileSetMaskSheet(*tileSet, 10);
			auto s = fs::Open(args[1], FileAccess::Write);
			auto m = fs::Open(PngCodec::GetMasksPath(args[1]), FileAccess::Write);
			PngCodec::Encode(*s, sheet);
			PngCodec::Encode(*m, masks);
			return DebugResult::Done;
		} });
		commands.push_back({ "/palette"_s, 2, "/palette <entry> <rrggbb>"_s, [this](const Args& args) {
			// An entry of the palette of the level, which it has then, starting as the one in use
			std::int32_t entry = std::int32_t(args.Float(1, 0.0f));
			if (entry <= 0 || entry > 255) {
				return DebugResult::Failed;
			}
			std::uint32_t palette[256];
			std::memcpy(palette, ContentResolver::Get().GetPalettes().data(), sizeof(palette));
			std::uint32_t rgb = std::uint32_t(std::strtoul(String(args[2]).data(), nullptr, 16));
			palette[entry] = ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16) | 0xFF000000u;
			TimeStamp start = TimeStamp::now();
			// TRANSLATORS: Name of an undo step in the history, after the palette of the level was changed
			_session.Execute(std::make_unique<SetLevelPaletteCommand>(_session.GetDocument(), true, palette, _x("editor", "Palette")));
			LOGI("Palette entry {} changed in {:.2f} ms", entry, start.millisecondsSince());
			return DebugResult::Done;
		} });
		commands.push_back({ "/palettemove"_s, 3, "/palettemove <first> <last> <-1|1> [keep]"_s, [this](const Args& args) {
			// Entries of the palette moved, with the pixels of the tiles
			TimeStamp start = TimeStamp::now();
			bool moved = MovePaletteEntries(_session, std::int32_t(args.Float(1, 0.0f)), std::int32_t(args.Float(2, 0.0f)), std::int32_t(args.Float(3, 0.0f)),
				args.size() >= 5 && args[4] == "keep"_s);
			LOGI("Palette entries moved in {:.2f} ms", start.millisecondsSince());
			return QuietResult(moved);
		} });
		commands.push_back({ "/tileoverride"_s, 2, "/tileoverride <tile ID> <palette index>"_s, [this](const Args& args) {
			// The tile filled with one color and solid, only in this level
			LevelDocument& document = _session.GetDocument();
			std::int32_t tileId = std::int32_t(args.Float(1, 0.0f));
			if (tileId <= 0 || tileId >= document.GetAnimatedTilesOffset()) {
				return DebugResult::Failed;
			}
			std::uint8_t indices[32 * 32], mask[32 * 32];
			std::memset(indices, std::uint8_t(args.Float(2, 1.0f)), sizeof(indices));
			std::memset(mask, 1, sizeof(mask));
			// TRANSLATORS: Name of an undo step in the history, after a tile was changed only in this level
			_session.Execute(std::make_unique<SetTileOverrideCommand>(document, std::uint16_t(tileId), indices, mask, _x("editor", "Tile override")));
			return DebugResult::Done;
		} });
		commands.push_back({ "/applytiles"_s, 1, "/applytiles <source> [new name|-] [palette]"_s, [this](const Args& args) {
			// The tiles changed in the level into their tile set or a new one
			StringView newName = (args.size() >= 3 && args[2] != "-"_s ? args[2] : StringView());
			return QuietResult(ApplyTileChanges(_session, std::uint8_t(args.Float(1, 0.0f)), newName, args.size() >= 4 && args[3] == "palette"_s));
		} });
		auto addTiles = [this](const Args& args) {
			// `/addtiles` adds the tiles of an image to the end of a tile set, `/fxtiles` effect tiles there, they're the brush then
			TileTarget target;
			if (args[1] == "level"_s) {
				target.Type = TileTarget::Kind::Level;
			} else if (args[1].hasPrefix("new:"_s)) {
				target.Type = TileTarget::Kind::NewTileSet;
				target.Name = args[1].exceptPrefix(4);
			} else {
				target.Source = std::uint8_t(args.Float(1, 0.0f));
			}
			if (args.Verb == "/fxtiles"_s) {
				Formats::TileSetData tiles;
				std::int32_t width = std::clamp<std::int32_t>(std::int32_t(args.Float(3, 2.0f)), 1, 16), height = std::clamp<std::int32_t>(std::int32_t(args.Float(4, 2.0f)), 1, 16);
				GenerateFxTiles(FxTileKind(std::clamp<std::int32_t>(std::int32_t(args.Float(2, 0.0f)), 0, std::int32_t(FxTileKind::Count) - 1)), width, height, 1.0f, tiles);
				TileRef first;
				if (!AppendTiles(_session, target, tiles, first)) {
					return DebugResult::Failed;
				}
				TileStamp stamp;
				stamp.Width = width;
				stamp.Height = height;
				for (std::int32_t i = 0; i < width * height; i++) {
					stamp.Tiles.push_back(TileRef{ std::uint16_t(first.Index + i), first.Source, 0 });
					stamp.Present.push_back(true);
				}
				_session.SetStamp(Death::move(stamp));
				LOGI("Effect tiles added to source {} from tile {}", first.Source, first.Index);
				return DebugResult::Quiet;
			}
			bool all = false, dither = false;
			for (std::size_t i = 3; i < args.size(); i++) {
				all |= (args[i] == "all"_s);
				dither |= (args[i] == "dither"_s);
			}
			return QuietResult(AddTilesFromImage(_session, target, args[2], !all, dither));
		};
		commands.push_back({ "/addtiles"_s, 2, "/addtiles <source|level|new:name> <image> [all] [dither]"_s, addTiles });
		commands.push_back({ "/fxtiles"_s, 4, "/fxtiles <source|level|new:name> <kind> <width> <height>"_s, addTiles });
		commands.push_back({ "/newtileset"_s, 2, "/newtileset <name> <tiles> [extra|default|save]"_s, [this](const Args& args) {
			StringView use = (args.size() >= 4 ? args[3] : "extra"_s);
			return QuietResult(CreateEmptyTileSet(_session, args[1], std::int32_t(args.Float(2, 100.0f)), use != "save"_s, use == "default"_s));
		} });
		commands.push_back({ "/importtileset"_s, 2, "/importtileset <image> <name> [extra|default|save] [own|true|match|reduce] [dither]"_s, [this](const Args& args) {
			StringView use = (args.size() >= 4 ? args[3] : "extra"_s);
			StringView colors = (args.size() >= 5 ? args[4] : StringView());
			ImportTileSetImage(_session, args[1], args[2], use != "save"_s, use == "default"_s, colors, args.size() >= 6 && args[5] == "dither"_s);
			return DebugResult::Done;
		} });
		commands.push_back({ "/importtilesetfile"_s, 2, "/importtilesetfile <file.j2t> <name> [extra|default|save]"_s, [this](const Args& args) {
			// A tile set file of the original game converted, or one of this game copied, as the dialog does
			Formats::TileSetData data;
			bool original = false;
			if (!ReadTileSetFile(args[1], data, original)) {
				return DebugResult::Failed;
			}
			StringView use = (args.size() >= 4 ? args[3] : "extra"_s);
			LOGI("Tile set file \"{}\" read, {} tiles, {}", args[1], data.TileCount, original ? "original"_s : "converted"_s);
			return (ImportTileSetFile(_session, data, args[2], use != "save"_s, use == "default"_s) ? DebugResult::Done : DebugResult::Failed);
		} });
		commands.push_back({ "/exportrules"_s, 1, "/exportrules <tile set> [<tile set> ...], a space in a name written as +"_s, [this](const Args& args) {
			// Rules of tile sets made from what the levels use the most, as a start for rules shipped with the game
			for (std::size_t i = 1; i < args.size(); i++) {
				String tileSet = StringUtils::replaceAll(String(args[i]), '+', ' ');
				SmartTileModel model;
				model.Request(tileSet, _session.GetTileSetLibrary());
				while (!model.IsReady()) {
					model.Update();
				}
				if (model.GetLevelCount() == 0) {
					LOGW("No level uses tile set \"{}\"", tileSet);
					continue;
				}
				TerrainRules& rules = model.GetRules();
				rules.SetCurrentTerrain(0);
				rules.Clear();
				SmallVector<std::uint8_t, 0> patterns;
				TerrainRules::GetPatterns(patterns);
				SmallVector<TileRef, 4> tiles;
				std::int32_t ruled = 0;
				for (std::uint8_t pattern : patterns) {
					model.GetTopTiles(pattern, true, 3, 0.25f, tiles);
					if (!tiles.empty()) {
						rules.Set(pattern, tiles);
						ruled++;
					}
				}
				bool saved = rules.Save(tileSet);
				LOGI("Rules of tile set \"{}\" made from {} levels, {} of 47 patterns ({})", tileSet, model.GetLevelCount(), ruled, saved ? "saved" : "NOT saved");
			}
			return DebugResult::Done;
		} });
	}

	void EditorDebugConsole::AddDiagnosticCommands(SmallVectorImpl<Command>& commands)
	{
		commands.push_back({ "/layers"_s, 0, "/layers"_s, [this](const Args&) {
			const LevelDocument& document = _session.GetDocument();
			for (std::size_t i = 0; i < document.Layers.size(); i++) {
				const DocumentLayer& layer = document.Layers[i];
				TileRef first = (layer.GetWidth() > 0 && layer.GetHeight() > 0 ? layer.GetTile(0, 0) : TileRef{});
				LOGI("Layer {}: kind {}, {}x{}, depth {}, speed {}x{}, offset {}x{}, flags 0x{:x}, editor 0x{:x}, renderer {}, color {} {} {} {}, texture {} bytes, first tile {}:{} flags 0x{:x}, name \"{}\"", i, std::int32_t(layer.Record.Kind),
					layer.GetWidth(), layer.GetHeight(), layer.Record.Depth, layer.Record.SpeedX, layer.Record.SpeedY, layer.Record.OffsetX, layer.Record.OffsetY,
					layer.Record.Flags, layer.EditorFlags,
					std::int32_t(layer.Record.RendererType), layer.Record.Color[0], layer.Record.Color[1], layer.Record.Color[2], layer.Record.Color[3],
					std::int32_t(layer.Texture.size()), std::int32_t(first.Source), first.Index, std::int32_t(first.Flags), layer.Name);
			}
			LOGI("Default tile set \"{}\": {} tiles in the document, {} tiles in the game", document.Properties.DefaultTileSet, document.DefaultTileCount,
				_session.GetEditHandler()->TileMap() != nullptr ? _session.GetEditHandler()->TileMap()->GetTileSetTileCount(0) : -1);
			for (std::size_t i = 0; i < document.ExtraTileSets.size(); i++) {
				const Formats::ExtraTileSetRecord& extra = document.ExtraTileSets[i];
				LOGI("Extra tile set {} (source {}): \"{}\", offset {}, count {} ({} in the game), flags 0x{:x}, alternate palette {}", i, i + 1, extra.Name,
					extra.Offset, extra.Count, _session.GetEditHandler()->TileMap() != nullptr ? _session.GetEditHandler()->TileMap()->GetTileSetTileCount(std::int32_t(i) + 1) : -1,
					extra.Flags, extra.AlternatePalette);
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/rebuild"_s, 0, "/rebuild"_s, [this](const Args&) {
			_session.ReloadRuntime();
			return DebugResult::Done;
		} });
		commands.push_back({ "/texts"_s, 0, "/texts"_s, [this](const Args&) {
			// The texts of the level, with the characters that aren't printable as their codes
			const LevelDocument& document = _session.GetDocument();
			for (std::size_t i = 0; i < document.Texts.size(); i++) {
				String escaped;
				for (char c : document.Texts[i]) {
					if (std::uint8_t(c) < 32) {
						escaped += "\\x"_s + String(std::to_string(std::uint8_t(c)).c_str());
					} else {
						escaped += StringView(&c, 1);
					}
				}
				LOGI("Text {}: \"{}\"", i, escaped);
			}
			return DebugResult::Done;
		} });
		// Not through the action of Check Level, that shows the problems in a dialog
		commands.push_back({ "/check"_s, 0, "/check [index]"_s, [this](const Args& args) {
			// The problems Check Level finds, or fixes the one of the index as its first Fix button does
			SmallVector<LevelProblem, 0> problems;
			CheckLevel(_session.GetDocument(), problems);
			CheckLayerTiles(_session, problems);
			CheckLevelSurroundings(_session, problems);
			if (args.size() >= 2) {
				std::int32_t index = std::int32_t(args.Float(1, -1.0f));
				if (index < 0 || index >= std::int32_t(problems.size()) || problems[index].Fix == LevelProblemFix::None) {
					return DebugResult::Failed;
				}
				if (!FixLevelProblem(_session, problems[index].Fix, problems[index].FixValue)) {
					return DebugResult::Failed;
				}
			} else {
				for (std::size_t i = 0; i < problems.size(); i++) {
					const LevelProblem& problem = problems[i];
					LOGI("Problem {} ({}) at {}, {}, off-grid {}, layer {}, fix {}: {}", i, std::int32_t(problem.Severity),
						problem.Cell.X, problem.Cell.Y, problem.OffGrid, problem.Layer, std::int32_t(problem.Fix), problem.Text);
				}
			}
			return DebugResult::Done;
		} });
		commands.push_back({ "/pngtest"_s, 1, "/pngtest <directory>"_s, [](const Args& args) {
			// Every image.png with an image.rgba next to it, the pixels it should decode to, then written and read again
			std::int32_t passed = 0, failed = 0;
			for (auto item : fs::Directory(args[1], fs::EnumerationOptions::SkipDirectories)) {
				if (fs::GetExtension(item) != "png"_s) {
					continue;
				}
				String path = item;
				String expectedPath = path.exceptSuffix(std::size_t(3)) + "rgba"_s;
				auto s = fs::Open(path, FileAccess::Read);
				PngImage image;
				String error;
				if (!PngCodec::Decode(*s, image, &error)) {
					LOGE("PNG test: \"{}\" cannot be read: {}", path, error);
					failed++;
					continue;
				}
				auto matches = [](const PngImage& image, ArrayView<const std::uint8_t> expected) {
					if (expected.size() != std::size_t(image.Width) * std::size_t(image.Height) * 4) {
						return false;
					}
					for (std::int32_t y = 0; y < image.Height; y++) {
						for (std::int32_t x = 0; x < image.Width; x++) {
							std::uint32_t color = image.GetColor(x, y);
							const std::uint8_t* e = &expected[(std::size_t(y) * std::size_t(image.Width) + std::size_t(x)) * 4];
							// Fully transparent pixels can have any color
							if ((color >> 24) == 0 && e[3] == 0) {
								continue;
							}
							if (std::uint8_t(color) != e[0] || std::uint8_t(color >> 8) != e[1] || std::uint8_t(color >> 16) != e[2] || std::uint8_t(color >> 24) != e[3]) {
								return false;
							}
						}
					}
					return true;
				};
				SmallVector<std::uint8_t, 0> expected;
				auto e = fs::Open(expectedPath, FileAccess::Read);
				if (e->IsValid()) {
					expected.resize_for_overwrite(std::size_t(e->GetSize()));
					e->Read(expected.data(), std::int64_t(expected.size()));
				}
				MemoryStream written;
				PngCodec::Encode(written, image);
				{
					// Kept for other decoders to check
					String outDirectory = fs::CombinePath(args[1], "out"_s);
					fs::CreateDirectories(outDirectory);
					auto o = fs::Open(fs::CombinePath(outDirectory, fs::GetFileName(path)), FileAccess::Write);
					o->Write(written.GetBuffer(), written.GetSize());
				}
				written.Seek(0, SeekOrigin::Begin);
				PngImage again;
				bool roundTrip = PngCodec::Decode(written, again) && again.Width == image.Width && again.Height == image.Height &&
					again.Indexed == image.Indexed && again.Pixels.size() == image.Pixels.size() &&
					std::memcmp(again.Pixels.data(), image.Pixels.data(), image.Pixels.size()) == 0;
				bool decoded = (expected.empty() || matches(image, expected));
				if (decoded && roundTrip) {
					LOGI("PNG test: \"{}\" OK ({}x{}, {})", fs::GetFileName(path), image.Width, image.Height, image.Indexed ? "indexed" : "RGBA");
					passed++;
				} else {
					LOGE("PNG test: \"{}\" {}{}", fs::GetFileName(path), decoded ? "" : "decodes to other pixels ", roundTrip ? "" : "doesn't survive writing");
					failed++;
				}
			}
			LOGI("PNG test finished: {} passed, {} failed", passed, failed);
			return DebugResult::Done;
		} });
		commands.push_back({ "/perf"_s, 1, "/perf reset|log [label]"_s, [this](const Args& args) {
			// `/perf reset`, then `/perf log <label>` logs the frame statistics averaged since then
			if (args[1] == "reset"_s) {
				FrameStatistics::SetEnabled(true);
				_perf = Perf();
				_perf.Running = true;
				_perf.Sequence = FrameStatistics::GetSnapshot().Sequence;
			} else if (args[1] == "log"_s) {
				const Perf& p = _perf;
				double frames = std::max<double>(double(p.Frames), 1.0);
				String label = (args.size() >= 3 ? String(args[2]) : String());
				LOGI("Perf {}: {} frames, frame {:.2f} ms (max {:.2f}), begin {:.2f}, update {:.2f}, visit {:.2f}, draw {:.2f}, end {:.2f}, present {:.2f}, wait {:.2f}, draw calls {:.0f}, commands {:.0f}, scale {:.3f}",
					label, p.Frames, p.FrameTime / frames, p.MaxFrameTime, p.PhaseTimes[std::int32_t(FrameStatistics::Phase::BeginFrame)] / frames,
					p.PhaseTimes[std::int32_t(FrameStatistics::Phase::Update)] / frames, p.PhaseTimes[std::int32_t(FrameStatistics::Phase::Visit)] / frames,
					p.PhaseTimes[std::int32_t(FrameStatistics::Phase::Draw)] / frames, p.PhaseTimes[std::int32_t(FrameStatistics::Phase::EndFrame)] / frames,
					p.PhaseTimes[std::int32_t(FrameStatistics::Phase::Present)] / frames, p.PhaseTimes[std::int32_t(FrameStatistics::Phase::Wait)] / frames,
					p.DrawCalls / frames, p.RenderCommands / frames, _session.GetEditHandler()->GetCamera().GetScale());
				for (std::int32_t i = 0; i < 8; i++) {
					if (p.CounterNames[i] != nullptr) {
						LOGI("Perf {}:   {} {:.3f}", label, p.CounterNames[i], p.Counters[i] / frames);
					}
				}
				Tiles::TileMap* tileMap = _session.GetTileMap();
				LOGI("Perf {}:   LOD {}, {} pixels for a tile, {:.1f} MB", label, tileMap != nullptr && tileMap->GetViewOptions().LayerOverride != nullptr ? "on" : "off",
					_tileLod.GetTilePixels(), double(_tileLod.GetMemoryUsage()) / (1024.0 * 1024.0));
			} else {
				return DebugResult::Failed;
			}
			return DebugResult::Done;
		} });
	}

	void EditorDebugConsole::UpdatePerf()
	{
		EditorLevelHandler* editHandler = _session.GetEditHandler();
		if ((_drift.X != 0.0f || _drift.Y != 0.0f || _driftZoom != 1.0f) && editHandler != nullptr) {
			// Back every so many frames, so it stays around where it was
			float direction = ((_driftFrame++ / _driftPeriod) % 2 == 0 ? 1.0f : -1.0f);
			EditorCamera& camera = editHandler->GetCamera();
			camera.PanBy(_drift * direction);
			if (_driftZoom != 1.0f) {
				camera.JumpTo(camera.GetCenter(), camera.GetScale() / std::pow(_driftZoom, direction));
			}
		}
		if (!_perf.Running) {
			return;
		}
		// Each snapshot averages its own frames, so it's added up weighted by them
		const FrameStatistics::Snapshot& snapshot = FrameStatistics::GetSnapshot();
		if (snapshot.Sequence == _perf.Sequence || snapshot.FrameCount == 0) {
			return;
		}
		_perf.Sequence = snapshot.Sequence;
		double frames = double(snapshot.FrameCount);
		_perf.Frames += snapshot.FrameCount;
		_perf.FrameTime += snapshot.FrameTime * frames;
		_perf.MaxFrameTime = std::max(_perf.MaxFrameTime, snapshot.MaxFrameTime);
		for (std::size_t i = 0; i < std::size_t(FrameStatistics::Phase::Count); i++) {
			_perf.PhaseTimes[i] += snapshot.PhaseTimes[i] * frames;
		}
		_perf.DrawCalls += snapshot.DrawCalls * frames;
		_perf.RenderCommands += snapshot.RenderCommands * frames;
		// By name, the order they first reported in can change
		for (std::uint32_t i = 0; i < snapshot.CounterCount; i++) {
			for (std::int32_t j = 0; j < 8; j++) {
				if (_perf.CounterNames[j] == nullptr) {
					_perf.CounterNames[j] = snapshot.Counters[i].Name;
				}
				if (std::strcmp(_perf.CounterNames[j], snapshot.Counters[i].Name) == 0) {
					_perf.Counters[j] += snapshot.Counters[i].Value * frames;
					break;
				}
			}
		}
	}
}

#endif
