#include "TileTools.h"

#if defined(WITH_EDITOR)

#include "EventTools.h"
#include "MeasureTool.h"
#include "PathTool.h"
#include "GroundLineTool.h"
#include "TilePixelTool.h"
#include "SmartBrushTool.h"
#include "ShapeTool.h"
#include "../Common/StampClipboard.h"
#include "../Common/ToolMath.h"
#include "../Input/ActionIds.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/TileDrawing.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
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
		constexpr std::size_t MaxFillTiles = 1 << 22;

		using ToolMath::PositiveModulo;
		using ToolMath::WalkLine;

		Recti MakeRect(Vector2i a, Vector2i b)
		{
			std::int32_t x1 = std::min(a.X, b.X), y1 = std::min(a.Y, b.Y);
			std::int32_t x2 = std::max(a.X, b.X), y2 = std::max(a.Y, b.Y);
			return Recti(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
		}

		// Whether a tile is the same as another one, with the flips or without them
		bool IsSameTile(const TileRef& a, const TileRef& b, bool ignoreFlips)
		{
			constexpr std::uint8_t Flips = Formats::TileRecord::FlipX | Formats::TileRecord::FlipY;
			return (ignoreFlips ? (a.Index == b.Index && a.Source == b.Source && (a.Flags & ~Flips) == (b.Flags & ~Flips)) : a == b);
		}

		// Marks the area of the same tiles as the one at the start, joined at the sides and across the edges of a layer that
		// repeats, as it's seen, or with `global` all of them in the layer. A cell is marked when it's found, so the cells
		// still to look around are never more than the area, and a fill of a whole layer is no problem. Returns the number
		// of cells, at most the limit.
		std::size_t FindArea(const DocumentLayer& layer, Vector2i start, bool repeatX, bool repeatY, const TileSelection* selection,
			std::size_t maxCount, BitArray& mask, bool ignoreFlips = false, bool global = false)
		{
			std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
			mask = BitArray(ValueInit, std::size_t(width) * std::size_t(height));
			if (!layer.Contains(start.X, start.Y)) {
				return 0;
			}
			TileRef target = layer.GetTile(start.X, start.Y);
			std::size_t count = 0;
			if (global) {
				for (std::int32_t y = 0; y < height && count < maxCount; y++) {
					for (std::int32_t x = 0; x < width && count < maxCount; x++) {
						std::size_t index = std::size_t(y) * std::size_t(width) + std::size_t(x);
						if (IsSameTile(layer.Tiles[index], target, ignoreFlips) && (selection == nullptr || selection->Contains(x, y))) {
							mask.set(index);
							count++;
						}
					}
				}
				return count;
			}
			SmallVector<Vector2i, 0> pending;
			auto visit = [&](std::int32_t x, std::int32_t y) {
				if (x < 0 || x >= width) {
					if (!repeatX) {
						return;
					}
					x = PositiveModulo(x, width);
				}
				if (y < 0 || y >= height) {
					if (!repeatY) {
						return;
					}
					y = PositiveModulo(y, height);
				}
				std::size_t index = std::size_t(y) * std::size_t(width) + std::size_t(x);
				if (count >= maxCount || mask[index] || !IsSameTile(layer.Tiles[index], target, ignoreFlips) || (selection != nullptr && !selection->Contains(x, y))) {
					return;
				}
				mask.set(index);
				count++;
				pending.push_back(Vector2i(x, y));
			};
			visit(start.X, start.Y);
			while (!pending.empty()) {
				Vector2i cell = pending.back();
				pending.pop_back();
				visit(cell.X - 1, cell.Y);
				visit(cell.X + 1, cell.Y);
				visit(cell.X, cell.Y - 1);
				visit(cell.X, cell.Y + 1);
			}
			return count;
		}
	}

	bool ToolSettings::ResolveTile(const TileStamp& stamp, Vector2i position, Vector2i origin, const TileRef& existing, bool options,
		bool onlyEmpty, TileRef& result) const
	{
		TileRef tile;
		if (!stamp.GetPatternTile(position.X, position.Y, origin, tile)) {
			return false;
		}
		if (onlyEmpty && !existing.IsEmpty()) {
			return false;
		}
		if (!options) {
			result = tile;
			return true;
		}
		if (stamp.Random && !tile.IsEmpty() && (RandomFlipX || RandomFlipY)) {
			// The same flip for the same position, as the choice of the tile
			std::uint32_t h = ToolMath::HashCell(position) ^ (stamp.Seed * 0x9E3779B1u);
			h ^= h >> 13;
			if (RandomFlipX && (h & 0x100) != 0) {
				tile.Flags ^= Formats::TileRecord::FlipX;
			}
			if (RandomFlipY && (h & 0x200) != 0) {
				tile.Flags ^= Formats::TileRecord::FlipY;
			}
		}
		std::uint8_t modifier;
		switch (Modifier) {
			case TileModifierMode::Translucent: modifier = Formats::TileRecord::ModifierTranslucent; break;
			case TileModifierMode::Invisible: modifier = Formats::TileRecord::ModifierInvisible; break;
			case TileModifierMode::Heat: modifier = Formats::TileRecord::ModifierHeat; break;
			default: modifier = 0; break;
		}
		if (ModifierOnly) {
			// The tile there stays, only how it's drawn changes
			if (existing.IsEmpty()) {
				return false;
			}
			std::uint8_t newModifier = (Modifier == TileModifierMode::Keep ? std::uint8_t(tile.Flags & Formats::TileRecord::ModifierMask) : modifier);
			result = existing;
			result.Flags = std::uint8_t((existing.Flags & ~Formats::TileRecord::ModifierMask) | newModifier);
			return true;
		}
		if (Modifier != TileModifierMode::Keep && !tile.IsEmpty()) {
			tile.Flags = std::uint8_t((tile.Flags & ~Formats::TileRecord::ModifierMask) | modifier);
		}
		result = tile;
		return true;
	}

	EditorTool::EditorTool(IToolHost& host)
		: _host(host), _settings(nullptr)
	{
	}

	EditorTool::~EditorTool()
	{
	}

	const ToolSettings& EditorTool::GetSettings() const
	{
		static const ToolSettings Defaults;
		return (_settings != nullptr ? *_settings : Defaults);
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

	bool EditorTool::HasPointerCapture() const
	{
		return IsBusy();
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
					// By where it comes from, a tile past the IDs the level can address yet is still drawn as itself
					DrawTile(painter, *tileMap, document, tile, tile.Flags, rect, alpha);
				}
			}
		}
	}

	void EditorTool::BuildOptions(ToolOptionsBuilder& builder)
	{
	}

	void EditorTool::AddStampOptions(ToolOptionsBuilder& builder)
	{
		builder.AddSeparator();
		builder.AddAction(ActionIds::StampFlipX);
		builder.AddAction(ActionIds::StampFlipY);
		builder.AddAction(ActionIds::StampRotateCCW);
		builder.AddAction(ActionIds::StampRotate);
		builder.AddSeparator();
		builder.AddAction(ActionIds::StampRandom, {}, EditorIcon::Dice);
		builder.AddAction(ActionIds::StampReroll, {}, EditorIcon::Shuffle);
		builder.AddAction(ActionIds::StampRandomFlipX);
		builder.AddAction(ActionIds::StampRandomFlipY);
		builder.AddAction(ActionIds::StampWeights);
	}

	void EditorTool::AddModifierOptions(ToolOptionsBuilder& builder)
	{
		builder.AddSeparator();
		const ToolOptionItem modifiers[] = {
			// TRANSLATORS: The modifier the tiles are painted with, as the tiles of the stamp have it
			{ _x("editor", "As they are"), EditorIcon::None, ActionIds::ToolsModifierKeep },
			{ _x("editor", "Normal"), EditorIcon::None, ActionIds::ToolsModifierNormal },
			{ _x("editor", "Translucent"), EditorIcon::None, ActionIds::ToolsModifierTranslucent },
			{ _x("editor", "Invisible"), EditorIcon::None, ActionIds::ToolsModifierInvisible },
			// TRANSLATORS: The modifier of tiles whose rows of pixels are drawn wavily, as if seen through heat
			{ _x("editor", "Heat"), EditorIcon::None, ActionIds::ToolsModifierHeat },
		};
		// TRANSLATORS: Label of the choice of how the tiles are painted, as they are, translucent, invisible or with heat
		builder.AddChoice(_x("editor", "Tiles"), modifiers, true);
		// TRANSLATORS: Toggle in the toolbar, short for changing only the modifier of the tiles painted over
		builder.AddAction(ActionIds::ToolsModifierOnly, _x("editor", "Only modifier"));
	}

	StrokeTool::StrokeTool(IToolHost& host)
		: EditorTool(host), _strokeLayer(-1), _strokePreview(true), _strokeOptions(true), _strokeOnlyEmpty(false), _lastStamp(nullptr)
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
			if (_strokePreview) {
				_host.RevertPreview(_strokeLayer, area);
			}
		}
	}

	bool StrokeTool::IsBusy() const
	{
		return (_stroke != nullptr);
	}

	bool StrokeTool::BeginStroke(String name, bool preview)
	{
		Cancel();
		std::int32_t layer = _host.GetCurrentLayer();
		if (!_host.IsLayerEditable(layer)) {
			_host.ShowMessage(_x("editor", "The layer is locked or hidden"));
			return false;
		}
		_strokeLayer = layer;
		_strokeView = _host.GetLayerView(layer);
		_strokePreview = preview;
		_strokeOptions = true;
		_strokeOnlyEmpty = false;
		_lastStamp = nullptr;
		_stroke = std::make_unique<SetTilesCommand>(_host.GetDocument(), layer, Death::move(name));
		return true;
	}

	void StrokeTool::PaintStampCell(const TileStamp& stamp, Vector2i cell, Vector2i patternOrigin)
	{
		if (_stroke == nullptr) {
			return;
		}
		Vector2i layerTile;
		if (!_strokeView.Wrap(cell, layerTile)) {
			return;
		}
		const LevelDocument& document = _host.GetDocument();
		if (_strokeLayer < 0 || _strokeLayer >= std::int32_t(document.Layers.size())) {
			return;
		}
		const TileRef& existing = document.Layers[_strokeLayer].GetTile(layerTile.X, layerTile.Y);
		TileRef tile;
		if (GetSettings().ResolveTile(stamp, cell, patternOrigin, existing, _strokeOptions, _strokeOnlyEmpty, tile)) {
			PaintCell(cell, tile);
		}
	}

	void StrokeTool::PaintCell(Vector2i cell, const TileRef& tile)
	{
		if (_stroke == nullptr) {
			return;
		}
		// Only the size and the repeating of the layer are used, they're the same as when the stroke started
		Vector2i layerTile;
		if (!_strokeView.Wrap(cell, layerTile)) {
			return;
		}
		const TileSelection& selection = _host.GetSelection();
		if (!selection.IsEmpty() && selection.GetLayer() == _strokeLayer && !selection.Contains(layerTile.X, layerTile.Y)) {
			return;
		}
		_stroke->Set(layerTile.X, layerTile.Y, tile);
		if (_strokePreview) {
			_host.PreviewTile(_strokeLayer, layerTile.X, layerTile.Y, tile);
		}
	}

	void StrokeTool::PaintStamp(const TileStamp& stamp, Vector2i cell, Vector2i anchor, Vector2i patternOrigin)
	{
		if (stamp.Random) {
			// One of its tiles in the cell
			PaintStampCell(stamp, cell, patternOrigin);
			return;
		}
		// A cell gets the same tile whenever the stamp covers it in the stroke, the tile is chosen by where the cell is.
		// A stroke steps a cell at a time, so only the cells the stamp didn't cover the last time are painted, a large
		// stamp would set all of its tiles again with every step otherwise.
		Recti area(cell.X - anchor.X, cell.Y - anchor.Y, stamp.Width, stamp.Height);
		bool skip = (_lastStamp == &stamp && _lastStampArea.W == area.W && _lastStampArea.H == area.H && _lastStampOrigin == patternOrigin);
		const Recti& last = _lastStampArea;
		for (std::int32_t y = area.Y; y < area.Y + area.H; y++) {
			bool rowCovered = (skip && y >= last.Y && y < last.Y + last.H);
			for (std::int32_t x = area.X; x < area.X + area.W; x++) {
				if (rowCovered && x >= last.X && x < last.X + last.W) {
					x = last.X + last.W - 1;
					continue;
				}
				PaintStampCell(stamp, Vector2i(x, y), patternOrigin);
			}
		}
		_lastStamp = &stamp;
		_lastStampArea = area;
		_lastStampOrigin = patternOrigin;
	}

	void StrokeTool::EndStroke()
	{
		if (_stroke == nullptr) {
			return;
		}
		std::unique_ptr<SetTilesCommand> stroke = Death::move(_stroke);
		stroke->Seal();
		if (!stroke->IsEmpty()) {
			_host.Execute(Death::move(stroke));
		}
	}

	BrushTool::BrushTool(IToolHost& host, bool erase)
		: StrokeTool(host), _erase(erase), _lineMode(false)
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
		// With Shift a straight line, shown while it's dragged and painted when it's let go
		bool lineMode = ((pointer.Modifiers & KeyMod::Shift) != 0);
		// TRANSLATORS: Names of undo steps in the history, after the brush erased or painted tiles
		if (stamp.IsEmpty() || !BeginStroke(_erase ? _x("editor", "Erase") : _x("editor", "Paint"), !lineMode)) {
			return;
		}
		_strokeOptions = !_erase;
		_lineMode = lineMode;
		_lastCell = _lineStart = _lineEnd = pointer.Cell;
		_patternOrigin = pointer.Cell - GetAnchor();
		if (!_lineMode) {
			PaintStamp(stamp, pointer.Cell, GetAnchor(), _patternOrigin);
		}
	}

	void BrushTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_stroke == nullptr) {
			return;
		}
		if (_lineMode) {
			_lineEnd = pointer.Cell;
			return;
		}
		if (pointer.Cell == _lastCell) {
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
		if (pointer.Button != MouseButton::Left) {
			return;
		}
		if (_lineMode && _stroke != nullptr) {
			const TileStamp& stamp = GetBrushStamp();
			Vector2i anchor = GetAnchor();
			WalkLine(_lineStart, pointer.Cell, [&](Vector2i cell) {
				PaintStamp(stamp, cell, anchor, _patternOrigin);
			});
		}
		_lineMode = false;
		EndStroke();
	}

	void BrushTool::Cancel()
	{
		_lineMode = false;
		StrokeTool::Cancel();
	}

	void BrushTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		if (!hoverValid) {
			return;
		}
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		const TileStamp& stamp = GetBrushStamp();
		Vector2i anchor = GetAnchor();
		if (_lineMode && _stroke != nullptr) {
			// The line as it would be painted
			std::int32_t cells = std::max(std::abs(_lineEnd.X - _lineStart.X), std::abs(_lineEnd.Y - _lineStart.Y)) + 1;
			bool ghosts = (!stamp.Random && std::int64_t(cells) * stamp.Width * stamp.Height <= MaxGhostTiles);
			WalkLine(_lineStart, _lineEnd, [&](Vector2i cell) {
				if (ghosts) {
					DrawStampGhost(painter, view, stamp, cell, anchor, 0.55f);
				} else {
					Vector2i from(cell.X - anchor.X, cell.Y - anchor.Y);
					DrawCellsOutline(painter, view, from, Vector2i(from.X + (stamp.Random ? 0 : stamp.Width - 1), from.Y + (stamp.Random ? 0 : stamp.Height - 1)),
						_erase ? UiTheme::Danger : UiTheme::Accent);
				}
			});
			return;
		}
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
		return (_erase ? _x("editor", "Click or drag to erase tiles - Shift+drag: a straight line") : _x("editor", "Click or drag to paint, right-click to pick a tile - Shift+drag: a straight line"));
	}

	void BrushTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		// Erasing paints nothing the options would change
		if (!_erase) {
			AddStampOptions(builder);
			AddModifierOptions(builder);
		}
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
			_host.ShowMessage(_x("editor", "The layer is locked or hidden"));
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

		TileStamp eraseStamp = TileStamp::FromTile(TileRef{});
		const TileStamp& stamp = (_erase ? eraseStamp : _host.GetStamp());
		// TRANSLATORS: Names of undo steps in the history, after the rectangle tool erased or filled a rectangle of tiles
		if (stamp.IsEmpty() || !BeginStroke(_erase ? _x("editor", "Erase rectangle") : _x("editor", "Fill rectangle"), false)) {
			return;
		}
		_strokeOptions = !_erase;
		_strokeOnlyEmpty = (!_erase && GetSettings().OnlyEmpty);
		Recti rect = MakeRect(_start, _end);
		for (std::int32_t y = rect.Y; y < rect.Y + rect.H; y++) {
			for (std::int32_t x = rect.X; x < rect.X + rect.W; x++) {
				PaintStampCell(stamp, Vector2i(x, y), Vector2i(rect.X, rect.Y));
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
						DrawTile(painter, *tileMap, document, tile, tile.Flags, view.CellToScreen(Vector2i(x, y)), 0.7f);
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
		return _x("editor", "Drag to fill a rectangle, right-drag to erase one");
	}

	void RectangleTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		AddStampOptions(builder);
		AddModifierOptions(builder);
		// TRANSLATORS: Toggle in the toolbar, short for painting only the tiles that are empty
		builder.AddAction(ActionIds::ToolsOnlyEmpty, _x("editor", "Only empty"));
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
		// TRANSLATORS: Names of undo steps in the history, after the fill tool erased or filled an area of tiles, "Fill" in the same verb form as "Erase"
		if (stamp.IsEmpty() || !BeginStroke(erase ? _x("editor", "Erase area") : _x("editor", "Fill"), false)) {
			return;
		}

		const ToolSettings& settings = GetSettings();
		const DocumentLayer& layer = _host.GetDocument().Layers[layerIndex];
		const TileSelection& selection = _host.GetSelection();
		bool useSelection = (!selection.IsEmpty() && selection.GetLayer() == layerIndex);
		BitArray area;
		std::size_t count = FindArea(layer, start, view.RepeatX && settings.AcrossSeams, view.RepeatY && settings.AcrossSeams,
			useSelection ? &selection : nullptr, MaxFillTiles, area, settings.IgnoreFlips);
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		for (std::int32_t y = 0; y < height; y++) {
			for (std::int32_t x = 0; x < width; x++) {
				TileRef tile;
				if (area[std::size_t(y) * std::size_t(width) + std::size_t(x)] &&
					settings.ResolveTile(stamp, Vector2i(x, y), start, layer.GetTile(x, y), !erase, !erase && settings.OnlyEmpty, tile)) {
					_stroke->Set(x, y, tile);
				}
			}
		}
		EndStroke();
		if (count >= MaxFillTiles) {
			_host.ShowMessage(_x("editor", "The area is too large, only a part of it was filled"));
		}
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
		return _x("editor", "Click to fill an area of the same tiles, right-click to erase it");
	}

	void FillTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		AddStampOptions(builder);
		AddModifierOptions(builder);
		// TRANSLATORS: Toggle in the toolbar, short for filling only the tiles that are empty
		builder.AddAction(ActionIds::ToolsOnlyEmpty, _x("editor", "Only empty"));
		builder.AddSeparator();
		AddAreaOptions(builder);
	}

	void EditorTool::AddAreaOptions(ToolOptionsBuilder& builder)
	{
		// TRANSLATORS: Toggle in the toolbar, short for going on across the edges of a layer that repeats
		builder.AddAction(ActionIds::ToolsAcrossSeams, _x("editor", "Across edges"));
		// TRANSLATORS: Toggle in the toolbar, short for taking tiles flipped as the same tiles
		builder.AddAction(ActionIds::ToolsIgnoreFlips, _x("editor", "Flipped too"));
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
		// As large as a stamp pasted can be, a larger one would make every step of a stroke paint too many tiles
		if (rect.W > StampClipboard::MaxStampSide || rect.H > StampClipboard::MaxStampSide ||
			std::int64_t(rect.W) * std::int64_t(rect.H) > StampClipboard::MaxStampTiles) {
			_host.ShowMessage(_fx("editor", "The area is too large to pick, a brush can have {} tiles at most", StampClipboard::MaxStampTiles));
			return;
		}

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
		_host.SetStamp(Death::move(stamp));
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
		return _x("editor", "Click to pick a tile, drag to pick an area");
	}

	void DrawTileModifiers(UiPainter& painter, const DocumentLayer& layer, const LayerView& view, const Rectf& viewRect)
	{
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(viewRect.X, viewRect.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(viewRect.X + viewRect.W, viewRect.Y + viewRect.H)));
		// Too small to tell apart, or too many to draw
		if (view.Scale * TileSize < 6.0f || std::int64_t(to.X - from.X + 1) * std::int64_t(to.Y - from.Y + 1) > 40000) {
			return;
		}
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		for (std::int32_t y = from.Y; y <= to.Y; y++) {
			for (std::int32_t x = from.X; x <= to.X; x++) {
				Vector2i tile;
				if (!view.Wrap(Vector2i(x, y), tile)) {
					continue;
				}
				std::uint8_t modifier = std::uint8_t(layer.GetTile(tile.X, tile.Y).Flags & Formats::TileRecord::ModifierMask);
				if (modifier == 0) {
					continue;
				}
				Rectf rect = view.CellToScreen(Vector2i(x, y));
				if (modifier == Formats::TileRecord::ModifierTranslucent) {
					// Translucent, framed
					Colorf color = UiTheme::OverlayInfo;
					color.A = 0.22f;
					painter.FillRect(rect, color, UiLayer::Fill);
					color.A = 0.8f;
					painter.Frame(rect, color, thickness, UiLayer::Overlay);
				} else {
					// Invisible, solid but not drawn, crossed out
					Colorf color = UiTheme::OverlayCaution;
					color.A = 0.2f;
					painter.FillRect(rect, color, UiLayer::Fill);
					color.A = 0.85f;
					painter.Line(Vector2f(rect.X, rect.Y), Vector2f(rect.X + rect.W, rect.Y + rect.H), thickness, color, UiLayer::Overlay);
					painter.Line(Vector2f(rect.X + rect.W, rect.Y), Vector2f(rect.X, rect.Y + rect.H), thickness, color, UiLayer::Overlay);
				}
			}
		}
	}

	void DrawSelectionEdge(UiPainter& painter, float x, float y, float length, bool horizontal, float animTime)
	{
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		float dash = 2.0f * thickness;
		float period = dash * 2.0f;
		float offset = std::fmod(animTime * dash * 4.0f, period);
		Rectf line = (horizontal ? Rectf(x, y, length, thickness) : Rectf(x, y, thickness, length));
		painter.FillRect(line, Colorf(0.1f, 0.08f, 0.2f, 1.0f), UiLayer::Content);
		float from = (horizontal ? x : y);
		float across = (horizontal ? y : x);
		float to = from + length;
		// The first dash that starts at or before the edge
		float start = std::floor((from + across - offset) / period) * period + offset - across;
		for (float pos = start; pos < to; pos += period) {
			float a = std::max(pos, from), b = std::min(pos + dash, to);
			if (b > a) {
				painter.FillRect(horizontal ? Rectf(a, y, b - a, thickness) : Rectf(x, a, thickness, b - a), UiTheme::Accent, UiLayer::Overlay);
			}
		}
	}

	bool MoveSelectedTiles(IToolHost& host, Vector2i offset)
	{
		TileSelection& selection = host.GetSelection();
		std::int32_t layerIndex = selection.GetLayer();
		LevelDocument& document = host.GetDocument();
		if (selection.IsEmpty() || offset == Vector2i() || layerIndex != host.GetCurrentLayer() || layerIndex < 0 ||
			layerIndex >= std::int32_t(document.Layers.size())) {
			return false;
		}
		if (!host.IsLayerEditable(layerIndex)) {
			host.ShowMessage(_x("editor", "The layer is locked or hidden"));
			return false;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		if (selection.GetLayerSize() != Vector2i(width, height)) {
			selection.Clear();
			return false;
		}
		bool repeatX = !layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatX) != 0;
		bool repeatY = !layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatY) != 0;

		// Where each selected tile goes, around a layer that repeats
		const Recti& bounds = selection.GetBounds();
		BitArray target(ValueInit, std::size_t(width) * std::size_t(height));
		auto move = [&](std::int32_t x, std::int32_t y, Vector2i& to) {
			to = Vector2i(x + offset.X, y + offset.Y);
			if (repeatX) {
				to.X = PositiveModulo(to.X, width);
			}
			if (repeatY) {
				to.Y = PositiveModulo(to.Y, height);
			}
			return layer.Contains(to.X, to.Y);
		};
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				Vector2i to;
				if (!selection.Contains(x, y)) {
					continue;
				}
				if (!move(x, y, to)) {
					host.ShowMessage(_x("editor", "Tiles can't be moved out of the layer"));
					return false;
				}
				target.set(std::size_t(to.Y) * std::size_t(width) + std::size_t(to.X));
			}
		}

		// The tiles left behind are empty, unless others are moved there
		// TRANSLATORS: Name of an undo step in the history, after selected tiles were moved
		String name = _x("editor", "Move Tiles");
		host.BeginChanges(name);
		auto command = std::make_unique<SetTilesCommand>(document, layerIndex, name);
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (selection.Contains(x, y) && !target[std::size_t(y) * std::size_t(width) + std::size_t(x)]) {
					command->Set(x, y, TileRef{});
				}
			}
		}
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				Vector2i to;
				if (selection.Contains(x, y) && move(x, y, to)) {
					command->Set(to.X, to.Y, layer.GetTile(x, y));
				}
			}
		}
		host.Execute(Death::move(command));
		// On the sprite layer, which never repeats, the events on the tiles go with them
		if (layerIndex == document.GetSpriteLayerIndex()) {
			CarrySelectedEvents(host, selection, name, [offset](Vector2i cell) { return cell + offset; },
				[offset](Vector2i pixel) { return pixel + offset * 32; }, nullptr);
		}
		host.EndChanges();
		selection.Select(layerIndex, Vector2i(width, height), target, false);
		return true;
	}

	bool CarrySelectedEvents(IToolHost& host, const TileSelection& selection, const String& name, Function<Vector2i(Vector2i)>&& mapCell,
		Function<Vector2i(Vector2i)>&& mapPixel, Function<void(Formats::EventRecord&)>&& transform)
	{
		LevelDocument& document = host.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		Vector2i gridSize = document.GetEventGridSize();
		if (spriteLayer < 0 || selection.IsEmpty() || selection.GetLayer() != spriteLayer || gridSize.X <= 0 || gridSize.Y <= 0) {
			return false;
		}

		// All taken away first, then placed, so events moved onto the places of others moved aren't lost
		struct Moved
		{
			Vector2i From;
			Vector2i To;
			Formats::EventRecord Event;
		};
		SmallVector<Moved, 0> moved;
		for (const DocumentEvent& event : document.Events) {
			Vector2i pos(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)));
			if (!selection.Contains(pos.X, pos.Y)) {
				continue;
			}
			Formats::EventRecord record = event.Record;
			if (transform) {
				transform(record);
			}
			moved.push_back(Moved{ pos, mapCell(pos), record });
		}
		bool changed = false;
		if (!moved.empty()) {
			auto events = std::make_unique<SetEventsCommand>(document, name);
			for (const Moved& item : moved) {
				events->Set(item.From.X, item.From.Y, LevelDocument::GetEmptyEvent());
			}
			for (const Moved& item : moved) {
				events->Set(item.To.X, item.To.Y, item.Event);
			}
			host.Execute(Death::move(events));
			changed = true;
		}

		// Those off the grid on the selected tiles, by their position in pixels, kept within the level
		SmallVector<Formats::OffGridEventRecord, 0> offGrid = CopyOffGridEvents(document);
		bool anyOffGrid = false;
		for (Formats::OffGridEventRecord& record : offGrid) {
			if (!selection.Contains(std::int32_t(record.X / 32), std::int32_t(record.Y / 32))) {
				continue;
			}
			Vector2i to = mapPixel(Vector2i(std::int32_t(record.X), std::int32_t(record.Y)));
			record.X = std::uint32_t(std::clamp<std::int64_t>(to.X, 0, std::int64_t(gridSize.X) * 32 - 1));
			record.Y = std::uint32_t(std::clamp<std::int64_t>(to.Y, 0, std::int64_t(gridSize.Y) * 32 - 1));
			if (transform) {
				transform(record.Event);
			}
			anyOffGrid = true;
		}
		if (anyOffGrid) {
			host.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(offGrid), name));
			changed = true;
		}
		return changed;
	}

	SelectTool::SelectTool(IToolHost& host)
		: EditorTool(host), _dragging(false), _add(false), _moving(false)
	{
	}

	EditorToolType SelectTool::GetType() const
	{
		return EditorToolType::Select;
	}

	void SelectTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button != MouseButton::Left) {
			return;
		}
		_start = _end = pointer.Cell;
		// Dragged from a selected tile, the selected tiles are moved
		TileSelection& selection = _host.GetSelection();
		std::int32_t layerIndex = _host.GetCurrentLayer();
		LayerView view = _host.GetLayerView(layerIndex);
		Vector2i tile;
		if ((pointer.Modifiers & (KeyMod::Shift | KeyMod::Ctrl)) == 0 && !selection.IsEmpty() && selection.GetLayer() == layerIndex &&
			view.Wrap(pointer.Cell, tile) && selection.Contains(tile.X, tile.Y)) {
			const DocumentLayer& layer = _host.GetDocument().Layers[layerIndex];
			const Recti& bounds = selection.GetBounds();
			_moveStamp = TileStamp::FromLayer(layer, bounds);
			for (std::int32_t y = 0; y < bounds.H; y++) {
				for (std::int32_t x = 0; x < bounds.W; x++) {
					if (!selection.Contains(bounds.X + x, bounds.Y + y)) {
						_moveStamp.Present[std::size_t(y) * bounds.W + x] = false;
					}
				}
			}
			// Where the selected tiles are in the view, the copy of a layer that repeats the pointer is over
			_moveOrigin = Vector2i(bounds.X + (pointer.Cell.X - tile.X), bounds.Y + (pointer.Cell.Y - tile.Y));
			_moving = true;
			return;
		}
		_dragging = true;
		_add = (pointer.Modifiers & KeyMod::Shift) != 0;
	}

	void SelectTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_dragging || _moving) {
			_end = pointer.Cell;
		}
	}

	void SelectTool::OnPointerUp(const ToolPointer& pointer)
	{
		if (_moving) {
			_moving = false;
			_moveStamp = TileStamp();
			MoveSelectedTiles(_host, pointer.Cell - _start);
			return;
		}
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
		const Vector2i layerSize(layer.GetWidth(), layer.GetHeight());
		const LayerView view = _host.GetLayerView(layerIndex);
		const Recti rect = MakeRect(_start, _end);

		// The rectangle is of the copy of a layer that repeats it was dragged over, the selection is of the layer itself (and
		// so of every copy), so on an axis the layer repeats in it's wrapped into the layer - into two parts if it goes over
		// the end of a copy into the next one, all of it if it's as long as the layer
		struct Span { std::int32_t Start, Length; };
		auto wrapSpan = [](std::int32_t start, std::int32_t length, std::int32_t size, bool repeats, Span* spans) {
			if (!repeats || size <= 0) {
				spans[0] = { start, length };
				return 1;
			}
			if (length >= size) {
				spans[0] = { 0, size };
				return 1;
			}
			start = PositiveModulo(start, size);
			if (start + length <= size) {
				spans[0] = { start, length };
				return 1;
			}
			spans[0] = { start, size - start };
			spans[1] = { 0, start + length - size };
			return 2;
		};
		Span spansX[2], spansY[2];
		std::int32_t countX = wrapSpan(rect.X, rect.W, layerSize.X, view.RepeatX, spansX);
		std::int32_t countY = wrapSpan(rect.Y, rect.H, layerSize.Y, view.RepeatY, spansY);
		bool add = _add;
		for (std::int32_t j = 0; j < countY; j++) {
			for (std::int32_t i = 0; i < countX; i++) {
				selection.Select(layerIndex, layerSize, Recti(spansX[i].Start, spansY[j].Start, spansX[i].Length, spansY[j].Length), add);
				// The other parts add to the first one
				add = true;
			}
		}
	}

	void SelectTool::Cancel()
	{
		_dragging = false;
		_moving = false;
		_moveStamp = TileStamp();
	}

	bool SelectTool::IsBusy() const
	{
		return (_dragging || _moving);
	}

	void SelectTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		if (_moving) {
			// The selected tiles where they would be moved
			Vector2i offset = _end - _start;
			Vector2i at = _moveOrigin + offset;
			DrawStampGhost(painter, view, _moveStamp, at, Vector2i(), 0.75f);
			DrawCellsOutline(painter, view, at, Vector2i(at.X + _moveStamp.Width - 1, at.Y + _moveStamp.Height - 1), UiTheme::Accent);
			// Arrows to all four sides by the pointer, as a window's cursor of moving (the surface leaves them out for a finger)
			if (UiSurface* surface = _host.GetSurface()) {
				surface->SetCursorOverlay(UiCursorOverlay::Move);
			}
		} else if (_dragging) {
			DrawCellsOutline(painter, view, _start, _end, UiTheme::Accent);
		} else if (hoverValid) {
			DrawCellsOutline(painter, view, hover.Cell, hover.Cell, UiTheme::Cursor);
		}
	}

	StringView SelectTool::GetHint() const
	{
		return _x("editor", "Drag to select tiles - Shift: add to the selection - drag the selection to move its tiles, Shift+arrows nudge them");
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

		const ToolSettings& settings = GetSettings();
		const DocumentLayer& layer = _host.GetDocument().Layers[layerIndex];
		// With Ctrl, or as chosen, all such tiles of the layer
		bool global = (settings.WandGlobal || (pointer.Modifiers & KeyMod::Ctrl) != 0);
		BitArray mask;
		FindArea(layer, start, view.RepeatX && settings.AcrossSeams, view.RepeatY && settings.AcrossSeams, nullptr, SIZE_MAX, mask,
			settings.IgnoreFlips, global);

		bool add = (pointer.Modifiers & KeyMod::Shift) != 0;
		_host.GetSelection().Select(layerIndex, Vector2i(layer.GetWidth(), layer.GetHeight()), mask, add);
	}

	StringView WandTool::GetHint() const
	{
		return _x("editor", "Click to select an area of the same tiles - Shift: add to the selection - Ctrl: all such tiles of the layer");
	}

	void WandTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		// TRANSLATORS: Toggle in the toolbar, short for the magic wand selecting all such tiles of the layer
		builder.AddAction(ActionIds::ToolsWandGlobal, _x("editor", "Whole layer"));
		AddAreaOptions(builder);
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
		return _x("editor", "Drag to move the view");
	}

	ToolManager::ToolManager(IToolHost& host)
		// Selecting first, a level just opened isn't changed by the first click or tap into it
		: _active(EditorToolType::Select), _chosen(EditorToolType::Select), _temporary(false)
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
		_tools[std::int32_t(EditorToolType::Shape)] = std::make_unique<ShapeTool>(host);
		for (auto& tool : _tools) {
			tool->SetSettings(&_settings);
		}
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
		if (_temporary || _active == type || GetActive()->HasPointerCapture()) {
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
