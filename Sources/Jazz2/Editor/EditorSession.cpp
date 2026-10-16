#include "EditorSession.h"

#if defined(WITH_EDITOR)

#include "Document/Commands.h"
#include "Document/DocumentViewState.h"
#include "Imaging/AccentColor.h"
#include "Imaging/PngCodec.h"
#include "Imaging/TileImages.h"
#include "Runtime/EditorLevelHandler.h"
#include "Runtime/RuntimeSync.h"
#include "Shell/EditorShell.h"
#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include "Files/WebFileBridge.h"
#endif
#include "Shell/LayerDialogs.h"
#include "Shell/LevelDialogs.h"
#include "Imaging/LevelThumbnail.h"
#if defined(WITH_MULTIPLAYER)
#	include "../Multiplayer/RaceRouteGenerator.h"
#endif
#include "Shell/FindReplaceDialog.h"
#include "Shell/FxTileGenerator.h"
#include "Shell/HelpDialogs.h"
#include "Shell/LevelCheck.h"
#include "Shell/LevelStatistics.h"
#include "Shell/Navigator.h"
#include "Shell/PrefabsDialog.h"
#include "Shell/SmartFill.h"
#include "Shell/TerrainRulesDialog.h"
#include "Smart/TilePrefabs.h"
#include "Shell/PaletteDialog.h"
#include "Shell/TileEditor.h"
#include "Shell/TileSetApplyDialog.h"
#include "Shell/TileSetLayoutDialog.h"
#include "Shell/TileSetImportDialog.h"
#include "Shell/TileSetsDialog.h"
#include "Tools/GroundLineTool.h"
#include "Tools/TileTools.h"
#include "UI/UiDialogs.h"
#include "UI/UiKeyboard.h"
#include "UI/UiRadialMenu.h"
#include "UI/UiSurface.h"
#include "../ContentResolver.h"
#include "../Tiles/TileCollisionParams.h"
#include "../Formats/TileSetFile.h"
#include "../Actors/Player.h"
#include "../Events/EventMap.h"
#include "../Rendering/PlayerViewport.h"
#include "../PreferencesCache.h"
#include "../Tiles/TileMap.h"

#include "../../nCine/Application.h"
#include "../../nCine/I18n.h"
#include "../../nCine/Graphics/IGfxDevice.h"
#include "../../nCine/Graphics/Viewport.h"
#include "../../nCine/Input/IInputManager.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

using namespace Death::IO;

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// A level exported only to be played has a flag the editor doesn't open it with
		// Why a level can't be opened in the editor, read from the start of its file, or an empty string if it can
		String GetOpenProblem(StringView path)
		{
			auto s = fs::Open(path, FileAccess::Read);
			Formats::LevelHeader header;
			if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
				return _("The file isn't a level of this version of the game");
			}
			// A level exported only to be played has a flag the editor doesn't open it with
			if ((header.Flags & LevelFlags::NotEditable) == LevelFlags::NotEditable) {
				return _("The level was exported only to be played, it can't be edited");
			}
			Formats::LevelReader reader(*s, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			if (ContentTileSetLibrary::FindTileSetFile(properties.DefaultTileSet).empty()) {
				return _f("The tile set \"{}\" of the level wasn't found", properties.DefaultTileSet);
			}
			return {};
		}

#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		constexpr std::size_t HistoryMemoryBudget = 8 * 1024 * 1024;
#else
		constexpr std::size_t HistoryMemoryBudget = 64 * 1024 * 1024;
#endif
		// Part of the view the arrow keys move it by
		constexpr float KeyboardPanFraction = 0.25f;
		// How much the music is muffled while the level is edited, 1 for not at all
		constexpr float EditMusicLowPass = EditorLevelHandler::EditMusicLowPass;
		// How long the music takes to open up or to be muffled again, in frames at 60 FPS
		constexpr float MusicFadeFrames = 45.0f;
		// Part of the view a step of the wheel moves it by
		constexpr float WheelPanFraction = 0.12f;
		// Units a frame the cursor of a gamepad moves by with the stick pushed all the way, and the view, then faster
		// the longer the stick is held, up to the boost
		constexpr float GamepadCursorSpeed = 8.0f;
		constexpr float GamepadPanSpeed = 12.0f;
		constexpr float GamepadMaxBoost = 2.0f;
		// Steps of the wheel a frame the right stick scrolls a list by, pushed all the way
		constexpr float GamepadScrollSpeed = 0.15f;
		constexpr float GamepadBoostFrames = 60.0f;
		// How much the zoom changes a frame with a trigger pulled all the way, as a logarithm, about 3× a second
		constexpr float GamepadZoomRate = 0.02f;
		// Prefix of a stamp in the clipboard of the system
		constexpr StringView ClipboardPrefix = "{\"J2R\":"_s;

		String StampToClipboardText(const TileStamp& stamp)
		{
			String text = ClipboardPrefix + "1,\"w\":"_s + String(std::to_string(stamp.Width).c_str()) + ",\"h\":"_s +
				String(std::to_string(stamp.Height).c_str()) + ",\"t\":["_s;
			for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
				const TileRef& tile = stamp.Tiles[i];
				char buffer[48];
				std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%s%d,%d,%d", i > 0 ? "," : "",
					stamp.Present[i] ? std::int32_t(tile.Source) : -1, std::int32_t(tile.Index), std::int32_t(tile.Flags));
				text += StringView(buffer, std::size_t(length));
			}
			text += "]}"_s;
			return text;
		}

		bool ClipboardTextToStamp(StringView text, TileStamp& stamp)
		{
			if (!text.hasPrefix(ClipboardPrefix)) {
				return false;
			}
			auto readNumber = [&text](StringView key, std::int32_t& value) {
				StringView found = text.find(key);
				if (found.empty()) {
					return false;
				}
				String rest = text.suffix(found.end());
				value = std::int32_t(std::strtol(rest.data(), nullptr, 10));
				return true;
			};
			std::int32_t width, height;
			if (!readNumber("\"w\":"_s, width) || !readNumber("\"h\":"_s, height) || width <= 0 || height <= 0 || width * height > (1 << 20)) {
				return false;
			}
			StringView found = text.find("\"t\":["_s);
			if (found.empty()) {
				return false;
			}
			String values = text.suffix(found.end());
			const char* p = values.data();
			stamp = TileStamp();
			stamp.Width = width;
			stamp.Height = height;
			for (std::int32_t i = 0; i < width * height; i++) {
				char* end;
				std::int32_t source = std::int32_t(std::strtol(p, &end, 10)); p = (*end == ',' ? end + 1 : end);
				std::int32_t index = std::int32_t(std::strtol(p, &end, 10)); p = (*end == ',' ? end + 1 : end);
				std::int32_t flags = std::int32_t(std::strtol(p, &end, 10)); p = (*end == ',' ? end + 1 : end);
				TileRef tile{ std::uint16_t(index), std::uint8_t(source < 0 ? 0 : source), std::uint8_t(flags) };
				stamp.Tiles.push_back(tile);
				stamp.Present.push_back(source >= 0);
			}
			return true;
		}

		// Events as numbers, each an offset, the type, the flags, the generator and the parameters
		constexpr std::int32_t ClipboardValuesPerEvent = 6 + Formats::LevelFile::EventParamsSize;

		String EventStampToClipboardText(const EventStamp& stamp)
		{
			String text = ClipboardPrefix + "1,\"events\":["_s;
			for (std::size_t i = 0; i < stamp.Items.size(); i++) {
				const EventStamp::Item& item = stamp.Items[i];
				const Formats::EventRecord& event = item.Event;
				char buffer[256];
				std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%s%d,%d,%u,%u,%u,%u", i > 0 ? "," : "",
					item.Offset.X, item.Offset.Y, std::uint32_t(event.Type), std::uint32_t(event.Flags),
					std::uint32_t(event.GeneratorFlags), std::uint32_t(event.GeneratorDelay));
				for (std::int32_t j = 0; j < Formats::LevelFile::EventParamsSize; j++) {
					length += std::snprintf(buffer + length, sizeof(buffer) - length, ",%u", std::uint32_t(event.Params[j]));
				}
				text += StringView(buffer, std::size_t(length));
			}
			text += "]}"_s;
			return text;
		}

		bool ClipboardTextToEventStamp(StringView text, EventStamp& stamp)
		{
			if (!text.hasPrefix(ClipboardPrefix)) {
				return false;
			}
			StringView found = text.find("\"events\":["_s);
			if (found.empty()) {
				return false;
			}
			String values = text.suffix(found.end());
			const char* p = values.data();
			stamp = EventStamp();
			while (*p != '\0' && *p != ']') {
				std::int32_t numbers[ClipboardValuesPerEvent];
				for (std::int32_t i = 0; i < ClipboardValuesPerEvent; i++) {
					char* end;
					numbers[i] = std::int32_t(std::strtol(p, &end, 10));
					if (end == p) {
						return false;
					}
					p = (*end == ',' ? end + 1 : end);
				}
				EventStamp::Item& item = stamp.Items.emplace_back();
				item.Offset = Vector2i(numbers[0], numbers[1]);
				item.Event.Type = std::uint16_t(numbers[2]);
				item.Event.Flags = std::uint8_t(numbers[3]);
				item.Event.GeneratorFlags = std::uint8_t(numbers[4]);
				item.Event.GeneratorDelay = std::uint8_t(numbers[5]);
				for (std::int32_t j = 0; j < Formats::LevelFile::EventParamsSize; j++) {
					item.Event.Params[j] = std::uint8_t(numbers[6 + j]);
				}
				if (stamp.Items.size() > (1 << 16)) {
					return false;
				}
			}
			return !stamp.IsEmpty();
		}
	}

	EditorSession::EditorSession(IRootController* root)
		: _root(root), _shell(nullptr), _mode(SessionMode::Edit), _playRevision(0), _resumePlay(false), _editCameraScale(1.0f),
			_showPlayPath(true), _currentLayer(0), _showGrid(true), _pointerKnown(false), _panning(false),
			_toolPointerDown(false), _toolButton(MouseButton::Left), _modifiers(0), _maskMode(0), _showEvents(true), _pickerPanel(PickerPanel::Tiles)
	{
		_tileLod = std::make_unique<TileLod>(*this);
	}

	EditorSession::~EditorSession()
	{
		_settings.Save();
		// The interface refers to the session, so it goes first
		_surface = nullptr;
		Viewport::GetChain().clear();
		_playHandler = nullptr;
		_editHandler = nullptr;
	}

	bool EditorSession::Initialize(const EditorInitialization& editorInit)
	{
		auto& resolver = ContentResolver::Get();
		_settings.Load();
		_showGrid = _settings.ShowGrid;

		String fullPath;
		if (!editorInit.FilePath.empty()) {
			fullPath = editorInit.FilePath;
			String fileName = fs::GetFileNameWithoutExtension(fullPath);
			StringUtils::lowercaseInPlace(fileName);
			_levelName = "unknown/"_s + fileName;
		} else if (!editorInit.LevelName.empty()) {
			_levelName = editorInit.LevelName;
			fullPath = resolver.FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(_levelName) + ".j2l"_s)));
			if (fullPath.empty()) {
				auto p = _levelName.partition('/');
				if (p[0] != "unknown"_s && !p[2].empty()) {
					_levelName = "unknown/"_s + p[2];
					fullPath = resolver.FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(_levelName) + ".j2l"_s)));
				}
			}
		}

		bool showOpenDialog = false;
		if (fullPath.empty()) {
			if (!editorInit.FilePath.empty() || !editorInit.LevelName.empty()) {
				LOGE("Level to edit was not found");
				return false;
			}
			// Nothing to open, so a new level, and the dialog to open one is shown over it
			String tileSet = (!editorInit.NewLevelTileSet.empty() ? String(editorInit.NewLevelTileSet) : FindDefaultTileSet());
			std::int32_t tileCount = _tileSets.GetTileCount(tileSet);
			if (tileCount < 0) {
				LOGE("No tile set was found to make a new level with");
				return false;
			}
			_document.CreateNew(tileSet, tileCount, Vector2i(128, 64));
			_document.Properties.Author = PreferencesCache::GetEffectivePlayerName();
			_levelName = "unknown/untitled"_s;
			showOpenDialog = true;
		} else {
			auto s = fs::Open(fullPath, FileAccess::Read);
			if (!s->IsValid() || !_document.Load(*s, &_tileSets)) {
				LOGE("Level \"{}\" cannot be opened in the editor", fullPath);
				return false;
			}
			if ((_document.Flags & LevelFlags::NotEditable) == LevelFlags::NotEditable) {
				LOGE("Level \"{}\" was exported only to be played, it cannot be opened in the editor", fullPath);
				return false;
			}
			if (_tileSets.GetTileCount(_document.Properties.DefaultTileSet) < 0) {
				LOGE("Tile set \"{}\" of level \"{}\" was not found", _document.Properties.DefaultTileSet, fullPath);
				return false;
			}
			s = nullptr;
			_filePath = fullPath;
		}

		String problem;
		if (!_document.Validate(&problem)) {
			LOGW("Level \"{}\" has a problem: {}", fullPath, problem);
		}

		_history = std::make_unique<CommandHistory>(_document, HistoryMemoryBudget);
		_tools = std::make_unique<ToolManager>(*this);
		_currentLayer = std::max(_document.GetSpriteLayerIndex(), 0);
		_stamp = TileStamp::FromTile(TileRef{ 1, TileRef::DefaultTileSet, 0 });
		LoadEventExtensions(true);

		_editHandler = std::make_shared<EditorLevelHandler>(this);
		if (!_editHandler->Initialize(_document, _levelName, _filePath)) {
			_editHandler = nullptr;
			return false;
		}
		AttachSceneContent();
		RememberAppliedOverrides();
		ApplyViewState();
		_autosave.BeforeSave = [this]() { StoreViewState(); };

		CreateActions();
		_actions.CaptureDefaults();
		_actions.LoadOverrides(GetHotkeysPath());
		CreateInterface();
		SetupTouchGestures();

		LOGI("Level \"{}\" opened in the editor", _filePath);
		UpdateWindowTitle();

		_autosave.SetSource(_filePath);
		if (!_filePath.empty()) {
			_settings.AddRecentFile(_filePath);
			_settings.Save();
		}
		// Changes that weren't saved last time come first
		if (!OfferRecovery() && showOpenDialog) {
			ShowOpenDialog();
		}

#if defined(DEATH_DEBUG)
		const AppConfiguration& config = theApplication().GetAppConfiguration();
		for (std::int32_t i = 0; i + 1 < config.argc(); i++) {
			if (config.argv(i) == "/console"_s) {
				_debugStartupCommands.emplace_back(config.argv(i + 1));
				i++;
			}
		}
