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
		: _kinds(DocumentChangeKind::None), _eventArea()
	{
	}

	void DocumentChanges::Add(DocumentChangeKind kinds)
	{
		_kinds |= kinds;
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
		_kinds |= other._kinds;
		for (const DocumentTileArea& tileArea : other._tileAreas) {
			AddTiles(tileArea.Layer, tileArea.Area);
		}
		if (other._eventArea.W > 0 && other._eventArea.H > 0) {
			UnionArea(_eventArea, other._eventArea);
		}
	}

	void DocumentChanges::Clear()
	{
		_kinds = DocumentChangeKind::None;
		_tileAreas.clear();
		_eventArea = Recti();
	}
}

#endif
