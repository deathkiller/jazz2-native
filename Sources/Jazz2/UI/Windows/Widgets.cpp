#include "Widgets.h"

#if defined(WITH_UI_WINDOWS)

#include "Keyboard.h"
#include "Surface.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Input/IInputManager.h"

#include <Containers/StringConcatenable.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace Death;

namespace Jazz2::UI::Windows
{
	Colorf LerpColor(const Colorf& a, const Colorf& b, float t)
	{
		return Colorf(a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t, a.A + (b.A - a.A) * t);
	}

	namespace
	{
		// Frames the caret is shown and hidden for
		constexpr float CaretBlinkPeriod = 60.0f;

		// Breaks a text into lines at spaces, each as long as fits the width, a word wider than it gets a line of its own
		template<class TMeasurer>
		void BreakLines(const TMeasurer& measurer, StringView text, float width, SmallVectorImpl<StringView>& lines)
		{
			lines.clear();
			float spaceWidth = measurer.MeasureText(StringView(" ", 1));
			for (StringView paragraph : text.split('\n')) {
				const char* lineBegin = paragraph.begin();
				const char* lineEnd = lineBegin;
				// Width of the line so far, by its words and spaces measured apart, each rounded up, so it's never less
				// than the line measured as a whole
				float lineWidth = 0.0f;
				const char* p = lineBegin;
				while (p < paragraph.end()) {
					const char* wordEnd = p;
					while (wordEnd < paragraph.end() && *wordEnd != ' ') {
						wordEnd++;
					}
					float wordWidth = measurer.MeasureText(StringView(p, std::size_t(wordEnd - p)));
					if (lineEnd != lineBegin) {
						float candidate = lineWidth + spaceWidth * float(p - lineEnd) + wordWidth;
						if (candidate > width) {
							// Measured as a whole only near the width, where the rounding of the parts may matter
							candidate = measurer.MeasureText(StringView(lineBegin, std::size_t(wordEnd - lineBegin)));
						}
						if (candidate > width) {
							lines.push_back(StringView(lineBegin, std::size_t(lineEnd - lineBegin)));
							lineBegin = p;
							lineWidth = wordWidth;
						} else {
							lineWidth = candidate;
						}
					} else {
						// The first word, after any spaces the line starts with
						lineWidth = spaceWidth * float(p - lineBegin) + wordWidth;
					}
					lineEnd = wordEnd;
					p = (wordEnd < paragraph.end() ? wordEnd + 1 : wordEnd);
				}
				lines.push_back(StringView(lineBegin, std::size_t(lineEnd - lineBegin)));
			}
		}
	}

	Label::Label(StringView text, const Colorf& color, Alignment align)
		: Text(text), Color(color), Align(align)
	{
	}

	StringView Label::GetText()
	{
		if (!TextFunc) {
			return Text;
		}
		// The text is measured and drawn in the same frame, it's made only once for both
		std::uint32_t layout = GetLayoutFrame();
		if (layout == 0 || layout != _frameTextLayout) {
			_frameText = TextFunc();
			_frameTextLayout = layout;
		}
		return _frameText;
	}

