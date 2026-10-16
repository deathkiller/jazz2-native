#include "UiWidgets.h"

#if defined(WITH_EDITOR)

#include "UiKeyboard.h"
#include "UiSurface.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Input/IInputManager.h"

#include <Containers/StringConcatenable.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace Death;

namespace Jazz2::Editor
{
	namespace
	{
		Colorf Lerp(const Colorf& a, const Colorf& b, float t)
		{
			return Colorf(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t, a.A + (b.A - a.A) * t);
		}

		// Frames the caret is shown and hidden for
		constexpr float CaretBlinkPeriod = 60.0f;
	}

	UiLabel::UiLabel(StringView text, const Colorf& color, Alignment align)
		: Text(text), Color(color), Align(align)
	{
	}

	String UiLabel::GetText()
	{
		return (TextFunc ? TextFunc() : Text);
	}

	Vector2f UiLabel::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f size(context.MeasureText(GetText()) + context.Px(1.0f), context.Px(UiTheme::RowHeight));
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void UiLabel::Draw(UiPainter& painter)
	{
		Colorf color = (IsUsable() ? Color : UiTheme::TextDisabled);
		painter.PushClip(Bounds);
		painter.TextInRect(GetText(), Bounds, color, Align);
		painter.PopClip();
	}

	UiButton::UiButton(StringView text, EditorIcon icon, Function<void()>&& onClick)
		: Text(text), Icon(icon), Style(UiButtonStyle::Normal), OnClick(std::move(onClick)), IconColor(UiTheme::IconColor), MinWidth(0.0f),
			TextAlign(Alignment::Center), TextColor(0.0f, 0.0f, 0.0f, 0.0f)
	{
	}

	bool UiButton::IsChecked()
	{
		return (IsCheckedFunc && IsCheckedFunc());
	}

	Vector2f UiButton::Measure(const UiContext& context, Vector2f available)
	{
		float height = context.Px(UiTheme::ButtonHeight);
		float width;
		if (Text.empty()) {
			width = height;
		} else {
			float padding = context.Px(UiTheme::Padding) * 1.5f;
			width = padding * 2.0f + context.MeasureText(Text);
			if (Icon != EditorIcon::None) {
				width += context.Px(UiTheme::IconSize) + context.Px(UiTheme::Spacing);
			}
		}

		Vector2f size(std::max(std::round(width), context.Px(MinWidth)), height);
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void UiButton::Draw(UiPainter& painter)
	{
		bool usable = IsUsable();
		bool checked = IsChecked();
		float hover = (usable ? _hoverAmount : 0.0f);

		Colorf fill;
		switch (Style) {
			default:
			case UiButtonStyle::Normal:
				fill = (checked ? Lerp(UiTheme::ButtonChecked, UiTheme::ButtonCheckedHover, hover) : Lerp(UiTheme::ButtonFill, UiTheme::ButtonHover, hover));
				break;
			case UiButtonStyle::Flat:
				fill = (checked ? Lerp(UiTheme::ButtonChecked, UiTheme::ButtonCheckedHover, hover) : Colorf(UiTheme::ButtonHover.R, UiTheme::ButtonHover.G, UiTheme::ButtonHover.B, UiTheme::ButtonHover.A * hover));
				break;
			case UiButtonStyle::Accent:
				fill = Lerp(UiTheme::ButtonChecked, UiTheme::ButtonCheckedHover, hover);
				break;
		}
		if (_pressed && usable) {
			fill = (checked || Style == UiButtonStyle::Accent ? UiTheme::ButtonChecked : UiTheme::ButtonPressed);
		}
		if (fill.A > 0.0f) {
			painter.RoundRect(Bounds, fill);
		}
		if (checked) {
			painter.RoundRectBorder(Bounds, UiTheme::Accent);
		} else if (Style == UiButtonStyle::Normal) {
			painter.RoundRectBorder(Bounds, UiTheme::PanelBorderDim);
		}

		Colorf iconColor = (usable ? IconColor : UiTheme::IconDisabled);
		Colorf textColor = (usable ? (checked ? UiTheme::TextOnAccent : (TextColor.A > 0.0f ? TextColor : UiTheme::Text)) : UiTheme::TextDisabled);
		// Pressed content moves down a little, like a key
		float offset = (_pressed && usable ? std::max(1.0f, std::round(painter.GetScale() * 0.5f)) : 0.0f);

		if (Text.empty()) {
			painter.Icon(Icon, Rectf(Bounds.X, Bounds.Y + offset, Bounds.W, Bounds.H), iconColor);
			return;
		}

		float padding = painter.Px(UiTheme::Padding) * 1.5f;
		float x = Bounds.X + padding;
		float contentWidth = painter.MeasureText(Text);
		if (Icon != EditorIcon::None) {
			contentWidth += painter.Px(UiTheme::IconSize) + painter.Px(UiTheme::Spacing);
		}
		// Centered if the button is wider than its content, unless it's in a list
		if ((TextAlign & Alignment::HorizontalMask) != Alignment::Left) {
			x = std::max(x, std::round(Bounds.X + (Bounds.W - contentWidth) * 0.5f));
		}
		if (Icon != EditorIcon::None) {
			float iconSize = painter.Px(UiTheme::IconSize);
			painter.Icon(Icon, Rectf(x, Bounds.Y + offset, iconSize, Bounds.H), iconColor);
			x += iconSize + painter.Px(UiTheme::Spacing);
		}
		painter.TextInRect(Text, Rectf(x, Bounds.Y + offset, Bounds.X + Bounds.W - x, Bounds.H), textColor, Alignment::Left);
	}

	bool UiButton::AcceptsPointer() const
	{
		return true;
	}

	bool UiButton::OnPointerDown(const UiPointerEvent& event)
	{
		return (event.Button == MouseButton::Left && IsUsable());
	}

	void UiButton::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (inside && event.Button == MouseButton::Left && IsUsable() && OnClick) {
			OnClick();
		}
	}

