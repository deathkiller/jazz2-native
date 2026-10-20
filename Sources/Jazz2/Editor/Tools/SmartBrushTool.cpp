#include "SmartBrushTool.h"

#if defined(WITH_EDITOR)

#include "../Common/ToolMath.h"
#include "../Runtime/TileDrawing.h"
#include "../Smart/SmartTileModel.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		using ToolMath::HashCell;
		using ToolMath::NeighborOffsets;
		using ToolMath::ToCellKey;
		using ToolMath::WalkLine;
	}

	SmartBrushTool::SmartBrushTool(IToolHost& host)
		: StrokeTool(host), _erase(false), _paintLayer(-1), _simulating(false), _ghostValid(false)
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
		if (!model.CanPick()) {
			// A cell painted now would keep the tile that only stands for any solid one
			if (model.IsReady()) {
				_host.ShowMessage(_("No level uses this tile set yet, so there's nothing to learn the tiles from"));
			} else {
				_host.ShowMessage(_f("Learning how the tiles fit from the levels of the tile set - {}%", std::int32_t(model.GetProgress() * 100.0f)));
			}
			return;
		}
		_erase = (pointer.Button == MouseButton::Right);
		if (!BeginStroke(_erase ? _("Erase ground") : _("Paint ground"))) {
			return;
		}
		_pending.clear();
		_lastCell = pointer.Cell;
		_paintLayer = _strokeLayer;
		_paintView = _strokeView;
		PaintAt(pointer.Cell);
	}

	void SmartBrushTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (_stroke == nullptr || pointer.Cell == _lastCell) {
			return;
		}
		Vector2i last = _lastCell;
		WalkLine(_lastCell, pointer.Cell, [this, last](Vector2i cell) {
			// The cell the line starts at is painted already
			if (cell != last) {
				PaintAt(cell);
			}
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
		std::int32_t layerIndex = _host.GetCurrentLayer();
		LayerView view = _host.GetLayerView(layerIndex);
		SmartTileModel& model = _host.GetSmartTiles();
		Tiles::TileMap* tileMap = _host.GetTileMap();
		if (_stroke == nullptr && tileMap != nullptr && model.CanPick() && _host.IsLayerEditable(layerIndex)) {
			// What a click would paint there, the cell and the neighbors that change with it, worked out without the level
			// changing; the right button would erase, which is shown only while the button is held. It's worked out again
			// only when anything it depends on changes, not every frame.
			GhostKey key;
			MakeGhostKey(layerIndex, view, hover.Cell, model.GetRevision(), key);
			if (!_ghostValid || !(key == _ghostKey)) {
				_pending.clear();
				_ghost.clear();
				_simulating = true;
				_erase = false;
				_paintLayer = layerIndex;
				_paintView = view;
				PaintAt(hover.Cell);
				_simulating = false;
				_pending.clear();
				_ghostKey = key;
				_ghostValid = true;
			}
			const LevelDocument& document = _host.GetDocument();
			for (const GhostTile& ghost : _ghost) {
				Rectf rect = view.CellToScreen(ghost.Cell);
				if (ghost.Tile.IsEmpty()) {
					painter.FillRect(rect, Colorf(0.9f, 0.25f, 0.2f, 0.25f), UiLayer::Content);
				} else {
					DrawTile(painter, *tileMap, document, ghost.Tile, ghost.Tile.Flags, rect, 0.6f);
				}
			}
		}
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

	bool SmartBrushTool::GhostKey::operator==(const GhostKey& other) const
	{
		if (Cell != other.Cell || Layer != other.Layer || Size != other.Size || RepeatX != other.RepeatX || RepeatY != other.RepeatY ||
			ModelRevision != other.ModelRevision || Changeable != other.Changeable) {
			return false;
		}
		for (std::int32_t i = 0; i < KeyTiles; i++) {
			if (!(Tiles[i] == other.Tiles[i])) {
				return false;
			}
		}
		return true;
	}

	void SmartBrushTool::MakeGhostKey(std::int32_t layerIndex, const LayerView& view, Vector2i cell, std::uint64_t modelRevision, GhostKey& key) const
	{
		key.Cell = cell;
		key.Layer = layerIndex;
		key.Size = view.Size;
		key.RepeatX = view.RepeatX;
		key.RepeatY = view.RepeatY;
		key.ModelRevision = modelRevision;
		key.Changeable = 0;
		for (TileRef& tile : key.Tiles) {
			tile = TileRef{};
		}
		const LevelDocument& document = _host.GetDocument();
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		const TileSelection& selection = _host.GetSelection();
		bool limited = (!selection.IsEmpty() && selection.GetLayer() == layerIndex);
		// PaintAt() changes the cell and its neighbors, by the tiles around each of them, so two cells around the cell
		for (std::int32_t dy = -KeyRadius; dy <= KeyRadius; dy++) {
			for (std::int32_t dx = -KeyRadius; dx <= KeyRadius; dx++) {
				std::int32_t i = (dy + KeyRadius) * (KeyRadius * 2 + 1) + (dx + KeyRadius);
				Vector2i tile;
				if (!view.Wrap(Vector2i(cell.X + dx, cell.Y + dy), tile)) {
					// Outside of the layer, unlike any tile
					key.Tiles[i] = TileRef{ UINT16_MAX, TileRef::Raw, UINT8_MAX };
					continue;
				}
				key.Tiles[i] = layer.GetTile(tile.X, tile.Y);
				if (std::abs(dx) <= 1 && std::abs(dy) <= 1 && (!limited || selection.Contains(tile.X, tile.Y))) {
					key.Changeable |= (1u << i);
				}
			}
		}
	}

	TileRef SmartBrushTool::GetTile(const DocumentLayer& layer, Vector2i tile) const
	{
		auto it = _pending.find(ToCellKey(tile));
		return (it != _pending.end() ? it->second : layer.GetTile(tile.X, tile.Y));
	}

	bool SmartBrushTool::IsSolid(const DocumentLayer& layer, const LayerView& view, Vector2i cell) const
	{
		// Outside of the layer counts as solid, as the levels were learned
		Vector2i tile;
		if (!view.Wrap(cell, tile)) {
			return true;
		}
		return ToolMath::IsSolidTile(GetTile(layer, tile));
	}

	void SmartBrushTool::SetTile(Vector2i tile, Vector2i cell, const TileRef& value)
	{
		_pending[ToCellKey(tile)] = value;
		if (_simulating) {
			// Only shown, the last tile chosen for a cell is the one shown
			auto it = std::find_if(_ghost.begin(), _ghost.end(), [cell](const GhostTile& ghost) { return ghost.Cell == cell; });
			if (it != _ghost.end()) {
				it->Tile = value;
			} else {
				_ghost.push_back(GhostTile{ cell, value });
			}
			return;
		}
		_stroke->Set(tile.X, tile.Y, value);
		_host.PreviewTile(_strokeLayer, tile.X, tile.Y, value);
	}

	void SmartBrushTool::PaintAt(Vector2i cell)
	{
		if (_stroke == nullptr && !_simulating) {
			return;
		}
		LevelDocument& document = _host.GetDocument();
		if (_paintLayer < 0 || _paintLayer >= std::int32_t(document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = document.Layers[_paintLayer];
		const LayerView& view = _paintView;
		const TileSelection& selection = _host.GetSelection();
		bool limited = (!selection.IsEmpty() && selection.GetLayer() == _paintLayer);
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
			SetTile(painted, cell, TileRef{});
		} else if (!wasSolid) {
			SetTile(painted, cell, TileRef{ 1, TileRef::DefaultTileSet, 0 });
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
				for (std::int32_t i = 0; i < 8; i++) {
					if (IsSolid(layer, view, Vector2i(target.X + NeighborOffsets[i][0], target.Y + NeighborOffsets[i][1]))) {
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
					SetTile(tile, target, picked);
				}
			}
		}
	}
}

#endif