#endif
		return true;
	}

	void EditorSession::CreateActions()
	{
		auto& a = _actions;

		// File
		a.Add("file.new"_s, _("New Level..."), EditorIcon::New, [this]() {
			ConfirmDiscard([this]() { ShowNewLevelDialog(); });
		}).Key = { Keys::N, KeyMod::Ctrl };
		a.Add("file.open"_s, _("Open..."), EditorIcon::Folder, [this]() {
			ConfirmDiscard([this]() { ShowOpenDialog(); });
		}).Key = { Keys::O, KeyMod::Ctrl };
		a.Add("file.save"_s, _("Save"), EditorIcon::Save, [this]() { Save(); }).Key = { Keys::S, KeyMod::Ctrl };
		a.Add("file.saveAs"_s, _("Save As..."), EditorIcon::Save, [this]() { ShowSaveAsDialog(false); }).Key = { Keys::S, KeyMod::Ctrl | KeyMod::Shift };
		a.Add("file.export"_s, _("Export..."), EditorIcon::Export, [this]() { ShowExportDialog(); }).Key = { Keys::E, KeyMod::Ctrl };
		a.Add("file.exit"_s, _("Exit Editor"), EditorIcon::Close, [this]() { ExitEditor(); });

		// Edit
		{
			auto& undo = a.Add("edit.undo"_s, _("Undo"), EditorIcon::Undo, [this]() { Undo(); });
			undo.Key = { Keys::Z, KeyMod::Ctrl };
			undo.IsEnabled = [this]() { return _history->CanUndo(); };
			auto& redo = a.Add("edit.redo"_s, _("Redo"), EditorIcon::Redo, [this]() { Redo(); });
			redo.Key = { Keys::Y, KeyMod::Ctrl };
			redo.AlternateKey = { Keys::Z, KeyMod::Ctrl | KeyMod::Shift };
			redo.IsEnabled = [this]() { return _history->CanRedo(); };
		}
		auto hasSelection = [this]() { return HasSelection(); };
		a.Add("edit.cut"_s, _("Cut"), EditorIcon::Cut, [this]() { CopySelection(true); }).Key = { Keys::X, KeyMod::Ctrl };
		a.Find("edit.cut"_s)->IsEnabled = hasSelection;
		a.Add("edit.copy"_s, _("Copy"), EditorIcon::Copy, [this]() { CopySelection(false); }).Key = { Keys::C, KeyMod::Ctrl };
		a.Find("edit.copy"_s)->IsEnabled = hasSelection;
		a.Add("edit.paste"_s, _("Paste"), EditorIcon::Paste, [this]() { Paste(); }).Key = { Keys::V, KeyMod::Ctrl };
		a.Add("edit.delete"_s, _("Delete"), EditorIcon::Delete, [this]() { DeleteSelection(); }).Key = { Keys::Delete, 0 };
		a.Find("edit.delete"_s)->IsEnabled = hasSelection;
		a.Add("edit.selectAll"_s, _("Select All"), EditorIcon::Select, [this]() { SelectAll(); }).Key = { Keys::A, KeyMod::Ctrl };
		a.Add("edit.deselect"_s, _("Deselect"), EditorIcon::None, [this]() { ClearSelection(); }).Key = { Keys::D, KeyMod::Ctrl };
		a.Add("edit.history"_s, _("History..."), EditorIcon::Undo, [this]() { ShowHistoryDialog(*this); }).Key = { Keys::H, KeyMod::Ctrl };
		a.Add("edit.findReplace"_s, _("Find and Replace..."), EditorIcon::Search, [this]() { ShowFindReplaceDialog(*this); }).Key = { Keys::F, KeyMod::Ctrl };
		a.Add("edit.prefabs"_s, _("Prefabs..."), EditorIcon::Tiles, [this]() { ShowPrefabsDialog(*this); });
		a.Add("edit.fillGround"_s, _("Fill Selection with Ground"), EditorIcon::Fill, [this]() { FillSelectionWithGround(*this); });
		a.Add("edit.beautify"_s, _("Beautify Selection"), EditorIcon::SmartBrush, [this]() { BeautifySelection(*this); });
		a.Add("edit.bakeGround"_s, _("Make Tiles Along Ground Line"), EditorIcon::Slope, [this]() { BakeGroundLine(); });
		a.Add("edit.continueRow"_s, _("Continue Row by Suggestions"), EditorIcon::SmartBrush, [this]() { ContinueRow(); }).Key = { Keys::Right, KeyMod::Ctrl | KeyMod::Shift };
		a.Add("edit.terrainRules"_s, _("Terrain Rules..."), EditorIcon::SmartBrush, [this]() { ShowTerrainRulesDialog(*this); });
		a.Add("edit.smartRetile"_s, _("Choose Tiles by Their Neighbors"), EditorIcon::SmartBrush, [this]() { RetileSelection(); });
		a.Add("edit.traceGround"_s, _("Make Ground Line from Selection"), EditorIcon::Slope, [this]() {
			if (GroundLineTool::TraceSelection(*this)) {
				_tools->SetActive(EditorToolType::Ground);
			}
		});
		a.Add("help.commands"_s, _("Command Palette..."), EditorIcon::None, [this]() { ShowCommandPalette(*this); }).Key = { Keys::K, KeyMod::Ctrl };
		a.Add("help.shortcuts"_s, _("Keyboard Shortcuts..."), EditorIcon::None, [this]() { ShowShortcutsDialog(*this); }).Key = { Keys::F1, 0 };
		a.Add("view.navigator"_s, _("Navigator..."), EditorIcon::Map, [this]() { ShowNavigator(*this); }).Key = { Keys::F2, 0 };
		a.Add("view.addNote"_s, _("Add Note"), EditorIcon::Plus, [this]() {
			// Where the pointer is over the level, otherwise in the middle of the view
			ToolPointer hover;
			Vector2f pos = (GetHoverPointer(hover) ? hover.World : (_editHandler != nullptr ? _editHandler->GetCamera().GetCenter() : Vector2f()));
			AddNote(*this, pos);
			ShowNavigator(*this);
		}).Key = { Keys::N, KeyMod::Ctrl | KeyMod::Shift };
		for (std::int32_t slot = 1; slot <= 9; slot++) {
			Keys key = Keys(std::int32_t(Keys::D1) + slot - 1);
			a.Add(String("bookmark.set"_s + String(std::to_string(slot).c_str())), _f("Set Bookmark {}", slot), EditorIcon::None,
				[this, slot]() { SetBookmark(*this, slot); }).Key = { key, KeyMod::Ctrl | KeyMod::Shift };
			a.Add(String("bookmark.go"_s + String(std::to_string(slot).c_str())), _f("Go to Bookmark {}", slot), EditorIcon::None,
				[this, slot]() { GoToBookmark(*this, slot); }).Key = { key, KeyMod::Ctrl };
		}
		{
			auto& sameType = a.Add("events.selectSameType"_s, _("Select Events of the Same Kind"), EditorIcon::Event, [this]() { SelectEventsOfSameType(); });
			sameType.IsEnabled = [this]() { return IsEditingEvents() && !_eventSelection.IsEmpty(); };
			auto& matching = a.Add("events.copyToMatchingTiles"_s, _("Copy Event to Every Tile Like Its Own"), EditorIcon::Event, [this]() { CopyEventToMatchingTiles(); });
			matching.IsEnabled = [this]() { return IsEditingEvents() && _eventSelection.GetCount() == 1; };
		}
		a.Add("stamp.flipX"_s, _("Flip Stamp Horizontally"), EditorIcon::FlipX, [this]() { _stamp.FlipX(); }).Key = { Keys::X, 0 };
		a.Add("stamp.flipY"_s, _("Flip Stamp Vertically"), EditorIcon::FlipY, [this]() { _stamp.FlipY(); }).Key = { Keys::Y, 0 };
		{
			auto& random = a.Add("stamp.random"_s, _("Paint Tiles of Stamp at Random"), EditorIcon::None, [this]() { _stamp.Random = !_stamp.Random; });
			random.Key = { Keys::Z, 0 };
			random.IsChecked = [this]() { return _stamp.Random; };
			auto& reroll = a.Add("stamp.reroll"_s, _("Choose Other Tiles at Random"), EditorIcon::None, [this]() { _stamp.Seed++; });
			reroll.Key = { Keys::Z, KeyMod::Shift };
			reroll.IsEnabled = [this]() { return _stamp.Random; };
		}

		// Tools
		struct ToolInfo { const char* Id; const char* Label; EditorIcon Icon; Keys Key; EditorToolType Type; };
		static const ToolInfo Tools[] = {
			{ "tool.brush", "Brush", EditorIcon::Brush, Keys::B, EditorToolType::Brush },
			{ "tool.eraser", "Eraser", EditorIcon::Eraser, Keys::E, EditorToolType::Eraser },
			{ "tool.rectangle", "Rectangle", EditorIcon::Rectangle, Keys::R, EditorToolType::Rectangle },
			{ "tool.fill", "Fill", EditorIcon::Fill, Keys::G, EditorToolType::Fill },
			{ "tool.eyedropper", "Eyedropper", EditorIcon::Eyedropper, Keys::I, EditorToolType::Eyedropper },
			{ "tool.select", "Select", EditorIcon::Select, Keys::M, EditorToolType::Select },
			{ "tool.wand", "Magic Wand", EditorIcon::Wand, Keys::W, EditorToolType::Wand },
			{ "tool.hand", "Hand", EditorIcon::Hand, Keys::H, EditorToolType::Hand },
			{ "tool.event", "Events", EditorIcon::Event, Keys::Q, EditorToolType::Event },
			{ "tool.measure", "Measure", EditorIcon::Measure, Keys::U, EditorToolType::Measure },
			{ "tool.path", "Events Along Path", EditorIcon::EventPath, Keys::K, EditorToolType::Path },
			{ "tool.smart", "Smart Brush", EditorIcon::SmartBrush, Keys::T, EditorToolType::Smart },
			{ "tool.ground", "Ground Line", EditorIcon::Slope, Keys::P, EditorToolType::Ground },
		};
		for (const ToolInfo& info : Tools) {
			EditorToolType type = info.Type;
			auto& action = a.Add(info.Id, _(info.Label), info.Icon, [this, type]() {
				_tools->SetActive(type);
				// The panel at the bottom offers what the tool uses
				if (type == EditorToolType::Event || type == EditorToolType::Path) {
					_pickerPanel = PickerPanel::Events;
				} else if (type == EditorToolType::Brush || type == EditorToolType::Rectangle || type == EditorToolType::Fill || type == EditorToolType::Eyedropper) {
					_pickerPanel = PickerPanel::Tiles;
				}
			});
			action.Key = { info.Key, 0 };
			action.IsChecked = [this, type]() { return (_tools->GetActiveType() == type); };
		}

		// View
		{
			auto& grid = a.Add("view.grid"_s, _("Show Grid"), EditorIcon::Grid, [this]() {
				_showGrid = !_showGrid;
				_settings.ShowGrid = _showGrid;
				_settings.Save();
			});
			grid.Key = { Keys::G, KeyMod::Ctrl };
			grid.IsChecked = [this]() { return _showGrid; };

			auto& flat = a.Add("view.flat"_s, _("Flat Layers"), EditorIcon::Parallax, [this]() {
				if (Tiles::TileMap* tileMap = GetTileMap()) {
					Tiles::TileMapViewOptions options = tileMap->GetViewOptions();
					options.FlatAmount = (options.FlatAmount > 0.5f ? 0.0f : 1.0f);
					tileMap->SetViewOptions(options);
				}
			});
			flat.Key = { Keys::P, KeyMod::Ctrl };

			auto& masks = a.Add("view.masks"_s, _("Show Masks"), EditorIcon::Mask, [this]() {
				// Over the tiles, instead of them, not at all
				_maskMode = (_maskMode + 1) % 3;
			});
			masks.Key = { Keys::M, KeyMod::Ctrl };
			masks.IsChecked = [this]() { return (_maskMode != 0); };

			auto& events = a.Add("view.events"_s, _("Show Events"), EditorIcon::Event, [this]() { _showEvents = !_showEvents; });
			events.Key = { Keys::E, KeyMod::Ctrl | KeyMod::Shift };
			events.IsChecked = [this]() { return _showEvents; };

			// The layer being edited always stays still, this keeps all of them still
			auto& pauseLayers = a.Add("view.pauseLayers"_s, _("Pause Layer Movement"), EditorIcon::Pause, [this]() {
				_settings.PauseLayerMovement = !_settings.PauseLayerMovement;
				_settings.Save();
			});
			pauseLayers.Key = { Keys::P, KeyMod::Ctrl | KeyMod::Shift };
			pauseLayers.IsChecked = [this]() { return _settings.PauseLayerMovement; };
			flat.IsChecked = [this]() {
				Tiles::TileMap* tileMap = GetTileMap();
				return (tileMap != nullptr && tileMap->GetViewOptions().FlatAmount > 0.5f);
			};
		}
		a.Add("view.zoomIn"_s, _("Zoom In"), EditorIcon::ZoomIn, [this]() {
			Rectf view = GetViewRect();
			_editHandler->GetCamera().ZoomSteps(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), 1.0f);
		}).Key = { Keys::Plus, 0 };
		a.Add("view.zoomOut"_s, _("Zoom Out"), EditorIcon::ZoomOut, [this]() {
			Rectf view = GetViewRect();
			_editHandler->GetCamera().ZoomSteps(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), -1.0f);
		}).Key = { Keys::Minus, 0 };
		a.Add("view.zoomGame"_s, _("Zoom as in Game"), EditorIcon::None, [this]() {
			EditorCamera& camera = _editHandler->GetCamera();
			Rectf view = GetViewRect();
			camera.ZoomTo(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), camera.SnapScale(camera.GetGameplayScale(), 0));
		}).Key = { Keys::D0, 0 };
		a.Add("view.fit"_s, _("Show Whole Level"), EditorIcon::Fit, [this]() {
			if (Tiles::TileMap* tileMap = GetTileMap()) {
				Vector2i levelBounds = tileMap->GetLevelBounds();
				_editHandler->GetCamera().Fit(Rectf(0.0f, 0.0f, float(levelBounds.X), float(levelBounds.Y)), true);
			}
		}).Key = { Keys::Home, 0 };

		// Size of the interface, in steps that keep it sharp where they can
		a.Add("view.uiLarger"_s, _("Larger Interface"), EditorIcon::None, [this]() { ChangeUiScale(1); }).Key = { Keys::Plus, KeyMod::Ctrl | KeyMod::Shift };
		a.Add("view.uiSmaller"_s, _("Smaller Interface"), EditorIcon::None, [this]() { ChangeUiScale(-1); }).Key = { Keys::Minus, KeyMod::Ctrl | KeyMod::Shift };
		a.Add("view.uiAuto"_s, _("Interface Size by Window"), EditorIcon::None, [this]() { ChangeUiScale(0); }).Key = { Keys::D0, KeyMod::Ctrl | KeyMod::Shift };

		// Layout for the screen, and how much is offered
		struct LayoutChoice { const char* Id; const char* Label; std::int32_t Value; };
		static const LayoutChoice Layouts[] = {
			{ "view.layoutAuto", N_("Layout by Window Size"), 0 },
			{ "view.layoutDesktop", N_("Desktop Layout"), 1 },
			{ "view.layoutCompact", N_("Compact Layout, Panels in Drawers"), 2 },
		};
		for (const LayoutChoice& choice : Layouts) {
			std::int32_t value = choice.Value;
			auto& layout = a.Add(choice.Id, _(choice.Label), EditorIcon::None, [this, value]() {
				_settings.Layout = value;
				_settings.Save();
			});
			layout.IsChecked = [this, value]() { return (_settings.Layout == value); };
		}
		auto& expert = a.Add("view.expert"_s, _("Expert Mode"), EditorIcon::Settings, [this]() {
			_settings.Expert = !_settings.Expert;
			_settings.Save();
			ShowMessage(_settings.Expert ? _("All tools and commands are offered") : _("Only the simple tools and commands are offered"));
		});
		expert.IsChecked = [this]() { return _settings.Expert; };
		auto& drawerTiles = a.Add("view.drawerTiles"_s, _("Tiles and Events"), EditorIcon::Tiles, [this]() {
			if (_shell != nullptr) {
				_shell->ToggleDrawer(true);
			}
		});
		drawerTiles.IsChecked = [this]() { return (_shell != nullptr && _shell->IsDrawerOpen(true)); };
		auto& drawerLayers = a.Add("view.drawerLayers"_s, _("Layers and Properties"), EditorIcon::Layers, [this]() {
			if (_shell != nullptr) {
				_shell->ToggleDrawer(false);
			}
		});
		drawerLayers.IsChecked = [this]() { return (_shell != nullptr && _shell->IsDrawerOpen(false)); };

		// Layers
		a.Add("layer.add"_s, _("Add Layer"), EditorIcon::Plus, [this]() { AddLayer(); });
		{
			auto& remove = a.Add("layer.remove"_s, _("Remove Layer"), EditorIcon::Delete, [this]() { RemoveLayer(); });
			remove.IsEnabled = [this]() {
				return (_currentLayer >= 0 && _currentLayer < std::int32_t(_document.Layers.size()) && !_document.Layers[_currentLayer].IsSpriteLayer());
			};
			auto& up = a.Add("layer.up"_s, _("Move Layer Up"), EditorIcon::ArrowUp, [this]() { MoveLayer(-1); });
			up.IsEnabled = [this]() { return (_currentLayer > 0); };
			auto& down = a.Add("layer.down"_s, _("Move Layer Down"), EditorIcon::ArrowDown, [this]() { MoveLayer(1); });
			down.IsEnabled = [this]() { return (_currentLayer + 1 < std::int32_t(_document.Layers.size())); };
			auto& resize = a.Add("layer.resize"_s, _("Resize Layer..."), EditorIcon::Fit, [this]() { ShowResizeLayerDialog(*this); });
			resize.IsEnabled = [this]() { return (_currentLayer >= 0 && _currentLayer < std::int32_t(_document.Layers.size())); };
		}
		a.Add("level.properties"_s, _("Level Properties..."), EditorIcon::Settings, [this]() { ShowLevelPropertiesDialog(*this); });
		a.Add("level.texts"_s, _("Level Texts..."), EditorIcon::None, [this]() { ShowLevelTextsDialog(*this); }).Key = { Keys::T, KeyMod::Ctrl };
		a.Add("level.statistics"_s, _("Statistics..."), EditorIcon::None, [this]() { ShowLevelStatisticsDialog(*this); });
		a.Add("level.check"_s, _("Check Level..."), EditorIcon::None, [this]() { ShowCheckLevelDialog(*this); });
		{
			auto& snap = a.Add("level.snapOffGrid"_s, _("Snap Off-Grid Events to Grid"), EditorIcon::None, [this]() { SnapOffGridEvents(); });
			snap.IsEnabled = [this]() { return !_document.OffGridEvents.empty(); };
		}
		a.Add("level.animations"_s, _("Animated Tiles..."), EditorIcon::Tiles, [this]() { ShowAnimatedTilesDialog(*this); });
		a.Add("level.tilesets"_s, _("Tile Sets..."), EditorIcon::None, [this]() { ShowTileSetsDialog(*this); });
		a.Add("level.palette"_s, _("Palette..."), EditorIcon::None, [this]() { ShowLevelPaletteDialog(*this); });
		{
			// The first tile of the brush, a tile of the tile picker is opened by a double click on it too
			auto& editTile = a.Add("level.editTile"_s, _("Edit Tile of the Brush..."), EditorIcon::Brush, [this]() {
				if (!_stamp.IsEmpty()) {
					ShowTileEditor(*this, _stamp.Tiles[0]);
				}
			});
			editTile.IsEnabled = [this]() { return (!_stamp.IsEmpty() && !_stamp.Tiles[0].IsEmpty()); };
		}
		a.Add("level.exportTileSet"_s, _("Export Tile Set Image..."), EditorIcon::Export, [this]() { ShowExportTileSetImageDialog(); });
		a.Add("level.newTileSet"_s, _("New Tile Set..."), EditorIcon::New, [this]() { ShowNewTileSetDialog(*this); });
		a.Add("level.importTileSet"_s, _("New Tile Set from Image..."), EditorIcon::New, [this]() { ShowImportTileSetDialog(*this); });
		a.Add("level.addTiles"_s, _("Add Tiles from Image..."), EditorIcon::Plus, [this]() { ShowAddTilesFromImageDialog(*this); });
		a.Add("level.fxTiles"_s, _("Generate Effect Tiles..."), EditorIcon::Plus, [this]() { ShowFxTileGeneratorDialog(*this); });
		a.Add("level.applyTileChanges"_s, _("Save Tile Changes into Tile Set..."), EditorIcon::Save, [this]() { ShowApplyTileChangesDialog(*this); });
		a.Add("level.rearrangeTileSet"_s, _("Rearrange Tile Set..."), EditorIcon::Tiles, [this]() { ShowTileSetLayoutDialog(*this); });
		a.Add("level.reloadEvents"_s, _("Read Event Descriptions Again"), EditorIcon::Event, [this]() {
			LoadEventExtensions(true);
			ShowMessage(_("Event packs and the script of the level were read again"));
		});
		a.Add("layer.previous"_s, _("Previous Layer"), EditorIcon::None, [this]() { SetCurrentLayer(_currentLayer - 1); }).Key = { Keys::PageUp, 0 };
		a.Add("layer.next"_s, _("Next Layer"), EditorIcon::None, [this]() { SetCurrentLayer(_currentLayer + 1); }).Key = { Keys::PageDown, 0 };

		// Playing
		{
			auto isEditing = [this]() { return (_mode == SessionMode::Edit); };
			auto& play = a.Add("play.resume"_s, _("Play"), EditorIcon::Play, [this]() { StartPlayTest(PlayStartMode::Resume); });
			play.Key = { Keys::F5, 0 };
			play.IsEnabled = isEditing;
			auto& here = a.Add("play.here"_s, _("Play Here"), EditorIcon::Play, [this]() { StartPlayTest(PlayStartMode::Here); });
			here.Key = { Keys::F5, KeyMod::Shift };
			here.IsEnabled = isEditing;
			auto& start = a.Add("play.start"_s, _("Play from Start"), EditorIcon::Play, [this]() { StartPlayTest(PlayStartMode::FromStart); });
			start.Key = { Keys::F5, KeyMod::Ctrl };
			start.IsEnabled = isEditing;
			auto& marker = a.Add("play.marker"_s, _("Play from Marker"), EditorIcon::Play, [this]() { StartPlayTest(PlayStartMode::Marker); });
			marker.Key = { Keys::F5, KeyMod::Alt };
			marker.IsEnabled = [this]() { return (_mode == SessionMode::Edit && _hasPlayStart); };
			// Where play-tests start from again and again, kept in the level
			a.Add("play.setMarker"_s, _("Set Play Start Marker Here"), EditorIcon::None, [this]() {
				ToolPointer pointer;
				Vector2f pos;
				if (_handlingShortcut && GetHoverPointer(pointer)) {
					pos = pointer.World;
				} else {
					Rectf view = GetViewRect();
					std::int32_t spriteLayer = std::max(_document.GetSpriteLayerIndex(), 0);
					pos = GetLayerView(spriteLayer).ScreenToWorld(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f));
				}
				SetPlayStart(true, Vector2i(std::int32_t(std::round(pos.X)), std::int32_t(std::round(pos.Y))));
				ShowMessage(_("Play-tests from the marker start here now (Alt+F5)"));
			}).Key = { Keys::F6, 0 };
			auto& clearMarker = a.Add("play.clearMarker"_s, _("Remove Play Start Marker"), EditorIcon::None, [this]() { SetPlayStart(false); });
			clearMarker.IsEnabled = [this]() { return _hasPlayStart; };
			struct PlayChoice { const char* Id; const char* Label; std::int32_t Value; bool Character; };
			static const PlayChoice Choices[] = {
				{ "play.jazz", "Jazz", std::int32_t(PlayerType::Jazz), true },
				{ "play.spaz", "Spaz", std::int32_t(PlayerType::Spaz), true },
				{ "play.lori", "Lori", std::int32_t(PlayerType::Lori), true },
				{ "play.easy", "Easy", std::int32_t(GameDifficulty::Easy), false },
				{ "play.normal", "Normal", std::int32_t(GameDifficulty::Normal), false },
				{ "play.hard", "Hard", std::int32_t(GameDifficulty::Hard), false },
			};
			for (const PlayChoice& choice : Choices) {
				std::int32_t value = choice.Value;
				bool character = choice.Character;
				auto& action = a.Add(choice.Id, _(choice.Label), EditorIcon::None, [this, value, character]() {
					(character ? _settings.PlayCharacter : _settings.PlayDifficulty) = value;
					_settings.Save();
				});
				action.IsChecked = [this, value, character]() {
					return ((character ? _settings.PlayCharacter : _settings.PlayDifficulty) == value);
				};
			}

#if defined(WITH_MULTIPLAYER)
			// Multiplayer in local splitscreen, the rules of a game mode with up to 4 players on one screen
			using Multiplayer::MpGameMode;
			struct ModeChoice { const char* Id; const char* Label; MpGameMode Mode; };
			static const ModeChoice Modes[] = {
				{ "play.alone", N_("Alone"), MpGameMode::Unknown },
				{ "play.cooperation", N_("Cooperation"), MpGameMode::Cooperation },
				{ "play.battle", N_("Battle"), MpGameMode::Battle },
				{ "play.teamBattle", N_("Team Battle"), MpGameMode::TeamBattle },
				{ "play.race", N_("Race"), MpGameMode::Race },
				{ "play.treasureHunt", N_("Treasure Hunt"), MpGameMode::TreasureHunt },
				{ "play.captureTheFlag", N_("Capture the Flag"), MpGameMode::CaptureTheFlag },
			};
			for (const ModeChoice& choice : Modes) {
				std::int32_t value = std::int32_t(choice.Mode);
				auto& action = a.Add(choice.Id, _(choice.Label), EditorIcon::None, [this, value]() {
					_settings.PlayGameMode = value;
					_settings.Save();
				});
				action.IsChecked = [this, value]() { return (_settings.PlayGameMode == value); };
			}
			for (std::int32_t count = 1; count <= LevelInitialization::MaxPlayerCount; count++) {
				auto& action = a.Add(String("play.players"_s + String(std::to_string(count).c_str())), _fn("{} Player", "{} Players", count, count), EditorIcon::None, [this, count]() {
					_settings.PlayPlayerCount = count;
					_settings.Save();
				});
				action.IsChecked = [this, count]() { return (_settings.PlayPlayerCount == count); };
				// Players of multiplayer only
				action.IsEnabled = [this]() { return (_settings.PlayGameMode != 0); };
			}
