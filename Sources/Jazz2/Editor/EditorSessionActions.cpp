#include "EditorSessionPrivate.h"

#if defined(WITH_EDITOR)

#include "Shell/EpisodeDialog.h"
#include "Shell/PackageDialog.h"
#include "Shell/RecoveryDialog.h"
#include "Shell/ShellActions.h"
#include "Tools/ToolActions.h"

// The actions of the level editor, their keys and when they're offered, see EditorSession::CreateActions()

namespace Jazz2::Editor
{
	void EditorSession::CreateActions()
	{
		// The order they're added in is the order of the command palette, and of two actions with the same key the
		// first one runs. Presets of the shortcuts (like those of other editors) are chosen in the shortcuts dialog
		CreateFileActions();
		CreateEditActions();
		CreateHelpActions();
		CreateNavigationActions();
		CreateEventActions();
		CreateStampActions();
		CreateToolActions();
		CreateViewActions();
		CreateLayerActions();
		CreateLevelActions();
		CreateLayerSelectActions();
		CreatePlayActions();
		CreatePlayTestDebugActions();
		CreateReachabilityActions();
		CreateScriptAndCompareActions();

		// Those of the panels, the overlays and the dialogs of the interface
		AddShellActions(*this, _actions);
		// Those of the tools and of the selection, with the options of the tools
		ToolActions::Register(*this);

		CancelInteractionsBeforeActions();
	}

