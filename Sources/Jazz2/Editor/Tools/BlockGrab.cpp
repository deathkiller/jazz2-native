#include "BlockGrab.h"

#if defined(WITH_EDITOR)

#include "EditorContext.h"
#include "EventTools.h"
#include "TileTools.h"
#include "../Common/StampClipboard.h"
#include "../Document/Commands.h"
#include "../Input/ActionRegistry.h"
#include "../UI/UiSurface.h"
#include "../UI/UiTheme.h"
#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		Recti MakeRect(Vector2i a, Vector2i b)
		{
			std::int32_t x1 = std::min(a.X, b.X), y1 = std::min(a.Y, b.Y);
			std::int32_t x2 = std::max(a.X, b.X), y2 = std::max(a.Y, b.Y);
			return Recti(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
		}

		// Picking a block means painting with it, as picking tiles in the picker does
		void PaintWithStamp(IEditorContext& session)
		{
			EditorToolType tool = session.GetTools().GetChosenType();
			if (tool != EditorToolType::Brush && tool != EditorToolType::Rectangle && tool != EditorToolType::Fill) {
				session.GetTools().SetActive(EditorToolType::Brush);
			}
		}

		// Tells what was grabbed and that Shift paints the empty tiles too
		void ShowGrabbedMessage(IEditorContext& session, const TileStamp& stamp)
		{
			std::int32_t events = std::int32_t(stamp.OffGridEvents.size());
			for (const Formats::EventRecord& event : stamp.Events) {
				if (!LevelDocument::IsEmptyEvent(event)) {
					events++;
				}
			}
			if (events > 0) {
				// TRANSLATORS: Status message after a block of tiles was grabbed as the brush with B, the first two {} are its width and height in tiles, the last one the number of events on it
				session.ShowMessage(_fnx("editor", "Grabbed {}x{} tiles with {} event - Shift+click paints the empty tiles too",
					"Grabbed {}x{} tiles with {} events - Shift+click paints the empty tiles too", events, stamp.Width, stamp.Height, events));
			} else {
				// TRANSLATORS: Status message after a block of tiles was grabbed as the brush with B, {} are its width and height in tiles
				session.ShowMessage(_fx("editor", "Grabbed {}x{} tiles - Shift+click paints the empty tiles too", stamp.Width, stamp.Height));
			}
		}
	}

	BlockGrab::BlockGrab(IEditorContext& session)
		: _session(session), _zone(Zone::None), _mode(Mode::Replace), _startTile(0), _layer(-1), _source(0)
	{
	}

	bool BlockGrab::PressLevel(Mode mode)
	{
		ToolPointer hover;
		if (!_session.GetHoverPointer(hover)) {
			_session.ShowMessage(_x("editor", "Point at a corner of the block in the level"));
			return false;
		}
		std::int32_t layer = _session.GetCurrentLayer();
		if (layer < 0 || layer >= std::int32_t(_session.GetDocument().Layers.size())) {
			return false;
		}
		// A corner marked on another layer, or in the picker, is forgotten, the block starts again here
		if (_zone == Zone::Level && _layer == layer) {
			FinishLevel(hover.Cell);
			return true;
		}
		_zone = Zone::Level;
		_mode = mode;
		_layer = layer;
		_start = hover.Cell;
		ShowStartMessage();
		return true;
	}

	bool BlockGrab::PressTileSet(std::uint8_t source, std::int32_t columns, Vector2i cell, Vector2i& start)
	{
		if (columns <= 0) {
			return false;
		}
		if (_zone == Zone::TileSet && _source == source) {
			// By the index of the tile, the picker may show the tiles in rows of another length meanwhile
			start = Vector2i(_startTile % columns, _startTile / columns);
			Cancel();
			return true;
		}
		_zone = Zone::TileSet;
		_source = source;
		_startTile = cell.Y * columns + cell.X;
		ShowStartMessage();
		return false;
	}

	bool BlockGrab::IsActive() const
	{
		return (_zone != Zone::None);
	}

	bool BlockGrab::GetTileSetCorner(std::uint8_t source, std::int32_t columns, Vector2i& corner) const
	{
		if (_zone != Zone::TileSet || _source != source || columns <= 0) {
			return false;
		}
		corner = Vector2i(_startTile % columns, _startTile / columns);
		return true;
	}

	void BlockGrab::Cancel()
	{
		_zone = Zone::None;
	}

	void BlockGrab::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		if (_zone != Zone::Level || !hoverValid || _layer != _session.GetCurrentLayer()) {
			return;
		}
		LayerView view = _session.GetLayerView(_layer);
		Recti cells = MakeRect(_start, hover.Cell);
		Rectf topLeft = view.CellToScreen(Vector2i(cells.X, cells.Y));
		Rectf bottomRight = view.CellToScreen(Vector2i(cells.X + cells.W - 1, cells.Y + cells.H - 1));
		Rectf rect(topLeft.X, topLeft.Y, bottomRight.X + bottomRight.W - topLeft.X, bottomRight.Y + bottomRight.H - topLeft.Y);
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		painter.FillRect(rect, UiTheme::Selection, UiLayer::Content);
		// A dark edge under a light one, so it can be seen over anything, as the outlines of the tools
		painter.Frame(Rectf(rect.X - thickness, rect.Y - thickness, rect.W + thickness * 2.0f, rect.H + thickness * 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.5f), thickness, UiLayer::Overlay);
		painter.Frame(rect, _mode == Mode::Subtract ? UiTheme::Danger : UiTheme::Accent, thickness, UiLayer::Overlay);
		if (UiSurface* surface = _session.GetSurface()) {
			// TRANSLATORS: Shown by the pointer while a block of tiles is being grabbed with B, its width and height in tiles
			surface->SetPointerTooltip(_fx("editor", "{} x {}", cells.W, cells.H));
		}
	}

	TileStamp BlockGrab::MakeStamp(const LevelDocument& document, std::int32_t layerIndex, const LayerView& view, const Recti& cells,
		const TileSelection* selection)
	{
		TileStamp stamp;
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size()) || cells.W <= 0 || cells.H <= 0) {
			return stamp;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		// Events of the sprite layer only, all of them, an empty one too erases where it's painted
		bool events = (layerIndex == document.GetSpriteLayerIndex());
		Vector2i gridSize = document.GetEventGridSize();
		stamp.Width = cells.W;
		stamp.Height = cells.H;
		stamp.EmptyIsTransparent = true;
		std::size_t count = std::size_t(cells.W) * std::size_t(cells.H);
		stamp.Tiles.reserve(count);
		stamp.Present.reserve(count);
		if (events) {
			stamp.Events.reserve(count);
		}
		for (std::int32_t y = cells.Y; y < cells.Y + cells.H; y++) {
			for (std::int32_t x = cells.X; x < cells.X + cells.W; x++) {
				Vector2i tile;
				bool present = (view.Wrap(Vector2i(x, y), tile) && layer.Contains(tile.X, tile.Y) &&
					(selection == nullptr || selection->Contains(tile.X, tile.Y)));
				stamp.Tiles.push_back(present ? layer.GetTile(tile.X, tile.Y) : TileRef{});
				stamp.Present.push_back(present);
				if (events) {
					bool inGrid = (present && tile.X < gridSize.X && tile.Y < gridSize.Y);
					stamp.Events.push_back(inGrid ? document.GetEvent(tile.X, tile.Y) : LevelDocument::GetEmptyEvent());
				}
			}
		}
		// Those off the grid on the tiles grabbed, by their positions in pixels from the corner, the sprite layer never repeats
		if (events) {
			for (const Formats::OffGridEventRecord& record : document.OffGridEvents) {
				Vector2i tile(std::int32_t(record.X / 32), std::int32_t(record.Y / 32));
				if (tile.X < cells.X || tile.Y < cells.Y || tile.X >= cells.X + cells.W || tile.Y >= cells.Y + cells.H ||
					!stamp.IsPresent(tile.X - cells.X, tile.Y - cells.Y)) {
					continue;
				}
				TileStamp::OffGridEvent& item = stamp.OffGridEvents.emplace_back();
				item.Offset = Vector2i(std::int32_t(record.X) - cells.X * 32, std::int32_t(record.Y) - cells.Y * 32);
				item.Event = record.Event;
			}
		}
		return stamp;
	}

	bool BlockGrab::GrabSelection(IEditorContext& session, bool cut)
	{
		LevelDocument& document = session.GetDocument();
		TileSelection& selection = session.GetSelection();
		std::int32_t layerIndex = session.GetCurrentLayer();
		if (selection.IsEmpty() || selection.GetLayer() != layerIndex || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			session.ShowMessage(_x("editor", "Select tiles of the layer first"));
			return false;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		if (selection.GetLayerSize() != Vector2i(layer.GetWidth(), layer.GetHeight())) {
			selection.Clear();
			return false;
		}
		if (cut && !session.IsLayerEditable(layerIndex)) {
			session.ShowMessage(_x("editor", "The layer is locked or hidden"));
			return false;
		}
		const Recti& bounds = selection.GetBounds();
		if (std::int64_t(bounds.W) * std::int64_t(bounds.H) > StampClipboard::MaxStampTiles) {
			session.ShowMessage(_fx("editor", "The area is too large to pick, a brush can have {} tiles at most", StampClipboard::MaxStampTiles));
			return false;
		}
		TileStamp stamp = MakeStamp(document, layerIndex, session.GetLayerView(layerIndex), bounds, &selection);

		if (cut) {
			// TRANSLATORS: Name of an undo step in the history, the selected tiles cut into the brush, with the events on them
			String name = _x("editor", "Cut into Brush");
			session.BeginChanges(name);
			auto tiles = std::make_unique<SetTilesCommand>(document, layerIndex, name);
			auto events = std::make_unique<SetEventsCommand>(document, name);
			bool sprite = stamp.HasEvents();
			for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
				for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
					if (selection.Contains(x, y)) {
						tiles->Set(x, y, TileRef{});
						if (sprite) {
							events->Set(x, y, LevelDocument::GetEmptyEvent());
						}
					}
				}
			}
			tiles->Seal();
			events->Seal();
			if (!tiles->IsEmpty()) {
				session.Execute(Death::move(tiles));
			}
			if (!events->IsEmpty()) {
				session.Execute(Death::move(events));
			}
			if (!stamp.OffGridEvents.empty()) {
				// Those off the grid on the selected tiles, the selection of events would lose track of them
				SmallVector<Formats::OffGridEventRecord, 0> kept;
				for (const Formats::OffGridEventRecord& record : document.OffGridEvents) {
					if (!selection.Contains(std::int32_t(record.X / 32), std::int32_t(record.Y / 32))) {
						kept.push_back(record);
					}
				}
				session.GetEventSelection().Clear();
				session.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(kept), name));
			}
			session.EndChanges();
		}

		// The stamp paints anywhere, not only into the selection
		selection.Clear();
		ShowGrabbedMessage(session, stamp);
		session.SetStamp(Death::move(stamp));
		PaintWithStamp(session);
		return true;
	}

	void BlockGrab::FinishLevel(Vector2i end)
	{
		_zone = Zone::None;
		LevelDocument& document = _session.GetDocument();
		if (_layer < 0 || _layer >= std::int32_t(document.Layers.size())) {
			return;
		}
		Recti cells = MakeRect(_start, end);
		// As large as a stamp pasted can be, a larger one would make every step of a stroke paint too many tiles
		if (cells.W > StampClipboard::MaxStampSide || cells.H > StampClipboard::MaxStampSide ||
			std::int64_t(cells.W) * std::int64_t(cells.H) > StampClipboard::MaxStampTiles) {
			_session.ShowMessage(_fx("editor", "The area is too large to pick, a brush can have {} tiles at most", StampClipboard::MaxStampTiles));
			return;
		}

		LayerView view = _session.GetLayerView(_layer);
		TileSelection& selection = _session.GetSelection();
		TileStamp stamp;
		if (_mode == Mode::Replace) {
			stamp = MakeStamp(document, _layer, view, cells);
		} else {
			// The selection of the layer changed by the rectangle, as the select tool would, nothing is selected on the layer
			// if the selection is of another one
			const DocumentLayer& layer = document.Layers[_layer];
			Vector2i layerSize(layer.GetWidth(), layer.GetHeight());
			if (selection.GetLayer() != _layer || selection.GetLayerSize() != layerSize) {
				selection.Clear();
			}
			Recti parts[4];
			std::int32_t count = WrapViewRect(cells, layerSize, view, parts);
			for (std::int32_t i = 0; i < count; i++) {
				if (_mode == Mode::Add) {
					selection.Select(_layer, layerSize, parts[i], true);
				} else {
					selection.Deselect(parts[i]);
				}
			}
			if (selection.IsEmpty()) {
				_session.ShowMessage(_x("editor", "Nothing is selected to grab"));
				return;
			}
			const Recti& bounds = selection.GetBounds();
			if (std::int64_t(bounds.W) * std::int64_t(bounds.H) > StampClipboard::MaxStampTiles) {
				_session.ShowMessage(_fx("editor", "The area is too large to pick, a brush can have {} tiles at most", StampClipboard::MaxStampTiles));
				return;
			}
			stamp = MakeStamp(document, _layer, view, bounds, &selection);
		}
		if (stamp.IsEmpty() || std::find(stamp.Present.begin(), stamp.Present.end(), true) == stamp.Present.end()) {
			_session.ShowMessage(_x("editor", "The block is outside of the layer"));
			return;
		}

		// The stamp paints anywhere, not only into the selection
		selection.Clear();
		ShowGrabbedMessage(_session, stamp);
		_session.SetStamp(Death::move(stamp));
		PaintWithStamp(_session);
	}

	void BlockGrab::ShowStartMessage()
	{
		EditorAction* action = _session.GetActions().Find(ActionIds::EditGrabBlock);
		String key = (action != nullptr ? action->Key.ToString() : String());
		if (key.empty()) {
			_session.ShowMessage(_x("editor", "Again at the other corner grabs the block, Esc cancels"));
		} else {
			// TRANSLATORS: Status message after the first corner of a block of tiles was marked, {} is the keyboard shortcut of grabbing a block, such as B
			_session.ShowMessage(_fx("editor", "{} at the other corner grabs the block, Esc cancels", key));
		}
	}
}

#endif
