#include "UiMenu.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Input/IInputManager.h"

#include <algorithm>

namespace Jazz2::Editor
{
	UiMenuEntry UiMenuEntry::Separator()
	{
		return UiMenuEntry();
	}

	namespace
	{
		// The first letter of a text in small letters, only ASCII letters are told apart for the keys
		char FirstLetter(StringView text)
		{
			for (char c : text) {
				if (c >= 'A' && c <= 'Z') {
					return char(c - 'A' + 'a');
				}
				if (c >= 'a' && c <= 'z') {
					return c;
				}
			}
			return '\0';
		}

		// Frames the pointer rests on a row before its submenu opens, so passing over rows on the way to one doesn't
		constexpr float SubmenuDelay = 15.0f;
		// Units of the border of a menu, a submenu overlaps it by as much
		constexpr float MenuBorder = 2.0f;
	}

	UiPopupMenu::UiPopupMenu(SmallVectorImpl<UiMenuEntry>&& entries)
		: UiPanel(UiPanelStyle::Floating, UiOrientation::Vertical, 0.0f, UiThickness(MenuBorder)), _highlight(-1), _revealHighlight(false),
			_submenu(nullptr), _submenuRow(nullptr), _hoverRow(nullptr), _hoverTime(0.0f)
	{
		// A menu taller than the screen scrolls, it's never larger than the screen (see UiSurface::OnDraw())
		_scroll = Add<UiScrollView>();
		_rows = _scroll->SetContent<UiStack>(UiOrientation::Vertical, 0.0f);
		for (UiMenuEntry& entry : entries) {
			if (entry.IsSeparator()) {
				_rows->Add<UiSeparator>();
			} else {
				_rows->Add<UiMenuRow>(Death::move(entry));
			}
		}
	}

	UiPopupMenu* UiPopupMenu::Show(UiSurface& surface, SmallVectorImpl<UiMenuEntry>&& entries, Vector2f pos, Function<void()>&& onClosed)
	{
		UiOverlayOptions options;
		options.Position = pos;
		options.OnClosed = Death::move(onClosed);
		return static_cast<UiPopupMenu*>(surface.PushOverlay(std::make_unique<UiPopupMenu>(Death::move(entries)), Death::move(options)));
	}

	void UiPopupMenu::GetRows(SmallVectorImpl<UiMenuRow*>& rows) const
	{
		for (std::int32_t i = 0; i < _rows->GetChildCount(); i++) {
			if (UiMenuRow* row = runtime_cast<UiMenuRow>(_rows->GetChild(i))) {
				rows.push_back(row);
			}
		}
	}

	void UiPopupMenu::SetHighlight(std::int32_t index, bool reveal)
	{
		SmallVector<UiMenuRow*, 16> rows;
		GetRows(rows);
		_highlight = index;
		for (std::int32_t i = 0; i < std::int32_t(rows.size()); i++) {
			rows[i]->Highlighted = (i == index);
		}
		if (reveal && index >= 0) {
			// Scrolled to once the rows are placed, a menu just shown isn't yet
			_revealHighlight = true;
		}
	}

	void UiPopupMenu::Arrange(const UiContext& context, const Rectf& bounds)
	{
		UiPanel::Arrange(context, bounds);
		if (_revealHighlight) {
			_revealHighlight = false;
			SmallVector<UiMenuRow*, 16> rows;
			GetRows(rows);
			if (_highlight >= 0 && _highlight < std::int32_t(rows.size())) {
				float top = rows[_highlight]->Bounds.Y - _rows->Bounds.Y;
				_scroll->ScrollIntoView(top, top + rows[_highlight]->Bounds.H);
			}
		}
		// Placed before the submenu, which is shown after it
		PlaceSubmenu();
	}

	void UiPopupMenu::PlaceSubmenu()
	{
		UiSurface* surface = GetSurface();
		if (_submenu == nullptr || _submenuRow == nullptr || surface == nullptr) {
			return;
		}
		// Its first row next to the row it's opened from, left of this menu if there's no room on the right
		float border = surface->GetContext().Px(MenuBorder);
		surface->MoveOverlay(_submenu, Vector2f(Bounds.X + Bounds.W - border, _submenuRow->Bounds.Y - border), Bounds.X + border);
	}

	void UiPopupMenu::HighlightFirst()
	{
		SmallVector<UiMenuRow*, 16> rows;
		GetRows(rows);
		std::int32_t first = -1;
		for (std::int32_t i = 0; i < std::int32_t(rows.size()); i++) {
			if (!rows[i]->IsEntryEnabled()) {
				continue;
			}
			if (rows[i]->Entry.IsChecked && rows[i]->Entry.IsChecked()) {
				first = i;
				break;
			}
			if (first < 0) {
				first = i;
			}
		}
		SetHighlight(first, true);
	}

