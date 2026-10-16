#include "EventTools.h"

#if defined(WITH_EDITOR)

#include "../Document/Commands.h"
#include "../Events/EventPreviews.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Jazz2::Editor
{
	namespace
	{
		// Calls a function for every cell on a line between two cells, so a fast drag doesn't leave gaps
		template<class TFunc>
		void ForEachCellOnLine(Vector2i from, Vector2i to, TFunc&& func)
		{
			std::int32_t dx = std::abs(to.X - from.X), dy = -std::abs(to.Y - from.Y);
			std::int32_t sx = (from.X < to.X ? 1 : -1), sy = (from.Y < to.Y ? 1 : -1);
			std::int32_t error = dx + dy;
			Vector2i cell = from;
			while (true) {
				func(cell);
				if (cell == to) {
					break;
				}
				std::int32_t e2 = error * 2;
				if (e2 >= dy) {
					error += dy;
					cell.X += sx;
				}
				if (e2 <= dx) {
					error += dx;
					cell.Y += sy;
				}
			}
		}

		bool IsInGrid(Vector2i cell, Vector2i gridSize)
		{
			return (cell.X >= 0 && cell.Y >= 0 && cell.X < gridSize.X && cell.Y < gridSize.Y);
		}
	}

	EventStamp EventStamp::FromEvent(const Formats::EventRecord& event)
	{
		EventStamp stamp;
		Item& item = stamp.Items.emplace_back();
		item.Offset = Vector2i(0, 0);
		item.Event = event;
		return stamp;
	}

	std::int32_t EventStamp::RemapLinkedIds(const LevelDocument& document)
	{
		auto findField = [](std::uint16_t type, ParamType paramType) -> const EventParamField* {
			if (const EventDescriptor* descriptor = EventCatalog::Find(EventType(type))) {
				for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
					if (descriptor->Fields[i].Type == paramType) {
						return &descriptor->Fields[i];
					}
				}
			}
			return nullptr;
		};
		// The end of a link that is the target, the other end is what leads there
		auto isTarget = [](std::uint16_t type) {
			return (EventType(type) == EventType::WarpTarget || EventType(type) == EventType::TriggerArea);
		};

		std::int32_t changed = 0;
		for (ParamType paramType : { ParamType::WarpId, ParamType::TriggerId }) {
			// Identifiers the level uses already
			SmallVector<bool, 0> used;
			auto markUsed = [&](const Formats::EventRecord& event) {
				if (const EventParamField* field = findField(event.Type, paramType)) {
					std::int32_t id = EventCatalog::ReadField(event, *field);
					if (id >= 0) {
						if (std::size_t(id) >= used.size()) {
							used.resize(std::size_t(id) + 1, false);
						}
						used[id] = true;
					}
				}
			};
			for (const DocumentEvent& event : document.Events) {
				markUsed(event.Record);
			}
			for (const Item& item : Items) {
				markUsed(item.Event);
			}

			// Identifiers the stamp has both ends of
			SmallVector<std::int32_t, 8> remapped;
			for (const Item& item : Items) {
				const EventParamField* field = findField(item.Event.Type, paramType);
				if (field == nullptr || !isTarget(item.Event.Type)) {
					continue;
				}
				std::int32_t id = EventCatalog::ReadField(item.Event, *field);
				if (std::find(remapped.begin(), remapped.end(), id) != remapped.end()) {
					continue;
				}
				bool hasSource = false;
				for (const Item& other : Items) {
					const EventParamField* otherField = findField(other.Event.Type, paramType);
					if (otherField != nullptr && !isTarget(other.Event.Type) && EventCatalog::ReadField(other.Event, *otherField) == id) {
						hasSource = true;
						break;
					}
				}
				if (!hasSource) {
					continue;
				}

				std::int32_t newId = -1;
				for (std::int32_t candidate = 0; candidate <= field->Max; candidate++) {
					if (std::size_t(candidate) >= used.size() || !used[candidate]) {
						newId = candidate;
						break;
					}
				}
				if (newId < 0) {
					// All identifiers are taken, the copy stays joined to the original
					continue;
				}
				if (std::size_t(newId) >= used.size()) {
					used.resize(std::size_t(newId) + 1, false);
				}
				used[newId] = true;
				// Both, so the other ends, which get the new one, aren't remapped again
				remapped.push_back(id);
				remapped.push_back(newId);

				for (Item& other : Items) {
					const EventParamField* otherField = findField(other.Event.Type, paramType);
					if (otherField != nullptr && EventCatalog::ReadField(other.Event, *otherField) == id) {
						EventCatalog::WriteField(other.Event, *otherField, newId);
					}
				}
				changed++;
			}
		}
		return changed;
	}

	bool EventSelection::Contains(std::uint32_t cell) const
	{
		return std::binary_search(_cells.begin(), _cells.end(), cell);
	}

	void EventSelection::Clear()
	{
		_cells.clear();
		_offGrid = -1;
	}

	void EventSelection::SetOffGrid(std::int32_t index)
	{
		_cells.clear();
		_offGrid = index;
	}

	void EventSelection::Set(std::uint32_t cell)
	{
		_cells.clear();
		_cells.push_back(cell);
		_offGrid = -1;
	}

	void EventSelection::Add(std::uint32_t cell)
	{
		_offGrid = -1;
		auto it = std::lower_bound(_cells.begin(), _cells.end(), cell);
		if (it == _cells.end() || *it != cell) {
			_cells.insert(it, cell);
		}
	}

	void EventSelection::Remove(std::uint32_t cell)
	{
		auto it = std::lower_bound(_cells.begin(), _cells.end(), cell);
		if (it != _cells.end() && *it == cell) {
			_cells.erase(it);
		}
	}

	void EventSelection::Assign(SmallVectorImpl<std::uint32_t>&& cells)
	{
		_offGrid = -1;
		_cells.clear();
		for (std::uint32_t cell : cells) {
			_cells.push_back(cell);
		}
		std::sort(_cells.begin(), _cells.end());
		_cells.truncate(std::size_t(std::unique(_cells.begin(), _cells.end()) - _cells.begin()));
	}

	void EventSelection::DropEmpty(const LevelDocument& document)
	{
		if (_offGrid >= std::int32_t(document.OffGridEvents.size())) {
			_offGrid = -1;
		}
		Vector2i gridSize = document.GetEventGridSize();
		std::size_t target = 0;
		for (std::size_t i = 0; i < _cells.size(); i++) {
			std::uint32_t cell = _cells[i];
			std::int32_t x = std::int32_t(cell % std::uint32_t(std::max(gridSize.X, 1)));
			std::int32_t y = std::int32_t(cell / std::uint32_t(std::max(gridSize.X, 1)));
			if (gridSize.X > 0 && y < gridSize.Y && !LevelDocument::IsEmptyEvent(document.GetEvent(x, y))) {
				_cells[target++] = cell;
			}
		}
		_cells.truncate(target);
	}

	EventTool::EventTool(IToolHost& host)
		: EditorTool(host), _mode(Mode::None), _overwrite(false), _offGridIndex(-1)
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

		// Events off the grid are drawn over those on it, so they're found first
		std::int32_t offGrid = FindOffGrid(pointer.Screen);
		if (offGrid >= 0 && (pointer.Button == MouseButton::Left || pointer.Button == MouseButton::Right)) {
			const Formats::OffGridEventRecord& record = document.OffGridEvents[offGrid];
			if ((pointer.Modifiers & KeyMod::Alt) != 0) {
				_host.GetEventBrush() = EventStamp::FromEvent(record.Event);
				selection.Clear();
				return;
			}
			if (!_host.IsLayerEditable(spriteLayer)) {
				_host.ShowMessage(_("The sprite layer is locked or hidden"));
				return;
			}
			if (pointer.Button == MouseButton::Right) {
				SmallVector<Formats::OffGridEventRecord, 0> events;
				for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
					if (std::int32_t(i) != offGrid) {
						events.push_back(document.OffGridEvents[i]);
					}
				}
				_host.Execute(std::make_unique<SetOffGridEventsCommand>(document, std::move(events), _("Erase Events")));
				// The indices of those after it changed
				if (selection.HasOffGrid()) {
					selection.Clear();
				}
				return;
			}
			LayerView view = _host.GetLayerView(spriteLayer);
			_mode = Mode::MoveOffGrid;
			_offGridIndex = offGrid;
			_gizmoGrab = Vector2f(float(record.X), float(record.Y)) - view.ScreenToWorld(pointer.Screen);
			_offGridTarget = Vector2f(float(record.X), float(record.Y));
			selection.SetOffGrid(offGrid);
			return;
		}

		bool ctrlShift = ((pointer.Modifiers & KeyMod::Ctrl) != 0 && (pointer.Modifiers & KeyMod::Shift) != 0);
		if (ctrlShift && pointer.Button == MouseButton::Left) {
			// The event of the brush off the grid, exactly where it's clicked
			const EventStamp& brush = _host.GetEventBrush();
			if (!brush.IsSingle()) {
				_host.ShowMessage(_("Only a single event can be placed off the grid"));
				return;
			}
			if (!_host.IsLayerEditable(spriteLayer)) {
				_host.ShowMessage(_("The sprite layer is locked or hidden"));
				return;
			}
			Vector2f world = _host.GetLayerView(spriteLayer).ScreenToWorld(pointer.Screen);
			if (world.X < 0.0f || world.Y < 0.0f || world.X >= gridSize.X * 32.0f || world.Y >= gridSize.Y * 32.0f) {
				return;
			}
			SmallVector<Formats::OffGridEventRecord, 0> events;
			for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
				events.push_back(event);
			}
			Formats::OffGridEventRecord& record = events.emplace_back();
			record.X = std::uint32_t(std::round(world.X));
			record.Y = std::uint32_t(std::round(world.Y));
			record.Event = brush.Items[0].Event;
			LevelDocument::NormalizeEvent(record.Event);
			std::int32_t placedIndex = std::int32_t(events.size()) - 1;
			_host.Execute(std::make_unique<SetOffGridEventsCommand>(document, std::move(events), _("Place Events")));
			selection.SetOffGrid(placedIndex);
			return;
		}

		if (pointer.Button == MouseButton::Right) {
			if (!_host.IsLayerEditable(spriteLayer)) {
				_host.ShowMessage(_("The sprite layer is locked or hidden"));
				return;
			}
			_mode = Mode::Erase;
			_stroke = std::make_unique<SetEventsCommand>(document, _("Erase Events"));
			EraseAt(cell);
			return;
		}
		if (pointer.Button != MouseButton::Left) {
			return;
		}

		bool ctrl = (pointer.Modifiers & KeyMod::Ctrl) != 0;
		bool shift = (pointer.Modifiers & KeyMod::Shift) != 0;
		if (!ctrl && !shift && (pointer.Modifiers & KeyMod::Alt) == 0) {
			// A handle of the selected event comes first, it can be over another event
			SmallVector<EventGizmo, 4> gizmos;
			Vector2i gizmoCell;
			std::int32_t hit = (GetGizmos(gizmos, gizmoCell) ? FindGizmo(gizmos, pointer.Screen) : -1);
			if (hit >= 0) {
				if (!_host.IsLayerEditable(spriteLayer)) {
					_host.ShowMessage(_("The sprite layer is locked or hidden"));
					return;
				}
				LayerView view = _host.GetLayerView(spriteLayer);
				_mode = Mode::Gizmo;
				_gizmo = gizmos[hit];
				_gizmoCell = gizmoCell;
				_gizmoGrab = _gizmo.World - view.ScreenToWorld(pointer.Screen);
				_stroke = std::make_unique<SetEventsCommand>(document, _("Change Event"));
				return;
			}
		}
		if ((pointer.Modifiers & KeyMod::Alt) != 0) {
			// The event becomes the stamp
			if (hasEvent) {
				_host.GetEventBrush() = EventStamp::FromEvent(document.GetEvent(cell.X, cell.Y));
				selection.Clear();
			}
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
			_mode = Mode::Marquee;
			if (!ctrl) {
				selection.Clear();
			}
			return;
		}

		if ((!selection.IsEmpty() || selection.HasOffGrid()) && !shift) {
			// The first click away from the selection only ends it, so nothing is placed by accident
			selection.Clear();
			return;
		}

		if (!_host.IsLayerEditable(spriteLayer)) {
			_host.ShowMessage(_("The sprite layer is locked or hidden"));
			return;
		}
		_mode = Mode::Paint;
		_overwrite = shift;
		_stroke = std::make_unique<SetEventsCommand>(document, _("Place Events"));
		PaintAt(cell);
	}

	void EventTool::OnPointerMove(const ToolPointer& pointer)
	{
		Vector2i cell = GetCell(pointer);
		switch (_mode) {
			case Mode::Paint:
				// A stamp of more events is placed only where it was clicked, it would cover itself otherwise
				if (_host.GetEventBrush().IsSingle() && cell != _current) {
					ForEachCellOnLine(_current, cell, [this](Vector2i c) { PaintAt(c); });
				}
				_current = cell;
				break;
			case Mode::Erase:
				if (cell != _current) {
					ForEachCellOnLine(_current, cell, [this](Vector2i c) { EraseAt(c); });
				}
				_current = cell;
				break;
			case Mode::Move:
			case Mode::Marquee:
				_current = cell;
				break;
			case Mode::MoveOffGrid:
				_offGridTarget = GetOffGridTarget(pointer);
				break;
			case Mode::Gizmo: {
				LevelDocument& document = _host.GetDocument();
				LayerView view = _host.GetLayerView(document.GetSpriteLayerIndex());
				Formats::EventRecord event = document.GetEvent(_gizmoCell.X, _gizmoCell.Y);
				// Shown in the level as it's dragged, recorded as one step when it's released
				if (EventGizmos::Apply(event, _gizmoCell, document.GetEventGridSize().X * 32, _gizmo, view.ScreenToWorld(pointer.Screen) + _gizmoGrab)) {
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
		if (selection.GetCount() != 1 || gridSize.X <= 0) {
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
		return Vector2f(std::max(std::round(world.X), 0.0f), std::max(std::round(world.Y), 0.0f));
	}

	std::int32_t EventTool::FindGizmo(ArrayView<const EventGizmo> gizmos, Vector2f screen)
	{
		LevelDocument& document = _host.GetDocument();
		LayerView view = _host.GetLayerView(document.GetSpriteLayerIndex());
		// The nearest within reach, the reach doesn't shrink with the view, so a handle can be grabbed zoomed out
		constexpr float Reach = 9.0f;
		std::int32_t nearest = -1;
		float nearestDistance = Reach * Reach;
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
					MoveSelection(_current - _start);
				}
				break;
			case Mode::MoveOffGrid: {
				LevelDocument& document = _host.GetDocument();
				_offGridTarget = GetOffGridTarget(pointer);
				if (_offGridIndex >= 0 && _offGridIndex < std::int32_t(document.OffGridEvents.size())) {
					SmallVector<Formats::OffGridEventRecord, 0> events;
					for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
						events.push_back(event);
					}
					Formats::OffGridEventRecord& moved = events[_offGridIndex];
					std::uint32_t x = std::uint32_t(_offGridTarget.X), y = std::uint32_t(_offGridTarget.Y);
					if (moved.X != x || moved.Y != y) {
						moved.X = x;
						moved.Y = y;
						_host.Execute(std::make_unique<SetOffGridEventsCommand>(document, std::move(events), _("Move Events")));
					}
				}
				_offGridIndex = -1;
				break;
			}
			case Mode::Marquee: {
				LevelDocument& document = _host.GetDocument();
				Vector2i gridSize = document.GetEventGridSize();
				std::int32_t x1 = std::max(std::min(_start.X, _current.X), 0), y1 = std::max(std::min(_start.Y, _current.Y), 0);
				std::int32_t x2 = std::min(std::max(_start.X, _current.X), gridSize.X - 1), y2 = std::min(std::max(_start.Y, _current.Y), gridSize.Y - 1);
				EventSelection& selection = _host.GetEventSelection();
				for (const DocumentEvent& event : document.Events) {
					std::int32_t x = std::int32_t(event.Cell % std::uint32_t(gridSize.X));
					std::int32_t y = std::int32_t(event.Cell / std::uint32_t(gridSize.X));
					if (x >= x1 && x <= x2 && y >= y1 && y <= y2) {
						selection.Add(event.Cell);
					}
				}
				break;
			}
			default:
				break;
		}
		_mode = Mode::None;
	}

	void EventTool::Cancel()
	{
		if (_stroke != nullptr) {
			// The stroke was shown in the level already
			_stroke->Seal();
			DocumentChanges changes;
			_stroke->Undo(_host.GetDocument(), changes);
			_stroke = nullptr;
		}
		_offGridIndex = -1;
		_mode = Mode::None;
	}

	bool EventTool::IsBusy() const
	{
		return (_mode != Mode::None);
	}

	void EventTool::PaintAt(Vector2i cell)
	{
		LevelDocument& document = _host.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
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

	void EventTool::EraseAt(Vector2i cell)
	{
		LevelDocument& document = _host.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		if (!IsInGrid(cell, gridSize) || LevelDocument::IsEmptyEvent(document.GetEvent(cell.X, cell.Y))) {
			return;
		}
		_stroke->Set(cell.X, cell.Y, LevelDocument::GetEmptyEvent());
		document.SetEvent(cell.X, cell.Y, LevelDocument::GetEmptyEvent());
		_host.GetEventSelection().Remove(std::uint32_t(cell.X + cell.Y * gridSize.X));
	}

	void EventTool::CommitStroke()
	{
		if (_stroke == nullptr) {
			return;
		}
		std::unique_ptr<SetEventsCommand> stroke = std::move(_stroke);
		if (!stroke->IsEmpty()) {
			// The document has the changes already, executing them again changes nothing but records them
			_host.Execute(std::move(stroke));
		}
	}

	void EventTool::MoveSelection(Vector2i offset)
	{
		LevelDocument& document = _host.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (!_host.IsLayerEditable(spriteLayer)) {
			_host.ShowMessage(_("The sprite layer is locked or hidden"));
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
			Vector2i to = from + offset;
			if (!IsInGrid(to, gridSize)) {
				_host.ShowMessage(_("Events can't be moved out of the level"));
				return;
			}
			const Formats::EventRecord& event = document.GetEvent(from.X, from.Y);
			if (!LevelDocument::IsEmptyEvent(event)) {
				moved.push_back(Moved{ from, to, event });
			}
		}
		if (moved.empty()) {
			return;
		}

		// All are taken away first, so events moved onto each other's tiles aren't lost
		auto command = std::make_unique<SetEventsCommand>(document, _("Move Events"));
		for (const Moved& item : moved) {
			command->Set(item.From.X, item.From.Y, LevelDocument::GetEmptyEvent());
		}
		SmallVector<std::uint32_t, 0> newCells;
		for (const Moved& item : moved) {
			command->Set(item.To.X, item.To.Y, item.Event);
			newCells.push_back(std::uint32_t(item.To.X + item.To.Y * gridSize.X));
		}
		_host.Execute(std::move(command));
		selection.Assign(std::move(newCells));
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

		if (_mode == Mode::None || _mode == Mode::Gizmo) {
			SmallVector<EventGizmo, 4> gizmos;
			Vector2i gizmoCell;
			if (GetGizmos(gizmos, gizmoCell)) {
				std::int32_t hovered = (_mode == Mode::None && hoverValid ? FindGizmo(gizmos, hover.Screen) : -1);
				float size = std::max(4.0f, std::round(painter.Px(4.0f)));
				for (std::size_t i = 0; i < gizmos.size(); i++) {
					const EventGizmo& gizmo = gizmos[i];
					Vector2f pos = view.WorldToScreen(gizmo.World);
					if (gizmo.Kind == EventGizmoKind::ChainLength || gizmo.Kind == EventGizmoKind::Offset) {
						// What it's measured from
						painter.Line(view.WorldToScreen(gizmo.Anchor), pos, thickness, Colorf(1.0f, 1.0f, 1.0f, 0.6f), UiLayer::Overlay);
					}
					bool active = (std::int32_t(i) == hovered || (_mode == Mode::Gizmo && gizmo.Kind == _gizmo.Kind && gizmo.Field == _gizmo.Field));
					Rectf outer(std::round(pos.X - size - thickness), std::round(pos.Y - size - thickness), (size + thickness) * 2.0f, (size + thickness) * 2.0f);
					Rectf inner(outer.X + thickness, outer.Y + thickness, outer.W - thickness * 2.0f, outer.H - thickness * 2.0f);
					painter.FillRect(outer, Colorf(0.0f, 0.0f, 0.0f, 0.75f), UiLayer::Overlay);
					painter.FillRect(inner, active ? UiTheme::Accent : Colorf::White, UiLayer::Overlay);
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
		if (_mode == Mode::MoveOffGrid) {
			if (_offGridIndex >= 0 && _offGridIndex < std::int32_t(document.OffGridEvents.size())) {
				Rectf rect = offGridRect(_offGridTarget);
				previews.DrawPlaced(painter, document.OffGridEvents[_offGridIndex].Event, rect, view.Scale, 0.7f, animTime, _offGridTarget);
				painter.Frame(rect, UiTheme::Accent, thickness, UiLayer::Overlay);
			}
			return;
		}
		std::int32_t selectedOffGrid = _host.GetEventSelection().GetOffGrid();
		if (selectedOffGrid >= 0 && selectedOffGrid < std::int32_t(document.OffGridEvents.size())) {
			const Formats::OffGridEventRecord& record = document.OffGridEvents[selectedOffGrid];
			painter.Frame(offGridRect(Vector2f(float(record.X), float(record.Y))), UiTheme::Accent, thickness, UiLayer::Overlay);
		}
		if (_mode == Mode::None && hoverValid) {
			std::int32_t offGrid = FindOffGrid(hover.Screen);
			if (offGrid >= 0) {
				if (offGrid != selectedOffGrid) {
					const Formats::OffGridEventRecord& record = document.OffGridEvents[offGrid];
					painter.Frame(offGridRect(Vector2f(float(record.X), float(record.Y))), UiTheme::Cursor, thickness, UiLayer::Overlay);
				}
				return;
			}
		}

		if (_mode == Mode::Move) {
			// The selected events where they will be
			Vector2i offset = _current - _start;
			for (std::uint32_t cell : _host.GetEventSelection().GetCells()) {
				Vector2i to(std::int32_t(cell % std::uint32_t(gridSize.X)) + offset.X, std::int32_t(cell / std::uint32_t(gridSize.X)) + offset.Y);
				Rectf rect = view.CellToScreen(to);
				const Formats::EventRecord& event = document.GetEvent(to.X - offset.X, to.Y - offset.Y);
				previews.DrawPlaced(painter, event, rect, view.Scale, 0.7f, animTime, Vector2f(to.X * 32.0f + 16.0f, to.Y * 32.0f + 16.0f));
				painter.Frame(rect, IsInGrid(to, gridSize) ? UiTheme::Accent : UiTheme::Danger, thickness, UiLayer::Overlay);
			}
			return;
		}

		if (_mode == Mode::Marquee) {
			Vector2i from(std::min(_start.X, _current.X), std::min(_start.Y, _current.Y));
			Vector2i to(std::max(_start.X, _current.X), std::max(_start.Y, _current.Y));
			DrawCellsOutline(painter, view, from, to, UiTheme::Accent);
			return;
		}

		if (!hoverValid) {
			return;
		}
		Vector2i cell = GetCell(hover);
		bool hasEvent = (IsInGrid(cell, gridSize) && !LevelDocument::IsEmptyEvent(document.GetEvent(cell.X, cell.Y)));
		bool shift = (hover.Modifiers & KeyMod::Shift) != 0;
		const EventStamp& brush = _host.GetEventBrush();
		if (_mode == Mode::Erase || (hasEvent && !shift && _mode == Mode::None) || brush.IsEmpty() || (_mode == Mode::None && (!_host.GetEventSelection().IsEmpty() || _host.GetEventSelection().HasOffGrid()) && !shift)) {
			painter.Frame(view.CellToScreen(cell), _mode == Mode::Erase ? UiTheme::Danger : UiTheme::Cursor, thickness, UiLayer::Overlay);
			return;
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
		return _("Click: place or select - Drag: move - Right: erase - Alt: pick - Ctrl: select more - Ctrl+Shift: place off the grid");
	}
}

#endif
