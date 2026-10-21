#include "EventTools.h"

#if defined(WITH_EDITOR)

#include "TileTools.h"
#include "../Common/ToolMath.h"
#include "../Document/Commands.h"
#include "../Input/ActionIds.h"
#include "../Events/EventPreviews.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Jazz2::Editor
{
	namespace
	{
		using ToolMath::WalkLine;

		// Tiles a fill places the event on at most, an open sky would take the whole level
		constexpr std::int32_t MaxEventFill = 65536;
		// Events shown where a rectangle would place them at most
		constexpr std::int32_t MaxRectPreviews = 1024;

		bool IsInGrid(Vector2i cell, Vector2i gridSize)
		{
			return (cell.X >= 0 && cell.Y >= 0 && cell.X < gridSize.X && cell.Y < gridSize.Y);
		}
	}

	SmallVector<Formats::OffGridEventRecord, 0> CopyOffGridEvents(const LevelDocument& document)
	{
		SmallVector<Formats::OffGridEventRecord, 0> events;
		events.reserve(document.OffGridEvents.size() + 1);
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			events.push_back(event);
		}
		return events;
	}

	std::int32_t RemapLinkedIds(EventStamp& stamp, const LevelDocument& document)
	{
		// Calls a function for each field of a link channel of an event and of those inside of it, as the catalog
		// describes them, see EventParamField::LinkChannel
		auto forEachLink = [](Formats::EventRecord& event, auto&& visit) -> bool {
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
			if (descriptor == nullptr) {
				return false;
			}
			return EventCatalog::ForEachContents(*descriptor, event, [&visit](const EventDescriptor& d, Formats::EventRecord& e) {
				bool changed = false;
				for (std::uint8_t i = 0; i < d.FieldCount; i++) {
					const EventParamField& field = d.Fields[i];
					if (field.LinkChannel != nullptr && field.LinkRole != EventLinkRole::None) {
						changed |= visit(field, e);
					}
				}
				return changed;
			});
		};

		// The events of the stamp, on the grid and off it, the ends of a link can be either
		SmallVector<Formats::EventRecord*, 0> events;
		for (EventStamp::Item& item : stamp.Items) {
			events.push_back(&item.Event);
		}
		for (EventStamp::OffGridItem& item : stamp.OffGrid) {
			events.push_back(&item.Event);
		}

		// The ends of links the stamp has, and their channels
		struct LinkEnd
		{
			std::uint32_t Channel;
			EventLinkRole Role;
			std::int32_t Id;
			std::int32_t Min;
			std::int32_t Max;
		};
		SmallVector<const char*, 4> channels;
		auto findChannel = [&channels](const char* channel) -> std::int32_t {
			for (std::size_t i = 0; i < channels.size(); i++) {
				if (std::strcmp(channels[i], channel) == 0) {
					return std::int32_t(i);
				}
			}
			return -1;
		};
		SmallVector<LinkEnd, 0> ends;
		for (Formats::EventRecord* event : events) {
			forEachLink(*event, [&](const EventParamField& field, Formats::EventRecord& e) {
				std::int32_t channel = findChannel(field.LinkChannel);
				if (channel < 0) {
					channel = std::int32_t(channels.size());
					channels.push_back(field.LinkChannel);
				}
				ends.push_back(LinkEnd{ std::uint32_t(channel), field.LinkRole, EventCatalog::ReadField(e, field), field.Min, field.Max });
				return false;
			});
		}
		if (ends.empty()) {
			return 0;
		}

		// Identifiers of the channels the level uses already, by the channel and the identifier
		auto makeKey = [](std::uint32_t channel, std::int32_t id) {
			return (std::uint64_t(channel) << 32) | std::uint32_t(id);
		};
		SmallVector<std::uint64_t, 0> used;
		auto markUsed = [&](const Formats::EventRecord& record) {
			Formats::EventRecord event = record;
			forEachLink(event, [&](const EventParamField& field, Formats::EventRecord& e) {
				std::int32_t channel = findChannel(field.LinkChannel);
				if (channel >= 0) {
					used.push_back(makeKey(std::uint32_t(channel), EventCatalog::ReadField(e, field)));
				}
				return false;
			});
		};
		for (const DocumentEvent& event : document.Events) {
			markUsed(event.Record);
		}
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			markUsed(event.Event);
		}
		std::sort(used.begin(), used.end());
		used.truncate(std::size_t(std::unique(used.begin(), used.end()) - used.begin()));
		// Only an identifier the level uses already is changed, so placing the stamp again and again gives each copy its own
		SmallVector<std::uint64_t, 0> usedByLevel = used;
		auto isUsed = [](const SmallVectorImpl<std::uint64_t>& keys, std::uint64_t key) {
			return std::binary_search(keys.begin(), keys.end(), key);
		};
		auto markKey = [](SmallVectorImpl<std::uint64_t>& keys, std::uint64_t key) {
			auto it = std::lower_bound(keys.begin(), keys.end(), key);
			if (it == keys.end() || *it != key) {
				keys.insert(it, key);
			}
		};
		// Those the stamp keeps aren't given to another link of it
		for (const LinkEnd& end : ends) {
			markKey(used, makeKey(end.Channel, end.Id));
		}

		// Identifiers the stamp has the whole link of: a source with a target, or more peers
		struct Remap
		{
			std::uint32_t Channel;
			std::int32_t From;
			std::int32_t To;
		};
		SmallVector<Remap, 8> remaps;
		for (std::size_t i = 0; i < ends.size(); i++) {
			const LinkEnd& end = ends[i];
			std::uint64_t key = makeKey(end.Channel, end.Id);
			bool done = false;
			for (const Remap& remap : remaps) {
				done |= (remap.Channel == end.Channel && remap.From == end.Id);
			}
			if (done || !isUsed(usedByLevel, key)) {
				continue;
			}
			bool hasSource = false, hasTarget = false;
			std::int32_t peers = 0;
			std::int32_t lowest = INT32_MIN, highest = INT32_MAX;
			for (const LinkEnd& other : ends) {
				if (other.Channel != end.Channel || other.Id != end.Id) {
					continue;
				}
				hasSource |= (other.Role == EventLinkRole::Source);
				hasTarget |= (other.Role == EventLinkRole::Target);
				peers += (other.Role == EventLinkRole::Peer ? 1 : 0);
				// The new one has to fit into all fields that get it
				if (other.Max > other.Min) {
					lowest = std::max<std::int32_t>(lowest, other.Min);
					highest = std::min<std::int32_t>(highest, other.Max);
				}
			}
			if (!(hasSource && hasTarget) && peers < 2) {
				continue;
			}

			// The first free one from the lowest, used ones are a few at most, so it's found soon
			std::int32_t newId = -1;
			bool found = false;
			for (std::int64_t candidate = std::max<std::int32_t>(lowest, 0); candidate <= highest; candidate++) {
				if (!isUsed(used, makeKey(end.Channel, std::int32_t(candidate)))) {
					newId = std::int32_t(candidate);
					found = true;
					break;
				}
			}
			if (!found) {
				// All identifiers are taken, the copy stays joined to the original
				continue;
			}
			markKey(used, makeKey(end.Channel, newId));
			remaps.push_back(Remap{ end.Channel, end.Id, newId });
		}
		if (remaps.empty()) {
			return 0;
		}

		// A new identifier is none the stamp had, so one changed isn't changed again by another
		for (Formats::EventRecord* event : events) {
			forEachLink(*event, [&](const EventParamField& field, Formats::EventRecord& e) {
				std::int32_t channel = findChannel(field.LinkChannel);
				std::int32_t id = EventCatalog::ReadField(e, field);
				for (const Remap& remap : remaps) {
					if (std::int32_t(remap.Channel) == channel && remap.From == id) {
						EventCatalog::WriteField(e, field, remap.To);
						return true;
					}
				}
				return false;
			});
		}
		return std::int32_t(remaps.size());
	}

	bool EventSelection::Contains(std::uint32_t cell) const
	{
		return std::binary_search(_cells.begin(), _cells.end(), cell);
	}

	bool EventSelection::ContainsOffGrid(std::int32_t index) const
	{
		return std::binary_search(_offGrid.begin(), _offGrid.end(), index);
	}

	void EventSelection::Clear()
	{
		_cells.clear();
		_offGrid.clear();
		_revision++;
	}

	void EventSelection::SetOffGrid(std::int32_t index)
	{
		_cells.clear();
		_offGrid.clear();
		if (index >= 0) {
			_offGrid.push_back(index);
		}
		_revision++;
	}

	void EventSelection::Set(std::uint32_t cell)
	{
		_cells.clear();
		_cells.push_back(cell);
		_offGrid.clear();
		_revision++;
	}

	void EventSelection::Add(std::uint32_t cell)
	{
		auto it = std::lower_bound(_cells.begin(), _cells.end(), cell);
		if (it == _cells.end() || *it != cell) {
			_cells.insert(it, cell);
			_revision++;
		}
	}

	void EventSelection::AddOffGrid(std::int32_t index)
	{
		if (index < 0) {
			return;
		}
		auto it = std::lower_bound(_offGrid.begin(), _offGrid.end(), index);
		if (it == _offGrid.end() || *it != index) {
			_offGrid.insert(it, index);
			_revision++;
		}
	}

	void EventSelection::Remove(std::uint32_t cell)
	{
		auto it = std::lower_bound(_cells.begin(), _cells.end(), cell);
		if (it != _cells.end() && *it == cell) {
			_cells.erase(it);
			_revision++;
		}
	}

	void EventSelection::RemoveOffGrid(std::int32_t index)
	{
		auto it = std::lower_bound(_offGrid.begin(), _offGrid.end(), index);
		if (it != _offGrid.end() && *it == index) {
			_offGrid.erase(it);
			_revision++;
		}
	}

	void EventSelection::Assign(SmallVectorImpl<std::uint32_t>&& cells)
	{
		_offGrid.clear();
		_cells.clear();
		for (std::uint32_t cell : cells) {
			_cells.push_back(cell);
		}
		std::sort(_cells.begin(), _cells.end());
		_cells.truncate(std::size_t(std::unique(_cells.begin(), _cells.end()) - _cells.begin()));
		_revision++;
	}

	void EventSelection::AssignOffGrid(SmallVectorImpl<std::int32_t>&& indices)
	{
		_offGrid.clear();
		for (std::int32_t index : indices) {
			if (index >= 0) {
				_offGrid.push_back(index);
			}
		}
		std::sort(_offGrid.begin(), _offGrid.end());
		_offGrid.truncate(std::size_t(std::unique(_offGrid.begin(), _offGrid.end()) - _offGrid.begin()));
		_revision++;
	}

	void EventSelection::DropEmpty(const LevelDocument& document)
	{
		const auto& current = document.OffGridEvents;
		bool changed = false;
		if (!_offGrid.empty() && _knownValid && _known.size() != current.size()) {
			// Events were added or removed, both keep their order otherwise, so the events as they were are matched in
			// order with those there are now, an index of each that is still there
			auto isSame = [](const Formats::OffGridEventRecord& a, const Formats::OffGridEventRecord& b) {
				return (a.X == b.X && a.Y == b.Y && LevelDocument::AreEventsEqual(a.Event, b.Event));
			};
			SmallVector<std::int32_t, 0> moved;
			moved.resize(_known.size(), -1);
			std::size_t j = 0, k = 0;
			if (current.size() < _known.size()) {
				for (; j < _known.size(); j++) {
					if (k < current.size() && isSame(_known[j], current[k])) {
						moved[j] = std::int32_t(k++);
					}
				}
			} else {
				for (; k < current.size(); k++) {
					if (j < _known.size() && isSame(_known[j], current[k])) {
						moved[j++] = std::int32_t(k);
					}
				}
			}
			std::size_t kept = 0;
			for (std::size_t i = 0; i < _offGrid.size(); i++) {
				std::int32_t index = _offGrid[i];
				std::int32_t now = (index >= 0 && std::size_t(index) < moved.size() ? moved[index] : -1);
				if (now >= 0) {
					changed |= (now != index);
					_offGrid[kept++] = now;
				}
			}
			changed |= (kept != _offGrid.size());
			_offGrid.truncate(kept);
		} else {
			std::size_t kept = 0;
			for (std::size_t i = 0; i < _offGrid.size(); i++) {
				if (_offGrid[i] < std::int32_t(current.size())) {
					_offGrid[kept++] = _offGrid[i];
				}
			}
			changed |= (kept != _offGrid.size());
			_offGrid.truncate(kept);
		}
		_known.clear();
		_known.reserve(current.size());
		for (const Formats::OffGridEventRecord& event : current) {
			_known.push_back(event);
		}
		_knownValid = true;

		Vector2i gridSize = document.GetEventGridSize();
		std::size_t target = 0;
		for (std::size_t i = 0; i < _cells.size(); i++) {
			std::uint32_t cell = _cells[i];
			std::int32_t x = std::int32_t(cell % std::uint32_t(std::max<std::int32_t>(gridSize.X, 1)));
			std::int32_t y = std::int32_t(cell / std::uint32_t(std::max<std::int32_t>(gridSize.X, 1)));
			if (gridSize.X > 0 && y < gridSize.Y && !LevelDocument::IsEmptyEvent(document.GetEvent(x, y))) {
				_cells[target++] = cell;
			}
		}
		changed |= (target != _cells.size());
		_cells.truncate(target);
		if (changed) {
			_revision++;
		}
	}

	EventTool::EventTool(IToolHost& host)
		: EditorTool(host), _mode(Mode::None), _marquee(MarqueeKind::Replace), _overwrite(false), _offGridIndex(-1)
	{
	}

	EventTool::~EventTool()
	{
	}

	EditorToolType EventTool::GetType() const
	{
		return EditorToolType::Event;
	}

	void EventTool::OnDeactivate()
	{
		Cancel();
	}

	Vector2i EventTool::GetCell(const ToolPointer& pointer)
	{
		std::int32_t spriteLayer = _host.GetDocument().GetSpriteLayerIndex();
		LayerView view = _host.GetLayerView(spriteLayer >= 0 ? spriteLayer : 0);
		return view.WorldToCell(view.ScreenToWorld(pointer.Screen));
	}

	void EventTool::OnPointerDown(const ToolPointer& pointer)
	{
		if (_mode != Mode::None) {
			return;
		}

		LevelDocument& document = _host.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (spriteLayer < 0) {
			return;
		}

		Vector2i cell = GetCell(pointer);
		Vector2i gridSize = document.GetEventGridSize();
		bool inside = IsInGrid(cell, gridSize);
		std::uint32_t index = (inside ? std::uint32_t(cell.X + cell.Y * gridSize.X) : 0);
		bool hasEvent = (inside && !LevelDocument::IsEmptyEvent(document.GetEvent(cell.X, cell.Y)));
		EventSelection& selection = _host.GetEventSelection();
		_start = _current = cell;
		bool ctrl = (pointer.Modifiers & KeyMod::Ctrl) != 0;
		bool shift = (pointer.Modifiers & KeyMod::Shift) != 0;
		// Only selecting, Shift adds to the selection and Ctrl takes away from it what is clicked or dragged over, once the
		// pointer is released, as the select tool has it; a drag with them selects a rectangle even from an event
		bool selectOnly = IsSelectOnly();
		MarqueeKind selectKind = (ctrl ? MarqueeKind::Subtract : MarqueeKind::Add);

		// Events off the grid are drawn over those on it, so they're found first
		std::int32_t offGrid = FindOffGrid(pointer.Screen);
		if (offGrid >= 0 && (pointer.Button == MouseButton::Left || pointer.Button == MouseButton::Right)) {
			const Formats::OffGridEventRecord& record = document.OffGridEvents[offGrid];
			if ((pointer.Modifiers & KeyMod::Alt) != 0) {
				_host.GetEventBrush() = EventStamp::FromEvent(record.Event);
				selection.Clear();
				PlaceEventsAgain();
				return;
			}
			if (selectOnly && pointer.Button == MouseButton::Left && (ctrl || shift)) {
				BeginMarquee(selectKind, offGrid);
				return;
			}
			if (!_host.IsLayerEditable(spriteLayer)) {
				_host.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
				return;
			}
			if (pointer.Button == MouseButton::Right) {
				SmallVector<Formats::OffGridEventRecord, 0> events = CopyOffGridEvents(document);
				events.erase(events.begin() + offGrid);
				// Not selected anymore while the indices are still of the events as they are, the indices of those after it
				// are one less then, the selection finds them again
				selection.DropEmpty(document);
				selection.RemoveOffGrid(offGrid);
				// TRANSLATORS: Name of an undo step in the history, after events were erased
				_host.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), _x("editor", "Erase Events")));
				selection.DropEmpty(document);
				return;
			}
			if ((pointer.Modifiers & (KeyMod::Ctrl | KeyMod::Shift)) != 0) {
				// Added to the selection or taken out of it
				if (selection.ContainsOffGrid(offGrid)) {
					selection.RemoveOffGrid(offGrid);
				} else {
					selection.AddOffGrid(offGrid);
				}
				return;
			}
			if (!selection.ContainsOffGrid(offGrid)) {
				selection.SetOffGrid(offGrid);
			}
			// Dragged, the selected events go with it
			LayerView view = _host.GetLayerView(spriteLayer);
			_mode = Mode::MoveOffGrid;
			_offGridIndex = offGrid;
			_gizmoGrab = Vector2f(float(record.X), float(record.Y)) - view.ScreenToWorld(pointer.Screen);
			_offGridTarget = Vector2f(float(record.X), float(record.Y));
			return;
		}

		if (ctrl && shift && pointer.Button == MouseButton::Left && !selectOnly) {
			// The event of the brush off the grid, exactly where it's clicked
			const EventStamp& brush = _host.GetEventBrush();
			if (!brush.IsSingle()) {
				_host.ShowMessage(_x("editor", "Only a single event can be placed off the grid"));
				return;
			}
			if (!_host.IsLayerEditable(spriteLayer)) {
				_host.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
				return;
			}
			Vector2f world = _host.GetLayerView(spriteLayer).ScreenToWorld(pointer.Screen);
			if (world.X < 0.0f || world.Y < 0.0f || world.X >= gridSize.X * 32.0f || world.Y >= gridSize.Y * 32.0f) {
				return;
			}
			SmallVector<Formats::OffGridEventRecord, 0> events = CopyOffGridEvents(document);
			Formats::OffGridEventRecord& record = events.emplace_back();
			// Rounded up at the far edge it would be just out of the level, where nothing can select it
			record.X = std::uint32_t(std::min<std::int32_t>(std::int32_t(std::round(world.X)), gridSize.X * 32 - 1));
			record.Y = std::uint32_t(std::min<std::int32_t>(std::int32_t(std::round(world.Y)), gridSize.Y * 32 - 1));
			record.Event = brush.Items[0].Event;
			LevelDocument::NormalizeEvent(record.Event);
			std::int32_t placedIndex = std::int32_t(events.size()) - 1;
			// TRANSLATORS: Name of an undo step in the history, after events were placed
			_host.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), _x("editor", "Place Events")));
			selection.SetOffGrid(placedIndex);
			return;
		}

		if (pointer.Button == MouseButton::Right) {
			if (!_host.IsLayerEditable(spriteLayer)) {
				_host.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
				return;
			}
			_mode = Mode::Erase;
			_stroke = std::make_unique<SetEventsCommand>(document, _x("editor", "Erase Events"));
			EraseAt(cell);
			return;
		}
		if (pointer.Button != MouseButton::Left) {
			return;
		}

		if (!ctrl && (pointer.Modifiers & KeyMod::Alt) == 0) {
			// A handle of the selected event comes first, it can be over another event; with Shift only the arrow of a
			// direction, which is snapped then, Shift places over other events otherwise
			SmallVector<EventGizmo, 4> gizmos;
			Vector2i gizmoCell;
			std::int32_t hit = (GetGizmos(gizmos, gizmoCell) ? FindGizmo(gizmos, pointer.Screen) : -1);
			if (hit >= 0 && shift && gizmos[hit].Kind != EventGizmoKind::Direction) {
				hit = -1;
			}
			if (hit >= 0) {
				if (!_host.IsLayerEditable(spriteLayer)) {
					_host.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
					return;
				}
				LayerView view = _host.GetLayerView(spriteLayer);
				_mode = Mode::Gizmo;
				_gizmo = gizmos[hit];
				_gizmoCell = gizmoCell;
				_gizmoGrab = _gizmo.World - view.ScreenToWorld(pointer.Screen);
				// TRANSLATORS: Name of an undo step in the history, after an event was changed by dragging its handle
				_stroke = std::make_unique<SetEventsCommand>(document, _x("editor", "Change Event"));
				return;
			}
		}
		if ((pointer.Modifiers & KeyMod::Alt) != 0) {
			// The event becomes the stamp
			if (hasEvent) {
				_host.GetEventBrush() = EventStamp::FromEvent(document.GetEvent(cell.X, cell.Y));
				selection.Clear();
				PlaceEventsAgain();
			}
			return;
		}

		if (selectOnly) {
			if (ctrl || shift) {
				BeginMarquee(selectKind, -1);
				return;
			}
			if (hasEvent) {
				if (!selection.Contains(index)) {
					selection.Set(index);
				}
				_mode = Mode::Move;
				return;
			}
			// A click on an empty spot ends the selection, a drag from there selects the events in a rectangle instead
			BeginMarquee(MarqueeKind::Replace, -1);
			return;
		}

		if (hasEvent && !shift) {
			if (ctrl) {
				if (selection.Contains(index)) {
					selection.Remove(index);
				} else {
					selection.Add(index);
				}
				return;
			}
			if (!selection.Contains(index)) {
				selection.Set(index);
			}
			_mode = Mode::Move;
			return;
		}

		if (ctrl || _host.GetEventBrush().IsEmpty()) {
			BeginMarquee(ctrl ? MarqueeKind::Add : MarqueeKind::Replace, -1);
			return;
		}

		if (!selection.IsEmpty() && !shift) {
			// The first click away from the selection only ends it, so nothing is placed by accident
			selection.Clear();
			return;
		}

		if (!_host.IsLayerEditable(spriteLayer)) {
			_host.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
			return;
		}
		_overwrite = shift;
		EventPlaceMode placeMode = GetSettings().EventMode;
		if (placeMode != EventPlaceMode::Paint && _host.GetEventBrush().IsSingle()) {
			if (placeMode == EventPlaceMode::Rectangle) {
				// On every tile of a rectangle dragged, when it's let go
				_mode = Mode::PlaceRect;
				return;
			}
			FillAt(cell);
			return;
		}
		_mode = Mode::Paint;
		_stroke = std::make_unique<SetEventsCommand>(document, _x("editor", "Place Events"));
		if (!_host.GetEventBrush().IsSingle()) {
			// Warps and triggers of the stamp placed before joined the level, this copy gets its own
			RemapLinkedIds(_host.GetEventBrush(), document);
		}
		PaintAt(cell);
	}

	void EventTool::OnPointerMove(const ToolPointer& pointer)
	{
		Vector2i cell = GetCell(pointer);
		switch (_mode) {
			case Mode::Paint:
				// A stamp of more events is placed only where it was clicked, it would cover itself otherwise
				if (_host.GetEventBrush().IsSingle() && cell != _current) {
					WalkLine(_current, cell, [this](Vector2i c) { PaintAt(c); });
				}
				_current = cell;
				break;
			case Mode::Erase:
				if (cell != _current) {
					WalkLine(_current, cell, [this](Vector2i c) { EraseAt(c); });
				}
				_current = cell;
				break;
			case Mode::Move:
			case Mode::Marquee:
			case Mode::PlaceRect:
				_current = cell;
				break;
			case Mode::MoveOffGrid:
				_offGridTarget = GetOffGridTarget(pointer);
				break;
			case Mode::Gizmo: {
				LevelDocument& document = _host.GetDocument();
				LayerView view = _host.GetLayerView(document.GetSpriteLayerIndex());
				Formats::EventRecord event = document.GetEvent(_gizmoCell.X, _gizmoCell.Y);
				// Shown in the level as it's dragged, recorded as one step when it's released; Shift keeps an arrow to the
				// 8 directions
				bool snap = ((pointer.Modifiers & KeyMod::Shift) != 0);
				if (EventGizmos::Apply(event, _gizmoCell, document.GetEventGridSize().X * 32, _gizmo, view.ScreenToWorld(pointer.Screen) + _gizmoGrab, snap)) {
					_stroke->Set(_gizmoCell.X, _gizmoCell.Y, event);
					document.SetEvent(_gizmoCell.X, _gizmoCell.Y, event);
				}
				break;
			}
			default:
				break;
		}
	}

	bool EventTool::GetGizmos(SmallVectorImpl<EventGizmo>& gizmos, Vector2i& cell)
	{
		LevelDocument& document = _host.GetDocument();
		EventSelection& selection = _host.GetEventSelection();
		Vector2i gridSize = document.GetEventGridSize();
		// Only of a single event of the grid selected alone
		if (selection.GetCellCount() != 1 || selection.HasOffGrid() || gridSize.X <= 0) {
			return false;
		}
		std::uint32_t index = selection.GetCells()[0];
		cell = Vector2i(std::int32_t(index % std::uint32_t(gridSize.X)), std::int32_t(index / std::uint32_t(gridSize.X)));
		if (!IsInGrid(cell, gridSize)) {
			return false;
		}
		EventGizmos::Collect(document.GetEvent(cell.X, cell.Y), cell, gridSize.X * 32, gizmos);
		return !gizmos.empty();
	}

	std::int32_t EventTool::FindOffGrid(Vector2f screen)
	{
		LevelDocument& document = _host.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (spriteLayer < 0 || document.OffGridEvents.empty()) {
			return -1;
		}
		// The tile its mark takes around it
		Vector2f world = _host.GetLayerView(spriteLayer).ScreenToWorld(screen);
		for (std::int32_t i = std::int32_t(document.OffGridEvents.size()) - 1; i >= 0; i--) {
			const Formats::OffGridEventRecord& record = document.OffGridEvents[i];
			if (std::abs(world.X - float(record.X)) <= 16.0f && std::abs(world.Y - float(record.Y)) <= 16.0f) {
				return i;
			}
		}
		return -1;
	}

	Vector2f EventTool::GetOffGridTarget(const ToolPointer& pointer)
	{
		LayerView view = _host.GetLayerView(_host.GetDocument().GetSpriteLayerIndex());
		Vector2f world = view.ScreenToWorld(pointer.Screen) + _gizmoGrab;
		if ((pointer.Modifiers & KeyMod::Shift) != 0) {
			// The center of the tile, where an event on the grid is
			world = Vector2f(std::floor(world.X / 32.0f) * 32.0f + 16.0f, std::floor(world.Y / 32.0f) * 32.0f + 16.0f);
		}
		// Inside of the level, as it's placed
		Vector2i gridSize = _host.GetDocument().GetEventGridSize();
		float maxX = std::max(gridSize.X * 32.0f - 1.0f, 0.0f), maxY = std::max(gridSize.Y * 32.0f - 1.0f, 0.0f);
		return Vector2f(std::clamp(std::round(world.X), 0.0f, maxX), std::clamp(std::round(world.Y), 0.0f, maxY));
	}

	std::int32_t EventTool::FindGizmo(ArrayView<const EventGizmo> gizmos, Vector2f screen)
	{
		LevelDocument& document = _host.GetDocument();
		LayerView view = _host.GetLayerView(document.GetSpriteLayerIndex());
		// The nearest within reach, the reach doesn't shrink with the view, so a handle can be grabbed zoomed out; it's
		// well beyond the small square drawn, more so for a finger
		UiSurface* surface = _host.GetSurface();
		float uiScale = (surface != nullptr ? surface->GetScale() : 1.0f);
		float reach = std::max((surface != nullptr && surface->TouchInput ? 18.0f : 10.0f) * uiScale, 8.0f);
		std::int32_t nearest = -1;
		float nearestDistance = reach * reach;
		for (std::size_t i = 0; i < gizmos.size(); i++) {
			float distance = (view.WorldToScreen(gizmos[i].World) - screen).SqrLength();
			if (distance <= nearestDistance) {
				nearestDistance = distance;
				nearest = std::int32_t(i);
			}
		}
		return nearest;
	}

	void EventTool::OnPointerUp(const ToolPointer& pointer)
	{
		switch (_mode) {
			case Mode::Paint:
			case Mode::Erase:
			case Mode::Gizmo:
				CommitStroke();
				break;
			case Mode::Move:
				if (_current != _start) {
					// The events off the grid by whole tiles too
					MoveSelection(_current - _start, (_current - _start) * 32);
				}
				break;
			case Mode::PlaceRect: {
				LevelDocument& document = _host.GetDocument();
				Vector2i gridSize = document.GetEventGridSize();
				const EventStamp& brush = _host.GetEventBrush();
				if (!brush.IsSingle()) {
					break;
				}
				std::int32_t x1 = std::max<std::int32_t>(std::min<std::int32_t>(_start.X, _current.X), 0), y1 = std::max<std::int32_t>(std::min<std::int32_t>(_start.Y, _current.Y), 0);
				std::int32_t x2 = std::min(std::max(_start.X, _current.X), gridSize.X - 1), y2 = std::min(std::max(_start.Y, _current.Y), gridSize.Y - 1);
				SmallVector<Vector2i, 0> cells;
				for (std::int32_t y = y1; y <= y2; y++) {
					for (std::int32_t x = x1; x <= x2; x++) {
						cells.push_back(Vector2i(x, y));
					}
				}
				PlaceOnCells(cells, _overwrite);
				break;
			}
			case Mode::MoveOffGrid: {
				_offGridTarget = GetOffGridTarget(pointer);
				Vector2i cellOffset, pixelOffset;
				if (GetOffGridDragOffset(cellOffset, pixelOffset)) {
					MoveSelection(cellOffset, pixelOffset);
				}
				_offGridIndex = -1;
				break;
			}
			case Mode::Marquee:
				EndMarquee();
				_offGridIndex = -1;
				break;
			default:
				break;
		}
		_mode = Mode::None;
	}

	bool EventTool::IsSelectOnly() const
	{
		return (GetSettings().EventMode == EventPlaceMode::Select);
	}

	void EventTool::PlaceEventsAgain()
	{
		if (_settings != nullptr) {
			_settings->PlaceEventsAgain();
		}
	}

	void EventTool::BeginMarquee(MarqueeKind kind, std::int32_t offGrid)
	{
		_mode = Mode::Marquee;
		_marquee = kind;
		_offGridIndex = offGrid;
	}

	void EventTool::EndMarquee()
	{
		LevelDocument& document = _host.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		EventSelection& selection = _host.GetEventSelection();
		if (_current == _start) {
			// Not dragged, a click on an empty spot only ends the selection, on an event it adds it, with Ctrl it toggles it
			if (_marquee == MarqueeKind::Replace) {
				selection.Clear();
				return;
			}
			bool toggle = (_marquee == MarqueeKind::Subtract);
			if (_offGridIndex >= 0 && _offGridIndex < std::int32_t(document.OffGridEvents.size())) {
				if (toggle && selection.ContainsOffGrid(_offGridIndex)) {
					selection.RemoveOffGrid(_offGridIndex);
				} else {
					selection.AddOffGrid(_offGridIndex);
				}
			} else if (IsInGrid(_start, gridSize) && !LevelDocument::IsEmptyEvent(document.GetEvent(_start.X, _start.Y))) {
				std::uint32_t cell = std::uint32_t(_start.X + _start.Y * gridSize.X);
				if (toggle && selection.Contains(cell)) {
					selection.Remove(cell);
				} else {
					selection.Add(cell);
				}
			}
			return;
		}
		if (gridSize.X <= 0) {
			return;
		}

		// The events within the tiles of the rectangle, those off the grid by the tile they're in
		std::int32_t x1 = std::max<std::int32_t>(std::min<std::int32_t>(_start.X, _current.X), 0), y1 = std::max<std::int32_t>(std::min<std::int32_t>(_start.Y, _current.Y), 0);
		std::int32_t x2 = std::min(std::max(_start.X, _current.X), gridSize.X - 1), y2 = std::min(std::max(_start.Y, _current.Y), gridSize.Y - 1);
		auto isInside = [x1, y1, x2, y2](std::int32_t x, std::int32_t y) {
			return (x >= x1 && x <= x2 && y >= y1 && y <= y2);
		};
		auto isCellInside = [&isInside, gridSize](std::uint32_t cell) {
			return isInside(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
		};
		auto isOffGridInside = [&isInside](const Formats::OffGridEventRecord& record) {
			return isInside(std::int32_t(record.X / 32), std::int32_t(record.Y / 32));
		};

		// The whole selection is assigned at once, adding or removing events one by one would move the rest each time
		SmallVector<std::uint32_t, 0> cells;
		SmallVector<std::int32_t, 0> offGrid;
		if (_marquee != MarqueeKind::Replace) {
			for (std::uint32_t cell : selection.GetCells()) {
				if (_marquee == MarqueeKind::Add || !isCellInside(cell)) {
					cells.push_back(cell);
				}
			}
			for (std::int32_t index : selection.GetOffGrid()) {
				if (index < std::int32_t(document.OffGridEvents.size()) && (_marquee == MarqueeKind::Add || !isOffGridInside(document.OffGridEvents[index]))) {
					offGrid.push_back(index);
				}
			}
		}
		if (_marquee != MarqueeKind::Subtract) {
			for (const DocumentEvent& event : document.Events) {
				if (isCellInside(event.Cell)) {
					cells.push_back(event.Cell);
				}
			}
			for (std::int32_t i = 0; i < std::int32_t(document.OffGridEvents.size()); i++) {
				if (isOffGridInside(document.OffGridEvents[i])) {
					offGrid.push_back(i);
				}
			}
		}
		selection.Assign(Death::move(cells));
		selection.AssignOffGrid(Death::move(offGrid));
	}

	void EventTool::Cancel()
	{
		if (_stroke != nullptr) {
			// The stroke was shown in the level already, but nothing was told of it yet, so nothing has to be told of
			// it being taken back either; the erased events that were selected are again
			_stroke->Seal();
			DocumentChanges changes;
			_stroke->Undo(_host.GetDocument(), changes);
			_stroke = nullptr;
			EventSelection& selection = _host.GetEventSelection();
			for (std::uint32_t cell : _erasedSelected) {
				selection.Add(cell);
			}
		}
		_erasedSelected.clear();
		_pendingOffGrid.clear();
		_offGridIndex = -1;
		_mode = Mode::None;
	}

	bool EventTool::GetOffGridDragOffset(Vector2i& cellOffset, Vector2i& pixelOffset)
	{
		const LevelDocument& document = _host.GetDocument();
		if (_offGridIndex < 0 || _offGridIndex >= std::int32_t(document.OffGridEvents.size())) {
			return false;
		}
		const Formats::OffGridEventRecord& grabbed = document.OffGridEvents[_offGridIndex];
		pixelOffset = Vector2i(std::int32_t(_offGridTarget.X) - std::int32_t(grabbed.X), std::int32_t(_offGridTarget.Y) - std::int32_t(grabbed.Y));
		cellOffset = Vector2i();
		if (_host.GetEventSelection().HasCells()) {
			// With events of the grid all move by whole tiles, so they stay where they were to each other
			cellOffset = Vector2i(std::int32_t(std::round(pixelOffset.X / 32.0f)), std::int32_t(std::round(pixelOffset.Y / 32.0f)));
			pixelOffset = cellOffset * 32;
		}
		return (pixelOffset != Vector2i() || cellOffset != Vector2i());
	}

	bool EventTool::IsBusy() const
	{
		return (_mode != Mode::None);
	}

	bool EventTool::IsMovingEvents() const
	{
		// A dragged handle changes the event where it is, it isn't moved anywhere
		return (_mode == Mode::Move || _mode == Mode::MoveOffGrid);
	}

	void EventTool::PaintAt(Vector2i cell)
	{
		LevelDocument& document = _host.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		// The events off the grid of the stamp are placed with the stroke, as one step, see CommitStroke()
		for (const EventStamp::OffGridItem& item : _host.GetEventBrush().OffGrid) {
			Vector2i target = cell * 32 + item.Offset;
			if (target.X >= 0 && target.Y >= 0 && target.X < gridSize.X * 32 && target.Y < gridSize.Y * 32) {
				Formats::OffGridEventRecord& record = _pendingOffGrid.emplace_back();
				record.X = std::uint32_t(target.X);
				record.Y = std::uint32_t(target.Y);
				record.Event = item.Event;
				LevelDocument::NormalizeEvent(record.Event);
			}
		}
		for (const EventStamp::Item& item : _host.GetEventBrush().Items) {
			Vector2i target = cell + item.Offset;
			if (!IsInGrid(target, gridSize)) {
				continue;
			}
			const Formats::EventRecord& existing = document.GetEvent(target.X, target.Y);
			if ((!_overwrite && !LevelDocument::IsEmptyEvent(existing)) || LevelDocument::AreEventsEqual(existing, item.Event)) {
				continue;
			}
			Formats::EventRecord event = item.Event;
			LevelDocument::NormalizeEvent(event);
			_stroke->Set(target.X, target.Y, event);
			document.SetEvent(target.X, target.Y, event);
		}
	}

	std::int32_t EventTool::PlaceOnCells(ArrayView<const Vector2i> cells, bool overwrite)
	{
		LevelDocument& document = _host.GetDocument();
		const EventStamp& brush = _host.GetEventBrush();
		if (brush.Items.empty()) {
			return 0;
		}
		if (!_host.IsLayerEditable(document.GetSpriteLayerIndex())) {
			_host.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
			return 0;
		}
		Vector2i gridSize = document.GetEventGridSize();
		Formats::EventRecord event = brush.Items[0].Event;
		LevelDocument::NormalizeEvent(event);
		auto command = std::make_unique<SetEventsCommand>(document, _x("editor", "Place Events"));
		std::int32_t placed = 0;
		for (const Vector2i& cell : cells) {
			if (!IsInGrid(cell, gridSize)) {
				continue;
			}
			const Formats::EventRecord& existing = document.GetEvent(cell.X, cell.Y);
			if ((!overwrite && !LevelDocument::IsEmptyEvent(existing)) || LevelDocument::AreEventsEqual(existing, event)) {
				continue;
			}
			command->Set(cell.X, cell.Y, event);
			placed++;
		}
		if (placed > 0) {
			_host.Execute(Death::move(command));
		}
		return placed;
	}

	void EventTool::FillAt(Vector2i cell)
	{
		LevelDocument& document = _host.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (!IsInGrid(cell, gridSize) || spriteLayer < 0) {
			return;
		}
		// The tiles joined at the sides with the same event as the one clicked, usually none, and solid or empty as it is,
		// so a room or a wall is filled, not the whole level
		const DocumentLayer& layer = document.Layers[spriteLayer];
		Formats::EventRecord target = document.GetEvent(cell.X, cell.Y);
		bool solid = (layer.Contains(cell.X, cell.Y) && ToolMath::IsSolidTile(layer.GetTile(cell.X, cell.Y)));
		BitArray visited(ValueInit, std::size_t(gridSize.X) * std::size_t(gridSize.Y));
		SmallVector<Vector2i, 0> pending;
		SmallVector<Vector2i, 0> cells;
		auto visit = [&](Vector2i c) {
			if (!IsInGrid(c, gridSize)) {
				return;
			}
			std::size_t index = std::size_t(c.Y) * std::size_t(gridSize.X) + std::size_t(c.X);
			if (visited[index] || std::int32_t(cells.size()) >= MaxEventFill) {
				return;
			}
			bool cellSolid = (layer.Contains(c.X, c.Y) && ToolMath::IsSolidTile(layer.GetTile(c.X, c.Y)));
			if (cellSolid != solid || !LevelDocument::AreEventsEqual(document.GetEvent(c.X, c.Y), target)) {
				return;
			}
			visited.set(index);
			cells.push_back(c);
			pending.push_back(c);
		};
		visit(cell);
		while (!pending.empty()) {
			Vector2i c = pending.back();
			pending.pop_back();
			visit(Vector2i(c.X - 1, c.Y));
			visit(Vector2i(c.X + 1, c.Y));
			visit(Vector2i(c.X, c.Y - 1));
			visit(Vector2i(c.X, c.Y + 1));
		}
		PlaceOnCells(cells, true);
		if (std::int32_t(cells.size()) >= MaxEventFill) {
			_host.ShowMessage(_x("editor", "The area is too large, only a part of it was filled"));
		}
	}

	void EventTool::EraseAt(Vector2i cell)
	{
		LevelDocument& document = _host.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		if (!IsInGrid(cell, gridSize) || LevelDocument::IsEmptyEvent(document.GetEvent(cell.X, cell.Y))) {
			return;
		}
		_stroke->Set(cell.X, cell.Y, LevelDocument::GetEmptyEvent());
		document.SetEvent(cell.X, cell.Y, LevelDocument::GetEmptyEvent());
		// Selected again if the stroke is canceled, see Cancel()
		EventSelection& selection = _host.GetEventSelection();
		std::uint32_t index = std::uint32_t(cell.X + cell.Y * gridSize.X);
		if (selection.Contains(index)) {
			selection.Remove(index);
			_erasedSelected.push_back(index);
		}
	}

	void EventTool::CommitStroke()
	{
		if (_stroke == nullptr) {
			return;
		}
		std::unique_ptr<SetEventsCommand> stroke = Death::move(_stroke);
		_erasedSelected.clear();
		// Changes that ended where they started merge into nothing, as a handle dragged back
		stroke->Seal();
		if (_pendingOffGrid.empty()) {
			if (!stroke->IsEmpty()) {
				// The document has the changes already, executing them again changes nothing but records them
				_host.Execute(Death::move(stroke));
			}
			return;
		}
		// The events off the grid of the stamp with those on it, as one step
		LevelDocument& document = _host.GetDocument();
		_host.BeginChanges(_x("editor", "Place Events"));
		if (!stroke->IsEmpty()) {
			_host.Execute(Death::move(stroke));
		}
		SmallVector<Formats::OffGridEventRecord, 0> events = CopyOffGridEvents(document);
		for (const Formats::OffGridEventRecord& record : _pendingOffGrid) {
			events.push_back(record);
		}
		_pendingOffGrid.clear();
		_host.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), _x("editor", "Place Events")));
		_host.EndChanges();
	}

	void EventTool::MoveSelection(Vector2i cellOffset, Vector2i pixelOffset)
	{
		LevelDocument& document = _host.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (!_host.IsLayerEditable(spriteLayer)) {
			_host.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
			return;
		}

		Vector2i gridSize = document.GetEventGridSize();
		EventSelection& selection = _host.GetEventSelection();
		struct Moved
		{
			Vector2i From;
			Vector2i To;
			Formats::EventRecord Event;
		};
		SmallVector<Moved, 0> moved;
		for (std::uint32_t cell : selection.GetCells()) {
			Vector2i from(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
			Vector2i to = from + cellOffset;
			if (!IsInGrid(to, gridSize)) {
				_host.ShowMessage(_x("editor", "Events can't be moved out of the level"));
				return;
			}
			const Formats::EventRecord& event = document.GetEvent(from.X, from.Y);
			if (!LevelDocument::IsEmptyEvent(event)) {
				moved.push_back(Moved{ from, to, event });
			}
		}
		// The events off the grid by pixels, all of them stay inside of the level or none moves
		SmallVector<Formats::OffGridEventRecord, 0> offGrid;
		bool offGridMoved = false;
		if (selection.HasOffGrid() && pixelOffset != Vector2i()) {
			offGrid = CopyOffGridEvents(document);
			for (std::int32_t index : selection.GetOffGrid()) {
				if (index < 0 || index >= std::int32_t(offGrid.size())) {
					continue;
				}
				Formats::OffGridEventRecord& record = offGrid[index];
				std::int64_t x = std::int64_t(record.X) + pixelOffset.X, y = std::int64_t(record.Y) + pixelOffset.Y;
				if (x < 0 || y < 0 || x >= std::int64_t(gridSize.X) * 32 || y >= std::int64_t(gridSize.Y) * 32) {
					_host.ShowMessage(_x("editor", "Events can't be moved out of the level"));
					return;
				}
				record.X = std::uint32_t(x);
				record.Y = std::uint32_t(y);
				offGridMoved = true;
			}
		}
		if (moved.empty() && !offGridMoved) {
			return;
		}

		// TRANSLATORS: Name of an undo step in the history, after events were moved
		_host.BeginChanges(_x("editor", "Move Events"));
		if (!moved.empty()) {
			// All are taken away first, so events moved onto each other's tiles aren't lost
			auto command = std::make_unique<SetEventsCommand>(document, _x("editor", "Move Events"));
			for (const Moved& item : moved) {
				command->Set(item.From.X, item.From.Y, LevelDocument::GetEmptyEvent());
			}
			for (const Moved& item : moved) {
				command->Set(item.To.X, item.To.Y, item.Event);
			}
			_host.Execute(Death::move(command));
		}
		if (offGridMoved) {
			_host.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(offGrid), _x("editor", "Move Events")));
		}
		_host.EndChanges();

		// The events stay selected where they are now, those off the grid keep their indices
		SmallVector<std::uint32_t, 0> newCells;
		for (const Moved& item : moved) {
			newCells.push_back(std::uint32_t(item.To.X + item.To.Y * gridSize.X));
		}
		SmallVector<std::int32_t, 0> offGridIndices;
		for (std::int32_t index : selection.GetOffGrid()) {
			offGridIndices.push_back(index);
		}
		selection.Assign(Death::move(newCells));
		selection.AssignOffGrid(Death::move(offGridIndices));
	}

	void EventTool::DrawSelectedEvents(UiPainter& painter, const LayerView& view)
	{
		const LevelDocument& document = _host.GetDocument();
		const EventSelection& selection = _host.GetEventSelection();
		Vector2i gridSize = document.GetEventGridSize();
		if (selection.IsEmpty() || gridSize.X <= 0) {
			return;
		}
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		float tileSize = 32.0f * view.Scale;
		// Zoomed out the dashes couldn't be told apart, the selected events are only framed then, those of the grid by the
		// marks of the events (see EditorShell::DrawEvents())
		bool ants = (tileSize >= 8.0f);
		float animTime = _host.GetAnimTime();
		const Rectf& clip = painter.GetClip();
		auto drawFrame = [&](const Rectf& rect) {
			if (clip.W > 0.0f && (rect.X > clip.X + clip.W || rect.Y > clip.Y + clip.H || rect.X + rect.W < clip.X || rect.Y + rect.H < clip.Y)) {
				return;
			}
			if (!ants) {
				painter.Frame(rect, UiTheme::Accent, thickness, UiLayer::Overlay);
				return;
			}
			DrawSelectionEdge(painter, rect.X, rect.Y, rect.W, true, animTime);
			DrawSelectionEdge(painter, rect.X, rect.Y + rect.H - thickness, rect.W, true, animTime);
			DrawSelectionEdge(painter, rect.X, rect.Y, rect.H, false, animTime);
			DrawSelectionEdge(painter, rect.X + rect.W - thickness, rect.Y, rect.H, false, animTime);
		};

		if (ants) {
			auto cells = selection.GetCells();
			std::uint32_t first = 0, last = UINT32_MAX;
			if (clip.W > 0.0f && clip.H > 0.0f) {
				// Only the rows in the view, all events of a large level can be selected
				std::int32_t top = std::max<std::int32_t>(view.WorldToCell(view.ScreenToWorld(Vector2f(clip.X, clip.Y))).Y, 0);
				std::int32_t bottom = std::min<std::int32_t>(view.WorldToCell(view.ScreenToWorld(Vector2f(clip.X, clip.Y + clip.H))).Y, gridSize.Y - 1);
				first = std::uint32_t(top) * std::uint32_t(gridSize.X);
				last = (bottom >= top ? std::uint32_t(bottom + 1) * std::uint32_t(gridSize.X) : first);
			}
			for (auto it = std::lower_bound(cells.begin(), cells.end(), first); it != cells.end() && *it < last; ++it) {
				drawFrame(view.CellToScreen(Vector2i(std::int32_t(*it % std::uint32_t(gridSize.X)), std::int32_t(*it / std::uint32_t(gridSize.X)))));
			}
		}
		// Those off the grid by the tile their mark takes around their position
		for (std::int32_t index : selection.GetOffGrid()) {
			if (index >= 0 && index < std::int32_t(document.OffGridEvents.size())) {
				const Formats::OffGridEventRecord& record = document.OffGridEvents[index];
				Vector2f topLeft = view.WorldToScreen(Vector2f(float(record.X) - 16.0f, float(record.Y) - 16.0f));
				drawFrame(Rectf(std::round(topLeft.X), std::round(topLeft.Y), std::round(tileSize), std::round(tileSize)));
			}
		}
	}

	void EventTool::DrawOverlay(UiPainter& painter, const ToolPointer& hover, bool hoverValid)
	{
		LevelDocument& document = _host.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (spriteLayer < 0) {
			return;
		}
		LayerView view = _host.GetLayerView(spriteLayer);
		Vector2i gridSize = document.GetEventGridSize();
		EventPreviews& previews = _host.GetEventPreviews();
		float animTime = _host.GetAnimTime();
		float thickness = std::max(1.0f, std::round(painter.GetScale()));

		// The selected events, under the handles of a single one
		DrawSelectedEvents(painter, view);

		if (_mode == Mode::None || _mode == Mode::Gizmo) {
			SmallVector<EventGizmo, 4> gizmos;
			Vector2i gizmoCell;
			if (GetGizmos(gizmos, gizmoCell)) {
				std::int32_t hovered = (_mode == Mode::None && hoverValid ? FindGizmo(gizmos, hover.Screen) : -1);
				// Small, so the end of what it changes is still seen under it, it's grabbed from further away, see FindGizmo()
				float size = std::max(2.0f, std::round(painter.Px(2.5f)));
				std::int32_t activeIndex = -1;
				for (std::size_t i = 0; i < gizmos.size(); i++) {
					const EventGizmo& gizmo = gizmos[i];
					Vector2f pos = view.WorldToScreen(gizmo.World);
					if (gizmo.Kind == EventGizmoKind::ChainLength || gizmo.Kind == EventGizmoKind::Offset) {
						// What it's measured from
						painter.Line(view.WorldToScreen(gizmo.Anchor), pos, thickness, Colorf(1.0f, 1.0f, 1.0f, 0.6f), UiLayer::Overlay);
					}
					bool active = (std::int32_t(i) == hovered || (_mode == Mode::Gizmo && gizmo.Kind == _gizmo.Kind && gizmo.Field == _gizmo.Field));
					if (active) {
						activeIndex = std::int32_t(i);
					}
					// Centered on the nearest corner of pixels, where the arrow of a direction ends too, see EditorShell::DrawEvents()
					Rectf outer(std::round(pos.X) - size - thickness, std::round(pos.Y) - size - thickness, (size + thickness) * 2.0f, (size + thickness) * 2.0f);
					Rectf inner(outer.X + thickness, outer.Y + thickness, outer.W - thickness * 2.0f, outer.H - thickness * 2.0f);
					painter.FillRect(outer, Colorf(0.0f, 0.0f, 0.0f, 0.75f), UiLayer::Overlay);
					painter.FillRect(inner, active ? UiTheme::Accent : Colorf::White, UiLayer::Overlay);
				}
				// Arrows by the pointer, which way the handle pointed at or dragged goes (the surface draws them over
				// everything and leaves them out for a finger)
				UiSurface* surface = _host.GetSurface();
				if (activeIndex >= 0 && surface != nullptr) {
					Vector2f direction = EventGizmos::GetDragDirection(document.GetEvent(gizmoCell.X, gizmoCell.Y), gizmos[activeIndex]);
					constexpr float Sin22_5 = 0.38268343f;
					UiCursorOverlay overlay;
					if (direction.X == 0.0f && direction.Y == 0.0f) {
						overlay = UiCursorOverlay::Move;
					} else if (std::abs(direction.Y) < Sin22_5) {
						overlay = UiCursorOverlay::ResizeHorizontal;
					} else if (std::abs(direction.X) < Sin22_5) {
						overlay = UiCursorOverlay::ResizeVertical;
					} else {
						// Y goes down, so right and down is the diagonal of the top left and bottom right corners
						overlay = ((direction.X > 0.0f) == (direction.Y > 0.0f) ? UiCursorOverlay::ResizeDiagonalNWSE : UiCursorOverlay::ResizeDiagonalNESW);
					}
					surface->SetCursorOverlay(overlay);
				}
				if (_mode == Mode::Gizmo || hovered >= 0) {
					return;
				}
			}
		}

		// The tile the mark of an off-grid event takes around its position
		auto offGridRect = [&view](Vector2f pos) {
			Vector2f topLeft = view.WorldToScreen(pos - Vector2f(16.0f, 16.0f));
			return Rectf(topLeft.X, topLeft.Y, 32.0f * view.Scale, 32.0f * view.Scale);
		};
		const EventSelection& selection = _host.GetEventSelection();
		// The selected events where a drag would move them, on the grid by tiles, off it by pixels
		auto drawMoved = [&](Vector2i cellOffset, Vector2i pixelOffset) {
			for (std::uint32_t cell : selection.GetCells()) {
				Vector2i to(std::int32_t(cell % std::uint32_t(gridSize.X)) + cellOffset.X, std::int32_t(cell / std::uint32_t(gridSize.X)) + cellOffset.Y);
				Rectf rect = view.CellToScreen(to);
				const Formats::EventRecord& event = document.GetEvent(to.X - cellOffset.X, to.Y - cellOffset.Y);
				previews.DrawPlaced(painter, event, rect, view.Scale, 0.7f, animTime, Vector2f(to.X * 32.0f + 16.0f, to.Y * 32.0f + 16.0f));
				// An event that isn't moved too would be replaced there, see MoveSelection()
				bool inside = IsInGrid(to, gridSize);
				bool replaces = (inside && cellOffset != Vector2i() && !LevelDocument::IsEmptyEvent(document.GetEvent(to.X, to.Y)) &&
					!selection.Contains(std::uint32_t(to.X + to.Y * gridSize.X)));
				painter.Frame(rect, inside && !replaces ? UiTheme::Accent : UiTheme::Danger, thickness, UiLayer::Overlay);
			}
			for (std::int32_t index : selection.GetOffGrid()) {
				if (index < 0 || index >= std::int32_t(document.OffGridEvents.size())) {
					continue;
				}
				const Formats::OffGridEventRecord& record = document.OffGridEvents[index];
				Vector2f pos(float(record.X) + pixelOffset.X, float(record.Y) + pixelOffset.Y);
				Rectf rect = offGridRect(pos);
				bool inside = (pos.X >= 0.0f && pos.Y >= 0.0f && pos.X < gridSize.X * 32.0f && pos.Y < gridSize.Y * 32.0f);
				previews.DrawPlaced(painter, record.Event, rect, view.Scale, 0.7f, animTime, pos, false, true);
				painter.Frame(rect, inside ? UiTheme::Accent : UiTheme::Danger, thickness, UiLayer::Overlay);
			}
		};
		if (_mode == Mode::MoveOffGrid) {
			Vector2i cellOffset, pixelOffset;
			GetOffGridDragOffset(cellOffset, pixelOffset);
			drawMoved(cellOffset, pixelOffset);
			return;
		}
		if (_mode == Mode::None && hoverValid) {
			std::int32_t offGrid = FindOffGrid(hover.Screen);
			if (offGrid >= 0) {
				if (!selection.ContainsOffGrid(offGrid)) {
					const Formats::OffGridEventRecord& record = document.OffGridEvents[offGrid];
					painter.Frame(offGridRect(Vector2f(float(record.X), float(record.Y))), UiTheme::Cursor, thickness, UiLayer::Overlay);
				}
				return;
			}
		}

		if (_mode == Mode::Move) {
			// The selected events where they will be
			Vector2i offset = _current - _start;
			drawMoved(offset, offset * 32);
			return;
		}

		if (_mode == Mode::Marquee) {
			Vector2i from(std::min(_start.X, _current.X), std::min(_start.Y, _current.Y));
			Vector2i to(std::max(_start.X, _current.X), std::max(_start.Y, _current.Y));
			DrawCellsOutline(painter, view, from, to, UiTheme::Accent);
			return;
		}

		if (_mode == Mode::PlaceRect) {
			// The event on every tile it would be placed on
			Vector2i from(std::max<std::int32_t>(std::min<std::int32_t>(_start.X, _current.X), 0), std::max<std::int32_t>(std::min<std::int32_t>(_start.Y, _current.Y), 0));
			Vector2i to(std::min(std::max(_start.X, _current.X), gridSize.X - 1), std::min(std::max(_start.Y, _current.Y), gridSize.Y - 1));
			const EventStamp& brush = _host.GetEventBrush();
			if (!brush.Items.empty() && std::int64_t(to.X - from.X + 1) * std::int64_t(to.Y - from.Y + 1) <= MaxRectPreviews) {
				for (std::int32_t y = from.Y; y <= to.Y; y++) {
					for (std::int32_t x = from.X; x <= to.X; x++) {
						if (!_overwrite && !LevelDocument::IsEmptyEvent(document.GetEvent(x, y))) {
							continue;
						}
						previews.DrawPlaced(painter, brush.Items[0].Event, view.CellToScreen(Vector2i(x, y)), view.Scale, 0.5f, animTime,
							Vector2f(x * 32.0f + 16.0f, y * 32.0f + 16.0f));
					}
				}
			}
			DrawCellsOutline(painter, view, _start, _current, UiTheme::Accent);
			return;
		}

		if (!hoverValid) {
			return;
		}
		Vector2i cell = GetCell(hover);
		bool hasEvent = (IsInGrid(cell, gridSize) && !LevelDocument::IsEmptyEvent(document.GetEvent(cell.X, cell.Y)));
		bool shift = (hover.Modifiers & KeyMod::Shift) != 0;
		const EventStamp& brush = _host.GetEventBrush();
		if (_mode == Mode::Erase || (hasEvent && !shift && _mode == Mode::None) || brush.IsEmpty() || (_mode == Mode::None && !selection.IsEmpty() && !shift) ||
			IsSelectOnly()) {
			painter.Frame(view.CellToScreen(cell), _mode == Mode::Erase ? UiTheme::Danger : UiTheme::Cursor, thickness, UiLayer::Overlay);
			return;
		}

		// The events off the grid of the stamp, by pixels from the tile
		for (const EventStamp::OffGridItem& item : brush.OffGrid) {
			Vector2f pos(cell.X * 32.0f + item.Offset.X, cell.Y * 32.0f + item.Offset.Y);
			Rectf rect = offGridRect(pos);
			previews.DrawPlaced(painter, item.Event, rect, view.Scale, 0.6f, animTime, pos, false, true);
			painter.Frame(rect, UiTheme::Cursor, thickness, UiLayer::Overlay);
		}

		// Where the stamp would be placed
		for (const EventStamp::Item& item : brush.Items) {
			Vector2i target = cell + item.Offset;
			if (!IsInGrid(target, gridSize)) {
				continue;
			}
			Rectf rect = view.CellToScreen(target);
			bool blocked = (!shift && !LevelDocument::IsEmptyEvent(document.GetEvent(target.X, target.Y)));
			previews.DrawPlaced(painter, item.Event, rect, view.Scale, blocked ? 0.25f : 0.6f, animTime, Vector2f(target.X * 32.0f + 16.0f, target.Y * 32.0f + 16.0f));
			painter.Frame(rect, blocked ? UiTheme::Danger : UiTheme::Cursor, thickness, UiLayer::Overlay);
		}
	}

	StringView EventTool::GetHint() const
	{
		switch (GetSettings().EventMode) {
			case EventPlaceMode::Rectangle: return _x("editor", "Drag over empty tiles: place the event on all of them - Shift: over events too - click an event: select it");
			case EventPlaceMode::Fill: return _x("editor", "Click an empty tile: place the event on the area of such tiles joined to it - click an event: select it");
			case EventPlaceMode::Select: return _x("editor", "Click or drag: select - Shift: add - Ctrl: toggle or take away - Drag selected events: move them - Right: erase - Alt: pick");
			default: break;
		}
		return _x("editor", "Click: place or select - Drag: move - Right: erase - Alt: pick - Ctrl: select more - Ctrl+Shift: place off the grid");
	}

	void EventTool::BuildOptions(ToolOptionsBuilder& builder)
	{
		const ToolOptionItem modes[] = {
			// TRANSLATORS: How the event tool places events, by clicking, in a rectangle, by filling an area, or that it only selects them
			{ _x("editor", "Click"), EditorIcon::Brush, ActionIds::EventsModePaint },
			{ _x("editor", "Rectangle"), EditorIcon::Rectangle, ActionIds::EventsModeRectangle },
			{ _x("editor", "Fill"), EditorIcon::Fill, ActionIds::EventsModeFill },
			{ _x("editor", "Select"), EditorIcon::Select, ActionIds::EventsModeSelect },
		};
		// TRANSLATORS: Label of the choice of how the event tool places events, or that it only selects them
		builder.AddChoice(_x("editor", "Mode"), modes);
	}
}

#endif
