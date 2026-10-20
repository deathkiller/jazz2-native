#include "DocumentChanges.h"

#if defined(WITH_EDITOR)

#include <algorithm>

namespace Jazz2::Editor
{
	namespace
	{
		void UnionArea(Recti& target, const Recti& area)
		{
			if (target.W <= 0 || target.H <= 0) {
				target = area;
			} else {
				std::int32_t x1 = std::min(target.X, area.X);
				std::int32_t y1 = std::min(target.Y, area.Y);
				std::int32_t x2 = std::max(target.X + target.W, area.X + area.W);
				std::int32_t y2 = std::max(target.Y + target.H, area.Y + area.H);
				target = Recti(x1, y1, x2 - x1, y2 - y1);
			}
		}
	}

	DocumentChanges::DocumentChanges()
		: _kinds(DocumentChangeKind::None), _eventArea(), _tilesOutsideAreas(false), _eventsOutsideArea(false)
	{
	}

	void DocumentChanges::Add(DocumentChangeKind kinds)
	{
		if ((kinds & DocumentChangeKind::LayerStructure) != DocumentChangeKind::None) {
			// Not by a move, so the order doesn't tell what the layers are now
			AddLayerOrder({}, false);
		}
		// Without an area, so any of them may have changed
		if ((kinds & DocumentChangeKind::Tiles) != DocumentChangeKind::None) {
			_tilesOutsideAreas = true;
		}
		if ((kinds & DocumentChangeKind::Events) != DocumentChangeKind::None) {
			_eventsOutsideArea = true;
		}
		_kinds |= kinds;
	}

	void DocumentChanges::AddLayerMove(std::int32_t from, std::int32_t to, std::int32_t count)
	{
		if (from < 0 || from >= count || to < 0 || to >= count) {
			AddLayerOrder({}, false);
			return;
		}
		SmallVector<std::int32_t, 16> order;
		for (std::int32_t i = 0; i < count; i++) {
			order.push_back(i);
		}
		order.erase(order.begin() + from);
		order.insert(order.begin() + to, from);
		AddLayerOrder(order, true);
	}

	void DocumentChanges::AddLayerOrder(ArrayView<const std::int32_t> order, bool onlyMoved)
	{
		bool hadStructure = HasAny(DocumentChangeKind::LayerStructure);
		_kinds |= DocumentChangeKind::LayerStructure;
		// Changed otherwise before or now, or moved among another number of layers, which only a change of another kind does
		if (!onlyMoved || order.empty() || (hadStructure && _layerOrder.size() != order.size())) {
			_layerOrder.clear();
			return;
		}
		if (!hadStructure) {
			_layerOrder.assign(order.begin(), order.end());
			return;
		}
		// After the moves before, a layer now at i was at order[i] after them, so at _layerOrder[order[i]] before all of them
		SmallVector<std::int32_t, 0> combined;
		combined.reserve(order.size());
		for (std::int32_t from : order) {
			combined.push_back(_layerOrder[from]);
		}
		_layerOrder = Death::move(combined);
	}

	void DocumentChanges::AddTiles(std::int32_t layer, const Recti& area)
	{
		if (area.W <= 0 || area.H <= 0) {
			return;
		}

		_kinds |= DocumentChangeKind::Tiles;
		for (DocumentTileArea& tileArea : _tileAreas) {
			if (tileArea.Layer == layer) {
				UnionArea(tileArea.Area, area);
				return;
			}
		}
		_tileAreas.push_back({ layer, area });
	}

	void DocumentChanges::AddEvents(const Recti& area)
	{
		if (area.W <= 0 || area.H <= 0) {
			return;
		}

		_kinds |= DocumentChangeKind::Events;
		UnionArea(_eventArea, area);
	}

	void DocumentChanges::Add(const DocumentChanges& other)
	{
		if (other.HasAny(DocumentChangeKind::LayerStructure)) {
			AddLayerOrder(other._layerOrder, !other._layerOrder.empty());
		}
		_kinds |= other._kinds;
		for (const DocumentTileArea& tileArea : other._tileAreas) {
			AddTiles(tileArea.Layer, tileArea.Area);
		}
		if (other._eventArea.W > 0 && other._eventArea.H > 0) {
			UnionArea(_eventArea, other._eventArea);
		}
		_tilesOutsideAreas |= other._tilesOutsideAreas;
		_eventsOutsideArea |= other._eventsOutsideArea;
	}

	void DocumentChanges::Clear()
	{
		_kinds = DocumentChangeKind::None;
		_tileAreas.clear();
		_eventArea = Recti();
		_tilesOutsideAreas = false;
		_eventsOutsideArea = false;
		_layerOrder.clear();
	}
}

#endif
