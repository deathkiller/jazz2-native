#include "UiMenu.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include <algorithm>

namespace Jazz2::Editor
{
	UiMenuEntry UiMenuEntry::FromAction(EditorAction& action)
	{
		UiMenuEntry entry;
		entry.Label = action.Label;
		entry.Icon = action.Icon;
		entry.ShortcutText = action.Key.ToString();
		EditorAction* actionPtr = &action;
		entry.Execute = [actionPtr]() {
			if (actionPtr->CanExecute()) {
				actionPtr->Execute();
			}
		};
		entry.IsEnabled = [actionPtr]() {
			return actionPtr->CanExecute();
		};
		if (action.IsChecked) {
			entry.IsChecked = [actionPtr]() {
				return actionPtr->IsChecked();
			};
		}
		return entry;
	}

	UiMenuEntry UiMenuEntry::Separator()
	{
		return UiMenuEntry();
	}

	UiPopupMenu::UiPopupMenu(SmallVectorImpl<UiMenuEntry>&& entries)
		: UiPanel(UiPanelStyle::Floating, UiOrientation::Vertical, 0.0f, UiThickness(2.0f))
	{
		for (UiMenuEntry& entry : entries) {
			if (entry.IsSeparator()) {
				Add<UiSeparator>();
			} else {
				Add<UiMenuRow>(std::move(entry));
			}
		}
	}

	UiPopupMenu* UiPopupMenu::Show(UiSurface& surface, SmallVectorImpl<UiMenuEntry>&& entries, Vector2f pos, Function<void()>&& onClosed)
	{
		UiOverlayOptions options;
		options.Position = pos;
		options.OnClosed = std::move(onClosed);
		return static_cast<UiPopupMenu*>(surface.PushOverlay(std::make_unique<UiPopupMenu>(std::move(entries)), std::move(options)));
	}

	UiMenuRow::UiMenuRow(UiMenuEntry&& entry)
		: Entry(std::move(entry))
	{
	}

	bool UiMenuRow::IsEntryEnabled()
	{
		return (!Entry.IsEnabled || Entry.IsEnabled());
	}

	Vector2f UiMenuRow::Measure(const UiContext& context, Vector2f available)
	{
		float padding = context.Px(UiTheme::Padding);
		float width = padding * 2.0f + context.Px(UiTheme::IconSize) + padding + context.MeasureText(Entry.Label);
		if (!Entry.ShortcutText.empty()) {
			width += context.Px(24.0f) + context.MeasureText(Entry.ShortcutText);
		}
		return Vector2f(std::max(width, context.Px(110.0f)), context.Px(UiTheme::RowHeight + 3.0f));
	}

	void UiMenuRow::Draw(UiPainter& painter)
	{
		bool enabled = IsEntryEnabled();
		bool checked = (Entry.IsChecked && Entry.IsChecked());
		if (enabled && _hoverAmount > 0.0f) {
			Colorf highlight = UiTheme::Selection;
			highlight.A *= _hoverAmount;
			painter.RoundRect(Bounds, highlight);
		}

		float padding = painter.Px(UiTheme::Padding);
		float iconSize = painter.Px(UiTheme::IconSize);
		Rectf iconRect(Bounds.X + padding, Bounds.Y, iconSize, Bounds.H);
		if (checked) {
			painter.Icon(EditorIcon::Check, iconRect, enabled ? UiTheme::Accent : UiTheme::IconDisabled);
		} else if (Entry.Icon != EditorIcon::None) {
			painter.Icon(Entry.Icon, iconRect, enabled ? UiTheme::IconColor : UiTheme::IconDisabled);
		}

		float x = iconRect.X + iconSize + padding;
		painter.TextInRect(Entry.Label, Rectf(x, Bounds.Y, Bounds.X + Bounds.W - x, Bounds.H), enabled ? UiTheme::Text : UiTheme::TextDisabled);
		if (!Entry.ShortcutText.empty()) {
			painter.TextInRect(Entry.ShortcutText, Rectf(Bounds.X, Bounds.Y, Bounds.W - padding * 2.0f, Bounds.H),
				enabled ? UiTheme::TextDim : UiTheme::TextDisabled, Alignment::Right);
		}
	}

	bool UiMenuRow::AcceptsPointer() const
	{
		return true;
	}

	bool UiMenuRow::OnPointerDown(const UiPointerEvent& event)
	{
		return IsEntryEnabled();
	}