#endif

			auto& path = a.Add("view.playPath"_s, _("Show Play Path"), EditorIcon::Path, [this]() { _showPlayPath = !_showPlayPath; });
			path.Key = { Keys::L, KeyMod::Ctrl };
			path.IsChecked = [this]() { return _showPlayPath; };

			auto& reach = a.Add("view.reachability"_s, _("Show Where the Player Can Get"), EditorIcon::Reach, [this]() {
				if (!_reachability.empty()) {
					_reachability.clear();
					return;
				}
				// The level is loaded again first, so the events are as they are now
				RebuildRuntime();
				if (!ComputeReachability(_reachability)) {
					ShowMessage(_("It can't be found out, the level needs a start of the character it's played with"));
					return;
				}
				std::int32_t reached = 0;
				for (std::int32_t distance : _reachability) {
					reached += (distance >= 0 ? 1 : 0);
				}
				ShowMessage(_f("The player can get to {} tiles, shown green, turn it on again after a change", reached));
			});
			reach.Key = { Keys::R, KeyMod::Ctrl | KeyMod::Shift };
			reach.IsChecked = [this]() { return !_reachability.empty(); };
		}
	}

	bool EditorSession::ComputeReachability(SmallVectorImpl<std::int32_t>& distances)
	{
		distances.clear();
#if defined(WITH_MULTIPLAYER)
		if (_editHandler == nullptr || _editHandler->TileMap() == nullptr || _editHandler->EventMap() == nullptr) {
			return false;
		}
		Events::EventMap* eventMap = _editHandler->EventMap();
		Vector2f start = eventMap->GetSpawnPosition(PlayerType(std::clamp(_settings.PlayCharacter, 1, 3)));
		if (start.X < 0.0f || start.Y < 0.0f) {
			start = eventMap->GetSpawnPosition(PlayerType::Jazz);
		}
		if (start.X < 0.0f || start.Y < 0.0f) {
			return false;
		}
		// As the multiplayer starts the tracer is given otherwise, it finds the nearest tile the player can be in
		float waterLevel = float(_document.Properties.WaterLevel);
		if (!Multiplayer::ComputeReachableTiles(_editHandler->TileMap(), eventMap, waterLevel, start, distances)) {
			return false;
		}
		// The tracer works on the sprite layer, so its tiles are those of the event grid
		Vector2i gridSize = _document.GetEventGridSize();
		return (std::int32_t(distances.size()) == gridSize.X * gridSize.Y);
#else
		return false;
#endif
	}

	void EditorSession::CreateInterface()
	{
		_surface = std::make_unique<UiSurface>();
		_shell = _surface->SetRoot<EditorShell>(*this);
		// The ring back into the editor reveals all of it, the panels too, so it's over them. The ring into the game is
		// over the panels too, they're seen outside of it as long as it grows.
		_surface->DrawOver = [this](UiPainter& painter, Vector2f screenSize) {
			if (_mode == SessionMode::Edit) {
				_playTransition.Draw(painter, screenSize);
			} else if (_mode == SessionMode::Play && _playTransition.HasInside()) {
				_playTransition.DrawRing(painter, screenSize);
			}
		};
		// Events that turn by the tiles around them are shown turned as the game turns them
		_eventPreviews.IsAreaEmpty = [this](const AABBf& aabb) {
			Tiles::TileMap* tileMap = (_editHandler != nullptr ? _editHandler->TileMap() : nullptr);
			if (tileMap == nullptr) {
				return true;
			}
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
			return tileMap->IsTileEmpty(aabb, params);
		};
	}

	void EditorSession::SetCurrentLayer(std::int32_t layer)
	{
		if (layer < 0 || layer >= std::int32_t(_document.Layers.size()) || layer == _currentLayer) {
			return;
		}
		_tools->GetActive()->Cancel();
		_currentLayer = layer;
	}

	bool EditorSession::GetHoverPointer(ToolPointer& pointer)
	{
		if (!_pointerKnown || _editHandler == nullptr || (!_toolPointerDown && _surface->IsOverInterface(_pointerPos))) {
			return false;
		}
		pointer = MakeToolPointer(_pointerPos, _toolButton);
		return true;
	}

	bool EditorSession::Undo()
	{
		_tools->GetActive()->Cancel();
		DocumentChanges changes;
		if (!_history->Undo(changes)) {
			return false;
		}
		OnDocumentChanged(changes);
		return true;
	}

	bool EditorSession::Redo()
	{
		_tools->GetActive()->Cancel();
		DocumentChanges changes;
		if (!_history->Redo(changes)) {
			return false;
		}
		OnDocumentChanged(changes);
		return true;
	}

	void EditorSession::OnDocumentChanged(const DocumentChanges& changes)
	{
		if (changes.IsEmpty() || _editHandler == nullptr) {
			return;
		}

		if (changes.HasAny(DocumentChangeKind::LayerStructure)) {
			_selection.Clear();
			_eventSelection.Clear();
			_currentLayer = std::clamp(_currentLayer, 0, std::max<std::int32_t>(std::int32_t(_document.Layers.size()) - 1, 0));
		}

		if (changes.HasAny(DocumentChangeKind::Events)) {
			// Undone events aren't selected anymore
			_eventSelection.DropEmpty(_document);
		}
		_tileLod->OnDocumentChanged(changes);
		if (changes.HasAny(DocumentChangeKind::AnimationStructure | DocumentChangeKind::AnimationTiming)) {
			// Tiles show other frames, so other masks
			_maskOverlay.Invalidate();
		} else if (changes.HasAny(DocumentChangeKind::Tiles)) {
			for (const DocumentTileArea& area : changes.GetTileAreas()) {
				_maskOverlay.InvalidateTiles(area.Layer, area.Area);
			}
		}

		if (changes.HasAny(DocumentChangeKind::LayerProperties | DocumentChangeKind::EditorState | DocumentChangeKind::LayerStructure)) {
			// Layers are shown as the document has them, the layer being edited is drawn as tiles again afterwards
			UpdateLayerOverride(true);
		}
		bool reload = (_editHandler->TileMap() == nullptr || !RuntimeSync::Apply(_document, *_editHandler->TileMap(), changes));
		if (!reload && changes.HasAny(DocumentChangeKind::TileOverrides)) {
			reload = !ApplyTileOverrides();
		}
		if (!reload && changes.HasAny(DocumentChangeKind::LevelProperties)) {
			if (_editHandler->NeedsReload(_document)) {
				reload = true;
			} else {
				_editHandler->ApplyLevelProperties(_document.Properties);
			}
		}
		if (reload) {
			RebuildRuntime();
		} else if (changes.HasAny(DocumentChangeKind::Palette) && (_document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette) {
			// Indexed tiles and sprites take their colors from the palette as they're drawn, so it's enough to replace it
			ContentResolver::Get().SetSpritePalette(_document.LevelPalette);
		}
		UpdateWindowTitle();
	}

	bool EditorSession::ApplyTileOverrides()
	{
		Tiles::TileMap* tileMap = (_editHandler != nullptr ? _editHandler->TileMap() : nullptr);
		if (tileMap == nullptr) {
			return false;
		}
		// A tile that isn't overridden anymore is as its tile set has it, which only loading the level again gives
		for (std::uint16_t tileId : _appliedDiffuseOverrides) {
			if (SetTileOverrideCommand::FindDiffuse(_document, tileId) == nullptr) {
				return false;
			}
		}
		for (std::uint16_t tileId : _appliedMaskOverrides) {
			if (SetTileOverrideCommand::FindMask(_document, tileId) == nullptr) {
				return false;
			}
		}

		// The tiles of the level itself, changed in place, they're few too
		for (std::int32_t slot = 0; slot < std::int32_t(_document.ExtraTileSets.size()); slot++) {
			const Formats::ExtraTileSetRecord& record = _document.ExtraTileSets[slot];
			if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0) {
				continue;
			}
			const Formats::TileSetData* data = GetTileSetData(record.Name);
			if (data == nullptr) {
				return false;
			}
			for (std::int32_t i = 0; i < record.Count && record.Offset + i < data->TileCount; i++) {
				std::int32_t index = record.Offset + i;
				std::int32_t tileId = _document.ToTileId(TileRef{ std::uint16_t(i), std::uint8_t(slot + 1), 0 });
				if (tileId < 0) {
					return false;
				}
				const std::uint8_t* image = &data->Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
				if (data->TrueColor[index] != 0) {
					if (!ContentResolver::Get().ApplyTileDiffuseOverrideRgba(*tileMap, tileId, image)) {
						return false;
					}
				} else {
					ContentResolver::Get().ApplyTileDiffuseOverride(*tileMap, tileId, image);
				}
				std::uint8_t mask[32 * 32];
				const std::uint8_t* bits = &data->Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile];
				for (std::int32_t p = 0; p < 32 * 32; p++) {
					mask[p] = ((bits[p / 8] >> (p % 8)) & 1);
				}
				tileMap->OverrideTileMask(tileId, mask);
			}
		}

		// Only a few tiles are overridden, so all of them are applied again
		for (const Formats::TileDiffuseOverride& tile : _document.DiffuseOverrides) {
			ContentResolver::Get().ApplyTileDiffuseOverride(*tileMap, tile.TileId, tile.Indices);
		}
		for (const Formats::TileMaskOverride& tile : _document.MaskOverrides) {
			tileMap->OverrideTileMask(tile.TileId, tile.Mask);
		}
		RememberAppliedOverrides();
		_maskOverlay.Invalidate();
		return true;
	}

	void EditorSession::RememberAppliedOverrides()
	{
		_appliedDiffuseOverrides.clear();
		for (const Formats::TileDiffuseOverride& tile : _document.DiffuseOverrides) {
			_appliedDiffuseOverrides.push_back(tile.TileId);
		}
		_appliedMaskOverrides.clear();
		for (const Formats::TileMaskOverride& tile : _document.MaskOverrides) {
			_appliedMaskOverrides.push_back(tile.TileId);
		}
	}

	void EditorSession::StoreViewState()
	{
		DocumentViewState state;
		if (_editHandler != nullptr) {
			// While the level is played, where the editor was left
			EditorCamera& camera = _editHandler->GetCamera();
			bool editing = (_mode == SessionMode::Edit);
			float scale = (editing ? camera.GetTargetScale() : _editCameraScale);
			if (camera.GetGameplayScale() > 0.0f && scale > 0.0f) {
				state.HasCamera = true;
				state.CameraCenter = (editing ? camera.GetCenter() : _editCameraCenter);
				state.CameraZoom = scale / camera.GetGameplayScale();
			}
		}
		state.CurrentLayer = _currentLayer;
		state.ShowGrid = _showGrid;
		state.ShowEvents = _showEvents;
		state.ShowPlayPath = _showPlayPath;
		if (Tiles::TileMap* tileMap = (_editHandler != nullptr ? _editHandler->TileMap() : nullptr)) {
			state.FlatLayers = (tileMap->GetViewOptions().FlatAmount > 0.5f);
		}
		state.MaskMode = std::uint8_t(_maskMode);
		state.PickerPanel = std::uint8_t(_pickerPanel == PickerPanel::Events ? 1 : 0);
		state.HasPlaySettings = true;
		state.PlayCharacter = std::uint8_t(_settings.PlayCharacter);
		state.PlayDifficulty = std::uint8_t(_settings.PlayDifficulty);
		state.PlayGameMode = std::uint8_t(_settings.PlayGameMode);
		state.PlayPlayerCount = std::uint8_t(_settings.PlayPlayerCount);
		state.HasPlayStart = _hasPlayStart;
		state.PlayStart = _playStart;
		state.Write(_document);
	}

	void EditorSession::ApplyViewState()
	{
		DocumentViewState state;
		if (!DocumentViewState::Read(_document, state)) {
			return;
		}
		if (state.HasCamera) {
			_editHandler->SetInitialCamera(state.CameraCenter, state.CameraZoom, true);
		}
		if (state.CurrentLayer >= 0 && state.CurrentLayer < std::int32_t(_document.Layers.size())) {
			_currentLayer = state.CurrentLayer;
		}
		_showGrid = state.ShowGrid;
		_showEvents = state.ShowEvents;
		_showPlayPath = state.ShowPlayPath;
		_maskMode = std::min<std::int32_t>(state.MaskMode, 2);
		_pickerPanel = (state.PickerPanel == 1 ? PickerPanel::Events : PickerPanel::Tiles);
		if (state.FlatLayers) {
			if (Tiles::TileMap* tileMap = _editHandler->TileMap()) {
				Tiles::TileMapViewOptions options = tileMap->GetViewOptions();
				options.FlatAmount = 1.0f;
				tileMap->SetViewOptions(options);
			}
		}
		if (state.HasPlaySettings) {
			// The level is played as it was the last time, which becomes how the next level is played too
			_settings.PlayCharacter = std::clamp<std::int32_t>(state.PlayCharacter, 1, 3);
			_settings.PlayDifficulty = std::clamp<std::int32_t>(state.PlayDifficulty, 1, 3);
			_settings.PlayGameMode = state.PlayGameMode;
			_settings.PlayPlayerCount = std::clamp<std::int32_t>(state.PlayPlayerCount, 1, 4);
		}
		_hasPlayStart = state.HasPlayStart;
		_playStart = state.PlayStart;
	}

	void EditorSession::SetPlayStart(bool placed, Vector2i pos)
	{
		if (_hasPlayStart == placed && (!placed || _playStart == pos)) {
			return;
		}
		_hasPlayStart = placed;
		_playStart = (placed ? pos : Vector2i());
		// Placed on purpose, so it's a part of the level to save, but not a change to undo
		_history->MarkDirty();
		UpdateWindowTitle();
	}

	void EditorSession::StartMusicFade(float from, float to)
	{
		_musicFadeFrom = from;
		_musicFadeTo = to;
		_musicFadeTime = 0.0f;
	}

	void EditorSession::UpdateMusicFade(float timeMult)
	{
#if defined(WITH_AUDIO)
		if (_musicFadeTime < 0.0f) {
			return;
		}
		_musicFadeTime += timeMult;
		float t = std::min(_musicFadeTime / MusicFadeFrames, 1.0f);
		float eased = t * t * (3.0f - 2.0f * t);
		// The cutoff is heard on a log scale, so it's faded on one
		float value = _musicFadeFrom * std::pow(_musicFadeTo / _musicFadeFrom, eased);
		LevelHandler* handler = (_mode == SessionMode::Play && _playHandler != nullptr ? &_playHandler->GetLevelHandler() : _editHandler.get());
		if (handler != nullptr) {
			handler->SetMusicMuffling(value);
		}
		if (t >= 1.0f) {
			_musicFadeTime = -1.0f;
			if (_mode == SessionMode::Edit && _editHandler != nullptr) {
				// Kept, so the music started again later is muffled too
				_editHandler->SetMusicLowPass(_musicFadeTo);
			}
		}
#endif
	}

	void EditorSession::RememberSuspendedLevel()
	{
		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		_suspendedSpriteTiles.clear();
		_suspendedSpriteSize = Vector2i();
		if (spriteLayer >= 0) {
			const auto& layer = _document.Layers[spriteLayer];
			_suspendedSpriteTiles = layer.Tiles;
			_suspendedSpriteSize = Vector2i(layer.GetWidth(), layer.GetHeight());
		}
		_suspendedEvents = _document.Events;
		_suspendedOffGridEvents = _document.OffGridEvents;
	}

	void EditorSession::RestorePlayState(IPlayTest& from, IPlayTest& to)
	{
		Actors::Player* oldPlayer = from.GetPlayer();
		Actors::Player* newPlayer = to.GetPlayer();
		if (oldPlayer != nullptr && newPlayer != nullptr) {
			newPlayer->TakeOverPlayTestState(*oldPlayer);
		}

		// What was collected, destroyed or triggered stays so, only if the cells are where they were
		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		if (spriteLayer < 0) {
			return;
		}
		const auto& layer = _document.Layers[spriteLayer];
		Vector2i size(layer.GetWidth(), layer.GetHeight());
		if (size != _suspendedSpriteSize || _suspendedSpriteTiles.size() != layer.Tiles.size()) {
			LOGI("Sprite layer was resized since the play-test was left, the level starts as new");
			return;
		}
		std::int32_t cellCount = size.X * size.Y;
		SmallVector<bool, 0> edited;
		edited.resize(std::size_t(cellCount), false);
		std::int32_t editedCount = 0;
		for (std::int32_t i = 0; i < cellCount; i++) {
			if (!(_suspendedSpriteTiles[i] == layer.Tiles[i])) {
				edited[i] = true;
			}
		}
		// Both lists are sorted by the cell
		auto markEvents = [&](const SmallVectorImpl<DocumentEvent>& a, const SmallVectorImpl<DocumentEvent>& b) {
			std::size_t j = 0;
			for (const DocumentEvent& event : a) {
				while (j < b.size() && b[j].Cell < event.Cell) {
					j++;
				}
				bool same = (j < b.size() && b[j].Cell == event.Cell && LevelDocument::AreEventsEqual(b[j].Record, event.Record));
				if (!same && event.Cell < std::uint32_t(cellCount)) {
					edited[event.Cell] = true;
				}
			}
		};
		markEvents(_suspendedEvents, _document.Events);
		markEvents(_document.Events, _suspendedEvents);
		for (bool cell : edited) {
			editedCount += (cell ? 1 : 0);
		}

		LevelHandler& oldHandler = from.GetLevelHandler();
		LevelHandler& newHandler = to.GetLevelHandler();

		// Tiles: a destruction frame for each cell of the sprite layer, then the triggers
		{
			MemoryStream oldState, newState, merged;
			oldHandler.TileMap()->SerializeResumableToStream(oldState, false);
			newHandler.TileMap()->SerializeResumableToStream(newState, false);
			oldState.Seek(0, SeekOrigin::Begin);
			newState.Seek(0, SeekOrigin::Begin);
			std::int32_t oldCount = oldState.ReadVariableInt32();
			std::int32_t newCount = newState.ReadVariableInt32();
			if (oldCount == cellCount && newCount == cellCount) {
				merged.WriteVariableInt32(cellCount);
				for (std::int32_t i = 0; i < cellCount; i++) {
					std::int32_t oldFrame = oldState.ReadVariableInt32();
					std::int32_t newFrame = newState.ReadVariableInt32();
					merged.WriteVariableInt32(edited[i] ? newFrame : oldFrame);
				}
				// Triggers are of the whole level, they stay as they were
				std::int64_t oldTriggerSize = oldState.GetSize() - oldState.GetPosition();
				std::int64_t newTriggerSize = newState.GetSize() - newState.GetPosition();
				MemoryStream& triggerSource = (oldTriggerSize == newTriggerSize ? oldState : newState);
				SmallVector<std::uint8_t, 0> triggers;
				triggers.resize(std::size_t(newTriggerSize));
				if (newTriggerSize > 0) {
					triggerSource.Read(triggers.data(), newTriggerSize);
					merged.Write(triggers.data(), newTriggerSize);
				}
				merged.Seek(0, SeekOrigin::Begin);
				newHandler.TileMap()->InitializeFromStream(merged);
			}
		}

		// Events: the type, the flags and the parameters of each cell, the grid and then rows of off-grid events
		{
			MemoryStream oldState, newState, merged;
			oldHandler.EventMap()->SerializeResumableToStream(oldState, false);
			newHandler.EventMap()->SerializeResumableToStream(newState, false);
			oldState.Seek(0, SeekOrigin::Begin);
			newState.Seek(0, SeekOrigin::Begin);
			std::int32_t oldCount = oldState.ReadVariableInt32();
			std::int32_t newCount = newState.ReadVariableInt32();
			if (oldCount == newCount && oldCount >= cellCount) {
				// Off-grid events are kept by their order, only if none of them changed
				bool sameOffGrid = (_suspendedOffGridEvents.size() == _document.OffGridEvents.size() &&
					std::memcmp(_suspendedOffGridEvents.data(), _document.OffGridEvents.data(), _document.OffGridEvents.size() * sizeof(Formats::OffGridEventRecord)) == 0);
				merged.WriteVariableInt32(newCount);
				for (std::int32_t i = 0; i < newCount; i++) {
					std::uint32_t oldType = oldState.ReadVariableUint32();
					std::uint32_t oldFlags = oldState.ReadVariableUint32();
					std::uint8_t oldParams[16];
					oldState.Read(oldParams, sizeof(oldParams));
					std::uint32_t newType = newState.ReadVariableUint32();
					std::uint32_t newFlags = newState.ReadVariableUint32();
					std::uint8_t newParams[16];
					newState.Read(newParams, sizeof(newParams));
					bool useNew = (i < cellCount ? edited[i] : !sameOffGrid);
					merged.WriteVariableUint32(useNew ? newType : oldType);
					merged.WriteVariableUint32(useNew ? newFlags : oldFlags);
					merged.Write(useNew ? newParams : oldParams, sizeof(oldParams));
				}
				merged.Seek(0, SeekOrigin::Begin);
				newHandler.EventMap()->InitializeFromStream(merged);
			}
		}

		LOGI("Play-test continues where it was left, {} edited cells start as new", editedCount);
	}

	bool EditorSession::SaveTo(StringView path, bool exportOnly)
	{
		StoreViewState();

		// The list of custom levels shows it small, it's made again each time, it's not a change of the level
		{
			SmallVector<std::uint8_t, 0> preview;
			LevelThumbnail::MakePreview(*this, preview);
			_document.SetChunk(Formats::LevelFile::PreviewChunk, preview);
		}

		// Written next to the target first, so a failed write never leaves a damaged level behind
		String tempPath = path + ".tmp"_s;
		{
			auto s = fs::Open(tempPath, FileAccess::Write);
			LevelFlags extraFlags = (exportOnly && _exportNotEditable ? LevelFlags::NotEditable : LevelFlags::None);
			if (!s->IsValid() || !_document.Save(*s, exportOnly, 9, extraFlags)) {
				s = nullptr;
				fs::RemoveFile(tempPath);
				LOGE("Level cannot be saved to \"{}\"", path);
				return false;
			}
		}
		if (fs::FileExists(path)) {
			fs::RemoveFile(path);
		}
		if (!fs::Move(tempPath, path)) {
			LOGE("Level cannot be saved to \"{}\"", path);
			return false;
		}

		if (!exportOnly) {
			_history->MarkSaved();
			_autosave.Discard();
			_settings.AddRecentFile(path);
			_settings.Save();
			UpdateWindowTitle();
		}
		LOGI("Level saved to \"{}\"", path);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// Only in the memory of the game, the browser saves it to the computer
		WebFileBridge::DownloadFile(path);
#endif
		return true;
	}

	void EditorSession::ToggleLayerFlag(std::int32_t layer, std::uint8_t flag)
	{
		if (layer < 0 || layer >= std::int32_t(_document.Layers.size())) {
			return;
		}
		_document.Layers[layer].EditorFlags ^= flag;
		if (layer == _currentLayer) {
			_tools->GetActive()->Cancel();
		}
		DocumentChanges changes;
		changes.Add(DocumentChangeKind::EditorState);
		OnDocumentChanged(changes);
	}

	LevelDocument& EditorSession::GetDocument()
	{
		return _document;
	}

	std::int32_t EditorSession::GetCurrentLayer() const
	{
		return _currentLayer;
	}

	bool EditorSession::IsLayerEditable(std::int32_t layer) const
	{
		return (layer >= 0 && layer < std::int32_t(_document.Layers.size()) &&
			(_document.Layers[layer].EditorFlags & (DocumentLayer::EditorHidden | DocumentLayer::EditorLocked)) == 0);
	}

	LayerView EditorSession::GetLayerView(std::int32_t layer)
	{
		LayerView view;
		Tiles::TileMap* tileMap = GetTileMap();
		if (tileMap == nullptr || layer < 0 || layer >= std::int32_t(_document.Layers.size())) {
			return view;
		}

		// The same view origin as the viewport uses, see PlayerViewport::UpdateCamera()
		EditorCamera& camera = _editHandler->GetCamera();
		Vector2i viewSize = camera.GetViewSize();
		Vector2f half(viewSize.X * 0.5f, viewSize.Y * 0.5f);
		float scale = camera.GetScale();
		Vector2f origin = camera.GetCenter() * scale - half;
		if (!PreferencesCache::UnalignedViewport) {
			origin = Vector2f(std::floor(origin.X), std::floor(origin.Y));
		}
		Vector2f center = (origin + half) / scale;

		const DocumentLayer& documentLayer = _document.Layers[layer];
		view.Scale = scale;
		view.ViewOrigin = origin;
		view.Origin = tileMap->GetLayerOrigin(layer, center, Vector2f(viewSize.X / scale, viewSize.Y / scale));
		view.Size = Vector2i(documentLayer.GetWidth(), documentLayer.GetHeight());
		if (!documentLayer.IsSpriteLayer()) {
			view.RepeatX = (documentLayer.Record.Flags & Formats::LayerRecord::RepeatX) != 0;
			view.RepeatY = (documentLayer.Record.Flags & Formats::LayerRecord::RepeatY) != 0;
		}
		return view;
	}

	TileStamp& EditorSession::GetStamp()
	{
		return _stamp;
	}

	void EditorSession::SetStamp(TileStamp&& stamp)
	{
		// Painting at random stays on for any other tiles
		bool random = _stamp.Random;
		std::uint32_t seed = _stamp.Seed;
		_stamp = std::move(stamp);
		_stamp.Random = random;
		_stamp.Seed = seed;
	}

	TileSelection& EditorSession::GetSelection()
	{
		return _selection;
	}

	void EditorSession::Execute(std::unique_ptr<EditCommand> command, bool allowMerge)
	{
		DocumentChanges changes;
		_history->Execute(std::move(command), changes, allowMerge);
		OnDocumentChanged(changes);
	}

	void EditorSession::PreviewTile(std::int32_t layer, std::int32_t x, std::int32_t y, const TileRef& tile)
	{
		if (Tiles::TileMap* tileMap = GetTileMap()) {
			tileMap->ReplaceLayerTile(layer, x, y, RuntimeSync::MakeLayerTile(_document, tile));
		}
	}

	void EditorSession::RevertPreview(std::int32_t layer, const Recti& area)
	{
		if (Tiles::TileMap* tileMap = GetTileMap()) {
			RuntimeSync::ApplyTiles(_document, *tileMap, layer, area);
		}
	}

	Tiles::TileMap* EditorSession::GetTileMap()
	{
		return (_editHandler != nullptr ? _editHandler->TileMap() : nullptr);
	}

	EditorCamera* EditorSession::GetCamera()
	{
		return (_editHandler != nullptr ? &_editHandler->GetCamera() : nullptr);
	}

	void EditorSession::ShowMessage(StringView text)
	{
		if (_surface != nullptr) {
			_surface->ShowToast(text);
		}
	}

	EventStamp& EditorSession::GetEventBrush()
	{
		return _eventBrush;
	}

	EventSelection& EditorSession::GetEventSelection()
	{
		return _eventSelection;
	}

	EventPreviews& EditorSession::GetEventPreviews()
	{
		return _eventPreviews;
	}

	float EditorSession::GetAnimTime() const
	{
		return (_surface != nullptr ? _surface->AnimTime : 0.0f);
	}

	void EditorSession::SetEventBrush(EventStamp&& brush)
	{
		_eventBrush = std::move(brush);
		_eventSelection.Clear();
		// The path tool places it too
		if (_tools->GetChosenType() != EditorToolType::Event && _tools->GetChosenType() != EditorToolType::Path) {
			_tools->SetActive(EditorToolType::Event);
		}
		_pickerPanel = PickerPanel::Events;
	}

	void EditorSession::PickEventFromList(Function<void(EventType)>&& callback)
	{
		_eventPick = std::move(callback);
		_pickerPanel = PickerPanel::Events;
		ShowMessage(_("Choose the event in the list below"));
	}

	void EditorSession::OnEventChosen(EventType type)
	{
		if (_eventPick) {
			Function<void(EventType)> callback = std::move(_eventPick);
			_eventPick = {};
			callback(type);
			return;
		}
		SetEventBrush(EventStamp::FromEvent(EventCatalog::MakeDefault(type)));
	}

	Vector2i EditorSession::GetViewSize() const
	{
		LevelHandler* handler = GetActiveHandler();
		return (handler != nullptr ? handler->GetViewSize() : Vector2i());
	}

	LevelHandler* EditorSession::GetActiveHandler() const
	{
		if (_mode == SessionMode::Play && _playHandler != nullptr) {
			return &_playHandler->GetLevelHandler();
		}
		return _editHandler.get();
	}

	void EditorSession::OnBeginFrame()
	{
#if defined(DEATH_DEBUG)
		// Run from the second frame, once the view was initialized, /wait <frames> delays the rest
		if DEATH_UNLIKELY(!_debugStartupCommands.empty() && ++_debugFrame >= 2) {
			if (_debugWaitFrames > 0) {
				_debugWaitFrames--;
			} else {
				while (!_debugStartupCommands.empty()) {
					String command = std::move(_debugStartupCommands.front());
					_debugStartupCommands.erase(_debugStartupCommands.begin());
					if (command.hasPrefix("/wait "_s)) {
						String frames = command.exceptPrefix(6);
						_debugWaitFrames = std::int32_t(std::strtol(frames.data(), nullptr, 10));
						break;
					}
					if (command == "/quit"_s) {
						theApplication().Quit();
						break;
					}
					if (!ExecuteDebugCommand(command)) {
						LOGW("Unknown editor command: {}", command);
					}
				}
			}
		}
#endif

		if (_editHandler != nullptr) {
			UpdateGamepad(theApplication().GetTimeMult());
			if (_mode == SessionMode::Edit) {
				_touch.Update(theApplication().GetTimeMult());
			}
		}
		UpdateMusicFade(theApplication().GetTimeMult());
#if defined(DEATH_TARGET_EMSCRIPTEN)
		WebFileBridge::Update();
#endif
		if (_surface != nullptr) {
			_surface->ShowVirtualCursor = (_inputModality == InputModality::Gamepad && _mode == SessionMode::Edit);
			_surface->VirtualCursor = _gamepadCursor;
		}
		if (_mode == SessionMode::Play && _playHandler != nullptr) {
			_playTransition.Update(theApplication().GetTimeMult());
			_playHandler->GetLevelHandler().OnBeginFrame();
		} else if (_editHandler != nullptr) {
			// The ring back into the editor, the play-test it shows the last frame of is released once it's done
			_playTransition.Update(theApplication().GetTimeMult());
			if (!_playTransition.IsActive()) {
				_closingPlayHandler = nullptr;
			}
			UpdateLayerOverride();
			UpdateTileLod();
			if (_smartTilesUsed && !_smartTiles.IsReady()) {
				_smartTiles.Update();
			}
			_editHandler->OnBeginFrame();
			// The frame without the panels was shown, the game takes over now
			if (_mode == SessionMode::EnteringPlay) {
				// Once the camera is there, and a frame showing it has been drawn
				if (_editHandler->GetCamera().IsMoving()) {
					_enteringPlayFrameShown = false;
				} else if (_enteringPlayFrameShown) {
					FinishStartPlayTest();
				} else {
					_enteringPlayFrameShown = true;
				}
			}
		}
		if (_history != nullptr) {
			_autosave.Update(theApplication().GetTimeMult(), _document, *_history);
		}
	}

	void EditorSession::OnEndFrame()
	{
		if (_mode == SessionMode::Play && _playHandler != nullptr) {
			_playHandler->GetLevelHandler().OnEndFrame();
		} else if (_editHandler != nullptr) {
			_editHandler->OnEndFrame();
		}
	}

	void EditorSession::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		if (_mode == SessionMode::Play && _playHandler != nullptr) {
			_playHandler->GetLevelHandler().OnInitializeViewport(width, height);
		} else if (_editHandler != nullptr) {
			_editHandler->OnInitializeViewport(width, height);
		}

		if (_surface != nullptr) {
			// Drawn over everything, at the size of the window
			_surface->setParent(&theApplication().GetRootNode());
			IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
			float scale = (_settings.UiScale > 0.0f ? _settings.UiScale
				: std::max(1.0f, std::floor(std::min(gfxDevice.drawableWidth() / 640.0f, gfxDevice.drawableHeight() / 400.0f))));
			_surface->SetScale(scale);
		}
	}

	void EditorSession::OnKeyPressed(const KeyboardEvent& event)
	{
		if (_editHandler == nullptr) {
			return;
		}

		_modifiers = event.mod;
		SetInputModality(InputModality::KeyboardMouse);
		if (_mode == SessionMode::Play) {
			if (event.sym == Keys::F5 && (event.mod & (KeyMod::Ctrl | KeyMod::Shift | KeyMod::Alt)) == 0) {
				StopPlayTest(true);
			} else if (_playHandler != nullptr) {
				_playHandler->GetLevelHandler().OnKeyPressed(event);
			}
			return;
		}
		if (_mode == SessionMode::EnteringPlay) {
			if (event.sym == Keys::Escape || event.sym == Keys::F5) {
				StopPlayTest(false);
			}
			return;
		}
		if (_surface->OnKeyPressed(event)) {
			return;
		}

		bool noModifiers = (event.mod & (KeyMod::Ctrl | KeyMod::Alt | KeyMod::Shift)) == 0;
		switch (event.sym) {
			case Keys::Space:
				_tools->BeginTemporary(EditorToolType::Hand);
				return;
			case Keys::LAlt:
				// The event tool picks events with Alt itself
				if (_tools->GetChosenType() != EditorToolType::Event) {
					_tools->BeginTemporary(EditorToolType::Eyedropper);
				}
				return;
			case Keys::Escape:
				if (_tools->GetActive()->IsBusy()) {
					_tools->GetActive()->Cancel();
					_toolPointerDown = false;
				} else {
					ClearSelection();
				}
				return;
			default:
				break;
		}

		if (noModifiers) {
			EditorCamera& camera = _editHandler->GetCamera();
			Rectf view = GetViewRect();
			switch (event.sym) {
				case Keys::Left: camera.PanBy(Vector2f(-view.W * KeyboardPanFraction, 0.0f)); return;
				case Keys::Right: camera.PanBy(Vector2f(view.W * KeyboardPanFraction, 0.0f)); return;
				case Keys::Up: camera.PanBy(Vector2f(0.0f, -view.H * KeyboardPanFraction)); return;
				case Keys::Down: camera.PanBy(Vector2f(0.0f, view.H * KeyboardPanFraction)); return;
				default: break;
			}
			// The number keys choose a layer
			if (event.sym >= Keys::D1 && event.sym <= Keys::D9) {
				SetCurrentLayer(std::int32_t(event.sym) - std::int32_t(Keys::D1));
				return;
			}
		} else if ((event.mod & KeyMod::Shift) != 0 && (event.mod & (KeyMod::Ctrl | KeyMod::Alt)) == 0 &&
			event.sym >= Keys::D1 && event.sym <= Keys::D9) {
			// With Shift they hide it
			ToggleLayerFlag(std::int32_t(event.sym) - std::int32_t(Keys::D1), DocumentLayer::EditorHidden);
			return;
		}

		_handlingShortcut = true;
		_actions.HandleKey(event);
		_handlingShortcut = false;
	}

	void EditorSession::OnKeyReleased(const KeyboardEvent& event)
	{
		_modifiers = event.mod;
		if (_mode == SessionMode::Play) {
			if (_playHandler != nullptr) {
				_playHandler->GetLevelHandler().OnKeyReleased(event);
			}
			return;
		}
		if (_tools == nullptr) {
			return;
		}
		if (event.sym == Keys::Space) {
			_tools->EndTemporary(EditorToolType::Hand);
		} else if (event.sym == Keys::LAlt) {
			_tools->EndTemporary(EditorToolType::Eyedropper);
		}
	}

	void EditorSession::OnTextInput(const TextInputEvent& event)
	{
		if (_mode == SessionMode::Play) {
			if (_playHandler != nullptr) {
				_playHandler->GetLevelHandler().OnTextInput(event);
			}
			return;
		}
		if (_surface != nullptr) {
			_surface->OnTextInput(event);
		}
	}

	void EditorSession::OnTouchEvent(const TouchEvent& event)
	{
		if (_mode == SessionMode::Play && _playHandler != nullptr) {
			_playHandler->GetLevelHandler().OnTouchEvent(event);
			return;
		}
		if (_editHandler == nullptr || _surface == nullptr || _mode != SessionMode::Edit) {
			return;
		}
		SetInputModality(InputModality::Touch);
		_touch.OnTouchEvent(event, _surface->GetScreenSize(), _surface->GetScale());
	}

	void EditorSession::SetupTouchGestures()
	{
		_touch.IsOverInterface = [this](Vector2f pos) {
			return _surface->IsOverInterface(pos);
		};
		_touch.PointerDown = [this](Vector2f pos) {
			PointerMove(pos);
			PointerDown(pos, MouseButton::Left);
		};
		_touch.PointerMove = [this](Vector2f pos) {
			PointerMove(pos);
		};
		_touch.PointerUp = [this](Vector2f pos) {
			PointerUp(pos, MouseButton::Left);
			// A box tapped to type into needs a keyboard, of the system or of the editor
			if (UiTextBox* field = (_surface->GetFocus() != nullptr ? runtime_cast<UiTextBox>(_surface->GetFocus()) : nullptr)) {
				if (runtime_cast<UiNumberBox>(field) == nullptr) {
					field->ShowScreenKeyboard();
				}
			}
		};
		_touch.PointerCancel = [this]() {
			// Two fingers move the view, what one of them started isn't done
			if (_toolPointerDown) {
				_toolPointerDown = false;
				_tools->GetActive()->Cancel();
			}
		};
		_touch.PanZoom = [this](Vector2f center, Vector2f pan, float zoom) {
			EditorCamera& camera = _editHandler->GetCamera();
			// The level follows the fingers
			camera.PanBy(Vector2f(-pan.X, -pan.Y));
			if (zoom != 1.0f) {
				camera.ZoomBy(center, zoom);
			}
		};
		_touch.Tap = [this](std::int32_t fingers) {
			if (fingers == 2) {
				Undo();
			} else if (fingers >= 3) {
				Redo();
			}
		};
		_touch.LongPress = [this](Vector2f pos) {
			ShowToolMenu(pos);
		};
	}

	void EditorSession::ShowToolMenu(Vector2f pos)
	{
		if (_surface == nullptr || _radialMenu != nullptr) {
			return;
		}
		SmallVector<UiMenuEntry, 0> entries;
		for (const char* id : { "tool.brush", "tool.eraser", "tool.rectangle", "tool.fill", "tool.smart", "tool.eyedropper", "tool.select", "tool.event", "tool.hand", "edit.undo", "edit.redo" }) {
			if (EditorAction* action = _actions.Find(id)) {
				entries.push_back(UiMenuEntry::FromAction(*action));
			}
		}
		_radialMenu = UiRadialMenu::Show(*_surface, pos, std::move(entries), [this]() { _radialMenu = nullptr; });
	}

	void EditorSession::OnMouseDown(const MouseEvent& event)
	{
		if (_editHandler == nullptr) {
			return;
		}
		SetInputModality(InputModality::KeyboardMouse);
		PointerDown(ToDrawable(event.x, event.y), event.button);
	}

	void EditorSession::SetInputModality(InputModality modality)
	{
		if (_inputModality == modality) {
			return;
		}
		_inputModality = modality;
		if (modality == InputModality::Gamepad) {
			// The cursor starts where the mouse was, or in the middle of the view
			Rectf view = GetViewRect();
			_gamepadCursor = (_pointerKnown && view.Contains(_pointerPos) ? _pointerPos : Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f));
			PointerMove(_gamepadCursor);
		}
	}

	void EditorSession::UpdateGamepad(float timeMult)
	{
		IInputManager& input = theApplication().GetInputManager();
		const JoyMappedState* state = nullptr;
		for (std::int32_t i = 0; i < IInputManager::MaxNumJoysticks; i++) {
			if (input.isJoyMapped(i)) {
				state = &input.joyMappedState(i);
				break;
			}
		}
		if (state == nullptr) {
			_gamepadButtons = 0;
			return;
		}

		std::uint32_t buttons = 0;
		for (std::int32_t i = 0; i < std::int32_t(ButtonName::Count); i++) {
			if (state->isButtonPressed(ButtonName(i))) {
				buttons |= (1u << i);
			}
		}
		std::uint32_t pressed = (buttons & ~_gamepadButtons);
		std::uint32_t released = (_gamepadButtons & ~buttons);
		_gamepadButtons = buttons;
		auto wasPressed = [pressed](ButtonName name) { return (pressed & (1u << std::int32_t(name))) != 0; };
		auto wasReleased = [released](ButtonName name) { return (released & (1u << std::int32_t(name))) != 0; };
		auto isHeld = [buttons](ButtonName name) { return (buttons & (1u << std::int32_t(name))) != 0; };

		Vector2f leftStick(state->axisValue(AxisName::LeftX), state->axisValue(AxisName::LeftY));
		Vector2f rightStick(state->axisValue(AxisName::RightX), state->axisValue(AxisName::RightY));
		input.deadZoneNormalize(leftStick, IInputManager::LeftStickDeadZone);
		input.deadZoneNormalize(rightStick, IInputManager::RightStickDeadZone);
		float zoom = state->axisValue(AxisName::RightTrigger) - state->axisValue(AxisName::LeftTrigger);
		if (std::abs(zoom) < 0.1f) {
			zoom = 0.0f;
		}

		if (pressed != 0 || leftStick.Length() > 0.0f || rightStick.Length() > 0.0f || zoom != 0.0f) {
			SetInputModality(InputModality::Gamepad);
		}
		if (_inputModality != InputModality::Gamepad || _mode != SessionMode::Edit) {
			return;
		}

		float scale = _surface->GetScale();
		EditorCamera& camera = _editHandler->GetCamera();
		Vector2f cursor = _gamepadCursor;

		// The left stick moves the cursor, faster the farther it's pushed, and faster the longer it's held, so it's
		// precise for a short push and quick across the screen
		// While the ring of tools is shown, the stick picks from it
		float push = (_radialMenu == nullptr ? leftStick.Length() : 0.0f);
		if (push > 0.0f) {
			_gamepadCursorHold = std::min(_gamepadCursorHold + timeMult, GamepadBoostFrames);
			float boost = 1.0f + (GamepadMaxBoost - 1.0f) * (_gamepadCursorHold / GamepadBoostFrames);
			cursor += leftStick * (GamepadCursorSpeed * scale * push * boost * timeMult);
		} else {
			_gamepadCursorHold = 0.0f;
		}

		// A box for a value focused or under the cursor takes the pad and Y: the pad changes a number, Y opens the
		// screen keyboard of the system to type into it
		UiElement* fieldElement = (_surface->GetFocus() != nullptr ? _surface->GetFocus() : _surface->GetHover());
		UiTextBox* field = (fieldElement != nullptr ? runtime_cast<UiTextBox>(fieldElement) : nullptr);
		UiNumberBox* numberField = (field != nullptr ? runtime_cast<UiNumberBox>(field) : nullptr);
		bool fieldTakesY = false;
		if (field != nullptr && field->IsUsable() && wasPressed(ButtonName::Y)) {
			fieldTakesY = field->ShowScreenKeyboard();
		}
		bool fieldTakesPad = (numberField != nullptr && numberField->IsUsable());
		if (fieldTakesPad) {
			std::int32_t change = (std::int32_t(isHeld(ButtonName::Up)) + std::int32_t(isHeld(ButtonName::Right))) -
				(std::int32_t(isHeld(ButtonName::Down)) + std::int32_t(isHeld(ButtonName::Left)));
			if (change != 0) {
				bool apply = false;
				if (wasPressed(ButtonName::Up) || wasPressed(ButtonName::Down) || wasPressed(ButtonName::Left) || wasPressed(ButtonName::Right)) {
					_gamepadRepeat = 18.0f;
					apply = true;
				} else {
					_gamepadRepeat -= timeMult;
					if (_gamepadRepeat <= 0.0f) {
						_gamepadRepeat = 3.0f;
						apply = true;
					}
				}
				if (apply) {
					numberField->StepBy(float(change));
				}
			}
		}

		// The pad moves it by a tile of the layer being edited, repeated while held
		Vector2i step(std::int32_t(isHeld(ButtonName::Right)) - std::int32_t(isHeld(ButtonName::Left)),
			std::int32_t(isHeld(ButtonName::Down)) - std::int32_t(isHeld(ButtonName::Up)));
		if (!fieldTakesPad && (step.X != 0 || step.Y != 0)) {
			bool move = false;
			if (wasPressed(ButtonName::Up) || wasPressed(ButtonName::Down) || wasPressed(ButtonName::Left) || wasPressed(ButtonName::Right)) {
				_gamepadRepeat = 18.0f;
				move = true;
			} else {
				_gamepadRepeat -= timeMult;
				if (_gamepadRepeat <= 0.0f) {
					_gamepadRepeat = 5.0f;
					move = true;
				}
			}
			if (move) {
				LayerView view = GetLayerView(_currentLayer);
				Rectf cell = view.CellToScreen(view.WorldToCell(view.ScreenToWorld(cursor)) + step);
				cursor = Vector2f(cell.X + cell.W * 0.5f, cell.Y + cell.H * 0.5f);
				// The view follows a cursor that left it
				Rectf viewRect = GetViewRect();
				Vector2f follow;
				if (cursor.X < viewRect.X) {
					follow.X = cursor.X - viewRect.X - cell.W;
				} else if (cursor.X > viewRect.X + viewRect.W) {
					follow.X = cursor.X - (viewRect.X + viewRect.W) + cell.W;
				}
				if (cursor.Y < viewRect.Y) {
					follow.Y = cursor.Y - viewRect.Y - cell.H;
				} else if (cursor.Y > viewRect.Y + viewRect.H) {
					follow.Y = cursor.Y - (viewRect.Y + viewRect.H) + cell.H;
				}
				if (follow.X != 0.0f || follow.Y != 0.0f) {
					camera.PanBy(follow);
					cursor -= follow;
				}
			}
		}

		Vector2f screen = _surface->GetScreenSize();
		cursor.X = std::clamp(cursor.X, 0.0f, screen.X - 1.0f);
		cursor.Y = std::clamp(cursor.Y, 0.0f, screen.Y - 1.0f);
		bool cursorMoved = (cursor.X != _gamepadCursor.X || cursor.Y != _gamepadCursor.Y);
		_gamepadCursor = cursor;

		// The right stick moves the view, the triggers zoom at the cursor
		// Over a list that scrolls, the right stick scrolls it, it moves the view otherwise
		bool scrolled = false;
		if (std::abs(rightStick.Y) > 0.0f) {
			UiScrollView* scrollView = nullptr;
			for (UiElement* element = _surface->GetHover(); element != nullptr && scrollView == nullptr; element = element->GetParent()) {
				scrollView = runtime_cast<UiScrollView>(element);
			}
			if (scrollView != nullptr) {
				UiPointerEvent wheel;
				wheel.Pos = _gamepadCursor;
				wheel.Wheel = Vector2f(0.0f, -rightStick.Y * std::abs(rightStick.Y) * GamepadScrollSpeed * timeMult);
				scrolled = scrollView->OnWheel(wheel);
			}
		}
		if (rightStick.Length() > 0.0f && !scrolled) {
			_gamepadPanHold = std::min(_gamepadPanHold + timeMult, GamepadBoostFrames);
			float boost = 1.0f + (GamepadMaxBoost - 1.0f) * (_gamepadPanHold / GamepadBoostFrames);
			camera.PanBy(rightStick * (GamepadPanSpeed * scale * rightStick.Length() * boost * timeMult));
			cursorMoved = true;
		} else {
			_gamepadPanHold = 0.0f;
		}
		if (zoom != 0.0f) {
			// Smoothly, by a little every frame, the steps of the wheel would make it jump from one to another
			camera.ZoomBy(_gamepadCursor, std::exp(zoom * std::abs(zoom) * GamepadZoomRate * timeMult));
		}
		if (cursorMoved) {
			PointerMove(_gamepadCursor);
		}

		// A and B are the buttons of a mouse, B closes what is shown over the interface first
		if (wasPressed(ButtonName::A)) {
			PointerDown(_gamepadCursor, MouseButton::Left);
		}
		if (wasReleased(ButtonName::A)) {
			PointerUp(_gamepadCursor, MouseButton::Left);
		}
		if (wasPressed(ButtonName::B)) {
			if (_surface->HasOverlays()) {
				KeyboardEvent escape;
				escape.sym = Keys::Escape;
				escape.mod = 0;
				_surface->OnKeyPressed(escape);
			} else {
				PointerDown(_gamepadCursor, MouseButton::Right);
			}
		}
		if (wasReleased(ButtonName::B)) {
			PointerUp(_gamepadCursor, MouseButton::Right);
		}

		// X picks what is under the cursor, an event with the event tool and a tile otherwise
		if (wasPressed(ButtonName::X)) {
			if (_tools->GetChosenType() == EditorToolType::Event) {
				std::int32_t modifiers = _modifiers;
				_modifiers |= KeyMod::Alt;
				PointerDown(_gamepadCursor, MouseButton::Left);
				PointerUp(_gamepadCursor, MouseButton::Left);
				_modifiers = modifiers;
			} else {
				_tools->BeginTemporary(EditorToolType::Eyedropper);
				PointerDown(_gamepadCursor, MouseButton::Left);
				_gamepadPicking = true;
			}
		}
		if (wasReleased(ButtonName::X) && _gamepadPicking) {
			_gamepadPicking = false;
			PointerUp(_gamepadCursor, MouseButton::Left);
		}

		// Y goes through the tools when tapped, held it shows them in a ring around the cursor, the left stick picks one
		constexpr float RadialHoldFrames = 14.0f;
		if (wasPressed(ButtonName::Y) && !fieldTakesY) {
			_gamepadYTime = 0.0f;
		} else if (isHeld(ButtonName::Y) && _gamepadYTime >= 0.0f) {
			_gamepadYTime += timeMult;
			if (_gamepadYTime >= RadialHoldFrames && _radialMenu == nullptr) {
				ShowToolMenu(_gamepadCursor);
			}
		}
		if (_radialMenu != nullptr && isHeld(ButtonName::Y)) {
			_radialMenu->PointTo(leftStick);
		}
		if (wasReleased(ButtonName::Y) && _gamepadYTime >= 0.0f) {
			if (_radialMenu != nullptr) {
				_radialMenu->Confirm();
			} else if (_gamepadYTime < RadialHoldFrames) {
				static const char* const ToolCycle[] = { "tool.brush", "tool.eraser", "tool.rectangle", "tool.fill", "tool.select", "tool.event", "tool.hand" };
				static const EditorToolType ToolCycleTypes[] = { EditorToolType::Brush, EditorToolType::Eraser, EditorToolType::Rectangle,
					EditorToolType::Fill, EditorToolType::Select, EditorToolType::Event, EditorToolType::Hand };
				std::int32_t current = -1;
				for (std::int32_t i = 0; i < std::int32_t(arraySize(ToolCycleTypes)); i++) {
					if (ToolCycleTypes[i] == _tools->GetChosenType()) {
						current = i;
					}
				}
				_actions.Execute(ToolCycle[(current + 1) % std::int32_t(arraySize(ToolCycle))]);
			}
			_gamepadYTime = -1.0f;
		}

		if (wasPressed(ButtonName::LeftBumper)) {
			SetCurrentLayer(_currentLayer - 1);
		}
		if (wasPressed(ButtonName::RightBumper)) {
			SetCurrentLayer(_currentLayer + 1);
		}
		if (wasPressed(ButtonName::Back)) {
			Undo();
		}
		if (wasPressed(ButtonName::LeftStick)) {
			Redo();
		}
		if (wasPressed(ButtonName::Start)) {
			StartPlayTest(PlayStartMode::Resume);
		}
	}

	void EditorSession::PointerDown(Vector2f pos, MouseButton button)
	{
		_pointerPos = pos;
		_pointerKnown = true;

		if (_toolPointerDown || _panning) {
			return;
		}

		UiPointerEvent uiEvent;
		uiEvent.Pos = _pointerPos;
		uiEvent.Button = button;
		uiEvent.Modifiers = _modifiers;
		// The panels are only seen while a ring grows over them, they don't take what is meant for the game
		if (_mode != SessionMode::Edit && _playTransition.IsActive()) {
			return;
		}
		if (_surface->OnPointerDown(uiEvent) || _mode != SessionMode::Edit) {
			return;
		}

		if (button == MouseButton::Middle) {
			_panning = true;
			_editHandler->GetCamera().BeginDrag(_pointerPos);
			return;
		}
		if (button == MouseButton::Left || button == MouseButton::Right) {
			_toolPointerDown = true;
			_toolButton = button;
			_tools->GetActive()->OnPointerDown(MakeToolPointer(_pointerPos, button));
		}
	}

	void EditorSession::OnMouseUp(const MouseEvent& event)
	{
		if (_editHandler == nullptr) {
			return;
		}
		PointerUp(ToDrawable(event.x, event.y), event.button);
	}

	void EditorSession::PointerUp(Vector2f pos, MouseButton button)
	{
		_pointerPos = pos;

		if (_toolPointerDown && button == _toolButton) {
			_toolPointerDown = false;
			_tools->GetActive()->OnPointerUp(MakeToolPointer(_pointerPos, button));
			// A tool used for a while that waited for the release is done now
			if (!theApplication().GetInputManager().keyboardState().isKeyDown(Keys::Space)) {
				_tools->EndTemporary(EditorToolType::Hand);
			}
			if (!theApplication().GetInputManager().keyboardState().isKeyDown(Keys::LAlt)) {
				_tools->EndTemporary(EditorToolType::Eyedropper);
			}
			return;
		}
		if (_panning && button == MouseButton::Middle) {
			_panning = false;
			_editHandler->GetCamera().EndDrag();
			return;
		}

		UiPointerEvent uiEvent;
		uiEvent.Pos = _pointerPos;
		uiEvent.Button = button;
		uiEvent.Modifiers = _modifiers;
		_surface->OnPointerUp(uiEvent);
	}

	void EditorSession::OnMouseMove(const MouseState& state)
	{
		if (_editHandler == nullptr) {
			return;
		}
		Vector2f pos = ToDrawable(state.x, state.y);
		bool moved = ((pos - _mousePos).Length() >= 4.0f);
		_mousePos = pos;
		if (_inputModality == InputModality::Gamepad && !moved) {
			// The mouse barely moved, the gamepad is still used
			return;
		}
		SetInputModality(InputModality::KeyboardMouse);
		PointerMove(pos);
	}

	void EditorSession::PointerMove(Vector2f pos)
	{
		_pointerPos = pos;
		_pointerKnown = true;

		if (_toolPointerDown) {
			_tools->GetActive()->OnPointerMove(MakeToolPointer(_pointerPos, _toolButton));
			return;
		}
		if (_panning) {
			_editHandler->GetCamera().DragTo(_pointerPos);
			return;
		}

		UiPointerEvent uiEvent;
		uiEvent.Pos = _pointerPos;
		uiEvent.Modifiers = _modifiers;
		if (!_surface->OnPointerMove(uiEvent) && _mode == SessionMode::Edit) {
			_tools->GetActive()->OnPointerMove(MakeToolPointer(_pointerPos, _toolButton));
		}
	}

	void EditorSession::OnMouseWheel(const ScrollEvent& event)
	{
		if (_editHandler == nullptr) {
			return;
		}

		UiPointerEvent uiEvent;
		uiEvent.Pos = _pointerPos;
		uiEvent.Wheel = Vector2f(event.x, event.y);
		uiEvent.Modifiers = _modifiers;
		if (_surface->OnWheel(uiEvent) || _mode != SessionMode::Edit) {
			return;
		}

		// The wheel zooms, with Shift or Ctrl it moves the view, with Ctrl+Shift it sizes the interface and with
		// Alt it changes the layer; a horizontal wheel always moves the view
		bool ctrl = (_modifiers & KeyMod::Ctrl) != 0;
		bool shift = (_modifiers & KeyMod::Shift) != 0;
		bool alt = (_modifiers & KeyMod::Alt) != 0;
		if (event.y != 0.0f && ctrl && shift) {
			ChangeUiScale(event.y > 0.0f ? 1 : -1);
			return;
		}
		if (event.y != 0.0f && alt) {
			SetCurrentLayer(_currentLayer + (event.y > 0.0f ? -1 : 1));
			return;
		}

		EditorCamera& camera = _editHandler->GetCamera();
		Rectf view = GetViewRect();
		float step = std::round(std::min(view.W, view.H) * WheelPanFraction);
		Vector2f pan(event.x, 0.0f);
		if (shift) {
			pan.X -= event.y;
		} else if (ctrl) {
			pan.Y -= event.y;
		} else if (event.y != 0.0f) {
			camera.ZoomSteps(_pointerPos, event.y);
		}
		if (pan.X != 0.0f || pan.Y != 0.0f) {
			camera.PanBy(pan * step);
		}
	}

	void EditorSession::OnSuspend()
	{
		if (_history != nullptr) {
			_autosave.SaveNow(_document, *_history);
		}
		_settings.Save();
	}

	IStateHandler::CursorPolicy EditorSession::GetCursorPolicy() const
	{
		// While playing, as in the game
		return (_mode == SessionMode::Play ? CursorPolicy::Default : CursorPolicy::Visible);
	}

	void EditorSession::InvokeAsync(Function<void()>&& callback)
	{
		_root->InvokeAsync(std::move(callback));
	}

	void EditorSession::InvokeAsync(std::weak_ptr<void> reference, Function<void()>&& callback)
	{
		_root->InvokeAsync(std::move(reference), std::move(callback));
	}

	void EditorSession::GoToMainMenu(bool afterIntro)
	{
		if (_mode != SessionMode::Edit) {
			// A play-test returns to the editor instead
			ReturnFromPlayTest(_("Back in the editor"));
			return;
		}
		_root->GoToMainMenu(afterIntro);
	}

	void EditorSession::ChangeLevel(LevelInitialization&& levelInit)
	{
		// The level being edited is never left for another one, the play-test ends
		if (_mode != SessionMode::Edit) {
			StringView levelName = levelInit.LevelName;
			bool gameOver = levelName.hasSuffix(":gameover"_s);
			if (_playHandler != nullptr && !gameOver) {
				auto& segments = _playHandler->GetPlayPath().Segments;
				if (!segments.empty()) {
					segments.back().End = PlayPath::Break::Exited;
				}
			}
			ReturnFromPlayTest(gameOver ? _("Game over") : _("The level was finished"));
		}
	}

	bool EditorSession::HasResumableState() const
	{
		return false;
	}

	void EditorSession::ResumeSavedState()
	{
	}

	bool EditorSession::SaveCurrentStateIfAny()
	{
		// The editor never leaves a state to resume, nor does a play-test
		return false;
	}

	void EditorSession::ApplyCursorPolicy()
	{
		_root->ApplyCursorPolicy();
	}

