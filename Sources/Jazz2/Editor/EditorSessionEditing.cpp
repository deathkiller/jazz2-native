#include "EditorSessionPrivate.h"

#if defined(WITH_EDITOR)

#include "Common/StampClipboard.h"

// The clipboard, the selection and the changes of layers and events, of EditorSession

namespace Jazz2::Editor
{
	namespace
	{
		// The text in the clipboard of the system, see StampClipboard, with the tile sets of the level the tiles are of
		String StampToClipboardText(const TileStamp& stamp, const LevelDocument& document)
		{
			return StampClipboard::WriteTiles(stamp, &document);
		}

		bool ClipboardTextToStamp(StringView text, TileStamp& stamp, const LevelDocument& document)
		{
			return StampClipboard::ReadTiles(text, stamp, &document);
		}

		void RemoveUnknownTiles(const LevelDocument& document, TileStamp& stamp)
		{
			StampClipboard::RemoveUnknownTiles(document, stamp);
		}

		String EventStampToClipboardText(const EventStamp& stamp)
		{
			return StampClipboard::WriteEvents(stamp);
		}

		bool ClipboardTextToEventStamp(StringView text, EventStamp& stamp)
		{
			return StampClipboard::ReadEvents(text, stamp);
		}
	}

	void EditorSession::SetEventBrush(EventStamp&& brush)
	{
		_eventBrush = Death::move(brush);
		_eventSelection.Clear();
		// The path tool places it too
		if (_tools->GetChosenType() != EditorToolType::Event && _tools->GetChosenType() != EditorToolType::Path) {
			_tools->SetActive(EditorToolType::Event);
		}
		// The next click places it, the event tool doesn't only select events anymore
		_tools->GetSettings().PlaceEventsAgain();
		_pickerPanel = PickerPanel::Events;
	}

	void EditorSession::PickEventFromList(Function<void(EventType)>&& callback)
	{
		_eventPick = Death::move(callback);
		_pickerPanel = PickerPanel::Events;
		// The picker panel can be on either side, floating or in a drawer, its tab is named the same anywhere
		ShowMessage(_x("editor", "Choose the event in the Events tab, Esc cancels"));
	}

	void EditorSession::OnEventChosen(EventType type)
	{
		if (_eventPick) {
			Function<void(EventType)> callback = Death::move(_eventPick);
			_eventPick = {};
			callback(type);
			return;
		}
		SetEventBrush(EventStamp::FromEvent(EventCatalog::MakeDefault(type)));
	}