	UiCheckBox::UiCheckBox(StringView text, Function<bool()>&& getter, Function<void(bool)>&& setter)
		: Text(text), Getter(std::move(getter)), Setter(std::move(setter))
	{
	}

	Vector2f UiCheckBox::Measure(const UiContext& context, Vector2f available)
	{
		float box = context.Px(10.0f);
		Vector2f size(box + context.Px(UiTheme::Spacing * 2.0f) + context.MeasureText(Text), context.Px(UiTheme::RowHeight));
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void UiCheckBox::Draw(UiPainter& painter)
	{
		bool usable = IsUsable();
		bool checked = (Getter && Getter());
		float box = painter.Px(10.0f);
		Rectf boxRect(Bounds.X, std::round(Bounds.Y + (Bounds.H - box) * 0.5f), box, box);
		painter.RoundRect(boxRect, Lerp(UiTheme::InputFill, UiTheme::ButtonHover, usable ? _hoverAmount : 0.0f));
		painter.RoundRectBorder(boxRect, checked ? UiTheme::Accent : UiTheme::InputBorder);
		if (checked) {
			float inset = std::max(2.0f, std::round(box * 0.25f));
			painter.RoundRect(Rectf(boxRect.X + inset, boxRect.Y + inset, boxRect.W - inset * 2.0f, boxRect.H - inset * 2.0f),
				usable ? UiTheme::Accent : UiTheme::AccentDim, UiLayer::Content);
		}
		float x = boxRect.X + box + painter.Px(UiTheme::Spacing * 2.0f);
		painter.TextInRect(Text, Rectf(x, Bounds.Y, Bounds.X + Bounds.W - x, Bounds.H), usable ? UiTheme::Text : UiTheme::TextDisabled);
	}

	bool UiCheckBox::AcceptsPointer() const
	{
		return true;
	}

	bool UiCheckBox::OnPointerDown(const UiPointerEvent& event)
	{
		return (event.Button == MouseButton::Left && IsUsable());
	}

	void UiCheckBox::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (inside && IsUsable() && Setter) {
			Setter(!(Getter && Getter()));
		}
	}

	UiSeparator::UiSeparator(UiOrientation orientation)
		: Orientation(orientation)
	{
	}

	Vector2f UiSeparator::Measure(const UiContext& context, Vector2f available)
	{
		float thickness = context.Px(UiTheme::Spacing * 2.0f) + std::max(1.0f, std::round(context.Scale * 0.5f));
		return (Orientation == UiOrientation::Horizontal ? Vector2f(0.0f, thickness) : Vector2f(thickness, 0.0f));
	}

	void UiSeparator::Draw(UiPainter& painter)
	{
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
		if (Orientation == UiOrientation::Horizontal) {
			painter.FillRect(Rectf(Bounds.X, std::round(Bounds.Y + (Bounds.H - thickness) * 0.5f), Bounds.W, thickness), UiTheme::Separator);
		} else {
			float inset = painter.Px(2.0f);
			painter.FillRect(Rectf(std::round(Bounds.X + (Bounds.W - thickness) * 0.5f), Bounds.Y + inset, thickness, Bounds.H - inset * 2.0f), UiTheme::Separator);
		}
	}

	UiScrollView::UiScrollView()
		: _scroll(0.0f), _targetScroll(0.0f), _contentHeight(0.0f), _dragOffset(0.0f), _draggingThumb(false)
	{
	}

	UiElement* UiScrollView::GetContent() const
	{
		return (!_children.empty() ? _children[0].get() : nullptr);
	}

	void UiScrollView::ScrollIntoView(float top, float bottom)
	{
		if (top < _targetScroll) {
			_targetScroll = top;
		} else if (bottom > _targetScroll + Bounds.H) {
			_targetScroll = bottom - Bounds.H;
		}
		_targetScroll = std::clamp(_targetScroll, 0.0f, GetMaxScroll());
	}

	Vector2f UiScrollView::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f size;
		Vector2f fixedSize = GetFixedSize(context);
		if (UiElement* content = GetContent()) {
			size = content->Measure(context, Vector2f(available.X, 1.0e6f));
			float maxHeight = (fixedSize.Y >= 0.0f ? fixedSize.Y : available.Y);
			if (size.Y > maxHeight) {
				// The scroll bar takes its own space next to the content
				size.X += context.Px(UiTheme::ScrollBarWidth);
			}
			size.Y = std::min(size.Y, available.Y);
		}
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void UiScrollView::Arrange(const UiContext& context, const Rectf& bounds)
	{
		Bounds = bounds;
		UiElement* content = GetContent();
		if (content == nullptr || !content->IsShown()) {
			_contentHeight = 0.0f;
			return;
		}

		float barWidth = context.Px(UiTheme::ScrollBarWidth);
		_contentHeight = content->Measure(context, Vector2f(bounds.W, 1.0e6f)).Y;
		if (_contentHeight > bounds.H) {
			// The scroll bar takes its own space
			_contentHeight = content->Measure(context, Vector2f(bounds.W - barWidth, 1.0e6f)).Y;
		}

		_targetScroll = std::clamp(_targetScroll, 0.0f, GetMaxScroll());
		_scroll = std::clamp(_scroll, 0.0f, GetMaxScroll());
		float width = (_contentHeight > bounds.H ? bounds.W - barWidth : bounds.W);
		content->Arrange(context, Rectf(bounds.X, std::round(bounds.Y - _scroll), width, std::max(_contentHeight, bounds.H)));
	}