	Vector2f Label::Measure(const Context& context, Vector2f available)
	{
		Vector2f size(context.MeasureText(GetText()) + context.Px(1.0f), context.Px(Theme::RowHeight));
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	bool Label::MeasureDependsOnAvailable() const
	{
		return false;
	}

	void Label::Draw(Painter& painter)
	{
		Colorf color = (IsUsable() ? Color : Theme::TextDisabled);
		painter.PushClip(Bounds);
		painter.TextInRect(GetText(), Bounds, color, Align);
		painter.PopClip();
	}

	WrappedLabel::WrappedLabel(StringView text, const Colorf& color)
		: Label(text, color)
	{
	}

	template<class TMeasurer>
	const WrappedLabel::WrappedLines& WrappedLabel::GetLines(WrappedLines& cache, const TMeasurer& measurer, float width)
	{
		StringView text = GetText();
		float textHeight = measurer.GetTextHeight();
		if (cache.Width == width && cache.TextHeight == textHeight && cache.FontVersion == Theme::FontVersion && cache.Text == text) {
			return cache;
		}
		cache.Text = text;
		cache.Width = width;
		cache.TextHeight = textHeight;
		cache.FontVersion = Theme::FontVersion;
		cache.Lines.clear();
		cache.Widest = 0.0f;
		SmallVector<StringView, 8> lines;
		BreakLines(measurer, cache.Text, width, lines);
		for (StringView line : lines) {
			std::uint32_t offset = (line.empty() ? 0 : std::uint32_t(line.data() - cache.Text.data()));
			cache.Lines.push_back({ offset, std::uint32_t(line.size()) });
			cache.Widest = std::max(cache.Widest, measurer.MeasureText(line));
		}
		return cache;
	}

	Vector2f WrappedLabel::Measure(const Context& context, Vector2f available)
	{
		float width = (Width >= 0.0f ? context.Px(Width) : available.X);
		const WrappedLines& wrapped = GetLines(_measured, context, width);
		float lineHeight = context.GetTextHeight() + context.Px(2.0f);
		return Vector2f(std::min(wrapped.Widest + context.Px(1.0f), width), lineHeight * float(wrapped.Lines.size()) + context.Px(4.0f));
	}

	bool WrappedLabel::MeasureDependsOnAvailable() const
	{
		return true;
	}

	void WrappedLabel::Draw(Painter& painter)
	{
		const WrappedLines& wrapped = GetLines(_drawn, painter, Bounds.W);
		float lineHeight = painter.GetTextHeight() + painter.Px(2.0f);
		float y = Bounds.Y + painter.Px(2.0f);
		Colorf color = (IsUsable() ? Color : Theme::TextDisabled);
		painter.PushClip(Bounds);
		for (const auto& line : wrapped.Lines) {
			painter.Text(wrapped.Text.slice(line.first, line.first + line.second), Vector2f(Bounds.X, y), color);
			y += lineHeight;
		}
		painter.PopClip();
	}

	Button::Button(StringView text, IconId icon, Function<void()>&& onClick)
		: Text(text), Icon(icon), Style(ButtonStyle::Normal), OnClick(Death::move(onClick)), IconColor(Theme::IconColor), MinWidth(0.0f),
			TextAlign(Alignment::Center), TextColor(0.0f, 0.0f, 0.0f, 0.0f), _textHidden(false), _neededWidth(0.0f)
	{
	}

	StringView Button::GetTooltip(Vector2f pos) const
	{
		StringView tooltip = Element::GetTooltip(pos);
		// What the button says, when only its icon is shown
		return (tooltip.empty() && _textHidden ? StringView(Text) : tooltip);
	}

	bool Button::IsChecked()
	{
		return (IsCheckedFunc && IsCheckedFunc());
	}

	bool Button::MeasureDependsOnAvailable() const
	{
		return false;
	}

	template<class TMeasurer>
	float Button::GetTextWidth(const TMeasurer& measurer)
	{
		float textHeight = measurer.GetTextHeight();
		if (_measuredTextHeight != textHeight || _measuredFontVersion != Theme::FontVersion || _measuredText != Text) {
			_measuredText = Text;
			_measuredTextHeight = textHeight;
			_measuredFontVersion = Theme::FontVersion;
			_measuredTextWidth = measurer.MeasureText(Text);
		}
		return _measuredTextWidth;
	}

	bool Button::YieldsToScroll() const
	{
		return true;
	}

	bool Button::IsTabStop() const
	{
		return true;
	}

	bool Button::OnKeyPressed(const KeyboardEvent& event)
	{
		if ((event.Sym == Keys::Return || event.Sym == Keys::NumPadEnter || event.Sym == Keys::Space) && (event.Mod & (KeyMod::Ctrl | KeyMod::Alt)) == 0) {
			if (IsUsable() && OnClick) {
				// Nothing of the button is touched after, as after a click
				OnClick();
			}
			return true;
		}
		return false;
	}

	Vector2f Button::Measure(const Context& context, Vector2f available)
	{
		// A button of only an icon is a little larger, the icon would look crowded otherwise
		float height = context.Px(Theme::ButtonHeight + (Text.empty() ? 2.0f : 0.0f));
		float width;
		if (Text.empty()) {
			width = height;
		} else {
			float padding = context.Px(Theme::Padding) * 1.5f;
			width = padding * 2.0f + GetTextWidth(context);
			if (Icon != StockIcon::None) {
				width += context.Px(Theme::IconSize) + context.Px(Theme::Spacing + IconGap);
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

	void Button::Draw(Painter& painter)
	{
		bool usable = IsUsable();
		bool checked = IsChecked();
		float hover = (usable ? _hoverAmount : 0.0f);

		Colorf fill;
		switch (Style) {
			default:
			case ButtonStyle::Normal:
				fill = (checked ? LerpColor(Theme::ButtonChecked, Theme::ButtonCheckedHover, hover) : LerpColor(Theme::ButtonFill, Theme::ButtonHover, hover));
				break;
			case ButtonStyle::Flat:
				fill = (checked ? LerpColor(Theme::ButtonChecked, Theme::ButtonCheckedHover, hover) : Colorf(Theme::ButtonHover.R, Theme::ButtonHover.G, Theme::ButtonHover.B, Theme::ButtonHover.A * hover));
				break;
			case ButtonStyle::Accent:
				fill = LerpColor(Theme::ButtonChecked, Theme::ButtonCheckedHover, hover);
				break;
		}
		if (_pressed && usable) {
			fill = (checked || Style == ButtonStyle::Accent ? Theme::ButtonChecked : Theme::ButtonPressed);
		}
		if (fill.A > 0.0f) {
			painter.RoundRect(Bounds, fill);
		}
		if (checked) {
			painter.RoundRectBorder(Bounds, Theme::Accent);
		} else {
			float outline = (_pressed && usable ? 1.0f : hover);
			Colorf border;
			if (Style == ButtonStyle::Accent) {
				// Halfway between the fill and the accent, a little more while pointed at
				border = LerpColor(LerpColor(Theme::ButtonChecked, Theme::AccentDim, 0.5f), LerpColor(Theme::ButtonCheckedHover, Theme::Accent, 0.5f), outline);
			} else {
				border = (Style == ButtonStyle::Normal ? Theme::PanelBorderDim
					: Colorf(Theme::ButtonOutline.R, Theme::ButtonOutline.G, Theme::ButtonOutline.B, 0.0f));
				border = LerpColor(border, Theme::ButtonOutline, outline);
			}
			if (border.A > 0.0f) {
				painter.RoundRectBorder(Bounds, border);
			}
		}
		if (_focused) {
			// Reached by Tab
			painter.FocusRing(Bounds);
		}

		Colorf iconColor = (usable ? IconColor : Theme::IconDisabled);
		Colorf textColor = (usable ? (checked ? Theme::TextOnAccent : (TextColor.A > 0.0f ? TextColor : Theme::Text)) : Theme::TextDisabled);
		// Pressed content moves down a little, like a key
		float offset = (_pressed && usable ? std::max(1.0f, std::round(painter.GetScale() * 0.5f)) : 0.0f);

		float padding = painter.Px(Theme::Padding) * 1.5f;
		float contentWidth = (Text.empty() ? 0.0f : GetTextWidth(painter));
		if (Icon != StockIcon::None) {
			contentWidth += painter.Px(Theme::IconSize) + painter.Px(Theme::Spacing + IconGap);
		}
		// Squeezed narrower than the icon and the text take, the icon alone says what it is better than a cut text
		_neededWidth = (Text.empty() ? 0.0f : contentWidth + padding * 2.0f);
		_textHidden = (!Text.empty() && Icon != StockIcon::None && (IconOnlyFunc ? IconOnlyFunc() : IsTextCut()));
		if (Text.empty() || _textHidden) {
			painter.Icon(Icon, Rectf(Bounds.X, Bounds.Y + offset, Bounds.W, Bounds.H), iconColor);
			return;
		}

		float x = Bounds.X + padding;
		// Centered if the button is wider than its content, unless it's in a list, or at the right as numbers in a table
		Alignment horizontal = (TextAlign & Alignment::HorizontalMask);
		if (horizontal == Alignment::Right) {
			x = std::max(x, std::round(Bounds.X + Bounds.W - padding - contentWidth));
		} else if (horizontal != Alignment::Left) {
			x = std::max(x, std::round(Bounds.X + (Bounds.W - contentWidth) * 0.5f));
		}
		if (Icon != StockIcon::None) {
			float iconSize = painter.Px(Theme::IconSize);
			painter.Icon(Icon, Rectf(x, Bounds.Y + offset, iconSize, Bounds.H), iconColor);
			x += iconSize + painter.Px(Theme::Spacing + IconGap);
		}
		painter.TextInRect(Text, Rectf(x, Bounds.Y + offset, Bounds.X + Bounds.W - x, Bounds.H), textColor, Alignment::Left);
	}

	bool Button::AcceptsPointer() const
	{
		return true;
	}

	bool Button::OnPointerDown(const PointerEvent& event)
	{
		return (event.Button == MouseButton::Left && IsUsable());
	}

	void Button::OnPointerUp(const PointerEvent& event, bool inside)
	{
		if (inside && event.Button == MouseButton::Left && IsUsable() && OnClick) {
			OnClick();
		}
	}

	CheckBox::CheckBox(StringView text, Function<bool()>&& getter, Function<void(bool)>&& setter)
		: Text(text), Getter(Death::move(getter)), Setter(Death::move(setter))
	{
	}

	Vector2f CheckBox::Measure(const Context& context, Vector2f available)
	{
		float box = context.Px(10.0f);
		Vector2f size(box + context.Px(Theme::Spacing * 2.0f) + context.MeasureText(Text), context.Px(Theme::RowHeight));
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void CheckBox::Draw(Painter& painter)
	{
		bool usable = IsUsable();
		bool checked = (Getter && Getter());
		float box = painter.Px(10.0f);
		Rectf boxRect(Bounds.X, std::round(Bounds.Y + (Bounds.H - box) * 0.5f), box, box);
		painter.RoundRect(boxRect, LerpColor(Theme::InputFill, Theme::ButtonHover, usable ? _hoverAmount : 0.0f));
		painter.RoundRectBorder(boxRect, checked ? Theme::Accent : Theme::InputBorder);
		if (checked) {
			float inset = std::max(2.0f, std::round(box * 0.25f));
			painter.RoundRect(Rectf(boxRect.X + inset, boxRect.Y + inset, boxRect.W - inset * 2.0f, boxRect.H - inset * 2.0f),
				usable ? Theme::Accent : Theme::AccentDim, PaintLayer::Content);
		}
		float x = boxRect.X + box + painter.Px(Theme::Spacing * 2.0f);
		painter.TextInRect(Text, Rectf(x, Bounds.Y, Bounds.X + Bounds.W - x, Bounds.H), usable ? Theme::Text : Theme::TextDisabled);
		if (_focused) {
			painter.FocusRing(boxRect);
		}
	}

	bool CheckBox::MeasureDependsOnAvailable() const
	{
		return false;
	}

	bool CheckBox::YieldsToScroll() const
	{
		return true;
	}

	bool CheckBox::IsTabStop() const
	{
		return true;
	}

	bool CheckBox::OnKeyPressed(const KeyboardEvent& event)
	{
		if (event.Sym == Keys::Space && (event.Mod & (KeyMod::Ctrl | KeyMod::Alt)) == 0) {
			if (IsUsable() && Setter) {
				Setter(!(Getter && Getter()));
			}
			return true;
		}
		return false;
	}

	bool CheckBox::AcceptsPointer() const
	{
		return true;
	}

	bool CheckBox::OnPointerDown(const PointerEvent& event)
	{
		return (event.Button == MouseButton::Left && IsUsable());
	}

	void CheckBox::OnPointerUp(const PointerEvent& event, bool inside)
	{
		if (inside && IsUsable() && Setter) {
			Setter(!(Getter && Getter()));
		}
	}

	Separator::Separator(Windows::Orientation orientation)
		: Orientation(orientation)
	{
	}

	Vector2f Separator::Measure(const Context& context, Vector2f available)
	{
		float thickness = context.Px(Theme::Spacing * 2.0f) + std::max(1.0f, std::round(context.Scale * 0.5f));
		return (Orientation == Orientation::Horizontal ? Vector2f(0.0f, thickness) : Vector2f(thickness, 0.0f));
	}

	bool Separator::MeasureDependsOnAvailable() const
	{
		return false;
	}

	void Separator::Draw(Painter& painter)
	{
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
		if (Orientation == Orientation::Horizontal) {
			painter.FillRect(Rectf(Bounds.X, std::round(Bounds.Y + (Bounds.H - thickness) * 0.5f), Bounds.W, thickness), Theme::Separator);
		} else {
			float inset = painter.Px(2.0f);
			painter.FillRect(Rectf(std::round(Bounds.X + (Bounds.W - thickness) * 0.5f), Bounds.Y + inset, thickness, Bounds.H - inset * 2.0f), Theme::Separator);
		}
	}

	ScrollView::ScrollView()
		: _scroll(0.0f), _targetScroll(0.0f), _contentHeight(0.0f), _dragOffset(0.0f), _draggingThumb(false)
	{
	}

	Element* ScrollView::GetContent() const
	{
		return (!_children.empty() ? _children[0].get() : nullptr);
	}

	void ScrollView::ScrollIntoView(float top, float bottom)
	{
		_velocity = 0.0f;
		if (top < _targetScroll) {
			_targetScroll = top;
		} else if (bottom > _targetScroll + Bounds.H) {
			_targetScroll = bottom - Bounds.H;
		}
		_targetScroll = std::clamp(_targetScroll, 0.0f, GetMaxScroll());
	}

	void ScrollView::ScrollTo(float scroll, bool immediate)
	{
		_velocity = 0.0f;
		_targetScroll = std::clamp(scroll, 0.0f, GetMaxScroll());
		if (immediate) {
			_scroll = _targetScroll;
		}
	}

	void ScrollView::Fling(float velocity)
	{
		_velocity = (Theme::ReducedMotion ? 0.0f : velocity);
		_targetScroll = _scroll;
	}

	Vector2f ScrollView::Measure(const Context& context, Vector2f available)
	{
		Vector2f size;
		Vector2f fixedSize = GetFixedSize(context);
		if (Element* content = GetContent()) {
			size = content->MeasureCached(context, Vector2f(available.X, 1.0e6f));
			// Kept for Arrange() in this frame, the content isn't measured in the same width again
			_measuredWidth = available.X;
			_measuredHeight = size.Y;
			_measuredFrame = GetLayoutFrame();
			float maxHeight = (fixedSize.Y >= 0.0f ? fixedSize.Y : available.Y);
			if (size.Y > maxHeight && !OverlayBar) {
				// The scroll bar takes its own space next to the content, which is as narrow as the rest then, and never
				// more than all the space there is
				float barWidth = context.Px(Theme::ScrollBarWidth);
				if (available.X < 1.0e5f) {
					Vector2f narrowSize = content->MeasureCached(context, Vector2f(std::max(available.X - barWidth, 0.0f), 1.0e6f));
					size = Vector2f(std::min(narrowSize.X + barWidth, available.X), narrowSize.Y);
				} else {
					size.X += barWidth;
				}
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

	void ScrollView::Arrange(const Context& context, const Rectf& bounds)
	{
		// Scrolled to the end as it was laid out last, at once the first time, see FollowEnd
		bool followEnd = (FollowEnd && _velocity == 0.0f && !_draggingThumb && _targetScroll >= GetMaxScroll() - 1.0f);
		bool firstLayout = (Bounds.H <= 0.0f);
		Bounds = bounds;
		Element* content = GetContent();
		if (content == nullptr || !content->IsShown()) {
			_contentHeight = 0.0f;
			return;
		}

		float barWidth = (OverlayBar ? 0.0f : context.Px(Theme::ScrollBarWidth));
		std::uint32_t layoutFrame = GetLayoutFrame();
		_contentHeight = (layoutFrame != 0 && _measuredFrame == layoutFrame && _measuredWidth == bounds.W
			? _measuredHeight : content->MeasureCached(context, Vector2f(bounds.W, 1.0e6f)).Y);
		if (_contentHeight > bounds.H && barWidth > 0.0f) {
			// The scroll bar takes its own space
			_contentHeight = content->MeasureCached(context, Vector2f(bounds.W - barWidth, 1.0e6f)).Y;
		}

		if (followEnd) {
			_targetScroll = GetMaxScroll();
			if (firstLayout || Theme::ReducedMotion) {
				_scroll = _targetScroll;
			}
		}
		_targetScroll = std::clamp(_targetScroll, 0.0f, GetMaxScroll());
		_scroll = std::clamp(_scroll, 0.0f, GetMaxScroll());
		float width = (_contentHeight > bounds.H ? bounds.W - barWidth : bounds.W);
		content->Arrange(context, Rectf(bounds.X, std::round(bounds.Y - _scroll), width, std::max(_contentHeight, bounds.H)));
	}

	void ScrollView::Draw(Painter& painter)
	{
		painter.PushClip(Bounds);
		Element::Draw(painter);
		painter.PopClip();

		if (_contentHeight > Bounds.H) {
			Rectf thumb = GetThumbRect();
			if (OverlayBar) {
				// Only a thin mark of where the content is scrolled to, over its edge
				float width = std::max(1.0f, std::round(thumb.W * 0.5f));
				thumb = Rectf(Bounds.X + Bounds.W - width - std::max(1.0f, painter.Px(1.0f)) + std::round(painter.Px(OverlayBarOffset)), thumb.Y, width, thumb.H);
			}
			painter.RoundRect(thumb, _draggingThumb ? Theme::Accent : Colorf(1.0f, 1.0f, 1.0f, 0.22f + 0.15f * _hoverAmount), PaintLayer::Overlay);
		}
	}

	void ScrollView::Update(float timeMult)
	{
		Element::Update(timeMult);

		if (_velocity != 0.0f) {
			// Flicked by a finger, it slows down by itself and stops at the ends
			_targetScroll = _scroll = std::clamp(_scroll - _velocity * timeMult, 0.0f, GetMaxScroll());
			_velocity *= std::pow(0.93f, timeMult);
			if (std::abs(_velocity) < 0.2f || _scroll <= 0.0f || _scroll >= GetMaxScroll()) {
				_velocity = 0.0f;
			}
		} else if (_scroll != _targetScroll) {
			float step = (Theme::ReducedMotion ? 1.0f : 1.0f - std::pow(0.65f, timeMult));
			_scroll += (_targetScroll - _scroll) * step;
			if (std::abs(_targetScroll - _scroll) < 0.5f) {
				_scroll = _targetScroll;
			}
		}
	}

	Element* ScrollView::HitTest(Vector2f pos)
	{
		if (!IsShown() || !Bounds.Contains(pos)) {
			return nullptr;
		}
		if (_contentHeight > Bounds.H && !OverlayBar && pos.X >= Bounds.X + Bounds.W - GetThumbZoneWidth()) {
			return this;
		}
		if (Element* content = GetContent()) {
			if (Element* hit = content->HitTest(pos)) {
				return hit;
			}
		}
		return this;
	}

	bool ScrollView::AcceptsPointer() const
	{
		return true;
	}

	bool ScrollView::OnPointerDown(const PointerEvent& event)
	{
		if (_contentHeight <= Bounds.H || OverlayBar || event.Button != MouseButton::Left) {
			return false;
		}
		Rectf thumb = GetThumbRect();
		if (event.Pos.X < Bounds.X + Bounds.W - GetThumbZoneWidth()) {
			return false;
		}
		if (event.Pos.Y < thumb.Y || event.Pos.Y >= thumb.Y + thumb.H) {
			// A click on the track moves the thumb there
			float ratio = std::clamp((event.Pos.Y - Bounds.Y - thumb.H * 0.5f) / std::max(Bounds.H - thumb.H, 1.0f), 0.0f, 1.0f);
			_targetScroll = _scroll = ratio * GetMaxScroll();
			thumb = GetThumbRect();
		}
		_draggingThumb = true;
		_dragOffset = event.Pos.Y - thumb.Y;
		return true;
	}

	void ScrollView::OnPointerMove(const PointerEvent& event)
	{
		if (!_draggingThumb) {
			return;
		}
		Rectf thumb = GetThumbRect();
		float ratio = std::clamp((event.Pos.Y - _dragOffset - Bounds.Y) / std::max(Bounds.H - thumb.H, 1.0f), 0.0f, 1.0f);
		_targetScroll = _scroll = ratio * GetMaxScroll();
	}

	void ScrollView::OnPointerUp(const PointerEvent& event, bool inside)
	{
		_draggingThumb = false;
	}

	void ScrollView::OnPointerCancel()
	{
		_draggingThumb = false;
	}

	bool ScrollView::OnWheel(const PointerEvent& event)
	{
		if (_contentHeight <= Bounds.H) {
			return false;
		}
		_draggingThumb = false;
		_velocity = 0.0f;
		Surface* surface = GetSurface();
		float rowHeight = std::round(Theme::RowHeight * (surface != nullptr ? surface->GetScale() : 1.0f));
		_targetScroll = std::clamp(_targetScroll - event.Wheel.Y * rowHeight * 3.0f, 0.0f, GetMaxScroll());
		return true;
	}

	Rectf ScrollView::GetThumbRect() const
	{
		Surface* surface = GetSurface();
		float scale = (surface != nullptr ? surface->GetScale() : 1.0f);
		float width = std::round(Theme::ScrollBarWidth * scale);
		float height = std::max(Bounds.H * Bounds.H / std::max(_contentHeight, 1.0f), width * 2.0f);
		float maxScroll = GetMaxScroll();
		float y = Bounds.Y + (maxScroll > 0.0f ? (Bounds.H - height) * (_scroll / maxScroll) : 0.0f);
		float inset = std::max(1.0f, std::round(scale));
		return Rectf(Bounds.X + Bounds.W - width + inset, y + inset, width - inset * 2.0f, height - inset * 2.0f);
	}

	float ScrollView::GetMaxScroll() const
	{
		return std::max(_contentHeight - Bounds.H, 0.0f);
	}

	float ScrollView::GetThumbZoneWidth() const
	{
		Surface* surface = GetSurface();
		float scale = (surface != nullptr ? surface->GetScale() : 1.0f);
		float zone = std::round(Theme::ScrollBarWidth * scale) * 1.5f;
		if (surface != nullptr && surface->TouchInput) {
			// A finger is wider than the bar, not too much more, a list is mostly scrolled by dragging what's in it
			zone = std::max(zone, std::round(8.0f * scale));
		}
		return zone;
	}

	Splitter::Splitter(Windows::Orientation orientation, Function<void()>&& onDragStart, Function<void(float)>&& onDrag, Function<void()>&& onDragEnd)
		: Orientation(orientation), OnDragStart(Death::move(onDragStart)), OnDrag(Death::move(onDrag)), OnDragEnd(Death::move(onDragEnd)), _startPos(0.0f)
	{
		Tooltip = _x("editor", "Drag to resize");
	}

	void Splitter::Update(float timeMult)
	{
		Element::Update(timeMult);
		// Arrows by the pointer while the edge is pointed at or dragged, which way it goes
		if (_hovered || _pressed) {
			if (Surface* surface = GetSurface()) {
				surface->SetCursorOverlay(Orientation == Orientation::Vertical ? CursorOverlay::ResizeHorizontal : CursorOverlay::ResizeVertical);
			}
		}
	}

	void Splitter::Draw(Painter& painter)
	{
		float amount = (_pressed ? 1.0f : _hoverAmount);
		if (amount <= 0.0f && !ShowLine) {
			return;
		}
		// A line along the middle of the edge
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		Rectf line = (Orientation == Orientation::Vertical
			? Rectf(std::round(Bounds.X + (Bounds.W - thickness) * 0.5f), Bounds.Y, thickness, Bounds.H)
			: Rectf(Bounds.X, std::round(Bounds.Y + (Bounds.H - thickness) * 0.5f), Bounds.W, thickness));
		if (ShowLine) {
			painter.FillRect(line, Theme::Separator, PaintLayer::Content);
		}
		if (amount > 0.0f) {
			Colorf color = Theme::Accent;
			color.A *= amount;
			painter.FillRect(line, color, PaintLayer::Overlay);
		}
	}

	bool Splitter::AcceptsPointer() const
	{
		return true;
	}

	bool Splitter::OnPointerDown(const PointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		_startPos = (Orientation == Orientation::Vertical ? event.Pos.X : event.Pos.Y);
		if (OnDragStart) {
			OnDragStart();
		}
		return true;
	}

	void Splitter::OnPointerMove(const PointerEvent& event)
	{
		float pos = (Orientation == Orientation::Vertical ? event.Pos.X : event.Pos.Y);
		if (OnDrag) {
			OnDrag(pos - _startPos);
		}
	}

	void Splitter::OnPointerUp(const PointerEvent& event, bool inside)
	{
		if (OnDragEnd) {
			OnDragEnd();
		}
	}

	DragBar::DragBar(float spacing)
		: Stack(Orientation::Horizontal, spacing)
	{
	}

	bool DragBar::AcceptsPointer() const
	{
		return true;
	}

	bool DragBar::OnPointerDown(const PointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		if (event.Clicks >= 2 && OnDoubleClick) {
			OnDoubleClick();
			return false;
		}
		_startPos = event.Pos;
		_moved = false;
		if (OnDragStart) {
			OnDragStart(event.Pos);
		}
		return true;
	}

	void DragBar::OnPointerMove(const PointerEvent& event)
	{
		if ((event.Pos - _startPos).SqrLength() > 4.0f) {
			_moved = true;
		}
		if (OnDrag) {
			OnDrag(event.Pos - _startPos, event.Pos);
		}
	}

	void DragBar::OnPointerUp(const PointerEvent& event, bool inside)
	{
		_moved = false;
		if (OnDragEnd) {
			OnDragEnd(event.Pos);
		}
	}

	void DragBar::Update(float timeMult)
	{
		Stack::Update(timeMult);
		// What is moved by it can be moved or is being moved, a window by its title, so it's seen before it's grabbed
		if ((_hovered || (_pressed && _moved)) && ShowMoveArrows && OnDrag) {
			if (Surface* surface = GetSurface()) {
				surface->SetCursorOverlay(CursorOverlay::Move);
			}
		}
	}

	Form::Form(float spacing, Thickness padding)
		: Stack(Orientation::Vertical, spacing, padding), MaxLabelWidth(120.0f), MaxLabelFraction(0.5f), MinLabelWidth(0.0f), _labelColumn(0.0f),
			_maxLabelColumn(0.0f)
	{
	}

	Stack* Form::AddRow(StringView label, StringView tooltip)
	{
		return Add<FormRow>(*this, label, tooltip)->GetContent();
	}

	void Form::UpdateLabelColumn(const Context& context, float width)
	{
		float padding = context.Px(Padding.Left) + context.Px(Padding.Right);
		_maxLabelColumn = std::min(context.Px(MaxLabelWidth), std::floor((width - padding) * MaxLabelFraction));
		_labelColumn = std::min(context.Px(MinLabelWidth), _maxLabelColumn);
		for (auto& child : _children) {
			if (!child->IsShown()) {
				continue;
			}
			if (auto* row = runtime_cast<FormRow>(child.get())) {
				float labelWidth = row->GetLabelWidth(context);
				if (labelWidth <= _maxLabelColumn) {
					_labelColumn = std::max(_labelColumn, labelWidth);
				}
			}
		}
	}

	Vector2f Form::Measure(const Context& context, Vector2f available)
	{
		UpdateLabelColumn(context, available.X);
		return Stack::Measure(context, available);
	}

	void Form::Arrange(const Context& context, const Rectf& bounds)
	{
		UpdateLabelColumn(context, bounds.W);
		Stack::Arrange(context, bounds);
	}

	FormRow::FormRow(Form& form, StringView label, StringView tooltip)
		: _form(form)
	{
		_label = Add<Label>(label, Theme::TextDim);
		_label->Tooltip = tooltip;
		_content = Add<Stack>(Orientation::Horizontal, Theme::Spacing);
	}

	float FormRow::GetLabelWidth(const Context& context) const
	{
		// The form and the row both ask for it, more than once a frame
		std::uint32_t layout = GetLayoutFrame();
		if (layout == 0 || layout != _labelWidthLayout || context.Scale != _labelWidthScale) {
			// What the label shows, its text may come from a function
			_labelWidth = std::ceil(context.MeasureText(_label->GetText()));
			_labelWidthLayout = layout;
			_labelWidthScale = context.Scale;
		}
		return _labelWidth;
	}

	bool FormRow::IsStacked(const Context& context) const
	{
		return (GetLabelWidth(context) > _form.GetMaxLabelColumn());
	}

	Vector2f FormRow::Measure(const Context& context, Vector2f available)
	{
		Vector2f labelSize = _label->MeasureCached(context, available);
		if (IsStacked(context)) {
			// The label above what it's about
			Vector2f contentSize = _content->MeasureCached(context, available);
			return Vector2f(std::max(labelSize.X, contentSize.X), labelSize.Y + contentSize.Y);
		}
		float column = _form.GetLabelColumn();
		float spacing = context.Px(Theme::Spacing);
		Vector2f contentSize = _content->MeasureCached(context, Vector2f(std::max(available.X - column - spacing, 0.0f), available.Y));
		return Vector2f(column + spacing + contentSize.X, std::max(labelSize.Y, contentSize.Y));
	}

	void FormRow::Arrange(const Context& context, const Rectf& bounds)
	{
		Bounds = bounds;
		if (IsStacked(context)) {
			float labelHeight = _label->MeasureCached(context, Vector2f(bounds.W, bounds.H)).Y;
			_label->Arrange(context, Rectf(bounds.X, bounds.Y, bounds.W, labelHeight));
			_content->Arrange(context, Rectf(bounds.X, bounds.Y + labelHeight, bounds.W, std::max(bounds.H - labelHeight, 0.0f)));
			return;
		}
		float column = _form.GetLabelColumn();
		float spacing = context.Px(Theme::Spacing);
		_label->Arrange(context, Rectf(bounds.X, bounds.Y, column, bounds.H));
		_content->Arrange(context, Rectf(bounds.X + column + spacing, bounds.Y, std::max(bounds.W - column - spacing, 0.0f), bounds.H));
	}

	TextBox::TextBox(StringView text)
		: Text(text), MaxLength(255), Multiline(false), _caret(0), _selectionStart(0), _scrollX(0.0f), _blinkTime(0.0f), _cancelled(false),
			_lastContext{ 1.0f }
	{
	}

	TextBox::~TextBox()
	{
		if (_alive != nullptr) {
			*_alive = false;
		}
	}

	void TextBox::BeginEdit()
	{
		if (Surface* surface = GetSurface()) {
			surface->SetFocus(this);
		}
		_selectionStart = 0;
		_caret = std::int32_t(Text.size());
	}

	bool TextBox::ShowScreenKeyboard()
	{
		// A box that is typed into already keeps its caret, a tap into it only places the caret, and the keys of
		// the on-screen keyboard tap too, so all of the text isn't selected again
		bool editing = _focused;
		if (!theApplication().CanShowScreenKeyboard()) {
			// The on-screen keyboard then, it types into the box as long as it has the focus
			Surface* surface = GetSurface();
			if (surface == nullptr) {
				return false;
			}
			if (!editing) {
				BeginEdit();
			}
			OnScreenKeyboard::Show(*surface);
			return true;
		}
		if (editing && theApplication().IsScreenKeyboardVisible()) {
			return true;
		}
		if (!editing) {
			BeginEdit();
		}
		if (_alive == nullptr) {
			_alive = std::make_shared<bool>(true);
		}
		// A keyboard that types like a real one types into the box, one that collects the text gives it all at once,
		// possibly frames later, when the box may be gone already
		std::shared_ptr<bool> alive = _alive;
		return theApplication().ShowScreenKeyboard(Text, [this, alive](StringView text) {
			if (!*alive) {
				return;
			}
			Text = String();
			_caret = _selectionStart = 0;
			Insert(text);
			NotifyChanged();
			if (Surface* surface = GetSurface()) {
				surface->SetFocus(nullptr);
			}
		});
	}

	void TextBox::GetLineStarts(StringView text, SmallVectorImpl<std::int32_t>& starts) const
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

	std::int32_t TextBox::GetLineOf(ArrayView<const std::int32_t> starts, std::int32_t offset)
	{
		std::int32_t line = 0;
		while (line + 1 < std::int32_t(starts.size()) && starts[line + 1] <= offset) {
			line++;
		}
		return line;
	}

	std::int32_t TextBox::GetLineEnd(ArrayView<const std::int32_t> starts, std::int32_t line) const
	{
		return (line + 1 < std::int32_t(starts.size()) ? starts[line + 1] - 1 : std::int32_t(Text.size()));
	}

	void TextBox::GetLineLayout(const Context& context, std::int32_t lineCount, float& lineHeight, float& top) const
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

	Vector2f TextBox::Measure(const Context& context, Vector2f available)
	{
		_lastContext = context;
		float height = context.Px(Theme::RowHeight + 4.0f);
		if (Multiline) {
			// Grows with the lines of the text
			StringView shown = GetShownText();
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

	bool TextBox::MeasureDependsOnAvailable() const
	{
		return false;
	}

	StringView TextBox::GetShownText()
	{
		if (_focused || !TextFunc) {
			return Text;
		}
		// Measured and drawn in the same frame, it's made only once for both
		std::uint32_t layout = GetLayoutFrame();
		if (layout == 0 || layout != _shownTextLayout) {
			_shownText = TextFunc();
			_shownTextLayout = layout;
		}
		return _shownText;
	}

	void TextBox::ClampCaret()
	{
		std::int32_t length = std::int32_t(Text.size());
		_caret = std::clamp<std::int32_t>(_caret, 0, length);
		_selectionStart = std::clamp<std::int32_t>(_selectionStart, 0, length);
	}

	void TextBox::Draw(Painter& painter)
	{
		_lastContext = Context{ painter.GetScale() };
		ClampCaret();

		bool usable = IsUsable();
		painter.RoundRect(Bounds, LerpColor(Theme::InputFill, Theme::ButtonPressed, usable && !_focused ? _hoverAmount * 0.6f : 0.0f));
		painter.RoundRectBorder(Bounds, _focused ? Theme::FocusRing : Theme::InputBorder);

		float padding = painter.Px(Theme::Padding);
		Rectf inner(Bounds.X + padding, Bounds.Y, Bounds.W - padding * 2.0f, Bounds.H);
		// The text being edited is shown as it is, a copy is made only of a text from the function
		StringView shown = GetShownText();

		SmallVector<std::int32_t, 8> starts;
		GetLineStarts(shown, starts);
		float lineHeight, top;
		GetLineLayout(_lastContext, std::int32_t(starts.size()), lineHeight, top);
		auto lineRect = [&](std::int32_t line) {
			return Rectf(inner.X, top + line * lineHeight, inner.W, lineHeight);
		};

		painter.PushClip(inner);
		if (shown.empty() && !_focused) {
			painter.TextInRect(PlaceholderFunc ? StringView(PlaceholderFunc()) : StringView(Placeholder), lineRect(0), Theme::TextDisabled);
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
					painter.FillRect(Rectf(inner.X + x1, rect.Y + inset, x2 - x1, rect.H - inset * 2.0f), Theme::Selection, PaintLayer::Content);
				}
			}

			Colorf color = (usable ? Theme::Text : Theme::TextDisabled);
			for (std::int32_t line = 0; line < std::int32_t(starts.size()); line++) {
				std::int32_t lineEnd = (line + 1 < std::int32_t(starts.size()) ? starts[line + 1] - 1 : std::int32_t(shown.size()));
				Rectf rect = lineRect(line);
				painter.TextInRect(shown.slice(std::size_t(starts[line]), std::size_t(lineEnd)), Rectf(rect.X - _scrollX, rect.Y, rect.W + _scrollX, rect.H), color);
			}

			// The caret doesn't blink with reduced motion
			if (_focused && (Theme::ReducedMotion || std::fmod(_blinkTime, CaretBlinkPeriod) < CaretBlinkPeriod * 0.5f)) {
				float caretWidth = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
				Rectf rect = lineRect(caretLine);
				float caretInset = (Multiline ? painter.Px(1.0f) : painter.Px(2.0f));
				painter.FillRect(Rectf(std::round(inner.X + caretX - _scrollX), rect.Y + caretInset, caretWidth, rect.H - caretInset * 2.0f),
					Theme::Accent, PaintLayer::Overlay);
			}
		}
		painter.PopClip();
	}

	void TextBox::Update(float timeMult)
	{
		Element::Update(timeMult);
		_blinkTime += timeMult;
	}

	bool TextBox::AcceptsPointer() const
	{
		return true;
	}

	bool TextBox::OnPointerDown(const PointerEvent& event)
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
		ClampCaret();
		_lastEdit = EditKind::None;
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

	void TextBox::OnPointerMove(const PointerEvent& event)
	{
		if (_focused) {
			_caret = GetOffsetAt(event.Pos);
		}
	}

	bool TextBox::OnKeyPressed(const KeyboardEvent& event)
	{
		if (!_focused) {
			return false;
		}
		if (OnKeyPreview && OnKeyPreview(event)) {
			return true;
		}

		ClampCaret();
		bool shift = (event.Mod & KeyMod::Shift) != 0;
		bool ctrl = (event.Mod & KeyMod::Ctrl) != 0;
#if defined(DEATH_TARGET_APPLE)
		// Cmd is what Ctrl is elsewhere
		ctrl = ctrl || (event.Mod & KeyMod::Super) != 0;
#endif
		_blinkTime = 0.0f;

		auto moveCaret = [this, shift](std::int32_t offset) {
			_caret = std::clamp<std::int32_t>(offset, 0, std::int32_t(Text.size()));
			if (!shift) {
				_selectionStart = _caret;
			}
			// Typing after the caret moved is another step of undo
			_lastEdit = EditKind::None;
		};

		switch (event.Sym) {
			case Keys::Left:
				if (ctrl) {
					moveCaret(FindWordBoundary(_caret, false));
				} else if (!shift && _selectionStart != _caret) {
					moveCaret(std::min(_selectionStart, _caret));
				} else if (_caret > 0) {
					moveCaret(std::int32_t(Utf8::PrevChar(Text, std::size_t(_caret)).second()));
				}
				return true;
			case Keys::Right:
				if (ctrl) {
					moveCaret(FindWordBoundary(_caret, true));
				} else if (!shift && _selectionStart != _caret) {
					moveCaret(std::max(_selectionStart, _caret));
				} else if (_caret < std::int32_t(Text.size())) {
					moveCaret(std::int32_t(Utf8::NextChar(Text, std::size_t(_caret)).second()));
				}
				return true;
			case Keys::Z:
			case Keys::Y:
				if (ctrl) {
					// Ctrl+Z undoes, Ctrl+Y or Ctrl+Shift+Z redoes, the level isn't touched while typing
					bool redo = (event.Sym == Keys::Y || shift);
					if (redo ? Redo() : Undo()) {
						NotifyChanged();
					}
					return true;
				}
				break;
			case Keys::Up:
			case Keys::Down: {
				if (!Multiline) {
					return true;
				}
				// To the same place on the line above or below
				SmallVector<std::int32_t, 8> starts;
				GetLineStarts(Text, starts);
				std::int32_t line = GetLineOf(starts, _caret);
				std::int32_t target = line + (event.Sym == Keys::Up ? -1 : 1);
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
				if (event.Sym == Keys::Home) {
					moveCaret(ctrl ? 0 : starts[line]);
				} else {
					moveCaret(ctrl ? std::int32_t(Text.size()) : GetLineEnd(starts, line));
				}
				return true;
			}
			case Keys::Backspace:
				if (_selectionStart != _caret || _caret > 0) {
					RecordUndo(EditKind::Deleting);
				}
				if (!DeleteSelection() && _caret > 0) {
					// With Ctrl, the whole word before the caret
					std::int32_t from = (ctrl ? FindWordBoundary(_caret, false) : std::int32_t(Utf8::PrevChar(Text, std::size_t(_caret)).second()));
					Text = Text.prefix(std::size_t(from)) + Text.exceptPrefix(std::size_t(_caret));
					_caret = _selectionStart = from;
				}
				NotifyChanged();
				return true;
			case Keys::Delete:
				if (_selectionStart != _caret || _caret < std::int32_t(Text.size())) {
					RecordUndo(EditKind::Deleting);
				}
				if (!DeleteSelection() && _caret < std::int32_t(Text.size())) {
					std::int32_t to = (ctrl ? FindWordBoundary(_caret, true) : std::int32_t(Utf8::NextChar(Text, std::size_t(_caret)).second()));
					Text = Text.prefix(std::size_t(_caret)) + Text.exceptPrefix(std::size_t(to));
				}
				NotifyChanged();
				return true;
			case Keys::Return:
			case Keys::NumPadEnter:
				if (Multiline && !ctrl) {
					RecordUndo(EditKind::Other);
					DeleteSelection();
					Insert(StringView("\n", 1));
					NotifyChanged();
					return true;
				}
				if (Surface* surface = GetSurface()) {
					surface->SetFocus(nullptr);
				}
				// Passed on, so a dialog is confirmed by the same key, unless the text has more lines and is one of more
				return Multiline;
			case Keys::Escape:
				_cancelled = true;
				if (Surface* surface = GetSurface()) {
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
					theApplication().GetInputManager().SetClipboardText(Text.slice(std::size_t(from), std::size_t(to)));
					if (event.Sym == Keys::X) {
						RecordUndo(EditKind::Other);
						DeleteSelection();
						NotifyChanged();
					}
					return true;
				}
				break;
			case Keys::V:
				if (ctrl) {
					// A single line takes only the first line of it, Insert() leaves out the rest
					String clipboard = theApplication().GetInputManager().GetClipboardText();
					RecordUndo(EditKind::Other);
					DeleteSelection();
					Insert(clipboard);
					NotifyChanged();
					return true;
				}
				break;
			default:
				break;
		}

		// Everything else typed goes here too, so it doesn't trigger hotkeys, except Tab, which moves the focus, and what
		// the surface leaves to the shortcuts, such as showing the help
		if (event.Sym == Keys::Tab) {
			return false;
		}
		Surface* surface = GetSurface();
		return (surface == nullptr || !surface->LeavesKeyToShortcuts(event));
	}

	bool TextBox::OnTextInput(const TextInputEvent& event)
	{
		if (!_focused || !IsUsable()) {
			return false;
		}
		ClampCaret();
		// Letters typed in a row are one step of undo, a space starts the next one
		StringView typed(event.Text, std::size_t(event.Length));
		bool space = (typed.size() == 1 && typed[0] == ' ');
		RecordUndo(space || _selectionStart != _caret ? EditKind::Other : EditKind::Typing);
		DeleteSelection();
		Insert(typed);
		NotifyChanged();
		return true;
	}

	void TextBox::RecordUndo(EditKind kind)
	{
		// Edits of the same kind in a row, such as typing a word, are undone together
		if (kind != EditKind::Other && kind == _lastEdit && !_undo.empty()) {
			return;
		}
		if (_undo.size() >= MaxUndoSteps) {
			_undo.erase(_undo.begin());
		}
		_undo.push_back(EditState{ Text, _caret, _selectionStart });
		_redo.clear();
		_lastEdit = kind;
	}

	bool TextBox::Undo()
	{
		if (_undo.empty()) {
			return false;
		}
		_redo.push_back(EditState{ Text, _caret, _selectionStart });
		EditState& state = _undo.back();
		Text = Death::move(state.Text);
		_caret = state.Caret;
		_selectionStart = state.SelectionStart;
		_undo.pop_back();
		_lastEdit = EditKind::None;
		ClampCaret();
		return true;
	}

	bool TextBox::Redo()
	{
		if (_redo.empty()) {
			return false;
		}
		_undo.push_back(EditState{ Text, _caret, _selectionStart });
		EditState& state = _redo.back();
		Text = Death::move(state.Text);
		_caret = state.Caret;
		_selectionStart = state.SelectionStart;
		_redo.pop_back();
		_lastEdit = EditKind::None;
		ClampCaret();
		return true;
	}

	std::int32_t TextBox::FindWordBoundary(std::int32_t offset, bool forward) const
	{
		// A word is letters and digits, any character beyond ASCII counts as a letter; spaces are skipped, and other
		// characters make words of their own
		auto classOf = [](char c) {
			std::uint8_t b = std::uint8_t(c);
			if (c == ' ' || c == '\t' || c == '\n') {
				return 0;
			}
			if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || b >= 0x80) {
				return 1;
			}
			return 2;
		};
		std::int32_t length = std::int32_t(Text.size());
		std::int32_t i = std::clamp<std::int32_t>(offset, 0, length);
		if (forward) {
			if (i < length) {
				std::int32_t kind = classOf(Text[std::size_t(i)]);
				while (i < length && kind != 0 && classOf(Text[std::size_t(i)]) == kind) {
					i++;
				}
			}
			while (i < length && classOf(Text[std::size_t(i)]) == 0) {
				i++;
			}
		} else {
			while (i > 0 && classOf(Text[std::size_t(i - 1)]) == 0) {
				i--;
			}
			if (i > 0) {
				std::int32_t kind = classOf(Text[std::size_t(i - 1)]);
				while (i > 0 && classOf(Text[std::size_t(i - 1)]) == kind) {
					i--;
				}
			}
		}
		return i;
	}

	bool TextBox::IsFocusable() const
	{
		return true;
	}

	void TextBox::OnFocusChanged(bool focused)
	{
		if (focused) {
			if (TextFunc) {
				Text = TextFunc();
			}
			// Undo goes back only within one edit
			_undo.clear();
			_redo.clear();
			_lastEdit = EditKind::None;
			_cancelled = false;
			_blinkTime = 0.0f;
			_selectionStart = 0;
			_caret = std::int32_t(Text.size());
			// Only a text typed into is confirmed when the edit ends, a box only tabbed or clicked through writes nothing
			_editStartText = Text;
			return;
		}

		bool cancelled = _cancelled;
		_cancelled = false;
		if (cancelled) {
			if (TextFunc) {
				Text = TextFunc();
			}
			return;
		}
		// Typed over with the same text is confirmed too, so a value can be given to everything it's shown for, unless it
		// was all undone again
		if (OnCommit && (Text != _editStartText || !_undo.empty())) {
			_editStartText = Text;
			// Called last and with a copy, what the text is confirmed into may rebuild the part of the interface the box is in
			String text = Text;
			OnCommit(text);
		}
	}

	std::int32_t TextBox::GetOffsetAt(Vector2f pos) const
	{
		SmallVector<std::int32_t, 8> starts;
		GetLineStarts(Text, starts);
		std::int32_t line = 0;
		if (Multiline) {
			float lineHeight, top;
			GetLineLayout(_lastContext, std::int32_t(starts.size()), lineHeight, top);
			line = std::clamp<std::int32_t>(std::int32_t(std::floor((pos.Y - top) / std::max(lineHeight, 1.0f))), 0, std::int32_t(starts.size()) - 1);
		}
		float padding = _lastContext.Px(Theme::Padding);
		return GetOffsetInLine(line, pos.X - (Bounds.X + padding) + _scrollX);
	}

	std::int32_t TextBox::GetOffsetInLine(std::int32_t line, float x) const
	{
		SmallVector<std::int32_t, 8> starts;
		GetLineStarts(Text, starts);
		line = std::clamp<std::int32_t>(line, 0, std::int32_t(starts.size()) - 1);
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
	bool TextBox::DeleteSelection()
	{
		if (_selectionStart == _caret) {
			return false;
		}
		std::int32_t from = std::min(_selectionStart, _caret), to = std::max(_selectionStart, _caret);
		Text = Text.prefix(std::size_t(from)) + Text.exceptPrefix(std::size_t(to));
		_caret = _selectionStart = from;
		return true;
	}

	void TextBox::Insert(StringView text)
	{
		// Control characters are left out --- a carriage return of a pasted Windows text, or a form feed, which the
		// font would take for the start of formatting --- a tab becomes a space, and a single line ends at a line break
		String filtered;
		bool needsFilter = false;
		for (char c : text) {
			if (std::uint8_t(c) < 0x20 || c == 0x7F) {
				needsFilter = true;
				break;
			}
		}
		if (needsFilter) {
			filtered = String(NoInit, text.size());
			std::size_t length = 0;
			for (char c : text) {
				if (c == '\n') {
					if (!Multiline) {
						break;
					}
					filtered[length++] = c;
				} else if (c == '\t') {
					filtered[length++] = ' ';
				} else if (std::uint8_t(c) >= 0x20 && c != 0x7F) {
					filtered[length++] = c;
				}
			}
			text = filtered.prefix(length);
		}

		ClampCaret();
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

	void TextBox::NotifyChanged()
	{
		if (OnChanged) {
			OnChanged(Text);
		}
	}

	NumberBox::NumberBox(Function<float()>&& getter, Function<void(float, bool)>&& setter, float minValue, float maxValue, float step, std::int32_t decimals)
		: Getter(Death::move(getter)), Setter(Death::move(setter)), MinValue(minValue), MaxValue(maxValue), Step(step), Decimals(decimals),
			_dragStartX(0.0f), _dragStartValue(0.0f), _dragging(false)
	{
		TextFunc = [this]() {
			return FormatValue(Getter ? Getter() : 0.0f);
		};
		OnCommit = [this](StringView text) {
			String value = text;
			char* end = nullptr;
			// strtod(), as strtof() is C99, which isn't in `std` on every console
			float parsed = float(std::strtod(value.data(), &end));
			// "nan" and "inf" are numbers to strtod(), not to a property
			if (end != value.data() && std::isfinite(parsed) && Setter) {
				Setter(std::clamp(parsed, MinValue, MaxValue), true);
			}
		};
		MaxLength = 16;
	}

	bool NumberBox::IsTabStop() const
	{
		return true;
	}

	bool NumberBox::OnKeyPressed(const KeyboardEvent& event)
	{
		if (_focused && (event.Sym == Keys::Up || event.Sym == Keys::Down)) {
			// Confirms what was typed, steps the value, and goes on typing with the new one
			float steps = (event.Sym == Keys::Up ? 1.0f : -1.0f) * ((event.Mod & KeyMod::Shift) != 0 ? 10.0f : 1.0f);
			if (Surface* surface = GetSurface()) {
				surface->SetFocus(nullptr);
				// What was typed may have rebuilt the part of the interface the box is in, a box taken out of it is left
				if (GetSurface() == surface) {
					StepValue(steps);
					BeginEdit();
				}
			}
			return true;
		}
		return TextBox::OnKeyPressed(event);
	}

	bool NumberBox::IsFocusable() const
	{
		// The surface would give it the focus before the click, which would always place the caret, the arrows and
		// dragging wouldn't work, it takes the focus itself after a click without dragging
		return false;
	}

	void NumberBox::OnPointerCancel()
	{
		// What was dragged to is kept, as if it was let go
		if (_dragging) {
			_dragging = false;
			if (Setter && Getter) {
				Setter(Getter(), true);
			}
		}
	}

	Rectf NumberBox::GetSpinRect() const
	{
		// The arrows and the padding right of them
		float width = _lastContext.Px(13.0f);
		return Rectf(Bounds.X + Bounds.W - width, Bounds.Y, width, Bounds.H);
	}

	void NumberBox::StepValue(float steps)
	{
		if (Getter && Setter) {
			Setter(std::clamp(Getter() + steps * Step, MinValue, MaxValue), true);
		}
	}

	void NumberBox::Draw(Painter& painter)
	{
		TextBox::Draw(painter);
		if (!_focused) {
			// Arrows to step the value, it can be dragged as well
			Rectf spin = GetSpinRect();
			bool overSpin = (_hovered && GetSurface() != nullptr && spin.Contains(GetSurface()->GetPointerPos()));
			Colorf color = Colorf(1.0f, 1.0f, 1.0f, (IsUsable() ? (overSpin ? 0.75f : 0.2f + 0.25f * _hoverAmount) : 0.1f));
			float pixel = std::max(1.0f, std::round(painter.GetScale()));
			float centerX = std::round(spin.X + (spin.W - painter.Px(2.0f)) * 0.5f - pixel * 0.5f);
			float middle = std::round(Bounds.Y + Bounds.H * 0.5f);
			for (std::int32_t i = 0; i < 4; i++) {
				float width = pixel * float(i * 2 + 1);
				// Up above the middle, down below it
				painter.FillRect(Rectf(centerX - pixel * i, middle - pixel * float(5 - i), width, pixel), color, PaintLayer::Content);
				painter.FillRect(Rectf(centerX - pixel * i, middle + pixel * float(4 - i), width, pixel), color, PaintLayer::Content);
			}
		}
	}

	bool NumberBox::OnWheel(const PointerEvent& event)
	{
		// Anywhere over the box, the list it's in scrolls only where the pointer isn't over a control
		if (!IsUsable() || !Bounds.Contains(event.Pos) || event.Wheel.Y == 0.0f) {
			return false;
		}
		float steps = (event.Wheel.Y > 0.0f ? 1.0f : -1.0f) * ((event.Modifiers & KeyMod::Shift) != 0 ? 10.0f : 1.0f);
		if (_focused) {
			// The edited text is replaced by the new value
			if (Surface* surface = GetSurface()) {
				surface->SetFocus(nullptr);
				// What was typed may have rebuilt the part of the interface the box is in, a box taken out of it is left
				if (GetSurface() != surface) {
					return true;
				}
			}
		}
		StepValue(steps);
		return true;
	}

	bool NumberBox::OnPointerDown(const PointerEvent& event)
	{
		// The wheel pressed resets it, also while it's typed into, what was typed is left then
		if (event.Button == MouseButton::Middle && DefaultValue && Setter && IsUsable()) {
			if (_focused) {
				if (Surface* surface = GetSurface()) {
					surface->SetFocus(nullptr);
					// What was typed may have rebuilt the part of the interface the box is in, a box taken out of it is left
					if (GetSurface() != surface) {
						return false;
					}
				}
			}
			Setter(std::clamp(DefaultValue(), MinValue, MaxValue), true);
			return false;
		}
		if (_focused) {
			return TextBox::OnPointerDown(event);
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

	void NumberBox::OnPointerMove(const PointerEvent& event)
	{
		if (_focused) {
			TextBox::OnPointerMove(event);
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

	void NumberBox::OnPointerUp(const PointerEvent& event, bool inside)
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

	String NumberBox::FormatValue(float value) const
	{
		char buffer[32];
		std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%.*f", Decimals, value);
		return String(buffer, std::size_t(std::max<std::int32_t>(length, 0)));
	}

	Rectf ResizeEdges::GetHitBounds(const Rectf& bounds, float scale)
	{
		float outside = std::max(2.0f, std::round(scale * GripOutside));
		return Rectf(bounds.X - outside, bounds.Y - outside, bounds.W + outside * 2.0f, bounds.H + outside * 2.0f);
	}

	std::uint8_t ResizeEdges::GetAt(const Rectf& bounds, float scale, Vector2f pos, float cornerSize)
	{
		if (!GetHitBounds(bounds, scale).Contains(pos)) {
			return 0;
		}
		float inside = std::max(2.0f, std::round(scale * GripInside));
		float right = bounds.X + bounds.W, bottom = bounds.Y + bounds.H;
		// The corner with the dots is easier to grab
		float dots = std::max(3.0f, std::round(scale * GripWidth)) * cornerSize;
		if (pos.X >= right - dots && pos.Y >= bottom - dots) {
			return Right | Bottom;
		}
		std::uint8_t edges = 0;
		if (pos.X < bounds.X + inside) {
			edges |= Left;
		} else if (pos.X >= right - inside) {
			edges |= Right;
		}
		if (pos.Y < bounds.Y + inside) {
			edges |= Top;
		} else if (pos.Y >= bottom - inside) {
			edges |= Bottom;
		}
		// Near the end of an edge it's the corner
		float corner = std::round(scale * CornerReach);
		if ((edges & (Left | Right)) != 0 && (edges & (Top | Bottom)) == 0) {
			if (pos.Y < bounds.Y + corner) {
				edges |= Top;
			} else if (pos.Y >= bottom - corner) {
				edges |= Bottom;
			}
		} else if ((edges & (Top | Bottom)) != 0 && (edges & (Left | Right)) == 0) {
			if (pos.X < bounds.X + corner) {
				edges |= Left;
			} else if (pos.X >= right - corner) {
				edges |= Right;
			}
		}
		return edges;
	}

	void ResizeEdges::Draw(Painter& painter, const Rectf& bounds, std::uint8_t edges)
	{
		// The edges it's resized by light up when pointed at
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		if (edges != 0) {
			Colorf color = Theme::Accent;
			color.A = 0.8f;
			float right = bounds.X + bounds.W, bottom = bounds.Y + bounds.H;
			if (edges & Left) {
				painter.FillRect(Rectf(bounds.X, bounds.Y, thickness, bounds.H), color, PaintLayer::Overlay);
			}
			if (edges & Right) {
				painter.FillRect(Rectf(right - thickness, bounds.Y, thickness, bounds.H), color, PaintLayer::Overlay);
			}
			if (edges & Top) {
				painter.FillRect(Rectf(bounds.X, bounds.Y, bounds.W, thickness), color, PaintLayer::Overlay);
			}
			if (edges & Bottom) {
				painter.FillRect(Rectf(bounds.X, bottom - thickness, bounds.W, thickness), color, PaintLayer::Overlay);
			}
		}

		// Dots in the corner it's resized by
		float dot = thickness;
		float step = dot * 2.0f;
		Colorf gripColor = Theme::IconColor;
		gripColor.A = ((edges & (Right | Bottom)) == (Right | Bottom) ? 0.9f : 0.45f);
		float cornerX = bounds.X + bounds.W - step - dot, cornerY = bounds.Y + bounds.H - step - dot;
		for (std::int32_t row = 0; row < 3; row++) {
			for (std::int32_t column = 2 - row; column < 3; column++) {
				painter.FillRect(Rectf(cornerX - (2 - column) * step, cornerY - (2 - row) * step, dot, dot), gripColor, PaintLayer::Overlay);
			}
		}
	}

	void ResizeEdges::ShowCursor(Surface& surface, std::uint8_t edges)
	{
		// Arrows by the pointer while an edge or a corner is pointed at or dragged, which way it resizes
		if (edges != 0) {
			surface.SetCursorOverlay(Surface::GetResizeOverlay((edges & Left) != 0, (edges & Top) != 0, (edges & Right) != 0, (edges & Bottom) != 0));
		}
	}
}

#endif