	void UiPopupMenu::OpenSubmenu(UiMenuRow* row, bool fromKeyboard)
	{
		UiSurface* surface = GetSurface();
		if (surface == nullptr || !surface->IsOverlayShown(this) || !row->Entry.Submenu || !row->IsEntryEnabled()) {
			return;
		}
		if (_submenu != nullptr && _submenuRow == row) {
			if (fromKeyboard) {
				_submenu->HighlightFirst();
			}
			return;
		}
		CloseSubmenu();

		SmallVector<UiMenuEntry, 0> entries;
		row->Entry.Submenu(entries);
		if (entries.empty()) {
			return;
		}
		SmallVector<UiMenuRow*, 16> rows;
		GetRows(rows);
		for (std::int32_t i = 0; i < std::int32_t(rows.size()); i++) {
			if (rows[i] == row) {
				SetHighlight(i, false);
				break;
			}
		}

		UiOverlayOptions options;
		options.Owner = this;
		// Only one is open at a time, the one before was closed already
		options.OnClosed = [this]() {
			_submenu = nullptr;
			_submenuRow = nullptr;
		};
		_submenu = static_cast<UiPopupMenu*>(surface->PushOverlay(std::make_unique<UiPopupMenu>(Death::move(entries)), Death::move(options)));
		_submenuRow = row;
		PlaceSubmenu();
		// Left closes it, Right goes on to the menu next to the one of the menu bar it's in
		_submenu->OnSwitchMenu = [this](std::int32_t direction) {
			if (direction < 0) {
				CloseSubmenu();
			} else if (OnSwitchMenu) {
				OnSwitchMenu(direction);
			}
		};
		if (fromKeyboard) {
			_submenu->HighlightFirst();
		}
	}

	void UiPopupMenu::CloseSubmenu()
	{
		if (_submenu != nullptr) {
			if (UiSurface* surface = GetSurface()) {
				surface->CloseOverlay(_submenu);
			}
			_submenu = nullptr;
			_submenuRow = nullptr;
		}
	}

	void UiPopupMenu::Update(float timeMult)
	{
		UiPanel::Update(timeMult);
		// The pointer moves the highlight too, the keys go on from where it is
		SmallVector<UiMenuRow*, 16> rows;
		GetRows(rows);
		UiMenuRow* hovered = nullptr;
		for (std::int32_t i = 0; i < std::int32_t(rows.size()); i++) {
			if (rows[i]->IsHovered()) {
				hovered = rows[i];
				if (i != _highlight) {
					SetHighlight(i, false);
				}
				break;
			}
		}

		if (hovered != _hoverRow) {
			_hoverRow = hovered;
			_hoverTime = 0.0f;
		} else if (hovered != nullptr && hovered != _submenuRow) {
			_hoverTime += timeMult;
			if (_hoverTime >= SubmenuDelay) {
				_hoverTime = 0.0f;
				if (hovered->Entry.Submenu) {
					OpenSubmenu(hovered, false);
				} else {
					CloseSubmenu();
				}
			}
		}
	}

	bool UiPopupMenu::OnWheel(const UiPointerEvent& event)
	{
		// Anywhere over the menu, also over its border
		return _scroll->OnWheel(event);
	}

	bool UiPopupMenu::OnKeyPressed(const KeyboardEvent& event)
	{
		SmallVector<UiMenuRow*, 16> rows;
		GetRows(rows);
		std::int32_t count = std::int32_t(rows.size());
		if (count == 0) {
			return false;
		}
		auto step = [&](std::int32_t from, std::int32_t direction) {
			// To the next item that can be used, around the end
			std::int32_t index = from;
			for (std::int32_t i = 0; i < count; i++) {
				index = (index + direction + count) % count;
				if (rows[index]->IsEntryEnabled()) {
					return index;
				}
			}
			return from;
		};
		switch (event.sym) {
			case Keys::Down:
				SetHighlight(step(_highlight < 0 ? -1 : _highlight, 1), true);
				return true;
			case Keys::Up:
				SetHighlight(step(_highlight < 0 ? 0 : _highlight, -1), true);
				return true;
			case Keys::Home:
				SetHighlight(step(-1, 1), true);
				return true;
			case Keys::End:
				SetHighlight(step(0, -1), true);
				return true;
			case Keys::Return:
			case Keys::NumPadEnter:
			case Keys::Space:
				if (_highlight >= 0 && _highlight < count && rows[_highlight]->IsEntryEnabled()) {
					if (rows[_highlight]->Entry.Submenu) {
						OpenSubmenu(rows[_highlight], true);
					} else {
						rows[_highlight]->Run();
					}
				}
				return true;
			case Keys::Left:
			case Keys::Right:
				if (event.sym == Keys::Right && _highlight >= 0 && _highlight < count && rows[_highlight]->Entry.Submenu &&
					rows[_highlight]->IsEntryEnabled()) {
					OpenSubmenu(rows[_highlight], true);
					return true;
				}
				if (OnSwitchMenu) {
					// Closes this menu, which is destroyed only in the next frame
					OnSwitchMenu(event.sym == Keys::Left ? -1 : 1);
					return true;
				}
				return false;
			default:
				return false;
		}
	}