	void EditorSession::BakeGroundLine()
	{
		constexpr std::int32_t Size = Formats::LevelFile::TileSize;
		auto* tool = static_cast<GroundLineTool*>(_tools->Get(EditorToolType::Ground));
		SmallVector<TerrainSpline, 0> splines;
		TerrainSpline::Read(_document, splines);
		std::int32_t index = tool->GetSelectedSpline();
		if (index < 0 || index >= std::int32_t(splines.size())) {
			ShowMessage(_x("editor", "Click a ground line with the Ground Line tool first"));
			return;
		}
		TerrainSpline spline = splines[index];
		if (spline.Kind == SplineKind::Closed) {
			ShowMessage(_x("editor", "Tiles can be made along a line of ground, a ceiling or a band, not along a shape"));
			return;
		}
		if (spline.Layer < 0 || spline.Layer >= std::int32_t(_document.Layers.size()) || !IsLayerEditable(spline.Layer)) {
			ShowMessage(_x("editor", "The layer is locked or hidden"));
			return;
		}

		// The inside of the ground, the flat tile the levels use the most, is cut to the line
		const TileShapes& shapes = GetTileShapes();
		SmartTileModel& model = GetSmartTiles();
		TileRef fullTile{};
		float best = -1.0f;
		for (const TileShapes::Surface& s : shapes.GetSurfaces()) {
			// The shapes have tiles stored in the level too, the image is taken from the tile set
			if (s.Left < 1.0f && s.Right < 1.0f && s.Error < 0.5f && s.Tile.Flags == 0 && s.Tile.Source == TileRef::DefaultTileSet) {
				float usage = float(model.GetSurfaceUsage(s.Tile));
				if (usage > best) {
					best = usage;
					fullTile = s.Tile;
				}
			}
		}
		const Formats::TileSetData* data = GetTileSetData(_document.Properties.DefaultTileSet);
		if (best < 0.0f || data == nullptr || fullTile.Index >= data->TileCount) {
			ShowMessage(_x("editor", "The tile set has no solid tile to cut the ground from"));
			return;
		}
		const std::uint8_t* inside = &data->Images[std::size_t(fullTile.Index) * Formats::TileSetData::ImageBytesPerTile];
		bool trueColor = (data->TrueColor[fullTile.Index] != 0);

		// A tile for each cell the line goes through, solid on the side of the ground
		SmallVector<Vector2f, 0> line;
		GroundLineTool::SampleLine(spline, line);
		if (line.empty()) {
			ShowMessage(_x("editor", "Click a ground line with the Ground Line tool first"));
			return;
		}
		const DocumentLayer& layer = _document.Layers[spline.Layer];
		bool ceiling = (spline.Kind == SplineKind::Ceiling);
		SmallVector<Vector2i, 0> cells;
		Formats::TileSetData tiles;
		std::memcpy(tiles.Palette, data->Palette, sizeof(tiles.Palette));
		std::int32_t cx1 = std::int32_t(std::floor(line.front().X / 32.0f));
		std::int32_t cx2 = std::int32_t(std::floor((line.back().X - 0.5f) / 32.0f));
		for (std::int32_t cx = cx1; cx <= cx2; cx++) {
			float depths[Size];
			float minDepth = 1e30f, maxDepth = -1e30f;
			for (std::int32_t x = 0; x < Size; x++) {
				depths[x] = GroundLineTool::GetLineY(line, cx * 32.0f + float(x) + 0.5f);
				minDepth = std::min(minDepth, depths[x]);
				maxDepth = std::max(maxDepth, depths[x]);
			}
			for (std::int32_t cy = std::int32_t(std::floor(minDepth / 32.0f)); cy <= std::int32_t(std::floor(maxDepth / 32.0f)); cy++) {
				if (!layer.Contains(cx, cy)) {
					continue;
				}
				// Only cells the line crosses, partly solid
				bool any = false, all = true;
				for (std::int32_t x = 0; x < Size; x++) {
					float d = depths[x] - cy * 32.0f;
					bool solidTop = (ceiling ? d > 0.5f : d < 0.5f), solidBottom = (ceiling ? d > 31.5f : d < 31.5f);
					any |= (solidTop || solidBottom);
					all &= (solidTop && solidBottom);
				}
				if (!any || all) {
					continue;
				}
				std::int32_t tile = tiles.TileCount;
				tiles.Resize(tile + 1);
				tiles.TrueColor[tile] = (trueColor ? 1 : 0);
				std::uint8_t* image = &tiles.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
				std::uint8_t* mask = &tiles.Masks[std::size_t(tile) * Formats::TileSetData::MaskBytesPerTile];
				for (std::int32_t y = 0; y < Size; y++) {
					for (std::int32_t x = 0; x < Size; x++) {
						float d = depths[x] - cy * 32.0f;
						bool solid = (ceiling ? float(y) + 0.5f < d : float(y) + 0.5f >= d);
						if (!solid) {
							continue;
						}
						std::int32_t i = y * Size + x;
						if (trueColor) {
							std::memcpy(&image[i * 4], &inside[i * 4], 4);
						} else {
							image[i] = inside[i];
						}
						mask[i / 8] |= std::uint8_t(1 << (i % 8));
					}
				}
				cells.push_back(Vector2i(cx, cy));
			}
		}
		if (cells.empty()) {
			ShowMessage(_x("editor", "The line goes along the edges of the tiles, no tile has to be cut"));
			return;
		}

		// TRANSLATORS: Name of an undo step in the history, new tiles cut along a ground line drawn with the Ground Line tool
		_history->BeginTransaction(String(_x("editor", "Make tiles along ground line")));
		TileTarget target;
		target.Type = TileTarget::Kind::Level;
		TileRef first;
		if (!AppendTiles(*this, target, tiles, first)) {
			DocumentChanges changes;
			_history->CancelTransaction(changes);
			OnDocumentChanged(changes);
			return;
		}
		// The line owns the new tiles, what was there before them stays what it gets back
		TerrainSpline::Read(_document, splines);
		auto command = std::make_unique<SetTilesCommand>(_document, spline.Layer, String(_x("editor", "Make tiles along ground line")));
		for (std::size_t i = 0; i < cells.size(); i++) {
			Vector2i cell = cells[i];
			TileRef tile{ std::uint16_t(first.Index + i), first.Source, 0 };
			command->Set(cell.X, cell.Y, tile);
			bool owned = false;
			for (TerrainSpline::OwnedTile& entry : splines[index].Owned) {
				if (entry.X == cell.X && entry.Y == cell.Y) {
					entry.Written = tile;
					owned = true;
					break;
				}
			}
			if (!owned) {
				splines[index].Owned.push_back(TerrainSpline::OwnedTile{ cell.X, cell.Y, tile, _document.Layers[spline.Layer].GetTile(cell.X, cell.Y) });
			}
		}
		command->Seal();
		Execute(std::make_unique<SetTerrainSplinesCommand>(_document, splines, Death::move(command), String(_x("editor", "Make tiles along ground line"))));
		_history->EndTransaction();
		ShowMessage(_fnx("editor", "{} tile was made for the line, only this level has it", "{} tiles were made for the line, only this level has them", std::int32_t(cells.size()), std::int32_t(cells.size())));
	}

