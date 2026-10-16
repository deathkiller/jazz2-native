#include "EventPanels.h"

#if defined(WITH_EDITOR)

#include "ShellPanels.h"
#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// Size of an item of the event grid in units
		constexpr float ChipWidth = 104.0f;
		constexpr float ChipHeight = 24.0f;

		String FormatNumber(std::int32_t value)
		{
			char buffer[16];
			std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%d", value);
			return String(buffer, std::size_t(std::max(length, 0)));
		}

		UiStack* AddRow(UiElement* parent, StringView label, StringView tooltip = {})
		{
			// A form puts the labels in a column as wide as the longest of them
			if (auto* form = runtime_cast<UiForm>(parent)) {
				return form->AddRow(label, tooltip);
			}
			auto* row = parent->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			auto* labelElement = row->Add<UiLabel>(label, UiTheme::TextDim);
			labelElement->Width = 62.0f;
			labelElement->Tooltip = tooltip;
			return row;
		}

		StringView GetUnitSuffix(ParamUnit unit)
		{
			switch (unit) {
				case ParamUnit::Pixels: return _("px");
				case ParamUnit::Tiles: return _("tiles");
				case ParamUnit::Frames: return _("frames");
				case ParamUnit::Seconds: return _("s");
				case ParamUnit::Percent: return "%"_s;
				case ParamUnit::Degrees: return _("deg");
				default: return {};
			}
		}

		bool ContainsIgnoreCase(StringView text, StringView search)
		{
			if (search.empty()) {
				return true;
			}
			String lowerText = StringUtils::lowercase(text);
			String lowerSearch = StringUtils::lowercase(search);
			return !lowerText.find(lowerSearch).empty();
		}

		/** The event shown by the inspector, with its name and what is edited */
		class EventHeader : public UiElement
		{
		public:
			EventHeader(EditorSession& session, EventInspectorPanel& panel)
				: _session(session), _panel(panel)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(available.X, context.Px(28.0f));
			}

			void Draw(UiPainter& painter) override
			{
				Formats::EventRecord event;
				if (!_panel.GetPrimary(event)) {
					return;
				}
				float size = Bounds.H;
				painter.RoundRect(Rectf(Bounds.X, Bounds.Y, size, size), UiTheme::InputFill);
				_session.GetEventPreviews().DrawFitted(painter, event, Rectf(Bounds.X + painter.Px(2.0f), Bounds.Y + painter.Px(2.0f),
					size - painter.Px(4.0f), size - painter.Px(4.0f)), _session.GetAnimTime());

				float x = Bounds.X + size + painter.Px(UiTheme::Padding);
				float lineHeight = std::round(Bounds.H * 0.5f);
				Rectf text(x, Bounds.Y, Bounds.X + Bounds.W - x, lineHeight);
				painter.PushClip(Rectf(x, Bounds.Y, text.W, Bounds.H));
				painter.TextInRect(EventPreviews::GetEventName(event.Type), text, UiTheme::TextHeader);

				std::int32_t count = _session.GetEventSelection().GetCount();
				String subtitle;
				if (_session.GetEventSelection().HasOffGrid()) {
					subtitle = _("Off the grid");
				} else if (count == 0) {
					subtitle = _("To place");
				} else if (count == 1) {
					const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
					subtitle = (descriptor != nullptr ? String(EventCatalog::GetCategoryName(descriptor->Category)) : String(_("Unknown event")));
				} else {
					subtitle = _f("{} selected", count);
				}
				painter.TextInRect(subtitle, Rectf(x, Bounds.Y + lineHeight, text.W, Bounds.H - lineHeight), UiTheme::TextDim);
				painter.PopClip();
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				Formats::EventRecord event;
				if (!_panel.GetPrimary(event)) {
					return {};
				}
				const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
				if (descriptor == nullptr || descriptor->Description == nullptr) {
					return {};
				}
				_tooltip = _(descriptor->Description);
				return _tooltip;
			}

		private:
			EditorSession& _session;
			EventInspectorPanel& _panel;
			mutable String _tooltip;
		};
	}

	EventGrid::EventGrid(EditorSession& session)
		: _session(session), _category(AllCategories), _catalogRevision(0), _chipWidth(1.0f), _chipHeight(1.0f), _columns(1), _pressedIndex(-1),
			_measuredScale(0.0f), _measuredNameWidth(-1.0f)
	{
		UpdateItems();
	}

	void EventGrid::SetFilter(std::int32_t category, StringView search)
	{
		if (_category == category && _search == search) {
			return;
		}
		_category = category;
		_search = search;
		UpdateItems();
		_measuredNameWidth = -1.0f;
	}

	void EventGrid::Update(float timeMult)
	{
		// Events described from outside of the game came or went
		if (_catalogRevision != EventCatalog::GetRevision()) {
			UpdateItems();
			_measuredNameWidth = -1.0f;
		}
		UiElement::Update(timeMult);
	}

	void EventGrid::UpdateItems()
	{
		_catalogRevision = EventCatalog::GetRevision();
		_items.clear();
		for (const EventDescriptor* descriptor : EventCatalog::GetAll()) {
			if (_category != AllCategories && std::int32_t(descriptor->Category) != _category) {
				continue;
			}
			// Events the game doesn't use are found only by searching
			if ((descriptor->Traits & EventTraits::Unused) == EventTraits::Unused && _search.empty()) {
				continue;
			}
			if (!ContainsIgnoreCase(_(descriptor->Name), _search) && !ContainsIgnoreCase(descriptor->Key, _search)) {
				continue;
			}
			_items.push_back(descriptor);
		}
		// By category, then as the catalog has them
		std::stable_sort(_items.begin(), _items.end(), [](const EventDescriptor* a, const EventDescriptor* b) {
			return (a->Category < b->Category);
		});
	}

	Vector2f EventGrid::Measure(const UiContext& context, Vector2f available)
	{
		// As wide as the longest name needs, measured again only when the names or the scale change
		if (_measuredScale != context.Scale || _measuredNameWidth < 0.0f) {
			_measuredScale = context.Scale;
			_measuredNameWidth = 0.0f;
			for (const EventDescriptor* descriptor : _items) {
				_measuredNameWidth = std::max(_measuredNameWidth, context.MeasureText(_(descriptor->Name)));
			}
		}
		float padding = context.Px(2.0f);
		float chipHeight = context.Px(ChipHeight);
		float minWidth = std::max(context.Px(ChipWidth * 0.5f), std::ceil(chipHeight + padding * 5.0f + _measuredNameWidth));
		_columns = std::max(1, std::int32_t(available.X / minWidth));
		_chipWidth = std::floor(available.X / float(_columns));
		_chipHeight = context.Px(ChipHeight);
		std::int32_t rows = (std::int32_t(_items.size()) + _columns - 1) / _columns;
		return Vector2f(available.X, rows * _chipHeight);
	}

	Rectf EventGrid::GetChipRect(std::int32_t index) const
	{
		return Rectf(Bounds.X + (index % _columns) * _chipWidth, Bounds.Y + (index / _columns) * _chipHeight, _chipWidth, _chipHeight);
	}

	std::int32_t EventGrid::GetIndexAt(Vector2f pos) const
	{
		if (!Bounds.Contains(pos)) {
			return -1;
		}
		std::int32_t column = std::int32_t((pos.X - Bounds.X) / _chipWidth);
		std::int32_t row = std::int32_t((pos.Y - Bounds.Y) / _chipHeight);
		std::int32_t index = row * _columns + column;
		return (column >= 0 && column < _columns && index >= 0 && index < std::int32_t(_items.size()) ? index : -1);
	}

	void EventGrid::Draw(UiPainter& painter)
	{
		if (_items.empty()) {
			painter.TextInRect(_("No event matches"), Rectf(Bounds.X + painter.Px(UiTheme::Padding), Bounds.Y, Bounds.W, painter.Px(ChipHeight)), UiTheme::TextDisabled);
			return;
		}

		const Rectf& clip = painter.GetClip();
		float clipTop = (clip.H > 0.0f ? clip.Y : Bounds.Y);
		float clipBottom = (clip.H > 0.0f ? clip.Y + clip.H : Bounds.Y + Bounds.H);
		std::int32_t firstRow = std::max(0, std::int32_t((clipTop - Bounds.Y) / _chipHeight));
		std::int32_t lastRow = std::int32_t((clipBottom - Bounds.Y) / _chipHeight);

		const EventStamp& brush = _session.GetEventBrush();
		std::int32_t brushType = (brush.IsSingle() && _session.GetEventSelection().IsEmpty() ? std::int32_t(brush.Items[0].Event.Type) : -1);
		std::int32_t hovered = (_hovered && GetSurface() != nullptr ? GetIndexAt(GetSurface()->GetPointerPos()) : -1);
		EventPreviews& previews = _session.GetEventPreviews();
		float animTime = _session.GetAnimTime();
		float inset = std::max(1.0f, painter.Px(1.0f));
		float padding = painter.Px(2.0f);

		for (std::int32_t row = firstRow; row <= lastRow; row++) {
			for (std::int32_t column = 0; column < _columns; column++) {
				std::int32_t index = row * _columns + column;
				if (index >= std::int32_t(_items.size())) {
					break;
				}
				const EventDescriptor* descriptor = _items[index];
				Rectf rect = GetChipRect(index);
				rect = Rectf(rect.X + inset, rect.Y + inset, rect.W - inset * 2.0f, rect.H - inset * 2.0f);

				bool selected = (std::int32_t(descriptor->Type) == brushType);
				if (selected) {
					painter.RoundRect(rect, UiTheme::Selection);
					painter.RoundRectBorder(rect, UiTheme::Accent);
				} else if (index == hovered) {
					painter.RoundRect(rect, UiTheme::Hover);
				}

				// A stripe of the color the event has in the level
				Colorf category = EventPreviews::GetCategoryColor(descriptor->Category);
				painter.FillRect(Rectf(rect.X, rect.Y + padding, std::max(1.0f, painter.Px(1.0f)), rect.H - padding * 2.0f), category, UiLayer::Content);

				float iconSize = rect.H - padding * 2.0f;
				Rectf icon(rect.X + padding * 2.0f, rect.Y + padding, iconSize, iconSize);
				bool unused = (descriptor->Traits & EventTraits::Unused) == EventTraits::Unused;
				previews.DrawFitted(painter, std::uint16_t(descriptor->Type), icon, (index == hovered ? animTime : 0.0f), unused ? 0.4f : 1.0f);

				float textX = icon.X + icon.W + padding * 2.0f;
				Rectf text(textX, rect.Y, rect.X + rect.W - textX - padding, rect.H);
				painter.PushClip(text);
				painter.TextInRect(_(descriptor->Name), text, unused ? UiTheme::TextDisabled : UiTheme::Text);
				painter.PopClip();
			}
		}
	}

	bool EventGrid::AcceptsPointer() const
	{
		return true;
	}

	bool EventGrid::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		_pressedIndex = GetIndexAt(event.Pos);
		return (_pressedIndex >= 0);
	}

	void EventGrid::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		std::int32_t index = GetIndexAt(event.Pos);
		if (inside && index >= 0 && index == _pressedIndex) {
			_session.OnEventChosen(_items[index]->Type);
		}
		_pressedIndex = -1;
	}

	StringView EventGrid::GetTooltip(Vector2f pos) const
	{
		std::int32_t index = GetIndexAt(pos);
		if (index < 0) {
			return {};
		}
		const EventDescriptor* descriptor = _items[index];
		_tooltip = _(descriptor->Name);
		if (descriptor->Description != nullptr) {
			_tooltip = _tooltip + "\n"_s + _(descriptor->Description);
		}
		return _tooltip;
	}

	EventPickerPanel::EventPickerPanel(EditorSession& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session), _grid(nullptr)
	{
		EditorSession* s = &session;
		auto gridScroll = std::make_unique<UiScrollView>();
		_grid = gridScroll->SetContent<EventGrid>(session);
		EventGrid* grid = _grid;

		// A narrow panel has no room for a column of categories, so they're in a list next to the search
		auto* header = Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		auto categoryName = [](std::int32_t category) {
			return String(category == EventGrid::AllCategories ? _("All") : EventCatalog::GetCategoryName(EventCategory(category)));
		};
		auto* categories = header->Add<UiDropdown>([grid, categoryName]() {
			return categoryName(grid->GetCategory());
		}, [grid, categoryName](SmallVectorImpl<UiMenuEntry>& entries) {
			for (std::int32_t category = EventGrid::AllCategories; category < std::int32_t(EventCategory::Count); category++) {
				UiMenuEntry entry;
				entry.Label = categoryName(category);
				entry.IsChecked = [grid, category]() { return (grid->GetCategory() == category); };
				entry.Execute = [grid, category]() { grid->SetFilter(category, grid->GetSearch()); };
				entries.push_back(std::move(entry));
			}
		});
		categories->Flex = 1.0f;
		categories->Tooltip = _("Category of the events shown");
		auto* search = header->Add<UiTextBox>();
		search->Placeholder = _("Search");
		search->Flex = 1.0f;
		search->OnChanged = [grid](StringView text) {
			grid->SetFilter(grid->GetCategory(), text);
		};

		auto* hint = Add<UiLabel>(_("Choose the event for the parameter"), UiTheme::TextAccent);
		hint->IsVisibleFunc = [s]() { return s->IsPickingEvent(); };

		UiScrollView* scroll = static_cast<UiScrollView*>(AddChild(std::move(gridScroll)));
		scroll->Flex = 1.0f;
	}

	EventInspectorPanel::EventInspectorPanel(EditorSession& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session), _scroll(nullptr),
			_shownType(-1), _shownCount(-1), _shownBrush(false), _shownRevision(0)
	{
		// The edge above, between it and the list of layers, is drawn by the splitter that resizes them
		Add<UiLabel>(_("Event Properties"), UiTheme::TextHeader);
		_scroll = Add<UiScrollView>();
		_scroll->Flex = 1.0f;
		Rebuild();
	}

	void EventInspectorPanel::Update(float timeMult)
	{
		Formats::EventRecord event;
		bool hasEvent = GetPrimary(event);
		std::int32_t type = (hasEvent ? std::int32_t(event.Type) : -1);
		std::int32_t count = _session.GetEventSelection().GetCount();
		bool brush = (count == 0 && !_session.GetEventSelection().HasOffGrid());
		if (type != _shownType || std::min(count, 2) != std::min(_shownCount, 2) || brush != _shownBrush || _shownRevision != EventCatalog::GetRevision()) {
			Rebuild();
		}
		UiPanel::Update(timeMult);
	}

	bool EventInspectorPanel::GetPrimary(Formats::EventRecord& event) const
	{
		const EventSelection& selection = _session.GetEventSelection();
		if (selection.HasOffGrid()) {
			const LevelDocument& document = _session.GetDocument();
			if (selection.GetOffGrid() >= std::int32_t(document.OffGridEvents.size())) {
				return false;
			}
			event = document.OffGridEvents[selection.GetOffGrid()].Event;
			return true;
		}
		if (!selection.IsEmpty()) {
			LevelDocument& document = _session.GetDocument();
			Vector2i gridSize = document.GetEventGridSize();
			if (gridSize.X <= 0) {
				return false;
			}
			std::uint32_t cell = selection.GetCells()[0];
			event = document.GetEvent(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
			return !LevelDocument::IsEmptyEvent(event);
		}
		const EventStamp& brush = _session.GetEventBrush();
		if (brush.IsEmpty()) {
			return false;
		}
		event = brush.Items[0].Event;
		return true;
	}

	void EventInspectorPanel::Change(const char* name, Function<void(Formats::EventRecord&)>&& change, bool sameTypeOnly)
	{
		Formats::EventRecord primary;
		if (!GetPrimary(primary)) {
			return;
		}

		EventSelection& selection = _session.GetEventSelection();
		if (selection.HasOffGrid()) {
			LevelDocument& document = _session.GetDocument();
			if (!_session.IsLayerEditable(document.GetSpriteLayerIndex())) {
				_session.ShowMessage(_("The sprite layer is locked or hidden"));
				return;
			}
			SmallVector<Formats::OffGridEventRecord, 0> events;
			for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
				events.push_back(event);
			}
			Formats::EventRecord& event = events[selection.GetOffGrid()].Event;
			Formats::EventRecord before = event;
			change(event);
			LevelDocument::NormalizeEvent(event);
			if (!LevelDocument::AreEventsEqual(before, event)) {
				_session.Execute(std::make_unique<SetOffGridEventsCommand>(document, std::move(events), _(name)), true);
			}
			return;
		}
		if (selection.IsEmpty()) {
			for (EventStamp::Item& item : _session.GetEventBrush().Items) {
				if (!sameTypeOnly || item.Event.Type == primary.Type) {
					change(item.Event);
				}
			}
			return;
		}

		LevelDocument& document = _session.GetDocument();
		if (!_session.IsLayerEditable(document.GetSpriteLayerIndex())) {
			_session.ShowMessage(_("The sprite layer is locked or hidden"));
			return;
		}
		Vector2i gridSize = document.GetEventGridSize();
		auto command = std::make_unique<SetEventsCommand>(document, _(name));
		for (std::uint32_t cell : selection.GetCells()) {
			std::int32_t x = std::int32_t(cell % std::uint32_t(gridSize.X)), y = std::int32_t(cell / std::uint32_t(gridSize.X));
			Formats::EventRecord event = document.GetEvent(x, y);
			if (LevelDocument::IsEmptyEvent(event) || (sameTypeOnly && event.Type != primary.Type)) {
				continue;
			}
			change(event);
			command->Set(x, y, event);
		}
		command->Seal();
		if (!command->IsEmpty()) {
			// Changes of the same property follow each other while a value is dragged, they're undone at once
			_session.Execute(std::move(command), true);
		}
	}

	void EventInspectorPanel::Rebuild()
	{
		Formats::EventRecord event;
		bool hasEvent = GetPrimary(event);
		_shownType = (hasEvent ? std::int32_t(event.Type) : -1);
		_shownCount = _session.GetEventSelection().GetCount();
		_shownBrush = (_shownCount == 0 && !_session.GetEventSelection().HasOffGrid());
		_shownRevision = EventCatalog::GetRevision();

		auto* rows = _scroll->SetContent<UiForm>(2.0f, UiThickness(0.0f, 0.0f, 2.0f, 0.0f));
		if (!hasEvent) {
			// In short lines, labels don't wrap
			rows->Add<UiLabel>(_("Pick an event below"), UiTheme::TextDim);
			rows->Add<UiLabel>(_("or in the level"), UiTheme::TextDim);
			return;
		}

		rows->Add<EventHeader>(_session, *this);
		if (const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type))) {
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				AddField(rows, descriptor->Fields[i]);
			}
			if (descriptor->FieldCount == 0) {
				rows->Add<UiLabel>(_("No parameters"), UiTheme::TextDisabled);
			}
		} else {
			rows->Add<UiLabel>(_("Unknown event, only its raw parameters can be changed"), UiTheme::TextDim);
		}
		rows->Add<UiSeparator>();
		AddCommonRows(rows);
	}

	void EventInspectorPanel::AddCommonRows(UiElement* rows)
	{
		EventInspectorPanel* self = this;
		auto readFlags = [self]() -> std::uint8_t {
			Formats::EventRecord event;
			return (self->GetPrimary(event) ? event.Flags : 0);
		};

		// Difficulties, an event of none of them is in all of them
		{
			auto* row = AddRow(rows, _("Difficulty"), _("In which difficulties the event is in the level"));
			struct Difficulty { const char* Label; std::uint8_t Bit; };
			static const Difficulty Difficulties[] = {
				{ N_("Easy"), Formats::EventRecord::Easy },
				{ N_("Normal"), Formats::EventRecord::Normal },
				{ N_("Hard"), Formats::EventRecord::Hard },
			};
			auto* checks = row->Add<UiStack>(UiOrientation::Vertical, 1.0f);
			checks->Flex = 1.0f;
			for (const Difficulty& difficulty : Difficulties) {
				std::uint8_t bit = difficulty.Bit;
				checks->Add<UiCheckBox>(_(difficulty.Label), [readFlags, bit]() {
					std::uint8_t flags = readFlags();
					return ((flags & Formats::EventRecord::DifficultyMask) == 0 || (flags & bit) != 0);
				}, [self, bit](bool value) {
					self->Change(N_("Difficulty"), [bit, value](Formats::EventRecord& event) {
						std::uint8_t difficulties = event.Flags & Formats::EventRecord::DifficultyMask;
						if (difficulties == 0) {
							difficulties = Formats::EventRecord::DifficultyMask;
						}
						difficulties = (value ? (difficulties | bit) : (difficulties & ~bit));
						// At least one stays, none would mean all of them
						if (difficulties != 0) {
							event.Flags = std::uint8_t((event.Flags & ~Formats::EventRecord::DifficultyMask) | difficulties);
						}
					}, false);
				});
			}
		}

		struct FlagRow { const char* Label; const char* Description; std::uint8_t Bit; };
		static const FlagRow FlagRows[] = {
			{ N_("Multiplayer only"), N_("The event is in the level only in multiplayer"), Formats::EventRecord::MultiplayerOnly },
			{ N_("Illuminated"), N_("The object glows in the dark"), Formats::EventRecord::Illuminated },
			{ N_("Generator"), N_("The object appears again a while after it's gone"), Formats::EventRecord::Generator },
		};
		for (const FlagRow& flagRow : FlagRows) {
			std::uint8_t bit = flagRow.Bit;
			const char* label = flagRow.Label;
			auto* check = rows->Add<UiCheckBox>(_(label), [readFlags, bit]() {
				return ((readFlags() & bit) != 0);
			}, [self, bit, label](bool value) {
				self->Change(label, [bit, value](Formats::EventRecord& event) {
					event.Flags = std::uint8_t(value ? (event.Flags | bit) : (event.Flags & ~bit));
				}, false);
			});
			check->Tooltip = _(flagRow.Description);
		}

		auto isGenerator = [readFlags]() {
			return ((readFlags() & Formats::EventRecord::Generator) != 0);
		};
		{
			auto* row = AddRow(rows, String(_("Delay") + " ("_s + _("s") + ")"_s), _("Seconds before the object appears again"));
			auto* number = row->Add<UiNumberBox>([self]() {
				Formats::EventRecord event;
				return (self->GetPrimary(event) ? float(event.GeneratorDelay) : 0.0f);
			}, [self](float value, bool final) {
				self->Change(N_("Generator delay"), [value](Formats::EventRecord& event) {
					event.GeneratorDelay = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 255));
				}, false);
			}, 0.0f, 255.0f, 1.0f, 0);
			number->Flex = 1.0f;
			// The whole row, with its label
			(row->GetParent() != rows ? row->GetParent() : row)->IsVisibleFunc = isGenerator;
		}
		{
			auto* check = rows->Add<UiCheckBox>(_("Delay the first one too"), [self]() {
				Formats::EventRecord event;
				return (self->GetPrimary(event) && (event.GeneratorFlags & 0x01) != 0);
			}, [self](bool value) {
				self->Change(N_("Generator delay"), [value](Formats::EventRecord& event) {
					event.GeneratorFlags = std::uint8_t(value ? (event.GeneratorFlags | 0x01) : (event.GeneratorFlags & ~0x01));
				}, false);
			});
			check->IsVisibleFunc = isGenerator;
		}

		// All parameters as they are stored, for anything the fields above don't cover
		{
			rows->Add<UiLabel>(_("Raw parameters"), UiTheme::TextDim);
			auto* raw = rows->Add<UiTextBox>();
			raw->MaxLength = Formats::LevelFile::EventParamsSize * 3;
			raw->TextFunc = [self]() {
				Formats::EventRecord event;
				if (!self->GetPrimary(event)) {
					return String();
				}
				char buffer[Formats::LevelFile::EventParamsSize * 3 + 1];
				std::int32_t length = 0;
				for (std::int32_t i = 0; i < Formats::LevelFile::EventParamsSize; i++) {
					length += std::snprintf(buffer + length, sizeof(buffer) - length, i > 0 ? " %02X" : "%02X", event.Params[i]);
				}
				return String(buffer, std::size_t(length));
			};
			raw->OnCommit = [self](StringView text) {
				std::uint8_t params[Formats::LevelFile::EventParamsSize] = {};
				String copy = text;
				const char* p = copy.data();
				for (std::int32_t i = 0; i < Formats::LevelFile::EventParamsSize && *p != '\0'; i++) {
					while (*p == ' ' || *p == ',') {
						p++;
					}
					char* end;
					long value = std::strtol(p, &end, 16);
					if (end == p) {
						break;
					}
					params[i] = std::uint8_t(value);
					p = end;
				}
				self->Change(N_("Raw parameters"), [params](Formats::EventRecord& event) {
					std::memcpy(event.Params, params, sizeof(params));
				}, true);
			};
		}
	}

	void EventInspectorPanel::AddField(UiElement* rows, const EventParamField& field)
	{
		EventInspectorPanel* self = this;
		const EventParamField* f = &field;
		String label = _(field.Name);
		StringView unit = GetUnitSuffix(field.Unit);
		String tooltip = (field.Description != nullptr ? String(_(field.Description)) : String());
		auto read = [self, f]() -> std::int32_t {
			Formats::EventRecord event;
			return (self->GetPrimary(event) ? EventCatalog::ReadField(event, *f) : 0);
		};

		switch (field.Type) {
			case ParamType::Bool: {
				auto* check = rows->Add<UiCheckBox>(label, [read]() {
					return (read() != 0);
				}, [self, f](bool value) {
					self->Change(f->Name, [f, value](Formats::EventRecord& event) {
						EventCatalog::WriteField(event, *f, value ? 1 : 0);
					}, true);
				});
				check->Tooltip = tooltip;
				break;
			}
			case ParamType::Enum: {
				auto* row = AddRow(rows, label, tooltip);
				auto* dropdown = row->Add<UiDropdown>([read, f]() {
					std::int32_t value = read();
					for (std::uint8_t i = 0; i < f->OptionCount; i++) {
						if (f->Options[i].Value == value) {
							return String(_(f->Options[i].Label));
						}
					}
					return FormatNumber(value);
				}, [self, read, f](SmallVectorImpl<UiMenuEntry>& entries) {
					std::int32_t current = read();
					for (std::uint8_t i = 0; i < f->OptionCount; i++) {
						std::int32_t value = f->Options[i].Value;
						UiMenuEntry entry;
						entry.Label = _(f->Options[i].Label);
						entry.IsChecked = [current, value]() { return (current == value); };
						entry.Execute = [self, f, value]() {
							self->Change(f->Name, [f, value](Formats::EventRecord& event) {
								EventCatalog::WriteField(event, *f, value);
							}, true);
						};
						entries.push_back(std::move(entry));
					}
				});
				dropdown->Flex = 1.0f;
				dropdown->Tooltip = tooltip;
				break;
			}
			case ParamType::Flags: {
				auto* title = rows->Add<UiLabel>(label, UiTheme::TextDim);
				title->Tooltip = tooltip;
				auto* checks = rows->Add<UiStack>(UiOrientation::Vertical, 1.0f, UiThickness(8.0f, 0.0f, 0.0f, 0.0f));
				for (std::uint8_t i = 0; i < field.OptionCount; i++) {
					std::int32_t mask = field.Options[i].Value;
					checks->Add<UiCheckBox>(_(field.Options[i].Label), [read, mask]() {
						return ((read() & mask) == mask);
					}, [self, f, mask](bool value) {
						self->Change(f->Name, [f, mask, value](Formats::EventRecord& event) {
							std::int32_t flags = EventCatalog::ReadField(event, *f);
							EventCatalog::WriteField(event, *f, value ? (flags | mask) : (flags & ~mask));
						}, true);
					});
				}
				break;
			}
			case ParamType::EventType: {
				auto* row = AddRow(rows, label, tooltip);
				EditorSession* session = &_session;
				auto* dropdown = row->Add<UiDropdown>([read]() {
					std::int32_t value = read();
					return (value == 0 ? String(_("Nothing")) : EventPreviews::GetEventName(std::uint16_t(value)));
				}, [self, f, session](SmallVectorImpl<UiMenuEntry>& entries) {
					UiMenuEntry choose;
					choose.Label = _("Choose in the list below...");
					choose.Icon = EditorIcon::Event;
					choose.Execute = [self, f, session]() {
						session->PickEventFromList([self, f](EventType type) {
							self->Change(f->Name, [f, type](Formats::EventRecord& event) {
								EventCatalog::WriteField(event, *f, std::int32_t(type));
							}, true);
						});
					};
					entries.push_back(std::move(choose));
					UiMenuEntry none;
					none.Label = _("Nothing");
					none.Execute = [self, f]() {
						self->Change(f->Name, [f](Formats::EventRecord& event) {
							EventCatalog::WriteField(event, *f, 0);
						}, true);
					};
					entries.push_back(std::move(none));
				});
				dropdown->Flex = 1.0f;
				dropdown->Tooltip = tooltip;
				break;
			}
			case ParamType::String: {
				auto* row = AddRow(rows, label, tooltip);
				auto* text = row->Add<UiTextBox>();
				text->Flex = 1.0f;
				text->MaxLength = field.Bits / 8;
				text->TextFunc = [self, f]() {
					Formats::EventRecord event;
					char buffer[Formats::LevelFile::EventParamsSize + 1];
					return (self->GetPrimary(event) ? String(EventCatalog::ReadString(event, *f, buffer, sizeof(buffer))) : String());
				};
				text->OnCommit = [self, f](StringView value) {
					String copy = value;
					self->Change(f->Name, [f, copy](Formats::EventRecord& event) {
						std::size_t length = f->Bits / 8;
						for (std::size_t i = 0; i < length && f->Offset + i < std::size_t(Formats::LevelFile::EventParamsSize); i++) {
							event.Params[f->Offset + i] = (i < copy.size() ? std::uint8_t(copy[i]) : 0);
						}
					}, true);
				};
				break;
			}
			case ParamType::Raw:
				// Covered by the raw parameters
				break;
			default: {
				String text = (unit.empty() ? label : String(label + " ("_s + unit + ")"_s));
				auto* row = AddRow(rows, text, tooltip);
				auto* number = row->Add<UiNumberBox>([read]() {
					return float(read());
				}, [self, f](float value, bool final) {
					std::int32_t rounded = std::int32_t(std::round(value));
					self->Change(f->Name, [f, rounded](Formats::EventRecord& event) {
						EventCatalog::WriteField(event, *f, rounded);
					}, true);
				}, float(field.Min), float(field.Max), 1.0f, 0);
				number->Flex = 1.0f;
				number->Tooltip = tooltip;

				if (field.Type == ParamType::TextId) {
					// The text itself, as far as it fits
					EditorSession* session = &_session;
					auto* preview = rows->Add<UiLabel>(StringView(), UiTheme::TextDim);
					preview->TextFunc = [session, read]() {
						std::int32_t index = read();
						const auto& texts = session->GetDocument().Texts;
						return (index >= 0 && index < std::int32_t(texts.size()) && !texts[index].empty()
							? String("\""_s + EventCatalog::MakeTextPreview(texts[index], 64) + "\""_s) : String(_("No such text")));
					};
				}
				break;
			}
		}
	}
}

#endif
