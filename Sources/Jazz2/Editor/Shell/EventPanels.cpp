#include "EventPanels.h"

#if defined(WITH_EDITOR)

#include "PropertyRows.h"
#include "ShellCommon.h"
#include "ShellPanels.h"
#include "../Tools/EditorContext.h"
#include "../Common/EventStamp.h"
#include "../Document/CommandHistory.h"
#include "../Document/LevelDocument.h"
#include "../Events/EventPreviews.h"
#include "../Files/EditorSettings.h"
#include "../Tools/EventTools.h"
#include "../Document/Commands.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Cryptography/xxHash.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Death::Containers::Literals;
using Death::Cryptography::xxHash3;

namespace Jazz2::Editor
{
	namespace
	{
		// Size of an item of the event grid in units
		constexpr float ChipWidth = 104.0f;
		constexpr float ChipHeight = 24.0f;

		UiStack* AddRow(UiElement* parent, StringView label, StringView tooltip = {})
		{
			return AddLabeledRow(parent, label, tooltip, 62.0f);
		}

		bool IsFavoriteEvent(const EditorSettings& settings, std::uint16_t type)
		{
			return std::find(settings.FavoriteEvents.begin(), settings.FavoriteEvents.end(), type) != settings.FavoriteEvents.end();
		}

		void ToggleFavoriteEvent(IEditorContext& session, std::uint16_t type)
		{
			EditorSettings& settings = session.GetSettings();
			auto it = std::find(settings.FavoriteEvents.begin(), settings.FavoriteEvents.end(), type);
			if (it != settings.FavoriteEvents.end()) {
				settings.FavoriteEvents.erase(it);
				// TRANSLATORS: Status message, {} is the name of an event
				session.ShowMessage(_fx("editor", "{} isn't a favourite anymore", EventPreviews::GetEventName(type)));
			} else if (std::int32_t(settings.FavoriteEvents.size()) < EditorSettings::MaxFavoriteItems) {
				settings.FavoriteEvents.push_back(type);
				// TRANSLATORS: Status message, {} is the name of an event
				session.ShowMessage(_fx("editor", "{} is a favourite now", EventPreviews::GetEventName(type)));
			}
			settings.Save();
		}

		// An event chosen to place is remembered as a recent one, not one chosen for a parameter
		void ChooseEvent(IEditorContext& session, std::uint16_t type)
		{
			if (!session.IsPickingEvent()) {
				EditorSettings& settings = session.GetSettings();
				settings.AddRecentEvent(type);
				settings.Save();
			}
			session.OnEventChosen(EventType(type));
		}

		/** Favourite events and those placed recently, a click chooses one, a right click makes it a favourite or not */
		class EventShortcutStrip : public UiElement
		{
		public:
			explicit EventShortcutStrip(IEditorContext& session)
				: _session(session), _pressed(-1)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				// Large enough to tell the events apart at a glance, they're square, as high as the strip
				return Vector2f(available.X, context.Px((UiTheme::ButtonHeight + 2.0f) * 2.0f));
			}

			void Update(float timeMult) override
			{
				UiElement::Update(timeMult);
				// Favourites first, then the recent ones that aren't
				const EditorSettings& settings = _session.GetSettings();
				_items.clear();
				for (std::uint16_t type : settings.FavoriteEvents) {
					if (EventCatalog::Find(EventType(type)) != nullptr) {
						_items.push_back(type);
					}
				}
				_favoriteCount = std::int32_t(_items.size());
				for (std::uint16_t type : settings.RecentEvents) {
					if (EventCatalog::Find(EventType(type)) != nullptr && std::find(_items.begin(), _items.end(), type) == _items.end()) {
						_items.push_back(type);
					}
				}
			}

			void Draw(UiPainter& painter) override
			{
				if (_items.empty()) {
					painter.TextInRect(_x("editor", "Right-click an event to keep it here"), Bounds, UiTheme::TextDisabled);
					return;
				}
				float size = Bounds.H;
				std::int32_t hovered = (_hovered && GetSurface() != nullptr ? GetIndexAt(GetSurface()->GetPointerPos()) : -1);
				for (std::int32_t i = 0; i < std::int32_t(_items.size()); i++) {
					Rectf rect = GetItemRect(i, size);
					if (rect.X + rect.W > Bounds.X + Bounds.W) {
						break;
					}
					painter.RoundRect(rect, i == hovered ? UiTheme::Hover : UiTheme::InputFill);
					float inset = painter.Px(2.0f);
					_session.GetEventPreviews().DrawFitted(painter, _items[i], Rectf(rect.X + inset, rect.Y + inset, rect.W - inset * 2.0f, rect.H - inset * 2.0f),
						i == hovered ? _session.GetAnimTime() : 0.0f);
					if (i < _favoriteCount) {
						painter.Circle(Vector2f(rect.X + rect.W - inset * 1.5f, rect.Y + inset * 1.5f), std::max(1.5f, inset), Colorf(1.0f, 0.85f, 0.3f, 1.0f), UiLayer::Overlay);
					}
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				std::int32_t index = GetIndexAt(event.Pos);
				if (event.Button == MouseButton::Right) {
					if (index >= 0) {
						ToggleFavoriteEvent(_session, _items[index]);
					}
					return false;
				}
				_pressed = (event.Button == MouseButton::Left ? index : -1);
				return (_pressed >= 0);
			}

			void OnPointerUp(const UiPointerEvent& event, bool inside) override
			{
				std::int32_t index = GetIndexAt(event.Pos);
				if (inside && index >= 0 && index == _pressed) {
					ChooseEvent(_session, _items[index]);
				}
				_pressed = -1;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				std::int32_t index = GetIndexAt(pos);
				if (index < 0) {
					return _x("editor", "Favourite events and those placed recently - right-click an event to make it a favourite");
				}
				_tooltip = EventPreviews::GetEventName(_items[index]) + "\n"_s + (index < _favoriteCount
					? StringView(_x("editor", "Favourite - right-click to take it away")) : StringView(_x("editor", "Placed recently - right-click to make it a favourite")));
				return _tooltip;
			}

		private:
			IEditorContext& _session;
			SmallVector<std::uint16_t, 0> _items;
			std::int32_t _favoriteCount = 0;
			std::int32_t _pressed;
			mutable String _tooltip;

			Rectf GetItemRect(std::int32_t index, float size) const
			{
				return Rectf(Bounds.X + index * (size + 2.0f), Bounds.Y, size, size);
			}

			std::int32_t GetIndexAt(Vector2f pos) const
			{
				float size = Bounds.H;
				if (!Bounds.Contains(pos) || size <= 0.0f) {
					return -1;
				}
				std::int32_t index = std::int32_t((pos.X - Bounds.X) / (size + 2.0f));
				return (index >= 0 && index < std::int32_t(_items.size()) && GetItemRect(index, size).Contains(pos) ? index : -1);
			}
		};

