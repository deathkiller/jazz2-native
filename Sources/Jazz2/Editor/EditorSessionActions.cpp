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
		a.Add(ActionIds::FileNew, _x("editor", "New Level"), EditorIcon::New, [this]() {
			_files->ConfirmDiscard([this]() { _files->ShowNewLevelDialog(); });
		}).Key = { Keys::N, KeyMod::Ctrl };
		// TRANSLATORS: Menu item in the File menu, opens a level (verb)
		a.Add(ActionIds::FileOpen, _x("editor", "Open"), EditorIcon::Folder, [this]() {
			_files->ConfirmDiscard([this]() { _files->ShowOpenDialog(); });
		}).Key = { Keys::O, KeyMod::Ctrl };
		// Saving works while a text is typed, the text is confirmed first
		auto& save = a.Add(ActionIds::FileSave, _("Save"), EditorIcon::Save, [this]() { _files->Save(); });
		save.Key = { Keys::S, KeyMod::Ctrl };
		save.WorksWhileTyping = true;
		auto& saveAs = a.Add(ActionIds::FileSaveAs, _x("editor", "Save As"), EditorIcon::Save, [this]() { ShowSaveAsDialog(false); });
		saveAs.Key = { Keys::S, KeyMod::Ctrl | KeyMod::Shift };
		saveAs.WorksWhileTyping = true;
		// TRANSLATORS: Menu item in the File menu, saves a copy of the level without what only the editor uses (verb)
		// Without a key, Ctrl+E takes the event under the pointer, as in the editors of the original game
		a.Add(ActionIds::FileExport, _x("editor", "Export"), EditorIcon::Export, [this]() { _files->ShowExportDialog(); });
		// TRANSLATORS: Menu item in the File menu, packs the level with the files it needs into one archive to share
		a.Add(ActionIds::FileExportPackage, _x("editor", "Export Package"), EditorIcon::Export, [this]() { ShowExportPackageDialog(*this); });
		// TRANSLATORS: Menu item in the File menu, opens the list of copies of levels the editor saved by itself
		a.Add(ActionIds::FileRecovery, _x("editor", "Recovery Copies"), EditorIcon::None, [this]() { ShowRecoveryDialog(*this); });
		a.Add(ActionIds::FileExit, _("Exit Editor"), EditorIcon::CloseSmall, [this]() { _files->ExitEditor(); });
	}

	void EditorSession::CreateEditActions()
	{
		auto& a = _actions;
		{
			auto& undo = a.Add(ActionIds::EditUndo, _x("editor", "Undo"), EditorIcon::Undo, [this]() { Undo(); });
			undo.Key = { Keys::Z, KeyMod::Ctrl };
			undo.IsEnabled = [this]() { return _history->CanUndo(); };
			auto& redo = a.Add(ActionIds::EditRedo, _x("editor", "Redo"), EditorIcon::Redo, [this]() { Redo(); });
			redo.Key = { Keys::Y, KeyMod::Ctrl };
			redo.AlternateKey = { Keys::Z, KeyMod::Ctrl | KeyMod::Shift };
			redo.IsEnabled = [this]() { return _history->CanRedo(); };
		}
		auto hasSelection = [this]() { return HasSelection(); };
		auto& cut = a.Add(ActionIds::EditCut, _x("editor", "Cut"), EditorIcon::Cut, [this]() { CopySelection(true); });
		cut.Key = { Keys::X, KeyMod::Ctrl };
		cut.IsEnabled = hasSelection;
		auto& copy = a.Add(ActionIds::EditCopy, _x("editor", "Copy"), EditorIcon::Copy, [this]() { CopySelection(false); });
		copy.Key = { Keys::C, KeyMod::Ctrl };
		copy.IsEnabled = hasSelection;
		// TRANSLATORS: Menu item in the Edit menu, copies the selected tiles together with the events on them, pasted together
		auto& copyWithEvents = a.Add(ActionIds::EditCopyWithEvents, _x("editor", "Copy Tiles and Events"), EditorIcon::Copy, [this]() { CopyTilesAndEvents(); });
		copyWithEvents.Key = { Keys::C, KeyMod::Ctrl | KeyMod::Shift };
		copyWithEvents.IsEnabled = hasSelection;
		a.Add(ActionIds::EditPaste, _x("editor", "Paste"), EditorIcon::Paste, [this]() { Paste(); }).Key = { Keys::V, KeyMod::Ctrl };
		auto& remove = a.Add(ActionIds::EditDelete, _x("editor", "Delete"), EditorIcon::Delete, [this]() { DeleteSelection(); });
		remove.Key = { Keys::Delete, 0 };
		remove.IsEnabled = hasSelection;
		a.Add(ActionIds::EditSelectAll, _x("editor", "Select All"), EditorIcon::Select, [this]() { SelectAll(); }).Key = { Keys::A, KeyMod::Ctrl };
		a.Add(ActionIds::EditDeselect, _x("editor", "Deselect"), EditorIcon::None, [this]() { ClearSelection(); }).Key = { Keys::D, KeyMod::Ctrl };

		// A block grabbed by its corners, as B of Jazz Creation Station, in the level or in the picker of tiles; a held key
		// repeats, which would mark the corner and grab at once
		{
			struct GrabChoice { const char* Id; String Label; std::int32_t Modifiers; BlockGrab::Mode Mode; };
			const GrabChoice grabs[] = {
				// TRANSLATORS: Command in the Edit menu, pressed at a corner of a block of tiles and again at the other corner, the block becomes the brush, as B of Jazz Creation Station
				{ ActionIds::EditGrabBlock, _x("editor", "Grab Block"), 0, BlockGrab::Mode::Replace },
				// TRANSLATORS: Command in the Edit menu, as Grab Block, the block is added to the selected tiles and all of them become the brush
				{ ActionIds::EditGrabBlockAdd, _x("editor", "Grab Block Adding to Selection"), KeyMod::Shift, BlockGrab::Mode::Add },
				// TRANSLATORS: Command in the Edit menu, as Grab Block, the block is taken out of the selected tiles and the rest of them become the brush
				{ ActionIds::EditGrabBlockSubtract, _x("editor", "Grab Block Taking from Selection"), KeyMod::Ctrl, BlockGrab::Mode::Subtract },
			};
			for (const GrabChoice& grab : grabs) {
				BlockGrab::Mode mode = grab.Mode;
				auto& action = a.Add(grab.Id, grab.Label, mode == BlockGrab::Mode::Replace ? EditorIcon::Select : EditorIcon::None, [this, mode]() { GrabBlock(mode); });
				action.Key = { Keys::B, grab.Modifiers };
				action.IgnoresRepeat = true;
			}
			auto tilesSelected = [this]() { return (!IsEditingEvents() && HasSelection()); };
			// TRANSLATORS: Command, the selected tiles with the events on them become the brush, as Ctrl+C of the Multi-Layer Level Editor
			auto& copyToBrush = a.Add(ActionIds::EditCopyToBrush, _x("editor", "Copy into Brush"), EditorIcon::Copy, [this]() { BlockGrab::GrabSelection(*this, false); });
			copyToBrush.IsEnabled = tilesSelected;
			// TRANSLATORS: Command, the selected tiles with the events on them become the brush and are erased, as Ctrl+X of the Multi-Layer Level Editor
			auto& cutToBrush = a.Add(ActionIds::EditCutToBrush, _x("editor", "Cut into Brush"), EditorIcon::Cut, [this]() { BlockGrab::GrabSelection(*this, true); });
			cutToBrush.IsEnabled = tilesSelected;
		}

		// TRANSLATORS: Menu item in the Edit menu, opens the list of the changes that can be undone
		a.Add(ActionIds::EditHistory, _x("editor", "History"), EditorIcon::History, [this]() { ShowHistoryDialog(*this); }).Key = { Keys::H, KeyMod::Ctrl };
		a.Add(ActionIds::EditFindReplace, _x("editor", "Find and Replace"), EditorIcon::Search, [this]() { ShowFindReplaceDialog(*this); }).Key = { Keys::F, KeyMod::Ctrl };
		// TRANSLATORS: Menu item in the Edit menu, opens the saved ready-made pieces of tiles to place in the level
		a.Add(ActionIds::EditPrefabs, _x("editor", "Prefabs"), EditorIcon::Tiles, [this]() { ShowPrefabsDialog(*this); });
		a.Add(ActionIds::EditFillGround, _x("editor", "Fill Selection with Ground"), EditorIcon::Fill, [this]() { FillSelectionWithGround(*this); });
		a.Add(ActionIds::EditBeautify, _x("editor", "Beautify Selection"), EditorIcon::SmartBrush, [this]() { BeautifySelection(*this); });
		a.Add(ActionIds::EditBakeGround, _x("editor", "Make Tiles Along Ground Line"), EditorIcon::Slope, [this]() { BakeGroundLine(); });
		a.Add(ActionIds::EditContinueRow, _x("editor", "Continue Row by Suggestions"), EditorIcon::SmartBrush, [this]() { ContinueRow(); }).Key = { Keys::Right, KeyMod::Ctrl | KeyMod::Shift };
		// TRANSLATORS: Menu item in the Edit menu, opens the rules of which tiles go next to which for the smart tools
		a.Add(ActionIds::EditTerrainRules, _x("editor", "Terrain Rules"), EditorIcon::SmartBrush, [this]() { ShowTerrainRulesDialog(*this); });
		a.Add(ActionIds::EditSmartRetile, _x("editor", "Choose Tiles by Their Neighbors"), EditorIcon::SmartBrush, [this]() { RetileSelection(); });
		a.Add(ActionIds::EditTraceGround, _x("editor", "Make Ground Line from Selection"), EditorIcon::Slope, [this]() {
			if (GroundLineTool::TraceSelection(*this)) {
				_tools->SetActive(EditorToolType::Ground);
			}
		});
	}

	void EditorSession::CreateHelpActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::HelpCommands, _x("editor", "Command Palette"), EditorIcon::None, [this]() { ShowCommandPalette(*this); }).Key = { Keys::K, KeyMod::Ctrl };
		a.Add(ActionIds::HelpShortcuts, _x("editor", "Keyboard Shortcuts"), EditorIcon::None, [this]() { ShowShortcutsDialog(*this); }).Key = { Keys::F1, 0 };
	}

	void EditorSession::CreateNavigationActions()
	{
		auto& a = _actions;
		// The key shows the navigator or hides it again
		// TRANSLATORS: Menu item in the View menu, shows a small map of the level with its notes and bookmarks
		a.Add(ActionIds::ViewNavigator, _x("editor", "Navigator"), EditorIcon::Map, [this]() { ShowNavigator(*this, true); }).Key = { Keys::F2, 0 };
		// TRANSLATORS: Menu item in the View menu, places a written note for the level authors where the pointer is
		a.Add(ActionIds::ViewAddNote, _x("editor", "Add Note"), EditorIcon::Plus, [this]() {
			// Where the pointer is over the level, otherwise in the middle of the view
			ToolPointer hover;
			Vector2f pos = (GetHoverPointer(hover) ? hover.World : (_editHandler != nullptr ? _editHandler->GetCamera().GetCenter() : Vector2f()));
			AddNote(*this, pos);
			ShowNavigator(*this);
		}).Key = { Keys::N, KeyMod::Ctrl | KeyMod::Shift };
		for (std::int32_t slot = 1; slot <= 9; slot++) {
			Keys key = Keys(std::int32_t(Keys::D1) + slot - 1);
			// TRANSLATORS: Command that remembers the current view as a bookmark, {} is its number from 1 to 9
			a.Add(String(StringView(ActionIds::BookmarkSetPrefix) + String(std::to_string(slot).c_str())), _fx("editor", "Set Bookmark {}", slot), EditorIcon::None,
				[this, slot]() { SetBookmark(*this, slot); }).Key = { key, KeyMod::Ctrl | KeyMod::Shift };
			// TRANSLATORS: Command that moves the view to a bookmark, {} is its number from 1 to 9
			a.Add(String(StringView(ActionIds::BookmarkGoPrefix) + String(std::to_string(slot).c_str())), _fx("editor", "Go to Bookmark {}", slot), EditorIcon::None,
				[this, slot]() { GoToBookmark(*this, slot); }).Key = { key, KeyMod::Ctrl };
		}
	}

	void EditorSession::CreateEventActions()
	{
		auto& a = _actions;
		auto& sameType = a.Add(ActionIds::EventsSelectSameType, _x("editor", "Select Events of the Same Kind"), EditorIcon::Event, [this]() { SelectEventsOfSameType(); });
		sameType.IsEnabled = [this]() { return IsEditingEvents() && !_eventSelection.IsEmpty(); };
		auto& matching = a.Add(ActionIds::EventsCopyToMatchingTiles, _x("editor", "Copy Event to Every Tile Like Its Own"), EditorIcon::Event, [this]() { CopyEventToMatchingTiles(); });
		matching.IsEnabled = [this]() { return IsEditingEvents() && _eventSelection.GetCellCount() == 1; };
	}

	void EditorSession::CreateStampActions()
	{
		auto& a = _actions;
		// TRANSLATORS: Command that mirrors the brush, the tiles being painted with, left to right
		a.Add(ActionIds::StampFlipX, _x("editor", "Flip Stamp Horizontally"), EditorIcon::FlipX, [this]() { _stamp.FlipX(); }).Key = { Keys::X, 0 };
		// TRANSLATORS: Command that mirrors the brush, the tiles being painted with, top to bottom
		a.Add(ActionIds::StampFlipY, _x("editor", "Flip Stamp Vertically"), EditorIcon::FlipY, [this]() { _stamp.FlipY(); }).Key = { Keys::Y, 0 };
		// TRANSLATORS: Toggle, each painted cell gets a random one of the tiles of the brush instead of the brush as it is
		auto& random = a.Add(ActionIds::StampRandom, _x("editor", "Paint Tiles of Stamp at Random"), EditorIcon::None, [this]() { _stamp.Random = !_stamp.Random; });
		random.Key = { Keys::Z, 0 };
		random.IsChecked = [this]() { return _stamp.Random; };
		auto& reroll = a.Add(ActionIds::StampReroll, _x("editor", "Choose Other Tiles at Random"), EditorIcon::None, [this]() { _stamp.Seed++; });
		reroll.Key = { Keys::Z, KeyMod::Shift };
		reroll.IsEnabled = [this]() { return _stamp.Random; };
	}

	void EditorSession::CreateToolActions()
	{
		auto& a = _actions;
		struct ToolInfo { const char* Id; String Label; EditorIcon Icon; Keys Key; EditorToolType Type; };
		static const ToolInfo Tools[] = {
			// TRANSLATORS: Names of the tools in the Tools menu and the tool strip, nouns ("Fill" is the bucket fill tool, "Hand" moves the view)
			// D for draw, B grabs a block as in the editors of the original game
			{ ActionIds::ToolBrush, _x("editor", "Brush"), EditorIcon::Brush, Keys::D, EditorToolType::Brush },
			{ ActionIds::ToolEraser, _x("editor", "Eraser"), EditorIcon::Eraser, Keys::E, EditorToolType::Eraser },
			{ ActionIds::ToolRectangle, _x("editor", "Rectangle"), EditorIcon::Rectangle, Keys::R, EditorToolType::Rectangle },
			{ ActionIds::ToolFill, _x("editor", "Fill"), EditorIcon::Fill, Keys::G, EditorToolType::Fill },
			{ ActionIds::ToolEyedropper, _x("editor", "Eyedropper"), EditorIcon::Eyedropper, Keys::I, EditorToolType::Eyedropper },
			{ ActionIds::ToolSelect, _x("editor", "Select"), EditorIcon::Select, Keys::M, EditorToolType::Select },
			{ ActionIds::ToolWand, _x("editor", "Magic Wand"), EditorIcon::Wand, Keys::W, EditorToolType::Wand },
			{ ActionIds::ToolHand, _x("editor", "Hand"), EditorIcon::Hand, Keys::H, EditorToolType::Hand },
			{ ActionIds::ToolEvent, _x("editor", "Events"), EditorIcon::Event, Keys::Q, EditorToolType::Event },
			{ ActionIds::ToolMeasure, _x("editor", "Measure"), EditorIcon::Measure, Keys::U, EditorToolType::Measure },
			{ ActionIds::ToolPath, _x("editor", "Events Along Path"), EditorIcon::EventPath, Keys::K, EditorToolType::Path },
			{ ActionIds::ToolSmart, _x("editor", "Smart Brush"), EditorIcon::SmartBrush, Keys::T, EditorToolType::Smart },
			{ ActionIds::ToolGround, _x("editor", "Ground Line"), EditorIcon::Slope, Keys::P, EditorToolType::Ground },
		};
		for (const ToolInfo& info : Tools) {
			EditorToolType type = info.Type;
			// The panel at the bottom follows the tool, see ToolManager::OnChosen in the constructor
			auto& action = a.Add(info.Id, info.Label, info.Icon, [this, type]() { _tools->SetActive(type); });
			action.Key = { info.Key, 0 };
			action.IsChecked = [this, type]() { return (_tools->GetActiveType() == type); };
		}
	}

	void EditorSession::CreateViewActions()
	{
		auto& a = _actions;
		{
			auto& grid = a.Add(ActionIds::ViewGrid, _x("editor", "Show Grid"), EditorIcon::Grid, [this]() {
				_showGrid = !_showGrid;
				_settings.ShowGrid = _showGrid;
				_settings.Save();
			});
			grid.Key = { Keys::G, KeyMod::Ctrl };
			grid.IsChecked = [this]() { return _showGrid; };

			// TRANSLATORS: Toggle in the View menu and the toolbar, draws all layers aligned with the sprite layer, without their parallax
			auto& flat = a.Add(ActionIds::ViewFlat, _x("editor", "Flat Layers"), EditorIcon::Parallax, [this]() {
				if (Tiles::TileMap* tileMap = GetTileMap()) {
					Tiles::TileMapViewOptions options = tileMap->GetViewOptions();
					options.FlatAmount = (options.FlatAmount > 0.5f ? 0.0f : 1.0f);
					tileMap->SetViewOptions(options);
				}
			});
			flat.Key = { Keys::P, KeyMod::Ctrl };

			// TRANSLATORS: Toggle in the View menu and the toolbar, shows the collision masks of the tiles, the solid parts
			auto& masks = a.Add(ActionIds::ViewMasks, _x("editor", "Show Masks"), EditorIcon::Mask, [this]() {
				// Over the tiles, instead of them, not at all; while peeking they stay as they're chosen now
				_maskPeek = false;
				_maskMode = (_maskMode + 1) % 3;
				if (_maskMode == 0) {
					// Its images are made again quickly, they'd only take memory meanwhile
					_maskOverlay->Release();
				}
			});
			masks.Key = { Keys::M, KeyMod::Ctrl };
			masks.IsChecked = [this]() { return (_maskMode != 0); };

			// While its key is held the masks are shown over the tiles, as M of the Multi-Layer Level Editor; chosen from a
			// menu, which has no key to release, it shows or hides them as Show Masks does
			// TRANSLATORS: Command whose key shows the collision masks over the tiles only while it's held
			auto& peekMasks = a.Add(ActionIds::ViewPeekMasks, _x("editor", "Show Masks While Held"), EditorIcon::Mask, [this]() {
				if (!_input->IsHandlingShortcut()) {
					_actions.Execute(ActionIds::ViewMasks);
				} else if (_maskMode == 0) {
					_maskMode = 1;
					_maskPeek = true;
				}
			});
			peekMasks.Release = [this]() {
				if (_maskPeek) {
					_maskPeek = false;
					_maskMode = 0;
					_maskOverlay->Release();
				}
			};
			peekMasks.IgnoresRepeat = true;

			auto& events = a.Add(ActionIds::ViewEvents, _x("editor", "Show Events"), EditorIcon::Event, [this]() { _showEvents = !_showEvents; });
			events.Key = { Keys::E, KeyMod::Ctrl | KeyMod::Shift };
			events.IsChecked = [this]() { return _showEvents; };

			// The layer being edited always stays still, this keeps all of them still
			auto& pauseLayers = a.Add(ActionIds::ViewPauseLayers, _x("editor", "Pause Layer Movement"), EditorIcon::Pause, [this]() {
				_settings.PauseLayerMovement = !_settings.PauseLayerMovement;
				_settings.Save();
			});
			pauseLayers.Key = { Keys::P, KeyMod::Ctrl | KeyMod::Shift };
			pauseLayers.IsChecked = [this]() { return _settings.PauseLayerMovement; };

			// TRANSLATORS: Toggle in the View menu and the properties of a textured layer, the layer being edited is drawn as in the game instead of as plain tiles
			auto& previewTextured = a.Add(ActionIds::ViewPreviewTextured, _x("editor", "Show Textured Layers"), EditorIcon::Eye, [this]() {
				_settings.PreviewTexturedLayers = !_settings.PreviewTexturedLayers;
				_settings.Save();
				UpdateLayerOverride();
			});
			previewTextured.Key = { Keys::T, KeyMod::Ctrl | KeyMod::Shift };
			previewTextured.IsChecked = [this]() { return _settings.PreviewTexturedLayers; };
#if defined(WITH_ANGELSCRIPT)
			// TRANSLATORS: Toggle in the View menu, events are drawn with the sprites the script of the level gives their objects
			auto& overriddenAssets = a.Add(ActionIds::ViewOverriddenAssets, _x("editor", "Show Overridden Assets"), EditorIcon::OverriddenAssets, [this]() {
				_settings.OverriddenAssets = !_settings.OverriddenAssets;
				_settings.Save();
				// Taken again when it's turned on, the script may have changed meanwhile
				_scriptGraphicsDirty = true;
			});
			overriddenAssets.IsChecked = [this]() { return _settings.OverriddenAssets; };
#endif
			flat.IsChecked = [this]() {
				Tiles::TileMap* tileMap = GetTileMap();
				return (tileMap != nullptr && tileMap->GetViewOptions().FlatAmount > 0.5f);
			};
		}
		// The level handler is missing only for the frame after it couldn't be loaded again, before the main menu
		a.Add(ActionIds::ViewZoomIn, _x("editor", "Zoom In"), EditorIcon::ZoomIn, [this]() {
			if (EditorCamera* camera = GetCamera()) {
				Rectf view = GetViewRect();
				camera->ZoomSteps(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), 1.0f);
			}
		}).Key = { Keys::Plus, 0 };
		a.Add(ActionIds::ViewZoomOut, _x("editor", "Zoom Out"), EditorIcon::ZoomOut, [this]() {
			if (EditorCamera* camera = GetCamera()) {
				Rectf view = GetViewRect();
				camera->ZoomSteps(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), -1.0f);
			}
		}).Key = { Keys::Minus, 0 };
		EditorAction& zoomGame = a.Add(ActionIds::ViewZoomGame, _x("editor", "Zoom as in Game"), EditorIcon::None, [this]() {
			if (EditorCamera* camera = GetCamera()) {
				Rectf view = GetViewRect();
				camera->ZoomTo(Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f), camera->SnapScale(camera->GetGameplayScale(), 0));
			}
		});
		// The digits of the numeric keypad are keys of their own, the zero there zooms the same way
		zoomGame.Key = { Keys::D0, 0 };
		zoomGame.AlternateKey = { Keys::NumPad0, 0 };
		a.Add(ActionIds::ViewFit, _x("editor", "Show Whole Level"), EditorIcon::Fit, [this]() {
			Tiles::TileMap* tileMap = GetTileMap();
			EditorCamera* camera = GetCamera();
			if (tileMap != nullptr && camera != nullptr) {
				Vector2i levelBounds = tileMap->GetLevelBounds();
				camera->Fit(Rectf(0.0f, 0.0f, float(levelBounds.X), float(levelBounds.Y)), true);
			}
		}).Key = { Keys::Home, 0 };

		// Size of the interface, in steps that keep it sharp where they can
		a.Add(ActionIds::ViewUiLarger, _x("editor", "Larger Interface"), EditorIcon::None, [this]() { ChangeUiScale(1); }).Key = { Keys::Plus, KeyMod::Ctrl | KeyMod::Shift };
		a.Add(ActionIds::ViewUiSmaller, _x("editor", "Smaller Interface"), EditorIcon::None, [this]() { ChangeUiScale(-1); }).Key = { Keys::Minus, KeyMod::Ctrl | KeyMod::Shift };
		EditorAction& uiAuto = a.Add(ActionIds::ViewUiAuto, _x("editor", "Interface Size by System"), EditorIcon::None, [this]() { ChangeUiScale(0); });
		uiAuto.Key = { Keys::D0, KeyMod::Ctrl | KeyMod::Shift };
		uiAuto.AlternateKey = { Keys::NumPad0, KeyMod::Ctrl | KeyMod::Shift };

		// Layout for the screen, and how much is offered
		struct LayoutChoice { const char* Id; String Label; std::int32_t Value; };
		static const LayoutChoice Layouts[] = {
			// TRANSLATORS: Choices of the layout of the editor in the View menu, drawers are panels that slide in from the side
			{ ActionIds::ViewLayoutAuto, _x("editor", "Layout by Window Size"), 0 },
			{ ActionIds::ViewLayoutDesktop, _x("editor", "Desktop Layout"), 1 },
			{ ActionIds::ViewLayoutCompact, _x("editor", "Compact Layout, Panels in Drawers"), 2 },
		};
		for (const LayoutChoice& choice : Layouts) {
			std::int32_t value = choice.Value;
			auto& layout = a.Add(choice.Id, choice.Label, EditorIcon::None, [this, value]() {
				_settings.Layout = value;
				_settings.Save();
			});
			layout.IsChecked = [this, value]() { return (_settings.Layout == value); };
		}
		auto& expert = a.Add(ActionIds::ViewExpert, _x("editor", "Expert Mode"), EditorIcon::Settings, [this]() {
			_settings.Expert = !_settings.Expert;
			_settings.Save();
			ShowMessage(_settings.Expert ? _x("editor", "All tools and commands are offered") : _x("editor", "Only the simple tools and commands are offered"));
		});
		expert.IsChecked = [this]() { return _settings.Expert; };
		// How the interface looks and moves, for eyes that need it
		auto applyTheme = [this]() {
			UiTheme::Apply(UiThemeKind(_settings.Theme), _settings.ReducedMotion, _settings.ReadableFont);
			_settings.Save();
		};
		auto& highContrast = a.Add(ActionIds::ViewHighContrast, _x("editor", "High Contrast Interface"), EditorIcon::None, [this, applyTheme]() {
			_settings.Theme = (_settings.Theme == std::int32_t(UiThemeKind::HighContrast) ? std::int32_t(UiThemeKind::Default) : std::int32_t(UiThemeKind::HighContrast));
			applyTheme();
		});
		highContrast.IsChecked = [this]() { return (_settings.Theme == std::int32_t(UiThemeKind::HighContrast)); };
		// TRANSLATORS: Menu item in the View menu, text is shown in a plain font instead of the one of the game
		auto& readableFont = a.Add(ActionIds::ViewReadableFont, _x("editor", "Readable Font"), EditorIcon::None, [this, applyTheme]() {
			_settings.ReadableFont = !_settings.ReadableFont;
			applyTheme();
		});
		readableFont.IsChecked = [this]() { return _settings.ReadableFont; };
		auto& reducedMotion = a.Add(ActionIds::ViewReducedMotion, _x("editor", "Reduce Motion"), EditorIcon::None, [this, applyTheme]() {
			_settings.ReducedMotion = !_settings.ReducedMotion;
			applyTheme();
		});
		reducedMotion.IsChecked = [this]() { return _settings.ReducedMotion; };
		// TRANSLATORS: Button in the toolbar of the compact layout, opens the drawer with the picker of tiles and events
		auto& drawerTiles = a.Add(ActionIds::ViewDrawerTiles, _x("editor", "Tiles and Events"), EditorIcon::Tiles, [this]() {
			if (_shell != nullptr) {
				_shell->ToggleDrawer(true);
			}
		});
		drawerTiles.IsChecked = [this]() { return (_shell != nullptr && _shell->IsDrawerOpen(true)); };
		// TRANSLATORS: Button in the toolbar of the compact layout, opens the drawer with the layers and their properties
		auto& drawerLayers = a.Add(ActionIds::ViewDrawerLayers, _x("editor", "Layers and Properties"), EditorIcon::Layers, [this]() {
			if (_shell != nullptr) {
				_shell->ToggleDrawer(false);
			}
		});
		drawerLayers.IsChecked = [this]() { return (_shell != nullptr && _shell->IsDrawerOpen(false)); };
	}

	void EditorSession::CreateLayerActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::LayerAdd, _x("editor", "Add Layer"), EditorIcon::Plus, [this]() { AddLayer(); });
		auto& remove = a.Add(ActionIds::LayerRemove, _x("editor", "Remove Layer"), EditorIcon::Delete, [this]() { RemoveLayer(); });
		// A locked layer can't be changed, which removing it would do the most
		remove.IsEnabled = [this]() {
			return (_currentLayer >= 0 && _currentLayer < std::int32_t(_document.Layers.size()) && !_document.Layers[_currentLayer].IsSpriteLayer() &&
				(_document.Layers[_currentLayer].EditorFlags & DocumentLayer::EditorLocked) == 0);
		};
		auto& up = a.Add(ActionIds::LayerUp, _x("editor", "Move Layer Up"), EditorIcon::ArrowUp, [this]() { MoveLayer(-1); });
		up.IsEnabled = [this]() { return (_currentLayer > 0); };
		auto& down = a.Add(ActionIds::LayerDown, _x("editor", "Move Layer Down"), EditorIcon::ArrowDown, [this]() { MoveLayer(1); });
		down.IsEnabled = [this]() { return (_currentLayer + 1 < std::int32_t(_document.Layers.size())); };
		auto& resize = a.Add(ActionIds::LayerResize, _x("editor", "Resize Layer"), EditorIcon::Fit, [this]() { ShowResizeLayerDialog(*this); });
		resize.IsEnabled = [this]() {
			return (_currentLayer >= 0 && _currentLayer < std::int32_t(_document.Layers.size()) &&
				(_document.Layers[_currentLayer].EditorFlags & DocumentLayer::EditorLocked) == 0);
		};
	}

	void EditorSession::CreateLevelActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::LevelProperties, _x("editor", "Level Properties"), EditorIcon::Properties, [this]() { ShowLevelPropertiesDialog(*this); });
		// TRANSLATORS: Menu item in the Level menu, opens the list of the texts the level shows to the player
		a.Add(ActionIds::LevelTexts, _x("editor", "Level Texts"), EditorIcon::None, [this]() { ShowLevelTextsDialog(*this); }).Key = { Keys::T, KeyMod::Ctrl };
		// The episode is the directory the level file is in, a level that wasn't saved yet is offered to be saved first
		// TRANSLATORS: Menu item in the Level menu, opens the properties of the episode the level is in
		a.Add(ActionIds::LevelEpisode, _x("editor", "Episode"), EditorIcon::None, [this]() { ShowEpisodeDialog(*this); });
		// TRANSLATORS: Menu item in the Level menu, opens the counts of what the level has
		a.Add(ActionIds::LevelStatistics, _x("editor", "Statistics"), EditorIcon::None, [this]() { ShowLevelStatisticsDialog(*this); });
		// TRANSLATORS: Menu item in the Level menu, looks for problems in the level (verb)
		a.Add(ActionIds::LevelCheck, _x("editor", "Check Level"), EditorIcon::None, [this]() { ShowCheckLevelDialog(*this); });
		auto& snap = a.Add(ActionIds::LevelSnapOffGrid, _x("editor", "Snap Off-Grid Events to Grid"), EditorIcon::None, [this]() { SnapOffGridEvents(); });
		snap.IsEnabled = [this]() { return !_document.OffGridEvents.empty(); };
		a.Add(ActionIds::LevelAnimations, _x("editor", "Animated Tiles"), EditorIcon::Tiles, [this]() { ShowAnimatedTilesDialog(*this); });
		a.Add(ActionIds::LevelTileSets, _x("editor", "Tile Sets"), EditorIcon::None, [this]() { ShowTileSetsDialog(*this); });
		// TRANSLATORS: Menu item in the Level menu, opens the dialog of the palette of the level and its additional palettes
		a.Add(ActionIds::LevelPalette, _x("editor", "Palettes"), EditorIcon::None, [this]() { ShowPalettesDialog(*this); });
		// The first tile of the brush, a tile of the tile picker is opened by a double click on it too
		auto& editTile = a.Add(ActionIds::LevelEditTile, _x("editor", "Edit Tile of the Brush"), EditorIcon::Brush, [this]() {
			if (!_stamp.IsEmpty()) {
				ShowTileEditor(*this, _stamp.Tiles[0]);
			}
		});
		editTile.IsEnabled = [this]() { return (!_stamp.IsEmpty() && !_stamp.Tiles[0].IsEmpty()); };
		a.Add(ActionIds::LevelExportTileSet, _x("editor", "Export Tile Set Image"), EditorIcon::Export, [this]() { ShowExportTileSetImageDialog(*this, TileRef::DefaultTileSet); });
		a.Add(ActionIds::LevelNewTileSet, _x("editor", "New Tile Set"), EditorIcon::New, [this]() { ShowNewTileSetDialog(*this); });
		a.Add(ActionIds::LevelImportTileSet, _x("editor", "New Tile Set from Image"), EditorIcon::New, [this]() { ShowImportTileSetDialog(*this); });
		// TRANSLATORS: Command that converts a tile set file of the original game (.j2t) into a new tile set, or copies one of this game
		a.Add(ActionIds::LevelImportTileSetFile, _x("editor", "New Tile Set from Tile Set File"), EditorIcon::New, [this]() { ShowImportTileSetFileDialog(*this); });
		a.Add(ActionIds::LevelAddTiles, _x("editor", "Add Tiles from Image"), EditorIcon::Plus, [this]() { ShowAddTilesFromImageDialog(*this); });
		// TRANSLATORS: Menu item in the Level menu, opens the dialog that generates tiles for a layer with a blur or glass effect
		a.Add(ActionIds::LevelFxTiles, _x("editor", "Generate Effect Tiles"), EditorIcon::None, [this]() { ShowFxTileGeneratorDialog(*this); });
		a.Add(ActionIds::LevelApplyTileChanges, _x("editor", "Save Tile Changes into Tile Set"), EditorIcon::Save, [this]() { ShowApplyTileChangesDialog(*this); });
		a.Add(ActionIds::LevelRearrangeTileSet, _x("editor", "Rearrange Tile Set"), EditorIcon::Tiles, [this]() { ShowTileSetLayoutDialog(*this); });
		a.Add(ActionIds::LevelReloadEvents, _x("editor", "Read Event Descriptions Again"), EditorIcon::None, [this]() {
			_files->LoadEventExtensions(true);
			ShowMessage(_x("editor", "Event packs and the script of the level were read again"));
		});
	}

	void EditorSession::CreateLayerSelectActions()
	{
		auto& a = _actions;
		a.Add(ActionIds::LayerPrevious, _x("editor", "Previous Layer"), EditorIcon::None, [this]() { SetCurrentLayer(_currentLayer - 1); }).Key = { Keys::PageUp, 0 };
		a.Add(ActionIds::LayerNext, _x("editor", "Next Layer"), EditorIcon::None, [this]() { SetCurrentLayer(_currentLayer + 1); }).Key = { Keys::PageDown, 0 };
		// A layer by its number key, as other editors choose them, Shift with the key hides it (see OnKeyPressed())
		for (std::int32_t number = 1; number <= 9; number++) {
			// TRANSLATORS: Command that makes a layer the current one by its number key, {} is the number of the layer
			auto& select = a.Add(String(StringView(ActionIds::LayerSelectPrefix) + String(std::to_string(number).c_str())), _fx("editor", "Layer {}", number), EditorIcon::None,
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
		// TRANSLATORS: Menu item in the Play menu and button in the toolbar, play-tests the level from where the player was left last time (verb)
		auto& play = a.Add(ActionIds::PlayResume, _x("editor", "Play"), EditorIcon::Play, [this]() { StartPlayTest(PlayStartMode::Resume); });
		play.Key = { Keys::F5, 0 };
		play.IsEnabled = isEditing;
		// TRANSLATORS: Menu item in the Play menu, play-tests the level with the player placed where the pointer is
		// Its own icon, only the outline of Play; Play from Start has none, three of the same next to each other would tell nothing
		auto& here = a.Add(ActionIds::PlayHere, _x("editor", "Play Here"), EditorIcon::PlayOutline, [this]() { StartPlayTest(PlayStartMode::Here); });
		here.Key = { Keys::F5, KeyMod::Shift };
		here.IsEnabled = isEditing;
		auto& start = a.Add(ActionIds::PlayStart, _x("editor", "Play from Start"), EditorIcon::None, [this]() { StartPlayTest(PlayStartMode::FromStart); });
		start.Key = { Keys::F5, KeyMod::Ctrl };
		start.IsEnabled = isEditing;
		// The key that plays goes back while the level is played, the place of the player is kept for "Play"
		auto& stop = a.Add(ActionIds::PlayStop, _("Back to Editor"), EditorIcon::EnterEditor, [this]() { StopPlayTest(true); });
		stop.Key = { Keys::F5, 0 };
		stop.Scope = ActionScope::Play;
		stop.IsEnabled = isInGame;
		struct PlayChoice { const char* Id; String Label; std::int32_t Value; bool Character; };
		static const PlayChoice Choices[] = {
			// Names of characters aren't translated
			{ ActionIds::PlayJazz, "Jazz", std::int32_t(PlayerType::Jazz), true },
			{ ActionIds::PlaySpaz, "Spaz", std::int32_t(PlayerType::Spaz), true },
			{ ActionIds::PlayLori, "Lori", std::int32_t(PlayerType::Lori), true },
			// TRANSLATORS: Choices in the Play menu of the difficulty to play-test the level with
			{ ActionIds::PlayEasy, _x("editor", "Easy"), std::int32_t(GameDifficulty::Easy), false },
			{ ActionIds::PlayNormal, _x("editor", "Normal"), std::int32_t(GameDifficulty::Normal), false },
			{ ActionIds::PlayHard, _x("editor", "Hard"), std::int32_t(GameDifficulty::Hard), false },
		};
		for (const PlayChoice& choice : Choices) {
			std::int32_t value = choice.Value;
			bool character = choice.Character;
			auto& action = a.Add(choice.Id, choice.Label, EditorIcon::None, [this, value, character]() {
				(character ? _settings.PlayCharacter : _settings.PlayDifficulty) = value;
				_settings.Save();
			});
			action.IsChecked = [this, value, character]() {
				return ((character ? _settings.PlayCharacter : _settings.PlayDifficulty) == value);
			};
		}

		// For this session only, a play-test kept until then starts again with it
		// TRANSLATORS: Toggle in the Play menu, the level is play-tested with the gameplay of Resurrection instead of the original
		auto& reforged = a.Add(ActionIds::PlayReforged, _x("editor", "Reforged Gameplay"), EditorIcon::None, [this]() {
			_playReforged = !_playReforged;
			_playTest->MarkPlayTestOutdated();
		});
		reforged.IsChecked = [this]() { return _playReforged; };
		// TRANSLATORS: Toggle in the Play menu, players can climb up ledges in the play-test
		auto& ledgeClimb = a.Add(ActionIds::PlayLedgeClimb, _x("editor", "Allow Ledge Climbing"), EditorIcon::None, [this]() {
			_playLedgeClimb = !_playLedgeClimb;
			_playTest->MarkPlayTestOutdated();
		});
		ledgeClimb.IsChecked = [this]() { return _playLedgeClimb; };

#if defined(WITH_MULTIPLAYER)
		// Multiplayer in local splitscreen, the rules of a game mode with up to 4 players on one screen
		using Multiplayer::MpGameMode;
		struct ModeChoice { const char* Id; String Label; MpGameMode Mode; };
		// Names of the game modes are shared with the menus of the game, they're without the context
		static const ModeChoice Modes[] = {
			// TRANSLATORS: Choice in the Play menu next to the multiplayer game modes, play-tests the level as a single player
			{ ActionIds::PlayAlone, _x("editor", "Alone"), MpGameMode::Unknown },
			{ ActionIds::PlayCooperation, _("Cooperation"), MpGameMode::Cooperation },
			{ ActionIds::PlayBattle, _("Battle"), MpGameMode::Battle },
			{ ActionIds::PlayTeamBattle, _("Team Battle"), MpGameMode::TeamBattle },
			{ ActionIds::PlayRace, _("Race"), MpGameMode::Race },
			{ ActionIds::PlayTreasureHunt, _("Treasure Hunt"), MpGameMode::TreasureHunt },
			{ ActionIds::PlayCaptureTheFlag, _("Capture The Flag"), MpGameMode::CaptureTheFlag },
		};
		for (const ModeChoice& choice : Modes) {
			std::int32_t value = std::int32_t(choice.Mode);
			auto& action = a.Add(choice.Id, choice.Label, EditorIcon::None, [this, value]() {
				_settings.PlayGameMode = value;
				_settings.Save();
			});
			action.IsChecked = [this, value]() { return (_settings.PlayGameMode == value); };
		}
		for (std::int32_t count = 1; count <= LevelInitialization::MaxPlayerCount; count++) {
			// TRANSLATORS: Choice in the Play menu of how many players play-test a multiplayer game mode on one screen, {} is the number
			auto& action = a.Add(String(StringView(ActionIds::PlayPlayersPrefix) + String(std::to_string(count).c_str())), _fnx("editor", "{} Player", "{} Players", count, count), EditorIcon::None, [this, count]() {
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
		// TRANSLATORS: Toggle in the Show and Play menus, shows the way the player went in the play-tests
		auto& path = a.Add(ActionIds::ViewPlayPath, _x("editor", "Show Play Path"), EditorIcon::Path, [this]() { _showPlayPath = !_showPlayPath; });
		path.Key = { Keys::L, KeyMod::Ctrl };
		path.IsChecked = [this]() { return _showPlayPath; };

		// Debug toggles of play-tests, kept for all of them, the menus toggle them anytime
		struct DebugToggle { const char* Id; String Label; bool EditorSettings::PlayTestSettings::*Option; String On; String Off; };
		static const DebugToggle DebugToggles[] = {
			// TRANSLATORS: Debug toggles in the bar of a running play-test, then the status messages after turning them on and off
			{ ActionIds::PlayHitboxes, _x("editor", "Show Hitboxes"), &EditorSettings::PlayTestSettings::ShowHitboxes, _x("editor", "Hitboxes are shown"), _x("editor", "Hitboxes are hidden") },
			{ ActionIds::PlayEventZones, _x("editor", "Show Event Zones"), &EditorSettings::PlayTestSettings::ShowEventZones, _x("editor", "Event zones are shown"), _x("editor", "Event zones are hidden") },
		};
		for (const DebugToggle& toggle : DebugToggles) {
			bool EditorSettings::PlayTestSettings::*option = toggle.Option;
			StringView on = toggle.On;
			StringView off = toggle.Off;
			auto& action = a.Add(toggle.Id, toggle.Label, EditorIcon::None, [this, option, on, off]() {
				TogglePlayTestDebug(option, on, off);
			});
			action.Scope = ActionScope::Play;
			action.IsChecked = [this, option]() { return _settings.PlayTest.*option; };
		}
		auto& slowMotion = a.Add(ActionIds::PlaySlowMotion, _x("editor", "Slow Motion"), EditorIcon::None, [this]() { CyclePlayTestTimeScale(); });
		slowMotion.Key = { Keys::F9, 0 };
		slowMotion.Scope = ActionScope::Play;
		slowMotion.IsChecked = [this]() { return (_settings.PlayTest.TimeScale < 1.0f); };
		auto& pause = a.Add(ActionIds::PlayPause, _x("editor", "Stop Time of Level"), EditorIcon::Pause, [this]() { TogglePlayTestPause(); });
		pause.Key = { Keys::F7, 0 };
		pause.Scope = ActionScope::Play;
		pause.IsEnabled = isInGame;
		pause.IsChecked = [this]() { return _playTest->GetDebugOptions().Paused; };
		// TRANSLATORS: Command of a running play-test, moves the stopped game on by one frame
		auto& step = a.Add(ActionIds::PlayStep, _x("editor", "Next Frame"), EditorIcon::None, [this]() { _playTest->StepFrame(); });
		step.Key = { Keys::F8, 0 };
		step.Scope = ActionScope::Play;
		step.IsEnabled = isInGame;
		// The pointer is over the game while it's played, so it's done by the key
		auto& teleport = a.Add(ActionIds::PlayTeleport, _x("editor", "Move Player to Pointer"), EditorIcon::None, [this]() { TeleportPlayTestPlayer(); });
		teleport.Key = { Keys::F6, 0 };
		teleport.Scope = ActionScope::Play;
		teleport.IsEnabled = isInGame;

		// What is shown of the paths of the play-tests kept with the level
		auto& deaths = a.Add(ActionIds::ViewDeaths, _x("editor", "Show Deaths in Play-Tests"), EditorIcon::None, [this]() {
			_settings.PlayTest.ShowDeaths = !_settings.PlayTest.ShowDeaths;
			_settings.Save();
		});
		deaths.IsChecked = [this]() { return _settings.PlayTest.ShowDeaths; };
		auto& clearPaths = a.Add(ActionIds::PlayClearPaths, _x("editor", "Forget Paths of Play-Tests"), EditorIcon::None, [this]() {
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
		auto& reach = a.Add(ActionIds::ViewReachability, _x("editor", "Show Where the Player Can Get"), EditorIcon::Reach, [this]() {
			_playOverlays->ToggleReachability();
		});
		reach.Key = { Keys::R, KeyMod::Ctrl | KeyMod::Shift };
		reach.IsChecked = [this]() { return _playOverlays->IsReachabilityShown(); };
		// Who gets there, the moves of each character differ, and from where
		struct ReachChoice { const char* Id; String Label; std::int32_t Character; };
		static const ReachChoice ReachChoices[] = {
			// TRANSLATORS: Choices in the Reachable Area submenu of the View menu of whose reach the shown reachable area is
			{ ActionIds::ViewReachAny, _x("editor", "Reachable with Any Character"), 0 },
			{ ActionIds::ViewReachJazz, _x("editor", "Reachable with Jazz"), std::int32_t(PlayerType::Jazz) },
			{ ActionIds::ViewReachSpaz, _x("editor", "Reachable with Spaz"), std::int32_t(PlayerType::Spaz) },
			{ ActionIds::ViewReachLori, _x("editor", "Reachable with Lori"), std::int32_t(PlayerType::Lori) },
		};
		for (const ReachChoice& choice : ReachChoices) {
			std::int32_t character = choice.Character;
			auto& action = a.Add(choice.Id, choice.Label, EditorIcon::None, [this, character]() {
				_settings.PlayTest.ReachCharacter = character;
				_settings.Save();
				if (_playOverlays->IsReachabilityShown()) {
					_playOverlays->RecomputeReachability(false);
				}
			});
			action.IsChecked = [this, character]() { return (_settings.PlayTest.ReachCharacter == character); };
		}
		auto& reachMp = a.Add(ActionIds::ViewReachFromMpStarts, _x("editor", "Reachable from Multiplayer Starts Too"), EditorIcon::None, [this]() {
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
		a.Add(ActionIds::LevelEditScript, _x("editor", "Edit Script"), EditorIcon::None, [this]() { _files->EditScript(); });
		// Where the level differs from the file on disk
		// TRANSLATORS: Toggle in the View menu, shows where the level differs from its saved file
		auto& compare = a.Add(ActionIds::ViewCompareSaved, _x("editor", "Compare with Saved"), EditorIcon::None, [this]() {
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