#if defined(WITH_MULTIPLAYER)
	void EditorSession::ConnectToServer(StringView endpoint, std::uint16_t defaultPort, StringView password)
	{
	}

	bool EditorSession::CreateServer(Multiplayer::ServerInitialization&& serverInit)
	{
		return false;
	}
#endif

	void EditorSession::OpenEditor(EditorInitialization&& editorInit)
	{
		_root->OpenEditor(std::move(editorInit));
	}

	IRootController::Flags EditorSession::GetFlags() const
	{
		return _root->GetFlags();
	}

	StringView EditorSession::GetNewestVersion() const
	{
		return _root->GetNewestVersion();
	}

	float EditorSession::GetInitializationProgress() const
	{
		return _root->GetInitializationProgress();
	}

	void EditorSession::RefreshCacheLevels(bool recreateAll, Compatibility::ConversionProgress progress)
	{
	}

	void EditorSession::StartPlayTest(PlayStartMode mode)
	{
		if (_mode != SessionMode::Edit || _editHandler == nullptr || _editHandler->TileMap() == nullptr) {
			return;
		}

		_tools->GetActive()->Cancel();
		_toolPointerDown = false;
		_panning = false;
		if (UiSurface* surface = _surface.get()) {
			surface->ClosePopups();
			surface->SetFocus(nullptr);
		}

		EditorCamera& camera = _editHandler->GetCamera();
		_editCameraCenter = camera.GetCenter();
		_editCameraScale = camera.GetTargetScale();

		_playOptions = PlayTestOptions();
		_playOptions.Player = PlayerType(_settings.PlayCharacter);
		_playOptions.Difficulty = GameDifficulty(_settings.PlayDifficulty);
#if defined(WITH_MULTIPLAYER)
		_playOptions.GameMode = std::uint8_t(_settings.PlayGameMode);
		_playOptions.PlayerCount = std::clamp(_settings.PlayPlayerCount, 1, LevelInitialization::MaxPlayerCount);
#endif
		_resumePlay = false;

		if (mode == PlayStartMode::Resume && _playHandler == nullptr && _hasResumePosition) {
			// The play-test wasn't kept, it starts where the player was
			_playOptions.HasSpawnPosition = true;
			_playOptions.SpawnPosition = _resumePosition;
		} else if (mode == PlayStartMode::Resume && _playHandler != nullptr) {
			// Multiplayer chosen since, or another one, starts again too
			bool sameMode = (_playedOptions.GameMode == _playOptions.GameMode && (_playOptions.GameMode == 0 || _playedOptions.PlayerCount == _playOptions.PlayerCount));
			if (_history->GetRevision() == _playRevision && sameMode && _playHandler->GetViewport() != nullptr) {
				// Nothing changed, the play-test goes on exactly where it was left
				_resumePlay = true;
			} else if (Actors::Player* player = _playHandler->GetPlayer()) {
				// The level changed, so it starts again, but where the player was and with what the player had
				_playOptions.HasSpawnPosition = true;
				_playOptions.SpawnPosition = player->GetPos();
				if (sameMode && _playOptions.GameMode == 0) {
					_restoreFrom = _playHandler;
				}
			}
		} else if (mode == PlayStartMode::Here) {
			// At the pointer by the keyboard, chosen from a menu or by a button the pointer is there, so in the middle
			ToolPointer pointer;
			_playOptions.HasSpawnPosition = true;
			if (_handlingShortcut && GetHoverPointer(pointer)) {
				_playOptions.SpawnPosition = pointer.World;
			} else {
				Rectf view = GetViewRect();
				std::int32_t spriteLayer = std::max(_document.GetSpriteLayerIndex(), 0);
				_playOptions.SpawnPosition = GetLayerView(spriteLayer).ScreenToWorld(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f));
			}
		} else if (mode == PlayStartMode::Marker && _hasPlayStart) {
			_playOptions.HasSpawnPosition = true;
			_playOptions.SpawnPosition = Vector2f(float(_playStart.X), float(_playStart.Y));
		}

		if (!_resumePlay && _playHandler != nullptr) {
			// The kept play-test is replaced by a new one
			_playHandler = nullptr;
		}

		// The view flies first to where the game will show the player, as large as the game shows it, then the game is
		// revealed over it in a ring. The camera of the game follows the player, so it's close to it.
		bool hasTarget = false;
		Vector2f target;
		if (_resumePlay && _playHandler != nullptr && _playHandler->GetViewport() != nullptr) {
			target = _playHandler->GetViewport()->GetCameraCenter();
			hasTarget = true;
		} else if (_playOptions.HasSpawnPosition) {
			target = _playOptions.SpawnPosition;
			hasTarget = true;
		} else {
			// The start of the character played, as EventMap::AddSpawnPosition() places it, the game picks one of several
			std::int32_t width = std::max(_document.GetEventGridSize().X, 1);
			std::uint8_t playerBit = std::uint8_t(1 << std::clamp(_settings.PlayCharacter - 1, 0, 2));
			for (const DocumentEvent& event : _document.Events) {
				if (EventType(event.Record.Type) == EventType::LevelStart && (event.Record.Params[0] & playerBit) != 0) {
					target = Vector2f(float(event.Cell % std::uint32_t(width)) * 32.0f, float(event.Cell / std::uint32_t(width)) * 32.0f - 8.0f);
					hasTarget = true;
					break;
				}
			}
		}
		if (hasTarget) {
			// The camera of the game doesn't show anything past the edges of the level
			float scale = camera.GetGameplayScale();
			Vector2i viewSize = camera.GetViewSize();
			Vector2f halfView(viewSize.X * 0.5f / scale, viewSize.Y * 0.5f / scale);
			std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
			if (spriteLayer >= 0 && !_resumePlay) {
				Vector2f levelSize(_document.Layers[spriteLayer].GetWidth() * 32.0f, _document.Layers[spriteLayer].GetHeight() * 32.0f);
				target.X = (levelSize.X > halfView.X * 2.0f ? std::clamp(target.X, halfView.X, levelSize.X - halfView.X) : levelSize.X * 0.5f);
				target.Y = (levelSize.Y > halfView.Y * 2.0f ? std::clamp(target.Y, halfView.Y, levelSize.Y - halfView.Y) : levelSize.Y * 0.5f);
			}
			LOGD("Flying to {:.0f}, {:.0f} before the level is played", target.X, target.Y);
			camera.FlyTo(target, scale);
		} else {
			camera.Stop();
		}
		_mode = SessionMode::EnteringPlay;
		_enteringPlayFrameShown = false;
	}

	void EditorSession::SetPlayTestExit(IPlayTest& handler)
	{
		std::weak_ptr<IStateHandler> self = weak_from_this();
		handler.OnExitEditor = [this, self]() {
			_root->InvokeAsync(self, [this]() {
				StopPlayTest(false);
				ExitEditor();
			});
		};
	}

	void EditorSession::FinishStartPlayTest()
	{
		Vector2i res = theApplication().GetResolution();
		Viewport::GetChain().clear();
		_editHandler->SuspendRendering();

		if (_resumePlay && _playHandler != nullptr) {
			_playHandler->Resume();
		} else {
			// Alone, or a game mode of multiplayer in local splitscreen
			std::shared_ptr<IPlayTest> handler;
			bool initialized;
#if defined(WITH_MULTIPLAYER)
			if (_playOptions.GameMode != 0) {
				auto mpHandler = std::make_shared<PlayTestMpLevelHandler>(this, _playOptions);
				handler = mpHandler;
				SetPlayTestExit(*handler);
				initialized = mpHandler->Initialize(_document, _levelName, _filePath);
			} else
#endif
			{
				auto spHandler = std::make_shared<PlayTestLevelHandler>(this);
				handler = spHandler;
				SetPlayTestExit(*handler);
				initialized = spHandler->Initialize(_document, _levelName, _filePath, _playOptions);
			}
			if (!initialized) {
				_restoreFrom = nullptr;
				LOGE("Edited level cannot be played");
				_mode = SessionMode::Edit;
				OnInitializeViewport(res.X, res.Y);
				_editHandler->SetMusicPaused(false);
				ShowMessage(_("The level couldn't be played"));
				return;
			}
			_playHandler = std::move(handler);
			_playedOptions = _playOptions;
			if (_playHandler->HasSpawnFallenBack()) {
				ShowMessage(_("There's no room for the player there, it starts at the start of the level"));
			}
			if (_restoreFrom != nullptr) {
				RestorePlayState(*_restoreFrom, *_playHandler);
			}
		}
		_restoreFrom = nullptr;

		// The music goes on, it opens up while the game is revealed
#if defined(WITH_AUDIO)
		if (_editHandler->HandOverMusic(_playHandler->GetLevelHandler(), EditMusicLowPass)) {
			StartMusicFade(EditMusicLowPass, 1.0f);
		} else
#endif
		{
			_editHandler->SetMusicPaused(true);
		}

		_mode = SessionMode::Play;
		OnInitializeViewport(res.X, res.Y);
		_root->ApplyCursorPolicy();

		// The game is revealed in a ring growing over the last image of the editor, its panels are still drawn outside
		// of the ring while it grows, the game is drawn over them inside of it
		_playTransition.Start(_editHandler->GetFrameTexture(), _playHandler->GetFrameTexture(), GetAccentColor());
	}

	const Formats::TileSetData* EditorSession::GetTileSetData(StringView name)
	{
		if (name.hasPrefix(':')) {
			// Stored in the level itself, an undo can change it any time
			const Formats::ExtraTileSetRecord* record = nullptr;
			for (const Formats::ExtraTileSetRecord& tileSet : _document.ExtraTileSets) {
				if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) != 0 && tileSet.Name == name) {
					record = &tileSet;
					break;
				}
			}
			for (std::size_t i = 0; i < _tileSetData.size(); i++) {
				if (_tileSetData[i].Name == name) {
					if (record != nullptr && _tileSetData[i].Bytes == record->Data.data() && _tileSetData[i].ByteCount == record->Data.size()) {
						return _tileSetData[i].Data.get();
					}
					_tileSetData.erase(_tileSetData.begin() + i);
					break;
				}
			}
			if (record == nullptr) {
				return nullptr;
			}
			CachedTileSet& cached = _tileSetData.emplace_back();
			cached.Name = name;
			cached.Bytes = record->Data.data();
			cached.ByteCount = record->Data.size();
			MemoryStream ms(record->Data.data(), std::int64_t(record->Data.size()));
			auto tileSet = std::make_unique<Formats::TileSetData>();
			if (Formats::TileSetFile::Read(ms, *tileSet)) {
				cached.Data = std::move(tileSet);
			}
			return cached.Data.get();
		}

		for (const CachedTileSet& cached : _tileSetData) {
			if (cached.Name == name) {
				return cached.Data.get();
			}
		}
		// Also when it can't be read, so it isn't tried again and again
		CachedTileSet& cached = _tileSetData.emplace_back();
		cached.Name = name;
		String path = ContentTileSetLibrary::FindTileSetFile(name);
		if (!path.empty()) {
			auto s = fs::Open(path, FileAccess::Read);
			auto tileSet = std::make_unique<Formats::TileSetData>();
			if (s->IsValid() && Formats::TileSetFile::Read(*s, *tileSet)) {
				cached.Data = std::move(tileSet);
			}
		}
		return cached.Data.get();
	}

	void EditorSession::InvalidateTileSet(StringView name)
	{
		for (std::size_t i = 0; i < _tileSetData.size(); i++) {
			if (_tileSetData[i].Name == name) {
				_tileSetData.erase(_tileSetData.begin() + i);
				break;
			}
		}
		_tileSets.Invalidate();
		_tileLod->Invalidate();
	}

	void EditorSession::LoadEventExtensions(bool packs)
	{
		auto readText = [](StringView path) -> String {
			auto s = fs::Open(path, FileAccess::Read);
			std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
			if (size <= 0 || size > 4 * 1024 * 1024) {
				return {};
			}
			String text{NoInit, std::size_t(size)};
			s->Read(text.data(), size);
			return text;
		};

		std::int32_t count = 0;
		auto& resolver = ContentResolver::Get();
		if (packs) {
			// Event packs of the content, then of the user content, which can describe the same events differently
			EventCatalog::ClearExtensions(EventExtensionScope::Global);
			SmallVector<String, 2> directories;
			directories.push_back(fs::CombinePath({ resolver.GetContentPath(), "Editor"_s, "Events"_s }));
			StringView userContent = resolver.GetUserContentPath();
			if (!userContent.empty()) {
				directories.push_back(fs::CombinePath({ userContent, "Editor"_s, "Events"_s }));
			}
			for (const String& directory : directories) {
				if (!fs::DirectoryExists(directory)) {
					continue;
				}
				for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
					if (StringUtils::lowercase(item).hasSuffix(".json"_s)) {
						count += EventCatalog::LoadExtensions(readText(item), item, EventExtensionScope::Global);
					}
				}
			}
		}

		// The script of the level describes the events it reacts to, each by a line "#pragma editor-event {...}"
		EventCatalog::ClearExtensions(EventExtensionScope::Level);
		String scriptPath;
		if (!_filePath.empty()) {
			scriptPath = fs::CombinePath(fs::GetDirectoryName(_filePath), String(fs::GetFileNameWithoutExtension(_filePath) + ".j2as"_s));
		}
		if ((scriptPath.empty() || !fs::IsReadableFile(scriptPath)) && !_levelName.empty()) {
			scriptPath = resolver.FindContentFile(fs::CombinePath("Episodes"_s, String(fs::ToNativeSeparators(_levelName) + ".j2as"_s)));
		}
		if (!scriptPath.empty() && fs::IsReadableFile(scriptPath)) {
			String script = readText(scriptPath);
			constexpr StringView Pragma = "#pragma editor-event"_s;
			StringView rest = script;
			while (!rest.empty()) {
				auto parts = rest.partition('\n');
				StringView line = parts[0].trimmed();
				rest = parts[2];
				if (line.hasPrefix(Pragma)) {
					count += EventCatalog::LoadExtensions(line.exceptPrefix(Pragma.size()).trimmed(), scriptPath, EventExtensionScope::Level);
				}
			}
		}
		if (count > 0) {
			LOGI("{} events described from outside of the game", count);
		}
	}

	void EditorSession::BakeGroundLine()
	{
		constexpr std::int32_t Size = Formats::LevelFile::TileSize;
		auto* tool = static_cast<GroundLineTool*>(_tools->Get(EditorToolType::Ground));
		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(_document, splines);
		std::int32_t index = tool->GetSelectedSpline();
		if (index < 0 || index >= std::int32_t(splines.size())) {
			ShowMessage(_("Click a ground line with the Ground Line tool first"));
			return;
		}
		TerrainSpline spline = splines[index];
		if (spline.Kind == SplineKind::Closed) {
			ShowMessage(_("Tiles can be made along a line of ground, a ceiling or a band, not along a shape"));
			return;
		}
		if (spline.Layer < 0 || spline.Layer >= std::int32_t(_document.Layers.size()) || !IsLayerEditable(spline.Layer)) {
			ShowMessage(_("The layer is locked or hidden"));
			return;
		}

		// The inside of the ground, the flat tile the levels use the most, is cut to the line
		const TileShapes& shapes = GetTileShapes();
		SmartTileModel& model = GetSmartTiles();
		TileRef fullTile{};
		float best = -1.0f;
		for (const TileShapes::Surface& s : shapes.GetSurfaces()) {
			if (s.Left < 1.0f && s.Right < 1.0f && s.Error < 0.5f && s.Tile.Flags == 0) {
				float usage = float(model.GetSurfaceUsage(s.Tile));
				if (usage > best) {
					best = usage;
					fullTile = s.Tile;
				}
			}
		}
		const Formats::TileSetData* data = GetTileSetData(_document.Properties.DefaultTileSet);
		if (best < 0.0f || data == nullptr || fullTile.Index >= data->TileCount) {
			ShowMessage(_("The tile set has no solid tile to cut the ground from"));
			return;
		}
		const std::uint8_t* inside = &data->Images[std::size_t(fullTile.Index) * Formats::TileSetData::ImageBytesPerTile];
		bool trueColor = (data->TrueColor[fullTile.Index] != 0);

		// A tile for each cell the line goes through, solid on the side of the ground
		SmallVector<Vector2f, 0> line;
		GroundLineTool::SampleLine(spline, line);
		const DocumentLayer& layer = _document.Layers[spline.Layer];
		bool ceiling = (spline.Kind == SplineKind::Ceiling);
		SmallVector<Vector2i, 0> cells;
		Formats::TileSetData tiles;
		std::memcpy(tiles.Palette, data->Palette, sizeof(tiles.Palette));
		std::int32_t cx1 = std::int32_t(std::floor(line.front().X / 32.0f));
		std::int32_t cx2 = std::int32_t(std::floor((line.back().X - 0.5f) / 32.0f));
		for (std::int32_t cx = cx1; cx <= cx2; cx++) {
			float depths[Size];
			float minDepth = 1e30f, maxDepth = -1e30f;
			for (std::int32_t x = 0; x < Size; x++) {
				depths[x] = GroundLineTool::GetLineY(line, cx * 32.0f + float(x) + 0.5f);
				minDepth = std::min(minDepth, depths[x]);
				maxDepth = std::max(maxDepth, depths[x]);
			}
			for (std::int32_t cy = std::int32_t(std::floor(minDepth / 32.0f)); cy <= std::int32_t(std::floor(maxDepth / 32.0f)); cy++) {
				if (!layer.Contains(cx, cy)) {
					continue;
				}
				// Only cells the line crosses, partly solid
				bool any = false, all = true;
				for (std::int32_t x = 0; x < Size; x++) {
					float d = depths[x] - cy * 32.0f;
					bool solidTop = (ceiling ? d > 0.5f : d < 0.5f), solidBottom = (ceiling ? d > 31.5f : d < 31.5f);
					any |= (solidTop || solidBottom);
					all &= (solidTop && solidBottom);
				}
				if (!any || all) {
					continue;
				}
				std::int32_t tile = tiles.TileCount;
				tiles.Resize(tile + 1);
				tiles.TrueColor[tile] = (trueColor ? 1 : 0);
				std::uint8_t* image = &tiles.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
				std::uint8_t* mask = &tiles.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile];
				for (std::int32_t y = 0; y < Size; y++) {
					for (std::int32_t x = 0; x < Size; x++) {
						float d = depths[x] - cy * 32.0f;
						bool solid = (ceiling ? float(y) + 0.5f < d : float(y) + 0.5f >= d);
						if (!solid) {
							continue;
						}
						std::int32_t i = y * Size + x;
						if (trueColor) {
							std::memcpy(&image[i * 4], &inside[i * 4], 4);
						} else {
							image[i] = inside[i];
						}
						mask[i / 8] |= std::uint8_t(1 << (i % 8));
					}
				}
				cells.push_back(Vector2i(cx, cy));
			}
		}
		if (cells.empty()) {
			ShowMessage(_("The line goes along the edges of the tiles, no tile has to be cut"));
			return;
		}

		_history->BeginTransaction(String(_("Make tiles along ground line")));
		TileTarget target;
		target.Type = TileTarget::Kind::Level;
		TileRef first;
		if (!AppendTiles(*this, target, tiles, first)) {
			DocumentChanges changes;
			_history->CancelTransaction(changes);
			return;
		}
		// The line owns the new tiles, what was there before them stays what it gets back
		TerrainSpline::Read(_document, splines);
		auto command = std::make_unique<SetTilesCommand>(_document, spline.Layer, String(_("Make tiles along ground line")));
		for (std::size_t i = 0; i < cells.size(); i++) {
			Vector2i cell = cells[i];
			TileRef tile{ std::uint16_t(first.Index + i), first.Source, 0 };
			command->Set(cell.X, cell.Y, tile);
			bool owned = false;
			for (TerrainSpline::OwnedTile& entry : splines[index].Owned) {
				if (entry.X == cell.X && entry.Y == cell.Y) {
					entry.Written = tile;
					owned = true;
					break;
				}
			}
			if (!owned) {
				splines[index].Owned.push_back(TerrainSpline::OwnedTile{ cell.X, cell.Y, tile, _document.Layers[spline.Layer].GetTile(cell.X, cell.Y) });
			}
		}
		command->Seal();
		Execute(std::make_unique<SetTerrainSplinesCommand>(_document, splines, std::move(command), String(_("Make tiles along ground line"))));
		_history->EndTransaction();
		ShowMessage(_fn("{} tile was made for the line, only this level has it", "{} tiles were made for the line, only this level has them", std::int32_t(cells.size()), std::int32_t(cells.size())));
	}

	void EditorSession::ContinueRow()
	{
		// From the cell under the pointer, or after the last tile of a selection of one row
		constexpr std::int32_t MaxLength = 64;
		std::int32_t layerIndex = _currentLayer;
		if (layerIndex < 0 || layerIndex >= std::int32_t(_document.Layers.size()) || !IsLayerEditable(layerIndex)) {
			return;
		}
		Vector2i start;
		ToolPointer pointer;
		LayerView view = GetLayerView(layerIndex);
		if (!GetHoverPointer(pointer) || !view.Wrap(pointer.Cell, start)) {
			ShowMessage(_("Point at the cell the row goes on from"));
			return;
		}
		const DocumentLayer& layer = _document.Layers[layerIndex];
		// A row goes on from the tile left of where it starts
		if (!layer.GetTile(start.X, start.Y).IsEmpty()) {
			start.X++;
		}
		auto at = [&layer](std::int32_t x, std::int32_t y) -> const TileRef* {
			return (layer.Contains(x, y) && !layer.GetTile(x, y).IsEmpty() ? &layer.GetTile(x, y) : nullptr);
		};

		SmartTileModel& model = GetSmartTiles();
		auto command = std::make_unique<SetTilesCommand>(_document, layerIndex, String(_("Continue row")));
		TileRef previous;
		bool hasPrevious = (at(start.X - 1, start.Y) != nullptr);
		if (hasPrevious) {
			previous = *at(start.X - 1, start.Y);
		}
		SmallVector<TileRef, 8> suggested;
		std::int32_t placed = 0;
		for (std::int32_t x = start.X; x < start.X + MaxLength && layer.Contains(x, start.Y); x++) {
			if (!layer.GetTile(x, start.Y).IsEmpty()) {
				break;
			}
			if (model.Suggest(hasPrevious ? &previous : nullptr, at(x, start.Y - 1), nullptr, at(x, start.Y + 1), 1, suggested) == 0) {
				break;
			}
			command->Set(x, start.Y, suggested[0]);
			previous = suggested[0];
			hasPrevious = true;
			placed++;
		}
		if (placed == 0) {
			ShowMessage(model.IsReady() ? _("The levels have nothing that would go on from here") : _("Still learning the tiles from the levels, try again in a moment"));
			return;
		}
		Execute(std::move(command));
	}

	void EditorSession::RetileSelection()
	{
		if (_selection.IsEmpty()) {
			ShowMessage(_("Select the tiles to choose again first"));
			return;
		}
		SmartTileModel& model = GetSmartTiles();
		if (!model.IsReady()) {
			ShowMessage(_f("Still learning how the tiles fit - {}%, try it again in a moment", std::int32_t(model.GetProgress() * 100.0f)));
			return;
		}
		if (model.GetLevelCount() == 0) {
			ShowMessage(_("No level uses this tile set yet, so there's nothing to learn the tiles from"));
			return;
		}
		std::int32_t layerIndex = _selection.GetLayer();
		if (layerIndex < 0 || layerIndex >= std::int32_t(_document.Layers.size()) || !IsLayerEditable(layerIndex)) {
			ShowMessage(_("The layer is locked or hidden"));
			return;
		}

		// Each selected tile of the tile set gets the tile the levels use the most for its neighbors, the solid and
		// empty cells stay as they are; tiles of another tile set and animated ones are kept
		const DocumentLayer& layer = _document.Layers[layerIndex];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		auto command = std::make_unique<SetTilesCommand>(_document, layerIndex, _("Choose tiles by neighbors"));
		std::unordered_map<std::uint32_t, TileRef> decided;
		auto getTile = [&](std::int32_t x, std::int32_t y) {
			auto it = decided.find(std::uint32_t(y) * std::uint32_t(width) + std::uint32_t(x));
			return (it != decided.end() ? it->second : layer.GetTile(x, y));
		};
		auto isSolid = [&](std::int32_t x, std::int32_t y) {
			if (x < 0 || y < 0 || x >= width || y >= height) {
				return true;
			}
			TileRef tile = layer.GetTile(x, y);
			return (!tile.IsEmpty() && (tile.Flags & Formats::TileRecord::ModifierMask) != Formats::TileRecord::ModifierInvisible);
		};
		const Recti& bounds = _selection.GetBounds();
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (!_selection.Contains(x, y) || !SmartTileModel::IsModelTile(layer.GetTile(x, y))) {
					continue;
				}
				std::uint8_t neighbors = 0;
				static const std::int32_t Offsets[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
				for (std::int32_t i = 0; i < 8; i++) {
					if (isSolid(x + Offsets[i][0], y + Offsets[i][1])) {
						neighbors |= std::uint8_t(1 << i);
					}
				}
				auto neighborTile = [&](std::int32_t nx, std::int32_t ny, TileRef& result) {
					if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
						return false;
					}
					result = getTile(nx, ny);
					return SmartTileModel::IsModelTile(result);
				};
				TileRef left, up, right, down, picked;
				bool hasLeft = neighborTile(x - 1, y, left), hasUp = neighborTile(x, y - 1, up);
				bool hasRight = neighborTile(x + 1, y, right), hasDown = neighborTile(x, y + 1, down);
				std::uint32_t hash = (std::uint32_t(x) * 0x8DA6B343u) ^ (std::uint32_t(y) * 0xD8163841u);
				if (model.Pick(neighbors, layer.IsSpriteLayer(), hasLeft ? &left : nullptr, hasUp ? &up : nullptr,
					hasRight ? &right : nullptr, hasDown ? &down : nullptr, hash * 0x2C1B3C6Du, picked)) {
					decided[std::uint32_t(y) * std::uint32_t(width) + std::uint32_t(x)] = picked;
					command->Set(x, y, picked);
				}
			}
		}
		command->Seal();
		if (command->IsEmpty()) {
			ShowMessage(_("The tiles fit their neighbors already"));
			return;
		}
		Execute(std::move(command));
	}

	const TileShapes& EditorSession::GetTileShapes()
	{
		if (const Formats::TileSetData* data = GetTileSetData(_document.Properties.DefaultTileSet)) {
			// The slope tags of the rules are taken into account
			_tileShapes.Build(_document.Properties.DefaultTileSet, *data, &GetSmartTiles().GetRules());
		}
		return _tileShapes;
	}

	SmartTileModel& EditorSession::GetSmartTiles()
	{
		_smartTiles.Request(_document.Properties.DefaultTileSet, _tileSets);
		_smartTiles.GetRules().Resolve(_document);
		_smartTilesUsed = true;
		return _smartTiles;
	}

	void EditorSession::UpdateTileLod()
	{
		Tiles::TileMap* tileMap = GetTileMap();
		if (tileMap == nullptr) {
			return;
		}
		// From far away, the layers are drawn as images instead of tiles
		Tiles::ITileMapLayerOverride* lod = (_editHandler->GetCamera().GetScale() < TileLod::MaxScale ? _tileLod.get() : nullptr);
		Tiles::TileMapViewOptions viewOptions = tileMap->GetViewOptions();
		if (viewOptions.LayerOverride != lod) {
			viewOptions.LayerOverride = lod;
			tileMap->SetViewOptions(viewOptions);
		}
		_tileLod->BeginFrame();
	}

	Colorf EditorSession::GetAccentColor()
	{
		const Formats::TileSetData* tileSetData = GetTileSetData(_document.Properties.DefaultTileSet);
		if (tileSetData == nullptr) {
			return UiTheme::Accent;
		}

		// Only the tiles of the sprite layer, the player sees them the most, as often as they're there
		SmallVector<float, 0> weights;
		weights.resize(std::size_t(tileSetData->TileCount), 0.0f);
		auto count = [this, &weights](const TileRef& tile, float weight) {
			if (tile.Source == TileRef::DefaultTileSet && tile.Index > 0 && tile.Index < weights.size()) {
				weights[tile.Index] += weight;
			} else if (tile.Source == TileRef::Animated && tile.Index < _document.Animations.size()) {
				// The frames of an animated tile share its place
				const DocumentAnimation& animation = _document.Animations[tile.Index];
				for (const TileRef& frame : animation.Frames) {
					if (frame.Source == TileRef::DefaultTileSet && frame.Index > 0 && frame.Index < weights.size()) {
						weights[frame.Index] += weight / float(animation.Frames.size());
					}
				}
			}
		};
		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		bool any = false;
		if (spriteLayer >= 0) {
			for (const TileRef& tile : _document.Layers[spriteLayer].Tiles) {
				if (!tile.IsEmpty()) {
					count(tile, 1.0f);
					any = true;
				}
			}
		}

		bool levelPalette = (_document.Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette;
		Colorf color;
		if (!FindAccentColor(*tileSetData, levelPalette ? _document.LevelPalette : tileSetData->Palette,
			any ? ArrayView<const float>(weights) : ArrayView<const float>(), color)) {
			return UiTheme::Accent;
		}
		return color;
	}

	void EditorSession::StopPlayTest(bool keep)
	{
		if (_mode == SessionMode::EnteringPlay) {
			_mode = SessionMode::Edit;
			_editHandler->GetCamera().FlyTo(_editCameraCenter, _editCameraScale);
			return;
		}
		if (_mode != SessionMode::Play) {
			return;
		}

		_playTransition.Stop();
		Vector2f center = _editCameraCenter;
		bool musicHandedOver = false;
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		// Two levels at once don't fit, the play-test isn't kept, the next one starts where the player was
		if (keep && _playHandler != nullptr) {
			if (Actors::Player* player = _playHandler->GetPlayer()) {
				_resumePosition = player->GetPos();
				_hasResumePosition = true;
			}
			keep = false;
		}
#endif
		if (_playHandler != nullptr) {
			if (Rendering::PlayerViewport* viewport = _playHandler->GetViewport()) {
				center = viewport->GetCameraCenter();
			}
			_lastPlayPath = _playHandler->GetPlayPath();
#if defined(WITH_AUDIO)
			// Muffled again over a while, as the editor comes back
			musicHandedOver = _playHandler->GetLevelHandler().HandOverMusic(*_editHandler, 1.0f);
			if (musicHandedOver) {
				StartMusicFade(1.0f, EditMusicLowPass);
			}
#endif
		}

		// The editor is revealed in a ring growing over the last frame of the game, as the game was revealed
		Texture* lastFrame = (_playHandler != nullptr ? _playHandler->GetFrameTexture() : nullptr);
		if (lastFrame != nullptr) {
			_playTransition.Start(lastFrame, nullptr, GetAccentColor());
		}

		Viewport::GetChain().clear();
		if (keep && _playHandler != nullptr) {
			_playHandler->GetLevelHandler().SuspendRendering();
			_playHandler->Suspend();
			_playRevision = _history->GetRevision();
			RememberSuspendedLevel();
		} else if (_playHandler != nullptr) {
			// Not drawn anymore, but its last frame is shown until the ring is gone
			_playHandler->GetLevelHandler().SuspendRendering();
			_closingPlayHandler = std::move(_playHandler);
		}

		_mode = SessionMode::Edit;
		Vector2i res = theApplication().GetResolution();
		OnInitializeViewport(res.X, res.Y);
		if (!musicHandedOver) {
			_editHandler->SetMusicPaused(false);
		}

		// Back where the player was, at the zoom of the editor
		_editHandler->GetCamera().JumpTo(center, _editCameraScale);

		_root->ApplyCursorPolicy();
		UpdateWindowTitle();
	}

	void EditorSession::ReturnFromPlayTest(StringView message)
	{
		// Called from inside the frame of the play-test, which can't be destroyed there
		String text = message;
		std::weak_ptr<IStateHandler> self = weak_from_this();
		_root->InvokeAsync(self, [this, text]() {
			if (_mode != SessionMode::Edit) {
				StopPlayTest(false);
				ShowMessage(text);
			}
		});
	}

	void EditorSession::AttachSceneContent()
	{
		// The sprites of the events are a part of the level, layers in front of them cover them
		_editHandler->SetSceneContent([this](UI::Canvas& canvas) {
			if (_shell != nullptr) {
				_shell->DrawSceneEvents(canvas);
			}
		});
	}

	void EditorSession::RebuildRuntime()
	{
		if (_editHandler == nullptr) {
			return;
		}

		LOGD("Loading the edited level again");
		_maskOverlay.Invalidate();
		// The new one shows all layers as the document has them
		_overriddenLayer = -1;

		_tools->GetActive()->Cancel();
		EditorCamera& camera = _editHandler->GetCamera();
		Vector2f center = camera.GetCenter();
		float scale = camera.GetTargetScale();
		Tiles::TileMapViewOptions viewOptions = _editHandler->TileMap()->GetViewOptions();

		// The music goes on playing, taken out of the old one
#if defined(WITH_AUDIO)
		String musicPath;
		std::unique_ptr<AudioStreamPlayer> music = _editHandler->TakeMusic(musicPath);
#endif

		// The old one goes first, the resources it doesn't share with the new one are released while loading
		Viewport::GetChain().clear();
		_editHandler = nullptr;

		auto handler = std::make_shared<EditorLevelHandler>(this);
		handler->SetInitialCamera(center, scale);
		if (!handler->Initialize(_document, _levelName, _filePath)) {
			LOGE("Edited level cannot be shown");
			_root->GoToMainMenu(false);
			return;
		}
#if defined(WITH_AUDIO)
		handler->ContinueMusic(music, musicPath, handler->GetMusicLowPass());
#endif

		_editHandler = std::move(handler);
		AttachSceneContent();
		Vector2i res = theApplication().GetResolution();
		OnInitializeViewport(res.X, res.Y);
		// The flat layers stay as they were
		viewOptions.ReferenceViewSize = _editHandler->GetReferenceViewSize();
		_editHandler->TileMap()->SetViewOptions(viewOptions);
		RememberAppliedOverrides();
	}

	void EditorSession::UpdateLayerOverride(bool restore)
	{
		Tiles::TileMap* tileMap = GetTileMap();
		if (tileMap == nullptr) {
			return;
		}

		std::int32_t layerCount = std::int32_t(_document.Layers.size());

		// Layers that move by themselves stay where their offsets put them, as the document has them
		Tiles::TileMapViewOptions viewOptions = tileMap->GetViewOptions();
		if (viewOptions.PauseLayerMovement != _settings.PauseLayerMovement) {
			viewOptions.PauseLayerMovement = _settings.PauseLayerMovement;
			tileMap->SetViewOptions(viewOptions);
			if (viewOptions.PauseLayerMovement) {
				for (std::int32_t i = 0; i < layerCount && i < tileMap->GetLayerCount(); i++) {
					const DocumentLayer& documentLayer = _document.Layers[i];
					if (i != _overriddenLayer && (documentLayer.Record.AutoSpeedX != 0.0f || documentLayer.Record.AutoSpeedY != 0.0f)) {
						tileMap->SetLayerDescription(i, Tiles::TileMap::MakeLayerDescription(documentLayer.Record), RuntimeSync::IsLayerShown(documentLayer));
					}
				}
			}
		}

		std::int32_t layer = -1;
		if (!restore && _mode == SessionMode::Edit && _currentLayer >= 0 && _currentLayer < layerCount &&
			NeedsLayerOverride(_document.Layers[_currentLayer])) {
			layer = _currentLayer;
		}
		if (layer == _overriddenLayer) {
			return;
		}

		if (_overriddenLayer >= 0 && _overriddenLayer < layerCount && _overriddenLayer < tileMap->GetLayerCount()) {
			// As the game draws it, with what was changed while it was edited
			const DocumentLayer& previous = _document.Layers[_overriddenLayer];
			tileMap->SetLayerDescription(_overriddenLayer, Tiles::TileMap::MakeLayerDescription(previous.Record), RuntimeSync::IsLayerShown(previous));
			tileMap->InvalidateTexturedBackground();
		}
		_overriddenLayer = layer;
		if (layer >= 0 && layer < tileMap->GetLayerCount()) {
			const DocumentLayer& current = _document.Layers[layer];
			Tiles::LayerDescription description = Tiles::TileMap::MakeLayerDescription(current.Record);
			if (RuntimeSync::IsTexturedBackground(current)) {
				description.RendererType = Tiles::LayerRendererType::Default;
				// The color of a sky is its horizon and stars, the tiles are shown as they are
				description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
			}
			// A layer that moves by itself couldn't be painted on
			description.AutoSpeedX = 0.0f;
			description.AutoSpeedY = 0.0f;
			tileMap->SetLayerDescription(layer, description, RuntimeSync::IsLayerShown(current));
		}
	}

	bool EditorSession::NeedsLayerOverride(const DocumentLayer& layer)
	{
		return (RuntimeSync::IsTexturedBackground(layer) || layer.Record.AutoSpeedX != 0.0f || layer.Record.AutoSpeedY != 0.0f);
	}

	ToolPointer EditorSession::MakeToolPointer(Vector2f pos, MouseButton button)
	{
		ToolPointer pointer;
		pointer.Screen = pos;
		pointer.Button = button;
		pointer.Modifiers = _modifiers;
		LayerView view = GetLayerView(_currentLayer);
		pointer.World = view.ScreenToWorld(pos);
		pointer.Cell = view.WorldToCell(pointer.World);
		return pointer;
	}

	Rectf EditorSession::GetViewRect() const
	{
		if (_shell != nullptr) {
			return _shell->GetViewRect();
		}
		Vector2i size = GetViewSize();
		return Rectf(0.0f, 0.0f, float(size.X), float(size.Y));
	}

	bool EditorSession::IsEditingEvents() const
	{
		return (_tools != nullptr && _tools->GetChosenType() == EditorToolType::Event);
	}

	bool EditorSession::HasSelection() const
	{
		if (IsEditingEvents()) {
			return (!_eventSelection.IsEmpty() || _eventSelection.HasOffGrid());
		}
		return (!_selection.IsEmpty() && _selection.GetLayer() == _currentLayer);
	}

	void EditorSession::ClearSelection()
	{
		if (IsEditingEvents()) {
			_eventSelection.Clear();
		} else {
			_selection.Clear();
		}
	}

	void EditorSession::CopySelection(bool cut)
	{
		if (IsEditingEvents()) {
			if (_eventSelection.HasOffGrid() && _eventSelection.GetOffGrid() < std::int32_t(_document.OffGridEvents.size())) {
				// Pasted as a single event on the grid, the clipboard holds them only so
				EventStamp stamp = EventStamp::FromEvent(_document.OffGridEvents[_eventSelection.GetOffGrid()].Event);
				theApplication().GetInputManager().setClipboardText(EventStampToClipboardText(stamp));
				_eventClipboard = std::move(stamp);
				if (cut) {
					DeleteSelection();
				} else {
					ShowMessage(_("Copied"));
				}
				return;
			}
			if (_eventSelection.IsEmpty()) {
				return;
			}
			// Placed relative to the top left of the copied events
			Vector2i gridSize = _document.GetEventGridSize();
			Vector2i origin(INT32_MAX, INT32_MAX);
			for (std::uint32_t cell : _eventSelection.GetCells()) {
				origin.X = std::min(origin.X, std::int32_t(cell % std::uint32_t(gridSize.X)));
				origin.Y = std::min(origin.Y, std::int32_t(cell / std::uint32_t(gridSize.X)));
			}
			EventStamp stamp;
			for (std::uint32_t cell : _eventSelection.GetCells()) {
				Vector2i pos(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
				EventStamp::Item& item = stamp.Items.emplace_back();
				item.Offset = pos - origin;
				item.Event = _document.GetEvent(pos.X, pos.Y);
			}
			theApplication().GetInputManager().setClipboardText(EventStampToClipboardText(stamp));
			_eventClipboard = std::move(stamp);
			if (cut) {
				DeleteSelection();
			} else {
				ShowMessage(_("Copied"));
			}
			return;
		}

		if (_selection.IsEmpty() || _selection.GetLayer() != _currentLayer) {
			return;
		}
		const DocumentLayer& layer = _document.Layers[_currentLayer];
		const Recti& bounds = _selection.GetBounds();
		TileStamp stamp = TileStamp::FromLayer(layer, bounds);
		for (std::int32_t y = 0; y < bounds.H; y++) {
			for (std::int32_t x = 0; x < bounds.W; x++) {
				if (!_selection.Contains(bounds.X + x, bounds.Y + y)) {
					stamp.Present[std::size_t(y) * bounds.W + x] = false;
				}
			}
		}
		theApplication().GetInputManager().setClipboardText(StampToClipboardText(stamp));
		_clipboard = std::move(stamp);

		if (cut) {
			DeleteSelection();
		} else {
			ShowMessage(_("Copied"));
		}
	}

	void EditorSession::DeleteSelection()
	{
		if (IsEditingEvents()) {
			if (_eventSelection.IsEmpty() && !_eventSelection.HasOffGrid()) {
				return;
			}
			if (!IsLayerEditable(_document.GetSpriteLayerIndex())) {
				ShowMessage(_("The sprite layer is locked or hidden"));
				return;
			}
			if (_eventSelection.HasOffGrid()) {
				std::int32_t index = _eventSelection.GetOffGrid();
				_eventSelection.Clear();
				if (index < std::int32_t(_document.OffGridEvents.size())) {
					SmallVector<Formats::OffGridEventRecord, 0> events;
					for (std::size_t i = 0; i < _document.OffGridEvents.size(); i++) {
						if (std::int32_t(i) != index) {
							events.push_back(_document.OffGridEvents[i]);
						}
					}
					Execute(std::make_unique<SetOffGridEventsCommand>(_document, std::move(events), _("Delete Events")));
				}
				return;
			}
			Vector2i gridSize = _document.GetEventGridSize();
			auto command = std::make_unique<SetEventsCommand>(_document, _("Delete Events"));
			for (std::uint32_t cell : _eventSelection.GetCells()) {
				command->Set(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)), LevelDocument::GetEmptyEvent());
			}
			_eventSelection.Clear();
			Execute(std::move(command));
			return;
		}

		if (_selection.IsEmpty() || _selection.GetLayer() != _currentLayer || !IsLayerEditable(_currentLayer)) {
			return;
		}
		const Recti& bounds = _selection.GetBounds();
		auto command = std::make_unique<SetTilesCommand>(_document, _currentLayer, _("Delete"));
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (_selection.Contains(x, y)) {
					command->Set(x, y, TileRef{});
				}
			}
		}
		Execute(std::move(command));
	}

	void EditorSession::Paste()
	{
		if (IsEditingEvents()) {
			EventStamp stamp;
			String text = theApplication().GetInputManager().getClipboardText();
			if (!ClipboardTextToEventStamp(text, stamp)) {
				stamp = _eventClipboard;
			}
			if (stamp.IsEmpty()) {
				return;
			}
			std::int32_t remapped = stamp.RemapLinkedIds(_document);
			SetEventBrush(std::move(stamp));
			ShowMessage(remapped > 0 ? _("Click to place what was copied, its warps and triggers got new identifiers") : _("Click to place what was copied"));
			return;
		}

		TileStamp stamp;
		String text = theApplication().GetInputManager().getClipboardText();
		if (!ClipboardTextToStamp(text, stamp)) {
			stamp = _clipboard;
		}
		if (stamp.IsEmpty()) {
			return;
		}
		_stamp = std::move(stamp);
		_selection.Clear();
		_tools->SetActive(EditorToolType::Brush);
		ShowMessage(_("Click to place what was copied"));
	}

	void EditorSession::SelectAll()
	{
		if (IsEditingEvents()) {
			SmallVector<std::uint32_t, 0> cells;
			for (const DocumentEvent& event : _document.Events) {
				cells.push_back(event.Cell);
			}
			_eventSelection.Assign(std::move(cells));
			return;
		}

		if (_currentLayer < 0 || _currentLayer >= std::int32_t(_document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = _document.Layers[_currentLayer];
		_selection.Select(_currentLayer, Vector2i(layer.GetWidth(), layer.GetHeight()), Recti(0, 0, layer.GetWidth(), layer.GetHeight()), false);
	}

	void EditorSession::AddLayer()
	{
		if (_document.Layers.size() >= UINT8_MAX) {
			return;
		}

		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		Vector2i size = (spriteLayer >= 0 ? Vector2i(_document.Layers[spriteLayer].GetWidth(), _document.Layers[spriteLayer].GetHeight()) : Vector2i(64, 64));

		auto command = SnapshotCommand::Begin(_document, SnapshotParts::Layers | SnapshotParts::Chunks, _("Add Layer"));
		DocumentLayer layer;
		std::memset(&layer.Record, 0, sizeof(layer.Record));
		layer.Record.Kind = Formats::LayerKind::Other;
		layer.Record.Flags = Formats::LayerRecord::Visible;
		layer.Record.Width = size.X;
		layer.Record.Height = size.Y;
		layer.Record.SpeedX = 1.0f;
		layer.Record.SpeedY = 1.0f;
		layer.Record.Depth = (_currentLayer < std::int32_t(_document.Layers.size()) ? _document.Layers[_currentLayer].Record.Depth : 0);
		std::memset(layer.Record.Color, 255, sizeof(layer.Record.Color));
		layer.Name = _("New Layer");
		layer.Tiles.resize(std::size_t(size.X) * std::size_t(size.Y), TileRef{});

		std::int32_t index = std::min(_currentLayer + 1, std::int32_t(_document.Layers.size()));
		SmallVector<std::int32_t, 16> newIndices;
		for (std::int32_t i = 0; i < std::int32_t(_document.Layers.size()); i++) {
			newIndices.push_back(i < index ? i : i + 1);
		}
		_document.Layers.insert(_document.Layers.begin() + index, std::move(layer));
		TerrainSpline::RemapLayers(_document, newIndices);

		DocumentChanges changes;
		command->End(_document, changes);
		_history->Record(std::move(command));
		_currentLayer = index;
		OnDocumentChanged(changes);
	}

	void EditorSession::SnapOffGridEvents()
	{
		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		if (_document.OffGridEvents.empty() || spriteLayer < 0) {
			return;
		}
		if (!IsLayerEditable(spriteLayer)) {
			ShowMessage(_("The sprite layer is locked or hidden"));
			return;
		}

		Vector2i gridSize = _document.GetEventGridSize();
		auto placed = std::make_unique<SetEventsCommand>(_document, _("Snap Off-Grid Events"));
		SmallVector<Formats::OffGridEventRecord, 0> kept;
		SmallVector<std::uint32_t, 0> taken;
		for (const Formats::OffGridEventRecord& event : _document.OffGridEvents) {
			std::int32_t x = std::int32_t(event.X / 32), y = std::int32_t(event.Y / 32);
			std::uint32_t cell = std::uint32_t(x + y * gridSize.X);
			bool free = (x < gridSize.X && y < gridSize.Y && LevelDocument::IsEmptyEvent(_document.GetEvent(x, y)) &&
				std::find(taken.begin(), taken.end(), cell) == taken.end());
			if (free) {
				placed->Set(x, y, event.Event);
				taken.push_back(cell);
			} else {
				kept.push_back(event);
			}
		}
		if (taken.empty()) {
			ShowMessage(_("The tiles of all off-grid events have an event already"));
			return;
		}

		std::int32_t keptCount = std::int32_t(kept.size());
		// The indices of those off the grid change
		_eventSelection.Clear();
		_history->BeginTransaction(_("Snap Off-Grid Events"));
		Execute(std::move(placed));
		Execute(std::make_unique<SetOffGridEventsCommand>(_document, std::move(kept), _("Snap Off-Grid Events")));
		_history->EndTransaction();
		ShowMessage(keptCount == 0
			? _fn("{} event was moved onto the grid", "{} events were moved onto the grid", std::int32_t(taken.size()), std::int32_t(taken.size()))
			: _f("{} events were moved onto the grid, {} stay off it, their tiles have an event already", std::int32_t(taken.size()), keptCount));
	}

	String EditorSession::GetHotkeysPath()
	{
		return fs::CombinePath(PreferencesCache::GetDirectory(), "Jazz2.EditorHotkeys.json"_s);
	}

	void EditorSession::SelectEventsOfSameType()
	{
		Vector2i gridSize = _document.GetEventGridSize();
		if (_eventSelection.IsEmpty() || gridSize.X <= 0) {
			return;
		}
		std::uint32_t first = _eventSelection.GetCells()[0];
		std::uint16_t type = _document.GetEvent(std::int32_t(first % std::uint32_t(gridSize.X)), std::int32_t(first / std::uint32_t(gridSize.X))).Type;
		SmallVector<std::uint32_t, 0> cells;
		for (const DocumentEvent& event : _document.Events) {
			if (event.Record.Type == type) {
				cells.push_back(event.Cell);
			}
		}
		std::int32_t count = std::int32_t(cells.size());
		_eventSelection.Assign(std::move(cells));
		ShowMessage(_fn("{} event selected", "{} events selected", count, count));
	}

	void EditorSession::CopyEventToMatchingTiles()
	{
		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		Vector2i gridSize = _document.GetEventGridSize();
		if (_eventSelection.GetCount() != 1 || spriteLayer < 0 || gridSize.X <= 0) {
			return;
		}
		if (!IsLayerEditable(spriteLayer)) {
			ShowMessage(_("The sprite layer is locked or hidden"));
			return;
		}
		std::uint32_t cell = _eventSelection.GetCells()[0];
		std::int32_t cx = std::int32_t(cell % std::uint32_t(gridSize.X)), cy = std::int32_t(cell / std::uint32_t(gridSize.X));
		const DocumentLayer& layer = _document.Layers[spriteLayer];
		if (!layer.Contains(cx, cy)) {
			return;
		}
		TileRef tile = layer.GetTile(cx, cy);
		if (tile.IsEmpty()) {
			ShowMessage(_("The event isn't on a tile"));
			return;
		}

		// Flipped tiles are left out, an event that makes the tile do something often depends on which way it faces
		Formats::EventRecord event = _document.GetEvent(cx, cy);
		auto command = std::make_unique<SetEventsCommand>(_document, _("Copy Event to Tiles"));
		SmallVector<std::uint32_t, 0> cells;
		std::int32_t taken = 0;
		std::int32_t height = std::min(layer.GetHeight(), gridSize.Y);
		std::int32_t width = std::min(layer.GetWidth(), gridSize.X);
		for (std::int32_t y = 0; y < height; y++) {
			for (std::int32_t x = 0; x < width; x++) {
				if (layer.GetTile(x, y) != tile || (x == cx && y == cy)) {
					continue;
				}
				if (!LevelDocument::IsEmptyEvent(_document.GetEvent(x, y))) {
					taken++;
					continue;
				}
				command->Set(x, y, event);
				cells.push_back(std::uint32_t(x + y * gridSize.X));
			}
		}
		if (cells.empty()) {
			ShowMessage(taken > 0 ? _("All other tiles like this one have an event already") : _("No other tile is like this one"));
			return;
		}
		std::int32_t count = std::int32_t(cells.size());
		Execute(std::move(command));
		cells.push_back(cell);
		_eventSelection.Assign(std::move(cells));
		ShowMessage(taken == 0
			? _fn("Copied to {} tile", "Copied to {} tiles", count, count)
			: _f("Copied to {} tiles, {} have an event already", count, taken));
	}

	void EditorSession::RemoveLayer()
	{
		if (_currentLayer < 0 || _currentLayer >= std::int32_t(_document.Layers.size()) || _document.Layers[_currentLayer].IsSpriteLayer()) {
			return;
		}

		auto command = SnapshotCommand::Begin(_document, SnapshotParts::Layers | SnapshotParts::Chunks, _("Remove Layer"));
		SmallVector<std::int32_t, 16> newIndices;
		for (std::int32_t i = 0; i < std::int32_t(_document.Layers.size()); i++) {
			newIndices.push_back(i < _currentLayer ? i : (i == _currentLayer ? -1 : i - 1));
		}
		_document.Layers.erase(_document.Layers.begin() + _currentLayer);
		TerrainSpline::RemapLayers(_document, newIndices);
		DocumentChanges changes;
		command->End(_document, changes);
		_history->Record(std::move(command));
		_currentLayer = std::min(_currentLayer, std::int32_t(_document.Layers.size()) - 1);
		OnDocumentChanged(changes);
	}

	void EditorSession::MoveLayer(std::int32_t direction)
	{
		MoveLayerTo(_currentLayer, _currentLayer + direction);
	}

	void EditorSession::MoveLayerTo(std::int32_t from, std::int32_t to)
	{
		std::int32_t count = std::int32_t(_document.Layers.size());
		if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
			return;
		}

		auto command = SnapshotCommand::Begin(_document, SnapshotParts::Layers | SnapshotParts::Chunks, _("Move Layer"));
		DocumentLayer layer = std::move(_document.Layers[from]);
		_document.Layers.erase(_document.Layers.begin() + from);
		_document.Layers.insert(_document.Layers.begin() + to, std::move(layer));
		// Ground lines stay on their layers
		SmallVector<std::int32_t, 16> order;
		for (std::int32_t i = 0; i < count; i++) {
			order.push_back(i);
		}
		std::int32_t moved = order[from];
		order.erase(order.begin() + from);
		order.insert(order.begin() + to, moved);
		SmallVector<std::int32_t, 16> newIndices;
		newIndices.resize(std::size_t(count));
		for (std::int32_t i = 0; i < count; i++) {
			newIndices[std::size_t(order[i])] = i;
		}
		TerrainSpline::RemapLayers(_document, newIndices);
		DocumentChanges changes;
		command->End(_document, changes);
		_history->Record(std::move(command));
		// The moved layer is the one edited then
		_currentLayer = to;
		OnDocumentChanged(changes);
	}

	void EditorSession::Save()
	{
		if (!CanSaveInPlace()) {
			// A level of the game itself is saved as a new one, so the original stays as it is
			ShowSaveAsDialog(false);
			return;
		}
		if (SaveTo(_filePath, false)) {
			ShowMessage(_("Saved"));
		} else {
			ShowMessage(_("The level couldn't be saved"));
		}
	}

	void EditorSession::ExitEditor()
	{
		ConfirmDiscard([this]() {
			_root->GoToMainMenu(false);
		});
	}

	void EditorSession::ConfirmDiscard(Function<void()>&& proceed)
	{
		if (!_history->IsDirty()) {
			proceed();
			return;
		}

		auto sharedProceed = std::make_shared<Function<void()>>(std::move(proceed));
		UiMessageDialog::Show(*_surface, _("Unsaved Changes"), _("The level was changed since it was saved.\nDo you want to save the changes?"),
			{ _("Save"), _("Don't Save"), _("Cancel") }, [this, sharedProceed](std::int32_t button) {
			if (button == 0) {
				if (CanSaveInPlace()) {
					if (SaveTo(_filePath, false)) {
						(*sharedProceed)();
					} else {
						ShowMessage(_("The level couldn't be saved"));
					}
				} else {
					ShowSaveAsDialog(false, [sharedProceed]() { (*sharedProceed)(); });
				}
			} else if (button == 1) {
				_autosave.Discard();
				(*sharedProceed)();
			}
		});
	}

	void EditorSession::ShowOpenDialog()
	{
		SmallVector<UiFileRoot, 4> roots;
		GetFileRoots(false, roots);
		String directory = (!_filePath.empty() ? String(fs::GetDirectoryName(_filePath)) : String());
		auto open = [this](StringView path) {
			// Told here, rather than leaving this session for a level that won't be opened
			String problem = GetOpenProblem(path);
			if (!problem.empty()) {
				ShowMessage(problem);
				return;
			}
			// Opened in a new session, so nothing of this one is left behind
			EditorInitialization editorInit;
			editorInit.FilePath = path;
			_root->OpenEditor(std::move(editorInit));
		};
		UiFileDialog* dialog = UiFileDialog::Show(*_surface, _("Open Level"), UiFileDialogMode::Open, std::move(roots), ".j2l"_s, directory, {}, open);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		// The browser has the files of the computer, a level is uploaded into the game
		dialog->GetContent()->Add<UiButton>(_("From This Computer..."), EditorIcon::Folder, [this, dialog, open]() mutable {
			dialog->Close();
			WebFileBridge::PickFile(".j2l"_s, std::move(open));
		})->Tooltip = _("Upload a level, saving it downloads it back");
#else
		static_cast<void>(dialog);
#endif
	}

	void EditorSession::ShowExportDialog()
	{
		String problem;
		if (!_document.Validate(&problem)) {
			UiMessageDialog::Show(*_surface, _("Export Level"), _f("The level can't be exported:\n{}", _(problem.data())), { _("OK") }, {});
			return;
		}

		SmallVector<String, 4> warnings;
		CollectExportWarnings(warnings);
		if (warnings.empty()) {
			ShowSaveAsDialog(true);
			return;
		}

		String text;
		for (const String& warning : warnings) {
			text += "- "_s + warning + "\n"_s;
		}
		text += _("Do you want to export it anyway?");
		UiMessageDialog::Show(*_surface, _("Export Level"), text, { _("Export Anyway"), _("Cancel") }, [this](std::int32_t button) {
			if (button == 0) {
				ShowSaveAsDialog(true);
			}
		});
	}

	void EditorSession::CollectExportWarnings(SmallVectorImpl<String>& warnings)
	{
		constexpr std::uint16_t LevelStartEvent = 1;
		bool hasStart = false;
		for (const DocumentEvent& event : _document.Events) {
			if (event.Record.Type == LevelStartEvent) {
				hasStart = true;
				break;
			}
		}
		for (const Formats::OffGridEventRecord& event : _document.OffGridEvents) {
			if (event.Event.Type == LevelStartEvent) {
				hasStart = true;
				break;
			}
		}
		if (!hasStart) {
			warnings.push_back(_("The level has no start position for players."));
		}

		if (_tileSets.GetTileCount(_document.Properties.DefaultTileSet) < 0) {
			warnings.push_back(_f("The tile set \"{}\" doesn't exist.", _document.Properties.DefaultTileSet));
		}
		for (const Formats::ExtraTileSetRecord& tileSet : _document.ExtraTileSets) {
			if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) == 0 && _tileSets.GetTileCount(tileSet.Name) < 0) {
				warnings.push_back(_f("The tile set \"{}\" doesn't exist.", tileSet.Name));
			}
		}

		auto& resolver = ContentResolver::Get();
		auto episode = _levelName.partition('/')[0];
		for (const String* next : { &_document.Properties.NextLevel, &_document.Properties.SecretLevel, &_document.Properties.BonusLevel }) {
			if (next->empty() || next->hasPrefix(':')) {
				continue;
			}
			String fullName = (next->contains('/') ? *next : String(episode + "/"_s + *next));
			if (!resolver.LevelExists(fullName)) {
				warnings.push_back(_f("The level \"{}\" that follows doesn't exist.", *next));
			}
		}
	}

	void EditorSession::ChangeUiScale(std::int32_t direction)
	{
		// Quarter steps where a step is felt most, larger ones above, sizes between whole ones make the text a bit soft
		static const float Steps[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f };
		if (direction == 0) {
			_settings.UiScale = 0.0f;
		} else {
			float current = _surface->GetScale();
			float next = current;
			if (direction > 0) {
				for (float step : Steps) {
					if (step > current + 0.01f) {
						next = step;
						break;
					}
				}
			} else {
				for (std::size_t i = std::size(Steps); i > 0; i--) {
					if (Steps[i - 1] < current - 0.01f) {
						next = Steps[i - 1];
						break;
					}
				}
			}
			_settings.UiScale = next;
		}
		_settings.Save();
		Vector2i res = theApplication().GetResolution();
		OnInitializeViewport(res.X, res.Y);
		ShowMessage(_f("Interface size {}%", std::int32_t(std::round(_surface->GetScale() * 100.0f))));
	}

	void EditorSession::OpenFile(StringView path)
	{
		String pathCopy = path;
		ConfirmDiscard([this, pathCopy]() {
			EditorInitialization editorInit;
			editorInit.FilePath = pathCopy;
			_root->OpenEditor(std::move(editorInit));
		});
	}

	bool EditorSession::OfferRecovery()
	{
		String recoveryPath = AutosaveService::FindRecovery(_filePath);
		if (recoveryPath.empty()) {
			return false;
		}

		UiMessageDialog::Show(*_surface, _("Unsaved Changes"), _("Changes of this level that weren't saved were found.\nDo you want to restore them?"),
			{ _("Restore"), _("Discard") }, [this, recoveryPath](std::int32_t button) {
			if (button != 0) {
				_autosave.Discard();
				return;
			}
			LevelDocument document;
			auto s = fs::Open(recoveryPath, FileAccess::Read);
			if (!s->IsValid() || !document.Load(*s, &_tileSets)) {
				ShowMessage(_("The changes couldn't be restored"));
				return;
			}
			s = nullptr;
			document.SetChunk(AutosaveService::RecoveryChunk, {});
			String filePath = _filePath, levelName = _levelName;
			ReplaceDocument(std::move(document), filePath, levelName);
			// Still not saved
			_history->MarkDirty();
			UpdateWindowTitle();
		});
		return true;
	}

	void EditorSession::ShowSaveAsDialog(bool exportOnly, Function<void()>&& onSaved)
	{
		SmallVector<UiFileRoot, 4> roots;
		GetFileRoots(true, roots);
		if (roots.empty()) {
			ShowMessage(_("There is no place to save levels to"));
			return;
		}

		// A level of the game goes into the same episode of the user content
		String directory;
		if (CanSaveInPlace()) {
			directory = fs::GetDirectoryName(_filePath);
		} else {
			auto p = _levelName.partition('/');
			directory = fs::CombinePath(roots[0].Path, p[0]);
		}
		String name = (!_filePath.empty() ? String(fs::GetFileName(_filePath)) : String("untitled.j2l"_s));

		auto sharedOnSaved = std::make_shared<Function<void()>>(std::move(onSaved));
		UiFileDialog* fileDialog = UiFileDialog::Show(*_surface, exportOnly ? _("Export Level") : _("Save Level As"), UiFileDialogMode::Save, std::move(roots), ".j2l"_s,
			directory, name, [this, exportOnly, sharedOnSaved](StringView chosenPath) {
			String path = chosenPath;
			auto save = [this, exportOnly, sharedOnSaved, path]() {
				SaveAsConfirmed(path, exportOnly, sharedOnSaved);
			};

			// The same level in another place either hides the new file or is hidden by it when the game is played
			String warning;
			SmallVector<UiFileRoot, 4> allRoots;
			GetFileRoots(false, allRoots);
			std::int32_t targetRoot = -1;
			String relative;
			for (std::size_t i = 0; i < allRoots.size(); i++) {
				if (path.hasPrefix(allRoots[i].Path)) {
					targetRoot = std::int32_t(i);
					relative = path.exceptPrefix(allRoots[i].Path.size());
					break;
				}
			}
			for (std::size_t i = 0; i < allRoots.size() && targetRoot >= 0; i++) {
				if (std::int32_t(i) != targetRoot && fs::FileExists(String(allRoots[i].Path + relative))) {
					warning = (std::int32_t(i) > targetRoot
						? _f("The game will play this level instead of the one in {}.", allRoots[i].Name)
						: _f("The game will play the level in {} instead of this one.", allRoots[i].Name));
					break;
				}
			}
			if (fs::FileExists(path)) {
				warning = (warning.empty() ? String(_("The file already exists.")) : String(_("The file already exists.") + "\n"_s + warning));
			}

			if (warning.empty()) {
				save();
			} else {
				UiMessageDialog::Show(*_surface, exportOnly ? _("Export Level") : _("Save Level As"), String(warning + "\n"_s + _("Do you want to continue?")),
					{ _("Save"), _("Cancel") }, [save](std::int32_t button) {
					if (button == 0) {
						save();
					}
				});
			}
		});

		if (exportOnly && fileDialog != nullptr) {
			// Whether the editor can open the exported level again, chosen along with where it goes
			_exportNotEditable = false;
			auto* notEditable = fileDialog->GetContent()->Add<UiCheckBox>(_("Don't allow editing"),
				[this]() { return _exportNotEditable; }, [this](bool value) { _exportNotEditable = value; });
			notEditable->Tooltip = _("Keep the exported level from being opened in the editor, keep the level you edit to change it later");
		}
	}

	void EditorSession::SaveAsConfirmed(StringView path, bool exportOnly, const std::shared_ptr<Function<void()>>& onSaved)
	{
		auto sharedOnSaved = onSaved;
		{
			fs::CreateDirectories(fs::GetDirectoryName(path));
			if (!SaveTo(path, exportOnly)) {
				ShowMessage(_("The level couldn't be saved"));
				return;
			}
			if (!exportOnly) {
				// The script of the level goes along
				String previousPath = _filePath;
				if (!previousPath.empty()) {
					String scriptFrom = fs::CombinePath(fs::GetDirectoryName(previousPath), String(fs::GetFileNameWithoutExtension(previousPath) + ".j2as"_s));
					String scriptTo = fs::CombinePath(fs::GetDirectoryName(path), String(fs::GetFileNameWithoutExtension(path) + ".j2as"_s));
					if (fs::FileExists(scriptFrom) && !fs::FileExists(scriptTo)) {
						fs::Copy(scriptFrom, scriptTo, false);
					}
				}
				_filePath = path;
				_levelName = MakeLevelName(path);
				_autosave.SetSource(_filePath);
				LoadEventExtensions(false);
				UpdateWindowTitle();
			}
			ShowMessage(exportOnly ? _("Exported") : _("Saved"));
			if (sharedOnSaved != nullptr && *sharedOnSaved) {
				(*sharedOnSaved)();
			}
		}
	}

	void EditorSession::ShowNewLevelDialog()
	{
		struct NewLevelSettings {
			String TileSet;
			float Width = 128.0f;
			float Height = 64.0f;
		};
		auto settings = std::make_shared<NewLevelSettings>();
		settings->TileSet = _document.Properties.DefaultTileSet;

		auto dialog = std::make_unique<UiDialog>(_("New Level"));
		UiDialog* dialogPtr = dialog.get();
		UiStack* content = dialog->GetContent();

		auto* tileSetRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		tileSetRow->Add<UiLabel>(_("Tile set"), UiTheme::TextDim)->Width = 60.0f;
		auto* tileSetBox = tileSetRow->Add<UiTextBox>(settings->TileSet);
		tileSetBox->Width = 120.0f;
		tileSetBox->OnChanged = [settings](StringView text) { settings->TileSet = text; };

		auto* widthRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		widthRow->Add<UiLabel>(_("Width"), UiTheme::TextDim)->Width = 60.0f;
		widthRow->Add<UiNumberBox>([settings]() { return settings->Width; }, [settings](float value, bool) { settings->Width = value; },
			16.0f, 1024.0f, 1.0f, 0)->Width = 60.0f;
		auto* heightRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		heightRow->Add<UiLabel>(_("Height"), UiTheme::TextDim)->Width = 60.0f;
		heightRow->Add<UiNumberBox>([settings]() { return settings->Height; }, [settings](float value, bool) { settings->Height = value; },
			16.0f, 1024.0f, 1.0f, 0)->Width = 60.0f;

		dialog->AddButton(_("Create"), [this, dialogPtr, settings, tileSetBox]() {
			String tileSet = tileSetBox->Text.trimmed();
			std::int32_t tileCount = _tileSets.GetTileCount(tileSet);
			if (tileCount < 0) {
				ShowMessage(_("The tile set doesn't exist"));
				return;
			}
			dialogPtr->Close();
			LevelDocument document;
			document.CreateNew(tileSet, tileCount, Vector2i(std::int32_t(settings->Width), std::int32_t(settings->Height)));
			// Made by whoever plays as the player name of the options
			document.Properties.Author = PreferencesCache::GetEffectivePlayerName();
			ReplaceDocument(std::move(document), {}, "unknown/untitled"_s);
		}, true);
		dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		_surface->PushOverlay(std::move(dialog), std::move(options));
	}

	void EditorSession::ReplaceDocument(LevelDocument&& document, StringView path, StringView levelName)
	{
		_tools->GetActive()->Cancel();
		_toolPointerDown = false;
		_selection.Clear();
		_eventSelection.Clear();
		_maskOverlay.Invalidate();
		_overriddenLayer = -1;

		_document = std::move(document);
		_history = std::make_unique<CommandHistory>(_document, HistoryMemoryBudget);
		_filePath = path;
		_levelName = levelName;
		_autosave.SetSource(_filePath);
		_currentLayer = std::max(_document.GetSpriteLayerIndex(), 0);
		_tileLod->Invalidate();
		LoadEventExtensions(false);

		// Loaded again with the camera where the new level starts
		Viewport::GetChain().clear();
		_editHandler = nullptr;
		auto handler = std::make_shared<EditorLevelHandler>(this);
		if (!handler->Initialize(_document, _levelName, _filePath)) {
			LOGE("New level cannot be shown");
			_root->GoToMainMenu(false);
			return;
		}
		_editHandler = std::move(handler);
		AttachSceneContent();
		RememberAppliedOverrides();
		Vector2i res = theApplication().GetResolution();
		OnInitializeViewport(res.X, res.Y);
		UpdateWindowTitle();
	}

	void EditorSession::GetFileRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots)
	{
		auto& resolver = ContentResolver::Get();

		StringView userContent = resolver.GetWritableContentPath(forSaving);
		if (!userContent.empty()) {
			roots.push_back({ _("User content"), fs::CombinePath(userContent, "Episodes"_s), true });
		}
		StringView content = resolver.GetContentPath();
		if (!content.empty() && content != userContent) {
			bool writable = fs::IsWritable(content);
			if (writable || !forSaving) {
				roots.push_back({ _("Content"), fs::CombinePath(content, "Episodes"_s), writable });
			}
		}
		StringView cache = resolver.GetCachePath();
		if (!cache.empty() && !forSaving) {
			roots.push_back({ _("Cache"), fs::CombinePath(cache, "Episodes"_s), false });
		}
	}

	void EditorSession::GetImageRoots(bool forSaving, SmallVectorImpl<UiFileRoot>& roots)
	{
		StringView userContent = ContentResolver::Get().GetWritableContentPath(forSaving);
		if (!userContent.empty()) {
			roots.push_back({ _("User content"), String(userContent), true });
		}
		if (!_filePath.empty()) {
			String levelDirectory = fs::GetDirectoryName(_filePath);
			bool writable = fs::IsWritable(levelDirectory);
			if (writable || !forSaving) {
				roots.push_back({ _("Level folder"), std::move(levelDirectory), writable });
			}
		}
		String home = fs::GetHomeDirectory();
		if (!home.empty() && fs::DirectoryExists(home)) {
			roots.push_back({ _("Home"), std::move(home), true });
		}
	}

	void EditorSession::ShowExportTileSetImageDialog()
	{
		StringView tileSetName = _document.Properties.DefaultTileSet;
		const Formats::TileSetData* tileSet = GetTileSetData(tileSetName);
		if (tileSet == nullptr) {
			ShowMessage(_("The tile set can't be read"));
			return;
		}
		SmallVector<UiFileRoot, 4> roots;
		GetImageRoots(true, roots);
		String initialName = fs::GetFileName(tileSetName) + ".png"_s;
		UiFileDialog::Show(*_surface, _("Export Tile Set Image"), UiFileDialogMode::Save, std::move(roots), ".png"_s, {}, initialName, [this, tileSet](StringView path) {
			// Rows of 10, as the original tile sets were drawn, the masks into an image of their own next to it
			constexpr std::int32_t Columns = 10;
			PngImage sheet = MakeTileSetSheet(*tileSet, ContentResolver::Get().GetPalettes().data(), Columns);
			PngImage masks = MakeTileSetMaskSheet(*tileSet, Columns);
			String maskPath = String(path.exceptSuffix(std::size_t(4))) + ".mask.png"_s;
			auto s = fs::Open(path, FileAccess::Write);
			auto m = fs::Open(maskPath, FileAccess::Write);
			if (!s->IsValid() || !m->IsValid() || !PngCodec::Encode(*s, sheet) || !PngCodec::Encode(*m, masks)) {
				ShowMessage(_("The images couldn't be saved"));
				return;
			}
			ShowMessage(_f("Saved {} and {}", fs::GetFileName(path), fs::GetFileName(maskPath)));
		});
	}

	bool EditorSession::CanSaveInPlace() const
	{
		if (_filePath.empty()) {
			return false;
		}
		// The cache is rebuilt from the original levels, nothing saved there would last
		StringView cachePath = ContentResolver::Get().GetCachePath();
		if (!cachePath.empty() && _filePath.hasPrefix(cachePath)) {
			return false;
		}
		return fs::IsWritable(fs::GetDirectoryName(_filePath));
	}

	String EditorSession::FindDefaultTileSet()
	{
		if (_tileSets.GetTileCount("castle1"_s) >= 0) {
			return "castle1"_s;
		}
		auto& resolver = ContentResolver::Get();
		for (StringView root : { resolver.GetCachePath(), resolver.GetContentPath() }) {
			String directory = fs::CombinePath(root, "Tilesets"_s);
			for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
				if (fs::GetExtension(item) == "j2t"_s) {
					return fs::GetFileNameWithoutExtension(item);
				}
			}
		}
		return {};
	}

	String EditorSession::MakeLevelName(StringView path)
	{
		String episode = fs::GetFileName(fs::GetDirectoryName(path));
		String name = fs::GetFileNameWithoutExtension(path);
		String levelName = (episode.empty() ? String("unknown"_s) : episode) + "/"_s + name;
		StringUtils::lowercaseInPlace(levelName);
		return levelName;
	}

