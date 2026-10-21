#include "UiVirtualList.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"
#include "UiWidgets.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>

#include <algorithm>
#include <cmath>

using namespace Death::Containers;

namespace Jazz2::Editor
{
	namespace
	{
		// How long letters typed in a row are looked for together, in frames at 60 FPS
		constexpr float TypeAheadFrames = 60.0f;
	}

	UiVirtualList::UiVirtualList()
		: RowHeight(UiTheme::RowHeight + 2.0f), Selectable(true), EmptyColor(UiTheme::TextDim), _selected(-1), _rowPixels(1.0f), _scrollTo(-1),
			_typedTime(0.0f), _pressedRow(-1)
	{
	}

	std::int32_t UiVirtualList::GetCount() const
	{
		return (CountFunc ? std::max<std::int32_t>(CountFunc(), 0) : 0);
	}

	void UiVirtualList::RebindRows()
	{
		for (std::int32_t& index : _rowIndices) {
			index = -1;
		}
	}

	void UiVirtualList::SetSelected(std::int32_t index, bool scrollIntoView)
	{
		std::int32_t count = GetCount();
		_selected = (index >= 0 && index < count ? index : -1);
		if (scrollIntoView && _selected >= 0) {
			ScrollToRow(_selected);
		}
	}

	void UiVirtualList::ScrollToRow(std::int32_t index)
	{
		// Done when it's laid out next, the rows may not be placed yet
		_scrollTo = index;
	}

	std::int32_t UiVirtualList::GetRowAt(Vector2f pos) const
	{
		if (!Bounds.Contains(pos) || _rowPixels <= 0.0f) {
			return -1;
		}
		std::int32_t index = std::int32_t((pos.Y - Bounds.Y) / _rowPixels);
		return (index >= 0 && index < GetCount() ? index : -1);
	}

