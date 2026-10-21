#include "EditorShell.h"

#if defined(WITH_EDITOR)

#include "EventPanels.h"
#include "LayerResizeHandles.h"
#include "ShellPanels.h"
#include "ToolOptionsBar.h"
#include "../EditorSession.h"
#include "../Document/Annotations.h"
#include "../Document/DocumentChanges.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../Runtime/MaskOverlay.h"
#include "../Runtime/PlayTransition.h"
#include "../Session/PlayOverlays.h"
#include "../Tools/TileTools.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../Rendering/PolylineRenderer.h"
#include "../../ILevelHandler.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Sizes of the docked panels in units, and at most which part of the screen they take
		constexpr float ToolStripWidth = 24.0f;
		constexpr float RightDockMinWidth = 100.0f;
		constexpr float RightDockMaxFraction = 0.6f;
		constexpr float PickerDockMinWidth = 80.0f;
		constexpr float PickerDockMaxFraction = 0.45f;
		// The list of layers and the properties below it both keep at least this much
		constexpr float LayerListMinHeight = 36.0f;
		constexpr float InspectorMinHeight = 48.0f;
		// Width of the edge of a panel that resizes it, in units
		constexpr float SplitterGrip = 5.0f;
		// Narrower windows than this in units get the panels in drawers, and the tools at the bottom
		constexpr float CompactWidth = 600.0f;
		constexpr float NarrowWidth = 420.0f;
		// Most of the screen a drawer takes
		constexpr float DrawerMaxFraction = 0.85f;
		// What's shown over the level is drawn in slot 0 and what the tool shows in slot 1, the bubbles with texts go over
		// both, so no mark, sprite or link of an event covers them, still under the panels (see UiSurface::RootSlots)
		constexpr std::int32_t BubbleSlot = 2;

		// Tools and commands only experts are offered, see EditorSettings::Expert
		const char* const ExpertActions[] = {
			ActionIds::ToolRectangle, ActionIds::ToolEyedropper, ActionIds::ToolWand, ActionIds::ToolPath, ActionIds::ToolMeasure, ActionIds::ToolGround,
			ActionIds::EditHistory, ActionIds::EditFindReplace, ActionIds::EditSmartRetile, ActionIds::EditBeautify, ActionIds::EditTerrainRules, ActionIds::EditTraceGround, ActionIds::EditBakeGround,
			ActionIds::EditContinueRow, ActionIds::EventsSelectSameType, ActionIds::EventsCopyToMatchingTiles, ActionIds::StampRandom, ActionIds::StampReroll, ActionIds::EditPrefabs,
			ActionIds::ViewFlat, ActionIds::ViewPauseLayers, ActionIds::ViewReachability, ActionIds::ViewAddNote,
			ActionIds::LayerResize, ActionIds::LevelSnapOffGrid, ActionIds::LevelReloadEvents, ActionIds::LevelEpisode, ActionIds::LevelAnimations, ActionIds::LevelTileSets, ActionIds::LevelPalette, ActionIds::LevelEditTile,
			ActionIds::LevelApplyTileChanges, ActionIds::LevelRearrangeTileSet, ActionIds::LevelNewTileSet, ActionIds::LevelImportTileSet, ActionIds::LevelAddTiles, ActionIds::LevelFxTiles,
			ActionIds::LevelExportTileSet,
			ActionIds::ToolShape, ActionIds::ShapeLine, ActionIds::ShapeRectangle, ActionIds::ShapeEllipse, ActionIds::ShapeFilledEllipse, ActionIds::StampRotate, ActionIds::StampRotateCCW, ActionIds::StampRandomFlipX,
			ActionIds::StampRandomFlipY, ActionIds::StampWeights, ActionIds::ToolsModifierKeep, ActionIds::ToolsModifierNormal, ActionIds::ToolsModifierTranslucent, ActionIds::ToolsModifierInvisible,
			ActionIds::ToolsModifierHeat, ActionIds::ToolsModifierOnly, ActionIds::ToolsAcrossSeams, ActionIds::ToolsIgnoreFlips, ActionIds::ToolsOnlyEmpty, ActionIds::ToolsWandGlobal, ActionIds::EventsModeRectangle,
			ActionIds::EventsModeFill, ActionIds::MeasureWalk, ActionIds::MeasureDash, ActionIds::MeasureDoubleJump, ActionIds::MeasureCopter, ActionIds::MeasureCollisions,
			ActionIds::SelectGrow, ActionIds::SelectShrink, ActionIds::SelectSameTiles, ActionIds::EditMirrorSwapTeams, ActionIds::EditCopyLayers, ActionIds::EditPasteLayers, ActionIds::EditModifierNormal,
			ActionIds::EditModifierTranslucent, ActionIds::EditModifierInvisible, ActionIds::EditModifierHeat, ActionIds::EventsFillSelection
		};

		// Button whose tooltip is made when it's shown, the shortcuts in it may have been changed meanwhile
		class LiveTooltipButton : public UiButton
		{
		public:
			using UiButton::UiButton;

			mutable Function<String()> TooltipFunc;

			StringView GetTooltip(Vector2f pos) const override
			{
				if (!TooltipFunc) {
					return UiButton::GetTooltip(pos);
				}
				_liveTooltip = TooltipFunc();
				return _liveTooltip;
			}

		private:
			mutable String _liveTooltip;
		};

		// The grid fades out when its cells get smaller than this, in pixels
		constexpr float GridMinCellSize = 6.0f;
		constexpr float GridFadeRange = 14.0f;
		// Ramer-Douglas-Peucker, as PolylineRenderer::SimplifyRdp(), only it tells which points are kept
		void SimplifyRdpIndices(ArrayView<const Vector2f> points, float tolerance, SmallVectorImpl<std::int32_t>& kept)
		{
			std::int32_t count = std::int32_t(points.size());
			if (count <= 2) {
				for (std::int32_t i = 0; i < count; i++) {
					kept.push_back(i);
				}
				return;
			}
			SmallVector<bool, 0> marked;
			marked.resize(std::size_t(count), false);
			marked[0] = marked[count - 1] = true;
			SmallVector<std::pair<std::int32_t, std::int32_t>, 32> ranges;
			ranges.push_back({ 0, count - 1 });
			while (!ranges.empty()) {
				auto [first, last] = ranges.back();
				ranges.pop_back();
				Vector2f a = points[first], b = points[last];
				Vector2f direction = b - a;
				float length = direction.Length();
				float farthest = 0.0f;
				std::int32_t farthestIndex = -1;
				for (std::int32_t i = first + 1; i < last; i++) {
					Vector2f d = points[i] - a;
					float distance = (length > 0.0001f ? std::abs(d.X * direction.Y - d.Y * direction.X) / length : d.Length());
					if (distance > farthest) {
						farthest = distance;
						farthestIndex = i;
					}
				}
				if (farthestIndex >= 0 && farthest > tolerance) {
					marked[farthestIndex] = true;
					ranges.push_back({ first, farthestIndex });
					ranges.push_back({ farthestIndex, last });
				}
			}
			for (std::int32_t i = 0; i < count; i++) {
				if (marked[i]) {
					kept.push_back(i);
				}
			}
		}

		// Takes events of the same identifier on tiles next to each other together, as the tiles of a warp. The items
		// are sorted by the tile, each gets the index of its group, the groups get the tiles they cover.
		template<class TItem, class TGroup>
		void GroupNeighbors(SmallVectorImpl<TItem>& items, std::int32_t gridWidth, SmallVectorImpl<TGroup>& groups)
		{
			std::int32_t count = std::int32_t(items.size());
			if (count == 0 || gridWidth <= 0) {
				return;
			}
			SmallVector<std::int32_t, 0> parents;
			parents.resize_for_overwrite(std::size_t(count));
			for (std::int32_t i = 0; i < count; i++) {
				parents[i] = i;
			}
			auto findRoot = [&parents](std::int32_t i) {
				while (parents[i] != i) {
					parents[i] = parents[parents[i]];
					i = parents[i];
				}
				return i;
			};
			auto indexOf = [&items](std::int64_t cell) -> std::int32_t {
				auto it = std::lower_bound(items.begin(), items.end(), cell, [](const TItem& item, std::int64_t c) { return std::int64_t(item.Cell) < c; });
				return (it != items.end() && std::int64_t(it->Cell) == cell ? std::int32_t(it - items.begin()) : -1);
			};
			// Only the neighbors after a tile, those before it looked at it already
			static const std::int32_t Offsets[4][2] = { { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
			for (std::int32_t i = 0; i < count; i++) {
				std::int32_t x = std::int32_t(items[i].Cell % std::uint32_t(gridWidth));
				for (const auto& offset : Offsets) {
					if (x + offset[0] < 0 || x + offset[0] >= gridWidth) {
						continue;
					}
					std::int32_t j = indexOf(std::int64_t(items[i].Cell) + std::int64_t(offset[1]) * gridWidth + offset[0]);
					if (j >= 0 && items[j].Id == items[i].Id) {
						std::int32_t a = findRoot(i), b = findRoot(j);
						if (a != b) {
							parents[b] = a;
						}
					}
				}
			}
			SmallVector<std::int32_t, 0> groupOfRoot;
			groupOfRoot.resize(std::size_t(count), -1);
			for (std::int32_t i = 0; i < count; i++) {
				std::int32_t root = findRoot(i);
				std::int32_t x = std::int32_t(items[i].Cell % std::uint32_t(gridWidth)), y = std::int32_t(items[i].Cell / std::uint32_t(gridWidth));
				if (groupOfRoot[root] < 0) {
					groupOfRoot[root] = std::int32_t(groups.size());
					TGroup& group = groups.emplace_back();
					group.Cells = Recti(x, y, 1, 1);
					group.Id = items[i].Id;
				} else {
					Recti& cells = groups[groupOfRoot[root]].Cells;
					std::int32_t right = std::max(cells.X + cells.W, x + 1), bottom = std::max(cells.Y + cells.H, y + 1);
					cells.X = std::min(cells.X, x);
					cells.Y = std::min(cells.Y, y);
					cells.W = right - cells.X;
					cells.H = bottom - cells.Y;
				}
				items[i].Group = groupOfRoot[root];
			}
		}
	}

	EditorShell::EditorShell(EditorSession& session)
		: _session(session), _menuBar(nullptr), _toolbar(nullptr), _toolStrip(nullptr), _rightFrame(nullptr), _rightDock(nullptr), _layersPanel(nullptr),
			_pickerFrame(nullptr), _pickerDock(nullptr), _statusBar(nullptr), _playBar(nullptr), _rightSplitter(nullptr), _layersSplitter(nullptr),
			_pickerSplitter(nullptr), _rightDockWidth(0.0f), _layerListHeight(0.0f), _pickerDockWidth(0.0f), _dragStartSize(0.0f), _cachedPathPoints(-1),
			_cachedPathSegments(-1)
	{
		CreateMenuBar();
		CreateToolbar();
		CreateToolStrip();
		// Before the panels, which are drawn over the handles and get the pointer first
		_resizeHandles = Add<LayerResizeHandles>(session, [this]() { return _viewRect; }, [this](const LayerView& view) { return GetLayerFrame(view); });
		{
			EditorSession* sessionPtr = &session;
			_resizeHandles->IsVisibleFunc = [sessionPtr]() { return sessionPtr->ArePanelsShown(); };
		}

		_changeObserver = std::make_shared<Function<void(const DocumentChanges&)>>([this](const DocumentChanges& changes) {
			OnDocumentChanged(changes);
		});
		session.AddChangeObserver(_changeObserver);

		_rightFrame = Add<UiDockFrame>(UiDockSide::Right, _x("editor", "Drag to move the panel, to the edge of the screen to dock it again"));
		_rightDock = _rightFrame->GetPanel();
		// TRANSLATORS: Header of the panel with the list of layers of the level and their properties
		_rightFrame->GetHeaderContent()->Add<UiLabel>(_x("editor", "Layers"), UiTheme::TextHeader)->Flex = 1.0f;
		_layersPanel = _rightDock->Add<LayersPanel>(session);
		EditorSession* s = &session;
		auto* inspector = _rightDock->Add<InspectorPanel>(session);
		inspector->Flex = 1.0f;
		inspector->IsVisibleFunc = [s]() { return !s->IsEditingEvents(); };
		auto* eventInspector = _rightDock->Add<EventInspectorPanel>(session);
		eventInspector->Flex = 1.0f;
		eventInspector->IsVisibleFunc = [s]() { return s->IsEditingEvents(); };

		// Tiles and events to pick from, next to the tools, where there's more room than below the view
		_pickerFrame = Add<UiDockFrame>(UiDockSide::Left, _x("editor", "Drag to move the panel, to the edge of the screen to dock it again"));
		_pickerDock = _pickerFrame->GetPanel();
		_pickerDock->ClipContent = true;
		AddPickerPanelTabs(_pickerFrame->GetHeaderContent(), session);
		auto* tilePicker = _pickerDock->Add<TilePickerPanel>(session);
		tilePicker->Flex = 1.0f;
		tilePicker->IsVisibleFunc = [s]() { return (s->GetPickerPanel() == PickerPanel::Tiles); };
		auto* eventPicker = _pickerDock->Add<EventPickerPanel>(session);
		eventPicker->Flex = 1.0f;
		eventPicker->IsVisibleFunc = [s]() { return (s->GetPickerPanel() == PickerPanel::Events); };
		_statusBar = Add<StatusBar>(session);

		// The panels make way for the game while the level is played, only a bar to get back stays
		auto isEditing = [s]() { return s->ArePanelsShown(); };
		for (UiElement* element : { static_cast<UiElement*>(_menuBar), static_cast<UiElement*>(_toolbar), static_cast<UiElement*>(_toolStrip),
				static_cast<UiElement*>(_rightFrame), static_cast<UiElement*>(_pickerFrame), _statusBar }) {
			element->IsVisibleFunc = isEditing;
		}

		// Whether the panels float and where is remembered, the one used last comes in front of the other
		EditorSettings* settings = &session.GetSettings();
		for (auto [frame, placement] : { std::make_pair(_pickerFrame, &settings->PickerPanelPlacement), std::make_pair(_rightFrame, &settings->RightPanelPlacement) }) {
			frame->FloatingBounds = Rectf(placement->X, placement->Y, placement->Width, placement->Height);
			frame->SetFloating(placement->Floating, false);
			frame->OnChanged = [frame = frame, placement = placement, settings]() {
				placement->Floating = frame->IsFloating();
				placement->X = frame->FloatingBounds.X;
				placement->Y = frame->FloatingBounds.Y;
				placement->Width = frame->FloatingBounds.W;
				placement->Height = frame->FloatingBounds.H;
				settings->Save();
			};
			frame->OnActivated = [this, frame = frame]() {
				auto it = std::find(_floatOrder.begin(), _floatOrder.end(), frame);
				if (it != _floatOrder.end() && it + 1 != _floatOrder.end()) {
					_floatOrder.erase(it);
					_floatOrder.push_back(frame);
				}
			};
			_floatOrder.push_back(frame);
		}

		// The panels are resized by their inner edges, the size is remembered
		_rightSplitter = Add<UiSplitter>(UiOrientation::Vertical, [this]() {
			_dragStartSize = _rightDockWidth;
		}, [this, settings](float offset) {
			float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
			settings->RightPanelWidth = std::max(RightDockMinWidth, (_dragStartSize - offset) / scale);
		}, [settings]() {
			settings->Save();
		});
		// A floating panel is resized by its own edges
		_rightSplitter->IsVisibleFunc = [s, this]() { return (s->ArePanelsShown() && !_rightFrame->IsLifted() && !_compact); };
		// The edge between the layers and the properties goes with the panel, wherever it is
		_layersSplitter = _rightFrame->Add<UiSplitter>(UiOrientation::Horizontal, [this]() {
			_dragStartSize = _layerListHeight;
		}, [this, settings](float offset) {
			float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
			settings->LayerListHeight = std::max(LayerListMinHeight, (_dragStartSize + offset) / scale);
		}, [settings]() {
			settings->Save();
		});
		_layersSplitter->IsVisibleFunc = isEditing;
		// It's the separator between them too, so the line seen is the one that is dragged
		_layersSplitter->ShowLine = true;
		_pickerSplitter = Add<UiSplitter>(UiOrientation::Vertical, [this]() {
			_dragStartSize = _pickerDockWidth;
		}, [this, settings](float offset) {
			float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
			settings->PickerPanelWidth = std::max(PickerDockMinWidth, (_dragStartSize + offset) / scale);
		}, [settings]() {
			settings->Save();
		});
		_pickerSplitter->IsVisibleFunc = [s, this]() { return (s->ArePanelsShown() && !_pickerFrame->IsLifted() && !_compact); };

		_playBar = Add<UiPanel>(UiPanelStyle::Floating, UiOrientation::Horizontal, UiTheme::Spacing, UiThickness(3.0f));
		_playBar->IsVisibleFunc = [s]() { return !s->ArePanelsShown(); };
		auto* back = _playBar->Add<LiveTooltipButton>(_("Back to Editor"), EditorIcon::EnterEditor, [s]() { s->StopPlayTest(true); });
		back->Style = UiButtonStyle::Accent;
		// The icons over the game are in its style, in colors of their own, white shows them as they are
		back->IconColor = Colorf::White;
		back->TooltipFunc = [s]() {
			EditorAction* stop = s->GetActions().Find(ActionIds::PlayStop);
			return (stop != nullptr ? stop->GetTooltip() : String(_("Back to Editor")));
		};
		// TRANSLATORS: Button in the bar shown while the level is play-tested, plays the level again from its start
		auto* restart = _playBar->Add<UiButton>(_x("editor", "Restart"), EditorIcon::Play, [s]() {
			s->StopPlayTest(false);
			s->StartPlayTest(PlayStartMode::FromStart);
		});
		restart->Style = UiButtonStyle::Flat;
		restart->IconColor = UiTheme::Play;
		// The debug toggles of the play-test, their keys work while the level is played
		// TRANSLATORS: Button in the bar shown while the level is play-tested, opens a menu of debug tools such as pause and slow motion
		LiveTooltipButton* debug = _playBar->Add<LiveTooltipButton>(_x("editor", "Debug"), EditorIcon::Debug);
		debug->Style = UiButtonStyle::Flat;
		debug->IconColor = Colorf::White;
		debug->TooltipFunc = [s]() {
			// The shortcuts as they are now, those without one are left out
			String text;
			for (const char* id : { ActionIds::PlayTeleport, ActionIds::PlayPause, ActionIds::PlayStep, ActionIds::PlaySlowMotion }) {
				EditorAction* action = s->GetActions().Find(StringView(id));
				String key = (action != nullptr ? action->Key.ToString() : String());
				if (!key.empty()) {
					text = (text.empty() ? String(action->Label + " - "_s + key) : String(text + "\n"_s + action->Label + " - "_s + key));
				}
			}
			return (text.empty() ? String(_x("editor", "Debug tools of the play-test")) : text);
		};
		debug->OnClick = [s, debug]() {
			static const char* const DebugActions[] = { ActionIds::PlayPause, ActionIds::PlayStep, ActionIds::PlaySlowMotion, ActionIds::PlayTeleport, nullptr,
				ActionIds::PlayHitboxes, ActionIds::PlayEventZones };
			SmallVector<UiMenuEntry, 0> entries;
			for (const char* id : DebugActions) {
				if (id == nullptr) {
					entries.push_back(UiMenuEntry::Separator());
				} else if (EditorAction* action = s->GetActions().Find(id)) {
					entries.push_back(MakeMenuEntry(*action));
				}
			}
			if (UiSurface* surface = s->GetSurface()) {
				UiPopupMenu::Show(*surface, Death::move(entries), Vector2f(debug->Bounds.X, debug->Bounds.Y + debug->Bounds.H));
			}
		};
	}

	EditorShell::~EditorShell()
	{
	}

	bool EditorShell::IsExpertAction(StringView actionId)
	{
		for (const char* id : ExpertActions) {
			if (actionId == id) {
				return true;
			}
		}
		return false;
	}

	UiElement* EditorShell::FindDialog(StringView id) const
	{
		for (const ShownDialog& dialog : _shownDialogs) {
			if (dialog.Id == id) {
				return dialog.Element;
			}
		}
		return nullptr;
	}

	void EditorShell::TrackDialog(StringView id, UiElement* dialog, Function<void()>&& onShownAgain)
	{
		ShownDialog& shown = _shownDialogs.emplace_back();
		shown.Id = id;
		shown.Element = dialog;
		shown.OnShownAgain = Death::move(onShownAgain);
	}

	void EditorShell::ForgetDialog(UiElement* dialog)
	{
		for (std::size_t i = 0; i < _shownDialogs.size(); i++) {
			if (_shownDialogs[i].Element == dialog) {
				_shownDialogs.erase(_shownDialogs.begin() + i);
				return;
			}
		}
	}

	void EditorShell::ShowDialogAgain(UiElement* dialog)
	{
		// Taken out while it runs, it may close the dialog, which forgets it
		Function<void()> onShownAgain;
		for (ShownDialog& shown : _shownDialogs) {
			if (shown.Element == dialog) {
				onShownAgain = Death::move(shown.OnShownAgain);
				break;
			}
		}
		if (!onShownAgain) {
			return;
		}
		onShownAgain();
		for (ShownDialog& shown : _shownDialogs) {
			if (shown.Element == dialog) {
				shown.OnShownAgain = Death::move(onShownAgain);
				break;
			}
		}
	}

	void EditorShell::OnDocumentChanged(const DocumentChanges& changes)
	{
		if (changes.HasAny(DocumentChangeKind::Events | DocumentChangeKind::LayerStructure)) {
			_linksDirty = true;
			_eventsSerial++;
		}
	}

	void EditorShell::ToggleDrawer(bool picker)
	{
		UiDockFrame* frame = (picker ? _pickerFrame : _rightFrame);
		UiDockFrame* other = (picker ? _rightFrame : _pickerFrame);
		if (!frame->IsDrawer()) {
			return;
		}
		bool open = !frame->IsDrawerOpen();
		frame->SetDrawerOpen(open);
		// On a small screen, one drawer at a time
		if (open) {
			other->SetDrawerOpen(false);
		}
	}

	bool EditorShell::IsDrawerOpen(bool picker) const
	{
		const UiDockFrame* frame = (picker ? _pickerFrame : _rightFrame);
		return (frame->IsDrawer() && frame->IsDrawerOpen());
	}

	void EditorShell::CreateMenuBar()
	{
		_menuBar = Add<UiMenuBar>();
		ActionRegistry& actions = _session.GetActions();

		EditorSettings* settings = &_session.GetSettings();
		auto addActions = [&actions, settings](SmallVectorImpl<UiMenuEntry>& entries, std::initializer_list<const char*> ids) {
			for (const char* id : ids) {
				if (id == nullptr) {
					// No separator first, or twice in a row, as some of the items between may be left out
					if (!entries.empty() && !entries.back().IsSeparator()) {
						entries.push_back(UiMenuEntry::Separator());
					}
				} else if (!settings->Expert && IsExpertAction(id)) {
					continue;
				} else if (EditorAction* action = actions.Find(id)) {
					entries.push_back(MakeMenuEntry(*action));
				}
			}
			if (!entries.empty() && entries.back().IsSeparator()) {
				entries.pop_back();
			}
		};

		EditorSession* session = &_session;
		// TRANSLATORS: Menu in the menu bar of the editor
		_menuBar->AddMenu(_x("editor", "File"), [addActions, session](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { ActionIds::FileNew, ActionIds::FileOpen });
			// Recent files, except the one open now
			const auto& recentFiles = session->GetSettings().RecentFiles;
			std::int32_t shown = 0;
			for (const String& path : recentFiles) {
				if (path == session->GetFilePath() || !fs::FileExists(path) || shown >= 6) {
					continue;
				}
				if (shown == 0) {
					entries.push_back(UiMenuEntry::Separator());
				}
				UiMenuEntry entry;
				entry.Label = fs::GetFileName(fs::GetDirectoryName(path)) + "/"_s + fs::GetFileName(path);
				entry.Icon = EditorIcon::File;
				String pathCopy = path;
				entry.Execute = [session, pathCopy]() { session->OpenFile(pathCopy); };
				entries.push_back(Death::move(entry));
				shown++;
			}
			addActions(entries, { nullptr, ActionIds::FileSave, ActionIds::FileSaveAs, ActionIds::FileExport, ActionIds::FileExportPackage, nullptr, ActionIds::FileRecovery, nullptr, ActionIds::FileExit });
		});
		// TRANSLATORS: Menu in the menu bar of the editor
		_menuBar->AddMenu(_x("editor", "Edit"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { ActionIds::EditUndo, ActionIds::EditRedo, ActionIds::EditHistory, nullptr, ActionIds::EditCut, ActionIds::EditCopy, ActionIds::EditPaste, ActionIds::EditDelete, nullptr,
				ActionIds::EditSelectAll, ActionIds::EditDeselect, ActionIds::EditFindReplace, nullptr, ActionIds::EditFillGround, ActionIds::EditSmartRetile, ActionIds::EditBeautify, ActionIds::EditContinueRow, ActionIds::EditTerrainRules, ActionIds::EditTraceGround, ActionIds::EditBakeGround, nullptr, ActionIds::EventsSelectSameType, ActionIds::EventsCopyToMatchingTiles, nullptr, ActionIds::EditPrefabs });
		});
		// The camera and the interface, what's drawn over the level is in "Show"
		// TRANSLATORS: Menu in the menu bar of the editor, the camera and the look of the editor
		_menuBar->AddMenu(_x("editor", "View"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { ActionIds::ViewZoomIn, ActionIds::ViewZoomOut, ActionIds::ViewZoomGame, ActionIds::ViewFit, nullptr, ActionIds::ViewGrid, ActionIds::ViewFlat, ActionIds::ViewPauseLayers, nullptr,
				ActionIds::ViewNavigator, ActionIds::ViewAddNote, nullptr, ActionIds::ViewUiLarger, ActionIds::ViewUiSmaller, ActionIds::ViewUiAuto, nullptr, ActionIds::ViewLayoutAuto, ActionIds::ViewLayoutDesktop, ActionIds::ViewLayoutCompact,
				nullptr, ActionIds::ViewExpert, nullptr, ActionIds::ViewHighContrast, ActionIds::ViewReducedMotion });
		});
		// TRANSLATORS: Menu in the menu bar of the editor, what is drawn over the level, such as masks, events or the frame of the screen
		_menuBar->AddMenu(_x("editor", "Show"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { ActionIds::ViewMasks, ActionIds::ViewEvents, ActionIds::ViewPlayPath, nullptr, ActionIds::ViewReachability, ActionIds::ViewReachAny, ActionIds::ViewReachJazz, ActionIds::ViewReachSpaz, ActionIds::ViewReachLori,
				ActionIds::ViewReachFromMpStarts, nullptr, ActionIds::ViewCompareSaved, nullptr, ActionIds::ViewScreenFrame, ActionIds::ViewScreenFrameGame, ActionIds::ViewScreenFrame4x3,
				ActionIds::ViewScreenFrame16x9, ActionIds::ViewScreenFrameSplit });
		});
		// TRANSLATORS: Menu in the menu bar of the editor, commands for the selection
		_menuBar->AddMenu(_x("editor", "Select"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { ActionIds::EditSelectAll, ActionIds::EditDeselect, ActionIds::SelectInvert, ActionIds::SelectGrow, ActionIds::SelectShrink, ActionIds::SelectSameTiles, nullptr,
				ActionIds::SelectMoveLeft, ActionIds::SelectMoveRight, ActionIds::SelectMoveUp, ActionIds::SelectMoveDown, nullptr, ActionIds::EditMirrorH, ActionIds::EditMirrorV, ActionIds::EditMirrorSwapTeams, nullptr,
				ActionIds::EditCopyLayers, ActionIds::EditPasteLayers, nullptr, ActionIds::EditModifierNormal, ActionIds::EditModifierTranslucent, ActionIds::EditModifierInvisible,
				ActionIds::EditModifierHeat, nullptr, ActionIds::EventsFillSelection });
		});
		// TRANSLATORS: Menu in the menu bar of the editor
		_menuBar->AddMenu(_x("editor", "Tools"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			// Only the tools, the options of the chosen one are in the toolbar; in the order of the tool strip
			addActions(entries, { ActionIds::ToolSelect, ActionIds::ToolBrush, ActionIds::ToolEraser, ActionIds::ToolRectangle, ActionIds::ToolFill, ActionIds::ToolEyedropper,
				ActionIds::ToolWand, ActionIds::ToolShape, ActionIds::ToolSmart, ActionIds::ToolGround, nullptr, ActionIds::ToolEvent, ActionIds::ToolPath, nullptr,
				ActionIds::ToolHand, ActionIds::ToolMeasure });
		});
		// TRANSLATORS: Menu in the menu bar of the editor, a layer of tiles
		_menuBar->AddMenu(_x("editor", "Layer"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			// Going to the previous or the next layer is left to its shortcuts and the layer list
			addActions(entries, { ActionIds::LayerAdd, ActionIds::LayerDuplicate, ActionIds::LayerRemove, ActionIds::LayerResize, nullptr, ActionIds::LayerUp, ActionIds::LayerDown });
		});
		// TRANSLATORS: Menu in the menu bar of the editor, the level as a whole
		_menuBar->AddMenu(_x("editor", "Level"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { ActionIds::LevelProperties, ActionIds::LevelTexts, ActionIds::LevelEpisode, ActionIds::LevelStatistics, ActionIds::LevelCheck, ActionIds::EventsList, ActionIds::LevelSnapOffGrid, ActionIds::LevelReloadEvents, ActionIds::LevelEditScript, nullptr, ActionIds::LevelAnimations, ActionIds::LevelTileSets, ActionIds::LevelPalette, ActionIds::LevelEditTile, ActionIds::LevelApplyTileChanges, ActionIds::LevelRearrangeTileSet, nullptr, ActionIds::LevelNewTileSet, ActionIds::LevelImportTileSet, ActionIds::LevelAddTiles, ActionIds::LevelFxTiles, ActionIds::LevelExportTileSet });
		});
		// TRANSLATORS: Menu in the menu bar of the editor, for play-testing the level
		_menuBar->AddMenu(_x("editor", "Play"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			// The debug toggles of a running play-test are in its play bar only
			addActions(entries, { ActionIds::PlayResume, ActionIds::PlayHere, ActionIds::PlayStart, nullptr, ActionIds::PlayJazz, ActionIds::PlaySpaz, ActionIds::PlayLori, nullptr,
				ActionIds::PlayEasy, ActionIds::PlayNormal, ActionIds::PlayHard, nullptr, ActionIds::ViewPlayPath, ActionIds::ViewDeaths, ActionIds::PlayClearPaths });
#if defined(WITH_MULTIPLAYER)
			// Multiplayer in local splitscreen
			addActions(entries, { nullptr, ActionIds::PlayAlone, ActionIds::PlayCooperation, ActionIds::PlayBattle, ActionIds::PlayTeamBattle, ActionIds::PlayRace, ActionIds::PlayTreasureHunt,
				ActionIds::PlayCaptureTheFlag, nullptr, ActionIds::PlayPlayers1, ActionIds::PlayPlayers2, ActionIds::PlayPlayers3, ActionIds::PlayPlayers4 });
#endif
		});
		// TRANSLATORS: Menu in the menu bar of the editor
		_menuBar->AddMenu(_x("editor", "Help"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			// Presets of the shortcuts are chosen in the shortcuts dialog
			addActions(entries, { ActionIds::HelpCommands, ActionIds::HelpShortcuts });
		});
	}

	void EditorShell::CreateToolbar()
	{
		_toolbar = Add<UiPanel>(UiPanelStyle::Header, UiOrientation::Horizontal, 1.0f, UiThickness(UiTheme::Padding, 2.0f));
		_toolbar->ClipContent = true;
		// The panels in drawers on a small screen
		for (const char* id : { ActionIds::ViewDrawerTiles, ActionIds::ViewDrawerLayers }) {
			if (UiButton* button = AddActionButton(_toolbar, id)) {
				button->IsVisibleFunc = [this]() { return _compact; };
			}
		}
		AddActionButton(_toolbar, ActionIds::FileNew);
		AddActionButton(_toolbar, ActionIds::FileOpen);
		AddActionButton(_toolbar, ActionIds::FileSave);
		_toolbar->Add<UiSeparator>(UiOrientation::Vertical);
		AddActionButton(_toolbar, ActionIds::EditUndo);
		AddActionButton(_toolbar, ActionIds::EditRedo);
		// A narrow window leaves out what the menus have too
		auto notNarrow = [this]() { return !_narrow; };
		_toolbar->Add<UiSeparator>(UiOrientation::Vertical)->IsVisibleFunc = notNarrow;
		for (const char* id : { ActionIds::EditCut, ActionIds::EditCopy, ActionIds::EditPaste }) {
			if (UiButton* button = AddActionButton(_toolbar, id)) {
				button->IsVisibleFunc = notNarrow;
			}
		}
		_toolbar->Add<UiSeparator>(UiOrientation::Vertical)->IsVisibleFunc = notNarrow;
		EditorSettings* settings = &_session.GetSettings();
		for (const char* id : { ActionIds::ViewGrid, ActionIds::ViewFlat, ActionIds::ViewPauseLayers, ActionIds::ViewMasks, ActionIds::ViewEvents, ActionIds::ViewPlayPath, ActionIds::ViewReachability }) {
			if (UiButton* button = AddActionButton(_toolbar, id)) {
				bool expert = IsExpertAction(id);
				button->IsVisibleFunc = [this, settings, expert]() { return (!_narrow && (!expert || settings->Expert)); };
			}
		}
		_toolbar->Add<UiSeparator>(UiOrientation::Vertical);
		AddActionButton(_toolbar, ActionIds::ViewZoomOut);
		AddActionButton(_toolbar, ActionIds::ViewZoomIn);
		AddActionButton(_toolbar, ActionIds::ViewFit);
		// The options of the tool chosen take the rest, as many as fit, see ToolOptionsBar
		_toolbar->Add<ToolOptionsBar>(_session)->Flex = 1.0f;
		if (UiButton* play = AddActionButton(_toolbar, ActionIds::PlayResume, true)) {
			play->Style = UiButtonStyle::Accent;
			play->IconColor = UiTheme::Play;
		}
	}

	void EditorShell::CreateToolStrip()
	{
		_toolStrip = Add<UiPanel>(UiPanelStyle::Panel, UiOrientation::Vertical, 2.0f, UiThickness(3.0f));
		// A short window cuts off the buttons that don't fit, they'd show under the status bar otherwise
		_toolStrip->ClipContent = true;
		// Selecting first, then the tools that paint tiles, those that make them up by themselves, apart from them those of
		// events, and the ones that change nothing last (a separator where the ID is null)
		static const char* const ToolStripItems[] = { ActionIds::ToolSelect, ActionIds::ToolBrush, ActionIds::ToolEraser, ActionIds::ToolRectangle,
			ActionIds::ToolFill, ActionIds::ToolEyedropper, ActionIds::ToolWand, ActionIds::ToolShape, ActionIds::ToolSmart, ActionIds::ToolGround, nullptr,
			ActionIds::ToolEvent, ActionIds::ToolPath, nullptr, ActionIds::ToolHand, ActionIds::ToolMeasure };
		for (const char* id : ToolStripItems) {
			if (id == nullptr) {
				_toolStrip->Add<UiSeparator>();
			} else {
				AddActionButton(_toolStrip, id);
			}
		}
		// Flipping and turning the stamp is among the options of the tools that paint it, in the toolbar
		_toolStrip->Add<UiSeparator>();
		_toolStrip->Add<StampPreview>(_session);
	}

	UiButton* EditorShell::AddActionButton(UiElement* parent, StringView actionId, bool showText)
	{
		EditorAction* action = _session.GetActions().Find(actionId);
		if (action == nullptr) {
			return nullptr;
		}
		auto* button = parent->Add<LiveTooltipButton>(showText ? StringView(action->Label) : StringView(), action->Icon, [action]() {
			if (action->CanExecute()) {
				action->Execute();
			}
		});
		button->Style = UiButtonStyle::Flat;
		// With the shortcut it has when it's shown, it can be changed meanwhile
		button->TooltipFunc = [action]() {
			return action->GetTooltip();
		};
		button->IsEnabledFunc = [action]() {
			return action->CanExecute();
		};
		if (IsExpertAction(actionId)) {
			EditorSettings* settings = &_session.GetSettings();
			button->IsVisibleFunc = [settings]() { return settings->Expert; };
		}
		// A unit more around the icon than other buttons have, the bars would look crowded otherwise
		button->Height = UiTheme::ButtonHeight + 2.0f;
		if (!showText) {
			button->Width = UiTheme::ButtonHeight + 2.0f;
		}
		if (action->IsChecked) {
			button->IsCheckedFunc = [action]() {
				return action->IsChecked();
			};
		}
		return button;
	}

	void EditorShell::Arrange(const UiContext& context, const Rectf& bounds)
	{
		Bounds = bounds;

		float menuHeight = context.Px(UiTheme::MenuBarHeight);
		float toolbarHeight = context.Px(UiTheme::ToolbarHeight);
		float statusHeight = context.Px(UiTheme::StatusBarHeight);
		const EditorSettings& settings = _session.GetSettings();

		// A small screen gets the panels in drawers, a narrow one the tools at the bottom too
		float widthUnits = bounds.W / std::max(context.Scale, 0.01f);
		_compact = (settings.Layout == 2 || (settings.Layout == 0 && widthUnits < CompactWidth));
		_narrow = (_compact && widthUnits < NarrowWidth);
		_pickerFrame->SetDrawerMode(_compact);
		_rightFrame->SetDrawerMode(_compact);
		UiOrientation stripOrientation = (_narrow ? UiOrientation::Horizontal : UiOrientation::Vertical);
		if (_toolStrip->Orientation != stripOrientation) {
			_toolStrip->Orientation = stripOrientation;
			for (std::int32_t i = 0; i < _toolStrip->GetChildCount(); i++) {
				if (UiSeparator* separator = runtime_cast<UiSeparator>(_toolStrip->GetChild(i))) {
					separator->Orientation = (_narrow ? UiOrientation::Vertical : UiOrientation::Horizontal);
				}
			}
		}

		float toolStripWidth = context.Px(ToolStripWidth);
		float maxPanel = (_compact ? DrawerMaxFraction : 1.0f);
		float rightWidth = std::min(context.Px(settings.RightPanelWidth), std::round(bounds.W * (_compact ? maxPanel : RightDockMaxFraction)));
		float pickerWidth = std::min(context.Px(settings.PickerPanelWidth), std::round(bounds.W * (_compact ? maxPanel : PickerDockMaxFraction)));
		_rightDockWidth = rightWidth;
		_pickerDockWidth = pickerWidth;

		float top = bounds.Y;
		_menuBar->Arrange(context, Rectf(bounds.X, top, bounds.W, menuHeight));
		top += menuHeight;
		_toolbar->Arrange(context, Rectf(bounds.X, top, bounds.W, toolbarHeight));
		top += toolbarHeight;

		float bottom = bounds.Y + bounds.H;
		_statusBar->Arrange(context, Rectf(bounds.X, bottom - statusHeight, bounds.W, statusHeight));
		bottom -= statusHeight;

		float left = bounds.X;
		if (_narrow) {
			// A row above the status bar
			_toolStrip->Arrange(context, Rectf(bounds.X, bottom - toolStripWidth, bounds.W, toolStripWidth));
			bottom -= toolStripWidth;
		} else {
			_toolStrip->Arrange(context, Rectf(left, top, toolStripWidth, bottom - top));
			left += toolStripWidth;
		}
		// A panel that floats leaves its room to the view, it closes as the panel glides away
		Rectf pickerDockBounds(left, top, pickerWidth, bottom - top);
		_pickerFrame->Arrange(context, pickerDockBounds);
		left += std::round(pickerWidth * _pickerFrame->GetDockedAmount());
		Rectf rightDockBounds(bounds.X + bounds.W - rightWidth, top, rightWidth, bottom - top);
		float right = bounds.X + bounds.W - std::round(rightWidth * _rightFrame->GetDockedAmount());

		// The list of layers is as high as it was dragged to, the properties take the rest
		float padding = context.Px(UiTheme::Padding);
		float header = context.Px(UiTheme::ButtonHeight) + context.Px(UiTheme::Spacing);
		float dockInner = _rightFrame->GetTargetBounds(context, rightDockBounds).H - padding * 2.0f - header;
		float layerListHeight = std::clamp(context.Px(settings.LayerListHeight), context.Px(LayerListMinHeight),
			std::max(context.Px(LayerListMinHeight), dockInner - context.Px(InspectorMinHeight)));
		_layerListHeight = layerListHeight;
		_layersPanel->Height = layerListHeight / context.Scale;
		_rightFrame->Arrange(context, rightDockBounds);

		_viewRect = Rectf(left, top, right - left, bottom - top);
		_resizeHandles->Arrange(context, _viewRect);

		float grip = context.Px(SplitterGrip);
		_rightSplitter->Arrange(context, Rectf(std::round(right - grip * 0.5f), top, grip, bottom - top));
		_pickerSplitter->Arrange(context, Rectf(std::round(left - grip * 0.5f), top, grip, bottom - top));
		const Rectf& layers = _layersPanel->Bounds;
		float spacing = context.Px(UiTheme::Spacing);
		_layersSplitter->Arrange(context, Rectf(layers.X, std::round(layers.Y + layers.H + (spacing - grip) * 0.5f), layers.W, grip));

		// The bar slides in from above the screen as it fades in
		Vector2f playBarSize = _playBar->Measure(context, Vector2f(bounds.W, bounds.H));
		float playBarEase = 1.0f - (1.0f - _playBarAppear) * (1.0f - _playBarAppear);
		float playBarY = bounds.Y + context.Px(6.0f) - (1.0f - playBarEase) * (playBarSize.Y + context.Px(6.0f));
		_playBar->Arrange(context, Rectf(std::round(bounds.X + (bounds.W - playBarSize.X) * 0.5f), std::round(playBarY), playBarSize.X, playBarSize.Y));
	}

	void EditorShell::Update(float timeMult)
	{
		// The bar of the level being played comes in when the panels are gone, in about a third of a second
		constexpr float PlayBarAppearFrames = 20.0f;
		_playBarAppear = (_session.ArePanelsShown() ? 0.0f : std::min(_playBarAppear + UiTheme::GetAnimationStep(timeMult, PlayBarAppearFrames), 1.0f));
		UiElement::Update(timeMult);
	}

	void EditorShell::Draw(UiPainter& painter)
	{
		PlayTransition& transition = _session.GetPlayTransition();
		if (!_session.ArePanelsShown()) {
			std::int32_t slot = painter.GetSlot();
			painter.SetSlot(0);
			if (transition.IsActive()) {
				// The events are still seen on the last image of the editor, outside of the ring
				Vector2f screenSize(Bounds.W, Bounds.H);
				transition.Draw(painter, screenSize);
				if (_session.AreEventsShown() || _session.IsEditingEvents()) {
					Vector2f center;
					float hole, outer;
					transition.GetHole(screenSize, painter.GetScale(), center, hole, outer);
					DrawEvents(painter, Bounds, center, hole);
				}
			}
			painter.SetSlot(slot);
			// Only the bar to get back
			painter.PushOpacity(_playBarAppear);
			UiElement::Draw(painter);
			painter.PopOpacity();
			return;
		}

		// What is shown over the level goes under the panels
		std::int32_t slot = painter.GetSlot();
		painter.SetSlot(0);
		if (_session.IsPlaying() && transition.IsActive()) {
			// The game is drawn already, the last image of the editor is under all that the editor still shows, until the
			// ring with the game in it has grown over it
			transition.DrawOutside(painter, Vector2f(Bounds.W, Bounds.H));
		}
		painter.PushClip(_viewRect);
		{
			std::int32_t layer = _session.GetCurrentLayer();
			LayerView view = _session.GetLayerView(layer);
			DrawLayerBounds(painter, view);
			DrawMasks(painter, view);
			DrawReachability(painter);
			if (_session.IsGridShown()) {
				DrawGrid(painter, view);
			}
			DrawSelection(painter, view);
			if (_session.AreEventsShown() || _session.IsEditingEvents()) {
				DrawEvents(painter, _viewRect);
			}
			if (_session.IsPlayPathShown()) {
				DrawPlayPath(painter);
			}
			// The deaths in the last play-tests, see the Play menu
			_session.DrawSessionOverlays(painter, _viewRect);
			DrawAnnotations(painter);
			if (_session.GetSettings().ShowScreenFrame) {
				DrawScreenFrame(painter);
			}

			// What the tool shows goes over the labels of events
			ToolPointer hover;
			bool hoverValid = _session.GetHoverPointer(hover) && _viewRect.Contains(hover.Screen);
			painter.SetSlot(1);
			_session.GetTools().GetActive()->DrawOverlay(painter, hover, hoverValid);
		}
		painter.PopClip();
		painter.SetSlot(slot);

		// The docked panels, then where a panel being dragged would dock, then the floating panels over them, each in
		// a slot of its own (see UiSurface::RootSlots), so one doesn't show through another
		for (auto& child : _children) {
			UiElement* element = child.get();
			bool lifted = ((element == _pickerFrame || element == _rightFrame) && static_cast<UiDockFrame*>(element)->IsLifted());
			if (!lifted && element->IsShown()) {
				element->Draw(painter);
			}
		}
		painter.SetSlot(slot + 1);
		_pickerFrame->DrawDockPreview(painter);
		_rightFrame->DrawDockPreview(painter);
		std::int32_t nextSlot = slot + 2;
		for (UiDockFrame* frame : _floatOrder) {
			if (frame->IsLifted() && frame->IsShown()) {
				painter.SetSlot(nextSlot++);
				frame->Draw(painter);
			}
		}
		painter.SetSlot(slot);
	}

	UiElement* EditorShell::HitTest(Vector2f pos)
	{
		if (!IsShown() || !Bounds.Contains(pos)) {
			return nullptr;
		}
		// Floating panels first, from the top one
		for (std::size_t i = _floatOrder.size(); i > 0; i--) {
			UiDockFrame* frame = _floatOrder[i - 1];
			if (frame->IsLifted()) {
				if (UiElement* hit = frame->HitTest(pos)) {
					return hit;
				}
			}
		}
		for (std::size_t i = _children.size(); i > 0; i--) {
			UiElement* element = _children[i - 1].get();
			if ((element == _pickerFrame || element == _rightFrame) && static_cast<UiDockFrame*>(element)->IsLifted()) {
				continue;
			}
			if (UiElement* hit = element->HitTest(pos)) {
				return hit;
			}
		}
		return nullptr;
	}

	void EditorShell::DrawGrid(UiPainter& painter, const LayerView& view)
	{
		float cellSize = 32.0f * view.Scale;
		float alpha = std::clamp((cellSize - GridMinCellSize) / GridFadeRange, 0.0f, 1.0f);
		if (alpha <= 0.0f || view.Size.X <= 0) {
			return;
		}

		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X, _viewRect.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X + _viewRect.W, _viewRect.Y + _viewRect.H)));
		if (!view.RepeatX) {
			from.X = std::max(from.X, 0);
			to.X = std::min(to.X, view.Size.X - 1);
		}
		if (!view.RepeatY) {
			from.Y = std::max(from.Y, 0);
			to.Y = std::min(to.Y, view.Size.Y - 1);
		}
		if (to.X < from.X || to.Y < from.Y) {
			return;
		}

		Rectf topLeft = view.CellToScreen(from);
		Rectf bottomRight = view.CellToScreen(to);
		float top = std::max(topLeft.Y, _viewRect.Y);
		float bottom = std::min(bottomRight.Y + bottomRight.H, _viewRect.Y + _viewRect.H);
		float left = std::max(topLeft.X, _viewRect.X);
		float right = std::min(bottomRight.X + bottomRight.W, _viewRect.X + _viewRect.W);

		Colorf color = UiTheme::GridLine;
		color.A *= alpha;
		Colorf strong = UiTheme::GridLineStrong;
		strong.A *= alpha;
		for (std::int32_t x = from.X; x <= to.X + 1; x++) {
			float sx = view.CellToScreen(Vector2i(x, from.Y)).X;
			painter.FillRect(Rectf(sx, top, 1.0f, bottom - top), (x % 10) == 0 ? strong : color, UiLayer::Fill);
		}
		for (std::int32_t y = from.Y; y <= to.Y + 1; y++) {
			float sy = view.CellToScreen(Vector2i(from.X, y)).Y;
			painter.FillRect(Rectf(left, sy, right - left, 1.0f), (y % 10) == 0 ? strong : color, UiLayer::Fill);
		}
	}

	void EditorShell::DrawSelection(UiPainter& painter, const LayerView& view)
	{
		TileSelection& selection = _session.GetSelection();
		if (selection.IsEmpty() || selection.GetLayer() != _session.GetCurrentLayer()) {
			return;
		}

		// The selection is of the cells of the layer itself, a layer that repeats shows it in every copy of it the same way -
		// the tiles of a copy are its tiles - so on an axis it repeats in, the cells seen are wrapped into the layer
		const bool repeatX = (view.RepeatX && view.Size.X > 0);
		const bool repeatY = (view.RepeatY && view.Size.Y > 0);
		const Recti& bounds = selection.GetBounds();
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X, _viewRect.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X + _viewRect.W, _viewRect.Y + _viewRect.H)));
		if (!repeatX) {
			from.X = std::max(from.X, bounds.X);
			to.X = std::min(to.X, bounds.X + bounds.W - 1);
		}
		if (!repeatY) {
			from.Y = std::max(from.Y, bounds.Y);
			to.Y = std::min(to.Y, bounds.Y + bounds.H - 1);
		}
		if (from.X > to.X || from.Y > to.Y) {
			return;
		}

		// Floor division, the cells of the copies before the layer are negative
		auto floorDiv = [](std::int32_t a, std::int32_t b) {
			return (a >= 0 ? a / b : -((-a + b - 1) / b));
		};
		auto wrap = [](std::int32_t value, std::int32_t size) {
			value %= size;
			return (value < 0 ? value + size : value);
		};

		// Selected tiles next to each other in a row are one rectangle, and so are the edges along them. Zoomed out
		// far, tiles of a few pixels are taken a few at a time, as selected as the tile in the middle of them, so
		// there are never more rectangles than such blocks seen.
		std::int32_t unit = GetCellUnit(view);
		Vector2i fromUnit(floorDiv(from.X, unit), floorDiv(from.Y, unit)), toUnit(floorDiv(to.X, unit), floorDiv(to.Y, unit));
		Recti units(fromUnit.X, fromUnit.Y, toUnit.X - fromUnit.X + 1, toUnit.Y - fromUnit.Y + 1);
		const std::int32_t repeats = (repeatX ? 1 : 0) | (repeatY ? 2 : 0);
		if (selection.GetRevision() != _selectionRevision || selection.GetLayer() != _selectionLayer || unit != _selectionUnit ||
			units.X != _selectionUnits.X || units.Y != _selectionUnits.Y || units.W != _selectionUnits.W || units.H != _selectionUnits.H ||
			view.Size != _selectionLayerSize || repeats != _selectionRepeats) {
			_selectionRevision = selection.GetRevision();
			_selectionLayer = selection.GetLayer();
			_selectionUnit = unit;
			_selectionUnits = units;
			_selectionLayerSize = view.Size;
			_selectionRepeats = repeats;
			_selectionRuns.clear();
			_selectionEdges.clear();

			auto isSelected = [&](std::int32_t ux, std::int32_t uy) {
				std::int32_t x = ux * unit + unit / 2;
				std::int32_t y = uy * unit + unit / 2;
				return selection.Contains(repeatX ? wrap(x, view.Size.X) : x, repeatY ? wrap(y, view.Size.Y) : y);
			};

			// The blocks of a row whose middles fall in a copy of the selected columns, only those can be selected - so a
			// view of many copies doesn't look up every block it shows
			SmallVector<Vector2i, 8> spans;
			if (!repeatX) {
				spans.push_back(Vector2i(fromUnit.X, toUnit.X));
			} else {
				const std::int32_t size = view.Size.X;
				const std::int32_t firstMiddle = fromUnit.X * unit + unit / 2;
				const std::int32_t lastMiddle = toUnit.X * unit + unit / 2;
				for (std::int32_t k = floorDiv(firstMiddle - (bounds.X + bounds.W - 1), size); ; k++) {
					const std::int32_t first = bounds.X + k * size;
					const std::int32_t last = first + bounds.W - 1;
					if (first > lastMiddle) {
						break;
					}
					// The blocks with their middle in [first, last]
					std::int32_t u0 = std::max(-floorDiv(-(first - unit / 2), unit), fromUnit.X);
					std::int32_t u1 = std::min(floorDiv(last - unit / 2, unit), toUnit.X);
					if (u0 > u1) {
						continue;
					}
					if (!spans.empty() && spans.back().Y + 1 >= u0) {
						spans.back().Y = std::max(spans.back().Y, u1);
					} else {
						spans.push_back(Vector2i(u0, u1));
					}
				}
			}

			for (std::int32_t uy = fromUnit.Y; uy <= toUnit.Y; uy++) {
				// Not a row of the selection at all
				std::int32_t middleY = uy * unit + unit / 2;
				if (repeatY) {
					middleY = wrap(middleY, view.Size.Y);
				}
				if (middleY < bounds.Y || middleY >= bounds.Y + bounds.H) {
					continue;
				}
				for (const Vector2i& span : spans) {
					std::int32_t ux = span.X;
					while (ux <= span.Y) {
						if (!isSelected(ux, uy)) {
							ux++;
							continue;
						}
						std::int32_t start = ux;
						while (ux <= span.Y && isSelected(ux, uy)) {
							ux++;
						}
						std::int32_t end = ux;
						_selectionRuns.push_back({ uy, start, end });
						// Across the seam of copies too, a selection that goes on in the next copy has no edge there
						if (!isSelected(start - 1, uy)) {
							_selectionEdges.push_back({ uy, start, start, 0 });
						}
						if (!isSelected(end, uy)) {
							_selectionEdges.push_back({ uy, end, end, 1 });
						}
						// Above and below, along the parts of the run next to tiles that aren't selected
						for (std::int32_t side = 0; side < 2; side++) {
							std::int32_t ny = (side == 0 ? uy - 1 : uy + 1);
							std::int32_t x = start;
							while (x < end) {
								if (isSelected(x, ny)) {
									x++;
									continue;
								}
								std::int32_t edgeStart = x;
								while (x < end && !isSelected(x, ny)) {
									x++;
								}
								_selectionEdges.push_back({ uy, edgeStart, x, std::uint8_t(side == 0 ? 2 : 3) });
							}
						}
					}
				}
			}
		}

		auto unitX = [&](std::int32_t ux) {
			return view.CellToScreen(Vector2i(ux * unit, 0)).X;
		};
		auto unitY = [&](std::int32_t uy) {
			return view.CellToScreen(Vector2i(0, uy * unit)).Y;
		};
		for (const SelectionRun& run : _selectionRuns) {
			float top = unitY(run.Y), bottom = unitY(run.Y + 1);
			float left = unitX(run.Start), right = unitX(run.End);
			painter.FillRect(Rectf(left, top, right - left, bottom - top), UiTheme::Selection, UiLayer::Fill);
		}
		// The edges between selected and unselected tiles are marching ants
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		float animTime = _session.GetAnimTime();
		for (const SelectionEdge& edge : _selectionEdges) {
			float top = unitY(edge.Y), bottom = unitY(edge.Y + 1);
			switch (edge.Side) {
				case 0: DrawSelectionEdge(painter, unitX(edge.Start), top, bottom - top, false, animTime); break;
				case 1: DrawSelectionEdge(painter, unitX(edge.Start) - thickness, top, bottom - top, false, animTime); break;
				default: {
					float left = unitX(edge.Start);
					DrawSelectionEdge(painter, left, edge.Side == 2 ? top : bottom - thickness, unitX(edge.End) - left, true, animTime);
					break;
				}
			}
		}
	}

	std::int32_t EditorShell::GetCellUnit(const LayerView& view)
	{
		// Tiles drawn one by one at least this large on the screen
		constexpr float MinUnitSize = 3.0f;
		float cellSize = view.CellToScreen(Vector2i(1, 1)).X - view.CellToScreen(Vector2i(0, 0)).X;
		if (cellSize >= MinUnitSize) {
			return 1;
		}
		return std::max<std::int32_t>(std::int32_t(std::ceil(MinUnitSize / std::max(cellSize, 0.001f))), 1);
	}

	void EditorShell::DrawPlayPath(UiPainter& painter)
	{
		const PlayPath& path = _session.GetLastPlayPath();
		std::int32_t pointCount = path.GetPointCount();
		if (pointCount != _cachedPathPoints || std::int32_t(path.Segments.size()) != _cachedPathSegments) {
			_cachedPathPoints = pointCount;
			_cachedPathSegments = std::int32_t(path.Segments.size());
			_cachedPath.clear();
			for (const PlayPath::Segment& segment : path.Segments) {
				if (segment.Points.empty()) {
					continue;
				}
				// The speed changes in steps between the samples, so it's smoothed, and so are the samples themselves,
				// the player's position jitters from frame to frame
				constexpr std::int32_t SpeedWindow = 4;
				constexpr std::int32_t PointWindow = 4;
				std::int32_t count = std::int32_t(segment.Points.size());
				SmallVector<float, 0> speeds;
				SmallVector<Vector2f, 0> smoothed;
				speeds.resize_for_overwrite(std::size_t(count));
				smoothed.resize_for_overwrite(std::size_t(count));
				for (std::int32_t i = 0; i < count; i++) {
					float sum = 0.0f;
					std::int32_t samples = 0;
					for (std::int32_t j = std::max(i - SpeedWindow, 0); j <= std::min(i + SpeedWindow, count - 1); j++) {
						sum += (j < std::int32_t(segment.Speeds.size()) ? segment.Speeds[j] : 0.0f);
						samples++;
					}
					speeds[i] = sum / float(samples);

					// The ends stay where they are, the markers are drawn there
					std::int32_t window = std::min({ PointWindow, i, count - 1 - i });
					Vector2f position;
					for (std::int32_t j = i - window; j <= i + window; j++) {
						position += segment.Points[j];
					}
					smoothed[i] = position / float(window * 2 + 1);
				}

				SmallVector<std::int32_t, 0> kept;
				// Only the course is kept, not every step the player made
				SimplifyRdpIndices(smoothed, 5.0f, kept);
				SmallVector<Vector2f, 0> controlPoints;
				SmallVector<float, 0> controlSpeeds;
				for (std::int32_t index : kept) {
					controlPoints.push_back(smoothed[index]);
					controlSpeeds.push_back(speeds[index]);
				}

				constexpr std::int32_t Subdivisions = 8;
				CachedPathSegment& cached = _cachedPath.emplace_back();
				Rendering::PolylineRenderer::SmoothCatmullRom(controlPoints, Subdivisions, cached.Points, 1 << 20);
				// A segment between two control points is divided into the same number of points, the speed goes along
				for (std::int32_t i = 0; i < std::int32_t(cached.Points.size()); i++) {
					std::int32_t control = i / Subdivisions;
					if (control + 1 >= std::int32_t(controlSpeeds.size())) {
						cached.Speeds.push_back(controlSpeeds.back());
					} else {
						float t = float(i % Subdivisions) / float(Subdivisions);
						cached.Speeds.push_back(controlSpeeds[control] + (controlSpeeds[control + 1] - controlSpeeds[control]) * t);
					}
				}
				cached.End = std::uint8_t(segment.End);
			}
		}
		if (_cachedPath.empty()) {
			return;
		}
		if (_pathRenderer == nullptr) {
			_pathRenderer = std::make_unique<Rendering::PolylineRenderer>(1 << 20);
		}
		_pathRenderer->BeginFrame();

		std::int32_t spriteLayer = _session.GetDocument().GetSpriteLayerIndex();
		LayerView view = _session.GetLayerView(spriteLayer >= 0 ? spriteLayer : 0);
		// Thin where the player was slow, thick where fast. Thinner when the view is far too, but only partly, so it's
		// still seen then, and doesn't cover what's around it when the view is close.
		constexpr float SlowSpeed = 1.0f;
		constexpr float FastSpeed = 10.0f;
		float zoom = std::clamp(std::sqrt(view.Scale), 0.4f, 1.5f);
		float minHalfThickness = std::max(1.2f, painter.GetScale() * 1.2f) * zoom;
		float maxHalfThickness = std::max(4.5f, painter.GetScale() * 4.5f) * zoom;
		// The dots follow the line, a little wider than it where it ends, so they don't stand out of a thin one
		float minMarkerRadius = std::max(1.5f, painter.GetScale() * 1.8f) * zoom;
		float ringWidth = std::max(1.0f, painter.GetScale() * 1.2f * zoom);
		Rectf visible(_viewRect.X - maxHalfThickness, _viewRect.Y - maxHalfThickness, _viewRect.W + maxHalfThickness * 2.0f, _viewRect.H + maxHalfThickness * 2.0f);
		std::uint16_t z = painter.GetDepth(UiLayer::Content);

		// From blue at the start to gold at the end, in short runs of a single color each
		std::int32_t total = 0;
		for (const CachedPathSegment& segment : _cachedPath) {
			total += std::int32_t(segment.Points.size());
		}
		std::int32_t runLength = std::max(8, total / 32);
		SmallVector<Vector2f, 0> screen;
		SmallVector<float, 0> halfThicknesses;
		std::int32_t index = 0;
		float endHalfThickness = minHalfThickness;
		for (const CachedPathSegment& segment : _cachedPath) {
			std::int32_t count = std::int32_t(segment.Points.size());
			screen.clear();
			halfThicknesses.clear();
			for (std::int32_t i = 0; i < count; i++) {
				screen.push_back(view.WorldToScreen(segment.Points[i]));
				float speed = std::clamp((segment.Speeds[i] - SlowSpeed) / (FastSpeed - SlowSpeed), 0.0f, 1.0f);
				halfThicknesses.push_back(minHalfThickness + (maxHalfThickness - minHalfThickness) * speed);
			}
			for (std::int32_t start = 0; start < count - 1; start += runLength) {
				std::int32_t end = std::min(start + runLength, count - 1);
				// Runs that can't be seen are left out
				bool seen = false;
				for (std::int32_t i = start; i <= end && !seen; i++) {
					seen = visible.Contains(screen[i]);
				}
				if (!seen) {
					continue;
				}
				float t = float(index + (start + end) / 2) / float(std::max(total, 1));
				Colorf color(0.3f + 0.7f * t, 0.55f + 0.2f * t, 1.0f - 0.8f * t, 0.85f);
				// The runs share their first and last point and their directions there, so they join without a seam
				SmallVector<RenderCommand*, 4> commands;
				_pathRenderer->DrawRange(screen, start, end, halfThicknesses, z, color, commands);
				for (RenderCommand* command : commands) {
					painter.Command(command);
				}
			}
			index += count;
			if (count == 0) {
				continue;
			}

			Vector2f last = screen.back();
			endHalfThickness = halfThicknesses.back();
			float radius = std::max(endHalfThickness * 1.4f, minMarkerRadius);
			switch (PlayPath::Break(segment.End)) {
				case PlayPath::Break::Died: painter.Circle(last, radius, UiTheme::Danger, UiLayer::Overlay); break;
				case PlayPath::Break::Warped: painter.Circle(last, radius, Colorf(0.4f, 0.6f, 1.0f, 1.0f), UiLayer::Overlay); break;
				case PlayPath::Break::Exited: painter.Circle(last, radius, UiTheme::Play, UiLayer::Overlay); break;
				default: break;
			}
		}
		// Where the play-test was left
		const CachedPathSegment& last = _cachedPath.back();
		if (!last.Points.empty()) {
			Vector2f end = view.WorldToScreen(last.Points.back());
			float inner = std::max(endHalfThickness * 1.2f, minMarkerRadius);
			painter.Circle(end, inner + ringWidth, Colorf(1.0f, 1.0f, 1.0f, 0.9f), UiLayer::Overlay);
			painter.Circle(end, inner, UiTheme::Accent, UiLayer::Overlay);
		}
	}
	void EditorShell::DrawMasks(UiPainter& painter, const LayerView& view)
	{
		std::int32_t mode = _session.GetMaskMode();
		Tiles::TileMap* tileMap = _session.GetTileMap();
		LevelDocument& document = _session.GetDocument();
		std::int32_t layerIndex = _session.GetCurrentLayer();
		if (mode == 0 || tileMap == nullptr || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}
		// Over the level, or instead of it
		_session.GetMaskOverlay().Draw(painter, *tileMap, document, layerIndex, view, _viewRect, mode == 2);
		if (layerIndex == document.GetSpriteLayerIndex()) {
			DrawMaskGlyphs(painter, view);
		}
		// Translucent and invisible tiles, solid as any other, but drawn otherwise or not at all
		DrawTileModifiers(painter, document.Layers[layerIndex], view, _viewRect);
	}

	void EditorShell::DrawReachability(UiPainter& painter)
	{
		const PlayOverlays& playOverlays = _session.GetPlayOverlays();
		ArrayView<const std::int32_t> distances = playOverlays.GetReachability();
		LevelDocument& document = _session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (distances.empty() || spriteLayer < 0 || std::int32_t(distances.size()) != gridSize.X * gridSize.Y) {
			return;
		}
		LayerView view = _session.GetLayerView(spriteLayer);
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X, _viewRect.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X + _viewRect.W, _viewRect.Y + _viewRect.H)));
		from.X = std::max(from.X, 0);
		from.Y = std::max(from.Y, 0);
		to.X = std::min(to.X, gridSize.X - 1);
		to.Y = std::min(to.Y, gridSize.Y - 1);

		if (from.X > to.X || from.Y > to.Y) {
			return;
		}

		// Brighter near the start, further away it fades, so the way through the level can be read; in a few steps, so
		// tiles next to each other in a row of the same step are one rectangle, and zoomed out far, tiles of a few pixels
		// are taken a few at a time, as the tile in the middle of them, see DrawSelection()
		constexpr std::int32_t Steps = 16;
		std::int32_t maxDistance = std::max<std::int32_t>(playOverlays.GetReachabilityMaxDistance(), 1);
		std::int32_t unit = GetCellUnit(view);
		auto getStep = [&](std::int32_t ux, std::int32_t uy) -> std::int32_t {
			std::int32_t x = std::min<std::int32_t>(ux * unit + unit / 2, gridSize.X - 1);
			std::int32_t y = std::min<std::int32_t>(uy * unit + unit / 2, gridSize.Y - 1);
			std::int32_t distance = distances[std::size_t(y) * std::size_t(gridSize.X) + std::size_t(x)];
			if (distance < 0) {
				return -1;
			}
			float near = std::clamp(1.0f - float(distance) / float(maxDistance), 0.0f, 1.0f);
			return std::int32_t(std::round(near * float(Steps)));
		};
		Vector2i fromUnit(from.X / unit, from.Y / unit), toUnit(to.X / unit, to.Y / unit);
		for (std::int32_t uy = fromUnit.Y; uy <= toUnit.Y; uy++) {
			float top = view.CellToScreen(Vector2i(0, uy * unit)).Y;
			float bottom = view.CellToScreen(Vector2i(0, std::min<std::int32_t>((uy + 1) * unit, gridSize.Y))).Y;
			std::int32_t ux = fromUnit.X;
			while (ux <= toUnit.X) {
				std::int32_t step = getStep(ux, uy);
				std::int32_t start = ux;
				do {
					ux++;
				} while (ux <= toUnit.X && getStep(ux, uy) == step);
				if (step < 0) {
					continue;
				}
				// The color of reached places, as the theme has it
				Colorf color = UiTheme::OverlayGood;
				color.A = 0.14f + 0.18f * (float(step) / float(Steps));
				float left = view.CellToScreen(Vector2i(start * unit, 0)).X;
				float right = view.CellToScreen(Vector2i(std::min<std::int32_t>(ux * unit, gridSize.X), 0)).X;
				painter.FillRect(Rectf(left, top, right - left, bottom - top), color, UiLayer::Fill);
			}
		}
	}

	void EditorShell::DrawAnnotations(UiPainter& painter)
	{
		const SmallVectorImpl<Annotation>& annotations = _annotations.Get(_session.GetDocument());
		if (annotations.empty()) {
			return;
		}
		std::int32_t spriteLayer = _session.GetDocument().GetSpriteLayerIndex();
		LayerView view = _session.GetLayerView(spriteLayer >= 0 ? spriteLayer : 0);
		float size = std::round(painter.Px(5.0f));
		float padding = painter.Px(UiTheme::Padding);
		for (const Annotation& annotation : annotations) {
			Vector2f pos = view.WorldToScreen(Vector2f(float(annotation.X), float(annotation.Y)));
			if (!_viewRect.Contains(pos)) {
				continue;
			}
			pos = Vector2f(std::round(pos.X), std::round(pos.Y));
			// In the color it was given, a note that was done is dimmer
			Colorf ownColor((annotation.Color & 0xFF) / 255.0f, ((annotation.Color >> 8) & 0xFF) / 255.0f, ((annotation.Color >> 16) & 0xFF) / 255.0f, 1.0f);
			float alpha = (annotation.Resolved ? 0.45f : 1.0f);
			painter.PushOpacity(alpha);
			if (annotation.IsBookmark()) {
				// A diamond with its number, and its name next to it
				Vector2f diamond[5] = { pos + Vector2f(0.0f, -size * 1.6f), pos + Vector2f(size * 1.6f, 0.0f), pos + Vector2f(0.0f, size * 1.6f),
					pos + Vector2f(-size * 1.6f, 0.0f), pos + Vector2f(0.0f, -size * 1.6f) };
				Colorf color = (annotation.Color != 0 ? ownColor : Colorf(0.4f, 0.8f, 1.0f, 1.0f));
				painter.Circle(pos, size * 1.3f, Colorf(color.R * 0.4f, color.G * 0.4f, color.B * 0.4f, 0.9f), UiLayer::Overlay);
				painter.Polyline(diamond, std::max(1.0f, painter.Px(1.0f)), color, UiLayer::Overlay);
				char number[2] = { char('0' + annotation.Slot), '\0' };
				painter.TextInRect(StringView(number, 1), Rectf(pos.X - size * 2.0f, pos.Y - size * 2.0f, size * 4.0f, size * 4.0f),
					UiTheme::Text, Alignment::Center, UiLayer::OverlayText);
				if (!annotation.Text.empty()) {
					String text = EventCatalog::MakeTextPreview(annotation.Text, 32);
					float height = painter.Px(UiTheme::RowHeight);
					std::int32_t previousSlot = painter.GetSlot();
					painter.SetSlot(BubbleSlot);
					painter.TextInRect(text, Rectf(pos.X + size * 2.0f, pos.Y - height * 0.5f, painter.MeasureText(text) + padding, height),
						UiTheme::Text, Alignment::Left, UiLayer::OverlayText);
					painter.SetSlot(previousSlot);
				}
			} else {
				// A pin with the text next to it, and how many replies it has
				Colorf color = (annotation.Color != 0 ? ownColor : Colorf(1.0f, 0.85f, 0.35f, 1.0f));
				painter.Circle(pos, size, color, UiLayer::Overlay);
				painter.Circle(pos, size * 0.45f, Colorf(color.R * 0.4f, color.G * 0.35f, color.B * 0.3f, 1.0f), UiLayer::Overlay);
				String text = EventCatalog::MakeTextPreview(annotation.Text, 40);
				if (!annotation.Replies.empty()) {
					// TRANSLATORS: After the text of a note pinned in the level, {} is the number of its replies, keep the space in front
					text += _fx("editor", " ({})", std::int32_t(annotation.Replies.size()));
				}
				if (!text.empty()) {
					float width = painter.MeasureText(text) + padding * 2.0f;
					float height = painter.Px(UiTheme::RowHeight);
					Rectf bubble(pos.X + size * 1.5f, pos.Y - height * 0.5f, width, height);
					std::int32_t previousSlot = painter.GetSlot();
					painter.SetSlot(BubbleSlot);
					painter.FillRect(bubble, Colorf(0.25f, 0.2f, 0.08f, 0.85f), UiLayer::Overlay);
					painter.TextInRect(text, Rectf(bubble.X + padding, bubble.Y, bubble.W - padding * 2.0f, bubble.H), UiTheme::TextAccent, Alignment::Left, UiLayer::OverlayText);
					painter.SetSlot(previousSlot);
				}
			}
			painter.PopOpacity();
		}
	}

	bool EditorShell::HasMaskGlyph(EventType type)
	{
		switch (type) {
			case EventType::ModifierOneWay:
			case EventType::ModifierVine:
			case EventType::ModifierHook:
			case EventType::ModifierHPole:
			case EventType::ModifierVPole:
			case EventType::ModifierHurt:
			case EventType::ModifierDeath:
			case EventType::SceneryDestruct:
			case EventType::SceneryDestructButtstomp:
			case EventType::SceneryDestructSpeed:
			case EventType::SceneryCollapse:
			case EventType::ModifierRicochet:
			case EventType::ModifierSlide:
			case EventType::ModifierNoClimb:
			case EventType::ModifierTube:
			case EventType::TriggerArea:
				return true;
			default:
				return false;
		}
	}

	void EditorShell::DrawMaskGlyphs(UiPainter& painter, const LayerView& view)
	{
		LevelDocument& document = _session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		float tileSize = 32.0f * view.Scale;
		// Too small to be told apart
		if (gridSize.X <= 0 || gridSize.Y <= 0 || tileSize < 8.0f) {
			return;
		}
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X, _viewRect.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X + _viewRect.W, _viewRect.Y + _viewRect.H)));
		from.X = std::max(from.X, 0);
		from.Y = std::max(from.Y, 0);
		to.X = std::min(to.X, gridSize.X - 1);
		to.Y = std::min(to.Y, gridSize.Y - 1);
		if (to.X < from.X || to.Y < from.Y) {
			return;
		}

		float thickness = std::max(1.5f, std::round(std::min(painter.GetScale() * 1.5f, tileSize / 12.0f)));
		Rectf rect;
		// Points in the tile from 0 to 1, a single bright color, the masks are gray and dark under them
		auto stroke = [&painter, &rect, thickness](std::initializer_list<Vector2f> points, const Colorf& color) {
			SmallVector<Vector2f, 16> screen;
			for (const Vector2f& p : points) {
				screen.push_back(Vector2f(rect.X + p.X * rect.W, rect.Y + p.Y * rect.H));
			}
			painter.Polyline(screen, thickness, color, UiLayer::Overlay);
		};
		auto arrowUp = [&stroke](float x, float top, float bottom, const Colorf& color) {
			stroke({ Vector2f(x, bottom), Vector2f(x, top) }, color);
			stroke({ Vector2f(x - 0.12f, top + 0.12f), Vector2f(x, top), Vector2f(x + 0.12f, top + 0.12f) }, color);
		};

		const Colorf passColor(0.6f, 1.0f, 0.5f, 1.0f);
		const Colorf grabColor(1.0f, 0.92f, 0.4f, 1.0f);
		const Colorf breakColor(1.0f, 0.68f, 0.32f, 1.0f);
		const Colorf dangerColor(1.0f, 0.42f, 0.42f, 1.0f);
		const Colorf otherColor(0.5f, 0.88f, 1.0f, 1.0f);
		// A muted brown, light enough to be seen over the masks
		const Colorf ricochetColor(0.78f, 0.62f, 0.46f, 1.0f);

		for (std::int32_t y = from.Y; y <= to.Y; y++) {
			std::uint32_t rowEnd = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(to.X);
			for (std::size_t i = document.FindEvent(std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(from.X)); i < document.Events.size(); i++) {
				const DocumentEvent& event = document.Events[i];
				if (event.Cell > rowEnd) {
					break;
				}
				rect = view.CellToScreen(Vector2i(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), y));
				switch (EventType(event.Record.Type)) {
					case EventType::ModifierOneWay:
						// Passed through from below
						stroke({ Vector2f(0.08f, 0.1f), Vector2f(0.92f, 0.1f) }, passColor);
						arrowUp(0.5f, 0.25f, 0.75f, passColor);
						break;
					case EventType::ModifierVine:
						stroke({ Vector2f(0.0f, 0.12f), Vector2f(0.125f, 0.2f), Vector2f(0.25f, 0.12f), Vector2f(0.375f, 0.2f), Vector2f(0.5f, 0.12f),
							Vector2f(0.625f, 0.2f), Vector2f(0.75f, 0.12f), Vector2f(0.875f, 0.2f), Vector2f(1.0f, 0.12f) }, grabColor);
						break;
					case EventType::ModifierHook:
						stroke({ Vector2f(0.5f, 0.05f), Vector2f(0.5f, 0.45f), Vector2f(0.45f, 0.55f), Vector2f(0.35f, 0.55f), Vector2f(0.3f, 0.45f) }, grabColor);
						break;
					case EventType::ModifierHPole:
						stroke({ Vector2f(0.0f, 0.5f), Vector2f(1.0f, 0.5f) }, grabColor);
						break;
					case EventType::ModifierVPole:
						stroke({ Vector2f(0.5f, 0.0f), Vector2f(0.5f, 1.0f) }, grabColor);
						break;
					case EventType::ModifierHurt: {
						// Spikes on the sides that hurt, all of them if none is given
						std::uint8_t sides = event.Record.Params[0];
						if ((sides & std::uint8_t(Direction::Any)) == 0) {
							sides = std::uint8_t(Direction::Any);
						}
						if ((sides & std::uint8_t(Direction::Up)) != 0) {
							stroke({ Vector2f(0.1f, 0.2f), Vector2f(0.25f, 0.05f), Vector2f(0.4f, 0.2f), Vector2f(0.55f, 0.05f), Vector2f(0.7f, 0.2f), Vector2f(0.85f, 0.05f), Vector2f(0.9f, 0.1f) }, dangerColor);
						}
						if ((sides & std::uint8_t(Direction::Down)) != 0) {
							stroke({ Vector2f(0.1f, 0.8f), Vector2f(0.25f, 0.95f), Vector2f(0.4f, 0.8f), Vector2f(0.55f, 0.95f), Vector2f(0.7f, 0.8f), Vector2f(0.85f, 0.95f), Vector2f(0.9f, 0.9f) }, dangerColor);
						}
						if ((sides & std::uint8_t(Direction::Left)) != 0) {
							stroke({ Vector2f(0.2f, 0.1f), Vector2f(0.05f, 0.25f), Vector2f(0.2f, 0.4f), Vector2f(0.05f, 0.55f), Vector2f(0.2f, 0.7f), Vector2f(0.05f, 0.85f), Vector2f(0.1f, 0.9f) }, dangerColor);
						}
						if ((sides & std::uint8_t(Direction::Right)) != 0) {
							stroke({ Vector2f(0.8f, 0.1f), Vector2f(0.95f, 0.25f), Vector2f(0.8f, 0.4f), Vector2f(0.95f, 0.55f), Vector2f(0.8f, 0.7f), Vector2f(0.95f, 0.85f), Vector2f(0.9f, 0.9f) }, dangerColor);
						}
						break;
					}
					case EventType::ModifierDeath:
						stroke({ Vector2f(0.25f, 0.25f), Vector2f(0.75f, 0.75f) }, dangerColor);
						stroke({ Vector2f(0.75f, 0.25f), Vector2f(0.25f, 0.75f) }, dangerColor);
						stroke({ Vector2f(0.15f, 0.15f), Vector2f(0.85f, 0.15f), Vector2f(0.85f, 0.85f), Vector2f(0.15f, 0.85f), Vector2f(0.15f, 0.15f) }, dangerColor);
						break;
					case EventType::SceneryDestruct:
						stroke({ Vector2f(0.3f, 0.3f), Vector2f(0.7f, 0.7f) }, breakColor);
						stroke({ Vector2f(0.7f, 0.3f), Vector2f(0.3f, 0.7f) }, breakColor);
						break;
					case EventType::SceneryDestructButtstomp:
						// Broken from above
						stroke({ Vector2f(0.5f, 0.15f), Vector2f(0.5f, 0.75f) }, breakColor);
						stroke({ Vector2f(0.38f, 0.63f), Vector2f(0.5f, 0.75f), Vector2f(0.62f, 0.63f) }, breakColor);
						break;
					case EventType::SceneryDestructSpeed:
						stroke({ Vector2f(0.2f, 0.3f), Vector2f(0.4f, 0.5f), Vector2f(0.2f, 0.7f) }, breakColor);
						stroke({ Vector2f(0.5f, 0.3f), Vector2f(0.7f, 0.5f), Vector2f(0.5f, 0.7f) }, breakColor);
						break;
					case EventType::SceneryCollapse:
						stroke({ Vector2f(0.3f, 0.15f), Vector2f(0.45f, 0.4f), Vector2f(0.35f, 0.55f), Vector2f(0.55f, 0.85f) }, breakColor);
						stroke({ Vector2f(0.45f, 0.4f), Vector2f(0.7f, 0.45f) }, breakColor);
						break;
					case EventType::ModifierRicochet:
						stroke({ Vector2f(0.5f, 0.2f), Vector2f(0.8f, 0.5f), Vector2f(0.5f, 0.8f), Vector2f(0.2f, 0.5f), Vector2f(0.5f, 0.2f) }, ricochetColor);
						break;
					case EventType::ModifierSlide:
						stroke({ Vector2f(0.1f, 0.85f), Vector2f(0.9f, 0.85f) }, otherColor);
						stroke({ Vector2f(0.2f, 0.65f), Vector2f(0.8f, 0.65f) }, otherColor);
						break;
					case EventType::ModifierNoClimb:
						stroke({ Vector2f(0.15f, 0.15f), Vector2f(0.85f, 0.85f) }, dangerColor);
						stroke({ Vector2f(0.3f, 0.1f), Vector2f(0.3f, 0.9f) }, dangerColor);
						stroke({ Vector2f(0.7f, 0.1f), Vector2f(0.7f, 0.9f) }, dangerColor);
						break;
					case EventType::ModifierTube:
						stroke({ Vector2f(0.3f, 0.3f), Vector2f(0.7f, 0.3f), Vector2f(0.7f, 0.7f), Vector2f(0.3f, 0.7f), Vector2f(0.3f, 0.3f) }, otherColor);
						break;
					case EventType::TriggerArea:
						// Corners, the tile changes when its trigger does
						stroke({ Vector2f(0.1f, 0.3f), Vector2f(0.1f, 0.1f), Vector2f(0.3f, 0.1f) }, breakColor);
						stroke({ Vector2f(0.7f, 0.9f), Vector2f(0.9f, 0.9f), Vector2f(0.9f, 0.7f) }, breakColor);
						break;
					default:
						break;
				}
			}
		}
	}

	void EditorShell::DrawSceneEvents(UI::Canvas& canvas)
	{
		if (!_session.AreEventsShown() && !_session.IsEditingEvents()) {
			return;
		}
		LevelDocument& document = _session.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		Vector2i gridSize = document.GetEventGridSize();
		if (spriteLayer < 0 || gridSize.X <= 0 || gridSize.Y <= 0) {
			return;
		}

		// All of the screen, the level is seen behind the panels too, and the tiles around it, sprites reach over them
		LayerView view = _session.GetLayerView(spriteLayer);
		constexpr std::int32_t Margin = 2;
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(Bounds.X, Bounds.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(Bounds.X + Bounds.W, Bounds.Y + Bounds.H)));
		from.X = std::max(from.X - Margin, 0);
		from.Y = std::max(from.Y - Margin, 0);
		to.X = std::min(to.X + Margin, gridSize.X - 1);
		to.Y = std::min(to.Y + Margin, gridSize.Y - 1);

		// Faded as the marks over them are, see DrawEvents(), at the depth of the objects of the game
		bool editingEvents = _session.IsEditingEvents();
		float alpha = (editingEvents || _session.GetCurrentLayer() == spriteLayer ? 1.0f : 0.5f);
		EventPreviews& previews = _session.GetEventPreviews();
		const EventSelection& selection = _session.GetEventSelection();
		float animTime = _session.GetAnimTime();
		bool moving = (editingEvents && IsMovingEvents());
		std::uint8_t difficultyBit = GetDifficultyFlag(_session.GetSettings().PlayDifficulty);
		constexpr std::uint16_t Z = std::uint16_t(ILevelHandler::MainPlaneZ);

		if (to.X >= from.X && to.Y >= from.Y) {
			for (std::int32_t y = from.Y; y <= to.Y; y++) {
				std::uint32_t rowEnd = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(to.X);
				for (std::size_t i = document.FindEvent(std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(from.X)); i < document.Events.size(); i++) {
					const DocumentEvent& event = document.Events[i];
					if (event.Cell > rowEnd) {
						break;
					}
					std::int32_t x = std::int32_t(event.Cell % std::uint32_t(gridSize.X));
					std::uint8_t difficulties = event.Record.Flags & Formats::EventRecord::DifficultyMask;
					float eventAlpha = (difficultyBit != 0 && difficulties != 0 && (difficulties & difficultyBit) == 0 ? alpha * 0.35f : alpha);
					if (moving && selection.Contains(event.Cell)) {
						eventAlpha *= 0.35f;
					}
					previews.DrawInScene(canvas, event.Record, Vector2f(x * 32.0f + 16.0f, y * 32.0f + 16.0f), eventAlpha, animTime, Z);
				}
			}
		}

		// Events off the grid, at their own position, those being moved faded as those on the grid
		for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
			const Formats::OffGridEventRecord& event = document.OffGridEvents[i];
			float eventAlpha = (moving && selection.ContainsOffGrid(std::int32_t(i)) ? alpha * 0.35f : alpha);
			previews.DrawInScene(canvas, event.Event, Vector2f(float(event.X), float(event.Y)), eventAlpha, animTime, Z);
		}
	}

	bool EditorShell::IsMovingEvents() const
	{
		ToolManager& tools = _session.GetTools();
		return (tools.GetActiveType() == EditorToolType::Event && static_cast<const EventTool*>(tools.GetActive())->IsMovingEvents());
	}

	void EditorShell::DrawEvents(UiPainter& painter, const Rectf& area, Vector2f holeCenter, float holeRadius)
	{
		LevelDocument& document = _session.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		Vector2i gridSize = document.GetEventGridSize();
		if (spriteLayer < 0 || gridSize.X <= 0 || gridSize.Y <= 0) {
			return;
		}

		LayerView view = _session.GetLayerView(spriteLayer);
		// Sprites reach over the tiles around theirs, so a few more rows and columns are drawn
		constexpr std::int32_t Margin = 2;
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(area.X, area.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(area.X + area.W, area.Y + area.H)));
		from.X = std::max(from.X - Margin, 0);
		from.Y = std::max(from.Y - Margin, 0);
		to.X = std::min(to.X + Margin, gridSize.X - 1);
		to.Y = std::min(to.Y + Margin, gridSize.Y - 1);
		if (to.X < from.X || to.Y < from.Y) {
			return;
		}

		// While the game is revealed in a ring, the events are seen only outside of it, the selection isn't seen at all
		bool inTransition = (holeRadius >= 0.0f);
		float tileSize = 32.0f * view.Scale;
		auto reachesHole = [inTransition, holeCenter, holeRadius, tileSize](const Rectf& rect) {
			if (!inTransition) {
				return false;
			}
			// With the parts of a sprite that reach out of its tile
			float margin = tileSize * 0.5f;
			float x = std::clamp(holeCenter.X, rect.X - margin, rect.X + rect.W + margin);
			float y = std::clamp(holeCenter.Y, rect.Y - margin, rect.Y + rect.H + margin);
			float dx = x - holeCenter.X, dy = y - holeCenter.Y;
			return (dx * dx + dy * dy < holeRadius * holeRadius);
		};

		bool editingEvents = _session.IsEditingEvents() && !inTransition;
		// Events are dimmed while another layer than theirs is edited
		float alpha = (editingEvents || inTransition || _session.GetCurrentLayer() == spriteLayer ? 1.0f : 0.5f);
		EventPreviews& previews = _session.GetEventPreviews();
		const EventSelection& selection = _session.GetEventSelection();
		float animTime = _session.GetAnimTime();
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		float badge = std::max(2.0f, std::round(tileSize / 8.0f));
		bool moving = (editingEvents && IsMovingEvents());
		std::uint8_t difficultyBit = GetDifficultyFlag(_session.GetSettings().PlayDifficulty);
		// See DrawMaskGlyphs()
		bool maskGlyphs = (_session.GetMaskMode() != 0 && _session.GetCurrentLayer() == spriteLayer && tileSize >= 8.0f);

		for (std::int32_t y = from.Y; y <= to.Y; y++) {
			std::uint32_t rowEnd = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(to.X);
			for (std::size_t i = document.FindEvent(std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(from.X)); i < document.Events.size(); i++) {
				const DocumentEvent& event = document.Events[i];
				if (event.Cell > rowEnd) {
					break;
				}
				Vector2i cell(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), y);
				Rectf rect = view.CellToScreen(cell);
				if (reachesHole(rect)) {
					continue;
				}
				bool selected = (editingEvents && selection.Contains(event.Cell));
				std::uint8_t flags = event.Record.Flags;
				std::uint8_t difficulties = flags & Formats::EventRecord::DifficultyMask;
				// Events that won't be there when the level is played are faded
				float eventAlpha = (difficultyBit != 0 && difficulties != 0 && (difficulties & difficultyBit) == 0 ? alpha * 0.35f : alpha);
				// Its sprite is drawn in the level, see DrawSceneEvents(), only its mark here, the masks show a symbol instead
				if (!maskGlyphs || !HasMaskGlyph(EventType(event.Record.Type))) {
					previews.DrawPlaced(painter, event.Record, rect, view.Scale, (selected && moving ? eventAlpha * 0.35f : eventAlpha), animTime, Vector2f(cell.X * 32.0f + 16.0f, cell.Y * 32.0f + 16.0f), true);
				}

				// Badges of what isn't seen otherwise
				if (tileSize >= 16.0f) {
					if (difficulties != 0 && difficulties != Formats::EventRecord::DifficultyMask) {
						static const std::uint8_t Bits[] = { Formats::EventRecord::Easy, Formats::EventRecord::Normal, Formats::EventRecord::Hard };
						for (std::int32_t d = 0; d < 3; d++) {
							Colorf color = ((difficulties & Bits[d]) != 0 ? Colorf(0.95f, 0.95f, 0.95f, alpha) : Colorf(0.15f, 0.12f, 0.2f, alpha));
							painter.FillRect(Rectf(rect.X + rect.W - badge * float(3 - d) - badge * 0.5f * float(2 - d), rect.Y + rect.H - badge, badge, badge), color, UiLayer::Overlay);
						}
					}
					if ((flags & Formats::EventRecord::Generator) != 0) {
						painter.FillRect(Rectf(rect.X + rect.W - badge, rect.Y, badge, badge), Colorf(0.35f, 0.85f, 1.0f, alpha), UiLayer::Overlay);
					}
					if ((flags & Formats::EventRecord::MultiplayerOnly) != 0) {
						painter.FillRect(Rectf(rect.X, rect.Y + rect.H - badge, badge, badge), Colorf(0.85f, 0.4f, 1.0f, alpha), UiLayer::Overlay);
					}
				}
				// Which way it pushes or rolls, an arrow for each group of values, longer for a stronger one; of the selected
				// event even zoomed far out, its handles are at the tips at any zoom
				if (tileSize >= 16.0f || selected) {
					EventDirection directions[4];
					std::int32_t directionCount = EventPreviews::CollectDirections(event.Record, directions);
					// From the position of the event, not the middle of its tile, the sprite layer is shown a pixel higher than
					// the level, to the tip the selected event is dragged by (see EventGizmos::Collect()), both in whole pixels
					// as the handle is drawn, see EventTool::DrawOverlay()
					Vector2f center(cell.X * 32.0f + 16.0f, cell.Y * 32.0f + 16.0f);
					Vector2f tail = view.WorldToScreen(center);
					tail = Vector2f(std::round(tail.X), std::round(tail.Y));
					for (std::int32_t d = 0; d < directionCount; d++) {
						Vector2f tip = view.WorldToScreen(EventPreviews::GetDirectionTip(center, directions[d].Vector));
						tip = Vector2f(std::round(tip.X), std::round(tip.Y));
						float length = (tip - tail).Length();
						if (length < 1.0f) {
							continue;
						}
						Vector2f unit = (tip - tail) / length;
						Colorf arrowColor = UiTheme::OverlaySeries[directions[d].Group % 4];
						arrowColor.A = 0.9f * alpha;
						float arrowThickness = std::max(1.0f, std::round(painter.GetScale() * 1.5f));
						painter.Line(tail, tip, arrowThickness, arrowColor, UiLayer::Overlay);
						Vector2f side(-unit.Y, unit.X);
						float head = std::max(4.0f, length * 0.3f);
						painter.Line(tip, tip - unit * head + side * head * 0.5f, arrowThickness, arrowColor, UiLayer::Overlay);
						painter.Line(tip, tip - unit * head - side * head * 0.5f, arrowThickness, arrowColor, UiLayer::Overlay);
					}
				}
				if (selected) {
					painter.Frame(rect, UiTheme::Accent, thickness, UiLayer::Overlay);
				}
			}
		}

		if (!inTransition) {
			DrawEventLinks(painter, view, alpha);
		}
		if (editingEvents) {
			DrawSelectedEventDetails(painter, view);
		}

		// Events off the grid, at their own position, those being moved faded as those on the grid
		for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
			const Formats::OffGridEventRecord& event = document.OffGridEvents[i];
			Vector2f pos = view.WorldToScreen(Vector2f(float(event.X), float(event.Y)));
			float half = tileSize * 0.5f;
			Rectf rect(pos.X - half, pos.Y - half, tileSize, tileSize);
			if (rect.X > area.X + area.W || rect.Y > area.Y + area.H || rect.X + rect.W < area.X || rect.Y + rect.H < area.Y || reachesHole(rect)) {
				continue;
			}
			float eventAlpha = (moving && selection.ContainsOffGrid(std::int32_t(i)) ? alpha * 0.35f : alpha);
			// Drawn as those on the grid, told apart by the mark in the corner
			previews.DrawPlaced(painter, event.Event, rect, view.Scale, eventAlpha, animTime, Vector2f(float(event.X), float(event.Y)), true, true);
		}
	}

	void EditorShell::DrawSelectedEventDetails(UiPainter& painter, const LayerView& view)
	{
		LevelDocument& document = _session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		Colorf detailColor(1.0f, 0.95f, 0.55f, 0.85f);
		if (gridSize.X <= 0) {
			return;
		}
		// Only around the view and not too many, all events of a large level can be selected
		constexpr std::int32_t MaxDetails = 256;
		float reach = painter.Px(200.0f);
		Rectf nearView(_viewRect.X - reach, _viewRect.Y - reach, _viewRect.W + reach * 2.0f, _viewRect.H + reach * 2.0f);
		std::int32_t drawn = 0;

		for (std::uint32_t cell : _session.GetEventSelection().GetCells()) {
			if (drawn >= MaxDetails) {
				break;
			}
			Vector2i pos(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
			const Formats::EventRecord& event = document.GetEvent(pos.X, pos.Y);
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
			if (descriptor == nullptr) {
				continue;
			}
			Vector2f center = view.WorldToScreen(Vector2f(pos.X * 32.0f + 16.0f, pos.Y * 32.0f + 16.0f));
			bool isNearView = nearView.Contains(center);

			// How a light lights up its surroundings, as the game's shader falls off: fully lit inside, rings where half and a
			// tenth of its light is left, and where it ends
			EventLightShape light;
			bool isLight = EventPreviews::GetLight(event, light) && light.RadiusFar > 0.0f;
			if (isLight) {
				// A pulsing light grows and shrinks as the game pulses it, both radii by the same; with motion reduced, the rings
				// stay in the middle of the pulse. It's seen as far as it reaches at its largest, so the rings don't come and go
				// at the edge of the view.
				bool pulsing = (!UiTheme::ReducedMotion && light.Pulse > 0.0f && light.PulseSpeed > 0.0f);
				float pulse = (pulsing ? EventPreviews::GetLightPulse(light, _session.GetAnimTime()) : 0.0f);
				float largest = (light.RadiusFar + (pulsing ? light.Pulse : 0.0f)) * view.Scale;
				if (largest >= 2.0f && center.X + largest >= _viewRect.X && center.X - largest <= _viewRect.X + _viewRect.W &&
					center.Y + largest >= _viewRect.Y && center.Y - largest <= _viewRect.Y + _viewRect.H) {
					drawn++;
					auto drawRing = [&painter, center](float radius, float lineThickness, const Colorf& color) {
						// A pulsing light can shrink so far that nothing is left of a ring
						if (radius <= 0.0f) {
							return;
						}
						constexpr std::int32_t Segments = 48;
						Vector2f last = center + Vector2f(radius, 0.0f);
						for (std::int32_t s = 1; s <= Segments; s++) {
							float angle = 6.2831853f * float(s) / float(Segments);
							Vector2f next = center + Vector2f(std::cos(angle) * radius, std::sin(angle) * radius);
							painter.Line(last, next, lineThickness, color, UiLayer::Overlay);
							last = next;
						}
					};
					// The distance where the light is left at a strength, found by halving the range
					auto distanceAt = [&light](float strength) {
						float low = light.RadiusNear, high = light.RadiusFar;
						for (std::int32_t i = 0; i < 16; i++) {
							float middle = (low + high) * 0.5f;
							if (EventPreviews::GetLightStrength(middle, light.RadiusNear, light.RadiusFar) > strength) {
								low = middle;
							} else {
								high = middle;
							}
						}
						return (low + high) * 0.5f;
					};
					Colorf lightColor(1.0f, 0.95f, 0.55f, 0.9f);
					if (pulsing) {
						// The middle of the pulse stays, the handle of the near radius is on it
						drawRing(light.RadiusNear * view.Scale, std::max(1.0f, thickness * 0.5f), Colorf(lightColor.R, lightColor.G, lightColor.B, 0.25f));
					}
					light.RadiusNear += pulse;
					light.RadiusFar += pulse;
					float far = light.RadiusFar * view.Scale;
					if (far >= 2.0f) {
						drawRing(light.RadiusNear * view.Scale, thickness, lightColor);
						drawRing(distanceAt(0.5f) * view.Scale, thickness, Colorf(lightColor.R, lightColor.G, lightColor.B, 0.6f));
						drawRing(distanceAt(0.1f) * view.Scale, thickness, Colorf(lightColor.R, lightColor.G, lightColor.B, 0.35f));
						drawRing(far, std::max(1.0f, thickness * 0.5f), Colorf(lightColor.R, lightColor.G, lightColor.B, 0.2f));
					}
				}
			}

			// The texts the event refers to, drawn above it once all of them are known
			struct TextBubble { const EventParamField* Field; String Text; };
			SmallVector<TextBubble, 2> bubbles;

			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				const EventParamField& field = descriptor->Fields[i];
				if (field.Unit == ParamUnit::Pixels && StringView(field.Name).contains("Radius"_s) && !isLight) {
					// How far a light reaches
					float radius = float(EventCatalog::ReadField(event, field)) * view.Scale;
					if (radius < 2.0f || center.X + radius < _viewRect.X || center.X - radius > _viewRect.X + _viewRect.W ||
						center.Y + radius < _viewRect.Y || center.Y - radius > _viewRect.Y + _viewRect.H) {
						continue;
					}
					drawn++;
					constexpr std::int32_t Segments = 48;
					Vector2f last = center + Vector2f(radius, 0.0f);
					for (std::int32_t s = 1; s <= Segments; s++) {
						float angle = 6.2831853f * float(s) / float(Segments);
						Vector2f next = center + Vector2f(std::cos(angle) * radius, std::sin(angle) * radius);
						painter.Line(last, next, thickness, detailColor, UiLayer::Overlay);
						last = next;
					}
				} else if (field.Type == ParamType::TextId && isNearView) {
					// The text the event refers to, above it, or the level an exit without one goes to
					String text;
					if (!EventPreviews::DescribeDefaultLevel(document, event, field, text)) {
						std::int32_t index = EventCatalog::ReadField(event, field);
						if (index < 0 || index >= std::int32_t(document.Texts.size()) || document.Texts[index].empty()) {
							continue;
						}
						text = EventCatalog::MakeTextPreview(document.Texts[index], 48);
					}
					if (text.empty()) {
						continue;
					}
					drawn++;
					bubbles.push_back({ &field, Death::move(text) });
				}
			}

			// As high as a row of the interface, the text is centered in it as in a label, well above the event, so its mark and
			// what's around it can still be seen. A boss has a text when it shows up and one when it's beaten, they're stacked
			// in the order of the fields from the top, and each says which one it is.
			if (!bubbles.empty()) {
				float padding = painter.Px(UiTheme::Padding * 2.0f);
				float height = painter.Px(UiTheme::RowHeight) + painter.Px(6.0f);
				float gap = painter.Px(4.0f);
				float bottom = center.Y - 16.0f * view.Scale - painter.Px(14.0f);
				std::int32_t previousSlot = painter.GetSlot();
				painter.SetSlot(BubbleSlot);
				for (std::size_t i = bubbles.size(); i-- > 0; ) {
					// TRANSLATORS: In front of a text of an event shown in a bubble over the level, {} is the name of the parameter that holds the text
					String label = (bubbles.size() > 1 ? _fx("editor", "{}: ", _x("editor", bubbles[i].Field->Name)) : String());
					float labelWidth = (label.empty() ? 0.0f : painter.MeasureText(label));
					float width = labelWidth + painter.MeasureText(bubbles[i].Text) + padding * 2.0f;
					Rectf bubble(std::round(center.X - width * 0.5f), std::round(bottom - height), width, height);
					bottom = bubble.Y - gap;
					painter.Panel(bubble, false);
					if (!label.empty()) {
						painter.TextInRect(label, Rectf(bubble.X + padding, bubble.Y, labelWidth, bubble.H), UiTheme::TextDim, Alignment::Left, UiLayer::OverlayText);
					}
					painter.TextInRect(bubbles[i].Text, Rectf(bubble.X + padding + labelWidth, bubble.Y, bubble.W - padding * 2.0f - labelWidth, bubble.H), UiTheme::Text, Alignment::Left, UiLayer::OverlayText);
				}
				painter.SetSlot(previousSlot);
			}

			if (EventType(event.Type) == EventType::ModifierSetWater && descriptor->FieldCount > 0 && isNearView) {
				// The new water level across the view
				drawn++;
				float y = std::round(view.WorldToScreen(Vector2f(0.0f, float(EventCatalog::ReadField(event, descriptor->Fields[0])))).Y);
				painter.FillRect(Rectf(_viewRect.X, y, _viewRect.W, std::max(thickness, 2.0f)), Colorf(0.3f, 0.6f, 1.0f, 0.8f), UiLayer::Overlay);
				painter.Line(center, Vector2f(center.X, y), thickness, Colorf(0.3f, 0.6f, 1.0f, 0.6f), UiLayer::Overlay);
			} else if (EventType(event.Type) == EventType::ModifierLimitCameraView && descriptor->FieldCount > 1 && isNearView) {
				// Where the camera is kept, darker outside of it, see Player::HandleAreaEventAt()
				drawn++;
				std::int32_t leftTiles = EventCatalog::ReadField(event, descriptor->Fields[0]);
				std::int32_t widthTiles = EventCatalog::ReadField(event, descriptor->Fields[1]);
				float left = (leftTiles == 0 ? pos.X : leftTiles) * 32.0f;
				float right = (widthTiles == 0 ? gridSize.X * 32.0f : left + widthTiles * 32.0f);
				// A span narrower than the view of the game is widened to the left, see LevelHandler::LimitCameraView()
				float effectiveLeft = left;
				if (EditorLevelHandler* editHandler = _session.GetEditHandler()) {
					float viewWidth = float(editHandler->GetReferenceViewSize().X);
					if (right - left < viewWidth) {
						effectiveLeft = std::max(right - viewWidth, 0.0f);
					}
				}
				float screenLeft = std::round(view.WorldToScreen(Vector2f(left, 0.0f)).X);
				float screenEffectiveLeft = std::round(view.WorldToScreen(Vector2f(effectiveLeft, 0.0f)).X);
				float screenRight = std::round(view.WorldToScreen(Vector2f(right, 0.0f)).X);
				Colorf shade(0.0f, 0.0f, 0.0f, 0.35f);
				Colorf edge(1.0f, 0.55f, 0.85f, 0.9f);
				if (screenEffectiveLeft > _viewRect.X) {
					painter.FillRect(Rectf(_viewRect.X, _viewRect.Y, std::min(screenEffectiveLeft, _viewRect.X + _viewRect.W) - _viewRect.X, _viewRect.H), shade, UiLayer::Fill);
				}
				if (screenEffectiveLeft < screenLeft) {
					// Where the camera can go too, as the view is wider than the span, lighter and with a thinner edge
					float from = std::max(screenEffectiveLeft, _viewRect.X);
					float to = std::min(screenLeft, _viewRect.X + _viewRect.W);
					if (to > from) {
						painter.FillRect(Rectf(from, _viewRect.Y, to - from, _viewRect.H), Colorf(edge.R, edge.G, edge.B, 0.08f), UiLayer::Fill);
					}
					painter.FillRect(Rectf(screenEffectiveLeft - thickness * 0.5f, _viewRect.Y, thickness, _viewRect.H), Colorf(edge.R, edge.G, edge.B, 0.55f), UiLayer::Overlay);
				}
				if (screenRight < _viewRect.X + _viewRect.W) {
					float from = std::max(screenRight, _viewRect.X);
					painter.FillRect(Rectf(from, _viewRect.Y, _viewRect.X + _viewRect.W - from, _viewRect.H), shade, UiLayer::Fill);
				}
				float lineWidth = std::max(thickness, 2.0f);
				painter.FillRect(Rectf(screenLeft - lineWidth * 0.5f, _viewRect.Y, lineWidth, _viewRect.H), edge, UiLayer::Overlay);
				painter.FillRect(Rectf(screenRight - lineWidth * 0.5f, _viewRect.Y, lineWidth, _viewRect.H), edge, UiLayer::Overlay);
				painter.Line(Vector2f(screenLeft, center.Y), Vector2f(screenRight, center.Y), thickness, Colorf(edge.R, edge.G, edge.B, 0.5f), UiLayer::Overlay);
			}
		}
	}

	void EditorShell::UpdateEventLinks()
	{
		LevelDocument& document = _session.GetDocument();
		// Events painted while a stroke goes on aren't told about until it ends, their number changes meanwhile
		if (!_linksDirty && _linksCatalogRevision == EventCatalog::GetRevision() && _linksEventCount == document.Events.size()) {
			return;
		}
		_linksDirty = false;
		_linksCatalogRevision = EventCatalog::GetRevision();
		_linksEventCount = document.Events.size();
		_warpGroups.clear();
		_warpTargets.clear();
		_triggerGroups.clear();
		_triggerEvents.clear();
		_raceLinks.clear();

		Vector2i gridSize = document.GetEventGridSize();
		if (gridSize.X <= 0) {
			return;
		}
		SmallVector<LinkEvent, 0> origins;
		for (const DocumentEvent& event : document.Events) {
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Record.Type));
			if (descriptor == nullptr) {
				continue;
			}
			if (const EventParamField* field = FindEventField(descriptor, ParamType::WarpId)) {
				LinkEvent link{ EventCatalog::ReadField(event.Record, *field), event.Cell, -1 };
				if (EventType(event.Record.Type) == EventType::WarpTarget) {
					_warpTargets.push_back(link);
				} else {
					origins.push_back(link);
				}
			}
			if (const EventParamField* field = FindEventField(descriptor, ParamType::TriggerId)) {
				_triggerEvents.push_back(LinkEvent{ EventCatalog::ReadField(event.Record, *field), event.Cell, -1 });
			}
		}
		// The events are sorted by the tile, so these are too
		GroupNeighbors(origins, gridSize.X, _warpGroups);
		GroupNeighbors(_triggerEvents, gridSize.X, _triggerGroups);
		auto byId = [](const LinkEvent& a, const LinkEvent& b) { return (a.Id < b.Id); };
		std::stable_sort(_warpTargets.begin(), _warpTargets.end(), byId);
		std::stable_sort(_triggerEvents.begin(), _triggerEvents.end(), byId);
		EventPreviews::CollectRaceTrack(document, _raceLinks);
	}

	void EditorShell::DrawEventLinks(UiPainter& painter, const LayerView& view, float alpha)
	{
		LevelDocument& document = _session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		if (gridSize.X <= 0) {
			return;
		}
		UpdateEventLinks();
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 1.5f));
		float tileSize = 32.0f * view.Scale;

		auto cellsRect = [&view](const Recti& cells) {
			Vector2f topLeft = view.WorldToScreen(Vector2f(cells.X * 32.0f, cells.Y * 32.0f));
			return Rectf(topLeft.X, topLeft.Y, cells.W * 32.0f * view.Scale, cells.H * 32.0f * view.Scale);
		};
		// Lines that can't cross the view aren't drawn
		Rectf viewRect = _viewRect;
		auto crossesView = [viewRect](const Rectf& a, const Rectf& b) {
			float left = std::min(a.X, b.X), right = std::max(a.X + a.W, b.X + b.W);
			float top = std::min(a.Y, b.Y), bottom = std::max(a.Y + a.H, b.Y + b.H);
			return (right >= viewRect.X && left <= viewRect.X + viewRect.W && bottom >= viewRect.Y && top <= viewRect.Y + viewRect.H);
		};
		auto findId = [](const SmallVectorImpl<LinkEvent>& events, std::int32_t id) {
			return std::equal_range(events.begin(), events.end(), LinkEvent{ id, 0, -1 }, [](const LinkEvent& a, const LinkEvent& b) { return (a.Id < b.Id); });
		};
		auto cellRect = [&view, gridSize](std::uint32_t cell) {
			Vector2f world(float(cell % std::uint32_t(gridSize.X)) * 32.0f, float(cell / std::uint32_t(gridSize.X)) * 32.0f);
			Vector2f topLeft = view.WorldToScreen(world);
			return Rectf(topLeft.X, topLeft.Y, 32.0f * view.Scale, 32.0f * view.Scale);
		};
		auto levelRect = [&view](const Rectf& rect) {
			Vector2f topLeft = view.WorldToScreen(Vector2f(rect.X, rect.Y));
			return Rectf(topLeft.X, topLeft.Y, rect.W * view.Scale, rect.H * view.Scale);
		};
		// How far from the middle of a rectangle a line in a direction leaves it
		auto exitDistance = [](const Rectf& rect, Vector2f direction) {
			float x = (std::abs(direction.X) > 0.0001f ? rect.W * 0.5f / std::abs(direction.X) : FLT_MAX);
			float y = (std::abs(direction.Y) > 0.0001f ? rect.H * 0.5f / std::abs(direction.Y) : FLT_MAX);
			return std::min(x, y);
		};
		// From the outline of the mark of one event to the outline of the other, not from the middle to the middle
		auto drawLink = [&painter, &exitDistance](const Rectf& fromRect, const Rectf& toRect, float lineThickness, const Colorf& color, bool withHead) {
			Vector2f from(fromRect.X + fromRect.W * 0.5f, fromRect.Y + fromRect.H * 0.5f);
			Vector2f to(toRect.X + toRect.W * 0.5f, toRect.Y + toRect.H * 0.5f);
			Vector2f delta = to - from;
			float length = delta.Length();
			if (length < 1.0f) {
				return;
			}
			Vector2f direction = delta / length;
			float start = exitDistance(fromRect, direction);
			float end = length - exitDistance(toRect, direction);
			if (end - start < 2.0f) {
				// The marks touch or overlap, there's nothing between them to draw
				return;
			}
			Vector2f tail = from + direction * start;
			Vector2f tip = from + direction * end;
			Vector2f shaft[2] = { tail, tip };
			painter.Polyline(shaft, lineThickness, color, UiLayer::Overlay);
			if (withHead) {
				float head = std::min(std::max(lineThickness * 4.0f, 6.0f), (end - start) * 0.5f);
				Vector2f side(-direction.Y, direction.X);
				Vector2f arrowHead[3] = { tip - direction * head + side * head * 0.5f, tip, tip - direction * head - side * head * 0.5f };
				painter.Polyline(arrowHead, lineThickness, color, UiLayer::Overlay);
			}
		};

		// Warps lead to all targets of the same identifier, tiles of a warp next to each other are taken together
		Colorf linkColor(0.45f, 0.65f, 1.0f, 0.85f * alpha);
		for (const LinkGroup& group : _warpGroups) {
			Rectf fromRect = cellsRect(group.Cells);
			auto targets = findId(_warpTargets, group.Id);
			if (targets.first == targets.second) {
				// A warp to nowhere
				float size = std::max(tileSize * 0.3f, 6.0f);
				Vector2f from(fromRect.X + fromRect.W * 0.5f, fromRect.Y + fromRect.H * 0.5f);
				if (crossesView(fromRect, Rectf(from.X - size, from.Y - size, size * 2.0f, size * 2.0f))) {
					painter.Line(from - Vector2f(size, size), from + Vector2f(size, size), thickness * 1.5f, UiTheme::OverlayBad, UiLayer::Overlay);
					painter.Line(from - Vector2f(size, -size), from + Vector2f(size, -size), thickness * 1.5f, UiTheme::OverlayBad, UiLayer::Overlay);
				}
				continue;
			}
			for (auto it = targets.first; it != targets.second; ++it) {
				Rectf toRect = cellRect(it->Cell);
				if (crossesView(fromRect, toRect)) {
					drawLink(fromRect, toRect, thickness, linkColor, true);
				}
			}
		}

		// The track of a race goes from waypoint to waypoint and on to what ends a lap, shown always as the warps are
		Colorf raceColor(0.7f, 0.95f, 0.3f, 0.85f * alpha);
		for (const EventLink& link : _raceLinks) {
			Rectf fromRect = levelRect(link.From), toRect = levelRect(link.To);
			if (crossesView(fromRect, toRect)) {
				drawLink(fromRect, toRect, thickness, raceColor, true);
			}
		}

		// Other links of the selected events, as a rolling rock and its trigger or links of events described by event packs,
		// found again only when the selection or the events change, see EventPreviews::CollectLinks()
		const EventSelection& selection = _session.GetEventSelection();
		if (!_session.IsEditingEvents() || !selection.HasCells()) {
			return;
		}
		{
			std::uint32_t key = 2166136261u ^ _eventsSerial;
			for (std::uint32_t cell : selection.GetCells()) {
				key = (key ^ cell) * 16777619u;
			}
			if (key != _selectedLinksKey) {
				_selectedLinksKey = key;
				SmallVector<EventLink, 0> links;
				EventPreviews::CollectLinks(document, links, selection.GetCells(), 1024);
				_selectedLinks.clear();
				for (const EventLink& link : links) {
					// Warps are shown always, triggers below with tiles of the same trigger taken together
					StringView channel = (link.Channel != nullptr ? StringView(link.Channel) : StringView());
					if (!link.Missing && channel != "warp"_s && channel != "trigger"_s) {
						_selectedLinks.push_back(link);
					}
				}
			}
			Colorf otherColor = UiTheme::OverlayInfo;
			otherColor.A *= 0.9f * alpha;
			float frameThickness = std::max(1.0f, std::round(painter.GetScale()));
			for (const EventLink& link : _selectedLinks) {
				Rectf fromRect = levelRect(link.From), toRect = levelRect(link.To);
				if (crossesView(fromRect, toRect)) {
					drawLink(fromRect, toRect, std::max(1.0f, thickness * 0.67f), otherColor, link.Directed);
					painter.Frame(toRect, otherColor, frameThickness, UiLayer::Overlay);
				}
			}
		}

		// Selected events show which events share their trigger, tiles next to each other are taken together here too
		if (_triggerEvents.empty()) {
			return;
		}
		SmallVector<bool, 0> selectedGroups;
		selectedGroups.resize(_triggerGroups.size(), false);
		SmallVector<std::int32_t, 8> selectedIds;
		for (const LinkEvent& event : _triggerEvents) {
			if (event.Group >= 0 && !selectedGroups[event.Group] && selection.Contains(event.Cell)) {
				selectedGroups[event.Group] = true;
				if (std::find(selectedIds.begin(), selectedIds.end(), event.Id) == selectedIds.end()) {
					selectedIds.push_back(event.Id);
				}
			}
		}

		// Not too many lines, a large selection could link everything to everything
		constexpr std::int32_t MaxLinkChecks = 4096;
		std::int32_t linkChecks = 0;
		Colorf triggerColor(1.0f, 0.62f, 0.25f, 0.9f * alpha);
		float frameThickness = std::max(1.0f, std::round(painter.GetScale()));
		SmallVector<std::int32_t, 16> fromGroups;
		SmallVector<std::int32_t, 16> toGroups;
		SmallVector<std::int32_t, 0> groupSeen;
		groupSeen.resize(_triggerGroups.size(), -1);
		for (std::int32_t k = 0; k < std::int32_t(selectedIds.size()); k++) {
			auto members = findId(_triggerEvents, selectedIds[k]);
			fromGroups.clear();
			toGroups.clear();
			for (auto it = members.first; it != members.second; ++it) {
				if (groupSeen[it->Group] != k) {
					groupSeen[it->Group] = k;
					(selectedGroups[it->Group] ? fromGroups : toGroups).push_back(it->Group);
				}
				// The other events are framed
				if (!selectedGroups[it->Group]) {
					Rectf rect = cellRect(it->Cell);
					if (crossesView(rect, rect)) {
						painter.Frame(rect, triggerColor, frameThickness, UiLayer::Overlay);
					}
				}
			}
			for (std::int32_t from : fromGroups) {
				Rectf fromRect = cellsRect(_triggerGroups[from].Cells);
				for (std::int32_t to : toGroups) {
					if (++linkChecks > MaxLinkChecks) {
						return;
					}
					Rectf toRect = cellsRect(_triggerGroups[to].Cells);
					if (crossesView(fromRect, toRect)) {
						drawLink(fromRect, toRect, std::max(1.0f, thickness * 0.67f), triggerColor, false);
					}
				}
			}
		}
	}

	void EditorShell::DrawLayerBounds(UiPainter& painter, const LayerView& view)
	{
		if (view.Size.X <= 0 || view.Size.Y <= 0) {
			return;
		}

		// Outside of a layer that doesn't repeat is darkened, so its edges can be seen
		Rectf topLeft = view.CellToScreen(Vector2i(0, 0));
		Rectf bottomRight = view.CellToScreen(Vector2i(view.Size.X - 1, view.Size.Y - 1));
		float left = (view.RepeatX ? _viewRect.X : std::max(topLeft.X, _viewRect.X));
		float right = (view.RepeatX ? _viewRect.X + _viewRect.W : std::min(bottomRight.X + bottomRight.W, _viewRect.X + _viewRect.W));
		float top = (view.RepeatY ? _viewRect.Y : std::max(topLeft.Y, _viewRect.Y));
		float bottom = (view.RepeatY ? _viewRect.Y + _viewRect.H : std::min(bottomRight.Y + bottomRight.H, _viewRect.Y + _viewRect.H));
		Colorf shade(0.02f, 0.0f, 0.06f, 0.68f);

		if (right <= left || bottom <= top) {
			painter.FillRect(_viewRect, shade, UiLayer::Back);
			return;
		}
		painter.FillRect(Rectf(_viewRect.X, _viewRect.Y, _viewRect.W, top - _viewRect.Y), shade, UiLayer::Back);
		painter.FillRect(Rectf(_viewRect.X, bottom, _viewRect.W, _viewRect.Y + _viewRect.H - bottom), shade, UiLayer::Back);
		painter.FillRect(Rectf(_viewRect.X, top, left - _viewRect.X, bottom - top), shade, UiLayer::Back);
		painter.FillRect(Rectf(right, top, _viewRect.X + _viewRect.W - right, bottom - top), shade, UiLayer::Back);

		if (view.RepeatX || view.RepeatY) {
			DrawRepeatedLayerBounds(painter, view, topLeft, bottomRight);
		}

		// The frame of the layer, the edges it's resized by, of the copy seen whole nearest the middle if it repeats; dark
		// under light, so it's seen on bright and dark layers alike
		Rectf copy = GetLayerFrame(view);
		float width = std::max(1.0f, std::round(painter.GetScale()));
		Rectf frame(std::round(copy.X) - width, std::round(copy.Y) - width, std::round(copy.W) + width * 2.0f, std::round(copy.H) + width * 2.0f);
		painter.Frame(Rectf(frame.X - width, frame.Y - width, frame.W + width * 2.0f, frame.H + width * 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.45f), width, UiLayer::Overlay);
		painter.Frame(frame, Colorf(1.0f, 1.0f, 1.0f, 0.85f), width, UiLayer::Overlay);
	}

	Rectf EditorShell::GetLayerFrame(const LayerView& view) const
	{
		Rectf topLeft = view.CellToScreen(Vector2i(0, 0));
		Rectf bottomRight = view.CellToScreen(Vector2i(view.Size.X - 1, view.Size.Y - 1));
		Rectf layer(topLeft.X, topLeft.Y, bottomRight.X + bottomRight.W - topLeft.X, bottomRight.Y + bottomRight.H - topLeft.Y);
		if (layer.W <= 0.0f || layer.H <= 0.0f) {
			return layer;
		}

		// The copy that is framed is one seen whole, the nearest to the middle of the view - or the one in the middle if
		// none fits - as the layer itself (where its first tile is) can be far off the screen when it repeats. The copies
		// are the same, and the axes don't depend on each other, so this is also the copy seen the most.
		auto pickCopy = [](float start, float size, float viewStart, float viewSize) {
			float viewCenter = viewStart + viewSize * 0.5f;
			if (size <= viewSize) {
				float first = start + std::ceil((viewStart - start) / size) * size;
				float last = start + std::floor((viewStart + viewSize - size - start) / size) * size;
				if (first <= last) {
					float nearest = start + std::round((viewCenter - size * 0.5f - start) / size) * size;
					return std::clamp(nearest, first, last);
				}
			}
			return start + std::floor((viewCenter - start) / size) * size;
		};
		Rectf copy = layer;
		if (view.RepeatX) {
			copy.X = pickCopy(layer.X, layer.W, _viewRect.X, _viewRect.W);
		}
		if (view.RepeatY) {
			copy.Y = pickCopy(layer.Y, layer.H, _viewRect.Y, _viewRect.H);
		}
		return copy;
	}

	void EditorShell::DrawRepeatedLayerBounds(UiPainter& painter, const LayerView& view, const Rectf& topLeft, const Rectf& bottomRight)
	{
		// The layer itself, where its tiles start and end, and fainter lines where it starts again, so a small layer
		// that is seen over and over can be told from its copies
		Rectf layer(topLeft.X, topLeft.Y, bottomRight.X + bottomRight.W - topLeft.X, bottomRight.Y + bottomRight.H - topLeft.Y);
		float width = std::max(1.0f, std::round(painter.GetScale()));
		Colorf seam(1.0f, 1.0f, 1.0f, 0.32f);
		// Not when the copies are so small on the screen that the lines would be all that is seen
		float minCopy = painter.GetScale() * 24.0f;

		if (view.RepeatX && layer.W >= minCopy) {
			float first = layer.X + std::floor((_viewRect.X - layer.X) / layer.W) * layer.W;
			for (float x = first; x <= _viewRect.X + _viewRect.W; x += layer.W) {
				float lineTop = (view.RepeatY ? _viewRect.Y : std::max(layer.Y, _viewRect.Y));
				float lineBottom = (view.RepeatY ? _viewRect.Y + _viewRect.H : std::min(layer.Y + layer.H, _viewRect.Y + _viewRect.H));
				if (lineBottom > lineTop) {
					painter.FillRect(Rectf(std::round(x), lineTop, width, lineBottom - lineTop), seam, UiLayer::Overlay);
				}
			}
		}
		if (view.RepeatY && layer.H >= minCopy) {
			float first = layer.Y + std::floor((_viewRect.Y - layer.Y) / layer.H) * layer.H;
			for (float y = first; y <= _viewRect.Y + _viewRect.H; y += layer.H) {
				float lineLeft = (view.RepeatX ? _viewRect.X : std::max(layer.X, _viewRect.X));
				float lineRight = (view.RepeatX ? _viewRect.X + _viewRect.W : std::min(layer.X + layer.W, _viewRect.X + _viewRect.W));
				if (lineRight > lineLeft) {
					painter.FillRect(Rectf(lineLeft, std::round(y), lineRight - lineLeft, width), seam, UiLayer::Overlay);
				}
			}
		}
	}

	void EditorShell::DrawScreenFrame(UiPainter& painter)
	{
		EditorLevelHandler* editHandler = _session.GetEditHandler();
		if (editHandler == nullptr) {
			return;
		}
		// Around the pointer over the level, or the middle of the view
		Vector2f center = editHandler->GetCamera().GetCenter();
		ToolPointer hover;
		if (_session.GetHoverPointer(hover) && _viewRect.Contains(hover.Screen)) {
			center = hover.World;
		}

		// The view of the game, the others as high as it is, a half of a split screen for two players
		Vector2i reference = editHandler->GetReferenceViewSize();
		Vector2f size(float(reference.X), float(reference.Y));
		String label;
		switch (_session.GetSettings().ScreenFrameKind) {
			default:
				// TRANSLATORS: Label of the frame of the screen drawn over the level, the view of the game, with its width and height in pixels
				label = _fx("editor", "Game view, {} x {}", reference.X, reference.Y);
				break;
			case 1:
				size.X = std::round(size.Y * 4.0f / 3.0f);
				// TRANSLATORS: Label of the frame of the screen drawn over the level, an aspect ratio, with the width and height in pixels
				label = _fx("editor", "4:3, {} x {}", std::int32_t(size.X), std::int32_t(size.Y));
				break;
			case 2:
				size.X = std::round(size.Y * 16.0f / 9.0f);
				// TRANSLATORS: Label of the frame of the screen drawn over the level, an aspect ratio, with the width and height in pixels
				label = _fx("editor", "16:9, {} x {}", std::int32_t(size.X), std::int32_t(size.Y));
				break;
			case 3:
				size.Y = std::round(size.Y * 0.5f);
				// TRANSLATORS: Label of the frame of the screen drawn over the level, a half of a split screen for two players, with its width and height in pixels
				label = _fx("editor", "Split screen, {} x {}", std::int32_t(size.X), std::int32_t(size.Y));
				break;
		}
		if (size.X <= 0.0f || size.Y <= 0.0f) {
			return;
		}

		std::int32_t spriteLayer = _session.GetDocument().GetSpriteLayerIndex();
		LayerView view = _session.GetLayerView(spriteLayer >= 0 ? spriteLayer : 0);
		Vector2f topLeft = view.WorldToScreen(center - size * 0.5f);
		Vector2f bottomRight = view.WorldToScreen(center + size * 0.5f);
		Rectf frame(std::round(topLeft.X), std::round(topLeft.Y), std::round(bottomRight.X - topLeft.X), std::round(bottomRight.Y - topLeft.Y));
		float width = std::max(1.0f, std::round(painter.GetScale()));
		// Dark under light, so it's seen on bright and dark tiles alike
		painter.Frame(Rectf(frame.X - width, frame.Y - width, frame.W + width * 2.0f, frame.H + width * 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.5f), width, UiLayer::Overlay);
		painter.Frame(frame, Colorf(1.0f, 1.0f, 1.0f, 0.8f), width, UiLayer::Overlay);
		float labelHeight = painter.Px(UiTheme::RowHeight);
		float labelWidth = painter.MeasureText(label) + painter.Px(UiTheme::Padding * 2.0f);
		Rectf labelRect(frame.X, frame.Y - labelHeight, labelWidth, labelHeight);
		painter.FillRect(labelRect, Colorf(0.0f, 0.0f, 0.0f, 0.55f), UiLayer::Overlay);
		painter.TextInRect(label, Rectf(labelRect.X + painter.Px(UiTheme::Padding), labelRect.Y, labelRect.W, labelRect.H), UiTheme::Text, Alignment::Left, UiLayer::OverlayText);
	}
}

#endif