	void EditorSession::ContinueRow()
	{
		// From the cell under the pointer, or after the last tile of a selection of one row
		constexpr std::int32_t MaxLength = 64;
		std::int32_t layerIndex = _currentLayer;
		if (layerIndex < 0 || layerIndex >= std::int32_t(_document.Layers.size()) || !IsLayerEditable(layerIndex)) {
			return;
		}
		Vector2i start;
		ToolPointer pointer;
		LayerView view = GetLayerView(layerIndex);
		if (!GetHoverPointer(pointer) || !view.Wrap(pointer.Cell, start)) {
			ShowMessage(_x("editor", "Point at the cell the row goes on from"));
			return;
		}
		const DocumentLayer& layer = _document.Layers[layerIndex];
		// A row goes on from the tile left of where it starts
		if (!layer.GetTile(start.X, start.Y).IsEmpty()) {
			start.X++;
		}
		auto at = [&layer](std::int32_t x, std::int32_t y) -> const TileRef* {
			return (layer.Contains(x, y) && !layer.GetTile(x, y).IsEmpty() ? &layer.GetTile(x, y) : nullptr);
		};

		SmartTileModel& model = GetSmartTiles();
		// TRANSLATORS: Name of an undo step in the history, a row of tiles continued by the suggested tiles
		auto command = std::make_unique<SetTilesCommand>(_document, layerIndex, String(_x("editor", "Continue row")));
		TileRef previous;
		bool hasPrevious = (at(start.X - 1, start.Y) != nullptr);
		if (hasPrevious) {
			previous = *at(start.X - 1, start.Y);
		}
		SmallVector<TileRef, 8> suggested;
		std::int32_t placed = 0;
		for (std::int32_t x = start.X; x < start.X + MaxLength && layer.Contains(x, start.Y); x++) {
			if (!layer.GetTile(x, start.Y).IsEmpty()) {
				break;
			}
			if (model.Suggest(hasPrevious ? &previous : nullptr, at(x, start.Y - 1), nullptr, at(x, start.Y + 1), 1, suggested) == 0) {
				break;
			}
			command->Set(x, start.Y, suggested[0]);
			previous = suggested[0];
			hasPrevious = true;
			placed++;
		}
		if (placed == 0) {
			ShowMessage(model.IsReady() ? _x("editor", "The levels have nothing that would go on from here") : _x("editor", "Still learning the tiles from the levels, try again in a moment"));
			return;
		}
		Execute(Death::move(command));
	}