	bool UiPopupMenu::OnTextInput(const TextInputEvent& event)
	{
		// A letter goes to the next item that starts with it
		char letter = FirstLetter(StringView(event.text, std::size_t(event.length)));
		if (letter == '\0') {
			return false;
		}
		SmallVector<UiMenuRow*, 16> rows;
		GetRows(rows);
		std::int32_t count = std::int32_t(rows.size());
		for (std::int32_t i = 1; i <= count; i++) {
			std::int32_t index = ((_highlight < 0 ? -1 : _highlight) + i + count) % count;
			if (rows[index]->IsEntryEnabled() && FirstLetter(rows[index]->Entry.Label) == letter) {
				SetHighlight(index, true);
				return true;
			}
		}
		return true;
	}

	UiMenuRow::UiMenuRow(UiMenuEntry&& entry)
		: Entry(Death::move(entry))
	{
	}

	bool UiMenuRow::IsEntryEnabled()
	{
		return (!Entry.IsEnabled || Entry.IsEnabled());
	}

	void UiMenuRow::Run()
	{
		if (!IsEntryEnabled()) {
			return;
		}
		if (Entry.Submenu) {
			for (UiElement* parent = GetParent(); parent != nullptr; parent = parent->GetParent()) {
				if (UiPopupMenu* menu = runtime_cast<UiPopupMenu>(parent)) {
					menu->OpenSubmenu(this, false);
					break;
				}
			}
			return;
		}
		// The menu goes first, what the item does may show something else over the interface
		Function<void()> execute = Death::move(Entry.Execute);
		if (UiSurface* surface = GetSurface()) {
			surface->ClosePopups();
		}
		if (execute) {
			execute();
		}
	}

	Vector2f UiMenuRow::Measure(const UiContext& context, Vector2f available)
	{
		float padding = context.Px(UiTheme::Padding);
		float width = padding * 2.0f + context.Px(UiTheme::IconSize) + padding + context.MeasureText(Entry.Label);
		if (Entry.Submenu) {
			width += context.Px(24.0f) + context.Px(UiTheme::IconSize);
		} else if (!Entry.ShortcutText.empty()) {
			width += context.Px(24.0f) + context.MeasureText(Entry.ShortcutText);
		}
		return Vector2f(std::max(width, context.Px(110.0f)), context.Px(UiTheme::RowHeight + 5.0f));
	}

	bool UiMenuRow::MeasureDependsOnAvailable() const
	{
		return false;
	}

	bool UiMenuRow::YieldsToScroll() const
	{
		return true;
	}

