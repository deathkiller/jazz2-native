#include "TilePixelTool.h"

#if defined(WITH_EDITOR)

#include "../Document/LevelDocument.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t TileSize = Formats::LevelFile::TileSize;
		// Cells outlined at most, a view zoomed far out isn't searched
		constexpr std::int32_t MaxOutlinedCells = 40000;
	}

	TilePixelTool::TilePixelTool(IToolHost& host)
		: EditorTool(host), _tileId(-1), _painting(false), _button(MouseButton::Left), _lastPixel(-1, -1), _lastCell(INT32_MIN, INT32_MIN)
	{
	}

	EditorToolType TilePixelTool::GetType() const
	{
		return EditorToolType::TilePixels;
	}

	void TilePixelTool::SetTarget(std::int32_t tileId, PaintFunction&& paint)
	{
		_tileId = tileId;
		_paint = Death::move(paint);
		_painting = false;
	}

	bool TilePixelTool::ShowsTile(std::int32_t layer, Vector2i tile, std::uint8_t& flags) const
	{
		LevelDocument& document = _host.GetDocument();
		if (layer < 0 || layer >= std::int32_t(document.Layers.size())) {
			return false;
		}
		const DocumentLayer& documentLayer = document.Layers[layer];
		if (!documentLayer.Contains(tile.X, tile.Y)) {
			return false;
		}
		const TileRef& ref = documentLayer.GetTile(tile.X, tile.Y);
		if (ref.IsEmpty() || ref.IsAnimated() || document.ToTileId(ref) != _tileId) {
			return false;
		}
		flags = ref.Flags;
		return true;
	}

	bool TilePixelTool::GetPixel(const ToolPointer& pointer, Vector2i& pixel) const
	{
		if (_tileId < 0) {
			return false;
		}
		std::int32_t layer = _host.GetCurrentLayer();
		LayerView view = _host.GetLayerView(layer);
		Vector2i tile;
		std::uint8_t flags;
		if (!view.Wrap(pointer.Cell, tile) || !ShowsTile(layer, tile, flags)) {
			return false;
		}
		// Where in the cell, then where in the tile, which the cell may show flipped
		std::int32_t x = std::clamp<std::int32_t>(std::int32_t(std::floor(pointer.World.X - view.Origin.X)) - pointer.Cell.X * TileSize, 0, TileSize - 1);
		std::int32_t y = std::clamp<std::int32_t>(std::int32_t(std::floor(pointer.World.Y - view.Origin.Y)) - pointer.Cell.Y * TileSize, 0, TileSize - 1);
		if ((flags & Formats::TileRecord::FlipX) != 0) {
			x = TileSize - 1 - x;
		}
		if ((flags & Formats::TileRecord::FlipY) != 0) {
			y = TileSize - 1 - y;
		}
		pixel = Vector2i(x, y);
		return true;
	}

	void TilePixelTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (!_paint || (pointer.Button != MouseButton::Left && pointer.Button != MouseButton::Right)) {
			return;
		}
		Vector2i pixel;
		if (!GetPixel(pointer, pixel)) {
			_host.ShowMessage(_x("editor", "Paint over a tile outlined in the level, it shows the tile being edited"));
			return;
		}
		_painting = true;
		_button = pointer.Button;
		_lastPixel = pixel;
		_lastCell = pointer.Cell;
		_paint(pixel, pixel, _button, false);
	}

	void TilePixelTool::OnPointerMove(const ToolPointer& pointer)
	{
		if (!_painting || !_paint) {
			return;
		}
		Vector2i pixel;
		if (!GetPixel(pointer, pixel) || (pixel == _lastPixel && pointer.Cell == _lastCell)) {
			return;
		}
		// Within the same cell the pixels between are painted too, another place of the tile starts anew
		Vector2i from = (pointer.Cell == _lastCell ? _lastPixel : pixel);
		_lastPixel = pixel;
		_lastCell = pointer.Cell;
		_paint(from, pixel, _button, true);
	}

	void TilePixelTool::OnPointerUp(const ToolPointer& pointer)
	{
		_painting = false;
	}

	void TilePixelTool::Cancel()
	{
		_painting = false;
	}

	bool TilePixelTool::IsBusy() const
	{
		return _painting;
	}

	void TilePixelTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		if (_tileId < 0) {
			return;
		}
		std::int32_t layer = _host.GetCurrentLayer();
		LayerView view = _host.GetLayerView(layer);
		const Rectf& clip = painter.GetClip();
		if (clip.W <= 0.0f || clip.H <= 0.0f) {
			return;
		}
		Vector2i from = view.WorldToCell(view.ScreenToWorld(Vector2f(clip.X, clip.Y)));
		Vector2i to = view.WorldToCell(view.ScreenToWorld(Vector2f(clip.X + clip.W, clip.Y + clip.H)));
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		if (std::int64_t(to.X - from.X + 1) * std::int64_t(to.Y - from.Y + 1) <= MaxOutlinedCells) {
			// Every place the tile is seen at, which all change together
			Colorf color = UiTheme::Accent;
			color.A = 0.7f;
			for (std::int32_t y = from.Y; y <= to.Y; y++) {
				for (std::int32_t x = from.X; x <= to.X; x++) {
					Vector2i tile;
					std::uint8_t flags;
					if (view.Wrap(Vector2i(x, y), tile) && ShowsTile(layer, tile, flags)) {
						painter.Frame(view.CellToScreen(Vector2i(x, y)), color, thickness, UiLayer::Overlay);
					}
				}
			}
		}

		// The pixel that would be painted
		Vector2i pixel;
		if (hoverValid && GetPixel(hover, pixel)) {
			Vector2f world = view.Origin + Vector2f(std::floor(hover.World.X - view.Origin.X), std::floor(hover.World.Y - view.Origin.Y));
			Vector2f a = view.WorldToScreen(world);
			Vector2f b = view.WorldToScreen(world + Vector2f(1.0f, 1.0f));
			Rectf rect(std::round(a.X), std::round(a.Y), std::max(std::round(b.X - a.X), 1.0f), std::max(std::round(b.Y - a.Y), 1.0f));
			if (rect.W >= 3.0f) {
				painter.Frame(rect, UiTheme::Cursor, thickness, UiLayer::Overlay);
			} else {
				painter.FillRect(rect, UiTheme::Cursor, UiLayer::Overlay);
			}
		}
	}

	StringView TilePixelTool::GetHint() const
	{
		return _x("editor", "Paint the pixels of the tile right in the level, everywhere it's seen changes - right-click erases");
	}
}

#endif