	Vector2f UiVirtualList::Measure(const UiContext& context, Vector2f available)
	{
		_rowPixels = std::max(context.Px(RowHeight), 1.0f);
		std::int32_t count = GetCount();
		// The text in place of the rows takes a line, as a label would
		float height = (count == 0 && !EmptyText.empty() ? context.Px(UiTheme::RowHeight) : _rowPixels * float(count));
		Vector2f size(available.X < 1.0e5f ? available.X : context.Px(120.0f), height);
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void UiVirtualList::Arrange(const UiContext& context, const Rectf& bounds)
	{
		// The elements of the rows, if any, are placed by ArrangeRows(), each over its own row
		Bounds = bounds;
		_rowPixels = std::max(context.Px(RowHeight), 1.0f);
		if (CreateRow) {
			ArrangeRows(context);
		}
		if (_scrollTo >= 0) {
			std::int32_t index = _scrollTo;
			_scrollTo = -1;
			// The list it's in, from the top of what it scrolls
			for (UiElement* parent = GetParent(); parent != nullptr; parent = parent->GetParent()) {
				if (UiScrollView* scrollView = runtime_cast<UiScrollView>(parent)) {
					if (UiElement* content = scrollView->GetContent()) {
						float top = Bounds.Y - content->Bounds.Y + float(index) * _rowPixels;
						scrollView->ScrollIntoView(top, top + _rowPixels);
					}
					break;
				}
			}
		}
	}

	void UiVirtualList::ArrangeRows(const UiContext& context)
	{
		std::int32_t count = GetCount();
		// The part of it that is seen, inside of the list it's in
		float top = Bounds.Y, bottom = Bounds.Y + Bounds.H, viewHeight = Bounds.H;
		for (UiElement* parent = GetParent(); parent != nullptr; parent = parent->GetParent()) {
			if (runtime_cast<UiScrollView>(parent) != nullptr) {
				top = std::max(top, parent->Bounds.Y);
				bottom = std::min(bottom, parent->Bounds.Y + parent->Bounds.H);
				viewHeight = parent->Bounds.H;
				break;
			}
		}

		// As many elements as rows can be seen at once, one more for the rows cut by both edges, more are made when the
		// view grows, every row gets the element of another index then
		std::int32_t needed = std::min<std::int32_t>(count, std::int32_t(std::ceil(std::max(viewHeight, 0.0f) / _rowPixels)) + 1);
		if (needed > std::int32_t(_children.size())) {
			while (std::int32_t(_children.size()) < needed) {
				AddChild(CreateRow());
			}
			_rowIndices.resize(_children.size());
			RebindRows();
		}
		std::int32_t poolSize = std::int32_t(_children.size());
		if (poolSize == 0) {
			return;
		}

		std::int32_t first = 0, last = -1;
		if (count > 0 && bottom > top) {
			first = std::max<std::int32_t>(0, std::int32_t(std::floor((top - Bounds.Y) / _rowPixels)));
			last = std::min<std::int32_t>({ count - 1, std::int32_t(std::floor((bottom - Bounds.Y) / _rowPixels)), first + poolSize - 1 });
		}
		UiSurface* surface = GetSurface();
		for (std::int32_t index = first; index <= last; index++) {
			std::int32_t slot = index % poolSize;
			UiElement* row = _children[slot].get();
			if (_rowIndices[slot] != index) {
				// What has the focus in it was of the row before, the keys mustn't go on to change another one
				if (surface != nullptr) {
					for (UiElement* focus = surface->GetFocus(); focus != nullptr; focus = focus->GetParent()) {
						if (focus == row) {
							surface->SetFocus(nullptr);
							break;
						}
					}
				}
				_rowIndices[slot] = index;
				BindRow(*row, index);
			}
			row->Arrange(context, Rectf(Bounds.X, Bounds.Y + float(index) * _rowPixels, Bounds.W, _rowPixels));
		}
		for (std::int32_t slot = 0; slot < poolSize; slot++) {
			_children[slot]->Visible = (_rowIndices[slot] >= first && _rowIndices[slot] <= last);
		}
	}

	void UiVirtualList::Draw(UiPainter& painter)
	{
		std::int32_t count = GetCount();
		if (count <= 0) {
			if (!EmptyText.empty()) {
				// As a label would show it
				Rectf rect(Bounds.X, Bounds.Y, Bounds.W, painter.Px(UiTheme::RowHeight));
				painter.PushClip(rect);
				painter.TextInRect(EmptyText, rect, IsUsable() ? EmptyColor : UiTheme::TextDisabled);
				painter.PopClip();
			}
			return;
		}
		if (!DrawRow && !CreateRow) {
			return;
		}
		// Only the rows inside of the clipping, which the list it's in sets
		float top = Bounds.Y, bottom = Bounds.Y + Bounds.H;
		const Rectf& clip = painter.GetClip();
		if (clip.W > 0.0f && clip.H > 0.0f) {
			top = std::max(top, clip.Y);
			bottom = std::min(bottom, clip.Y + clip.H);
		}
		std::int32_t first = std::max<std::int32_t>(0, std::int32_t(std::floor((top - Bounds.Y) / _rowPixels)));
		std::int32_t last = std::min<std::int32_t>(count - 1, std::int32_t(std::floor((bottom - Bounds.Y) / _rowPixels)));

		std::int32_t hovered = -1;
		if (_hovered) {
			if (UiSurface* surface = GetSurface()) {
				hovered = GetRowAt(surface->GetPointerPos());
			}
		}
		if (DrawRow) {
			for (std::int32_t i = first; i <= last; i++) {
				Rectf rect(Bounds.X, Bounds.Y + float(i) * _rowPixels, Bounds.W, _rowPixels);
				DrawRow(painter, i, rect, i == _selected, i == hovered ? _hoverAmount : 0.0f);
			}
		}
		// The elements of the rows over what was drawn of them
		UiElement::Draw(painter);
		if (_focused && _selected >= first && _selected <= last) {
			// Where the keys are, as the border of the picked row, which is usually highlighted already; the ring of other
			// elements goes around them, it would be a second line next to the highlight
			painter.RoundRectBorder(Rectf(Bounds.X, Bounds.Y + float(_selected) * _rowPixels, Bounds.W, _rowPixels), UiTheme::FocusRing, UiLayer::Overlay);
		}
	}

	void UiVirtualList::Update(float timeMult)
	{
		UiElement::Update(timeMult);
		if (_typedTime > 0.0f) {
			_typedTime -= timeMult;
			if (_typedTime <= 0.0f) {
				_typed = {};
			}
		}
	}

	bool UiVirtualList::AcceptsPointer() const
	{
		return Selectable;
	}

	bool UiVirtualList::OnPointerDown(const UiPointerEvent& event)
	{
		std::int32_t index = GetRowAt(event.Pos);
		if (index < 0 || event.Button != MouseButton::Left) {
			return false;
		}
		// A double click is two clicks on the same row, the surface tells them only by the element
		bool doubleClick = (event.Clicks >= 2 && index == _pressedRow);
		_pressedRow = (doubleClick ? -1 : index);
		UiPointerEvent rowEvent = event;
		if (!doubleClick) {
			rowEvent.Clicks = 1;
		}
		Select(index);
		if (OnRowPressed) {
			OnRowPressed(index, Vector2f(event.Pos.X - Bounds.X, event.Pos.Y - (Bounds.Y + float(index) * _rowPixels)), rowEvent);
		}
		if (doubleClick && OnActivated && index < GetCount()) {
			OnActivated(index);
		}
		// Not held, so a finger dragged over it scrolls the list
		return false;
	}

	bool UiVirtualList::OnKeyPressed(const KeyboardEvent& event)
	{
		std::int32_t count = GetCount();
		if (_selected >= count) {
			// The list got shorter since, the row that was picked isn't there anymore, another one isn't activated instead
			_selected = -1;
		}
		if (count <= 0) {
			return false;
		}
		// Rows that fit in the list it's in, for Page Up and Page Down
		std::int32_t page = 10;
		for (UiElement* parent = GetParent(); parent != nullptr; parent = parent->GetParent()) {
			if (runtime_cast<UiScrollView>(parent) != nullptr) {
				page = std::max<std::int32_t>(1, std::int32_t(parent->Bounds.H / _rowPixels) - 1);
				break;
			}
		}
		switch (event.sym) {
			case Keys::Up: Select(std::max<std::int32_t>(_selected - 1, 0)); return true;
			case Keys::Down: Select(std::min<std::int32_t>(_selected + 1, count - 1)); return true;
			case Keys::PageUp: Select(std::max<std::int32_t>(_selected - page, 0)); return true;
			case Keys::PageDown: Select(std::min<std::int32_t>(_selected + page, count - 1)); return true;
			case Keys::Home: Select(0); return true;
			case Keys::End: Select(count - 1); return true;
			case Keys::Return:
			case Keys::NumPadEnter:
				if (_selected >= 0 && OnActivated) {
					OnActivated(_selected);
					return true;
				}
				return false;
			default:
				return false;
		}
	}

	bool UiVirtualList::OnTextInput(const TextInputEvent& event)
	{
		if (!RowText) {
			return false;
		}
		std::int32_t count = GetCount();
		if (count <= 0) {
			return false;
		}
		// Letters typed in a row are looked for together, the same letter again goes on to the next row with it
		String typed = StringUtils::lowercase(StringView(event.text, std::size_t(event.length)));
		bool repeated = (_typed.size() == typed.size() && _typed == typed);
		_typed = (repeated || _typedTime <= 0.0f ? typed : String(_typed + typed));
		_typedTime = TypeAheadFrames;
		std::int32_t start = (repeated ? _selected + 1 : std::max<std::int32_t>(_selected, 0));
		for (std::int32_t i = 0; i < count; i++) {
			std::int32_t index = (start + i) % count;
			String text = StringUtils::lowercase(RowText(index));
			if (text.hasPrefix(_typed)) {
				Select(index);
				return true;
			}
		}
		return true;
	}

	bool UiVirtualList::IsFocusable() const
	{
		return Selectable;
	}

	StringView UiVirtualList::GetTooltip(Vector2f pos) const
	{
		if (!RowTooltip) {
			return Tooltip;
		}
		std::int32_t index = GetRowAt(pos);
		if (index < 0) {
			return {};
		}
		// Asked for again every time, at most a few times a frame while it's shown, the rows may have been filtered or
		// changed since, a tooltip kept by the row would be of another one then
		_tooltip = RowTooltip(index);
		return _tooltip;
	}

	void UiVirtualList::Select(std::int32_t index)
	{
		if (index == _selected) {
			ScrollToRow(index);
			return;
		}
		SetSelected(index, true);
		if (OnSelectionChanged) {
			OnSelectionChanged(_selected);
		}
	}
}

#endif