	void UiMenuRow::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (!inside || !IsEntryEnabled()) {
			return;
		}
		// The menu goes first, what the item does may show something else over the interface
		Function<void()> execute = std::move(Entry.Execute);
		if (UiSurface* surface = GetSurface()) {
			surface->ClosePopups();
		}
		if (execute) {
			execute();
		}
	}

	UiDropdown::UiDropdown(Function<String()>&& textFunc, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder)
		: TextFunc(std::move(textFunc)), Builder(std::move(builder)), _justClosed(std::make_shared<bool>(false)), _skipOpen(false)
	{
	}

	void UiDropdown::Update(float timeMult)
	{
		*_justClosed = false;
		UiElement::Update(timeMult);
	}

	Vector2f UiDropdown::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f size(context.Px(80.0f), context.Px(UiTheme::RowHeight + 2.0f));
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	void UiDropdown::Draw(UiPainter& painter)
	{
		bool usable = IsUsable();
		float t = (usable ? _hoverAmount * 0.6f : 0.0f);
		const Colorf& from = UiTheme::InputFill;
		const Colorf& to = UiTheme::ButtonPressed;
		painter.RoundRect(Bounds, Colorf(from.R + (to.R - from.R) * t, from.G + (to.G - from.G) * t, from.B + (to.B - from.B) * t, from.A + (to.A - from.A) * t));
		painter.RoundRectBorder(Bounds, (_pressed && usable) ? UiTheme::FocusRing : UiTheme::InputBorder);

		float padding = painter.Px(UiTheme::Padding);
		float iconSize = painter.Px(UiTheme::IconSize);
		Rectf inner(Bounds.X + padding, Bounds.Y, Bounds.W - padding * 2.0f - iconSize, Bounds.H);
		painter.PushClip(inner);
		painter.TextInRect(TextFunc ? TextFunc() : String(), inner, usable ? UiTheme::Text : UiTheme::TextDisabled);
		painter.PopClip();
		painter.Icon(EditorIcon::ChevronDown, Rectf(Bounds.X + Bounds.W - iconSize - padding * 0.5f, Bounds.Y, iconSize, Bounds.H),
			usable ? UiTheme::IconColor : UiTheme::IconDisabled);
	}

	bool UiDropdown::AcceptsPointer() const
	{
		return true;
	}

	bool UiDropdown::OnPointerDown(const UiPointerEvent& event)
	{
		// A click on it while its list is shown only closes the list
		_skipOpen = *_justClosed;
		return (event.Button == MouseButton::Left);
	}

	void UiDropdown::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		UiSurface* surface = GetSurface();
		if (!inside || surface == nullptr || !Builder || _skipOpen) {
			_skipOpen = false;
			return;
		}
		SmallVector<UiMenuEntry, 0> entries;
		Builder(entries);
		if (!entries.empty()) {
			std::shared_ptr<bool> justClosed = _justClosed;
			UiPopupMenu::Show(*surface, std::move(entries), Vector2f(Bounds.X, Bounds.Y + Bounds.H), [justClosed]() {
				*justClosed = true;
			});
		}
	}

	UiMenuBarItem::UiMenuBarItem(UiMenuBar* bar, StringView label, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder)
		: Label(label), Builder(std::move(builder)), _bar(bar)
	{
	}

	void UiMenuBarItem::Open()
	{
		UiSurface* surface = GetSurface();
		if (surface == nullptr) {
			return;
		}
		if (_bar->_openMenu != nullptr) {
			surface->CloseOverlay(_bar->_openMenu);
		}

		SmallVector<UiMenuEntry, 0> entries;
		if (Builder) {
			Builder(entries);
		}
		UiMenuBar* bar = _bar;
		UiMenuBarItem* self = this;
		_bar->_openItem = this;
		_bar->_openMenu = UiPopupMenu::Show(*surface, std::move(entries), Vector2f(Bounds.X, Bounds.Y + Bounds.H), [bar, self]() {
			if (bar->_openItem == self) {
				bar->_openItem = nullptr;
				bar->_openMenu = nullptr;
				bar->_justClosedItem = self;
			}
		});
	}

	Vector2f UiMenuBarItem::Measure(const UiContext& context, Vector2f available)
	{
		return Vector2f(context.MeasureText(Label) + context.Px(UiTheme::Padding * 3.0f), context.Px(UiTheme::MenuBarHeight));
	}

	void UiMenuBarItem::Draw(UiPainter& painter)
	{
		bool open = (_bar->_openItem == this);
		if (open) {
			painter.RoundRect(Bounds, UiTheme::ButtonChecked);
		} else if (_hoverAmount > 0.0f) {
			Colorf highlight = UiTheme::ButtonHover;
			highlight.A *= _hoverAmount;
			painter.RoundRect(Bounds, highlight);
		}
		painter.TextInRect(Label, Bounds, open ? UiTheme::TextOnAccent : UiTheme::Text, Alignment::Center);
	}

	void UiMenuBarItem::Update(float timeMult)
	{
		UiElement::Update(timeMult);

		// While a menu of the bar is open, pointing at another item opens its menu instead
		if (_hovered && _bar->_openItem != nullptr && _bar->_openItem != this) {
			Open();
		}
	}

	bool UiMenuBarItem::AcceptsPointer() const
	{
		return true;
	}

	bool UiMenuBarItem::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		if (_bar->_justClosedItem == this) {
			// This click closed its menu, it doesn't open it again
			return false;
		}
		if (_bar->_openItem == this) {
			if (UiSurface* surface = GetSurface()) {
				surface->ClosePopups();
			}
		} else {
			Open();
		}
		return false;
	}

	UiMenuBar::UiMenuBar()
		: UiPanel(UiPanelStyle::Header, UiOrientation::Horizontal, 0.0f, UiThickness(2.0f, 0.0f)), _openItem(nullptr), _justClosedItem(nullptr), _openMenu(nullptr)
	{
	}

	UiMenuBarItem* UiMenuBar::AddMenu(StringView label, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder)
	{
		return Add<UiMenuBarItem>(this, label, std::move(builder));
	}

	void UiMenuBar::Draw(UiPainter& painter)
	{
		UiPanel::Draw(painter);
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
		painter.FillRect(Rectf(Bounds.X, Bounds.Y + Bounds.H - thickness, Bounds.W, thickness), UiTheme::PanelBorderDim, UiLayer::Overlay);
	}

	void UiMenuBar::Update(float timeMult)
	{
		_justClosedItem = nullptr;
		UiPanel::Update(timeMult);
	}
}

#endif
