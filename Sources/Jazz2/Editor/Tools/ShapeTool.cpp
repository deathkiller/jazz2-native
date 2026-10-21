#include "ShapeTool.h"

#if defined(WITH_EDITOR)

#include "../Common/ToolMath.h"
#include "../Input/ActionIds.h"
#include "../Runtime/TileDrawing.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		// Ghosts of more tiles than this are only outlined, it would be slow to draw them all
		constexpr std::int32_t MaxGhostTiles = 4096;
		// Cells of a shape at most, a drag far out over a layer that repeats doesn't stop the editor
		constexpr std::int64_t MaxShapeCells = 1 << 20;
	}

	ShapeTool::ShapeTool(IToolHost& host)
		: StrokeTool(host), _dragging(false), _erase(false)
	{
	}

	EditorToolType ShapeTool::GetType() const
	{
		return EditorToolType::Shape;
	}

	void ShapeTool::OnDeactivate()
	{
		Cancel();
	}

	void ShapeTool::GetShapeCells(ToolShape shape, Vector2i from, Vector2i to, SmallVectorImpl<Vector2i>& cells)
	{
		cells.clear();
		std::int32_t x1 = std::min(from.X, to.X), y1 = std::min(from.Y, to.Y);
		std::int32_t x2 = std::max(from.X, to.X), y2 = std::max(from.Y, to.Y);
		std::int64_t width = std::int64_t(x2) - x1 + 1, height = std::int64_t(y2) - y1 + 1;
		// Every shape has a cell in each row and column at least, the cells it has are counted before they're made
		if (std::max(width, height) > MaxShapeCells) {
			return;
		}
		switch (shape) {
			case ToolShape::Line: {
				ToolMath::WalkLine(from, to, [&cells](Vector2i cell) {
					cells.push_back(cell);
				});
				break;
			}
			case ToolShape::Rectangle: {
				if ((width == 1 || height == 1 ? width * height : 2 * (width + height) - 4) > MaxShapeCells) {
					return;
				}
				for (std::int32_t x = x1; x <= x2; x++) {
					cells.push_back(Vector2i(x, y1));
					if (y2 != y1) {
						cells.push_back(Vector2i(x, y2));
					}
				}
				for (std::int32_t y = y1 + 1; y < y2; y++) {
					cells.push_back(Vector2i(x1, y));
					if (x2 != x1) {
						cells.push_back(Vector2i(x2, y));
					}
				}
				break;
			}
			case ToolShape::Ellipse:
			case ToolShape::FilledEllipse: {
				// The cells whose middle is inside, half a cell larger, so a narrow ellipse still has all its rows and columns
				float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
				float rx = (x2 - x1) * 0.5f + 0.5f, ry = (y2 - y1) * 0.5f + 0.5f;
				auto isInside = [&](std::int32_t x, std::int32_t y) {
					if (x < x1 || x > x2 || y < y1 || y > y2) {
						return false;
					}
					float dx = (x - cx) / rx, dy = (y - cy) / ry;
					return (dx * dx + dy * dy <= 1.0f);
				};
				// The cells inside a row are a single span around the middle, found from where the edge is and checked
				// against the test above, so the shape is the same as if every cell was tested
				struct Span
				{
					std::int32_t From;
					std::int32_t To;
				};
				SmallVector<Span, 0> spans;
				spans.resize(std::size_t(height));
				std::int32_t middle = std::int32_t(std::floor(cx));
				std::int64_t total = 0;
				for (std::int32_t y = y1; y <= y2; y++) {
					Span& span = spans[std::size_t(y - y1)];
					if (!isInside(middle, y)) {
						span = Span{ 0, -1 };
						continue;
					}
					float dy = (y - cy) / ry;
					float half = rx * std::sqrt(std::max(0.0f, 1.0f - dy * dy));
					std::int32_t a = std::clamp<std::int32_t>(std::int32_t(std::ceil(cx - half)), x1, middle);
					std::int32_t b = std::clamp<std::int32_t>(std::int32_t(std::floor(cx + half)), middle, x2);
					while (a > x1 && isInside(a - 1, y)) {
						a--;
					}
					while (a < middle && !isInside(a, y)) {
						a++;
					}
					while (b < x2 && isInside(b + 1, y)) {
						b++;
					}
					while (b > middle && !isInside(b, y)) {
						b--;
					}
					span = Span{ a, b };
					total += std::int64_t(b) - a + 1;
				}

				if (shape == ToolShape::FilledEllipse) {
					if (total > MaxShapeCells) {
						return;
					}
					for (std::int32_t y = y1; y <= y2; y++) {
						const Span& span = spans[std::size_t(y - y1)];
						for (std::int32_t x = span.From; x <= span.To; x++) {
							cells.push_back(Vector2i(x, y));
						}
					}
					break;
				}

				// Of the outline only the cells next to the outside, at the sides, so it's a line without gaps: the ends of
				// the span of a row, and its cells the row above or below doesn't have
				Span none{ 0, -1 };
				for (std::int32_t y = y1; y <= y2; y++) {
					const Span& span = spans[std::size_t(y - y1)];
					if (span.From > span.To) {
						continue;
					}
					const Span& above = (y > y1 ? spans[std::size_t(y - 1 - y1)] : none);
					const Span& below = (y < y2 ? spans[std::size_t(y + 1 - y1)] : none);
					auto isOutline = [&](std::int32_t x) {
						return (x == span.From || x == span.To || above.From > above.To || x < above.From || x > above.To ||
							below.From > below.To || x < below.From || x > below.To);
					};
					// The middle of a row is inside the rows next to it, which are spans too, it's skipped at once
					std::int32_t innerFrom = std::max(above.From > above.To ? span.To + 1 : above.From, below.From > below.To ? span.To + 1 : below.From);
					std::int32_t innerTo = std::min(above.From > above.To ? span.From - 1 : above.To, below.From > below.To ? span.From - 1 : below.To);
					for (std::int32_t x = span.From; x <= span.To; x++) {
						if (x > innerFrom && x < innerTo && x > span.From && x < span.To) {
							x = std::min(innerTo, span.To) - 1;
							continue;
						}
						if (isOutline(x)) {
							if (std::int64_t(cells.size()) >= MaxShapeCells) {
								cells.clear();
								return;
							}
							cells.push_back(Vector2i(x, y));
						}
					}
				}
				break;
			}
		}
	}

	Vector2i ShapeTool::Constrain(ToolShape shape, Vector2i from, Vector2i to)
	{
		std::int32_t dx = to.X - from.X, dy = to.Y - from.Y;
		std::int32_t adx = std::abs(dx), ady = std::abs(dy);
		if (shape == ToolShape::Line) {
			// Along the nearest of the 8 directions
			if (adx > ady * 2) {
				return Vector2i(to.X, from.Y);
			}
			if (ady > adx * 2) {
				return Vector2i(from.X, to.Y);
			}
		}
		std::int32_t size = std::max(adx, ady);
		return Vector2i(from.X + (dx < 0 ? -size : size), from.Y + (dy < 0 ? -size : size));
	}

	void ShapeTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button != MouseButton::Left && pointer.Button != MouseButton::Right) {
			return;
		}
		if (!_host.IsLayerEditable(_host.GetCurrentLayer())) {
			_host.ShowMessage(_x("editor", "The layer is locked or hidden"));
			return;
		}
		_dragging = true;
		_erase = (pointer.Button == MouseButton::Right);
		_start = _end = pointer.Cell;
	}

	void ShapeTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragging) {
			_end = ((pointer.Modifiers & KeyMod::Shift) != 0 ? Constrain(GetSettings().Shape, _start, pointer.Cell) : pointer.Cell);
		}
	}

	void ShapeTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (!_dragging) {
			return;
		}
		_dragging = false;
		_end = ((pointer.Modifiers & KeyMod::Shift) != 0 ? Constrain(GetSettings().Shape, _start, pointer.Cell) : pointer.Cell);

		TileStamp eraseStamp = TileStamp::FromTile(TileRef{});
		const TileStamp& stamp = (_erase ? eraseStamp : _host.GetStamp());
		// TRANSLATORS: Names of undo steps in the history, after the shape tool erased or drew a shape of tiles
		if (stamp.IsEmpty() || !BeginStroke(_erase ? _x("editor", "Erase shape") : _x("editor", "Draw shape"), false)) {
			return;
		}
		_strokeOptions = !_erase;
		_strokeOnlyEmpty = (!_erase && GetSettings().OnlyEmpty);
		SmallVector<Vector2i, 0> cells;
		GetShapeCells(GetSettings().Shape, _start, _end, cells);
		for (const Vector2i& cell : cells) {
			PaintStampCell(stamp, cell, _start);
		}
		EndStroke();
	}

	void ShapeTool::Cancel()
	{
		_dragging = false;
		StrokeTool::Cancel();
	}

	bool ShapeTool::IsBusy() const
	{
		return _dragging;
	}

	void ShapeTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		if (!_dragging) {
			if (hoverValid) {
				DrawCellsOutline(painter, view, hover.Cell, hover.Cell, UiTheme::Cursor);
			}
			return;
		}

		SmallVector<Vector2i, 0> cells;
		GetShapeCells(GetSettings().Shape, _start, _end, cells);
		Tiles::TileMap* tileMap = _host.GetTileMap();
		const TileStamp& stamp = _host.GetStamp();
		const LevelDocument& document = _host.GetDocument();
		bool ghosts = (!_erase && tileMap != nullptr && !stamp.IsEmpty() && std::int32_t(cells.size()) <= MaxGhostTiles);
		for (const Vector2i& cell : cells) {
			Rectf rect = view.CellToScreen(cell);
			TileRef tile;
			if (ghosts && stamp.GetPatternTile(cell.X, cell.Y, _start, tile) && !tile.IsEmpty()) {
				DrawTile(painter, *tileMap, document, tile, tile.Flags, rect, 0.7f);
			} else {
				painter.FillRect(rect, _erase ? Colorf(0.9f, 0.25f, 0.2f, 0.3f) : Colorf(1.0f, 1.0f, 1.0f, 0.25f), UiLayer::Content);
			}
		}
		DrawCellsOutline(painter, view, _start, _end, _erase ? UiTheme::Danger : UiTheme::Accent);
	}

	StringView ShapeTool::GetHint() const
	{
		return _x("editor", "Drag to draw the shape, right-drag to erase it - Shift: 45 degrees or square");
	}

	void ShapeTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		const ToolOptionItem shapes[] = {
			// TRANSLATORS: Shapes the shape tool draws
			{ _x("editor", "Line"), EditorIcon::ShapeLine, ActionIds::ShapeLine },
			{ _x("editor", "Rectangle"), EditorIcon::Rectangle, ActionIds::ShapeRectangle },
			{ _x("editor", "Ellipse"), EditorIcon::Ellipse, ActionIds::ShapeEllipse },
			{ _x("editor", "Filled ellipse"), EditorIcon::FilledEllipse, ActionIds::ShapeFilledEllipse },
		};
		// TRANSLATORS: Label of the choice of the shape the shape tool draws
		builder.AddChoice(_x("editor", "Shape"), shapes);
		AddStampOptions(builder);
		AddModifierOptions(builder);
		// TRANSLATORS: Toggle in the toolbar, short for painting only the tiles that are empty
		builder.AddAction(ActionIds::ToolsOnlyEmpty, _x("editor", "Only empty"));
	}
}

#endif