	void EditorSession::CreateFileActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::FileNew, _("New Level"), EditorIcon::New, [this]() {
			_files->ConfirmDiscard([this]() { _files->ShowNewLevelDialog(); });
		}).Key = { Keys::N, KeyMod::Ctrl };
		a.Add(ActionIds::FileOpen, _("Open"), EditorIcon::Folder, [this]() {
			_files->ConfirmDiscard([this]() { _files->ShowOpenDialog(); });
		}).Key = { Keys::O, KeyMod::Ctrl };
		// Saving works while a text is typed, the text is confirmed first
		auto& save = a.Add(ActionIds::FileSave, _("Save"), EditorIcon::Save, [this]() { _files->Save(); });
		save.Key = { Keys::S, KeyMod::Ctrl };
		save.WorksWhileTyping = true;
		auto& saveAs = a.Add(ActionIds::FileSaveAs, _("Save As"), EditorIcon::Save, [this]() { ShowSaveAsDialog(false); });
		saveAs.Key = { Keys::S, KeyMod::Ctrl | KeyMod::Shift };
		saveAs.WorksWhileTyping = true;
		a.Add(ActionIds::FileExport, _("Export"), EditorIcon::Export, [this]() { _files->ShowExportDialog(); }).Key = { Keys::E, KeyMod::Ctrl };
		a.Add(ActionIds::FileExportPackage, _("Export Package"), EditorIcon::Export, [this]() { ShowExportPackageDialog(*this); });
		a.Add(ActionIds::FileRecovery, _("Recovery Copies"), EditorIcon::None, [this]() { ShowRecoveryDialog(*this); });
		a.Add(ActionIds::FileExit, _("Exit Editor"), EditorIcon::Close, [this]() { _files->ExitEditor(); });
	}

	void EditorSession::CreateEditActions()
	{
		auto& a = _actions;
		{
			auto& undo = a.Add(ActionIds::EditUndo, _("Undo"), EditorIcon::Undo, [this]() { Undo(); });
			undo.Key = { Keys::Z, KeyMod::Ctrl };
			undo.IsEnabled = [this]() { return _history->CanUndo(); };
			auto& redo = a.Add(ActionIds::EditRedo, _("Redo"), EditorIcon::Redo, [this]() { Redo(); });
			redo.Key = { Keys::Y, KeyMod::Ctrl };
			redo.AlternateKey = { Keys::Z, KeyMod::Ctrl | KeyMod::Shift };
			redo.IsEnabled = [this]() { return _history->CanRedo(); };
		}
		auto hasSelection = [this]() { return HasSelection(); };
		auto& cut = a.Add(ActionIds::EditCut, _("Cut"), EditorIcon::Cut, [this]() { CopySelection(true); });
		cut.Key = { Keys::X, KeyMod::Ctrl };
		cut.IsEnabled = hasSelection;
		auto& copy = a.Add(ActionIds::EditCopy, _("Copy"), EditorIcon::Copy, [this]() { CopySelection(false); });
		copy.Key = { Keys::C, KeyMod::Ctrl };
		copy.IsEnabled = hasSelection;
		a.Add(ActionIds::EditPaste, _("Paste"), EditorIcon::Paste, [this]() { Paste(); }).Key = { Keys::V, KeyMod::Ctrl };
		auto& remove = a.Add(ActionIds::EditDelete, _("Delete"), EditorIcon::Delete, [this]() { DeleteSelection(); });
		remove.Key = { Keys::Delete, 0 };
		remove.IsEnabled = hasSelection;
		a.Add(ActionIds::EditSelectAll, _("Select All"), EditorIcon::Select, [this]() { SelectAll(); }).Key = { Keys::A, KeyMod::Ctrl };
		a.Add(ActionIds::EditDeselect, _("Deselect"), EditorIcon::None, [this]() { ClearSelection(); }).Key = { Keys::D, KeyMod::Ctrl };
		a.Add(ActionIds::EditHistory, _("History"), EditorIcon::History, [this]() { ShowHistoryDialog(*this); }).Key = { Keys::H, KeyMod::Ctrl };
		a.Add(ActionIds::EditFindReplace, _("Find and Replace"), EditorIcon::Search, [this]() { ShowFindReplaceDialog(*this); }).Key = { Keys::F, KeyMod::Ctrl };
		a.Add(ActionIds::EditPrefabs, _("Prefabs"), EditorIcon::Tiles, [this]() { ShowPrefabsDialog(*this); });
		a.Add(ActionIds::EditFillGround, _("Fill Selection with Ground"), EditorIcon::Fill, [this]() { FillSelectionWithGround(*this); });
		a.Add(ActionIds::EditBeautify, _("Beautify Selection"), EditorIcon::SmartBrush, [this]() { BeautifySelection(*this); });
		a.Add(ActionIds::EditBakeGround, _("Make Tiles Along Ground Line"), EditorIcon::Slope, [this]() { BakeGroundLine(); });
		a.Add(ActionIds::EditContinueRow, _("Continue Row by Suggestions"), EditorIcon::SmartBrush, [this]() { ContinueRow(); }).Key = { Keys::Right, KeyMod::Ctrl | KeyMod::Shift };
		a.Add(ActionIds::EditTerrainRules, _("Terrain Rules"), EditorIcon::SmartBrush, [this]() { ShowTerrainRulesDialog(*this); });
		a.Add(ActionIds::EditSmartRetile, _("Choose Tiles by Their Neighbors"), EditorIcon::SmartBrush, [this]() { RetileSelection(); });
		a.Add(ActionIds::EditTraceGround, _("Make Ground Line from Selection"), EditorIcon::Slope, [this]() {
			if (GroundLineTool::TraceSelection(*this)) {
				_tools->SetActive(EditorToolType::Ground);
			}
		});
	}

	void EditorSession::CreateHelpActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::HelpCommands, _("Command Palette"), EditorIcon::None, [this]() { ShowCommandPalette(*this); }).Key = { Keys::K, KeyMod::Ctrl };
		a.Add(ActionIds::HelpShortcuts, _("Keyboard Shortcuts"), EditorIcon::None, [this]() { ShowShortcutsDialog(*this); }).Key = { Keys::F1, 0 };
	}

	void EditorSession::CreateNavigationActions()
	{
		auto& a = _actions;
		// The key shows the navigator or hides it again
		a.Add(ActionIds::ViewNavigator, _("Navigator"), EditorIcon::Map, [this]() { ShowNavigator(*this, true); }).Key = { Keys::F2, 0 };
		a.Add(ActionIds::ViewAddNote, _("Add Note"), EditorIcon::Plus, [this]() {
			// Where the pointer is over the level, otherwise in the middle of the view
			ToolPointer hover;
			Vector2f pos = (GetHoverPointer(hover) ? hover.World : (_editHandler != nullptr ? _editHandler->GetCamera().GetCenter() : Vector2f()));
			AddNote(*this, pos);
			ShowNavigator(*this);
		}).Key = { Keys::N, KeyMod::Ctrl | KeyMod::Shift };
		for (std::int32_t slot = 1; slot <= 9; slot++) {
			Keys key = Keys(std::int32_t(Keys::D1) + slot - 1);
			a.Add(String(StringView(ActionIds::BookmarkSetPrefix) + String(std::to_string(slot).c_str())), _f("Set Bookmark {}", slot), EditorIcon::None,
				[this, slot]() { SetBookmark(*this, slot); }).Key = { key, KeyMod::Ctrl | KeyMod::Shift };
			a.Add(String(StringView(ActionIds::BookmarkGoPrefix) + String(std::to_string(slot).c_str())), _f("Go to Bookmark {}", slot), EditorIcon::None,
				[this, slot]() { GoToBookmark(*this, slot); }).Key = { key, KeyMod::Ctrl };
		}
	}

	void EditorSession::CreateEventActions()
	{
		auto& a = _actions;
		auto& sameType = a.Add(ActionIds::EventsSelectSameType, _("Select Events of the Same Kind"), EditorIcon::Event, [this]() { SelectEventsOfSameType(); });
		sameType.IsEnabled = [this]() { return IsEditingEvents() && !_eventSelection.IsEmpty(); };
		auto& matching = a.Add(ActionIds::EventsCopyToMatchingTiles, _("Copy Event to Every Tile Like Its Own"), EditorIcon::Event, [this]() { CopyEventToMatchingTiles(); });
		matching.IsEnabled = [this]() { return IsEditingEvents() && _eventSelection.GetCellCount() == 1; };
	}

	void EditorSession::CreateStampActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::StampFlipX, _("Flip Stamp Horizontally"), EditorIcon::FlipX, [this]() { _stamp.FlipX(); }).Key = { Keys::X, 0 };
		a.Add(ActionIds::StampFlipY, _("Flip Stamp Vertically"), EditorIcon::FlipY, [this]() { _stamp.FlipY(); }).Key = { Keys::Y, 0 };
		auto& random = a.Add(ActionIds::StampRandom, _("Paint Tiles of Stamp at Random"), EditorIcon::None, [this]() { _stamp.Random = !_stamp.Random; });
		random.Key = { Keys::Z, 0 };
		random.IsChecked = [this]() { return _stamp.Random; };
		auto& reroll = a.Add(ActionIds::StampReroll, _("Choose Other Tiles at Random"), EditorIcon::None, [this]() { _stamp.Seed++; });
		reroll.Key = { Keys::Z, KeyMod::Shift };
		reroll.IsEnabled = [this]() { return _stamp.Random; };
	}

	void EditorSession::CreateToolActions()
	{
		auto& a = _actions;
		struct ToolInfo { const char* Id; const char* Label; EditorIcon Icon; Keys Key; EditorToolType Type; };
		static const ToolInfo Tools[] = {
			{ ActionIds::ToolBrush, "Brush", EditorIcon::Brush, Keys::B, EditorToolType::Brush },
			{ ActionIds::ToolEraser, "Eraser", EditorIcon::Eraser, Keys::E, EditorToolType::Eraser },
			{ ActionIds::ToolRectangle, "Rectangle", EditorIcon::Rectangle, Keys::R, EditorToolType::Rectangle },
			{ ActionIds::ToolFill, "Fill", EditorIcon::Fill, Keys::G, EditorToolType::Fill },
			{ ActionIds::ToolEyedropper, "Eyedropper", EditorIcon::Eyedropper, Keys::I, EditorToolType::Eyedropper },
			{ ActionIds::ToolSelect, "Select", EditorIcon::Select, Keys::M, EditorToolType::Select },
			{ ActionIds::ToolWand, "Magic Wand", EditorIcon::Wand, Keys::W, EditorToolType::Wand },
			{ ActionIds::ToolHand, "Hand", EditorIcon::Hand, Keys::H, EditorToolType::Hand },
			{ ActionIds::ToolEvent, "Events", EditorIcon::Event, Keys::Q, EditorToolType::Event },
			{ ActionIds::ToolMeasure, "Measure", EditorIcon::Measure, Keys::U, EditorToolType::Measure },
			{ ActionIds::ToolPath, "Events Along Path", EditorIcon::EventPath, Keys::K, EditorToolType::Path },
			{ ActionIds::ToolSmart, "Smart Brush", EditorIcon::SmartBrush, Keys::T, EditorToolType::Smart },
			{ ActionIds::ToolGround, "Ground Line", EditorIcon::Slope, Keys::P, EditorToolType::Ground },
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
	}

	void EditorSession::CreateViewActions()
	{
		auto& a = _actions;
		{
			auto& grid = a.Add(ActionIds::ViewGrid, _("Show Grid"), EditorIcon::Grid, [this]() {
				_showGrid = !_showGrid;
				_settings.ShowGrid = _showGrid;
				_settings.Save();
			});
			grid.Key = { Keys::G, KeyMod::Ctrl };
			grid.IsChecked = [this]() { return _showGrid; };

			auto& flat = a.Add(ActionIds::ViewFlat, _("Flat Layers"), EditorIcon::Parallax, [this]() {
				if (Tiles::TileMap* tileMap = GetTileMap()) {
					Tiles::TileMapViewOptions options = tileMap->GetViewOptions();
					options.FlatAmount = (options.FlatAmount > 0.5f ? 0.0f : 1.0f);
					tileMap->SetViewOptions(options);
				}
			});
			flat.Key = { Keys::P, KeyMod::Ctrl };

			auto& masks = a.Add(ActionIds::ViewMasks, _("Show Masks"), EditorIcon::Mask, [this]() {
				// Over the tiles, instead of them, not at all
				_maskMode = (_maskMode + 1) % 3;
				if (_maskMode == 0) {
					// Its images are made again quickly, they'd only take memory meanwhile
					_maskOverlay->Release();
				}
			});
			masks.Key = { Keys::M, KeyMod::Ctrl };
			masks.IsChecked = [this]() { return (_maskMode != 0); };

			auto& events = a.Add(ActionIds::ViewEvents, _("Show Events"), EditorIcon::Event, [this]() { _showEvents = !_showEvents; });
			events.Key = { Keys::E, KeyMod::Ctrl | KeyMod::Shift };
			events.IsChecked = [this]() { return _showEvents; };

			// The layer being edited always stays still, this keeps all of them still
			auto& pauseLayers = a.Add(ActionIds::ViewPauseLayers, _("Pause Layer Movement"), EditorIcon::Pause, [this]() {
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
		// The level handler is missing only for the frame after it couldn't be loaded again, before the main menu
		a.Add(ActionIds::ViewZoomIn, _("Zoom In"), EditorIcon::ZoomIn, [this]() {
			if (EditorCamera* camera = GetCamera()) {
				Rectf view = GetViewRect();
				camera->ZoomSteps(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), 1.0f);
			}
		}).Key = { Keys::Plus, 0 };
		a.Add(ActionIds::ViewZoomOut, _("Zoom Out"), EditorIcon::ZoomOut, [this]() {
			if (EditorCamera* camera = GetCamera()) {
				Rectf view = GetViewRect();
				camera->ZoomSteps(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), -1.0f);
			}
		}).Key = { Keys::Minus, 0 };
		EditorAction& zoomGame = a.Add(ActionIds::ViewZoomGame, _("Zoom as in Game"), EditorIcon::None, [this]() {
			if (EditorCamera* camera = GetCamera()) {
				Rectf view = GetViewRect();
				camera->ZoomTo(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), camera->SnapScale(camera->GetGameplayScale(), 0));
			}
		});
		// The digits of the numeric keypad are keys of their own, the zero there zooms the same way
		zoomGame.Key = { Keys::D0, 0 };
		zoomGame.AlternateKey = { Keys::NumPad0, 0 };
		a.Add(ActionIds::ViewFit, _("Show Whole Level"), EditorIcon::Fit, [this]() {
			Tiles::TileMap* tileMap = GetTileMap();
			EditorCamera* camera = GetCamera();
			if (tileMap != nullptr && camera != nullptr) {
				Vector2i levelBounds = tileMap->GetLevelBounds();
				camera->Fit(Rectf(0.0f, 0.0f, float(levelBounds.X), float(levelBounds.Y)), true);
			}
		}).Key = { Keys::Home, 0 };

		// Size of the interface, in steps that keep it sharp where they can
		a.Add(ActionIds::ViewUiLarger, _("Larger Interface"), EditorIcon::None, [this]() { ChangeUiScale(1); }).Key = { Keys::Plus, KeyMod::Ctrl | KeyMod::Shift };
		a.Add(ActionIds::ViewUiSmaller, _("Smaller Interface"), EditorIcon::None, [this]() { ChangeUiScale(-1); }).Key = { Keys::Minus, KeyMod::Ctrl | KeyMod::Shift };
		EditorAction& uiAuto = a.Add(ActionIds::ViewUiAuto, _("Interface Size by Window"), EditorIcon::None, [this]() { ChangeUiScale(0); });
		uiAuto.Key = { Keys::D0, KeyMod::Ctrl | KeyMod::Shift };
		uiAuto.AlternateKey = { Keys::NumPad0, KeyMod::Ctrl | KeyMod::Shift };

		// Layout for the screen, and how much is offered
		struct LayoutChoice { const char* Id; const char* Label; std::int32_t Value; };
		static const LayoutChoice Layouts[] = {
			{ ActionIds::ViewLayoutAuto, N_("Layout by Window Size"), 0 },
			{ ActionIds::ViewLayoutDesktop, N_("Desktop Layout"), 1 },
			{ ActionIds::ViewLayoutCompact, N_("Compact Layout, Panels in Drawers"), 2 },
		};
		for (const LayoutChoice& choice : Layouts) {
			std::int32_t value = choice.Value;
			auto& layout = a.Add(choice.Id, _(choice.Label), EditorIcon::None, [this, value]() {
				_settings.Layout = value;
				_settings.Save();
			});
			layout.IsChecked = [this, value]() { return (_settings.Layout == value); };
		}
		auto& expert = a.Add(ActionIds::ViewExpert, _("Expert Mode"), EditorIcon::Settings, [this]() {
			_settings.Expert = !_settings.Expert;
			_settings.Save();
			ShowMessage(_settings.Expert ? _("All tools and commands are offered") : _("Only the simple tools and commands are offered"));
		});
		expert.IsChecked = [this]() { return _settings.Expert; };
		// How the interface looks and moves, for eyes that need it
		auto applyTheme = [this]() {
			UiTheme::Apply(UiThemeKind(_settings.Theme), _settings.ReducedMotion);
			_settings.Save();
		};
		auto& highContrast = a.Add(ActionIds::ViewHighContrast, _("High Contrast Interface"), EditorIcon::None, [this, applyTheme]() {
			_settings.Theme = (_settings.Theme == std::int32_t(UiThemeKind::HighContrast) ? std::int32_t(UiThemeKind::Default) : std::int32_t(UiThemeKind::HighContrast));
			applyTheme();
		});
		highContrast.IsChecked = [this]() { return (_settings.Theme == std::int32_t(UiThemeKind::HighContrast)); };
		auto& reducedMotion = a.Add(ActionIds::ViewReducedMotion, _("Reduce Motion"), EditorIcon::None, [this, applyTheme]() {
			_settings.ReducedMotion = !_settings.ReducedMotion;
			applyTheme();
		});
		reducedMotion.IsChecked = [this]() { return _settings.ReducedMotion; };
		auto& drawerTiles = a.Add(ActionIds::ViewDrawerTiles, _("Tiles and Events"), EditorIcon::Tiles, [this]() {
			if (_shell != nullptr) {
				_shell->ToggleDrawer(true);
			}
		});
		drawerTiles.IsChecked = [this]() { return (_shell != nullptr && _shell->IsDrawerOpen(true)); };
		auto& drawerLayers = a.Add(ActionIds::ViewDrawerLayers, _("Layers and Properties"), EditorIcon::Layers, [this]() {
			if (_shell != nullptr) {
				_shell->ToggleDrawer(false);
			}
		});
		drawerLayers.IsChecked = [this]() { return (_shell != nullptr && _shell->IsDrawerOpen(false)); };
	}

	void EditorSession::CreateLayerActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::LayerAdd, _("Add Layer"), EditorIcon::Plus, [this]() { AddLayer(); });
		auto& remove = a.Add(ActionIds::LayerRemove, _("Remove Layer"), EditorIcon::Delete, [this]() { RemoveLayer(); });
		// A locked layer can't be changed, which removing it would do the most
		remove.IsEnabled = [this]() {
			return (_currentLayer >= 0 && _currentLayer < std::int32_t(_document.Layers.size()) && !_document.Layers[_currentLayer].IsSpriteLayer() &&
				(_document.Layers[_currentLayer].EditorFlags & DocumentLayer::EditorLocked) == 0);
		};
		auto& up = a.Add(ActionIds::LayerUp, _("Move Layer Up"), EditorIcon::ArrowUp, [this]() { MoveLayer(-1); });
		up.IsEnabled = [this]() { return (_currentLayer > 0); };
		auto& down = a.Add(ActionIds::LayerDown, _("Move Layer Down"), EditorIcon::ArrowDown, [this]() { MoveLayer(1); });
		down.IsEnabled = [this]() { return (_currentLayer + 1 < std::int32_t(_document.Layers.size())); };
		auto& resize = a.Add(ActionIds::LayerResize, _("Resize Layer"), EditorIcon::Fit, [this]() { ShowResizeLayerDialog(*this); });
		resize.IsEnabled = [this]() {
			return (_currentLayer >= 0 && _currentLayer < std::int32_t(_document.Layers.size()) &&
				(_document.Layers[_currentLayer].EditorFlags & DocumentLayer::EditorLocked) == 0);
		};
	}

	void EditorSession::CreateLevelActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::LevelProperties, _("Level Properties"), EditorIcon::Settings, [this]() { ShowLevelPropertiesDialog(*this); });
		a.Add(ActionIds::LevelTexts, _("Level Texts"), EditorIcon::None, [this]() { ShowLevelTextsDialog(*this); }).Key = { Keys::T, KeyMod::Ctrl };
		// The episode is the directory the level file is in, a level that wasn't saved yet is offered to be saved first
		a.Add(ActionIds::LevelEpisode, _("Episode"), EditorIcon::None, [this]() { ShowEpisodeDialog(*this); });
		a.Add(ActionIds::LevelStatistics, _("Statistics"), EditorIcon::None, [this]() { ShowLevelStatisticsDialog(*this); });
		a.Add(ActionIds::LevelCheck, _("Check Level"), EditorIcon::None, [this]() { ShowCheckLevelDialog(*this); });
		auto& snap = a.Add(ActionIds::LevelSnapOffGrid, _("Snap Off-Grid Events to Grid"), EditorIcon::None, [this]() { SnapOffGridEvents(); });
		snap.IsEnabled = [this]() { return !_document.OffGridEvents.empty(); };
		a.Add(ActionIds::LevelAnimations, _("Animated Tiles"), EditorIcon::Tiles, [this]() { ShowAnimatedTilesDialog(*this); });
		a.Add(ActionIds::LevelTileSets, _("Tile Sets"), EditorIcon::None, [this]() { ShowTileSetsDialog(*this); });
		a.Add(ActionIds::LevelPalette, _("Palette"), EditorIcon::None, [this]() { ShowLevelPaletteDialog(*this); });
		// The first tile of the brush, a tile of the tile picker is opened by a double click on it too
		auto& editTile = a.Add(ActionIds::LevelEditTile, _("Edit Tile of the Brush"), EditorIcon::Brush, [this]() {
			if (!_stamp.IsEmpty()) {
				ShowTileEditor(*this, _stamp.Tiles[0]);
			}
		});
		editTile.IsEnabled = [this]() { return (!_stamp.IsEmpty() && !_stamp.Tiles[0].IsEmpty()); };
		a.Add(ActionIds::LevelExportTileSet, _("Export Tile Set Image"), EditorIcon::Export, [this]() { _files->ShowExportTileSetImageDialog(); });
		a.Add(ActionIds::LevelNewTileSet, _("New Tile Set"), EditorIcon::New, [this]() { ShowNewTileSetDialog(*this); });
		a.Add(ActionIds::LevelImportTileSet, _("New Tile Set from Image"), EditorIcon::New, [this]() { ShowImportTileSetDialog(*this); });
		a.Add(ActionIds::LevelAddTiles, _("Add Tiles from Image"), EditorIcon::Plus, [this]() { ShowAddTilesFromImageDialog(*this); });
		a.Add(ActionIds::LevelFxTiles, _("Generate Effect Tiles"), EditorIcon::None, [this]() { ShowFxTileGeneratorDialog(*this); });
		a.Add(ActionIds::LevelApplyTileChanges, _("Save Tile Changes into Tile Set"), EditorIcon::Save, [this]() { ShowApplyTileChangesDialog(*this); });
		a.Add(ActionIds::LevelRearrangeTileSet, _("Rearrange Tile Set"), EditorIcon::Tiles, [this]() { ShowTileSetLayoutDialog(*this); });
		a.Add(ActionIds::LevelReloadEvents, _("Read Event Descriptions Again"), EditorIcon::None, [this]() {
			_files->LoadEventExtensions(true);
			ShowMessage(_("Event packs and the script of the level were read again"));
		});
	}

	void EditorSession::CreateLayerSelectActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::LayerPrevious, _("Previous Layer"), EditorIcon::None, [this]() { SetCurrentLayer(_currentLayer - 1); }).Key = { Keys::PageUp, 0 };
		a.Add(ActionIds::LayerNext, _("Next Layer"), EditorIcon::None, [this]() { SetCurrentLayer(_currentLayer + 1); }).Key = { Keys::PageDown, 0 };
		// A layer by its number key, as other editors choose them, Shift with the key hides it (see OnKeyPressed())
		for (std::int32_t number = 1; number <= 9; number++) {
			auto& select = a.Add(String(StringView(ActionIds::LayerSelectPrefix) + String(std::to_string(number).c_str())), _f("Layer {}", number), EditorIcon::None,
				[this, number]() { SetCurrentLayer(number - 1); });
			select.Key = { Keys(std::int32_t(Keys::D1) + number - 1), 0 };
			select.IsEnabled = [this, number]() { return (number <= std::int32_t(_document.Layers.size())); };
		}
	}

	void EditorSession::CreatePlayActions()
	{
		auto& a = _actions;
		auto isEditing = [this]() { return (_playTest->IsEditing()); };
		auto isInGame = [this]() { return _playTest->IsInGame(); };
		auto& play = a.Add(ActionIds::PlayResume, _("Play"), EditorIcon::Play, [this]() { StartPlayTest(PlayStartMode::Resume); });
		play.Key = { Keys::F5, 0 };
		play.IsEnabled = isEditing;
		auto& here = a.Add(ActionIds::PlayHere, _("Play Here"), EditorIcon::Play, [this]() { StartPlayTest(PlayStartMode::Here); });
		here.Key = { Keys::F5, KeyMod::Shift };
		here.IsEnabled = isEditing;
		auto& start = a.Add(ActionIds::PlayStart, _("Play from Start"), EditorIcon::Play, [this]() { StartPlayTest(PlayStartMode::FromStart); });
		start.Key = { Keys::F5, KeyMod::Ctrl };
		start.IsEnabled = isEditing;
		// The key that plays goes back while the level is played, the place of the player is kept for "Play"
		auto& stop = a.Add(ActionIds::PlayStop, _("Back to Editor"), EditorIcon::EnterEditor, [this]() { StopPlayTest(true); });
		stop.Key = { Keys::F5, 0 };
		stop.Scope = ActionScope::Play;
		stop.IsEnabled = isInGame;
		struct PlayChoice { const char* Id; const char* Label; std::int32_t Value; bool Character; };
		static const PlayChoice Choices[] = {
			{ ActionIds::PlayJazz, "Jazz", std::int32_t(PlayerType::Jazz), true },
			{ ActionIds::PlaySpaz, "Spaz", std::int32_t(PlayerType::Spaz), true },
			{ ActionIds::PlayLori, "Lori", std::int32_t(PlayerType::Lori), true },
			{ ActionIds::PlayEasy, "Easy", std::int32_t(GameDifficulty::Easy), false },
			{ ActionIds::PlayNormal, "Normal", std::int32_t(GameDifficulty::Normal), false },
			{ ActionIds::PlayHard, "Hard", std::int32_t(GameDifficulty::Hard), false },
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
			{ ActionIds::PlayAlone, N_("Alone"), MpGameMode::Unknown },
			{ ActionIds::PlayCooperation, N_("Cooperation"), MpGameMode::Cooperation },
			{ ActionIds::PlayBattle, N_("Battle"), MpGameMode::Battle },
			{ ActionIds::PlayTeamBattle, N_("Team Battle"), MpGameMode::TeamBattle },
			{ ActionIds::PlayRace, N_("Race"), MpGameMode::Race },
			{ ActionIds::PlayTreasureHunt, N_("Treasure Hunt"), MpGameMode::TreasureHunt },
			{ ActionIds::PlayCaptureTheFlag, N_("Capture the Flag"), MpGameMode::CaptureTheFlag },
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
			auto& action = a.Add(String(StringView(ActionIds::PlayPlayersPrefix) + String(std::to_string(count).c_str())), _fn("{} Player", "{} Players", count, count), EditorIcon::None, [this, count]() {
				_settings.PlayPlayerCount = count;
				_settings.Save();
			});
			action.IsChecked = [this, count]() { return (_settings.PlayPlayerCount == count); };
			// Players of multiplayer only
			action.IsEnabled = [this]() { return (_settings.PlayGameMode != 0); };
		}
#endif
	}

	void EditorSession::CreatePlayTestDebugActions()
	{
		auto& a = _actions;
		auto isInGame = [this]() { return _playTest->IsInGame(); };
		auto& path = a.Add(ActionIds::ViewPlayPath, _("Show Play Path"), EditorIcon::Path, [this]() { _showPlayPath = !_showPlayPath; });
		path.Key = { Keys::L, KeyMod::Ctrl };
		path.IsChecked = [this]() { return _showPlayPath; };

		// Debug toggles of play-tests, kept for all of them, the menus toggle them anytime
		struct DebugToggle { const char* Id; const char* Label; bool EditorSettings::PlayTestSettings::*Option; const char* On; const char* Off; };
		static const DebugToggle DebugToggles[] = {
			{ ActionIds::PlayHitboxes, N_("Show Hitboxes"), &EditorSettings::PlayTestSettings::ShowHitboxes, N_("Hitboxes are shown"), N_("Hitboxes are hidden") },
			{ ActionIds::PlayEventZones, N_("Show Event Zones"), &EditorSettings::PlayTestSettings::ShowEventZones, N_("Event zones are shown"), N_("Event zones are hidden") },
		};
		for (const DebugToggle& toggle : DebugToggles) {
			bool EditorSettings::PlayTestSettings::*option = toggle.Option;
			const char* on = toggle.On;
			const char* off = toggle.Off;
			auto& action = a.Add(toggle.Id, _(toggle.Label), EditorIcon::None, [this, option, on, off]() {
				TogglePlayTestDebug(option, _(on), _(off));
			});
			action.Scope = ActionScope::Play;
			action.IsChecked = [this, option]() { return _settings.PlayTest.*option; };
		}
		auto& slowMotion = a.Add(ActionIds::PlaySlowMotion, _("Slow Motion"), EditorIcon::None, [this]() { CyclePlayTestTimeScale(); });
		slowMotion.Key = { Keys::F9, 0 };
		slowMotion.Scope = ActionScope::Play;
		slowMotion.IsChecked = [this]() { return (_settings.PlayTest.TimeScale < 1.0f); };
		auto& pause = a.Add(ActionIds::PlayPause, _("Stop Time of Level"), EditorIcon::Pause, [this]() { TogglePlayTestPause(); });
		pause.Key = { Keys::F7, 0 };
		pause.Scope = ActionScope::Play;
		pause.IsEnabled = isInGame;
		pause.IsChecked = [this]() { return _playTest->GetDebugOptions().Paused; };
		auto& step = a.Add(ActionIds::PlayStep, _("Next Frame"), EditorIcon::None, [this]() { _playTest->StepFrame(); });
		step.Key = { Keys::F8, 0 };
		step.Scope = ActionScope::Play;
		step.IsEnabled = isInGame;
		// The pointer is over the game while it's played, so it's done by the key
		auto& teleport = a.Add(ActionIds::PlayTeleport, _("Move Player to Pointer"), EditorIcon::None, [this]() { TeleportPlayTestPlayer(); });
		teleport.Key = { Keys::F6, 0 };
		teleport.Scope = ActionScope::Play;
		teleport.IsEnabled = isInGame;

		// What is shown of the paths of the play-tests kept with the level
		auto& deaths = a.Add(ActionIds::ViewDeaths, _("Show Deaths in Play-Tests"), EditorIcon::None, [this]() {
			_settings.PlayTest.ShowDeaths = !_settings.PlayTest.ShowDeaths;
			_settings.Save();
		});
		deaths.IsChecked = [this]() { return _settings.PlayTest.ShowDeaths; };
		auto& clearPaths = a.Add(ActionIds::PlayClearPaths, _("Forget Paths of Play-Tests"), EditorIcon::None, [this]() {
			_playTest->ClearPathHistory();
			_document.SetChunk(PlayPathHistory::Chunk, {});
			// Kept in the level, so it's a part of it to save, but not a change to undo
			_history->MarkDirty();
			_files->UpdateWindowTitle();
		});
		clearPaths.IsEnabled = [this]() { return !_playTest->GetPathHistory().IsEmpty(); };
	}

	void EditorSession::CreateReachabilityActions()
	{
#if defined(WITH_MULTIPLAYER)
		auto& a = _actions;
		// The route tracer of multiplayer finds it, without it there's no way to tell
		auto& reach = a.Add(ActionIds::ViewReachability, _("Show Where the Player Can Get"), EditorIcon::Reach, [this]() {
			_playOverlays->ToggleReachability();
		});
		reach.Key = { Keys::R, KeyMod::Ctrl | KeyMod::Shift };
		reach.IsChecked = [this]() { return _playOverlays->IsReachabilityShown(); };
		// Who gets there, the moves of each character differ, and from where
		struct ReachChoice { const char* Id; const char* Label; std::int32_t Character; };
		static const ReachChoice ReachChoices[] = {
			{ ActionIds::ViewReachAny, N_("Reachable with Any Character"), 0 },
			{ ActionIds::ViewReachJazz, N_("Reachable with Jazz"), std::int32_t(PlayerType::Jazz) },
			{ ActionIds::ViewReachSpaz, N_("Reachable with Spaz"), std::int32_t(PlayerType::Spaz) },
			{ ActionIds::ViewReachLori, N_("Reachable with Lori"), std::int32_t(PlayerType::Lori) },
		};
		for (const ReachChoice& choice : ReachChoices) {
			std::int32_t character = choice.Character;
			auto& action = a.Add(choice.Id, _(choice.Label), EditorIcon::None, [this, character]() {
				_settings.PlayTest.ReachCharacter = character;
				_settings.Save();
				if (_playOverlays->IsReachabilityShown()) {
					_playOverlays->RecomputeReachability(false);
				}
			});
			action.IsChecked = [this, character]() { return (_settings.PlayTest.ReachCharacter == character); };
		}
		auto& reachMp = a.Add(ActionIds::ViewReachFromMpStarts, _("Reachable from Multiplayer Starts Too"), EditorIcon::None, [this]() {
			_settings.PlayTest.ReachFromMpStarts = !_settings.PlayTest.ReachFromMpStarts;
			_settings.Save();
			if (_playOverlays->IsReachabilityShown()) {
				_playOverlays->RecomputeReachability(false);
			}
		});
		reachMp.IsChecked = [this]() { return _settings.PlayTest.ReachFromMpStarts; };
#endif
	}

	void EditorSession::CreateScriptAndCompareActions()
	{
		auto& a = _actions;
		// The script of the level, opened in the program of the settings or the one the system has for it
		a.Add(ActionIds::LevelEditScript, _("Edit Script"), EditorIcon::None, [this]() { _files->EditScript(); });
		// Where the level differs from the file on disk
		auto& compare = a.Add(ActionIds::ViewCompareSaved, _("Compare with Saved"), EditorIcon::None, [this]() {
			_comparison->Toggle(_input->IsToolPointerDown());
		});
		compare.IsChecked = [this]() { return _comparison->IsShown(); };
	}

	void EditorSession::CancelInteractionsBeforeActions()
	{
		// Whatever the pointer is in the middle of ends before an action changes the level under it, by its key also what
		// the interface holds; only looking at the level and the help leave a stroke alone, as zooming while painting
		for (const auto& action : _actions.GetAll()) {
			if (!action->Execute || action->Id.hasPrefix(ActionIds::ViewPrefix) || action->Id.hasPrefix(ActionIds::HelpPrefix)) {
				continue;
			}
			auto execute = std::make_shared<Function<void()>>(Death::move(action->Execute));
			action->Execute = [this, execute]() {
				if (!_input->IsInToolCallback()) {
					CancelInteractions(_input->IsHandlingShortcut());
				}
				(*execute)();
			};
		}
	}
}

#endif