	void UiScrollView::Draw(UiPainter& painter)
	{
		painter.PushClip(Bounds);
		UiElement::Draw(painter);
		painter.PopClip();

		if (_contentHeight > Bounds.H) {
			Rectf thumb = GetThumbRect();
			painter.RoundRect(thumb, _draggingThumb ? UiTheme::Accent : Colorf(1.0f, 1.0f, 1.0f, 0.22f + 0.15f * _hoverAmount), UiLayer::Overlay);
		}
	}

	void UiScrollView::Update(float timeMult)
	{
		UiElement::Update(timeMult);

		if (_scroll != _targetScroll) {
			float step = 1.0f - std::pow(0.65f, timeMult);
			_scroll += (_targetScroll - _scroll) * step;
			if (std::abs(_targetScroll - _scroll) < 0.5f) {
				_scroll = _targetScroll;
			}
		}
	}

	UiElement* UiScrollView::HitTest(Vector2f pos)
	{
		if (!IsShown() || !Bounds.Contains(pos)) {
			return nullptr;
		}
		if (_contentHeight > Bounds.H && pos.X >= Bounds.X + Bounds.W - GetThumbRect().W * 1.5f) {
			return this;
		}
		if (UiElement* content = GetContent()) {
			if (UiElement* hit = content->HitTest(pos)) {
				return hit;
			}
		}
		return this;
	}

	bool UiScrollView::AcceptsPointer() const
	{
		return true;
	}

	bool UiScrollView::OnPointerDown(const UiPointerEvent& event)
	{
		if (_contentHeight <= Bounds.H || event.Button != MouseButton::Left) {
			return false;
		}
		Rectf thumb = GetThumbRect();
		if (event.Pos.X < thumb.X - thumb.W * 0.5f) {
			return false;
		}
		if (!thumb.Contains(event.Pos)) {
			// A click on the track moves the thumb there
			float ratio = std::clamp((event.Pos.Y - Bounds.Y - thumb.H * 0.5f) / std::max(Bounds.H - thumb.H, 1.0f), 0.0f, 1.0f);
			_targetScroll = _scroll = ratio * GetMaxScroll();
			thumb = GetThumbRect();
		}
		_draggingThumb = true;
		_dragOffset = event.Pos.Y - thumb.Y;
		return true;
	}

	void UiScrollView::OnPointerMove(const UiPointerEvent& event)
	{
		if (!_draggingThumb) {
			return;
		}
		Rectf thumb = GetThumbRect();
		float ratio = std::clamp((event.Pos.Y - _dragOffset - Bounds.Y) / std::max(Bounds.H - thumb.H, 1.0f), 0.0f, 1.0f);
		_targetScroll = _scroll = ratio * GetMaxScroll();
	}

	bool UiScrollView::OnWheel(const UiPointerEvent& event)
	{
		if (_contentHeight <= Bounds.H) {
			return false;
		}
		_draggingThumb = false;
		UiSurface* surface = GetSurface();
		float rowHeight = std::round(UiTheme::RowHeight * (surface != nullptr ? surface->GetScale() : 1.0f));
		_targetScroll = std::clamp(_targetScroll - event.Wheel.Y * rowHeight * 3.0f, 0.0f, GetMaxScroll());
		return true;
	}

	Rectf UiScrollView::GetThumbRect() const
	{
		UiSurface* surface = GetSurface();
		float scale = (surface != nullptr ? surface->GetScale() : 1.0f);
		float width = std::round(UiTheme::ScrollBarWidth * scale);
		float height = std::max(Bounds.H * Bounds.H / std::max(_contentHeight, 1.0f), width * 2.0f);
		float maxScroll = GetMaxScroll();
		float y = Bounds.Y + (maxScroll > 0.0f ? (Bounds.H - height) * (_scroll / maxScroll) : 0.0f);
		float inset = std::max(1.0f, std::round(scale));
		return Rectf(Bounds.X + Bounds.W - width + inset, y + inset, width - inset * 2.0f, height - inset * 2.0f);
	}

	float UiScrollView::GetMaxScroll() const
	{
		return std::max(_contentHeight - Bounds.H, 0.0f);
	}

	UiSplitter::UiSplitter(UiOrientation orientation, Function<void()>&& onDragStart, Function<void(float)>&& onDrag, Function<void()>&& onDragEnd)
		: Orientation(orientation), OnDragStart(std::move(onDragStart)), OnDrag(std::move(onDrag)), OnDragEnd(std::move(onDragEnd)), _startPos(0.0f)
	{
		Tooltip = _("Drag to resize");
	}

