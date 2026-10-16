#include "EditorShell.h"

#if defined(WITH_EDITOR)

#include "EventPanels.h"
#include "ShellPanels.h"
#include "../EditorSession.h"
#include "../Document/Annotations.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/EditorLevelHandler.h"
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

		// Tools and commands only experts are offered, see EditorSettings::Expert
		const char* const ExpertActions[] = {
			"tool.rectangle", "tool.eyedropper", "tool.wand", "tool.path", "tool.measure", "tool.ground",
			"edit.history", "edit.findReplace", "edit.smartRetile", "edit.beautify", "edit.terrainRules", "edit.traceGround", "edit.bakeGround",
			"edit.continueRow", "events.selectSameType", "events.copyToMatchingTiles", "stamp.random", "stamp.reroll", "edit.prefabs",
			"view.flat", "view.pauseLayers", "view.reachability", "view.addNote",
			"layer.resize", "level.snapOffGrid", "level.reloadEvents", "level.animations", "level.tilesets", "level.palette", "level.editTile",
			"level.applyTileChanges", "level.rearrangeTileSet", "level.newTileSet", "level.importTileSet", "level.addTiles", "level.fxTiles",
			"level.exportTileSet", "play.setMarker", "play.clearMarker", "play.marker"
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

		_rightFrame = Add<UiDockFrame>(UiDockSide::Right, _("Drag to move the panel, to the edge of the screen to dock it again"));
		_rightDock = _rightFrame->GetPanel();
		_rightFrame->GetHeaderContent()->Add<UiLabel>(_("Layers"), UiTheme::TextHeader)->Flex = 1.0f;
		_layersPanel = _rightDock->Add<LayersPanel>(session);
		EditorSession* s = &session;
		auto* inspector = _rightDock->Add<InspectorPanel>(session);
		inspector->Flex = 1.0f;
		inspector->IsVisibleFunc = [s]() { return !s->IsEditingEvents(); };
		auto* eventInspector = _rightDock->Add<EventInspectorPanel>(session);
		eventInspector->Flex = 1.0f;
		eventInspector->IsVisibleFunc = [s]() { return s->IsEditingEvents(); };

		// Tiles and events to pick from, next to the tools, where there's more room than below the view
		_pickerFrame = Add<UiDockFrame>(UiDockSide::Left, _("Drag to move the panel, to the edge of the screen to dock it again"));
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
		auto* back = _playBar->Add<UiButton>(_("Back to Editor"), EditorIcon::Stop, [s]() { s->StopPlayTest(true); });
		back->Style = UiButtonStyle::Accent;
		back->Tooltip = _("Back to Editor (F5)");
		auto* restart = _playBar->Add<UiButton>(_("Restart"), EditorIcon::Play, [s]() {
			s->StopPlayTest(false);
			s->StartPlayTest(PlayStartMode::FromStart);
		});
		restart->Style = UiButtonStyle::Flat;
		restart->IconColor = UiTheme::Play;
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
					entries.push_back(UiMenuEntry::FromAction(*action));
				}
			}
			if (!entries.empty() && entries.back().IsSeparator()) {
				entries.pop_back();
			}
		};

		EditorSession* session = &_session;
		_menuBar->AddMenu(_("File"), [addActions, session](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { "file.new", "file.open" });
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
				entries.push_back(std::move(entry));
				shown++;
			}
			addActions(entries, { nullptr, "file.save", "file.saveAs", "file.export", nullptr, "file.exit" });
		});
		_menuBar->AddMenu(_("Edit"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { "edit.undo", "edit.redo", "edit.history", nullptr, "edit.cut", "edit.copy", "edit.paste", "edit.delete", nullptr,
				"edit.selectAll", "edit.deselect", "edit.findReplace", nullptr, "edit.fillGround", "edit.smartRetile", "edit.beautify", "edit.continueRow", "edit.terrainRules", "edit.traceGround", "edit.bakeGround", nullptr, "events.selectSameType", "events.copyToMatchingTiles", nullptr, "stamp.flipX", "stamp.flipY", "stamp.random", "stamp.reroll", "edit.prefabs" });
		});
		_menuBar->AddMenu(_("View"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { "view.grid", "view.flat", "view.pauseLayers", "view.masks", "view.events", "view.playPath", "view.reachability", nullptr, "view.navigator", "view.addNote", nullptr, "view.zoomIn", "view.zoomOut", "view.zoomGame", "view.fit", nullptr, "view.uiLarger", "view.uiSmaller", "view.uiAuto",
				nullptr, "view.layoutAuto", "view.layoutDesktop", "view.layoutCompact", nullptr, "view.expert" });
		});
		_menuBar->AddMenu(_("Tools"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { "tool.brush", "tool.eraser", "tool.rectangle", "tool.fill", "tool.smart", "tool.ground", "tool.eyedropper", nullptr,
				"tool.select", "tool.wand", nullptr, "tool.event", "tool.path", nullptr, "tool.measure", "tool.hand" });
		});
		_menuBar->AddMenu(_("Layer"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { "layer.add", "layer.remove", "layer.resize", nullptr, "layer.up", "layer.down", nullptr, "layer.previous", "layer.next" });
		});
		_menuBar->AddMenu(_("Level"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { "level.properties", "level.texts", "level.statistics", "level.check", "level.snapOffGrid", "level.reloadEvents", nullptr, "level.animations", "level.tilesets", "level.palette", "level.editTile", "level.applyTileChanges", "level.rearrangeTileSet", nullptr, "level.newTileSet", "level.importTileSet", "level.addTiles", "level.fxTiles", "level.exportTileSet" });
		});
		_menuBar->AddMenu(_("Play"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { "play.resume", "play.here", "play.start", "play.marker", nullptr, "play.setMarker", "play.clearMarker", nullptr, "play.jazz", "play.spaz", "play.lori", nullptr,
				"play.easy", "play.normal", "play.hard", nullptr, "view.playPath" });
#if defined(WITH_MULTIPLAYER)
			// Multiplayer in local splitscreen
			addActions(entries, { nullptr, "play.alone", "play.cooperation", "play.battle", "play.teamBattle", "play.race", "play.treasureHunt",
				"play.captureTheFlag", nullptr, "play.players1", "play.players2", "play.players3", "play.players4" });
#endif
		});
		_menuBar->AddMenu(_("Help"), [addActions](SmallVectorImpl<UiMenuEntry>& entries) {
			addActions(entries, { "help.commands", "help.shortcuts" });
		});
	}

	void EditorShell::CreateToolbar()
	{
		_toolbar = Add<UiPanel>(UiPanelStyle::Header, UiOrientation::Horizontal, 1.0f, UiThickness(UiTheme::Padding, 2.0f));
		_toolbar->ClipContent = true;
		// The panels in drawers on a small screen
		for (const char* id : { "view.drawerTiles", "view.drawerLayers" }) {
			if (UiButton* button = AddActionButton(_toolbar, id)) {
				button->IsVisibleFunc = [this]() { return _compact; };
			}
		}
		AddActionButton(_toolbar, "file.new"_s);
		AddActionButton(_toolbar, "file.open"_s);
		AddActionButton(_toolbar, "file.save"_s);
		_toolbar->Add<UiSeparator>(UiOrientation::Vertical);
		AddActionButton(_toolbar, "edit.undo"_s);
		AddActionButton(_toolbar, "edit.redo"_s);
		// A narrow window leaves out what the menus have too
		auto notNarrow = [this]() { return !_narrow; };
		_toolbar->Add<UiSeparator>(UiOrientation::Vertical)->IsVisibleFunc = notNarrow;
		for (const char* id : { "edit.cut", "edit.copy", "edit.paste" }) {
			if (UiButton* button = AddActionButton(_toolbar, id)) {
				button->IsVisibleFunc = notNarrow;
			}
		}
		_toolbar->Add<UiSeparator>(UiOrientation::Vertical)->IsVisibleFunc = notNarrow;
		EditorSettings* settings = &_session.GetSettings();
		for (const char* id : { "view.grid", "view.flat", "view.pauseLayers", "view.masks", "view.events", "view.playPath", "view.reachability" }) {
			if (UiButton* button = AddActionButton(_toolbar, id)) {
				bool expert = IsExpertAction(id);
				button->IsVisibleFunc = [this, settings, expert]() { return (!_narrow && (!expert || settings->Expert)); };
			}
		}
		_toolbar->Add<UiSeparator>(UiOrientation::Vertical);
		AddActionButton(_toolbar, "view.zoomOut"_s);
		AddActionButton(_toolbar, "view.zoomIn"_s);
		AddActionButton(_toolbar, "view.fit"_s);
		_toolbar->Add<UiElement>()->Flex = 1.0f;
		if (UiButton* play = AddActionButton(_toolbar, "play.resume"_s, true)) {
			play->Style = UiButtonStyle::Accent;
			play->IconColor = UiTheme::Play;
		}
	}

	void EditorShell::CreateToolStrip()
	{
		_toolStrip = Add<UiPanel>(UiPanelStyle::Panel, UiOrientation::Vertical, 2.0f, UiThickness(3.0f));
		// A short window cuts off the buttons that don't fit, they'd show under the status bar otherwise
		_toolStrip->ClipContent = true;
		for (const char* id : { "tool.brush", "tool.eraser", "tool.rectangle", "tool.fill", "tool.smart", "tool.ground", "tool.eyedropper", "tool.select", "tool.wand", "tool.event", "tool.path", "tool.measure", "tool.hand" }) {
			AddActionButton(_toolStrip, id);
		}
		_toolStrip->Add<UiSeparator>();
		AddActionButton(_toolStrip, "stamp.flipX"_s);
		AddActionButton(_toolStrip, "stamp.flipY"_s);
		_toolStrip->Add<UiSeparator>();
		_toolStrip->Add<StampPreview>(_session);
	}

	UiButton* EditorShell::AddActionButton(UiElement* parent, StringView actionId, bool showText)
	{
		EditorAction* action = _session.GetActions().Find(actionId);
		if (action == nullptr) {
			return nullptr;
		}
		auto* button = parent->Add<UiButton>(showText ? StringView(action->Label) : StringView(), action->Icon, [action]() {
			if (action->CanExecute()) {
				action->Execute();
			}
		});
		button->Style = UiButtonStyle::Flat;
		button->Tooltip = action->GetTooltip();
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
		_playBarAppear = (_session.ArePanelsShown() ? 0.0f : std::min(_playBarAppear + timeMult / PlayBarAppearFrames, 1.0f));
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
			DrawAnnotations(painter);

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

		const Recti& bounds = selection.GetBounds();
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X, _viewRect.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(_viewRect.X + _viewRect.W, _viewRect.Y + _viewRect.H)));
		from.X = std::max(from.X, bounds.X);
		from.Y = std::max(from.Y, bounds.Y);
		to.X = std::min(to.X, bounds.X + bounds.W - 1);
		to.Y = std::min(to.Y, bounds.Y + bounds.H - 1);

		// The edges between selected and unselected tiles are marching ants of short dashes, as a bitmap editor has them,
		// the dashes follow the diagonal of the screen so they continue around corners
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		float dash = 2.0f * thickness;
		float period = dash * 2.0f;
		UiSurface* surface = GetSurface();
		float offset = std::fmod((surface != nullptr ? surface->AnimTime : 0.0f) * dash * 4.0f, period);
		Colorf edgeA = UiTheme::Accent;
		Colorf edgeB = Colorf(0.1f, 0.08f, 0.2f, 1.0f);
		auto drawEdge = [&](float x, float y, float length, bool horizontal) {
			Rectf line = (horizontal ? Rectf(x, y, length, thickness) : Rectf(x, y, thickness, length));
			painter.FillRect(line, edgeB, UiLayer::Content);
			float from = (horizontal ? x : y);
			float across = (horizontal ? y : x);
			float to = from + length;
			// The first dash that starts at or before the edge
			float start = std::floor((from + across - offset) / period) * period + offset - across;
			for (float pos = start; pos < to; pos += period) {
				float a = std::max(pos, from), b = std::min(pos + dash, to);
				if (b > a) {
					painter.FillRect(horizontal ? Rectf(a, y, b - a, thickness) : Rectf(x, a, thickness, b - a), edgeA, UiLayer::Overlay);
				}
			}
		};
		for (std::int32_t y = from.Y; y <= to.Y; y++) {
			for (std::int32_t x = from.X; x <= to.X; x++) {
				if (!selection.Contains(x, y)) {
					continue;
				}
				Rectf rect = view.CellToScreen(Vector2i(x, y));
				painter.FillRect(rect, UiTheme::Selection, UiLayer::Fill);
				if (!selection.Contains(x, y - 1)) {
					drawEdge(rect.X, rect.Y, rect.W, true);
				}
				if (!selection.Contains(x, y + 1)) {
					drawEdge(rect.X, rect.Y + rect.H - thickness, rect.W, true);
				}
				if (!selection.Contains(x - 1, y)) {
					drawEdge(rect.X, rect.Y, rect.H, false);
				}
				if (!selection.Contains(x + 1, y)) {
					drawEdge(rect.X + rect.W - thickness, rect.Y, rect.H, false);
				}
			}
		}
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
	}

	void EditorShell::DrawReachability(UiPainter& painter)
	{
		ArrayView<const std::int32_t> distances = _session.GetReachability();
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

		// Brighter near the start, further away it fades, so the way through the level can be read
		std::int32_t maxDistance = 1;
		for (std::int32_t distance : distances) {
			maxDistance = std::max(maxDistance, distance);
		}
		for (std::int32_t y = from.Y; y <= to.Y; y++) {
			for (std::int32_t x = from.X; x <= to.X; x++) {
				std::int32_t distance = distances[std::size_t(y) * std::size_t(gridSize.X) + std::size_t(x)];
				if (distance < 0) {
					continue;
				}
				float near = 1.0f - float(distance) / float(maxDistance);
				painter.FillRect(view.CellToScreen(Vector2i(x, y)), Colorf(0.3f + 0.4f * near, 1.0f, 0.4f, 0.14f + 0.18f * near), UiLayer::Fill);
			}
		}
	}

	void EditorShell::DrawAnnotations(UiPainter& painter)
	{
		// The play-start marker, a flag on a pole standing where the player starts
		Vector2i playStart;
		if (_session.GetPlayStart(playStart)) {
			std::int32_t spriteLayer = _session.GetDocument().GetSpriteLayerIndex();
			LayerView view = _session.GetLayerView(spriteLayer >= 0 ? spriteLayer : 0);
			Vector2f pos = view.WorldToScreen(Vector2f(float(playStart.X), float(playStart.Y)));
			pos = Vector2f(std::round(pos.X), std::round(pos.Y));
			if (_viewRect.Contains(pos)) {
				float unit = std::max(1.0f, std::round(painter.GetScale()));
				float pole = painter.Px(18.0f);
				painter.Circle(pos, painter.Px(3.0f), Colorf(0.0f, 0.0f, 0.0f, 0.5f), UiLayer::Overlay);
				painter.FillRect(Rectf(pos.X - std::floor(unit * 0.5f), pos.Y - pole, unit, pole), Colorf(0.9f, 0.9f, 0.9f, 1.0f), UiLayer::Overlay);
				// A pennant, row by row, the painter has no triangles
				float flagHeight = painter.Px(8.0f), flagWidth = painter.Px(10.0f);
				for (float y = 0.0f; y < flagHeight; y += unit) {
					float width = std::round(flagWidth * (1.0f - std::abs(y + unit * 0.5f - flagHeight * 0.5f) / (flagHeight * 0.5f)));
					if (width > 0.0f) {
						painter.FillRect(Rectf(pos.X, pos.Y - pole + y, width, unit), UiTheme::Play, UiLayer::Overlay);
					}
				}
				// TRANSLATORS: Label of the flag in the level where play-tests from the marker start
				painter.Text(_("Play start"), Vector2f(pos.X + flagWidth + painter.Px(2.0f), pos.Y - pole), UiTheme::TextPlay, Alignment::TopLeft, UiLayer::OverlayText);
			}
		}

		const Formats::ChunkRecord* chunk = _session.GetDocument().FindChunk(Annotation::Chunk);
		if (chunk == nullptr) {
			return;
		}
		SmallVector<Annotation, 0> annotations;
		Annotation::Read(_session.GetDocument(), annotations);
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
			if (annotation.IsBookmark()) {
				// A diamond with its number
				Vector2f diamond[5] = { pos + Vector2f(0.0f, -size * 1.6f), pos + Vector2f(size * 1.6f, 0.0f), pos + Vector2f(0.0f, size * 1.6f),
					pos + Vector2f(-size * 1.6f, 0.0f), pos + Vector2f(0.0f, -size * 1.6f) };
				painter.Circle(pos, size * 1.3f, Colorf(0.15f, 0.35f, 0.55f, 0.9f), UiLayer::Overlay);
				painter.Polyline(diamond, std::max(1.0f, painter.Px(1.0f)), Colorf(0.4f, 0.8f, 1.0f, 1.0f), UiLayer::Overlay);
				char number[2] = { char('0' + annotation.Slot), '\0' };
				painter.TextInRect(StringView(number, 1), Rectf(pos.X - size * 2.0f, pos.Y - size * 2.0f, size * 4.0f, size * 4.0f),
					UiTheme::Text, Alignment::Center, UiLayer::OverlayText);
			} else {
				// A pin with the text next to it
				painter.Circle(pos, size, Colorf(1.0f, 0.85f, 0.35f, 1.0f), UiLayer::Overlay);
				painter.Circle(pos, size * 0.45f, Colorf(0.4f, 0.3f, 0.1f, 1.0f), UiLayer::Overlay);
				String text = EventCatalog::MakeTextPreview(annotation.Text, 40);
				if (!text.empty()) {
					float width = painter.MeasureText(text) + padding * 2.0f;
					float height = painter.Px(UiTheme::RowHeight);
					Rectf bubble(pos.X + size * 1.5f, pos.Y - height * 0.5f, width, height);
					painter.FillRect(bubble, Colorf(0.25f, 0.2f, 0.08f, 0.85f), UiLayer::Overlay);
					painter.TextInRect(text, Rectf(bubble.X + padding, bubble.Y, bubble.W - padding * 2.0f, bubble.H), UiTheme::TextAccent, Alignment::Left, UiLayer::OverlayText);
				}
			}
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
						stroke({ Vector2f(0.5f, 0.2f), Vector2f(0.8f, 0.5f), Vector2f(0.5f, 0.8f), Vector2f(0.2f, 0.5f), Vector2f(0.5f, 0.2f) }, otherColor);
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
		bool moving = (editingEvents && _session.GetTools().GetActive()->IsBusy() && _session.GetTools().GetActiveType() == EditorToolType::Event);
		std::int32_t playDifficulty = _session.GetSettings().PlayDifficulty;
		std::uint8_t difficultyBit = (playDifficulty >= std::int32_t(GameDifficulty::Easy) && playDifficulty <= std::int32_t(GameDifficulty::Hard)
			? std::uint8_t(Formats::EventRecord::Easy << (playDifficulty - std::int32_t(GameDifficulty::Easy))) : 0);
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

		// Events off the grid, at their own position
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			previews.DrawInScene(canvas, event.Event, Vector2f(float(event.X), float(event.Y)), alpha * 0.6f, animTime, Z);
		}
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
		bool moving = (editingEvents && _session.GetTools().GetActive()->IsBusy() && _session.GetTools().GetActiveType() == EditorToolType::Event);
		std::int32_t playDifficulty = _session.GetSettings().PlayDifficulty;
		std::uint8_t difficultyBit = (playDifficulty >= std::int32_t(GameDifficulty::Easy) && playDifficulty <= std::int32_t(GameDifficulty::Hard)
			? std::uint8_t(Formats::EventRecord::Easy << (playDifficulty - std::int32_t(GameDifficulty::Easy))) : 0);
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

		// Events off the grid, at their own position
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			Vector2f pos = view.WorldToScreen(Vector2f(float(event.X), float(event.Y)));
			float half = tileSize * 0.5f;
			Rectf rect(pos.X - half, pos.Y - half, tileSize, tileSize);
			if (rect.X > area.X + area.W || rect.Y > area.Y + area.H || rect.X + rect.W < area.X || rect.Y + rect.H < area.Y || reachesHole(rect)) {
				continue;
			}
			previews.DrawPlaced(painter, event.Event, rect, view.Scale, alpha * 0.6f, animTime, Vector2f(float(event.X), float(event.Y)), true);
		}
	}

	void EditorShell::DrawSelectedEventDetails(UiPainter& painter, const LayerView& view)
	{
		LevelDocument& document = _session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		Colorf detailColor(1.0f, 0.95f, 0.55f, 0.85f);

		for (std::uint32_t cell : _session.GetEventSelection().GetCells()) {
			Vector2i pos(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
			const Formats::EventRecord& event = document.GetEvent(pos.X, pos.Y);
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
			if (descriptor == nullptr) {
				continue;
			}
			Vector2f center = view.WorldToScreen(Vector2f(pos.X * 32.0f + 16.0f, pos.Y * 32.0f + 16.0f));

			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				const EventParamField& field = descriptor->Fields[i];
				if (field.Unit == ParamUnit::Pixels && StringView(field.Name).contains("Radius"_s)) {
					// How far a light reaches
					float radius = float(EventCatalog::ReadField(event, field)) * view.Scale;
					if (radius < 2.0f) {
						continue;
					}
					constexpr std::int32_t Segments = 48;
					Vector2f last = center + Vector2f(radius, 0.0f);
					for (std::int32_t s = 1; s <= Segments; s++) {
						float angle = 6.2831853f * float(s) / float(Segments);
						Vector2f next = center + Vector2f(std::cos(angle) * radius, std::sin(angle) * radius);
						painter.Line(last, next, thickness, detailColor, UiLayer::Overlay);
						last = next;
					}
				} else if (field.Type == ParamType::TextId) {
					// The text the event refers to, above it
					std::int32_t index = EventCatalog::ReadField(event, field);
					if (index < 0 || index >= std::int32_t(document.Texts.size()) || document.Texts[index].empty()) {
						continue;
					}
					String text = EventCatalog::MakeTextPreview(document.Texts[index], 48);
					if (text.empty()) {
						continue;
					}
					// As high as a row of the interface, the text is centered in it as in a label
					// Well above the event, so its mark and what's around it can still be seen
					float padding = painter.Px(UiTheme::Padding * 2.0f);
					float width = painter.MeasureText(text) + padding * 2.0f;
					float height = painter.Px(UiTheme::RowHeight) + painter.Px(6.0f);
					Rectf bubble(std::round(center.X - width * 0.5f), std::round(center.Y - 16.0f * view.Scale - height - painter.Px(14.0f)), width, height);
					painter.Panel(bubble, false);
					painter.TextInRect(text, Rectf(bubble.X + padding, bubble.Y, bubble.W - padding * 2.0f, bubble.H), UiTheme::Text, Alignment::Left, UiLayer::OverlayText);
				}
			}

			if (EventType(event.Type) == EventType::ModifierSetWater && descriptor->FieldCount > 0) {
				// The new water level across the view
				float y = std::round(view.WorldToScreen(Vector2f(0.0f, float(EventCatalog::ReadField(event, descriptor->Fields[0])))).Y);
				painter.FillRect(Rectf(_viewRect.X, y, _viewRect.W, std::max(thickness, 2.0f)), Colorf(0.3f, 0.6f, 1.0f, 0.8f), UiLayer::Overlay);
				painter.Line(center, Vector2f(center.X, y), thickness, Colorf(0.3f, 0.6f, 1.0f, 0.6f), UiLayer::Overlay);
			} else if (EventType(event.Type) == EventType::ModifierLimitCameraView && descriptor->FieldCount > 1) {
				// Where the camera is kept, darker outside of it, see Player::HandleAreaEventAt()
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

	void EditorShell::DrawEventLinks(UiPainter& painter, const LayerView& view, float alpha)
	{
		LevelDocument& document = _session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 1.5f));
		float tileSize = 32.0f * view.Scale;

		auto findField = [](std::uint16_t type, ParamType paramType) -> const EventParamField* {
			if (const EventDescriptor* descriptor = EventCatalog::Find(EventType(type))) {
				for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
					if (descriptor->Fields[i].Type == paramType) {
						return &descriptor->Fields[i];
					}
				}
			}
			return nullptr;
		};
		auto cellRect = [&view, gridSize](std::uint32_t cell) {
			Vector2f world(float(cell % std::uint32_t(gridSize.X)) * 32.0f, float(cell / std::uint32_t(gridSize.X)) * 32.0f);
			Vector2f topLeft = view.WorldToScreen(world);
			return Rectf(topLeft.X, topLeft.Y, 32.0f * view.Scale, 32.0f * view.Scale);
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
		struct Endpoint
		{
			std::uint32_t Cell;
			std::int32_t Id;
			std::int32_t Group;
		};
		SmallVector<Endpoint, 0> origins;
		SmallVector<Endpoint, 0> targets;
		for (const DocumentEvent& event : document.Events) {
			const EventParamField* field = findField(event.Record.Type, ParamType::WarpId);
			if (field == nullptr) {
				continue;
			}
			std::int32_t id = EventCatalog::ReadField(event.Record, *field);
			if (EventType(event.Record.Type) == EventType::WarpTarget) {
				targets.push_back(Endpoint{ event.Cell, id, -1 });
			} else {
				origins.push_back(Endpoint{ event.Cell, id, std::int32_t(origins.size()) });
			}
		}
		if (!origins.empty()) {
			// Groups by repeated merging of neighbors, there are a few dozens of warp tiles in a level at most
			auto findGroup = [&origins](std::int32_t i) {
				while (origins[i].Group != i) {
					i = origins[i].Group;
				}
				return i;
			};
			for (std::size_t i = 0; i < origins.size(); i++) {
				for (std::size_t j = i + 1; j < origins.size(); j++) {
					if (origins[i].Id != origins[j].Id) {
						continue;
					}
					std::int32_t dx = std::abs(std::int32_t(origins[i].Cell % std::uint32_t(gridSize.X)) - std::int32_t(origins[j].Cell % std::uint32_t(gridSize.X)));
					std::int32_t dy = std::abs(std::int32_t(origins[i].Cell / std::uint32_t(gridSize.X)) - std::int32_t(origins[j].Cell / std::uint32_t(gridSize.X)));
					if (dx <= 1 && dy <= 1) {
						origins[findGroup(std::int32_t(j))].Group = findGroup(std::int32_t(i));
					}
				}
			}

			Colorf linkColor(0.45f, 0.65f, 1.0f, 0.85f * alpha);
			for (std::size_t i = 0; i < origins.size(); i++) {
				if (findGroup(std::int32_t(i)) != std::int32_t(i)) {
					continue;
				}
				// The tiles of the warp together, their bounding rectangle
				Rectf fromRect;
				bool first = true;
				for (std::size_t j = 0; j < origins.size(); j++) {
					if (findGroup(std::int32_t(j)) == std::int32_t(i)) {
						Rectf rect = cellRect(origins[j].Cell);
						if (first) {
							fromRect = rect;
							first = false;
						} else {
							float right = std::max(fromRect.X + fromRect.W, rect.X + rect.W), bottom = std::max(fromRect.Y + fromRect.H, rect.Y + rect.H);
							fromRect.X = std::min(fromRect.X, rect.X);
							fromRect.Y = std::min(fromRect.Y, rect.Y);
							fromRect.W = right - fromRect.X;
							fromRect.H = bottom - fromRect.Y;
						}
					}
				}
				Vector2f from(fromRect.X + fromRect.W * 0.5f, fromRect.Y + fromRect.H * 0.5f);
				bool hasTarget = false;
				for (const Endpoint& target : targets) {
					if (target.Id == origins[i].Id) {
						drawLink(fromRect, cellRect(target.Cell), thickness, linkColor, true);
						hasTarget = true;
					}
				}
				if (!hasTarget) {
					// A warp to nowhere
					float size = std::max(tileSize * 0.3f, 6.0f);
					painter.Line(from - Vector2f(size, size), from + Vector2f(size, size), thickness * 1.5f, UiTheme::Danger, UiLayer::Overlay);
					painter.Line(from - Vector2f(size, -size), from + Vector2f(size, -size), thickness * 1.5f, UiTheme::Danger, UiLayer::Overlay);
				}
			}
		}

		// Selected events show which events share their trigger
		if (!_session.IsEditingEvents()) {
			return;
		}
		Colorf triggerColor(1.0f, 0.62f, 0.25f, 0.9f * alpha);
		for (std::uint32_t selected : _session.GetEventSelection().GetCells()) {
			const Formats::EventRecord& event = document.GetEvent(std::int32_t(selected % std::uint32_t(gridSize.X)), std::int32_t(selected / std::uint32_t(gridSize.X)));
			const EventParamField* field = findField(event.Type, ParamType::TriggerId);
			if (field == nullptr) {
				continue;
			}
			std::int32_t id = EventCatalog::ReadField(event, *field);
			Rectf fromRect = cellRect(selected);
			for (const DocumentEvent& other : document.Events) {
				if (other.Cell == selected) {
					continue;
				}
				const EventParamField* otherField = findField(other.Record.Type, ParamType::TriggerId);
				if (otherField != nullptr && EventCatalog::ReadField(other.Record, *otherField) == id) {
					Rectf rect = cellRect(other.Cell);
					drawLink(fromRect, rect, std::max(1.0f, thickness * 0.67f), triggerColor, false);
					painter.Frame(rect, triggerColor, std::max(1.0f, std::round(painter.GetScale())), UiLayer::Overlay);
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

		// Dark under light, so it's seen on bright and dark layers alike
		Rectf frame(std::round(layer.X) - width, std::round(layer.Y) - width, std::round(layer.W) + width * 2.0f, std::round(layer.H) + width * 2.0f);
		painter.Frame(Rectf(frame.X - width, frame.Y - width, frame.W + width * 2.0f, frame.H + width * 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.45f), width, UiLayer::Overlay);
		painter.Frame(frame, Colorf(1.0f, 1.0f, 1.0f, 0.85f), width, UiLayer::Overlay);
	}
}

#endif
