#include "SmartBrushTool.h"

#if defined(WITH_EDITOR)

#include "../Smart/SmartTileModel.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		std::uint64_t ToKey(Vector2i tile)
		{
			return (std::uint64_t(std::uint32_t(tile.Y)) << 32) | std::uint32_t(tile.X);
		}

		std::uint32_t HashCell(Vector2i tile)
		{
			std::uint32_t h = std::uint32_t(tile.X) * 0x8DA6B343u ^ std::uint32_t(tile.Y) * 0xD8163841u;
			h ^= h >> 15;
			return h * 0x2C1B3C6Du;
		}

		// Walks the cells of a line, so a fast stroke has no gaps
		template<typename Func>
		void WalkCells(Vector2i from, Vector2i to, Func&& func)
		{
			std::int32_t steps = std::max(std::abs(to.X - from.X), std::abs(to.Y - from.Y));
			for (std::int32_t i = 1; i <= steps; i++) {
				float t = float(i) / float(steps);
				func(Vector2i(std::int32_t(std::round(from.X + (to.X - from.X) * t)), std::int32_t(std::round(from.Y + (to.Y - from.Y) * t))));
			}
		}
	}

	SmartBrushTool::SmartBrushTool(IToolHost& host)
		: StrokeTool(host), _erase(false)
	{
	}

	EditorToolType SmartBrushTool::GetType() const
	{
		return EditorToolType::Smart;
	}

	void SmartBrushTool::OnActivate()
	{
		// Starts learning the tile set, it takes a while
		_host.GetSmartTiles();
	}

	void SmartBrushTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (pointer.Button != MouseButton::Left && pointer.Button != MouseButton::Right) {
			return;
		}
		SmartTileModel& model = _host.GetSmartTiles();
		if (model.IsReady() && model.GetLevelCount() == 0) {
			_host.ShowMessage(_("No level uses this tile set yet, so there's nothing to learn the tiles from"));
			return;
		}
		_erase = (pointer.Button == MouseButton::Right);
		if (!BeginStroke(_erase ? _("Erase ground") : _("Paint ground"))) {
			return;
		}
		_pending.clear();
		_lastCell = pointer.Cell;
		PaintAt(pointer.Cell);
	}

	void SmartBrushTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_stroke == nullptr || pointer.Cell == _lastCell) {
			return;
		}
		WalkCells(_lastCell, pointer.Cell, [this](Vector2i cell) {
			PaintAt(cell);
		});
		_lastCell = pointer.Cell;
	}

	void SmartBrushTool::OnPointerUp(const ToolPointer& pointer)
	{
		if ((pointer.Button == MouseButton::Left && !_erase) || (pointer.Button == MouseButton::Right && _erase)) {
			EndStroke();
			_pending.clear();
		}
	}

	void SmartBrushTool::Cancel()
	{
		StrokeTool::Cancel();
		_pending.clear();
	}

	void SmartBrushTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		if (!hoverValid) {
			return;
		}
		LayerView view = _host.GetLayerView(_host.GetCurrentLayer());
		DrawCellsOutline(painter, view, hover.Cell, hover.Cell, (_stroke != nullptr && _erase) ? UiTheme::Danger : UiTheme::Cursor);
	}

	StringView SmartBrushTool::GetHint() const
	{
		SmartTileModel& model = _host.GetSmartTiles();
		if (!model.IsReady()) {
			_hint = _f("Learning how the tiles fit from the levels of the tile set - {}%", std::int32_t(model.GetProgress() * 100.0f));
			return _hint;
		}
		return _("Click or drag to paint ground, the right button makes it empty - the tiles are chosen by the tiles around");
	}

	TileRef SmartBrushTool::GetTile(const DocumentLayer& layer, Vector2i tile) const
	{
		auto it = _pending.find(ToKey(tile));
		return (it != _pending.end() ? it->second : layer.GetTile(tile.X, tile.Y));
	}

	bool SmartBrushTool::IsSolid(const DocumentLayer& layer, const LayerView& view, Vector2i cell) const
	{
		// Outside of the layer counts as solid, as the levels were learned
		Vector2i tile;
		if (!view.Wrap(cell, tile)) {
			return true;
		}
		TileRef value = GetTile(layer, tile);
		return (!value.IsEmpty() && (value.Flags & Formats::TileRecord::ModifierMask) != Formats::TileRecord::ModifierInvisible);
	}

	void SmartBrushTool::SetTile(Vector2i tile, const TileRef& value)
	{
		_pending[ToKey(tile)] = value;
		_stroke->Set(tile.X, tile.Y, value);
		_host.PreviewTile(_strokeLayer, tile.X, tile.Y, value);
	}

	void SmartBrushTool::PaintAt(Vector2i cell)
	{
		if (_stroke == nullptr) {
			return;
		}
		LevelDocument& document = _host.GetDocument();
		if (_strokeLayer < 0 || _strokeLayer >= std::int32_t(document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = document.Layers[_strokeLayer];
		LayerView view = _host.GetLayerView(_strokeLayer);
		const TileSelection& selection = _host.GetSelection();
		bool limited = (!selection.IsEmpty() && selection.GetLayer() == _strokeLayer);
		auto canChange = [&](Vector2i tile) {
			return (!limited || selection.Contains(tile.X, tile.Y));
		};

		Vector2i painted;
		if (!view.Wrap(cell, painted) || !canChange(painted)) {
			return;
		}
		SmartTileModel& model = _host.GetSmartTiles();

		// The cell is solid or empty now, any solid tile stands for it until its tile is chosen below
		bool wasSolid = IsSolid(layer, view, cell);
		if (_erase) {
			if (!wasSolid && GetTile(layer, painted).IsEmpty()) {
				return;
			}
			SetTile(painted, TileRef{});
		} else if (!wasSolid) {
			SetTile(painted, TileRef{ 1, TileRef::DefaultTileSet, 0 });
		}

		// The cell and its neighbors get tiles by the cells around them, left to right and top to bottom, so the tiles
		// already chosen on the left and above are taken into account
		for (std::int32_t dy = -1; dy <= 1; dy++) {
			for (std::int32_t dx = -1; dx <= 1; dx++) {
				Vector2i target(cell.X + dx, cell.Y + dy);
				Vector2i tile;
				if (!view.Wrap(target, tile) || !canChange(tile) || !IsSolid(layer, view, target)) {
					continue;
				}
				TileRef current = GetTile(layer, tile);
				bool isPainted = (dx == 0 && dy == 0);
				// Neighbors made by hand of other tiles stay as they are
				if (!isPainted && !SmartTileModel::IsModelTile(current)) {
					continue;
				}
				std::uint8_t neighbors = 0;
				static const std::int32_t Offsets[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
				for (std::int32_t i = 0; i < 8; i++) {
					if (IsSolid(layer, view, Vector2i(target.X + Offsets[i][0], target.Y + Offsets[i][1]))) {
						neighbors |= std::uint8_t(1 << i);
					}
				}
				auto neighborTile = [&](std::int32_t ox, std::int32_t oy, TileRef& result) {
					Vector2i n;
					if (!view.Wrap(Vector2i(target.X + ox, target.Y + oy), n)) {
						return false;
					}
					result = GetTile(layer, n);
					return SmartTileModel::IsModelTile(result);
				};
				// Tiles around stay as they are if the levels use them with their neighbors as they are now
				if (!isPainted && model.Fits(neighbors, layer.IsSpriteLayer(), current)) {
					continue;
				}
				TileRef left, up, right, down;
				bool hasLeft = neighborTile(-1, 0, left), hasUp = neighborTile(0, -1, up);
				bool hasRight = neighborTile(1, 0, right), hasDown = neighborTile(0, 1, down);
				TileRef picked;
				if (model.Pick(neighbors, layer.IsSpriteLayer(), hasLeft ? &left : nullptr, hasUp ? &up : nullptr,
					hasRight ? &right : nullptr, hasDown ? &down : nullptr, HashCell(tile), picked, isPainted ? nullptr : &current) && picked != current) {
					SetTile(tile, picked);
				}
			}
		}
	}
}

#endif