	void UiSplitter::Draw(UiPainter& painter)
	{
		float amount = (_pressed ? 1.0f : _hoverAmount);
		if (amount <= 0.0f && !ShowLine) {
			return;
		}
		// A line along the middle of the edge
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		Rectf line = (Orientation == UiOrientation::Vertical
			? Rectf(std::round(Bounds.X + (Bounds.W - thickness) * 0.5f), Bounds.Y, thickness, Bounds.H)
			: Rectf(Bounds.X, std::round(Bounds.Y + (Bounds.H - thickness) * 0.5f), Bounds.W, thickness));
		if (ShowLine) {
			painter.FillRect(line, UiTheme::Separator, UiLayer::Content);
		}
		if (amount > 0.0f) {
			Colorf color = UiTheme::Accent;
			color.A *= amount;
			painter.FillRect(line, color, UiLayer::Overlay);
		}
	}

	bool UiSplitter::AcceptsPointer() const
	{
		return true;
	}

	bool UiSplitter::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		_startPos = (Orientation == UiOrientation::Vertical ? event.Pos.X : event.Pos.Y);
		if (OnDragStart) {
			OnDragStart();
		}
		return true;
	}

	void UiSplitter::OnPointerMove(const UiPointerEvent& event)
	{
		float pos = (Orientation == UiOrientation::Vertical ? event.Pos.X : event.Pos.Y);
		if (OnDrag) {
			OnDrag(pos - _startPos);
		}
	}

	void UiSplitter::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (OnDragEnd) {
			OnDragEnd();
		}
	}

	UiDragBar::UiDragBar(float spacing)
		: UiStack(UiOrientation::Horizontal, spacing)
	{
	}

	bool UiDragBar::AcceptsPointer() const
	{
		return true;
	}

	bool UiDragBar::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		if (event.Clicks >= 2 && OnDoubleClick) {
			OnDoubleClick();
			return false;
		}
		_startPos = event.Pos;
		if (OnDragStart) {
			OnDragStart(event.Pos);
		}
		return true;
	}

	void UiDragBar::OnPointerMove(const UiPointerEvent& event)
	{
		if (OnDrag) {
			OnDrag(event.Pos - _startPos, event.Pos);
		}
	}

	void UiDragBar::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (OnDragEnd) {
			OnDragEnd(event.Pos);
		}
	}

	UiForm::UiForm(float spacing, UiThickness padding)
		: UiStack(UiOrientation::Vertical, spacing, padding), MaxLabelWidth(120.0f), MaxLabelFraction(0.5f), _labelColumn(0.0f), _maxLabelColumn(0.0f)
	{
	}

	UiStack* UiForm::AddRow(StringView label, StringView tooltip)
	{
		return Add<UiFormRow>(*this, label, tooltip)->GetContent();
	}

	void UiForm::UpdateLabelColumn(const UiContext& context, float width)
	{
		float padding = context.Px(Padding.Left) + context.Px(Padding.Right);
		_maxLabelColumn = std::min(context.Px(MaxLabelWidth), std::floor((width - padding) * MaxLabelFraction));
		_labelColumn = 0.0f;
		for (auto& child : _children) {
			if (!child->IsShown()) {
				continue;
			}
			if (auto* row = runtime_cast<UiFormRow>(child.get())) {
				float labelWidth = std::ceil(context.MeasureText(row->GetLabel()->Text));
				if (labelWidth <= _maxLabelColumn) {
					_labelColumn = std::max(_labelColumn, labelWidth);
				}
			}
		}
	}

	Vector2f UiForm::Measure(const UiContext& context, Vector2f available)
	{
		UpdateLabelColumn(context, available.X);
		return UiStack::Measure(context, available);
	}

	void UiForm::Arrange(const UiContext& context, const Rectf& bounds)
	{
		UpdateLabelColumn(context, bounds.W);
		UiStack::Arrange(context, bounds);
	}

	UiFormRow::UiFormRow(UiForm& form, StringView label, StringView tooltip)
		: _form(form)
	{
		_label = Add<UiLabel>(label, UiTheme::TextDim);
		_label->Tooltip = tooltip;
		_content = Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
	}

	bool UiFormRow::IsStacked(const UiContext& context) const
	{
		return (std::ceil(context.MeasureText(_label->Text)) > _form.GetMaxLabelColumn());
	}

	Vector2f UiFormRow::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f labelSize = _label->Measure(context, available);
		if (IsStacked(context)) {
			// The label above what it's about
			Vector2f contentSize = _content->Measure(context, available);
			return Vector2f(std::max(labelSize.X, contentSize.X), labelSize.Y + contentSize.Y);
		}
		float column = _form.GetLabelColumn();
		float spacing = context.Px(UiTheme::Spacing);
		Vector2f contentSize = _content->Measure(context, Vector2f(std::max(available.X - column - spacing, 0.0f), available.Y));
		return Vector2f(column + spacing + contentSize.X, std::max(labelSize.Y, contentSize.Y));
	}

	void UiFormRow::Arrange(const UiContext& context, const Rectf& bounds)
	{
		Bounds = bounds;
		if (IsStacked(context)) {
			float labelHeight = _label->Measure(context, Vector2f(bounds.W, bounds.H)).Y;
			_label->Arrange(context, Rectf(bounds.X, bounds.Y, bounds.W, labelHeight));
			_content->Arrange(context, Rectf(bounds.X, bounds.Y + labelHeight, bounds.W, std::max(bounds.H - labelHeight, 0.0f)));
			return;
		}
		float column = _form.GetLabelColumn();
		float spacing = context.Px(UiTheme::Spacing);
		_label->Arrange(context, Rectf(bounds.X, bounds.Y, column, bounds.H));
		_content->Arrange(context, Rectf(bounds.X + column + spacing, bounds.Y, std::max(bounds.W - column - spacing, 0.0f), bounds.H));
	}

	UiTextBox::UiTextBox(StringView text)
		: Text(text), MaxLength(255), Multiline(false), _caret(0), _selectionStart(0), _scrollX(0.0f), _blinkTime(0.0f), _cancelled(false),
			_lastContext{ 1.0f }
	{
	}

	void UiTextBox::BeginEdit()
	{
		if (UiSurface* surface = GetSurface()) {
			surface->SetFocus(this);
		}
		_selectionStart = 0;
		_caret = std::int32_t(Text.size());
	}

	bool UiTextBox::ShowScreenKeyboard()
	{
		if (!theApplication().CanShowScreenKeyboard()) {
			// The keyboard of the editor then, it types into the box as long as it has the focus
			UiSurface* surface = GetSurface();
			if (surface == nullptr) {
				return false;
			}
			BeginEdit();
			UiOnScreenKeyboard::Show(*surface);
			return true;
		}
		BeginEdit();
		// A keyboard that types like a real one types into the box, one that collects the text gives it all at once
		return theApplication().ShowScreenKeyboard(Text, [this](StringView text) {
			Text = text;
			_caret = std::int32_t(Text.size());
			_selectionStart = _caret;
			NotifyChanged();
			if (UiSurface* surface = GetSurface()) {
				surface->SetFocus(nullptr);
			}
		});
	}

	void UiTextBox::GetLineStarts(StringView text, SmallVectorImpl<std::int32_t>& starts) const
	{
		starts.push_back(0);
		if (Multiline) {
			for (std::size_t i = 0; i < text.size(); i++) {
				if (text[i] == '\n') {
					starts.push_back(std::int32_t(i + 1));
				}
			}
		}
	}

	std::int32_t UiTextBox::GetLineOf(ArrayView<const std::int32_t> starts, std::int32_t offset)
	{
		std::int32_t line = 0;
		while (line + 1 < std::int32_t(starts.size()) && starts[line + 1] <= offset) {
			line++;
		}
		return line;
	}

	std::int32_t UiTextBox::GetLineEnd(ArrayView<const std::int32_t> starts, std::int32_t line) const
	{
		return (line + 1 < std::int32_t(starts.size()) ? starts[line + 1] - 1 : std::int32_t(Text.size()));
	}

	void UiTextBox::GetLineLayout(const UiContext& context, std::int32_t lineCount, float& lineHeight, float& top) const
	{
		if (!Multiline) {
			// A single line in the middle
			lineHeight = Bounds.H;
			top = Bounds.Y;
			return;
		}
		lineHeight = context.GetTextHeight();
		top = std::round(Bounds.Y + (Bounds.H - lineHeight * lineCount) * 0.5f);
	}

	Vector2f UiTextBox::Measure(const UiContext& context, Vector2f available)
	{
		_lastContext = context;
		float height = context.Px(UiTheme::RowHeight + 2.0f);
		if (Multiline) {
			// Grows with the lines of the text
			String shown = (_focused || !TextFunc ? Text : TextFunc());
			std::int32_t lines = 1;
			for (char c : shown) {
				if (c == '\n') {
					lines++;
				}
			}
			height = std::max(height, std::round(lines * context.GetTextHeight() + context.Px(4.0f)));
		}
		Vector2f size(context.Px(80.0f), height);
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void UiTextBox::Draw(UiPainter& painter)
	{
		_lastContext = UiContext{ painter.GetScale() };

		bool usable = IsUsable();
		painter.RoundRect(Bounds, Lerp(UiTheme::InputFill, UiTheme::ButtonPressed, usable && !_focused ? _hoverAmount * 0.6f : 0.0f));
		painter.RoundRectBorder(Bounds, _focused ? UiTheme::FocusRing : UiTheme::InputBorder);

		float padding = painter.Px(UiTheme::Padding);
		Rectf inner(Bounds.X + padding, Bounds.Y, Bounds.W - padding * 2.0f, Bounds.H);
		String shown = (_focused || !TextFunc ? Text : TextFunc());

		SmallVector<std::int32_t, 8> starts;
		GetLineStarts(shown, starts);
		float lineHeight, top;
		GetLineLayout(_lastContext, std::int32_t(starts.size()), lineHeight, top);
		auto lineRect = [&](std::int32_t line) {
			return Rectf(inner.X, top + line * lineHeight, inner.W, lineHeight);
		};

		painter.PushClip(inner);
		if (shown.empty() && !_focused) {
			painter.TextInRect(PlaceholderFunc ? StringView(PlaceholderFunc()) : StringView(Placeholder), lineRect(0), UiTheme::TextDisabled);
		} else {
			// While focused, what is shown is the text being edited
			std::int32_t caretLine = GetLineOf(starts, _caret);
			float caretX = (_focused ? painter.MeasureText(shown.slice(std::size_t(starts[caretLine]), std::size_t(_caret))) : 0.0f);
			if (_focused) {
				// The caret stays visible
				if (caretX - _scrollX > inner.W) {
					_scrollX = caretX - inner.W;
				} else if (caretX - _scrollX < 0.0f) {
					_scrollX = caretX;
				}
			} else {
				_scrollX = 0.0f;
			}

			float inset = (Multiline ? 0.0f : painter.Px(2.0f));
			if (_focused && _selectionStart != _caret) {
				std::int32_t from = std::min(_selectionStart, _caret), to = std::max(_selectionStart, _caret);
				for (std::int32_t line = GetLineOf(starts, from); line <= GetLineOf(starts, to); line++) {
					std::int32_t lineStart = starts[line];
					std::int32_t lineEnd = GetLineEnd(starts, line);
					float x1 = painter.MeasureText(shown.slice(std::size_t(lineStart), std::size_t(std::max(from, lineStart)))) - _scrollX;
					float x2 = painter.MeasureText(shown.slice(std::size_t(lineStart), std::size_t(std::min(to, lineEnd)))) - _scrollX;
					if (to > lineEnd) {
						// The line break is selected too
						x2 += painter.Px(3.0f);
					}
					Rectf rect = lineRect(line);
					painter.FillRect(Rectf(inner.X + x1, rect.Y + inset, x2 - x1, rect.H - inset * 2.0f), UiTheme::Selection, UiLayer::Content);
				}
			}

			Colorf color = (usable ? UiTheme::Text : UiTheme::TextDisabled);
			for (std::int32_t line = 0; line < std::int32_t(starts.size()); line++) {
				std::int32_t lineEnd = (line + 1 < std::int32_t(starts.size()) ? starts[line + 1] - 1 : std::int32_t(shown.size()));
				Rectf rect = lineRect(line);
				painter.TextInRect(shown.slice(std::size_t(starts[line]), std::size_t(lineEnd)), Rectf(rect.X - _scrollX, rect.Y, rect.W + _scrollX, rect.H), color);
			}

			if (_focused && std::fmod(_blinkTime, CaretBlinkPeriod) < CaretBlinkPeriod * 0.5f) {
				float caretWidth = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
				Rectf rect = lineRect(caretLine);
				float caretInset = (Multiline ? painter.Px(1.0f) : painter.Px(2.0f));
				painter.FillRect(Rectf(std::round(inner.X + caretX - _scrollX), rect.Y + caretInset, caretWidth, rect.H - caretInset * 2.0f),
					UiTheme::Accent, UiLayer::Overlay);
			}
		}
		painter.PopClip();
	}

	void UiTextBox::Update(float timeMult)
	{
		UiElement::Update(timeMult);
		_blinkTime += timeMult;
	}

	bool UiTextBox::AcceptsPointer() const
	{
		return true;
	}

	bool UiTextBox::OnPointerDown(const UiPointerEvent& event)
	{
		if (!IsUsable() || event.Button != MouseButton::Left) {
			return false;
		}
		if (!_focused) {
			BeginEdit();
			if (event.Clicks >= 2) {
				return true;
			}
		}
		_caret = GetOffsetAt(event.Pos);
		if ((event.Modifiers & KeyMod::Shift) == 0) {
			_selectionStart = _caret;
		}
		if (event.Clicks >= 2) {
			// A double click selects everything
			_selectionStart = 0;
			_caret = std::int32_t(Text.size());
		}
		_blinkTime = 0.0f;
		return true;
	}

	void UiTextBox::OnPointerMove(const UiPointerEvent& event)
	{
		if (_focused) {
			_caret = GetOffsetAt(event.Pos);
		}
	}

	bool UiTextBox::OnKeyPressed(const KeyboardEvent& event)
	{
		if (!_focused) {
			return false;
		}

		bool shift = (event.mod & KeyMod::Shift) != 0;
		bool ctrl = (event.mod & KeyMod::Ctrl) != 0;
		_blinkTime = 0.0f;

		auto moveCaret = [this, shift](std::int32_t offset) {
			_caret = std::clamp(offset, 0, std::int32_t(Text.size()));
			if (!shift) {
				_selectionStart = _caret;
			}
		};

		switch (event.sym) {
			case Keys::Left:
				if (!shift && _selectionStart != _caret) {
					moveCaret(std::min(_selectionStart, _caret));
				} else if (_caret > 0) {
					moveCaret(std::int32_t(Utf8::PrevChar(Text, std::size_t(_caret)).second()));
				}
				return true;
			case Keys::Right:
				if (!shift && _selectionStart != _caret) {
					moveCaret(std::max(_selectionStart, _caret));
				} else if (_caret < std::int32_t(Text.size())) {
					moveCaret(std::int32_t(Utf8::NextChar(Text, std::size_t(_caret)).second()));
				}
				return true;
			case Keys::Up:
			case Keys::Down: {
				if (!Multiline) {
					return true;
				}
				// To the same place on the line above or below
				SmallVector<std::int32_t, 8> starts;
				GetLineStarts(Text, starts);
				std::int32_t line = GetLineOf(starts, _caret);
				std::int32_t target = line + (event.sym == Keys::Up ? -1 : 1);
				if (target < 0) {
					moveCaret(0);
				} else if (target >= std::int32_t(starts.size())) {
					moveCaret(std::int32_t(Text.size()));
				} else {
					float x = _lastContext.MeasureText(Text.slice(std::size_t(starts[line]), std::size_t(_caret)));
					moveCaret(GetOffsetInLine(target, x));
				}
				return true;
			}
			case Keys::Home:
			case Keys::End: {
				SmallVector<std::int32_t, 8> starts;
				GetLineStarts(Text, starts);
				std::int32_t line = GetLineOf(starts, _caret);
				if (event.sym == Keys::Home) {
					moveCaret(ctrl ? 0 : starts[line]);
				} else {
					moveCaret(ctrl ? std::int32_t(Text.size()) : GetLineEnd(starts, line));
				}
				return true;
			}
			case Keys::Backspace:
				if (!DeleteSelection() && _caret > 0) {
					std::int32_t from = std::int32_t(Utf8::PrevChar(Text, std::size_t(_caret)).second());
					Text = Text.prefix(std::size_t(from)) + Text.exceptPrefix(std::size_t(_caret));
					_caret = _selectionStart = from;
				}
				NotifyChanged();
				return true;
			case Keys::Delete:
				if (!DeleteSelection() && _caret < std::int32_t(Text.size())) {
					std::int32_t to = std::int32_t(Utf8::NextChar(Text, std::size_t(_caret)).second());
					Text = Text.prefix(std::size_t(_caret)) + Text.exceptPrefix(std::size_t(to));
				}
				NotifyChanged();
				return true;
			case Keys::Return:
			case Keys::NumPadEnter:
				if (Multiline && !ctrl) {
					DeleteSelection();
					Insert(StringView("\n", 1));
					NotifyChanged();
					return true;
				}
				if (UiSurface* surface = GetSurface()) {
					surface->SetFocus(nullptr);
				}
				// Passed on, so a dialog is confirmed by the same key, unless the text has more lines and is one of more
				return Multiline;
			case Keys::Escape:
				_cancelled = true;
				if (UiSurface* surface = GetSurface()) {
					surface->SetFocus(nullptr);
				}
				return false;
			case Keys::A:
				if (ctrl) {
					_selectionStart = 0;
					_caret = std::int32_t(Text.size());
					return true;
				}
				break;
			case Keys::C:
			case Keys::X:
				if (ctrl && _selectionStart != _caret) {
					std::int32_t from = std::min(_selectionStart, _caret), to = std::max(_selectionStart, _caret);
					theApplication().GetInputManager().setClipboardText(Text.slice(std::size_t(from), std::size_t(to)));
					if (event.sym == Keys::X) {
						DeleteSelection();
						NotifyChanged();
					}
					return true;
				}
				break;
			case Keys::V:
				if (ctrl) {
					String clipboard = theApplication().GetInputManager().getClipboardText();
					if (!Multiline) {
						// A single line only
						clipboard = clipboard.prefix(clipboard.findOr('\n', clipboard.end()).begin());
					}
					DeleteSelection();
					Insert(clipboard);
					NotifyChanged();
					return true;
				}
				break;
			default:
				break;
		}

		// Everything else typed goes here too, so it doesn't trigger hotkeys
		return (event.sym != Keys::Tab && event.sym != Keys::F1 && event.sym != Keys::F5);
	}

	bool UiTextBox::OnTextInput(const TextInputEvent& event)
	{
		if (!_focused) {
			return false;
		}
		DeleteSelection();
		Insert(StringView(event.text, std::size_t(event.length)));
		NotifyChanged();
		return true;
	}

	bool UiTextBox::IsFocusable() const
	{
		return true;
	}

	void UiTextBox::OnFocusChanged(bool focused)
	{
		if (focused) {
			if (TextFunc) {
				Text = TextFunc();
			}
			_cancelled = false;
			_blinkTime = 0.0f;
			_selectionStart = 0;
			_caret = std::int32_t(Text.size());
		} else if (!_cancelled) {
			if (OnCommit) {
				OnCommit(Text);
			}
		} else if (TextFunc) {
			Text = TextFunc();
		}
		_cancelled = false;
	}

	std::int32_t UiTextBox::GetOffsetAt(Vector2f pos) const
	{
		SmallVector<std::int32_t, 8> starts;
		GetLineStarts(Text, starts);
		std::int32_t line = 0;
		if (Multiline) {
			float lineHeight, top;
			GetLineLayout(_lastContext, std::int32_t(starts.size()), lineHeight, top);
			line = std::clamp(std::int32_t(std::floor((pos.Y - top) / lineHeight)), 0, std::int32_t(starts.size()) - 1);
		}
		float padding = _lastContext.Px(UiTheme::Padding);
		return GetOffsetInLine(line, pos.X - (Bounds.X + padding) + _scrollX);
	}

	std::int32_t UiTextBox::GetOffsetInLine(std::int32_t line, float x) const
	{
		SmallVector<std::int32_t, 8> starts;
		GetLineStarts(Text, starts);
		line = std::clamp(line, 0, std::int32_t(starts.size()) - 1);
		std::int32_t start = starts[line];
		std::int32_t end = GetLineEnd(starts, line);
		std::int32_t best = start;
		float bestDistance = std::abs(x);
		std::size_t offset = std::size_t(start);
		while (offset < std::size_t(end)) {
			offset = Utf8::NextChar(Text, offset).second();
			float width = _lastContext.MeasureText(Text.slice(std::size_t(start), offset));
			float distance = std::abs(x - width);
			if (distance < bestDistance) {
				bestDistance = distance;
				best = std::int32_t(offset);
			}
		}
		return best;
	}
	bool UiTextBox::DeleteSelection()
	{
		if (_selectionStart == _caret) {
			return false;
		}
		std::int32_t from = std::min(_selectionStart, _caret), to = std::max(_selectionStart, _caret);
		Text = Text.prefix(std::size_t(from)) + Text.exceptPrefix(std::size_t(to));
		_caret = _selectionStart = from;
		return true;
	}

	void UiTextBox::Insert(StringView text)
	{
		std::size_t room = (MaxLength > std::int32_t(Text.size()) ? std::size_t(MaxLength) - Text.size() : 0);
		if (text.size() > room) {
			// Not in the middle of a character
			std::size_t length = 0;
			while (length < text.size()) {
				std::size_t next = Utf8::NextChar(text, length).second();
				if (next > room) {
					break;
				}
				length = next;
			}
			text = text.prefix(length);
		}
		if (text.empty()) {
			return;
		}
		Text = Text.prefix(std::size_t(_caret)) + text + Text.exceptPrefix(std::size_t(_caret));
		_caret += std::int32_t(text.size());
		_selectionStart = _caret;
	}

	void UiTextBox::NotifyChanged()
	{
		if (OnChanged) {
			OnChanged(Text);
		}
	}

	UiNumberBox::UiNumberBox(Function<float()>&& getter, Function<void(float, bool)>&& setter, float minValue, float maxValue, float step, std::int32_t decimals)
		: Getter(std::move(getter)), Setter(std::move(setter)), MinValue(minValue), MaxValue(maxValue), Step(step), Decimals(decimals),
			_dragStartX(0.0f), _dragStartValue(0.0f), _dragging(false)
	{
		TextFunc = [this]() {
			return FormatValue(Getter ? Getter() : 0.0f);
		};
		OnCommit = [this](StringView text) {
			String value = text;
			char* end = nullptr;
			float parsed = std::strtof(value.data(), &end);
			if (end != value.data() && Setter) {
				Setter(std::clamp(parsed, MinValue, MaxValue), true);
			}
		};
		MaxLength = 16;
	}

	Rectf UiNumberBox::GetSpinRect() const
	{
		float width = _lastContext.Px(9.0f);
		return Rectf(Bounds.X + Bounds.W - width, Bounds.Y, width, Bounds.H);
	}

	void UiNumberBox::StepValue(float steps)
	{
		if (Getter && Setter) {
			Setter(std::clamp(Getter() + steps * Step, MinValue, MaxValue), true);
		}
	}

	void UiNumberBox::Draw(UiPainter& painter)
	{
		UiTextBox::Draw(painter);
		if (!_focused) {
			// Arrows to step the value, it can be dragged as well
			Rectf spin = GetSpinRect();
			bool overSpin = (_hovered && GetSurface() != nullptr && spin.Contains(GetSurface()->GetPointerPos()));
			Colorf color = Colorf(1.0f, 1.0f, 1.0f, (IsUsable() ? (overSpin ? 0.75f : 0.2f + 0.25f * _hoverAmount) : 0.1f));
			float pixel = std::max(1.0f, std::round(painter.GetScale()));
			float centerX = std::round(spin.X + spin.W * 0.5f - pixel * 0.5f);
			float middle = std::round(Bounds.Y + Bounds.H * 0.5f);
			for (std::int32_t i = 0; i < 3; i++) {
				float width = pixel * float(i * 2 + 1);
				// Up above the middle, down below it
				painter.FillRect(Rectf(centerX - pixel * i, middle - pixel * float(4 - i), width, pixel), color, UiLayer::Content);
				painter.FillRect(Rectf(centerX - pixel * i, middle + pixel * float(3 - i), width, pixel), color, UiLayer::Content);
			}
		}
	}

	bool UiNumberBox::OnWheel(const UiPointerEvent& event)
	{
		// Over the arrows only, a panel full of numbers scrolls over the rest of them
		if (!IsUsable() || (!_focused && !GetSpinRect().Contains(event.Pos)) || event.Wheel.Y == 0.0f) {
			return false;
		}
		float steps = (event.Wheel.Y > 0.0f ? 1.0f : -1.0f) * ((event.Modifiers & KeyMod::Shift) != 0 ? 10.0f : 1.0f);
		if (_focused) {
			// The edited text is replaced by the new value
			if (UiSurface* surface = GetSurface()) {
				surface->SetFocus(nullptr);
			}
		}
		StepValue(steps);
		return true;
	}

	bool UiNumberBox::OnPointerDown(const UiPointerEvent& event)
	{
		if (_focused) {
			return UiTextBox::OnPointerDown(event);
		}
		if (!IsUsable() || event.Button != MouseButton::Left) {
			return false;
		}
		Rectf spin = GetSpinRect();
		if (spin.Contains(event.Pos)) {
			// The upper half steps up, the lower one down
			StepValue(event.Pos.Y < spin.Y + spin.H * 0.5f ? 1.0f : -1.0f);
			return false;
		}
		_dragStartX = event.Pos.X;
		_dragStartValue = (Getter ? Getter() : 0.0f);
		_dragging = false;
		return true;
	}

	void UiNumberBox::OnPointerMove(const UiPointerEvent& event)
	{
		if (_focused) {
			UiTextBox::OnPointerMove(event);
			return;
		}

		float delta = event.Pos.X - _dragStartX;
		float unit = std::max(_lastContext.Px(3.0f), 1.0f);
		if (!_dragging && std::abs(delta) > unit) {
			_dragging = true;
		}
		if (_dragging && Setter) {
			float value = _dragStartValue + std::round(delta / unit) * Step;
			Setter(std::clamp(value, MinValue, MaxValue), false);
		}
	}

	void UiNumberBox::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (_focused) {
			return;
		}
		if (_dragging) {
			_dragging = false;
			if (Setter && Getter) {
				Setter(Getter(), true);
			}
		} else if (inside) {
			// A click without dragging types the value
			BeginEdit();
		}
	}

	String UiNumberBox::FormatValue(float value) const
	{
		char buffer[32];
		std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%.*f", Decimals, value);
		return String(buffer, std::size_t(std::max(length, 0)));
	}
}

#endif