		// The type of the event inside of a crate resets its parameters, which mean something else for another type
		void WriteEventTypeField(Formats::EventRecord& event, const EventParamField& field, std::uint16_t type)
		{
			if (const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type))) {
				EventCatalog::SetContentsType(*descriptor, field, event, type);
			} else {
				EventCatalog::WriteField(event, field, std::int32_t(type));
			}
		}

		/** The event shown by the inspector, with its name and what is edited */
		class EventHeader : public UiElement
		{
		public:
			EventHeader(IEditorContext& session, EventInspectorPanel& panel)
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
				float inset = painter.Px(2.0f);
				bool mixedKinds = _panel.HasMixedKinds();
				painter.RoundRect(Rectf(Bounds.X, Bounds.Y, size, size), UiTheme::InputFill);
				EventPreviews& previews = _session.GetEventPreviews();
				if (mixedKinds) {
					// Two of the kinds, one over the other
					float part = std::round((size - inset * 2.0f) * 0.65f);
					previews.DrawFitted(painter, _panel.GetOtherKind(), Rectf(Bounds.X + size - inset - part, Bounds.Y + size - inset - part, part, part), _session.GetAnimTime());
					previews.DrawFitted(painter, event, Rectf(Bounds.X + inset, Bounds.Y + inset, part, part), _session.GetAnimTime());
				} else {
					previews.DrawFitted(painter, event, Rectf(Bounds.X + inset, Bounds.Y + inset, size - inset * 2.0f, size - inset * 2.0f), _session.GetAnimTime());
				}

				float x = Bounds.X + size + painter.Px(UiTheme::Padding);
				float lineHeight = std::round(Bounds.H * 0.5f);
				Rectf text(x, Bounds.Y, Bounds.X + Bounds.W - x, lineHeight);
				painter.PushClip(Rectf(x, Bounds.Y, text.W, Bounds.H));
				// TRANSLATORS: Instead of the name of the selected events in the event properties, when they're of different kinds
				painter.TextInRect(mixedKinds ? String(_x("editor", "Different kinds")) : EventPreviews::GetEventName(event.Type), text, UiTheme::TextHeader);

				const EventSelection& selection = _session.GetEventSelection();
				std::int32_t count = selection.GetTotalCount();
				String subtitle;
				if (count == 1 && selection.HasOffGrid()) {
					// TRANSLATORS: Under the name of the event in the event properties, the selected event is placed off the grid of tiles
					subtitle = _x("editor", "Off the grid");
				} else if (count == 0) {
					// TRANSLATORS: Under the name of the event in the event properties, the event isn't selected but about to be placed
					subtitle = _x("editor", "To place");
				} else if (count == 1) {
					const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
					// TRANSLATORS: Under the name of the event in the event properties instead of its category, when the event isn't known
					subtitle = (descriptor != nullptr ? String(EventCatalog::GetCategoryName(descriptor->Category)) : String(_x("editor", "Unknown event")));
				} else {
					// TRANSLATORS: Under the name of the event in the event properties, {} is the count of selected events
					subtitle = _fx("editor", "{} selected", count);
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
				if (!_panel.GetPrimary(event) || _panel.HasMixedKinds()) {
					return {};
				}
				const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
				if (descriptor == nullptr || descriptor->Description == nullptr) {
					return {};
				}
				_tooltip = _x("editor", descriptor->Description);
				return _tooltip;
			}

		private:
			IEditorContext& _session;
			EventInspectorPanel& _panel;
			mutable String _tooltip;
		};
	}

	EventGrid::EventGrid(IEditorContext& session)
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
			if (!MatchesSearch(_x("editor", descriptor->Name), _search) && !MatchesSearch(descriptor->Key, _search)) {
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
				_measuredNameWidth = std::max(_measuredNameWidth, context.MeasureText(_x("editor", descriptor->Name)));
			}
		}
		float padding = context.Px(2.0f);
		float chipHeight = context.Px(ChipHeight);
		float minWidth = std::max(context.Px(ChipWidth * 0.5f), std::ceil(chipHeight + padding * 5.0f + _measuredNameWidth));
		_columns = std::max<std::int32_t>(1, std::int32_t(available.X / minWidth));
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
			painter.TextInRect(_x("editor", "No event matches"), Rectf(Bounds.X + painter.Px(UiTheme::Padding), Bounds.Y, Bounds.W, painter.Px(ChipHeight)), UiTheme::TextDisabled);
			return;
		}

		const Rectf& clip = painter.GetClip();
		float clipTop = (clip.H > 0.0f ? clip.Y : Bounds.Y);
		float clipBottom = (clip.H > 0.0f ? clip.Y + clip.H : Bounds.Y + Bounds.H);
		std::int32_t firstRow = std::max<std::int32_t>(0, std::int32_t((clipTop - Bounds.Y) / _chipHeight));
		std::int32_t lastRow = std::int32_t((clipBottom - Bounds.Y) / _chipHeight);

		const EventStamp& brush = _session.GetEventBrush();
		std::int32_t brushType = (brush.IsSingle() && _session.GetEventSelection().IsEmpty() ? std::int32_t(brush.GetFirst()->Type) : -1);
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
				painter.TextInRect(_x("editor", descriptor->Name), text, unused ? UiTheme::TextDisabled : UiTheme::Text);
				painter.PopClip();

				if (IsFavoriteEvent(_session.GetSettings(), std::uint16_t(descriptor->Type))) {
					painter.Circle(Vector2f(rect.X + rect.W - padding * 2.0f, rect.Y + padding * 2.0f), std::max(1.5f, padding), Colorf(1.0f, 0.85f, 0.3f, 1.0f), UiLayer::Overlay);
				}
			}
		}
	}

	bool EventGrid::AcceptsPointer() const
	{
		return true;
	}

	bool EventGrid::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button == MouseButton::Right) {
			// A favourite, kept above the list
			std::int32_t index = GetIndexAt(event.Pos);
			if (index >= 0) {
				ToggleFavoriteEvent(_session, std::uint16_t(_items[index]->Type));
			}
			return false;
		}
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
			ChooseEvent(_session, std::uint16_t(_items[index]->Type));
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
		_tooltip = _x("editor", descriptor->Name);
		if (descriptor->Description != nullptr) {
			_tooltip = _tooltip + "\n"_s + _x("editor", descriptor->Description);
		}
		_tooltip = _tooltip + "\n"_s + (IsFavoriteEvent(_session.GetSettings(), std::uint16_t(descriptor->Type))
			? StringView(_x("editor", "Right-click to take it away from the favourites")) : StringView(_x("editor", "Right-click to make it a favourite")));
		return _tooltip;
	}

	EventPickerPanel::EventPickerPanel(IEditorContext& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session), _grid(nullptr)
	{
		IEditorContext* s = &session;
		auto gridScroll = std::make_unique<UiScrollView>();
		_grid = gridScroll->SetContent<EventGrid>(session);
		EventGrid* grid = _grid;

		// A narrow panel has no room for a column of categories, so they're in a list next to the search
		auto* header = Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		auto categoryName = [](std::int32_t category) {
			// TRANSLATORS: First option of the dropdown of categories in the Events tab, shows events of all categories
			return String(category == EventGrid::AllCategories ? _x("editor", "All") : EventCatalog::GetCategoryName(EventCategory(category)));
		};
		auto* categories = header->Add<UiDropdown>([grid, categoryName]() {
			return categoryName(grid->GetCategory());
		}, [grid, categoryName](SmallVectorImpl<UiMenuEntry>& entries) {
			for (std::int32_t category = EventGrid::AllCategories; category < std::int32_t(EventCategory::Count); category++) {
				UiMenuEntry entry;
				entry.Label = categoryName(category);
				entry.IsChecked = [grid, category]() { return (grid->GetCategory() == category); };
				entry.Execute = [grid, category]() { grid->SetFilter(category, grid->GetSearch()); };
				entries.push_back(Death::move(entry));
			}
		});
		categories->Flex = 1.0f;
		categories->Tooltip = _x("editor", "Category of the events shown");
		auto* search = header->Add<UiTextBox>();
		// TRANSLATORS: Placeholder of the search box in the Events tab, shown while it's empty
		search->Placeholder = _x("editor", "Search");
		search->Flex = 1.0f;
		search->OnChanged = [grid](StringView text) {
			grid->SetFilter(grid->GetCategory(), text);
		};

		auto* hint = Add<UiLabel>(_x("editor", "Choose the event for the parameter - Esc cancels"), UiTheme::TextAccent);
		hint->IsVisibleFunc = [s]() { return s->IsPickingEvent(); };
		Add<EventShortcutStrip>(session);

		UiScrollView* scroll = static_cast<UiScrollView*>(AddChild(Death::move(gridScroll)));
		scroll->Flex = 1.0f;
	}

	EventInspectorPanel::EventInspectorPanel(IEditorContext& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session), _scroll(nullptr),
			_shownType(-1), _shownCount(-1), _shownBrush(false), _shownRevision(0), _mixed(0), _mixedRevision(UINT32_MAX), _mixedSelectionRevision(UINT32_MAX)
	{
		// The edge above, between it and the list of layers, is drawn by the splitter that resizes them
		// TRANSLATORS: Header of the panel with the properties of the selected events
		Add<UiLabel>(_x("editor", "Event Properties"), UiTheme::TextHeader);
		_scroll = Add<UiScrollView>();
		_scroll->Flex = 1.0f;
		Rebuild();
	}

	void EventInspectorPanel::Update(float timeMult)
	{
		// Events of different kinds show other rows, see Rebuild()
		UpdateMixed();
		Formats::EventRecord event;
		bool hasEvent = GetPrimary(event);
		std::int32_t type = (hasEvent ? std::int32_t(event.Type) : -1);
		std::int32_t count = _session.GetEventSelection().GetTotalCount();
		bool brush = (count == 0);
		// The fields of the event inside of a crate are those of its kind
		std::uint32_t contents = (hasEvent ? GetContentsSignature(event) : 0);
		if (type != _shownType || std::min<std::int32_t>(count, 2) != std::min<std::int32_t>(_shownCount, 2) || brush != _shownBrush || _shownRevision != EventCatalog::GetRevision() ||
			contents != _shownContents || HasMixedKinds() != _shownMixedKinds) {
			Rebuild();
		}
		// Another event selected while one is chosen for a parameter, the list chooses the event to place again
		if (std::shared_ptr<PickTarget> pick = _pendingPick.lock()) {
			if (!IsPickTargetShown(*pick)) {
				pick = nullptr;
				_session.CancelEventPick();
			}
		}
		UiPanel::Update(timeMult);
	}

	bool EventInspectorPanel::GetPrimary(Formats::EventRecord& event) const
	{
		return GetPrimaryEvent(_session, event);
	}

	void EventInspectorPanel::UpdateMixed()
	{
		const EventSelection& selection = _session.GetEventSelection();
		auto cells = selection.GetCells();
		auto offGrid = selection.GetOffGrid();
		// Found again only when the selection or the level changes, it goes through all selected events
		std::uint32_t selectionRevision = selection.GetRevision();
		std::uint32_t revision = _session.GetHistory().GetRevision();
		if (selectionRevision == _mixedSelectionRevision && revision == _mixedRevision) {
			return;
		}
		_mixedSelectionRevision = selectionRevision;
		_mixedRevision = revision;
		_mixed = 0;
		_otherKind = 0;

		Formats::EventRecord primary;
		const LevelDocument& document = _session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		if (selection.GetTotalCount() < 2 || (!cells.empty() && gridSize.X <= 0) || !GetPrimary(primary)) {
			return;
		}
		// The fields of the shown kind among the events of that kind, the rest among all selected events, as Change() does;
		// those of the grid and those off it alike
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(primary.Type));
		auto effectiveDifficulties = [](const Formats::EventRecord& event) {
			std::uint8_t difficulties = event.Flags & Formats::EventRecord::DifficultyMask;
			return (difficulties == 0 ? std::uint8_t(Formats::EventRecord::DifficultyMask) : difficulties);
		};
		std::uint8_t primaryDifficulties = effectiveDifficulties(primary);
		auto compare = [&](const Formats::EventRecord& event) {
			if (LevelDocument::IsEmptyEvent(event)) {
				return;
			}
			if (event.Type != primary.Type) {
				if (_otherKind == 0) {
					_otherKind = event.Type;
				}
			} else if (descriptor != nullptr) {
				for (std::uint8_t i = 0; i < descriptor->FieldCount && i < 32; i++) {
					if (EventCatalog::ReadField(event, descriptor->Fields[i]) != EventCatalog::ReadField(primary, descriptor->Fields[i])) {
						_mixed |= (std::uint64_t(1) << i);
					}
				}
				if (std::memcmp(event.Params, primary.Params, sizeof(event.Params)) != 0) {
					_mixed |= (std::uint64_t(1) << MixedRaw);
				}
			}
			std::uint8_t difficulties = effectiveDifficulties(event);
			static const std::uint8_t DifficultyBits[] = { Formats::EventRecord::Easy, Formats::EventRecord::Normal, Formats::EventRecord::Hard };
			for (std::int32_t d = 0; d < 3; d++) {
				if ((difficulties & DifficultyBits[d]) != (primaryDifficulties & DifficultyBits[d])) {
					_mixed |= (std::uint64_t(1) << (MixedEasy + d));
				}
			}
			std::uint8_t flagDifferences = std::uint8_t(event.Flags ^ primary.Flags);
			if ((flagDifferences & Formats::EventRecord::MultiplayerOnly) != 0) _mixed |= (std::uint64_t(1) << MixedMultiplayer);
			if ((flagDifferences & Formats::EventRecord::Illuminated) != 0) _mixed |= (std::uint64_t(1) << MixedIlluminated);
			if ((flagDifferences & Formats::EventRecord::Generator) != 0) _mixed |= (std::uint64_t(1) << MixedGenerator);
			if (event.GeneratorDelay != primary.GeneratorDelay) _mixed |= (std::uint64_t(1) << MixedDelay);
			if (((event.GeneratorFlags ^ primary.GeneratorFlags) & 0x01) != 0) _mixed |= (std::uint64_t(1) << MixedDelayFirst);
		};
		for (std::uint32_t cell : cells) {
			compare(document.GetEvent(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X))));
		}
		for (std::int32_t index : offGrid) {
			if (index >= 0 && index < std::int32_t(document.OffGridEvents.size())) {
				compare(document.OffGridEvents[index].Event);
			}
		}
	}

	bool EventInspectorPanel::IsShownValue(std::int32_t shown, std::int32_t committed, const EventParamField* field, bool final)
	{
		bool same = (committed == shown);
		if (!same && field != nullptr && field->Type != ParamType::Flags && field->Max > field->Min && (shown < field->Min || shown > field->Max)) {
			// A value out of the range is shown clamped by the box, committing it isn't an edit
			same = (committed == std::clamp(shown, field->Min, field->Max));
		}
		if (same && final) {
			// The end of a drag back to where it was, the steps before it are one
			_session.GetHistory().BreakMerge();
		}
		return same;
	}

	void EventInspectorPanel::AddMixedMark(UiElement* parent, std::uint64_t mask)
	{
		// TRANSLATORS: Next to a property of more selected events that differs among them
		auto* mark = parent->Add<UiLabel>(_x("editor", "mixed"), UiTheme::TextAccent);
		mark->Tooltip = _x("editor", "The selected events differ in it, a change sets it in all of them");
		EventInspectorPanel* self = this;
		mark->IsVisibleFunc = [self, mask]() { return (self->_mixed & mask) != 0; };
	}

	void EventInspectorPanel::BeginEventPick(const EventParamField* field)
	{
		Formats::EventRecord primary;
		if (!GetPrimary(primary)) {
			return;
		}
		const EventSelection& selection = _session.GetEventSelection();
		auto target = std::make_shared<PickTarget>();
		for (std::uint32_t cell : selection.GetCells()) {
			target->Cells.push_back(cell);
		}
		for (std::int32_t index : selection.GetOffGrid()) {
			target->OffGrid.push_back(index);
		}
		target->Type = primary.Type;
		target->Field = field;
		target->CatalogRevision = EventCatalog::GetRevision();
		_pendingPick = target;

		EventInspectorPanel* self = this;
		_session.PickEventFromList([self, target](EventType type) {
			// The events could have changed while the event was being chosen, the field is theirs only while they're shown
			if (!self->IsPickTargetShown(*target)) {
				self->_session.ShowMessage(_x("editor", "The events changed meanwhile, choose the event for the parameter again"));
				return;
			}
			const EventParamField* f = target->Field;
			self->Change(f->Name, [f, type](Formats::EventRecord& event) {
				WriteEventTypeField(event, *f, std::uint16_t(type));
			}, true);
		});
	}

	bool EventInspectorPanel::IsPickTargetShown(const PickTarget& target) const
	{
		Formats::EventRecord primary;
		if (target.CatalogRevision != EventCatalog::GetRevision() || !GetPrimary(primary) || primary.Type != target.Type) {
			return false;
		}
		const EventSelection& selection = _session.GetEventSelection();
		auto offGrid = selection.GetOffGrid();
		auto cells = selection.GetCells();
		return (offGrid.size() == target.OffGrid.size() && std::equal(offGrid.begin(), offGrid.end(), target.OffGrid.begin()) &&
			cells.size() == target.Cells.size() && std::equal(cells.begin(), cells.end(), target.Cells.begin()));
	}

	void EventInspectorPanel::Change(const char* name, Function<void(Formats::EventRecord&)>&& change, bool sameTypeOnly, bool final)
	{
		// A value that isn't dragged, or the end of a drag, ends the step, whichever way this returns
		struct MergeBreak {
			CommandHistory& History;
			bool Final;
			~MergeBreak() {
				if (Final) {
					History.BreakMerge();
				}
			}
		} mergeBreak{ _session.GetHistory(), final };

		Formats::EventRecord primary;
		if (!GetPrimary(primary)) {
			return;
		}

		EventSelection& selection = _session.GetEventSelection();
		// Parameters mean something else for each kind, they're changed only while all selected events are of the same kind
		UpdateMixed();
		if (sameTypeOnly && !selection.IsEmpty() && HasMixedKinds()) {
			return;
		}
		if (selection.IsEmpty()) {
			EventStamp& brush = _session.GetEventBrush();
			for (EventStamp::Item& item : brush.Items) {
				if (!sameTypeOnly || item.Event.Type == primary.Type) {
					change(item.Event);
				}
			}
			for (EventStamp::OffGridItem& item : brush.OffGrid) {
				if (!sameTypeOnly || item.Event.Type == primary.Type) {
					change(item.Event);
				}
			}
			return;
		}

		LevelDocument& document = _session.GetDocument();
		if (!_session.IsLayerEditable(document.GetSpriteLayerIndex())) {
			_session.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
			return;
		}

		std::unique_ptr<SetEventsCommand> command;
		Vector2i gridSize = document.GetEventGridSize();
		if (selection.HasCells() && gridSize.X > 0) {
			command = std::make_unique<SetEventsCommand>(document, _x("editor", name));
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
			if (command->IsEmpty()) {
				command = nullptr;
			}
		}

		std::unique_ptr<SetOffGridEventsCommand> offGridCommand;
		if (selection.HasOffGrid()) {
			SmallVector<Formats::OffGridEventRecord, 0> events;
			for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
				events.push_back(event);
			}
			bool changed = false;
			for (std::int32_t index : selection.GetOffGrid()) {
				if (index < 0 || index >= std::int32_t(events.size())) {
					continue;
				}
				Formats::EventRecord& event = events[index].Event;
				if (LevelDocument::IsEmptyEvent(event) || (sameTypeOnly && event.Type != primary.Type)) {
					continue;
				}
				Formats::EventRecord before = event;
				change(event);
				LevelDocument::NormalizeEvent(event);
				if (!LevelDocument::AreEventsEqual(before, event)) {
					changed = true;
				}
			}
			if (changed) {
				offGridCommand = std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), _x("editor", name));
			}
		}

		// Changes of the same property follow each other while a value is dragged, they're undone at once
		if (command != nullptr && offGridCommand != nullptr) {
			// Those on the grid and those off it are undone together, but such a step isn't merged with the next
			CommandHistory& history = _session.GetHistory();
			history.BeginTransaction(_x("editor", name));
			_session.Execute(Death::move(command));
			_session.Execute(Death::move(offGridCommand));
			history.EndTransaction();
		} else if (command != nullptr) {
			_session.Execute(Death::move(command), true);
		} else if (offGridCommand != nullptr) {
			_session.Execute(Death::move(offGridCommand), true);
		}
	}

	void EventInspectorPanel::Rebuild()
	{
		UpdateMixed();
		Formats::EventRecord event;
		bool hasEvent = GetPrimary(event);
		_shownType = (hasEvent ? std::int32_t(event.Type) : -1);
		_shownCount = _session.GetEventSelection().GetTotalCount();
		_shownBrush = (_shownCount == 0);
		_shownRevision = EventCatalog::GetRevision();
		_shownContents = (hasEvent ? GetContentsSignature(event) : 0);
		_shownMixedKinds = HasMixedKinds();

		auto* rows = _scroll->SetContent<UiForm>(2.0f, UiThickness(0.0f, 0.0f, 2.0f, 0.0f));
		if (!hasEvent) {
			// In short lines, labels don't wrap
			// TRANSLATORS: First line of a sentence in the event properties when no event is picked, "or in the level" is its second line
			rows->Add<UiLabel>(_x("editor", "Pick an event below"), UiTheme::TextDim);
			// TRANSLATORS: Second line of the sentence that starts with "Pick an event below", keep both lines short
			rows->Add<UiLabel>(_x("editor", "or in the level"), UiTheme::TextDim);
			return;
		}

		rows->Add<EventHeader>(_session, *this);
		if (_shownMixedKinds) {
			// Not the parameters of any of them, the difficulties and the flags below are all they have in common
			rows->Add<UiWrappedLabel>(_x("editor", "Events of different kinds are selected - only what they have in common can be changed"), UiTheme::TextDim);
		} else if (const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type))) {
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				AddField(rows, *descriptor, i);
			}
			if (descriptor->FieldCount == 0) {
				// TRANSLATORS: In the event properties instead of the parameters, when the event has none
				rows->Add<UiLabel>(_x("editor", "No parameters"), UiTheme::TextDisabled);
			}
		} else {
			rows->Add<UiWrappedLabel>(_x("editor", "Unknown event, only its raw parameters can be changed"), UiTheme::TextDim);
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
			auto* row = AddRow(rows, _("Difficulty"), _x("editor", "In which difficulties the event is in the level"));
			struct Difficulty { const char* Label; std::uint8_t Bit; };
			static const Difficulty Difficulties[] = {
				// TRANSLATORS: Check boxes of the difficulties the event is in, in the event properties
				{ NC_("editor", "Easy"), Formats::EventRecord::Easy },
				{ NC_("editor", "Normal"), Formats::EventRecord::Normal },
				{ NC_("editor", "Hard"), Formats::EventRecord::Hard },
			};
			auto* checks = row->Add<UiStack>(UiOrientation::Vertical, 1.0f);
			checks->Flex = 1.0f;
			AddMixedMark(row, (std::uint64_t(1) << MixedEasy) | (std::uint64_t(1) << MixedNormal) | (std::uint64_t(1) << MixedHard));
			for (const Difficulty& difficulty : Difficulties) {
				std::uint8_t bit = difficulty.Bit;
				checks->Add<UiCheckBox>(_x("editor", difficulty.Label), [readFlags, bit]() {
					std::uint8_t flags = readFlags();
					return ((flags & Formats::EventRecord::DifficultyMask) == 0 || (flags & bit) != 0);
				}, [self, bit](bool value) {
					// TRANSLATORS: Name of an undo step in the history, the difficulties of events were changed
					self->Change(NC_("editor", "Difficulty"), [bit, value](Formats::EventRecord& event) {
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

		struct FlagRow { const char* Label; const char* Description; std::uint8_t Bit; std::int32_t Mixed; };
		static const FlagRow FlagRows[] = {
			// TRANSLATORS: Check boxes in the event properties with their tooltips, the labels are also names of undo steps in the history
			{ NC_("editor", "Multiplayer only"), NC_("editor", "The event is in the level only in multiplayer"), Formats::EventRecord::MultiplayerOnly, MixedMultiplayer },
			{ NC_("editor", "Illuminated"), NC_("editor", "The object glows in the dark"), Formats::EventRecord::Illuminated, MixedIlluminated },
			{ NC_("editor", "Generator"), NC_("editor", "The object appears again a while after it's gone"), Formats::EventRecord::Generator, MixedGenerator },
		};
		for (const FlagRow& flagRow : FlagRows) {
			std::uint8_t bit = flagRow.Bit;
			const char* label = flagRow.Label;
			auto* check = rows->Add<UiCheckBox>(_x("editor", label), [readFlags, bit]() {
				return ((readFlags() & bit) != 0);
			}, [self, bit, label](bool value) {
				self->Change(label, [bit, value](Formats::EventRecord& event) {
					event.Flags = std::uint8_t(value ? (event.Flags | bit) : (event.Flags & ~bit));
				}, false);
			});
			check->Tooltip = _x("editor", flagRow.Description);
			AddMixedMark(rows, flagRow.Mixed);
		}

		auto isGenerator = [readFlags]() {
			return ((readFlags() & Formats::EventRecord::Generator) != 0);
		};
		{
			// TRANSLATORS: Label of the delay of a generator in the event properties (noun), "s" is its unit, short for seconds
			auto* row = AddRow(rows, String(_x("editor", "Delay") + " ("_s + _x("editor", "s") + ")"_s), _x("editor", "Seconds before the object appears again"));
			auto* number = row->Add<UiNumberBox>([self]() {
				Formats::EventRecord event;
				return (self->GetPrimary(event) ? float(event.GeneratorDelay) : 0.0f);
			}, [self](float value, bool final) {
				Formats::EventRecord primary;
				if (self->GetPrimary(primary) && self->IsShownValue(primary.GeneratorDelay, std::int32_t(std::round(value)), nullptr, final)) {
					return;
				}
				// TRANSLATORS: Name of an undo step in the history, the delay of generator events was changed
				self->Change(NC_("editor", "Generator delay"), [value](Formats::EventRecord& event) {
					event.GeneratorDelay = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 255));
				}, false, final);
			}, 0.0f, 255.0f, 1.0f, 0);
			number->Flex = 1.0f;
			AddMixedMark(row, MixedDelay);
			// The whole row, with its label
			ShowRowWhen(row, isGenerator);
		}
		{
			// TRANSLATORS: Check box in the event properties of a generator, the first object also appears only after the delay
			auto* check = rows->Add<UiCheckBox>(_x("editor", "Delay the first one too"), [self]() {
				Formats::EventRecord event;
				return (self->GetPrimary(event) && (event.GeneratorFlags & 0x01) != 0);
			}, [self](bool value) {
				// Named apart from the delay, changes of the same name would be merged into one step
				// TRANSLATORS: Name of an undo step in the history, the same as the check box in the event properties of a generator
				self->Change(NC_("editor", "Delay the first one too"), [value](Formats::EventRecord& event) {
					event.GeneratorFlags = std::uint8_t(value ? (event.GeneratorFlags | 0x01) : (event.GeneratorFlags & ~0x01));
				}, false);
			});
			check->IsVisibleFunc = isGenerator;
		}

		// All parameters as they are stored, for anything the fields above don't cover, only of events of a single kind
		if (!_shownMixedKinds) {
			// TRANSLATORS: Label in the event properties above the bytes of the parameters of the event as they're stored
			rows->Add<UiLabel>(_x("editor", "Raw parameters"), UiTheme::TextDim);
			AddMixedMark(rows, MixedRaw);
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
				// Only the bytes changed from those shown, of the shown event, the other selected events keep the rest
				Formats::EventRecord primary;
				if (!self->GetPrimary(primary)) {
					return;
				}
				bool changed[Formats::LevelFile::EventParamsSize];
				bool anyChanged = false;
				for (std::int32_t i = 0; i < Formats::LevelFile::EventParamsSize; i++) {
					changed[i] = (params[i] != primary.Params[i]);
					anyChanged |= changed[i];
				}
				if (!anyChanged) {
					return;
				}
				// TRANSLATORS: Name of an undo step in the history, the raw parameters of events were changed
				self->Change(NC_("editor", "Raw parameters"), [params, changed](Formats::EventRecord& event) {
					for (std::int32_t i = 0; i < Formats::LevelFile::EventParamsSize; i++) {
						if (changed[i]) {
							event.Params[i] = params[i];
						}
					}
				}, true);
			};
		}
	}

	void EventInspectorPanel::AddField(UiElement* rows, const EventDescriptor& descriptor, std::int32_t index)
	{
		const EventParamField& field = descriptor.Fields[index];
		// A value only kept as it is
		if (field.Hidden) {
			return;
		}
		std::int32_t firstChild = rows->GetChildCount();
		AddFieldRows(rows, descriptor, index);
		// A field shown only while another one has a value, all of its rows with it, see EventCatalog::IsFieldVisible()
		if (field.VisibleIfField >= 0) {
			EventInspectorPanel* self = this;
			const EventDescriptor* d = &descriptor;
			for (std::int32_t i = firstChild; i < rows->GetChildCount(); i++) {
				rows->GetChild(i)->IsVisibleFunc = [self, d, index]() {
					Formats::EventRecord event;
					return (!self->GetPrimary(event) || event.Type != std::uint16_t(d->Type) || EventCatalog::IsFieldVisible(*d, std::size_t(index), event));
				};
			}
		}
	}

	std::uint32_t EventInspectorPanel::GetContentsSignature(const Formats::EventRecord& event) const
	{
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
		if (descriptor == nullptr) {
			return 0;
		}
		std::uint32_t types[UINT8_MAX];
		std::size_t count = 0;
		for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
			const EventParamField& field = descriptor->Fields[i];
			if (field.Type == ParamType::Contents && field.ContentsTypeField >= 0 && field.ContentsTypeField < descriptor->FieldCount) {
				types[count++] = std::uint32_t(EventCatalog::ReadField(event, descriptor->Fields[field.ContentsTypeField]));
			}
		}
		// Not 0 even without any contents, that's for no event
		std::uint64_t signature = xxHash3(types, count * sizeof(std::uint32_t));
		return (std::uint32_t(signature ^ (signature >> 32)) | 1u);
	}

	void EventInspectorPanel::AddContentsFields(UiElement* rows, const EventDescriptor& descriptor, std::int32_t index)
	{
		const EventDescriptor* d = &descriptor;
		const EventParamField* contents = &descriptor.Fields[index];
		Formats::EventRecord primary;
		if (!GetPrimary(primary)) {
			return;
		}
		const EventDescriptor* inner = EventCatalog::GetContentsDescriptor(descriptor, *contents, primary);
		if (inner == nullptr) {
			rows->Add<UiLabel>(_x("editor", "Nothing inside, or nothing known of it"), UiTheme::TextDisabled);
			return;
		}
		// TRANSLATORS: Header of the parameters of the event inside of a crate or a barrel, with its name
		rows->Add<UiLabel>(_fx("editor", "Inside - {}", EventPreviews::GetEventName(std::uint16_t(inner->Type))), UiTheme::TextHeader);

		EventInspectorPanel* self = this;
		for (std::uint8_t i = 0; i < inner->FieldCount; i++) {
			const EventParamField* f = &inner->Fields[i];
			if (f->Hidden || !EventCatalog::FitsInContents(*contents, *f)) {
				continue;
			}
			std::int32_t firstChild = rows->GetChildCount();
			auto read = [self, d, contents, f]() -> std::int32_t {
				Formats::EventRecord event;
				if (!self->GetPrimary(event)) {
					return 0;
				}
				return EventCatalog::ReadField(EventCatalog::GetContents(*d, *contents, event), *f);
			};
			auto write = [self, d, contents, inner, f](std::int32_t value, bool final) {
				self->Change(f->Name, [d, contents, inner, f, value](Formats::EventRecord& event) {
					// Only inside of those with the same kind of event inside, the field means something else in another kind
					if (EventCatalog::GetContentsDescriptor(*d, *contents, event) != inner) {
						return;
					}
					Formats::EventRecord inside = EventCatalog::GetContents(*d, *contents, event);
					EventCatalog::WriteField(inside, *f, value);
					EventCatalog::SetContents(*contents, event, inside);
				}, true, final);
			};
			String label = _x("editor", f->Name);
			String tooltip = (f->Description != nullptr ? String(_x("editor", f->Description)) : String());
			switch (f->Type) {
				case ParamType::Bool: {
					auto* check = rows->Add<UiCheckBox>(label, [read]() { return (read() != 0); }, [write](bool value) { write(value ? 1 : 0, true); });
					check->Tooltip = tooltip;
					break;
				}
				case ParamType::Enum: {
					auto* row = AddRow(rows, label, tooltip);
					auto* dropdown = row->Add<UiDropdown>([read, f]() {
						std::int32_t value = read();
						for (std::uint8_t j = 0; j < f->OptionCount; j++) {
							if (f->Options[j].Value == value) {
								return String(_x("editor", f->Options[j].Label));
							}
						}
						return FormatInteger(value);
					}, [read, write, f](SmallVectorImpl<UiMenuEntry>& entries) {
						std::int32_t current = read();
						for (std::uint8_t j = 0; j < f->OptionCount; j++) {
							std::int32_t value = f->Options[j].Value;
							UiMenuEntry entry;
							entry.Label = _x("editor", f->Options[j].Label);
							entry.IsChecked = [current, value]() { return (current == value); };
							entry.Execute = [write, value]() { write(value, true); };
							entries.push_back(Death::move(entry));
						}
					});
					dropdown->Flex = 1.0f;
					dropdown->Tooltip = tooltip;
					break;
				}
				case ParamType::Flags: {
					rows->Add<UiLabel>(label, UiTheme::TextDim)->Tooltip = tooltip;
					auto* checks = rows->Add<UiStack>(UiOrientation::Vertical, 1.0f, UiThickness(8.0f, 0.0f, 0.0f, 0.0f));
					for (std::uint8_t j = 0; j < f->OptionCount; j++) {
						std::int32_t mask = f->Options[j].Value;
						checks->Add<UiCheckBox>(_x("editor", f->Options[j].Label), [read, mask]() {
							return ((read() & mask) == mask);
						}, [self, d, contents, inner, f, mask](bool value) {
							// Only the bit, each event keeps its other flags
							self->Change(f->Name, [d, contents, inner, f, mask, value](Formats::EventRecord& event) {
								if (EventCatalog::GetContentsDescriptor(*d, *contents, event) != inner) {
									return;
								}
								Formats::EventRecord inside = EventCatalog::GetContents(*d, *contents, event);
								std::int32_t flags = EventCatalog::ReadField(inside, *f);
								EventCatalog::WriteField(inside, *f, value ? (flags | mask) : (flags & ~mask));
								EventCatalog::SetContents(*contents, event, inside);
							}, true);
						});
					}
					break;
				}
				case ParamType::UInt:
				case ParamType::Int:
				case ParamType::TextId:
				case ParamType::TriggerId:
				case ParamType::WarpId: {
					StringView unit = GetUnitSuffix(f->Unit);
					auto* row = AddRow(rows, unit.empty() ? label : String(label + " ("_s + unit + ")"_s), tooltip);
					auto* number = row->Add<UiNumberBox>([read, f]() {
						return float(EventCatalog::ToDisplayValue(*f, read()));
					}, [self, read, write, f](float value, bool final) {
						std::int32_t stored = EventCatalog::FromDisplayValue(*f, std::int32_t(std::round(value)));
						if (!self->IsShownValue(read(), stored, f, final)) {
							write(stored, final);
						}
					}, float(EventCatalog::ToDisplayValue(*f, f->Min)), float(EventCatalog::ToDisplayValue(*f, f->Max)), float(std::max<std::int32_t>(f->DisplayScale, 1)), 0);
					number->Flex = 1.0f;
					number->Tooltip = tooltip;
					break;
				}
				default:
					// Texts, events inside events and raw bytes are left to the raw parameters
					break;
			}
			// Shown by the condition of the event inside, as it is in the crate now
			if (f->VisibleIfField >= 0) {
				std::size_t fieldIndex = i;
				for (std::int32_t c = firstChild; c < rows->GetChildCount(); c++) {
					rows->GetChild(c)->IsVisibleFunc = [self, d, contents, inner, fieldIndex]() {
						Formats::EventRecord event;
						if (!self->GetPrimary(event) || event.Type != std::uint16_t(d->Type)) {
							return true;
						}
						return EventCatalog::IsFieldVisible(*inner, fieldIndex, EventCatalog::GetContents(*d, *contents, event));
					};
				}
			}
		}
	}

	void EventInspectorPanel::AddFieldRows(UiElement* rows, const EventDescriptor& descriptor, std::int32_t index)
	{
		const EventParamField& field = descriptor.Fields[index];
		// The mark of a field that differs among the selected events, see UpdateMixed()
		std::int32_t mixedBit = (index >= 0 && index < 32 ? index : -1);
		auto addMixedMark = [this, mixedBit](UiElement* parent) {
			if (mixedBit >= 0) {
				AddMixedMark(parent, mixedBit);
			}
		};
		EventInspectorPanel* self = this;
		const EventParamField* f = &field;
		String label = _x("editor", field.Name);
		StringView unit = GetUnitSuffix(field.Unit);
		String tooltip = (field.Description != nullptr ? String(_x("editor", field.Description)) : String());
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
				addMixedMark(rows);
				break;
			}
			case ParamType::Enum: {
				auto* row = AddRow(rows, label, tooltip);
				auto* dropdown = row->Add<UiDropdown>([read, f]() {
					std::int32_t value = read();
					for (std::uint8_t i = 0; i < f->OptionCount; i++) {
						if (f->Options[i].Value == value) {
							return String(_x("editor", f->Options[i].Label));
						}
					}
					return FormatInteger(value);
				}, [self, read, f](SmallVectorImpl<UiMenuEntry>& entries) {
					std::int32_t current = read();
					for (std::uint8_t i = 0; i < f->OptionCount; i++) {
						std::int32_t value = f->Options[i].Value;
						UiMenuEntry entry;
						entry.Label = _x("editor", f->Options[i].Label);
						entry.IsChecked = [current, value]() { return (current == value); };
						entry.Execute = [self, f, value]() {
							self->Change(f->Name, [f, value](Formats::EventRecord& event) {
								EventCatalog::WriteField(event, *f, value);
							}, true);
						};
						entries.push_back(Death::move(entry));
					}
				});
				dropdown->Flex = 1.0f;
				dropdown->Tooltip = tooltip;
				addMixedMark(row);
				break;
			}
			case ParamType::Flags: {
				auto* title = rows->Add<UiLabel>(label, UiTheme::TextDim);
				title->Tooltip = tooltip;
				addMixedMark(rows);
				auto* checks = rows->Add<UiStack>(UiOrientation::Vertical, 1.0f, UiThickness(8.0f, 0.0f, 0.0f, 0.0f));
				for (std::uint8_t i = 0; i < field.OptionCount; i++) {
					std::int32_t mask = field.Options[i].Value;
					checks->Add<UiCheckBox>(_x("editor", field.Options[i].Label), [read, mask]() {
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
				auto* dropdown = row->Add<UiDropdown>([read]() {
					std::int32_t value = read();
					// TRANSLATORS: Value of a parameter in the event properties that holds an event, when it holds none
					return (value == 0 ? String(_x("editor", "Nothing")) : EventPreviews::GetEventName(std::uint16_t(value)));
				}, [self, f](SmallVectorImpl<UiMenuEntry>& entries) {
					UiMenuEntry choose;
					// TRANSLATORS: Option of a parameter in the event properties that holds an event, the event is then picked in the Events tab
					choose.Label = _x("editor", "Choose in the Events tab");
					choose.Icon = EditorIcon::Event;
					choose.Execute = [self, f]() {
						self->BeginEventPick(f);
					};
					entries.push_back(Death::move(choose));
					UiMenuEntry none;
					// TRANSLATORS: Option of a parameter in the event properties that holds an event, sets it to no event
					none.Label = _x("editor", "Nothing");
					none.Execute = [self, f]() {
						self->Change(f->Name, [f](Formats::EventRecord& event) {
							WriteEventTypeField(event, *f, 0);
						}, true);
					};
					entries.push_back(Death::move(none));
				});
				dropdown->Flex = 1.0f;
				dropdown->Tooltip = tooltip;
				addMixedMark(row);
				break;
			}
			case ParamType::String: {
				auto* row = AddRow(rows, label, tooltip);
				auto* text = row->Add<UiTextBox>();
				text->Flex = 1.0f;
				addMixedMark(row);
				text->MaxLength = field.Bits / 8;
				text->TextFunc = [self, f]() {
					Formats::EventRecord event;
					char buffer[Formats::LevelFile::EventParamsSize + 1];
					return (self->GetPrimary(event) ? String(EventCatalog::ReadString(event, *f, buffer, sizeof(buffer))) : String());
				};
				text->OnCommit = [self, f](StringView value) {
					// The text shown isn't written again, into other selected events with another one
					Formats::EventRecord primary;
					char buffer[Formats::LevelFile::EventParamsSize + 1];
					if (self->GetPrimary(primary) && value == EventCatalog::ReadString(primary, *f, buffer, sizeof(buffer))) {
						return;
					}
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
			case ParamType::Contents:
				AddContentsFields(rows, descriptor, index);
				break;
			default: {
				// Shown multiplied by its scale, as the steps of a bridge in pixels, see EventParamField::DisplayScale
				String text = (unit.empty() ? label : String(label + " ("_s + unit + ")"_s));
				auto* row = AddRow(rows, text, tooltip);
				auto* number = row->Add<UiNumberBox>([read, f]() {
					return float(EventCatalog::ToDisplayValue(*f, read()));
				}, [self, read, f](float value, bool final) {
					std::int32_t stored = EventCatalog::FromDisplayValue(*f, std::int32_t(std::round(value)));
					if (self->IsShownValue(read(), stored, f, final)) {
						return;
					}
					self->Change(f->Name, [f, stored](Formats::EventRecord& event) {
						EventCatalog::WriteField(event, *f, stored);
					}, true, final);
				}, float(EventCatalog::ToDisplayValue(field, field.Min)), float(EventCatalog::ToDisplayValue(field, field.Max)), float(std::max<std::int32_t>(field.DisplayScale, 1)), 0);
				number->Flex = 1.0f;
				number->Tooltip = tooltip;
				addMixedMark(row);

				if (field.Type == ParamType::TextId) {
					// The text itself, as far as it fits, or the level an exit without one goes to
					IEditorContext* session = &_session;
					auto* preview = rows->Add<UiLabel>(StringView(), UiTheme::TextDim);
					preview->TextFunc = [self, session, read, f]() {
						Formats::EventRecord event;
						String defaultLevel;
						if (self->GetPrimary(event) && EventPreviews::DescribeDefaultLevel(session->GetDocument(), event, *f, defaultLevel, true)) {
							return defaultLevel;
						}
						std::int32_t index = read();
						const auto& texts = session->GetDocument().Texts;
						return (index >= 0 && index < std::int32_t(texts.size()) && !texts[index].empty()
							? String("\""_s + EventCatalog::MakeTextPreview(texts[index], 64) + "\""_s) : String(_x("editor", "No such text")));
					};
				}
				break;
			}
		}
	}
}

#endif
