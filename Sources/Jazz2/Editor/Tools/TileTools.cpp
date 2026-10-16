#include "TileTools.h"

#if defined(WITH_EDITOR)

#include "EventTools.h"
#include "MeasureTool.h"
#include "PathTool.h"
#include "GroundLineTool.h"
#include "TilePixelTool.h"
#include "SmartBrushTool.h"
#include "../Runtime/EditorCamera.h"
#include "../../Tiles/TileMap.h"
#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float TileSize = 32.0f;
		// Ghosts of more tiles than this are only outlined, it would be slow to draw them all
		constexpr std::int32_t MaxGhostTiles = 4096;
		// The largest area a fill changes at once
		constexpr std::int32_t MaxFillTiles = 1 << 22;

		std::int32_t PositiveModulo(std::int32_t value, std::int32_t divisor)
		{
			std::int32_t result = value % divisor;
			return (result < 0 ? result + divisor : result);
		}

		Recti MakeRect(Vector2i a, Vector2i b)
		{
			std::int32_t x1 = std::min(a.X, b.X), y1 = std::min(a.Y, b.Y);
			std::int32_t x2 = std::max(a.X, b.X), y2 = std::max(a.Y, b.Y);
			return Recti(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
		}

		// Visits all cells of a line from one cell to another (Bresenham)
		template<class TCallback>
		void WalkLine(Vector2i from, Vector2i to, TCallback&& callback)
		{
			std::int32_t dx = std::abs(to.X - from.X), sx = (from.X < to.X ? 1 : -1);
			std::int32_t dy = -std::abs(to.Y - from.Y), sy = (from.Y < to.Y ? 1 : -1);
			std::int32_t error = dx + dy;
			Vector2i cell = from;
			while (true) {
				callback(cell);
				if (cell == to) {
					break;
				}
				std::int32_t e2 = error * 2;
				if (e2 >= dy) {
					error += dy;
					cell.X += sx;
				}
				if (e2 <= dx) {
					error += dx;
					cell.Y += sy;
				}
			}
		}
	}

	Vector2f LayerView::ScreenToWorld(Vector2f screen) const
	{
		return (screen - ScreenOffset + ViewOrigin) / Scale;
	}

	Vector2f LayerView::WorldToScreen(Vector2f world) const
	{
		return world * Scale - ViewOrigin + ScreenOffset;
	}

	Vector2i LayerView::WorldToCell(Vector2f world) const
	{
		return Vector2i(std::int32_t(std::floor((world.X - Origin.X) / TileSize)), std::int32_t(std::floor((world.Y - Origin.Y) / TileSize)));
	}

	Rectf LayerView::CellToScreen(Vector2i cell) const
	{
		Vector2f topLeft = WorldToScreen(Vector2f(Origin.X + cell.X * TileSize, Origin.Y + cell.Y * TileSize));
		Vector2f bottomRight = WorldToScreen(Vector2f(Origin.X + (cell.X + 1) * TileSize, Origin.Y + (cell.Y + 1) * TileSize));
		return Rectf(std::round(topLeft.X), std::round(topLeft.Y), std::round(bottomRight.X) - std::round(topLeft.X), std::round(bottomRight.Y) - std::round(topLeft.Y));
	}

	bool LayerView::Wrap(Vector2i cell, Vector2i& tile) const
	{
		if (Size.X <= 0 || Size.Y <= 0) {
			return false;
		}
		if (RepeatX) {
			tile.X = PositiveModulo(cell.X, Size.X);
		} else if (cell.X < 0 || cell.X >= Size.X) {
			return false;
		} else {
			tile.X = cell.X;
		}
		if (RepeatY) {
			tile.Y = PositiveModulo(cell.Y, Size.Y);
		} else if (cell.Y < 0 || cell.Y >= Size.Y) {
			return false;
		} else {
			tile.Y = cell.Y;
		}
		return true;
	}

	EditorTool::EditorTool(IToolHost& host)
		: _host(host)
	{
	}

	EditorTool::~EditorTool()
	{
	}

	void EditorTool::OnActivate()
	{
	}

	void EditorTool::OnDeactivate()
	{
		Cancel();
	}

	void EditorTool::OnPointerDown(const ToolPointer& pointer)
	{
	}

	void EditorTool::OnPointerMove(const ToolPointer& pointer)
	{
	}

	void EditorTool::OnPointerUp(const ToolPointer& pointer)
	{
	}

	void EditorTool::Cancel()
	{
	}

	bool EditorTool::IsBusy() const
	{
		return false;
	}

	void EditorTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		if (hoverValid) {
			LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
			DrawCellsOutline(painter, view, hover.Cell, hover.Cell, UiTheme::Cursor);
		}
	}

	StringView EditorTool::GetHint() const
	{
		return {};
	}

	void EditorTool::DrawCellsOutline(UiPainter& painter, const LayerView& view, Vector2i from, Vector2i to, const Colorf& color)
	{
		Recti cells = MakeRect(from, to);
		Rectf topLeft = view.CellToScreen(Vector2i(cells.X, cells.Y));
		Rectf bottomRight = view.CellToScreen(Vector2i(cells.X + cells.W - 1, cells.Y + cells.H - 1));
		Rectf rect(topLeft.X, topLeft.Y, bottomRight.X + bottomRight.W - topLeft.X, bottomRight.Y + bottomRight.H - topLeft.Y);
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		// A dark edge under a light one, so it can be seen over anything
		painter.Frame(Rectf(rect.X - thickness, rect.Y - thickness, rect.W + thickness * 2.0f, rect.H + thickness * 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.5f), thickness, UiLayer::Overlay);
		painter.Frame(rect, color, thickness, UiLayer::Overlay);
	}

	void EditorTool::DrawStampGhost(UiPainter& painter, const LayerView& view, const TileStamp& stamp, Vector2i cell, Vector2i anchor, float alpha)
	{
		Tiles::TileMap* tileMap = _host.GetTileMap();
		if (tileMap == nullptr || stamp.IsEmpty() || stamp.Width * stamp.Height > MaxGhostTiles) {
			return;
		}
		const LevelDocument& document = _host.GetDocument();
		for (std::int32_t y = 0; y < stamp.Height; y++) {
			for (std::int32_t x = 0; x < stamp.Width; x++) {
				if (!stamp.IsPresent(x, y)) {
					continue;
				}
				const TileRef& tile = stamp.GetTile(x, y);
				Rectf rect = view.CellToScreen(Vector2i(cell.X - anchor.X + x, cell.Y - anchor.Y + y));
				if (tile.IsEmpty()) {
					painter.FillRect(rect, Colorf(0.9f, 0.25f, 0.2f, 0.25f * alpha), UiLayer::Content);
				} else {
					painter.Tile(*tileMap, document.ToTileId(tile), tile.Flags, rect, alpha);
				}
			}
		}
	}

	StrokeTool::StrokeTool(IToolHost& host)
		: EditorTool(host), _strokeLayer(-1)
	{
	}

	void StrokeTool::OnDeactivate()
	{
		Cancel();
	}

	void StrokeTool::Cancel()
	{
		if (_stroke != nullptr) {
			Recti area = _stroke->GetArea();
			_stroke = nullptr;
			_host.RevertPreview(_strokeLayer, area);
		}
	}

	bool StrokeTool::IsBusy() const
	{
		return (_stroke != nullptr);
	}

	bool StrokeTool::BeginStroke(String name)
	{
		Cancel();
		std::int32_t layer = _host.GetCurrentLayer();
		if (!_host.IsLayerEditable(layer)) {
			_host.ShowMessage(_("The layer is locked or hidden"));
			return false;
		}
		_strokeLayer = layer;
		_stroke = std::make_unique<SetTilesCommand>(_host.GetDocument(), layer, std::move(name));
		return true;
	}

	void StrokeTool::PaintCell(Vector2i cell, const TileRef& tile)
	{
		if (_stroke == nullptr) {
			return;
		}
		LayerView view = _host.GetLayerView(_strokeLayer);
		Vector2i layerTile;
		if (!view.Wrap(cell, layerTile)) {
			return;
		}
		const TileSelection& selection = _host.GetSelection();
		if (!selection.IsEmpty() && selection.GetLayer() == _strokeLayer && !selection.Contains(layerTile.X, layerTile.Y)) {
			return;
		}
		_stroke->Set(layerTile.X, layerTile.Y, tile);
		_host.PreviewTile(_strokeLayer, layerTile.X, layerTile.Y, tile);
	}

	void StrokeTool::PaintStamp(const TileStamp& stamp, Vector2i cell, Vector2i anchor, Vector2i patternOrigin)
	{
		if (stamp.Random) {
			// One of its tiles in the cell
			TileRef tile;
			if (stamp.GetPatternTile(cell.X, cell.Y, patternOrigin, tile)) {
				PaintCell(cell, tile);
			}
			return;
		}
		for (std::int32_t y = 0; y < stamp.Height; y++) {
			for (std::int32_t x = 0; x < stamp.Width; x++) {
				Vector2i target(cell.X - anchor.X + x, cell.Y - anchor.Y + y);
				TileRef tile;
				if (stamp.GetPatternTile(target.X, target.Y, patternOrigin, tile)) {
					PaintCell(target, tile);
				}
			}
		}
	}

	void StrokeTool::EndStroke()
	{
		if (_stroke == nullptr) {
			return;
		}
		std::unique_ptr<SetTilesCommand> stroke = std::move(_stroke);
		stroke->Seal();
		if (!stroke->IsEmpty()) {
			_host.Execute(std::move(stroke));
		}
	}

	BrushTool::BrushTool(IToolHost& host, bool erase)
		: StrokeTool(host), _erase(erase)
	{
	}

	EditorToolType BrushTool::GetType() const
	{
		return (_erase ? EditorToolType::Eraser : EditorToolType::Brush);
	}

	const TileStamp& BrushTool::GetBrushStamp() const
	{
		static const TileStamp EmptyStamp = TileStamp::FromTile(TileRef{});
		return (_erase ? EmptyStamp : _host.GetStamp());
	}

	Vector2i BrushTool::GetAnchor() const
	{
		const TileStamp& stamp = GetBrushStamp();
		return (stamp.Random ? Vector2i() : Vector2i(stamp.Width / 2, stamp.Height / 2));
	}

	void BrushTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button == MouseButton::Right) {
			// A quick pick of the tile under the pointer
			LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
			Vector2i tile;
			if (!_erase && view.Wrap(pointer.Cell, tile)) {
				const DocumentLayer& layer = _host.GetDocument().Layers[_host.GetCurrentLayer()];
				_host.SetStamp(TileStamp::FromTile(layer.GetTile(tile.X, tile.Y)));
			}
			return;
		}
		if (pointer.Button != MouseButton::Left) {
			return;
		}
		const TileStamp& stamp = GetBrushStamp();
		if (stamp.IsEmpty() || !BeginStroke(_erase ? _("Erase") : _("Paint"))) {
			return;
		}
		_lastCell = pointer.Cell;
		_patternOrigin = pointer.Cell - GetAnchor();
		PaintStamp(stamp, pointer.Cell, GetAnchor(), _patternOrigin);
	}

	void BrushTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_stroke == nullptr || pointer.Cell == _lastCell) {
			return;
		}
		const TileStamp& stamp = GetBrushStamp();
		Vector2i anchor = GetAnchor();
		WalkLine(_lastCell, pointer.Cell, [&](Vector2i cell) {
			PaintStamp(stamp, cell, anchor, _patternOrigin);
		});
		_lastCell = pointer.Cell;
	}

	void BrushTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (pointer.Button == MouseButton::Left) {
			EndStroke();
		}
	}

	void BrushTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		if (!hoverValid) {
			return;
		}
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		const TileStamp& stamp = GetBrushStamp();
		Vector2i anchor = GetAnchor();
		if (stamp.Random) {
			// A single cell, with the tile it would get
			TileRef tile;
			if (_stroke == nullptr && stamp.GetPatternTile(hover.Cell.X, hover.Cell.Y, Vector2i(), tile)) {
				DrawStampGhost(painter, view, TileStamp::FromTile(tile), hover.Cell, Vector2i(), 0.65f);
			}
			DrawCellsOutline(painter, view, hover.Cell, hover.Cell, _erase ? UiTheme::Danger : UiTheme::Cursor);
			return;
		}
		if (_stroke == nullptr) {
			DrawStampGhost(painter, view, stamp, hover.Cell, anchor, 0.65f);
		}
		Vector2i from(hover.Cell.X - anchor.X, hover.Cell.Y - anchor.Y);
		DrawCellsOutline(painter, view, from, Vector2i(from.X + stamp.Width - 1, from.Y + stamp.Height - 1), _erase ? UiTheme::Danger : UiTheme::Cursor);
	}

	StringView BrushTool::GetHint() const
	{
		return (_erase ? _("Click or drag to erase tiles") : _("Click or drag to paint, right-click to pick a tile"));
	}

	RectangleTool::RectangleTool(IToolHost& host)
		: StrokeTool(host), _dragging(false), _erase(false)
	{
	}

	EditorToolType RectangleTool::GetType() const
	{
		return EditorToolType::Rectangle;
	}

	void RectangleTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button != MouseButton::Left && pointer.Button != MouseButton::Right) {
			return;
		}
		if (!_host.IsLayerEditable(_host.GetCurrentLayer())) {
			_host.ShowMessage(_("The layer is locked or hidden"));
			return;
		}
		_dragging = true;
		_erase = (pointer.Button == MouseButton::Right);
		_start = _end = pointer.Cell;
	}

	void RectangleTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragging) {
			_end = pointer.Cell;
		}
	}

	void RectangleTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (!_dragging) {
			return;
		}
		_dragging = false;
		_end = pointer.Cell;

		const TileStamp& stamp = (_erase ? TileStamp::FromTile(TileRef{}) : _host.GetStamp());
		if (stamp.IsEmpty() || !BeginStroke(_erase ? _("Erase rectangle") : _("Fill rectangle"))) {
			return;
		}
		Recti rect = MakeRect(_start, _end);
		for (std::int32_t y = rect.Y; y < rect.Y + rect.H; y++) {
			for (std::int32_t x = rect.X; x < rect.X + rect.W; x++) {
				TileRef tile;
				if (stamp.GetPatternTile(x, y, Vector2i(rect.X, rect.Y), tile)) {
					PaintCell(Vector2i(x, y), tile);
				}
			}
		}
		EndStroke();
	}

	void RectangleTool::Cancel()
	{
		_dragging = false;
		StrokeTool::Cancel();
	}

	bool RectangleTool::IsBusy() const
	{
		return _dragging;
	}

	void RectangleTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		if (!_dragging) {
			if (hoverValid) {
				DrawCellsOutline(painter, view, hover.Cell, hover.Cell, UiTheme::Cursor);
			}
			return;
		}

		Recti rect = MakeRect(_start, _end);
		const TileStamp& stamp = _host.GetStamp();
		if (!_erase && rect.W * rect.H <= MaxGhostTiles && !stamp.IsEmpty()) {
			Tiles::TileMap* tileMap = _host.GetTileMap();
			const LevelDocument& document = _host.GetDocument();
			for (std::int32_t y = rect.Y; y < rect.Y + rect.H && tileMap != nullptr; y++) {
				for (std::int32_t x = rect.X; x < rect.X + rect.W; x++) {
					TileRef tile;
					if (stamp.GetPatternTile(x, y, Vector2i(rect.X, rect.Y), tile) && !tile.IsEmpty()) {
						painter.Tile(*tileMap, document.ToTileId(tile), tile.Flags, view.CellToScreen(Vector2i(x, y)), 0.7f);
					}
				}
			}
		} else if (_erase) {
			Rectf topLeft = view.CellToScreen(Vector2i(rect.X, rect.Y));
			Rectf bottomRight = view.CellToScreen(Vector2i(rect.X + rect.W - 1, rect.Y + rect.H - 1));
			painter.FillRect(Rectf(topLeft.X, topLeft.Y, bottomRight.X + bottomRight.W - topLeft.X, bottomRight.Y + bottomRight.H - topLeft.Y),
				Colorf(0.9f, 0.25f, 0.2f, 0.25f), UiLayer::Content);
		}
		DrawCellsOutline(painter, view, _start, _end, _erase ? UiTheme::Danger : UiTheme::Accent);
	}

	StringView RectangleTool::GetHint() const
	{
		return _("Drag to fill a rectangle, right-drag to erase one");
	}

	FillTool::FillTool(IToolHost& host)
		: StrokeTool(host)
	{
	}

	EditorToolType FillTool::GetType() const
	{
		return EditorToolType::Fill;
	}

	void FillTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button != MouseButton::Left && pointer.Button != MouseButton::Right) {
			return;
		}

		std::int32_t layerIndex = _host.GetCurrentLayer();
		LayerView view = _host.GetLayerView(layerIndex);
		Vector2i start;
		if (!view.Wrap(pointer.Cell, start)) {
			return;
		}

		bool erase = (pointer.Button == MouseButton::Right);
		TileStamp eraseStamp = TileStamp::FromTile(TileRef{});
		const TileStamp& stamp = (erase ? eraseStamp : _host.GetStamp());
		if (stamp.IsEmpty() || !BeginStroke(erase ? _("Erase area") : _("Fill"))) {
			return;
		}

		const DocumentLayer& layer = _host.GetDocument().Layers[layerIndex];
		const TileSelection& selection = _host.GetSelection();
		bool useSelection = (!selection.IsEmpty() && selection.GetLayer() == layerIndex);
		TileRef target = layer.GetTile(start.X, start.Y);
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();

		// Scanned without the stack growing with the area, so a fill of a whole layer is no problem
		BitArray visited(ValueInit, std::size_t(width) * height);
		SmallVector<Vector2i, 0> pending;
		pending.push_back(start);
		std::int32_t count = 0;
		while (!pending.empty() && count < MaxFillTiles) {
			Vector2i cell = pending.back();
			pending.pop_back();
			std::size_t index = std::size_t(cell.Y) * width + cell.X;
			if (visited[index] || layer.Tiles[index] != target || (useSelection && !selection.Contains(cell.X, cell.Y))) {
				continue;
			}
			visited.set(index);
			count++;

			TileRef tile;
			if (stamp.GetPatternTile(cell.X, cell.Y, start, tile)) {
				PaintCell(cell, tile);
			}
			if (cell.X > 0) pending.push_back(Vector2i(cell.X - 1, cell.Y));
			if (cell.X < width - 1) pending.push_back(Vector2i(cell.X + 1, cell.Y));
			if (cell.Y > 0) pending.push_back(Vector2i(cell.X, cell.Y - 1));
			if (cell.Y < height - 1) pending.push_back(Vector2i(cell.X, cell.Y + 1));
		}
		EndStroke();
	}

	void FillTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		if (hoverValid) {
			LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
			DrawStampGhost(painter, view, TileStamp::FromTile(_host.GetStamp().IsEmpty() ? TileRef{} : _host.GetStamp().Tiles[0]), hover.Cell, Vector2i(), 0.5f);
			DrawCellsOutline(painter, view, hover.Cell, hover.Cell, UiTheme::Cursor);
		}
	}

	StringView FillTool::GetHint() const
	{
		return _("Click to fill an area of the same tiles, right-click to erase it");
	}

	EyedropperTool::EyedropperTool(IToolHost& host)
		: EditorTool(host), _dragging(false)
	{
	}

	EditorToolType EyedropperTool::GetType() const
	{
		return EditorToolType::Eyedropper;
	}

	void EyedropperTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button == MouseButton::Left) {
			_dragging = true;
			_start = _end = pointer.Cell;
		}
	}

	void EyedropperTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragging) {
			_end = pointer.Cell;
		}
	}

	void EyedropperTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (!_dragging) {
			return;
		}
		_dragging = false;

		std::int32_t layerIndex = _host.GetCurrentLayer();
		const DocumentLayer& layer = _host.GetDocument().Layers[layerIndex];
		LayerView view = _host.GetLayerView(layerIndex);
		Recti rect = MakeRect(_start, _end);

		TileStamp stamp;
		stamp.Width = rect.W;
		stamp.Height = rect.H;
		for (std::int32_t y = rect.Y; y < rect.Y + rect.H; y++) {
			for (std::int32_t x = rect.X; x < rect.X + rect.W; x++) {
				Vector2i tile;
				if (view.Wrap(Vector2i(x, y), tile)) {
					stamp.Tiles.push_back(layer.GetTile(tile.X, tile.Y));
					stamp.Present.push_back(true);
				} else {
					stamp.Tiles.push_back(TileRef{});
					stamp.Present.push_back(false);
				}
			}
		}
		_host.SetStamp(std::move(stamp));
	}

	void EyedropperTool::Cancel()
	{
		_dragging = false;
	}

	bool EyedropperTool::IsBusy() const
	{
		return _dragging;
	}

	void EyedropperTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		if (_dragging) {
			DrawCellsOutline(painter, view, _start, _end, UiTheme::Accent);
		} else if (hoverValid) {
			DrawCellsOutline(painter, view, hover.Cell, hover.Cell, UiTheme::Accent);
		}
	}

	StringView EyedropperTool::GetHint() const
	{
		return _("Click to pick a tile, drag to pick an area");
	}

	SelectTool::SelectTool(IToolHost& host)
		: EditorTool(host), _dragging(false), _add(false)
	{
	}

	EditorToolType SelectTool::GetType() const
	{
		return EditorToolType::Select;
	}

	void SelectTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button == MouseButton::Left) {
			_dragging = true;
			_add = (pointer.Modifiers & KeyMod::Shift) != 0;
			_start = _end = pointer.Cell;
		}
	}

	void SelectTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragging) {
			_end = pointer.Cell;
		}
	}

	void SelectTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (!_dragging) {
			return;
		}
		_dragging = false;

		TileSelection& selection = _host.GetSelection();
		if (_start == _end && !_add && !selection.IsEmpty()) {
			// A click without dragging only ends the selection
			selection.Clear();
			return;
		}

		std::int32_t layerIndex = _host.GetCurrentLayer();
		const DocumentLayer& layer = _host.GetDocument().Layers[layerIndex];
		selection.Select(layerIndex, Vector2i(layer.GetWidth(), layer.GetHeight()), MakeRect(_start, _end), _add);
	}

	void SelectTool::Cancel()
	{
		_dragging = false;
	}

	bool SelectTool::IsBusy() const
	{
		return _dragging;
	}

	void SelectTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		if (_dragging) {
			DrawCellsOutline(painter, view, _start, _end, UiTheme::Accent);
		} else if (hoverValid) {
			DrawCellsOutline(painter, view, hover.Cell, hover.Cell, UiTheme::Cursor);
		}
	}

	StringView SelectTool::GetHint() const
	{
		return _("Drag to select tiles, hold Shift to add to the selection");
	}

	WandTool::WandTool(IToolHost& host)
		: EditorTool(host)
	{
	}

	EditorToolType WandTool::GetType() const
	{
		return EditorToolType::Wand;
	}

	void WandTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button != MouseButton::Left) {
			return;
		}

		std::int32_t layerIndex = _host.GetCurrentLayer();
		LayerView view = _host.GetLayerView(layerIndex);
		Vector2i start;
		if (!view.Wrap(pointer.Cell, start)) {
			return;
		}

		const DocumentLayer& layer = _host.GetDocument().Layers[layerIndex];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		TileRef target = layer.GetTile(start.X, start.Y);

		BitArray mask(ValueInit, std::size_t(width) * height);
		SmallVector<Vector2i, 0> pending;
		pending.push_back(start);
		while (!pending.empty()) {
			Vector2i cell = pending.back();
			pending.pop_back();
			std::size_t index = std::size_t(cell.Y) * width + cell.X;
			if (mask[index] || layer.Tiles[index] != target) {
				continue;
			}
			mask.set(index);
			if (cell.X > 0) pending.push_back(Vector2i(cell.X - 1, cell.Y));
			if (cell.X < width - 1) pending.push_back(Vector2i(cell.X + 1, cell.Y));
			if (cell.Y > 0) pending.push_back(Vector2i(cell.X, cell.Y - 1));
			if (cell.Y < height - 1) pending.push_back(Vector2i(cell.X, cell.Y + 1));
		}

		bool add = (pointer.Modifiers & KeyMod::Shift) != 0;
		_host.GetSelection().Select(layerIndex, Vector2i(width, height), mask, add);
	}

	StringView WandTool::GetHint() const
	{
		return _("Click to select an area of the same tiles, hold Shift to add to the selection");
	}

	HandTool::HandTool(IToolHost& host)
		: EditorTool(host), _dragging(false)
	{
	}

	EditorToolType HandTool::GetType() const
	{
		return EditorToolType::Hand;
	}

	void HandTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (EditorCamera* camera = _host.GetCamera()) {
			_dragging = true;
			camera->BeginDrag(pointer.Screen);
		}
	}

	void HandTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragging) {
			if (EditorCamera* camera = _host.GetCamera()) {
				camera->DragTo(pointer.Screen);
			}
		}
	}

	void HandTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (_dragging) {
			_dragging = false;
			if (EditorCamera* camera = _host.GetCamera()) {
				camera->EndDrag();
			}
		}
	}

	void HandTool::Cancel()
	{
		if (_dragging) {
			_dragging = false;
			if (EditorCamera* camera = _host.GetCamera()) {
				camera->EndDrag();
			}
		}
	}

	bool HandTool::IsBusy() const
	{
		return _dragging;
	}

	StringView HandTool::GetHint() const
	{
		return _("Drag to move the view");
	}

	ToolManager::ToolManager(IToolHost& host)
		: _active(EditorToolType::Brush), _chosen(EditorToolType::Brush), _temporary(false)
	{
		_tools[std::int32_t(EditorToolType::Brush)] = std::make_unique<BrushTool>(host, false);
		_tools[std::int32_t(EditorToolType::Eraser)] = std::make_unique<BrushTool>(host, true);
		_tools[std::int32_t(EditorToolType::Rectangle)] = std::make_unique<RectangleTool>(host);
		_tools[std::int32_t(EditorToolType::Fill)] = std::make_unique<FillTool>(host);
		_tools[std::int32_t(EditorToolType::Eyedropper)] = std::make_unique<EyedropperTool>(host);
		_tools[std::int32_t(EditorToolType::Select)] = std::make_unique<SelectTool>(host);
		_tools[std::int32_t(EditorToolType::Wand)] = std::make_unique<WandTool>(host);
		_tools[std::int32_t(EditorToolType::Hand)] = std::make_unique<HandTool>(host);
		_tools[std::int32_t(EditorToolType::Event)] = std::make_unique<EventTool>(host);
		_tools[std::int32_t(EditorToolType::Measure)] = std::make_unique<MeasureTool>(host);
		_tools[std::int32_t(EditorToolType::Path)] = std::make_unique<PathTool>(host);
		_tools[std::int32_t(EditorToolType::Smart)] = std::make_unique<SmartBrushTool>(host);
		_tools[std::int32_t(EditorToolType::Ground)] = std::make_unique<GroundLineTool>(host);
		_tools[std::int32_t(EditorToolType::TilePixels)] = std::make_unique<TilePixelTool>(host);
	}

	ToolManager::~ToolManager()
	{
	}

	EditorTool* ToolManager::GetActive() const
	{
		return _tools[std::int32_t(_active)].get();
	}

	void ToolManager::SetActive(EditorToolType type)
	{
		if (_active != type) {
			GetActive()->OnDeactivate();
			_active = type;
			GetActive()->OnActivate();
		}
		_chosen = type;
		_temporary = false;
	}

	void ToolManager::BeginTemporary(EditorToolType type)
	{
		if (_temporary || _active == type || GetActive()->IsBusy()) {
			return;
		}
		GetActive()->OnDeactivate();
		_active = type;
		_temporary = true;
		GetActive()->OnActivate();
	}

	void ToolManager::EndTemporary(EditorToolType type)
	{
		if (!_temporary || _active != type) {
			return;
		}
		if (GetActive()->IsBusy()) {
			// Finished when the tool is done with what it's doing, which is the next release
			return;
		}
		GetActive()->OnDeactivate();
		_active = _chosen;
		_temporary = false;
		GetActive()->OnActivate();
	}

	EditorTool* ToolManager::Get(EditorToolType type) const
	{
		return _tools[std::int32_t(type)].get();
	}
}

#endif