#if defined(DEATH_DEBUG)
	bool EditorSession::ExecuteDebugCommand(StringView line)
	{
		if (_editHandler == nullptr) {
			return false;
		}

		SmallVector<StringView, 8> args;
		for (StringView arg : line.trimmed().split(' ')) {
			if (!arg.empty()) {
				args.push_back(arg);
			}
		}
		if (args.empty()) {
			return false;
		}

		const auto parseFloat = [&args](std::size_t index, float defaultValue) {
			if (index >= args.size()) {
				return defaultValue;
			}
			String value = args[index];
			return std::strtof(value.data(), nullptr);
		};

		EditorCamera& camera = _editHandler->GetCamera();
		Vector2f viewCenter = Vector2f(float(camera.GetViewSize().X), float(camera.GetViewSize().Y)) * 0.5f;
		StringView command = args[0];
		if (command == "/zoom"_s) {
			camera.ZoomTo(viewCenter, parseFloat(1, camera.GetGameplayScale()));
		} else if (command == "/jump"_s) {
			camera.JumpTo(Vector2f(parseFloat(1, 0.0f), parseFloat(2, 0.0f)), parseFloat(3, camera.GetScale()));
		} else if (command == "/fly"_s) {
			camera.FlyTo(Vector2f(parseFloat(1, 0.0f), parseFloat(2, 0.0f)), parseFloat(3, camera.GetScale()));
		} else if (command == "/fit"_s) {
			Vector2i levelBounds = _editHandler->TileMap()->GetLevelBounds();
			camera.Fit(Rectf(0.0f, 0.0f, float(levelBounds.X), float(levelBounds.Y)), false);
		} else if (command == "/paint"_s && args.size() >= 5) {
			// /paint <layer> <x> <y> <[source:]tile> [width] [height]
			std::int32_t layer = std::int32_t(parseFloat(1, 0.0f));
			std::int32_t x = std::int32_t(parseFloat(2, 0.0f));
			std::int32_t y = std::int32_t(parseFloat(3, 0.0f));
			TileRef tile = { std::uint16_t(parseFloat(4, 0.0f)), TileRef::DefaultTileSet, 0 };
			auto sourceAndTile = args[4].partition(':');
			if (!sourceAndTile[2].empty()) {
				tile.Source = std::uint8_t(std::strtol(String(sourceAndTile[0]).data(), nullptr, 10));
				tile.Index = std::uint16_t(std::strtol(String(sourceAndTile[2]).data(), nullptr, 10));
			}
			std::int32_t width = std::int32_t(parseFloat(5, 1.0f));
			std::int32_t height = std::int32_t(parseFloat(6, 1.0f));
			if (layer < 0 || layer >= std::int32_t(_document.Layers.size())) {
				return false;
			}
			auto paint = std::make_unique<SetTilesCommand>(_document, layer, _("Paint"));
			for (std::int32_t j = 0; j < height; j++) {
				for (std::int32_t i = 0; i < width; i++) {
					paint->Set(x + i, y + j, tile);
				}
			}
			Execute(std::move(paint));
		} else if (command == "/saveprefab"_s && args.size() >= 2) {
			// /saveprefab <name>, the brush as a prefab of the default tile set
			SmallVector<TilePrefab, 0> prefabs;
			TilePrefab::Load(_document.Properties.DefaultTileSet, prefabs);
			prefabs.push_back(TilePrefab::FromStamp(_document, _stamp, args[1]));
			return TilePrefab::Save(_document.Properties.DefaultTileSet, prefabs);
		} else if (command == "/preview"_s && args.size() >= 2) {
			// /preview <image.png>, the preview kept with the level when it's saved
			SmallVector<std::uint8_t, 0> preview;
			LevelThumbnail::MakePreview(*this, preview);
			if (preview.size() < 4) {
				return false;
			}
			PngImage image;
			image.Create(preview[0] | (preview[1] << 8), preview[2] | (preview[3] << 8), false);
			for (std::size_t i = 0; i < std::size_t(image.Width) * std::size_t(image.Height); i++) {
				image.Pixels[i * 4] = preview[4 + i * 3];
				image.Pixels[i * 4 + 1] = preview[4 + i * 3 + 1];
				image.Pixels[i * 4 + 2] = preview[4 + i * 3 + 2];
				image.Pixels[i * 4 + 3] = 255;
			}
			auto s = fs::Open(args[1], FileAccess::Write);
			return (s->IsValid() && PngCodec::Encode(*s, image));
		} else if (command == "/undo"_s) {
			Undo();
		} else if (command == "/redo"_s) {
			Redo();
		} else if (command == "/hide"_s && args.size() >= 2) {
			ToggleLayerFlag(std::int32_t(parseFloat(1, 0.0f)), DocumentLayer::EditorHidden);
		} else if (command == "/layer"_s && args.size() >= 2) {
			SetCurrentLayer(std::int32_t(parseFloat(1, 0.0f)));
		} else if (command == "/action"_s && args.size() >= 2) {
			return _actions.Execute(args[1]);
		} else if ((command == "/click"_s || command == "/rclick"_s) && args.size() >= 3) {
			// Through the same path as the mouse, relative to the size of the window if the values are fractions
			Vector2f pos(parseFloat(1, 0.0f), parseFloat(2, 0.0f));
			Vector2i size = GetViewSize();
			if (pos.X < 1.0f && pos.Y < 1.0f) {
				pos = Vector2f(pos.X * size.X, pos.Y * size.Y);
			}
			MouseEvent event;
			event.x = std::int32_t(pos.X);
			event.y = std::int32_t(pos.Y);
			event.button = (command == "/rclick"_s ? MouseButton::Right : MouseButton::Left);
			OnMouseDown(event);
			OnMouseUp(event);
		} else if (command == "/drag"_s && args.size() >= 5) {
			Vector2f from(parseFloat(1, 0.0f), parseFloat(2, 0.0f));
			Vector2f to(parseFloat(3, 0.0f), parseFloat(4, 0.0f));
			MouseEvent event;
			event.button = MouseButton::Left;
			event.x = std::int32_t(from.X);
			event.y = std::int32_t(from.Y);
			OnMouseDown(event);
			struct DebugMouseState : MouseState {
				bool isButtonDown(MouseButton button) const override { return button == MouseButton::Left; }
			} state;
			constexpr std::int32_t Steps = 16;
			for (std::int32_t i = 1; i <= Steps; i++) {
				state.x = std::int32_t(from.X + (to.X - from.X) * i / Steps);
				state.y = std::int32_t(from.Y + (to.Y - from.Y) * i / Steps);
				OnMouseMove(state);
			}
			event.x = std::int32_t(to.X);
			event.y = std::int32_t(to.Y);
			OnMouseUp(event);
		} else if ((command == "/press"_s || command == "/release"_s) && args.size() >= 3) {
			// A drag over several frames, with /move between, so what animates while it's held can be seen
			MouseEvent event;
			event.button = MouseButton::Left;
			event.x = std::int32_t(parseFloat(1, 0.0f));
			event.y = std::int32_t(parseFloat(2, 0.0f));
			if (command == "/press"_s) {
				OnMouseDown(event);
			} else {
				OnMouseUp(event);
			}
		} else if (command == "/move"_s && args.size() >= 3) {
			struct DebugMouseState : MouseState {
				bool isButtonDown(MouseButton button) const override { return button == MouseButton::Left; }
			} state;
			state.x = std::int32_t(parseFloat(1, 0.0f));
			state.y = std::int32_t(parseFloat(2, 0.0f));
			OnMouseMove(state);
		} else if (command == "/hover"_s && args.size() >= 3) {
			struct DebugMouseState : MouseState {
				bool isButtonDown(MouseButton button) const override { return false; }
			} state;
			state.x = std::int32_t(parseFloat(1, 0.0f));
			state.y = std::int32_t(parseFloat(2, 0.0f));
			OnMouseMove(state);
		} else if (command == "/exporttileset"_s && args.size() >= 2) {
			// The default tile set as images, without the dialog
			const Formats::TileSetData* tileSet = GetTileSetData(_document.Properties.DefaultTileSet);
			if (tileSet == nullptr) {
				return false;
			}
			PngImage sheet = MakeTileSetSheet(*tileSet, ContentResolver::Get().GetPalettes().data(), 10);
			PngImage masks = MakeTileSetMaskSheet(*tileSet, 10);
			auto s = fs::Open(args[1], FileAccess::Write);
			auto m = fs::Open(String(args[1].exceptSuffix(std::size_t(4)) + ".mask.png"_s), FileAccess::Write);
			PngCodec::Encode(*s, sheet);
			PngCodec::Encode(*m, masks);
		} else if (command == "/palette"_s && args.size() >= 3) {
			// /palette <entry> <rrggbb>, an entry of the palette of the level, which it has then, starting as the one in use
			std::int32_t entry = std::int32_t(parseFloat(1, 0.0f));
			if (entry <= 0 || entry > 255) {
				return false;
			}
			std::uint32_t palette[256];
			std::memcpy(palette, ContentResolver::Get().GetPalettes().data(), sizeof(palette));
			std::uint32_t rgb = std::uint32_t(std::strtoul(String(args[2]).data(), nullptr, 16));
			palette[entry] = ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16) | 0xFF000000u;
			Execute(std::make_unique<SetLevelPaletteCommand>(_document, true, palette, _("Palette")));
		} else if (command == "/tileoverride"_s && args.size() >= 3) {
			// /tileoverride <tile ID> <palette index>, the tile filled with one color and solid, only in this level
			std::int32_t tileId = std::int32_t(parseFloat(1, 0.0f));
			if (tileId <= 0 || tileId >= _document.GetAnimatedTilesOffset()) {
				return false;
			}
			std::uint8_t indices[32 * 32], mask[32 * 32];
			std::memset(indices, std::uint8_t(parseFloat(2, 1.0f)), sizeof(indices));
			std::memset(mask, 1, sizeof(mask));
			Execute(std::make_unique<SetTileOverrideCommand>(_document, std::uint16_t(tileId), indices, mask, _("Tile override")));
		} else if (command == "/applytiles"_s && args.size() >= 2) {
			// /applytiles <source> [new name|-] [palette], the tiles changed in the level into their tile set or a new one
			StringView newName = (args.size() >= 3 && args[2] != "-"_s ? args[2] : StringView());
			return ApplyTileChanges(*this, std::uint8_t(parseFloat(1, 0.0f)), newName, args.size() >= 4 && args[3] == "palette"_s);
		} else if ((command == "/addtiles"_s && args.size() >= 3) || (command == "/fxtiles"_s && args.size() >= 5)) {
			// /addtiles <source|level|new:name> <image> [all] [dither], the tiles of an image to the end of a tile set
			// /fxtiles <source|level|new:name> <kind> <width> <height>, effect tiles there, they're the brush then
			TileTarget target;
			if (args[1] == "level"_s) {
				target.Type = TileTarget::Kind::Level;
			} else if (args[1].hasPrefix("new:"_s)) {
				target.Type = TileTarget::Kind::NewTileSet;
				target.Name = args[1].exceptPrefix(4);
			} else {
				target.Source = std::uint8_t(parseFloat(1, 0.0f));
			}
			if (command == "/fxtiles"_s) {
				Formats::TileSetData tiles;
				std::int32_t width = std::clamp<std::int32_t>(std::int32_t(parseFloat(3, 2.0f)), 1, 16), height = std::clamp<std::int32_t>(std::int32_t(parseFloat(4, 2.0f)), 1, 16);
				GenerateFxTiles(FxTileKind(std::clamp<std::int32_t>(std::int32_t(parseFloat(2, 0.0f)), 0, std::int32_t(FxTileKind::Count) - 1)), width, height, 1.0f, tiles);
				TileRef first;
				if (!AppendTiles(*this, target, tiles, first)) {
					return false;
				}
				TileStamp stamp;
				stamp.Width = width;
				stamp.Height = height;
				for (std::int32_t i = 0; i < width * height; i++) {
					stamp.Tiles.push_back(TileRef{ std::uint16_t(first.Index + i), first.Source, 0 });
					stamp.Present.push_back(true);
				}
				SetStamp(std::move(stamp));
				LOGI("Effect tiles added to source {} from tile {}", first.Source, first.Index);
				return true;
			}
			bool all = false, dither = false;
			for (std::size_t i = 3; i < args.size(); i++) {
				all |= (args[i] == "all"_s);
				dither |= (args[i] == "dither"_s);
			}
			return AddTilesFromImage(*this, target, args[2], !all, dither);
		} else if (command == "/newtileset"_s && args.size() >= 3) {
			// /newtileset <name> <tiles> [extra|default|save]
			StringView use = (args.size() >= 4 ? args[3] : "extra"_s);
			return CreateEmptyTileSet(*this, args[1], std::int32_t(parseFloat(2, 100.0f)), use != "save"_s, use == "default"_s);
		} else if (command == "/importtileset"_s && args.size() >= 3) {
			// /importtileset <image> <name> [extra|default|save] [own|true|match|reduce] [dither]
			StringView use = (args.size() >= 4 ? args[3] : "extra"_s);
			StringView colors = (args.size() >= 5 ? args[4] : StringView());
			ImportTileSetImage(*this, args[1], args[2], use != "save"_s, use == "default"_s, colors, args.size() >= 6 && args[5] == "dither"_s);
		} else if (command == "/pngtest"_s && args.size() >= 2) {
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
		} else if (command == "/renderer"_s && args.size() >= 2) {
			// Changes the renderer of the current layer, as an undoable step
			if (_currentLayer >= 0 && _currentLayer < std::int32_t(_document.Layers.size())) {
				const DocumentLayer& layer = _document.Layers[_currentLayer];
				Formats::LayerRecord record = layer.Record;
				record.RendererType = std::uint8_t(parseFloat(1, 0.0f));
				Execute(std::make_unique<SetLayerPropertiesCommand>(_document, _currentLayer, record, layer.Name, _("Layer properties")));
			}
		} else if (command == "/wheel"_s && args.size() >= 4) {
			// Scrolls at a position, as the mouse would
			struct DebugMouseState : MouseState {
				bool isButtonDown(MouseButton button) const override { return false; }
			} state;
			state.x = std::int32_t(parseFloat(1, 0.0f));
			state.y = std::int32_t(parseFloat(2, 0.0f));
			OnMouseMove(state);
			ScrollEvent event;
			event.x = 0.0f;
			event.y = parseFloat(3, 0.0f);
			OnMouseWheel(event);
		} else if ((command == "/key"_s || command == "/keydown"_s || command == "/keyup"_s) && args.size() >= 2) {
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
			if (event.sym == Keys::Unknown) {
				return false;
			}
			// The modifier keys held, as a real key event has them
			std::int32_t modifier = (event.sym == Keys::LCtrl ? KeyMod::Ctrl : (event.sym == Keys::LShift ? KeyMod::Shift : (event.sym == Keys::LAlt ? KeyMod::Alt : 0)));
			event.mod = (command == "/keyup"_s ? (_modifiers & ~modifier) : (_modifiers | modifier));
			if (command != "/keyup"_s) {
				OnKeyPressed(event);
			}
			if (command != "/keydown"_s) {
				OnKeyReleased(event);
			}
		} else if (command == "/exitlevel"_s) {
			if (_playHandler == nullptr || _mode != SessionMode::Play) {
				return false;
			}
			_playHandler->GetLevelHandler().BeginLevelChange(nullptr, ExitType::Normal);
		} else if (command == "/autosave"_s) {
			return _autosave.SaveNow(_document, *_history);
		} else if (command == "/layers"_s) {
			for (std::size_t i = 0; i < _document.Layers.size(); i++) {
				const DocumentLayer& layer = _document.Layers[i];
				LOGI("Layer {}: kind {}, {}x{}, depth {}, speed {}x{}, flags 0x{:x}, name \"{}\"", i, std::int32_t(layer.Record.Kind),
					layer.GetWidth(), layer.GetHeight(), layer.Record.Depth, layer.Record.SpeedX, layer.Record.SpeedY, layer.Record.Flags, layer.Name);
			}
		} else if (command == "/rebuild"_s) {
			RebuildRuntime();
		} else if (command == "/brush"_s && args.size() >= 2) {
			// Event to place by its key, as the catalog names it
			const EventDescriptor* descriptor = EventCatalog::FindByKey(args[1]);
			if (descriptor == nullptr) {
				return false;
			}
			SetEventBrush(EventStamp::FromEvent(EventCatalog::MakeDefault(descriptor->Type)));
		} else if (command == "/gamepad"_s && args.size() >= 3) {
			// As if a gamepad was used, with its cursor at a position
			SetInputModality(InputModality::Gamepad);
			_gamepadCursor = Vector2f(parseFloat(1, 0.0f), parseFloat(2, 0.0f));
			PointerMove(_gamepadCursor);
		} else if (command == "/radial"_s) {
			// `/radial` shows the ring of tools at the gamepad cursor, `/radial x y` then points the stick at one
			if (_radialMenu == nullptr) {
				ShowToolMenu(_gamepadCursor);
			} else if (args.size() >= 3) {
				_radialMenu->PointTo(Vector2f(parseFloat(1, 0.0f), parseFloat(2, 0.0f)));
			}
		} else if (command == "/exportrules"_s && args.size() >= 2) {
			// Rules of tile sets made from what the levels use the most, as a start for rules shipped with the game
			for (std::size_t i = 1; i < args.size(); i++) {
				// A space in a name is written as +
				String tileSet = StringUtils::replaceAll(String(args[i]), '+', ' ');
				SmartTileModel model;
				model.Request(tileSet, _tileSets);
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
		} else if (command == "/touch"_s && args.size() >= 2) {
			// `/touch down|pointerdown|move|pointerup|up id x y [id x y ...]`, in pixels of the screen, the index of the
			// finger that came or went is the last one given
			TouchEvent event;
			StringView type = args[1];
			event.type = (type == "down"_s ? TouchEventType::Down : type == "pointerdown"_s ? TouchEventType::PointerDown :
				type == "pointerup"_s ? TouchEventType::PointerUp : type == "up"_s ? TouchEventType::Up : TouchEventType::Move);
			Vector2f screen = _surface->GetScreenSize();
			for (std::size_t i = 2; i + 2 < args.size() + 0 && event.count < TouchEvent::MaxPointers; i += 3) {
				TouchEvent::Pointer& pointer = event.pointers[event.count++];
				pointer.id = std::int32_t(parseFloat(i, 0.0f));
				pointer.x = parseFloat(i + 1, 0.0f) / screen.X;
				pointer.y = parseFloat(i + 2, 0.0f) / screen.Y;
				pointer.pressure = 1.0f;
			}
			event.actionIndex = std::int32_t(event.count) - 1;
			OnTouchEvent(event);
		} else if (command == "/keyboard"_s) {
			// The keyboard of the editor, as where the system has none
			UiOnScreenKeyboard::Show(*_surface);
		} else if (command == "/stopplay"_s) {
			// Back to the editor, `/stopplay keep` keeps the play-test to resume
			StopPlayTest(args.size() >= 2 && args[1] == "keep"_s);
		} else if (command == "/playerstate"_s) {
			// Score and lives added to the player of the play-test, and what it has then logged
			if (Actors::Player* player = (_playHandler != nullptr ? _playHandler->GetPlayer() : nullptr)) {
				if (args.size() >= 3) {
					player->AddScore(std::int32_t(parseFloat(1, 0.0f)));
					player->AddLives(std::int32_t(parseFloat(2, 0.0f)));
				}
				LOGI("Play-test player: score {}, lives {}, health {}", player->GetScore(), player->GetLives(), player->GetHealth());
			}
		} else if (command == "/playring"_s && args.size() >= 2) {
			// Holds the transition into the level being played at a point, so it can be looked at, or the one back into
			// the editor after a play-test
			if (_mode == SessionMode::Play) {
				_playTransition.Start(_editHandler->GetFrameTexture(), _playHandler != nullptr ? _playHandler->GetFrameTexture() : nullptr, GetAccentColor());
			} else {
				IPlayTest* played = (_closingPlayHandler != nullptr ? _closingPlayHandler.get() : _playHandler.get());
				if (_mode != SessionMode::Edit || played == nullptr || played->GetFrameTexture() == nullptr) {
					return false;
				}
				_playTransition.Start(played->GetFrameTexture(), nullptr, GetAccentColor());
			}
			_playTransition.SetProgress(parseFloat(1, 0.5f), true);
		} else if (command == "/animframe"_s && args.size() >= 3) {
			// Frame time of an animated tile in frames
			std::int32_t index = std::int32_t(parseFloat(1, 0.0f));
			if (index < 0 || index >= std::int32_t(_document.Animations.size())) {
				return false;
			}
			DocumentAnimation animation = _document.Animations[index];
			animation.Timing.FrameDuration16 = std::uint16_t(parseFloat(2, 1.0f) * 16.0f);
			Execute(std::make_unique<SetAnimationCommand>(_document, index, std::move(animation), "Animation timing"_s), true);
		} else if (command == "/selectevent"_s && args.size() >= 3) {
			Vector2i gridSize = _document.GetEventGridSize();
			std::int32_t x = std::int32_t(parseFloat(1, 0.0f)), y = std::int32_t(parseFloat(2, 0.0f));
			if (x < 0 || y < 0 || x >= gridSize.X || y >= gridSize.Y) {
				return false;
			}
			if (args.size() >= 4 && args[3] == "add"_s) {
				_eventSelection.Add(std::uint32_t(x + y * gridSize.X));
			} else {
				_eventSelection.Set(std::uint32_t(x + y * gridSize.X));
			}
		} else if (command == "/placeevent"_s && args.size() >= 4) {
			// /placeevent <key> <x> <y>, an event of the catalog with its default parameters
			const EventDescriptor* descriptor = EventCatalog::FindByKey(args[1]);
			Vector2i gridSize = _document.GetEventGridSize();
			std::int32_t x = std::int32_t(parseFloat(2, -1.0f)), y = std::int32_t(parseFloat(3, -1.0f));
			if (descriptor == nullptr || x < 0 || y < 0 || x >= gridSize.X || y >= gridSize.Y) {
				return false;
			}
			auto place = std::make_unique<SetEventsCommand>(_document, _("Place Events"));
			place->Set(x, y, EventCatalog::MakeDefault(descriptor->Type));
			Execute(std::move(place));
		} else if (command == "/placeoffgrid"_s && args.size() >= 4) {
			// /placeoffgrid <key> <x> <y> [delay], an off-grid event at a position in pixels, the editor can't place them
			// otherwise, with a delay it's a generator of the event
			const EventDescriptor* descriptor = EventCatalog::FindByKey(args[1]);
			if (descriptor == nullptr) {
				return false;
			}
			SmallVector<Formats::OffGridEventRecord, 0> events;
			for (const Formats::OffGridEventRecord& event : _document.OffGridEvents) {
				events.push_back(event);
			}
			Formats::OffGridEventRecord& record = events.emplace_back();
			record.X = std::uint32_t(std::max(parseFloat(2, 0.0f), 0.0f));
			record.Y = std::uint32_t(std::max(parseFloat(3, 0.0f), 0.0f));
			record.Event = EventCatalog::MakeDefault(descriptor->Type);
			if (args.size() >= 5) {
				record.Event.Flags |= Formats::EventRecord::Generator;
				record.Event.GeneratorDelay = std::uint8_t(std::clamp(parseFloat(4, 0.0f), 0.0f, 255.0f));
			}
			Execute(std::make_unique<SetOffGridEventsCommand>(_document, std::move(events), _("Place Events")));
		} else if (command == "/events"_s) {
			LOGI("{} events, {} selected", _document.Events.size(), _eventSelection.GetCount());
			for (const EventStamp::Item& item : _eventBrush.Items) {
				LOGI("Brush event at {}, {}: type {}, params {} {} {} {}", item.Offset.X, item.Offset.Y, item.Event.Type,
					item.Event.Params[0], item.Event.Params[1], item.Event.Params[2], item.Event.Params[3]);
			}
			Vector2i gridSize = _document.GetEventGridSize();
			for (std::uint32_t cell : _eventSelection.GetCells()) {
				const Formats::EventRecord& event = _document.GetEvent(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
				LOGI("Selected event at {}, {}: type {}, flags 0x{:x}, params {} {} {} {}", cell % std::uint32_t(gridSize.X), cell / std::uint32_t(gridSize.X),
					event.Type, event.Flags, event.Params[0], event.Params[1], event.Params[2], event.Params[3]);
			}
		} else if (command == "/texts"_s) {
			// The texts of the level, with the characters that aren't printable as their codes
			for (std::size_t i = 0; i < _document.Texts.size(); i++) {
				String escaped;
				for (char c : _document.Texts[i]) {
					if (std::uint8_t(c) < 32) {
						escaped += "\\x"_s + String(std::to_string(std::uint8_t(c)).c_str());
					} else {
						escaped += StringView(&c, 1);
					}
				}
				LOGI("Text {}: \"{}\"", i, escaped);
			}
		} else if (command == "/findevents"_s && args.size() >= 2) {
			// /findevents <key>, where the events of a kind of the catalog are, at most 20 of them
			const EventDescriptor* descriptor = EventCatalog::FindByKey(args[1]);
			if (descriptor == nullptr) {
				return false;
			}
			Vector2i gridSize = _document.GetEventGridSize();
			std::int32_t found = 0;
			for (std::int32_t y = 0; y < gridSize.Y && found < 20; y++) {
				for (std::int32_t x = 0; x < gridSize.X && found < 20; x++) {
					const Formats::EventRecord& event = _document.GetEvent(x, y);
					if (EventType(event.Type) == descriptor->Type) {
						LOGI("Event {} at {}, {}: params {} {} {} {}", args[1], x, y, event.Params[0], event.Params[1], event.Params[2], event.Params[3]);
						found++;
					}
				}
			}
		} else if (command == "/exportplayonly"_s && args.size() >= 2) {
			// As Export with "Only to be played" checked
			String path = line.trimmed().exceptPrefix(command.size()).trimmed();
			_exportNotEditable = true;
			bool saved = SaveTo(path, true);
			_exportNotEditable = false;
			return saved;
		} else if ((command == "/save"_s || command == "/export"_s) && args.size() >= 2) {
			String path = line.trimmed().exceptPrefix(command.size()).trimmed();
			return SaveTo(path, command == "/export"_s);
		} else {
			return false;
		}
		LOGI("Editor command: {}", line);
		return true;
	}
#endif

	Vector2f EditorSession::ToDrawable(std::int32_t x, std::int32_t y)
	{
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		float scaleX = (gfxDevice.width() > 0 ? float(gfxDevice.drawableWidth()) / float(gfxDevice.width()) : 1.0f);
		float scaleY = (gfxDevice.height() > 0 ? float(gfxDevice.drawableHeight()) / float(gfxDevice.height()) : 1.0f);
		return Vector2f(x * scaleX, y * scaleY);
	}

	void EditorSession::UpdateWindowTitle()
	{
		String name = (!_document.Properties.DisplayName.empty() ? _document.Properties.DisplayName : String(fs::GetFileName(_filePath)));
		if (_history != nullptr && _history->IsDirty()) {
			name += "*"_s;
		}
		theApplication().GetGfxDevice().setWindowTitle(String(NCINE_APP_NAME " - "_s + _("Level Editor") + " - "_s + name));
	}
}

#endif