	void UiMenuRow::Draw(UiPainter& painter)
	{
		bool enabled = IsEntryEnabled();
		bool checked = (Entry.IsChecked && Entry.IsChecked());
		float amount = (Highlighted ? 1.0f : _hoverAmount);
		if (enabled && amount > 0.0f) {
			// Faded in, it keeps the color of the theme, see UiTheme::Resolve()
			Colorf highlight = UiTheme::Selection;
			highlight.A *= amount;
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
		if (Entry.Submenu) {
			painter.Icon(EditorIcon::ChevronRight, Rectf(Bounds.X + Bounds.W - iconSize - padding, Bounds.Y, iconSize, Bounds.H),
				enabled ? UiTheme::IconColor : UiTheme::IconDisabled);
		} else if (!Entry.ShortcutText.empty()) {
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
		if (inside) {
			Run();
		}
	}

	UiDropdown::UiDropdown(Function<String()>&& textFunc, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder)
		: TextFunc(Death::move(textFunc)), Builder(Death::move(builder)), _justClosed(std::make_shared<bool>(false)), _skipOpen(false)
	{
	}

	void UiDropdown::Update(float timeMult)
	{
		*_justClosed = false;
		UiElement::Update(timeMult);
	}

	Vector2f UiDropdown::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f size(context.Px(80.0f), context.Px(UiTheme::RowHeight + 4.0f));
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	bool UiDropdown::MeasureDependsOnAvailable() const
	{
		return false;
	}

	bool UiDropdown::YieldsToScroll() const
	{
		return true;
	}

	void UiDropdown::Draw(UiPainter& painter)
	{
		bool usable = IsUsable();
		painter.RoundRect(Bounds, UiLerpColor(UiTheme::InputFill, UiTheme::ButtonPressed, usable ? _hoverAmount * 0.6f : 0.0f));
		painter.RoundRectBorder(Bounds, (_pressed && usable) ? UiTheme::FocusRing : UiTheme::InputBorder);

		float padding = painter.Px(UiTheme::Padding);
		float iconSize = painter.Px(UiTheme::IconSize);
		Rectf inner(Bounds.X + padding, Bounds.Y, Bounds.W - padding * 2.0f - iconSize, Bounds.H);
		painter.PushClip(inner);
		painter.TextInRect(TextFunc ? TextFunc() : String(), inner, usable ? UiTheme::Text : UiTheme::TextDisabled);
		painter.PopClip();
		painter.Icon(EditorIcon::ChevronDown, Rectf(Bounds.X + Bounds.W - iconSize - padding * 0.5f, Bounds.Y, iconSize, Bounds.H),
			usable ? UiTheme::IconColor : UiTheme::IconDisabled);
		if (_focused) {
			painter.FocusRing(Bounds);
		}
	}

	bool UiDropdown::IsTabStop() const
	{
		return true;
	}

	bool UiDropdown::OnKeyPressed(const KeyboardEvent& event)
	{
		if (!IsUsable()) {
			return false;
		}
		bool alt = (event.mod & KeyMod::Alt) != 0;
		switch (event.sym) {
			case Keys::Return:
			case Keys::NumPadEnter:
			case Keys::Space:
			case Keys::F4:
				Open(true);
				return true;
			case Keys::Up:
			case Keys::Down: {
				// The next choice or the one before, as the list would choose it, without opening it, the list is opened
				// only if none is shown checked
				if (alt || !StepChoice(event.sym == Keys::Down ? 1 : -1)) {
					Open(true);
				}
				return true;
			}
			default:
				return false;
		}
	}

	bool UiDropdown::OnWheel(const UiPointerEvent& event)
	{
		// The control under the pointer gets the wheel, the list it's in scrolls only where there's none
		if (!IsUsable() || event.Wheel.Y == 0.0f || !Bounds.Contains(event.Pos)) {
			return false;
		}
		// Up goes to the choice before, as Up on the keyboard does; a list that doesn't show which choice is the current
		// one can't be stepped, the wheel scrolls then
		return StepChoice(event.Wheel.Y > 0.0f ? -1 : 1);
	}

	bool UiDropdown::StepChoice(std::int32_t direction)
	{
		if (!Builder) {
			return false;
		}
		SmallVector<UiMenuEntry, 0> entries;
		Builder(entries);
		std::int32_t checked = -1;
		for (std::int32_t i = 0; i < std::int32_t(entries.size()); i++) {
			if (!entries[i].IsSeparator() && entries[i].IsChecked && entries[i].IsChecked()) {
				checked = i;
				break;
			}
		}
		if (checked < 0) {
			return false;
		}
		for (std::int32_t i = checked + direction; i >= 0 && i < std::int32_t(entries.size()); i += direction) {
			UiMenuEntry& entry = entries[i];
			if (entry.IsSeparator() || (entry.IsEnabled && !entry.IsEnabled())) {
				continue;
			}
			// The same as picking it in the list
			if (entry.Execute) {
				entry.Execute();
			}
			break;
		}
		return true;
	}

	void UiDropdown::Open(bool fromKeyboard)
	{
		UiSurface* surface = GetSurface();
		if (surface == nullptr || !Builder) {
			return;
		}
		SmallVector<UiMenuEntry, 0> entries;
		Builder(entries);
		if (!entries.empty()) {
			std::shared_ptr<bool> justClosed = _justClosed;
			UiPopupMenu* menu = UiPopupMenu::Show(*surface, Death::move(entries), Vector2f(Bounds.X, Bounds.Y + Bounds.H), [justClosed]() {
				*justClosed = true;
			});
			if (fromKeyboard && menu != nullptr) {
				menu->HighlightFirst();
			}
		}
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
		if (!inside || _skipOpen) {
			_skipOpen = false;
			return;
		}
		Open(false);
	}

	UiMenuBarItem::UiMenuBarItem(UiMenuBar* bar, StringView label, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder)
		: Label(label), Builder(Death::move(builder)), _bar(bar)
	{
	}

	void UiMenuBarItem::Open(bool fromKeyboard)
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
		_bar->_openMenu = UiPopupMenu::Show(*surface, Death::move(entries), Vector2f(Bounds.X, Bounds.Y + Bounds.H), [bar, self]() {
			if (bar->_openItem == self) {
				bar->_openItem = nullptr;
				bar->_openMenu = nullptr;
				bar->_justClosedItem = self;
			}
		});
		if (_bar->_openMenu != nullptr) {
			// Left and Right go to the menus next to it
			_bar->_openMenu->OnSwitchMenu = [bar, self](std::int32_t direction) {
				bar->SwitchMenu(self, direction);
			};
			if (fromKeyboard) {
				_bar->_openMenu->HighlightFirst();
			}
		}
	}

	Vector2f UiMenuBarItem::Measure(const UiContext& context, Vector2f available)
	{
		return Vector2f(context.MeasureText(Label) + context.Px(UiTheme::Padding * 3.0f), context.Px(UiTheme::MenuBarHeight));
	}

	bool UiMenuBarItem::MeasureDependsOnAvailable() const
	{
		return false;
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

		// While Alt is held, the letter that opens the menu with it is underlined
		if (_mnemonic >= 0 && std::size_t(_mnemonic) < Label.size()) {
			const auto& keyboard = theApplication().GetInputManager().keyboardState();
			if (keyboard.isKeyDown(Keys::LAlt) || keyboard.isKeyDown(Keys::RAlt)) {
				float textWidth = painter.MeasureText(Label);
				float left = std::round(Bounds.X + (Bounds.W - textWidth) * 0.5f);
				float before = painter.MeasureText(Label.prefix(std::size_t(_mnemonic)));
				float letter = painter.MeasureText(Label.slice(std::size_t(_mnemonic), std::size_t(_mnemonic) + 1));
				float thickness = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
				float y = std::round(Bounds.Y + (Bounds.H + painter.GetTextHeight() * 0.6f) * 0.5f);
				painter.FillRect(Rectf(std::round(left + before), y, std::max(letter - thickness, thickness), thickness),
					open ? UiTheme::TextOnAccent : UiTheme::IconColor, UiLayer::Overlay);
			}
		}
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
		UiMenuBarItem* item = Add<UiMenuBarItem>(this, label, Death::move(builder));
		AssignMnemonics();
		return item;
	}

	void UiMenuBar::AssignMnemonics()
	{
		// The first letter of each label that no menu before it has, the labels are translated, so it can't be fixed
		bool used[26] = {};
		for (auto& child : _children) {
			UiMenuBarItem* item = runtime_cast<UiMenuBarItem>(child.get());
			if (item == nullptr) {
				continue;
			}
			item->_mnemonic = -1;
			for (std::size_t i = 0; i < item->Label.size(); i++) {
				char c = item->Label[i];
				char lower = (c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
				if (lower >= 'a' && lower <= 'z' && !used[lower - 'a']) {
					used[lower - 'a'] = true;
					item->_mnemonic = std::int32_t(i);
					break;
				}
			}
		}
	}

	bool UiMenuBar::OpenByMnemonic(char letter)
	{
		for (auto& child : _children) {
			UiMenuBarItem* item = runtime_cast<UiMenuBarItem>(child.get());
			if (item == nullptr || item->_mnemonic < 0 || !item->IsShown()) {
				continue;
			}
			char c = item->Label[std::size_t(item->_mnemonic)];
			char lower = (c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
			if (lower == letter) {
				item->Open(true);
				return true;
			}
		}
		return false;
	}

	void UiMenuBar::SwitchMenu(UiMenuBarItem* from, std::int32_t direction)
	{
		SmallVector<UiMenuBarItem*, 16> items;
		for (auto& child : _children) {
			if (UiMenuBarItem* item = runtime_cast<UiMenuBarItem>(child.get())) {
				if (item->IsShown()) {
					items.push_back(item);
				}
			}
		}
		for (std::int32_t i = 0; i < std::int32_t(items.size()); i++) {
			if (items[i] == from) {
				std::int32_t count = std::int32_t(items.size());
				items[(i + direction + count) % count]->Open(true);
				return;
			}
		}
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
