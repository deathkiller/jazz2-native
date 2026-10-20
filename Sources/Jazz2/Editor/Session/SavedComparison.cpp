#include "SavedComparison.h"

#if defined(WITH_EDITOR)

#include "../Common/LayerView.h"
#include "../Document/CommandHistory.h"
#include "../Document/LevelDocument.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Tools/EditorContext.h"
#include "../UI/UiPainter.h"
#include "../UI/UiTheme.h"

#include "../../../nCine/I18n.h"

#include <Containers/DateTime.h>
#include <IO/FileSystem.h>

#include <algorithm>

using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		std::int64_t GetModificationTime(StringView path)
		{
			return (!path.empty() && fs::FileExists(path) ? fs::GetLastModificationTime(path).ToUnixMilliseconds() : 0);
		}

		// Adds the cells of events that differ between two runs of events sorted by the cell, in the order of the cells
		void FindEventDifferences(ArrayView<const DocumentEvent> events, ArrayView<const DocumentEvent> savedEvents, SmallVectorImpl<std::uint32_t>& cells)
		{
			std::size_t i = 0, j = 0;
			while (i < events.size() || j < savedEvents.size()) {
				if (j >= savedEvents.size() || (i < events.size() && events[i].Cell < savedEvents[j].Cell)) {
					cells.push_back(events[i].Cell);
					i++;
				} else if (i >= events.size() || savedEvents[j].Cell < events[i].Cell) {
					cells.push_back(savedEvents[j].Cell);
					j++;
				} else {
					if (!LevelDocument::AreEventsEqual(events[i].Record, savedEvents[j].Record)) {
						cells.push_back(events[i].Cell);
					}
					i++;
					j++;
				}
			}
		}
	}

	SavedComparison::SavedComparison(IEditorContext& context)
		: _context(context)
	{
	}

	SavedComparison::~SavedComparison()
	{
	}

	void SavedComparison::Toggle(bool strokeInProgress)
	{
		if (_savedDocument != nullptr) {
			Hide();
			return;
		}
		StringView filePath = _context.GetFilePath();
		if (filePath.empty() || !fs::FileExists(filePath)) {
			_context.ShowMessage(_("The level wasn't saved yet, there's nothing to compare it with"));
			return;
		}
		auto saved = std::make_unique<LevelDocument>();
		auto s = fs::Open(filePath, FileAccess::Read);
		if (!s->IsValid() || !saved->Load(*s, &_context.GetTileSetLibrary())) {
			_context.ShowMessage(_("The saved level couldn't be read"));
			return;
		}
		_savedDocument = Death::move(saved);
		_savedFileTime = GetModificationTime(filePath);
		_compareFull = true;
		UpdateDifferences(strokeInProgress);
		if (_savedDifferences.empty() && _savedEventDifferences.empty()) {
			_context.ShowMessage(_("The level is as it was saved"));
		}
	}

	void SavedComparison::Hide()
	{
		_savedDocument = nullptr;
		_savedDifferences.clear();
		_savedEventDifferences.clear();
		_compareChanges.Clear();
	}

	void SavedComparison::NoteDocumentChanges(const DocumentChanges& changes)
	{
		if (_savedDocument != nullptr) {
			// Compared with the saved level again only where it changed, see UpdateDifferences()
			_compareChanges.Add(changes);
		}
	}

	void SavedComparison::ReloadIfSavedChanged()
	{
		// The level compared with is read again once it was saved, also by another program
		if (_savedDocument == nullptr) {
			return;
		}
		StringView filePath = _context.GetFilePath();
		std::int64_t fileTime = GetModificationTime(filePath);
		if (fileTime != _savedFileTime) {
			auto s = fs::Open(filePath, FileAccess::Read);
			auto saved = std::make_unique<LevelDocument>();
			if (s->IsValid() && saved->Load(*s, &_context.GetTileSetLibrary())) {
				_savedDocument = Death::move(saved);
			}
			_savedFileTime = fileTime;
			_compareFull = true;
		}
	}

	void SavedComparison::UpdateDifferences(bool strokeInProgress)
	{
		if (_savedDocument == nullptr) {
			return;
		}
		// The file saved since is read again by ReloadIfSavedChanged(), not every frame
		const LevelDocument& document = _context.GetDocument();
		std::uint32_t revision = _context.GetHistory().GetRevision();
		if (!_compareFull && revision == _compareRevision && _compareChanges.IsEmpty()) {
			return;
		}

		// Only where tiles and events changed, if it's known where; a change of no known area, or one that adds, removes,
		// resizes or moves layers or changes which tiles there are, or a change of the revision none was told about
		constexpr DocumentChangeKind FullKinds = DocumentChangeKind::LayerStructure | DocumentChangeKind::AnimationStructure | DocumentChangeKind::TileSets;
		bool full = (_compareFull || _compareChanges.HasAny(FullKinds) || !_compareChanges.AreTileAreasComplete() ||
			(revision != _compareRevision && _compareChanges.IsEmpty()));
		if (!full) {
			for (const DocumentTileArea& area : _compareChanges.GetTileAreas()) {
				UpdateTileDifferences(area.Layer, area.Area);
			}
			if (_compareChanges.HasAny(DocumentChangeKind::Events)) {
				Vector2i gridSize = document.GetEventGridSize();
				UpdateEventDifferences(_compareChanges.IsEventAreaComplete() ? _compareChanges.GetEventArea() : Recti(0, 0, gridSize.X, gridSize.Y));
			}
			_compareRevision = revision;
			_compareChanges.Clear();
			return;
		}
		// The whole level is gone through, so not for every step of a stroke, only once it's done
		if (strokeInProgress) {
			return;
		}
		_compareFull = false;
		_compareRevision = revision;
		_compareChanges.Clear();

		// Cell by cell for each layer of the same size, a layer otherwise is different as a whole
		_savedDifferences.clear();
		for (std::int32_t i = 0; i < std::int32_t(document.Layers.size()); i++) {
			const DocumentLayer& layer = document.Layers[i];
			SavedLayerDifference difference;
			difference.Layer = i;
			const DocumentLayer* savedLayer = (i < std::int32_t(_savedDocument->Layers.size()) ? &_savedDocument->Layers[i] : nullptr);
			if (savedLayer == nullptr || savedLayer->GetWidth() != layer.GetWidth() || savedLayer->GetHeight() != layer.GetHeight()) {
				difference.Whole = true;
			} else {
				for (std::size_t c = 0; c < layer.Tiles.size() && c < savedLayer->Tiles.size(); c++) {
					if (!(layer.Tiles[c] == savedLayer->Tiles[c])) {
						difference.Cells.push_back(std::uint32_t(c));
					}
				}
			}
			if (difference.Whole || !difference.Cells.empty()) {
				_savedDifferences.push_back(Death::move(difference));
			}
		}
		// Events of the grid, by the cell, both lists are sorted by it
		_savedEventDifferences.clear();
		FindEventDifferences(ArrayView<const DocumentEvent>(document.Events.data(), document.Events.size()),
			ArrayView<const DocumentEvent>(_savedDocument->Events.data(), _savedDocument->Events.size()), _savedEventDifferences);
	}

	void SavedComparison::UpdateTileDifferences(std::int32_t layerIndex, const Recti& area)
	{
		const LevelDocument& document = _context.GetDocument();
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		const DocumentLayer* savedLayer = (layerIndex < std::int32_t(_savedDocument->Layers.size()) ? &_savedDocument->Layers[layerIndex] : nullptr);
		// Kept sorted by the layer
		std::size_t entry = 0;
		while (entry < _savedDifferences.size() && _savedDifferences[entry].Layer < layerIndex) {
			entry++;
		}
		bool exists = (entry < _savedDifferences.size() && _savedDifferences[entry].Layer == layerIndex);
		if (savedLayer == nullptr || savedLayer->GetWidth() != layer.GetWidth() || savedLayer->GetHeight() != layer.GetHeight()) {
			// Different as a whole, whatever its tiles are
			if (!exists) {
				SavedLayerDifference difference;
				difference.Layer = layerIndex;
				difference.Whole = true;
				_savedDifferences.insert(_savedDifferences.begin() + entry, Death::move(difference));
			}
			return;
		}

		std::int32_t width = layer.GetWidth();
		std::int32_t x1 = std::max<std::int32_t>(area.X, 0), y1 = std::max<std::int32_t>(area.Y, 0);
		std::int32_t x2 = std::min<std::int32_t>(area.X + area.W, width), y2 = std::min<std::int32_t>(area.Y + area.H, layer.GetHeight());
		if (x2 <= x1 || y2 <= y1) {
			return;
		}
		// The tiles of the area row by row, so in the order of the cells
		SmallVector<std::uint32_t, 0> found;
		for (std::int32_t y = y1; y < y2; y++) {
			for (std::int32_t x = x1; x < x2; x++) {
				std::uint32_t cell = std::uint32_t(y) * std::uint32_t(width) + std::uint32_t(x);
				if (cell < layer.Tiles.size() && cell < savedLayer->Tiles.size() && !(layer.Tiles[cell] == savedLayer->Tiles[cell])) {
					found.push_back(cell);
				}
			}
		}
		SmallVector<std::uint32_t, 0> cells;
		if (exists) {
			// Those outside of the area stay, they're merged with those found in it
			const SmallVector<std::uint32_t, 0>& previous = _savedDifferences[entry].Cells;
			cells.reserve(previous.size() + found.size());
			std::size_t f = 0;
			for (std::uint32_t cell : previous) {
				std::int32_t x = std::int32_t(cell % std::uint32_t(width)), y = std::int32_t(cell / std::uint32_t(width));
				if (x >= x1 && x < x2 && y >= y1 && y < y2) {
					continue;
				}
				while (f < found.size() && found[f] < cell) {
					cells.push_back(found[f++]);
				}
				cells.push_back(cell);
			}
			cells.append(found.begin() + f, found.end());
		} else {
			cells = Death::move(found);
		}

		if (cells.empty()) {
			if (exists) {
				_savedDifferences.erase(_savedDifferences.begin() + entry);
			}
			return;
		}
		if (!exists) {
			SavedLayerDifference difference;
			difference.Layer = layerIndex;
			_savedDifferences.insert(_savedDifferences.begin() + entry, Death::move(difference));
		}
		_savedDifferences[entry].Whole = false;
		_savedDifferences[entry].Cells = Death::move(cells);
	}

	void SavedComparison::UpdateEventDifferences(const Recti& area)
	{
		const LevelDocument& document = _context.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		std::int32_t x1 = std::max<std::int32_t>(area.X, 0), y1 = std::max<std::int32_t>(area.Y, 0);
		std::int32_t x2 = std::int32_t(std::min<std::int64_t>(std::int64_t(area.X) + area.W, gridSize.X));
		std::int32_t y2 = std::int32_t(std::min<std::int64_t>(std::int64_t(area.Y) + area.H, gridSize.Y));
		if (x2 <= x1 || y2 <= y1) {
			return;
		}

		// Row by row, the events of a row of the area are next to each other in both lists
		auto rowEvents = [](const SmallVectorImpl<DocumentEvent>& events, std::uint32_t first, std::uint32_t last) {
			auto compare = [](const DocumentEvent& event, std::uint32_t cell) { return event.Cell < cell; };
			const DocumentEvent* begin = std::lower_bound(events.begin(), events.end(), first, compare);
			const DocumentEvent* end = std::lower_bound(begin, events.end(), last, compare);
			return ArrayView<const DocumentEvent>(begin, std::size_t(end - begin));
		};
		SmallVector<std::uint32_t, 0> found;
		for (std::int32_t y = y1; y < y2; y++) {
			std::uint32_t first = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(x1);
			std::uint32_t last = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(x2);
			FindEventDifferences(rowEvents(document.Events, first, last), rowEvents(_savedDocument->Events, first, last), found);
		}

		// Those outside of the area stay, they're merged with those found in it
		SmallVector<std::uint32_t, 0> cells;
		cells.reserve(_savedEventDifferences.size() + found.size());
		std::size_t f = 0;
		std::uint32_t gridCells = std::uint32_t(gridSize.X) * std::uint32_t(gridSize.Y);
		for (std::uint32_t cell : _savedEventDifferences) {
			if (cell < gridCells) {
				std::int32_t x = std::int32_t(cell % std::uint32_t(gridSize.X)), y = std::int32_t(cell / std::uint32_t(gridSize.X));
				if (x >= x1 && x < x2 && y >= y1 && y < y2) {
					continue;
				}
			}
			while (f < found.size() && found[f] < cell) {
				cells.push_back(found[f++]);
			}
			cells.push_back(cell);
		}
		cells.append(found.begin() + f, found.end());
		_savedEventDifferences = Death::move(cells);
	}

	void SavedComparison::Draw(UiPainter& painter, const Rectf& viewRect, bool strokeInProgress)
	{
		if (_savedDocument == nullptr) {
			return;
		}
		// Found again where the level changed, also during a stroke, see UpdateDifferences()
		UpdateDifferences(strokeInProgress);

		const LevelDocument& document = _context.GetDocument();
		Colorf fill = UiTheme::OverlayCaution;
		fill.A = 0.35f;
		std::int32_t layerIndex = _context.GetCurrentLayer();
		for (const SavedLayerDifference& difference : _savedDifferences) {
			if (difference.Layer != layerIndex || layerIndex >= std::int32_t(document.Layers.size())) {
				continue;
			}
			LayerView view = _context.GetLayerView(layerIndex);
			const DocumentLayer& layer = document.Layers[layerIndex];
			if (difference.Whole) {
				Rectf first = view.CellToScreen(Vector2i(0, 0));
				Rectf last = view.CellToScreen(Vector2i(layer.GetWidth() - 1, layer.GetHeight() - 1));
				painter.Frame(Rectf(first.X, first.Y, last.X + last.W - first.X, last.Y + last.H - first.Y), UiTheme::OverlayCaution, std::max(2.0f, painter.GetScale() * 2.0f));
				continue;
			}
			std::int32_t width = std::max<std::int32_t>(layer.GetWidth(), 1);
			for (std::uint32_t cell : difference.Cells) {
				Rectf rect = view.CellToScreen(Vector2i(std::int32_t(cell % std::uint32_t(width)), std::int32_t(cell / std::uint32_t(width))));
				if (rect.X + rect.W < viewRect.X || rect.Y + rect.H < viewRect.Y || rect.X > viewRect.X + viewRect.W || rect.Y > viewRect.Y + viewRect.H) {
					continue;
				}
				painter.FillRect(rect, fill, UiLayer::Fill);
			}
		}

		// Changed events, a mark in the corner of their tile, whichever layer is edited
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (!_savedEventDifferences.empty() && spriteLayer >= 0) {
			LayerView view = _context.GetLayerView(spriteLayer);
			std::int32_t width = std::max<std::int32_t>(document.GetEventGridSize().X, 1);
			float size = std::max(4.0f, std::min(10.0f, 32.0f * view.Scale * 0.35f));
			for (std::uint32_t cell : _savedEventDifferences) {
				Rectf rect = view.CellToScreen(Vector2i(std::int32_t(cell % std::uint32_t(width)), std::int32_t(cell / std::uint32_t(width))));
				if (rect.X + rect.W < viewRect.X || rect.Y + rect.H < viewRect.Y || rect.X > viewRect.X + viewRect.W || rect.Y > viewRect.Y + viewRect.H) {
					continue;
				}
				painter.FillRect(Rectf(rect.X, rect.Y, size, size), UiTheme::OverlayCaution, UiLayer::Content);
			}
		}
	}
}

#endif