	void EditorSession::RetileSelection()
	{
		if (_selection.IsEmpty()) {
			ShowMessage(_x("editor", "Select the tiles to choose again first"));
			return;
		}
		SmartTileModel& model = GetSmartTiles();
		if (!model.IsReady()) {
			ShowMessage(_fx("editor", "Still learning how the tiles fit - {}%, try it again in a moment", std::int32_t(model.GetProgress() * 100.0f)));
			return;
		}
		if (model.GetLevelCount() == 0) {
			ShowMessage(_x("editor", "No level uses this tile set yet, so there's nothing to learn the tiles from"));
			return;
		}
		std::int32_t layerIndex = _selection.GetLayer();
		if (layerIndex < 0 || layerIndex >= std::int32_t(_document.Layers.size()) || !IsLayerEditable(layerIndex)) {
			ShowMessage(_x("editor", "The layer is locked or hidden"));
			return;
		}

		// Each selected tile of the tile set gets the tile the levels use the most for its neighbors, the solid and
		// empty cells stay as they are; tiles of another tile set and animated ones are kept
		// As large as a fill can be, choosing more at once would take long and much memory
		constexpr std::int64_t MaxRetileCells = 1 << 20;
		const Recti& bounds = _selection.GetBounds();
		if (std::int64_t(bounds.W) * std::int64_t(bounds.H) > MaxRetileCells) {
			ShowMessage(_fx("editor", "The selection is too large, tiles can be chosen again in {} tiles at most at once", std::int32_t(MaxRetileCells)));
			return;
		}

		const DocumentLayer& layer = _document.Layers[layerIndex];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		// TRANSLATORS: Name of an undo step in the history, the selected tiles replaced by those that fit their neighbors
		auto command = std::make_unique<SetTilesCommand>(_document, layerIndex, _x("editor", "Choose tiles by neighbors"));
		// The tiles chosen already in the selected area, the neighbors after them are chosen by them
		SmallVector<TileRef, 0> decidedTiles;
		SmallVector<bool, 0> decided;
		decidedTiles.resize(std::size_t(bounds.W) * std::size_t(bounds.H));
		decided.resize(std::size_t(bounds.W) * std::size_t(bounds.H), false);
		auto getTile = [&](std::int32_t x, std::int32_t y) {
			if (x >= bounds.X && y >= bounds.Y && x < bounds.X + bounds.W && y < bounds.Y + bounds.H) {
				std::size_t i = std::size_t(y - bounds.Y) * std::size_t(bounds.W) + std::size_t(x - bounds.X);
				if (decided[i]) {
					return decidedTiles[i];
				}
			}
			return layer.GetTile(x, y);
		};
		auto isSolid = [&](std::int32_t x, std::int32_t y) {
			if (x < 0 || y < 0 || x >= width || y >= height) {
				return true;
			}
			TileRef tile = layer.GetTile(x, y);
			return (!tile.IsEmpty() && (tile.Flags & Formats::TileRecord::ModifierMask) != Formats::TileRecord::ModifierInvisible);
		};
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (!_selection.Contains(x, y) || !SmartTileModel::IsModelTile(layer.GetTile(x, y))) {
					continue;
				}
				std::uint8_t neighbors = 0;
				static const std::int32_t Offsets[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
				for (std::int32_t i = 0; i < 8; i++) {
					if (isSolid(x + Offsets[i][0], y + Offsets[i][1])) {
						neighbors |= std::uint8_t(1 << i);
					}
				}
				auto neighborTile = [&](std::int32_t nx, std::int32_t ny, TileRef& result) {
					if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
						return false;
					}
					result = getTile(nx, ny);
					return SmartTileModel::IsModelTile(result);
				};
				TileRef left, up, right, down, picked;
				bool hasLeft = neighborTile(x - 1, y, left), hasUp = neighborTile(x, y - 1, up);
				bool hasRight = neighborTile(x + 1, y, right), hasDown = neighborTile(x, y + 1, down);
				std::uint32_t hash = (std::uint32_t(x) * 0x8DA6B343u) ^ (std::uint32_t(y) * 0xD8163841u);
				if (model.Pick(neighbors, layer.IsSpriteLayer(), hasLeft ? &left : nullptr, hasUp ? &up : nullptr,
					hasRight ? &right : nullptr, hasDown ? &down : nullptr, hash * 0x2C1B3C6Du, picked)) {
					std::size_t i = std::size_t(y - bounds.Y) * std::size_t(bounds.W) + std::size_t(x - bounds.X);
					decidedTiles[i] = picked;
					decided[i] = true;
					command->Set(x, y, picked);
				}
			}
		}
		command->Seal();
		if (command->IsEmpty()) {
			ShowMessage(_x("editor", "The tiles fit their neighbors already"));
			return;
		}
		Execute(Death::move(command));
	}

	bool EditorSession::IsEditingEvents() const
	{
		return (_tools != nullptr && _tools->GetChosenType() == EditorToolType::Event);
	}

	void EditorSession::UpdateEventSound()
	{
		// Only while it's edited and the editor is used, it stops once another event or another tool is chosen
		const Formats::EventRecord* heard = nullptr;
		if (_editHandler != nullptr && _playTest->IsEditing() && IsEditingEvents() && theApplication().HasFocus() && _eventSelection.GetTotalCount() == 1) {
			Vector2i gridSize = _document.GetEventGridSize();
			if (_eventSelection.HasCells() && gridSize.X > 0) {
				std::uint32_t cell = _eventSelection.GetCells()[0];
				heard = &_document.GetEvent(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
			} else if (_eventSelection.HasOffGrid()) {
				std::int32_t index = _eventSelection.GetOffGrid()[0];
				if (index >= 0 && index < std::int32_t(_document.OffGridEvents.size())) {
					heard = &_document.OffGridEvents[index].Event;
				}
			}
		}
		_eventPreviews.UpdateSound(heard);
	}

	bool EditorSession::HasSelection() const
	{
		if (IsEditingEvents()) {
			return !_eventSelection.IsEmpty();
		}
		return (!_selection.IsEmpty() && _selection.GetLayer() == _currentLayer);
	}

	void EditorSession::ClearSelection()
	{
		if (IsEditingEvents()) {
			_eventSelection.Clear();
		} else {
			_selection.Clear();
		}
	}

	void EditorSession::CopySelection(bool cut)
	{
		if (IsEditingEvents()) {
			Vector2i gridSize = _document.GetEventGridSize();
			if (_eventSelection.IsEmpty() || gridSize.X <= 0) {
				return;
			}
			// Placed relative to the top left tile of the copied events, those off the grid by pixels from it
			Vector2i origin(INT32_MAX, INT32_MAX);
			for (std::uint32_t cell : _eventSelection.GetCells()) {
				origin.X = std::min<std::int32_t>(origin.X, std::int32_t(cell % std::uint32_t(gridSize.X)));
				origin.Y = std::min<std::int32_t>(origin.Y, std::int32_t(cell / std::uint32_t(gridSize.X)));
			}
			for (std::int32_t index : _eventSelection.GetOffGrid()) {
				if (index < std::int32_t(_document.OffGridEvents.size())) {
					const Formats::OffGridEventRecord& record = _document.OffGridEvents[index];
					origin.X = std::min<std::int32_t>(origin.X, std::int32_t(record.X / 32));
					origin.Y = std::min<std::int32_t>(origin.Y, std::int32_t(record.Y / 32));
				}
			}
			EventStamp stamp;
			for (std::uint32_t cell : _eventSelection.GetCells()) {
				Vector2i pos(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
				EventStamp::Item& item = stamp.Items.emplace_back();
				item.Offset = pos - origin;
				item.Event = _document.GetEvent(pos.X, pos.Y);
			}
			for (std::int32_t index : _eventSelection.GetOffGrid()) {
				if (index < std::int32_t(_document.OffGridEvents.size())) {
					const Formats::OffGridEventRecord& record = _document.OffGridEvents[index];
					EventStamp::OffGridItem& item = stamp.OffGrid.emplace_back();
					item.Offset = Vector2i(std::int32_t(record.X) - origin.X * 32, std::int32_t(record.Y) - origin.Y * 32);
					item.Event = record.Event;
				}
			}
			if (stamp.IsEmpty()) {
				return;
			}
			theApplication().GetInputManager().setClipboardText(EventStampToClipboardText(stamp));
			_eventClipboard = Death::move(stamp);
			if (cut) {
				DeleteSelection();
			} else {
				// TRANSLATORS: Status message after the selection was copied to the clipboard
				ShowMessage(_x("editor", "Copied"));
			}
			return;
		}

		if (_selection.IsEmpty() || _selection.GetLayer() != _currentLayer) {
			return;
		}
		const DocumentLayer& layer = _document.Layers[_currentLayer];
		const Recti& bounds = _selection.GetBounds();
		TileStamp stamp = TileStamp::FromLayer(layer, bounds);
		for (std::int32_t y = 0; y < bounds.H; y++) {
			for (std::int32_t x = 0; x < bounds.W; x++) {
				if (!_selection.Contains(bounds.X + x, bounds.Y + y)) {
					stamp.Present[std::size_t(y) * bounds.W + x] = false;
				}
			}
		}
		theApplication().GetInputManager().setClipboardText(StampToClipboardText(stamp, _document));
		_clipboard = Death::move(stamp);

		if (cut) {
			DeleteSelection();
		} else {
			ShowMessage(_x("editor", "Copied"));
		}
	}

	void EditorSession::DeleteSelection()
	{
		if (IsEditingEvents()) {
			if (_eventSelection.IsEmpty()) {
				return;
			}
			if (!IsLayerEditable(_document.GetSpriteLayerIndex())) {
				ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
				return;
			}
			// Those on the grid and those off it, as one step
			// TRANSLATORS: Name of an undo step in the history, the selected events deleted
			String name = _x("editor", "Delete Events");
			BeginChanges(name);
			if (_eventSelection.HasCells()) {
				Vector2i gridSize = _document.GetEventGridSize();
				auto command = std::make_unique<SetEventsCommand>(_document, name);
				for (std::uint32_t cell : _eventSelection.GetCells()) {
					command->Set(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)), LevelDocument::GetEmptyEvent());
				}
				Execute(Death::move(command));
			}
			if (_eventSelection.HasOffGrid()) {
				SmallVector<Formats::OffGridEventRecord, 0> events;
				for (std::size_t i = 0; i < _document.OffGridEvents.size(); i++) {
					if (!_eventSelection.ContainsOffGrid(std::int32_t(i))) {
						events.push_back(_document.OffGridEvents[i]);
					}
				}
				if (events.size() != _document.OffGridEvents.size()) {
					Execute(std::make_unique<SetOffGridEventsCommand>(_document, Death::move(events), name));
				}
			}
			EndChanges();
			_eventSelection.Clear();
			return;
		}

		if (_selection.IsEmpty() || _selection.GetLayer() != _currentLayer || !IsLayerEditable(_currentLayer)) {
			return;
		}
		const Recti& bounds = _selection.GetBounds();
		// TRANSLATORS: Name of an undo step in the history, the selected tiles deleted, also the Delete menu item in the Edit menu
		auto command = std::make_unique<SetTilesCommand>(_document, _currentLayer, _x("editor", "Delete"));
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (_selection.Contains(x, y)) {
					command->Set(x, y, TileRef{});
				}
			}
		}
		Execute(Death::move(command));
	}

	void EditorSession::Paste()
	{
		if (IsEditingEvents()) {
			EventStamp stamp;
			String text = theApplication().GetInputManager().getClipboardText();
			if (!ClipboardTextToEventStamp(text, stamp)) {
				stamp = _eventClipboard;
			}
			if (stamp.IsEmpty()) {
				return;
			}
			std::int32_t remapped = RemapLinkedIds(stamp, _document);
			SetEventBrush(Death::move(stamp));
			ShowMessage(remapped > 0 ? _x("editor", "Click to place what was copied, its warps and triggers got new identifiers") : _x("editor", "Click to place what was copied"));
			return;
		}

		TileStamp stamp;
		String text = theApplication().GetInputManager().getClipboardText();
		if (!ClipboardTextToStamp(text, stamp, _document)) {
			stamp = _clipboard;
		}
		if (stamp.IsEmpty()) {
			return;
		}
		// Copied from another level maybe, even the stamp copied here may be of a level opened before
		RemoveUnknownTiles(_document, stamp);
		_stamp = Death::move(stamp);
		_selection.Clear();
		_tools->SetActive(EditorToolType::Brush);
		ShowMessage(_x("editor", "Click to place what was copied"));
	}

	void EditorSession::SelectAll()
	{
		if (IsEditingEvents()) {
			SmallVector<std::uint32_t, 0> cells;
			for (const DocumentEvent& event : _document.Events) {
				cells.push_back(event.Cell);
			}
			// And all off the grid
			SmallVector<std::int32_t, 0> offGrid;
			for (std::int32_t i = 0; i < std::int32_t(_document.OffGridEvents.size()); i++) {
				offGrid.push_back(i);
			}
			_eventSelection.Assign(Death::move(cells));
			_eventSelection.AssignOffGrid(Death::move(offGrid));
			return;
		}

		if (_currentLayer < 0 || _currentLayer >= std::int32_t(_document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = _document.Layers[_currentLayer];
		_selection.Select(_currentLayer, Vector2i(layer.GetWidth(), layer.GetHeight()), Recti(0, 0, layer.GetWidth(), layer.GetHeight()), false);
	}

	void EditorSession::AddLayer()
	{
		if (_document.Layers.size() >= UINT8_MAX) {
			return;
		}

		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		Vector2i size = (spriteLayer >= 0 ? Vector2i(_document.Layers[spriteLayer].GetWidth(), _document.Layers[spriteLayer].GetHeight()) : Vector2i(64, 64));

		DocumentLayer layer;
		std::memset(&layer.Record, 0, sizeof(layer.Record));
		layer.Record.Textured = {};
		layer.Record.Kind = Formats::LayerKind::Other;
		layer.Record.Flags = Formats::LayerRecord::Visible;
		layer.Record.Width = size.X;
		layer.Record.Height = size.Y;
		layer.Record.SpeedX = 1.0f;
		layer.Record.SpeedY = 1.0f;
		layer.Record.Depth = (_currentLayer < std::int32_t(_document.Layers.size()) ? _document.Layers[_currentLayer].Record.Depth : 0);
		std::memset(layer.Record.Color, 255, sizeof(layer.Record.Color));
		// TRANSLATORS: Name a newly added layer gets
		layer.Name = _x("editor", "New Layer");
		layer.Tiles.resize(std::size_t(size.X) * std::size_t(size.Y), TileRef{});

		// Only the new layer is kept by the step, not the whole level, the ground lines of the layers after it follow them
		std::int32_t index = std::min(_currentLayer + 1, std::int32_t(_document.Layers.size()));
		if (!_input->IsInToolCallback()) {
			CancelInteractions(false);
		}
		DocumentChanges changes;
		// TRANSLATORS: Name of an undo step in the history, also a menu item in the Layer menu (verb)
		_history->Execute(InsertLayerCommand::Add(_document, index, layer, _x("editor", "Add Layer")), changes);
		// As any other change of the layer edited, what the tool did on the one before is cancelled
		SetCurrentLayer(index);
		OnDocumentChanged(changes);
	}

	void EditorSession::SnapOffGridEvents()
	{
		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		if (_document.OffGridEvents.empty() || spriteLayer < 0) {
			return;
		}
		if (!IsLayerEditable(spriteLayer)) {
			ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
			return;
		}

		Vector2i gridSize = _document.GetEventGridSize();
		// TRANSLATORS: Name of an undo step in the history, events placed off the grid of tiles moved onto it
		auto placed = std::make_unique<SetEventsCommand>(_document, _x("editor", "Snap Off-Grid Events"));
		SmallVector<Formats::OffGridEventRecord, 0> kept;
		SmallVector<std::uint32_t, 0> taken;
		for (const Formats::OffGridEventRecord& event : _document.OffGridEvents) {
			std::int32_t x = std::int32_t(event.X / 32), y = std::int32_t(event.Y / 32);
			std::uint32_t cell = std::uint32_t(x + y * gridSize.X);
			bool free = (x < gridSize.X && y < gridSize.Y && LevelDocument::IsEmptyEvent(_document.GetEvent(x, y)) &&
				std::find(taken.begin(), taken.end(), cell) == taken.end());
			if (free) {
				placed->Set(x, y, event.Event);
				taken.push_back(cell);
			} else {
				kept.push_back(event);
			}
		}
		if (taken.empty()) {
			ShowMessage(_x("editor", "The tiles of all off-grid events have an event already"));
			return;
		}

		std::int32_t keptCount = std::int32_t(kept.size());
		// The indices of those off the grid change
		_eventSelection.Clear();
		_history->BeginTransaction(_x("editor", "Snap Off-Grid Events"));
		Execute(Death::move(placed));
		Execute(std::make_unique<SetOffGridEventsCommand>(_document, Death::move(kept), _x("editor", "Snap Off-Grid Events")));
		_history->EndTransaction();
		ShowMessage(keptCount == 0
			? _fnx("editor", "{} event was moved onto the grid", "{} events were moved onto the grid", std::int32_t(taken.size()), std::int32_t(taken.size()))
			: _fx("editor", "{} events were moved onto the grid, {} stay off it, their tiles have an event already", std::int32_t(taken.size()), keptCount));
	}

	void EditorSession::SelectEventsOfSameType()
	{
		Vector2i gridSize = _document.GetEventGridSize();
		if (_eventSelection.IsEmpty() || gridSize.X <= 0) {
			return;
		}
		// Of the first selected event, on the grid or off it, both kinds are selected
		std::uint16_t type;
		if (_eventSelection.HasCells()) {
			std::uint32_t first = _eventSelection.GetCells()[0];
			type = _document.GetEvent(std::int32_t(first % std::uint32_t(gridSize.X)), std::int32_t(first / std::uint32_t(gridSize.X))).Type;
		} else {
			std::int32_t first = _eventSelection.GetOffGrid()[0];
			if (first >= std::int32_t(_document.OffGridEvents.size())) {
				return;
			}
			type = _document.OffGridEvents[first].Event.Type;
		}
		SmallVector<std::uint32_t, 0> cells;
		for (const DocumentEvent& event : _document.Events) {
			if (event.Record.Type == type) {
				cells.push_back(event.Cell);
			}
		}
		SmallVector<std::int32_t, 0> offGrid;
		for (std::int32_t i = 0; i < std::int32_t(_document.OffGridEvents.size()); i++) {
			if (_document.OffGridEvents[i].Event.Type == type) {
				offGrid.push_back(i);
			}
		}
		std::int32_t count = std::int32_t(cells.size() + offGrid.size());
		_eventSelection.Assign(Death::move(cells));
		_eventSelection.AssignOffGrid(Death::move(offGrid));
		ShowMessage(_fnx("editor", "{} event selected", "{} events selected", count, count));
	}

	void EditorSession::CopyEventToMatchingTiles()
	{
		std::int32_t spriteLayer = _document.GetSpriteLayerIndex();
		Vector2i gridSize = _document.GetEventGridSize();
		if (_eventSelection.GetCellCount() != 1 || spriteLayer < 0 || gridSize.X <= 0) {
			return;
		}
		if (!IsLayerEditable(spriteLayer)) {
			ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
			return;
		}
		std::uint32_t cell = _eventSelection.GetCells()[0];
		std::int32_t cx = std::int32_t(cell % std::uint32_t(gridSize.X)), cy = std::int32_t(cell / std::uint32_t(gridSize.X));
		const DocumentLayer& layer = _document.Layers[spriteLayer];
		if (!layer.Contains(cx, cy)) {
			return;
		}
		TileRef tile = layer.GetTile(cx, cy);
		if (tile.IsEmpty()) {
			ShowMessage(_x("editor", "The event isn't on a tile"));
			return;
		}

		// Flipped tiles are left out, an event that makes the tile do something often depends on which way it faces
		Formats::EventRecord event = _document.GetEvent(cx, cy);
		// TRANSLATORS: Name of an undo step in the history, an event copied to every tile like its own
		auto command = std::make_unique<SetEventsCommand>(_document, _x("editor", "Copy Event to Tiles"));
		SmallVector<std::uint32_t, 0> cells;
		std::int32_t taken = 0;
		std::int32_t height = std::min<std::int32_t>(layer.GetHeight(), gridSize.Y);
		std::int32_t width = std::min<std::int32_t>(layer.GetWidth(), gridSize.X);
		for (std::int32_t y = 0; y < height; y++) {
			for (std::int32_t x = 0; x < width; x++) {
				if (layer.GetTile(x, y) != tile || (x == cx && y == cy)) {
					continue;
				}
				if (!LevelDocument::IsEmptyEvent(_document.GetEvent(x, y))) {
					taken++;
					continue;
				}
				command->Set(x, y, event);
				cells.push_back(std::uint32_t(x + y * gridSize.X));
			}
		}
		if (cells.empty()) {
			ShowMessage(taken > 0 ? _x("editor", "All other tiles like this one have an event already") : _x("editor", "No other tile is like this one"));
			return;
		}
		std::int32_t count = std::int32_t(cells.size());
		Execute(Death::move(command));
		cells.push_back(cell);
		_eventSelection.Assign(Death::move(cells));
		ShowMessage(taken == 0
			? _fnx("editor", "Copied to {} tile", "Copied to {} tiles", count, count)
			: _fx("editor", "Copied to {} tiles, {} have an event already", count, taken));
	}

	void EditorSession::RemoveLayer()
	{
		if (_currentLayer < 0 || _currentLayer >= std::int32_t(_document.Layers.size()) || _document.Layers[_currentLayer].IsSpriteLayer()) {
			return;
		}

		// Only the removed layer is kept by the step, not the whole level, its ground lines come back with it
		if (!_input->IsInToolCallback()) {
			CancelInteractions(false);
		}
		DocumentChanges changes;
		// TRANSLATORS: Name of an undo step in the history, also a menu item in the Layer menu (verb)
		_history->Execute(InsertLayerCommand::Remove(_document, _currentLayer, _x("editor", "Remove Layer")), changes);
		_currentLayer = std::min(_currentLayer, std::int32_t(_document.Layers.size()) - 1);
		OnDocumentChanged(changes);
	}

	void EditorSession::MoveLayer(std::int32_t direction)
	{
		MoveLayerTo(_currentLayer, _currentLayer + direction);
	}

	void EditorSession::MoveLayerTo(std::int32_t from, std::int32_t to)
	{
		std::int32_t count = std::int32_t(_document.Layers.size());
		if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
			return;
		}

		// Only the positions are kept by the step, ground lines stay on their layers
		if (!_input->IsInToolCallback()) {
			CancelInteractions(false);
		}
		DocumentChanges changes;
		// TRANSLATORS: Name of an undo step in the history, a layer moved up or down in the list of layers
		_history->Execute(std::make_unique<MoveLayerCommand>(from, to, _x("editor", "Move Layer")), changes);
		// The moved layer is the one edited then
		_currentLayer = to;
		OnDocumentChanged(changes);
	}
}

#endif
