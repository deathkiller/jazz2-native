#include "Menu.h"

#if defined(WITH_UI_WINDOWS)

#include "Surface.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Input/IInputManager.h"

#include <algorithm>

namespace Jazz2::UI::Windows
{
	MenuEntry MenuEntry::Separator()
	{
		return MenuEntry();
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

	PopupMenu::PopupMenu(SmallVectorImpl<MenuEntry>&& entries)
		: Panel(PanelStyle::Floating, Orientation::Vertical, 0.0f, Thickness(MenuBorder)), _highlight(-1), _revealHighlight(false),
			_submenu(nullptr), _submenuRow(nullptr), _hoverRow(nullptr), _hoverTime(0.0f)
	{
		// A menu taller than the screen scrolls, it's never larger than the screen (see Surface::OnDraw())
		_scroll = Add<ScrollView>();
		_rows = _scroll->SetContent<Stack>(Orientation::Vertical, 0.0f);
		for (MenuEntry& entry : entries) {
			if (entry.IsSeparator()) {
				_rows->Add<Separator>();
			} else {
				_rows->Add<MenuRow>(Death::move(entry));
			}
		}
	}

	PopupMenu* PopupMenu::Show(Surface& surface, SmallVectorImpl<MenuEntry>&& entries, Vector2f pos, Function<void()>&& onClosed)
	{
		OverlayOptions options;
		options.Position = pos;
		options.OnClosed = Death::move(onClosed);
		return static_cast<PopupMenu*>(surface.PushOverlay(std::make_unique<PopupMenu>(Death::move(entries)), Death::move(options)));
	}

	void PopupMenu::GetRows(SmallVectorImpl<MenuRow*>& rows) const
	{
		for (std::int32_t i = 0; i < _rows->GetChildCount(); i++) {
			if (MenuRow* row = runtime_cast<MenuRow>(_rows->GetChild(i))) {
				rows.push_back(row);
			}
		}
	}

	void PopupMenu::SetHighlight(std::int32_t index, bool reveal)
	{
		SmallVector<MenuRow*, 16> rows;
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

	void PopupMenu::Arrange(const Context& context, const Rectf& bounds)
	{
		Panel::Arrange(context, bounds);
		if (_revealHighlight) {
			_revealHighlight = false;
			SmallVector<MenuRow*, 16> rows;
			GetRows(rows);
			if (_highlight >= 0 && _highlight < std::int32_t(rows.size())) {
				float top = rows[_highlight]->Bounds.Y - _rows->Bounds.Y;
				_scroll->ScrollIntoView(top, top + rows[_highlight]->Bounds.H);
			}
		}
		// Placed before the submenu, which is shown after it
		PlaceSubmenu();
	}

	void PopupMenu::PlaceSubmenu()
	{
		Surface* surface = GetSurface();
		if (_submenu == nullptr || _submenuRow == nullptr || surface == nullptr) {
			return;
		}
		// Its first row next to the row it's opened from, left of this menu if there's no room on the right
		float border = surface->GetContext().Px(MenuBorder);
		surface->MoveOverlay(_submenu, Vector2f(Bounds.X + Bounds.W - border, _submenuRow->Bounds.Y - border), Bounds.X + border);
	}

	void PopupMenu::HighlightFirst()
	{
		SmallVector<MenuRow*, 16> rows;
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

	void PopupMenu::OpenSubmenu(MenuRow* row, bool fromKeyboard)
	{
		Surface* surface = GetSurface();
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

		SmallVector<MenuEntry, 0> entries;
		row->Entry.Submenu(entries);
		if (entries.empty()) {
			return;
		}
		SmallVector<MenuRow*, 16> rows;
		GetRows(rows);
		for (std::int32_t i = 0; i < std::int32_t(rows.size()); i++) {
			if (rows[i] == row) {
				SetHighlight(i, false);
				break;
			}
		}

		OverlayOptions options;
		options.Owner = this;
		// Only one is open at a time, the one before was closed already
		options.OnClosed = [this]() {
			_submenu = nullptr;
			_submenuRow = nullptr;
		};
		_submenu = static_cast<PopupMenu*>(surface->PushOverlay(std::make_unique<PopupMenu>(Death::move(entries)), Death::move(options)));
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

	void PopupMenu::CloseSubmenu()
	{
		if (_submenu != nullptr) {
			if (Surface* surface = GetSurface()) {
				surface->CloseOverlay(_submenu);
			}
			_submenu = nullptr;
			_submenuRow = nullptr;
		}
	}

	void PopupMenu::Update(float timeMult)
	{
		Panel::Update(timeMult);
		// The pointer moves the highlight too, the keys go on from where it is
		SmallVector<MenuRow*, 16> rows;
		GetRows(rows);
		MenuRow* hovered = nullptr;
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

	bool PopupMenu::OnWheel(const PointerEvent& event)
	{
		// Anywhere over the menu, also over its border
		return _scroll->OnWheel(event);
	}

	bool PopupMenu::OnKeyPressed(const KeyboardEvent& event)
	{
		SmallVector<MenuRow*, 16> rows;
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
		switch (event.Sym) {
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
				if (event.Sym == Keys::Right && _highlight >= 0 && _highlight < count && rows[_highlight]->Entry.Submenu &&
					rows[_highlight]->IsEntryEnabled()) {
					OpenSubmenu(rows[_highlight], true);
					return true;
				}
				if (OnSwitchMenu) {
					// Closes this menu, which is destroyed only in the next frame
					OnSwitchMenu(event.Sym == Keys::Left ? -1 : 1);
					return true;
				}
				return false;
			default:
				return false;
		}
	}

	bool PopupMenu::OnTextInput(const TextInputEvent& event)
	{
		// A letter goes to the next item that starts with it
		char letter = FirstLetter(StringView(event.Text, std::size_t(event.Length)));
		if (letter == '\0') {
			return false;
		}
		SmallVector<MenuRow*, 16> rows;
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

	MenuRow::MenuRow(MenuEntry&& entry)
		: Entry(Death::move(entry))
	{
	}

	bool MenuRow::IsEntryEnabled()
	{
		return (!Entry.IsEnabled || Entry.IsEnabled());
	}

	void MenuRow::Run()
	{
		if (!IsEntryEnabled()) {
			return;
		}
		if (Entry.Submenu) {
			for (Element* parent = GetParent(); parent != nullptr; parent = parent->GetParent()) {
				if (PopupMenu* menu = runtime_cast<PopupMenu>(parent)) {
					menu->OpenSubmenu(this, false);
					break;
				}
			}
			return;
		}
		// The menu goes first, what the item does may show something else over the interface
		Function<void()> execute = Death::move(Entry.Execute);
		if (Surface* surface = GetSurface()) {
			surface->ClosePopups();
		}
		if (execute) {
			execute();
		}
	}

	Vector2f MenuRow::Measure(const Context& context, Vector2f available)
	{
		float padding = context.Px(Theme::Padding);
		float width = padding * 2.0f + context.Px(Theme::IconSize) + padding + context.MeasureText(Entry.Label);
		if (Entry.Submenu) {
			width += context.Px(24.0f) + context.Px(Theme::IconSize);
		} else if (!Entry.ShortcutText.empty()) {
			width += context.Px(24.0f) + context.MeasureText(Entry.ShortcutText);
		}
		return Vector2f(std::max(width, context.Px(110.0f)), context.Px(Theme::RowHeight + 5.0f));
	}

	bool MenuRow::MeasureDependsOnAvailable() const
	{
		return false;
	}

	bool MenuRow::YieldsToScroll() const
	{
		return true;
	}

	void MenuRow::Draw(Painter& painter)
	{
		bool enabled = IsEntryEnabled();
		bool checked = (Entry.IsChecked && Entry.IsChecked());
		float amount = (Highlighted ? 1.0f : _hoverAmount);
		if (enabled && amount > 0.0f) {
			// Faded in, it keeps the color of the theme, see Theme::Resolve()
			Colorf highlight = Theme::Selection;
			highlight.A *= amount;
			painter.RoundRect(Bounds, highlight);
		}

		float padding = painter.Px(Theme::Padding);
		float iconSize = painter.Px(Theme::IconSize);
		Rectf iconRect(Bounds.X + padding, Bounds.Y, iconSize, Bounds.H);
		if (checked) {
			painter.Icon(StockIcon::Check, iconRect, enabled ? Theme::Accent : Theme::IconDisabled);
		} else if (Entry.Icon != StockIcon::None) {
			painter.Icon(Entry.Icon, iconRect, enabled ? Theme::IconColor : Theme::IconDisabled);
		}

		float x = iconRect.X + iconSize + padding;
		painter.TextInRect(Entry.Label, Rectf(x, Bounds.Y, Bounds.X + Bounds.W - x, Bounds.H), enabled ? Theme::Text : Theme::TextDisabled);
		if (Entry.Submenu) {
			painter.Icon(StockIcon::ChevronRight, Rectf(Bounds.X + Bounds.W - iconSize - padding, Bounds.Y, iconSize, Bounds.H),
				enabled ? Theme::IconColor : Theme::IconDisabled);
		} else if (!Entry.ShortcutText.empty()) {
			painter.TextInRect(Entry.ShortcutText, Rectf(Bounds.X, Bounds.Y, Bounds.W - padding * 2.0f, Bounds.H),
				enabled ? Theme::TextDim : Theme::TextDisabled, Alignment::Right);
		}
	}

	bool MenuRow::AcceptsPointer() const
	{
		return true;
	}

	bool MenuRow::OnPointerDown(const PointerEvent& event)
	{
		return IsEntryEnabled();
	}

	void MenuRow::OnPointerUp(const PointerEvent& event, bool inside)
	{
		if (inside) {
			Run();
		}
	}

	Dropdown::Dropdown(Function<String()>&& textFunc, Function<void(SmallVectorImpl<MenuEntry>&)>&& builder)
		: TextFunc(Death::move(textFunc)), Builder(Death::move(builder)), _justClosed(std::make_shared<bool>(false)), _skipOpen(false)
	{
	}

	void Dropdown::Update(float timeMult)
	{
		*_justClosed = false;
		Element::Update(timeMult);
	}

	Vector2f Dropdown::Measure(const Context& context, Vector2f available)
	{
		Vector2f size(context.Px(80.0f), context.Px(Theme::RowHeight + 4.0f));
		Vector2f fixedSize = GetFixedSize(context);
		if (fixedSize.X >= 0.0f) {
			size.X = fixedSize.X;
		}
		if (fixedSize.Y >= 0.0f) {
			size.Y = fixedSize.Y;
		}
		return size;
	}

	bool Dropdown::MeasureDependsOnAvailable() const
	{
		return false;
	}

	bool Dropdown::YieldsToScroll() const
	{
		return true;
	}

	void Dropdown::Draw(Painter& painter)
	{
		bool usable = IsUsable();
		painter.RoundRect(Bounds, LerpColor(Theme::InputFill, Theme::ButtonPressed, usable ? _hoverAmount * 0.6f : 0.0f));
		painter.RoundRectBorder(Bounds, (_pressed && usable) ? Theme::FocusRing : Theme::InputBorder);

		float padding = painter.Px(Theme::Padding);
		float iconSize = painter.Px(Theme::IconSize);
		Rectf inner(Bounds.X + padding, Bounds.Y, Bounds.W - padding * 2.0f - iconSize, Bounds.H);
		painter.PushClip(inner);
		painter.TextInRect(TextFunc ? TextFunc() : String(), inner, usable ? Theme::Text : Theme::TextDisabled);
		painter.PopClip();
		painter.Icon(StockIcon::ChevronDown, Rectf(Bounds.X + Bounds.W - iconSize - padding * 0.5f, Bounds.Y, iconSize, Bounds.H),
			usable ? Theme::IconColor : Theme::IconDisabled);
		if (_focused) {
			painter.FocusRing(Bounds);
		}
	}

	bool Dropdown::IsTabStop() const
	{
		return true;
	}

	bool Dropdown::OnKeyPressed(const KeyboardEvent& event)
	{
		if (!IsUsable()) {
			return false;
		}
		bool alt = (event.Mod & KeyMod::Alt) != 0;
		switch (event.Sym) {
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
				if (alt || !StepChoice(event.Sym == Keys::Down ? 1 : -1)) {
					Open(true);
				}
				return true;
			}
			default:
				return false;
		}
	}

	bool Dropdown::OnWheel(const PointerEvent& event)
	{
		// The control under the pointer gets the wheel, the list it's in scrolls only where there's none
		if (!IsUsable() || event.Wheel.Y == 0.0f || !Bounds.Contains(event.Pos)) {
			return false;
		}
		// Up goes to the choice before, as Up on the keyboard does; a list that doesn't show which choice is the current
		// one can't be stepped, the wheel scrolls then
		return StepChoice(event.Wheel.Y > 0.0f ? -1 : 1);
	}

	bool Dropdown::StepChoice(std::int32_t direction)
	{
		if (!Builder) {
			return false;
		}
		SmallVector<MenuEntry, 0> entries;
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
			MenuEntry& entry = entries[i];
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

	void Dropdown::Open(bool fromKeyboard)
	{
		Surface* surface = GetSurface();
		if (surface == nullptr || !Builder) {
			return;
		}
		SmallVector<MenuEntry, 0> entries;
		Builder(entries);
		if (!entries.empty()) {
			std::shared_ptr<bool> justClosed = _justClosed;
			PopupMenu* menu = PopupMenu::Show(*surface, Death::move(entries), Vector2f(Bounds.X, Bounds.Y + Bounds.H), [justClosed]() {
				*justClosed = true;
			});
			if (fromKeyboard && menu != nullptr) {
				menu->HighlightFirst();
			}
		}
	}

	bool Dropdown::AcceptsPointer() const
	{
		return true;
	}

	bool Dropdown::OnPointerDown(const PointerEvent& event)
	{
		// A click on it while its list is shown only closes the list
		_skipOpen = *_justClosed;
		return (event.Button == MouseButton::Left);
	}

	void Dropdown::OnPointerUp(const PointerEvent& event, bool inside)
	{
		if (!inside || _skipOpen) {
			_skipOpen = false;
			return;
		}
		Open(false);
	}

	MenuBarItem::MenuBarItem(MenuBar* bar, StringView label, Function<void(SmallVectorImpl<MenuEntry>&)>&& builder)
		: Label(label), Builder(Death::move(builder)), _bar(bar)
	{
	}

	void MenuBarItem::Open(bool fromKeyboard)
	{
		Surface* surface = GetSurface();
		if (surface == nullptr) {
			return;
		}
		if (_bar->_openMenu != nullptr) {
			surface->CloseOverlay(_bar->_openMenu);
		}

		SmallVector<MenuEntry, 0> entries;
		if (Builder) {
			Builder(entries);
		}
		MenuBar* bar = _bar;
		MenuBarItem* self = this;
		_bar->_openItem = this;
		_bar->_openMenu = PopupMenu::Show(*surface, Death::move(entries), Vector2f(Bounds.X, Bounds.Y + Bounds.H), [bar, self]() {
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

	Vector2f MenuBarItem::Measure(const Context& context, Vector2f available)
	{
		return Vector2f(context.MeasureText(Label) + context.Px(Theme::Padding * 3.0f), context.Px(Theme::MenuBarHeight));
	}

	bool MenuBarItem::MeasureDependsOnAvailable() const
	{
		return false;
	}

	void MenuBarItem::Draw(Painter& painter)
	{
		bool open = (_bar->_openItem == this);
		if (open) {
			painter.RoundRect(Bounds, Theme::ButtonChecked);
		} else if (_hoverAmount > 0.0f) {
			Colorf highlight = Theme::ButtonHover;
			highlight.A *= _hoverAmount;
			painter.RoundRect(Bounds, highlight);
		}
		painter.TextInRect(Label, Bounds, open ? Theme::TextOnAccent : Theme::Text, Alignment::Center);

		// While Alt is held, the letter that opens the menu with it is underlined
		if (_mnemonic >= 0 && std::size_t(_mnemonic) < Label.size()) {
			const auto& keyboard = theApplication().GetInputManager().GetKeyboardState();
			if (keyboard.IsKeyDown(Keys::LAlt) || keyboard.IsKeyDown(Keys::RAlt)) {
				float textWidth = painter.MeasureText(Label);
				float left = std::round(Bounds.X + (Bounds.W - textWidth) * 0.5f);
				float before = painter.MeasureText(Label.prefix(std::size_t(_mnemonic)));
				float letter = painter.MeasureText(Label.slice(std::size_t(_mnemonic), std::size_t(_mnemonic) + 1));
				float thickness = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
				float y = std::round(Bounds.Y + (Bounds.H + painter.GetTextHeight() * 0.6f) * 0.5f);
				painter.FillRect(Rectf(std::round(left + before), y, std::max(letter - thickness, thickness), thickness),
					open ? Theme::TextOnAccent : Theme::IconColor, PaintLayer::Overlay);
			}
		}
	}

	void MenuBarItem::Update(float timeMult)
	{
		Element::Update(timeMult);

		// While a menu of the bar is open, pointing at another item opens its menu instead
		if (_hovered && _bar->_openItem != nullptr && _bar->_openItem != this) {
			Open();
		}
	}

	bool MenuBarItem::AcceptsPointer() const
	{
		return true;
	}

	bool MenuBarItem::OnPointerDown(const PointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		if (_bar->_justClosedItem == this) {
			// This click closed its menu, it doesn't open it again
			return false;
		}
		if (_bar->_openItem == this) {
			if (Surface* surface = GetSurface()) {
				surface->ClosePopups();
			}
		} else {
			Open();
		}
		return false;
	}

	MenuBar::MenuBar()
		: Panel(PanelStyle::Header, Orientation::Horizontal, 0.0f, Thickness(2.0f, 0.0f)), _openItem(nullptr), _justClosedItem(nullptr), _openMenu(nullptr)
	{
	}

	MenuBarItem* MenuBar::AddMenu(StringView label, Function<void(SmallVectorImpl<MenuEntry>&)>&& builder)
	{
		MenuBarItem* item = Add<MenuBarItem>(this, label, Death::move(builder));
		AssignMnemonics();
		return item;
	}

	void MenuBar::AssignMnemonics()
	{
		// The first letter of each label that no menu before it has, the labels are translated, so it can't be fixed
		bool used[26] = {};
		for (auto& child : _children) {
			MenuBarItem* item = runtime_cast<MenuBarItem>(child.get());
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

	bool MenuBar::OpenByMnemonic(char letter)
	{
		for (auto& child : _children) {
			MenuBarItem* item = runtime_cast<MenuBarItem>(child.get());
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

	void MenuBar::SwitchMenu(MenuBarItem* from, std::int32_t direction)
	{
		SmallVector<MenuBarItem*, 16> items;
		for (auto& child : _children) {
			if (MenuBarItem* item = runtime_cast<MenuBarItem>(child.get())) {
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

	void MenuBar::Draw(Painter& painter)
	{
		Panel::Draw(painter);
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
		painter.FillRect(Rectf(Bounds.X, Bounds.Y + Bounds.H - thickness, Bounds.W, thickness), Theme::PanelBorderDim, PaintLayer::Overlay);
	}

	void MenuBar::Update(float timeMult)
	{
		_justClosedItem = nullptr;
		Panel::Update(timeMult);
	}
}

#endif
