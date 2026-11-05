#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Icons.h"
#include "Widgets.h"

namespace Jazz2::UI::Windows
{
	/**
		@brief Item of a menu
	*/
	struct MenuEntry
	{
		/** @brief Text, an entry without it is a separator */
		String Label;
		/** @brief Icon */
		IconId Icon = StockIcon::None;
		/** @brief Shortcut shown next to the text */
		String ShortcutText;
		/** @brief What it does */
		Function<void()> Execute;
		/** @brief Returns whether it can be used now, always if not set */
		Function<bool()> IsEnabled;
		/** @brief Returns whether it's shown checked */
		Function<bool()> IsChecked;
		/** @brief Fills the submenu it opens instead of running @ref Execute, when it's opened */
		Function<void(SmallVectorImpl<MenuEntry>&)> Submenu;

		/** @brief Returns `true` if it's a separator */
		bool IsSeparator() const {
			return Label.empty();
		}

		/** @brief Makes a separator */
		static MenuEntry Separator();
	};

	class MenuRow;

	/**
		@brief Menu shown over the interface

		<kbd>Up</kbd> and <kbd>Down</kbd> go through its items, <kbd>Enter</kbd> runs one, a letter goes to the next
		item starting with it. An item with a submenu opens it when it's pointed at for a while, clicked, or by
		<kbd>Enter</kbd> or <kbd>Right</kbd>, <kbd>Left</kbd> or <kbd>Esc</kbd> closes it again.
	*/
	class PopupMenu : public Panel
	{
		DEATH_RUNTIME_OBJECT(Panel);

	public:
		/** @brief Creates a menu of entries */
		explicit PopupMenu(SmallVectorImpl<MenuEntry>&& entries);

		/** @brief Shows a menu at a position */
		static PopupMenu* Show(Surface& surface, SmallVectorImpl<MenuEntry>&& entries, Vector2f pos, Function<void()>&& onClosed = {});

		/** @brief Called with -1 or 1 when <kbd>Left</kbd> or <kbd>Right</kbd> asks for the menu next to this one, as on the menu bar */
		Function<void(std::int32_t)> OnSwitchMenu;

		/** @brief Highlights the item <kbd>Enter</kbd> runs, the one shown checked or the first one that can be used */
		void HighlightFirst();
		/** @brief Opens the submenu of a row of this menu next to it, @p fromKeyboard highlights its first item */
		void OpenSubmenu(MenuRow* row, bool fromKeyboard);
		/** @brief Closes the submenu shown from this menu, if any */
		void CloseSubmenu();

		void Arrange(const Context& context, const Rectf& bounds) override;
		void Update(float timeMult) override;
		/** @brief Scrolls a menu taller than the screen, anywhere over it */
		bool OnWheel(const PointerEvent& event) override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
		bool OnTextInput(const TextInputEvent& event) override;

	private:
		std::int32_t _highlight;
		// The rows scroll in it if they don't fit the screen
		ScrollView* _scroll;
		Stack* _rows;
		// The highlighted row is scrolled to once the rows are placed
		bool _revealHighlight;
		PopupMenu* _submenu;
		MenuRow* _submenuRow;
		// The row the pointer rests on, its submenu opens, or the open one closes, after a while
		MenuRow* _hoverRow;
		float _hoverTime;

		// Rows that can be run, in their order, separators are left out
		void GetRows(SmallVectorImpl<MenuRow*>& rows) const;
		// Highlights a row, and scrolls to it if it's to be revealed, as by the keys, not by the pointer
		void SetHighlight(std::int32_t index, bool reveal);
		// Puts the submenu next to its row, again whenever this menu is placed
		void PlaceSubmenu();
	};

	/**
		@brief Row of a menu
	*/
	class MenuRow : public Element
	{
		DEATH_RUNTIME_OBJECT(Element);

	public:
		/** @brief Creates a new instance */
		explicit MenuRow(MenuEntry&& entry);

		/** @brief The entry */
		MenuEntry Entry;
		/** @brief Whether it's highlighted as the item the keys of the keyboard are at */
		bool Highlighted = false;

		/** @brief Runs the entry, after the menu it's in is closed */
		void Run();
		/** @brief Returns `true` if the entry can be used now */
		bool IsEntryEnabled();

		Vector2f Measure(const Context& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		void Draw(Painter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const PointerEvent& event) override;
		void OnPointerUp(const PointerEvent& event, bool inside) override;
		bool YieldsToScroll() const override;
	};

	/**
		@brief Shows a value and opens a menu to choose another one
	*/
	class Dropdown : public Element
	{
		DEATH_RUNTIME_OBJECT(Element);

	public:
		/** @brief Creates a new instance, the builder fills the menu when it's opened */
		Dropdown(Function<String()>&& textFunc, Function<void(SmallVectorImpl<MenuEntry>&)>&& builder);

		/** @brief Returns the text shown */
		Function<String()> TextFunc;
		/** @brief Fills the menu when it's opened, it may also do something else and leave the menu empty */
		Function<void(SmallVectorImpl<MenuEntry>&)> Builder;

		Vector2f Measure(const Context& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		void Update(float timeMult) override;
		void Draw(Painter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const PointerEvent& event) override;
		void OnPointerUp(const PointerEvent& event, bool inside) override;
		bool YieldsToScroll() const override;
		bool IsTabStop() const override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
		/** @brief The wheel over it chooses the choice before or after the one shown checked, without going around */
		bool OnWheel(const PointerEvent& event) override;

		/** @brief Opens the list, @p fromKeyboard highlights an item in it */
		void Open(bool fromKeyboard);
		/**
		 * @brief Chooses the choice before or after the one shown checked, as picking it in the list does
		 *
		 * Disabled choices and separators are skipped, it doesn't go around the ends. Returns `false` if no choice is
		 * shown checked, so there's nothing to go on from.
		 */
		bool StepChoice(std::int32_t direction);

	private:
		// Set when the list closes, until the next frame, so the click that closed it doesn't open it again; shared
		// with the list, which can outlive this
		std::shared_ptr<bool> _justClosed;
		bool _skipOpen;
	};

	class MenuBar;

	/**
		@brief Item of the menu bar, opens its menu
	*/
	class MenuBarItem : public Element
	{
		DEATH_RUNTIME_OBJECT(Element);

		friend class MenuBar;

	public:
		/** @brief Creates a new instance, the builder fills the menu when it's opened */
		MenuBarItem(MenuBar* bar, StringView label, Function<void(SmallVectorImpl<MenuEntry>&)>&& builder);

		/** @brief Text */
		String Label;
		/** @brief Fills the menu when it's opened */
		Function<void(SmallVectorImpl<MenuEntry>&)> Builder;

		/** @brief Opens the menu, @p fromKeyboard highlights its first item */
		void Open(bool fromKeyboard = false);
		/** @brief Returns the index of the letter of the label that opens the menu with <kbd>Alt</kbd>, or -1 */
		std::int32_t GetMnemonicIndex() const {
			return _mnemonic;
		}

		Vector2f Measure(const Context& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		void Draw(Painter& painter) override;
		void Update(float timeMult) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const PointerEvent& event) override;

	private:
		MenuBar* _bar;
		std::int32_t _mnemonic = -1;
	};

	/**
		@brief Menu bar at the top of the window
	*/
	class MenuBar : public Panel
	{
		DEATH_RUNTIME_OBJECT(Panel);

		friend class MenuBarItem;

	public:
		MenuBar();

		/** @brief Adds a menu */
		MenuBarItem* AddMenu(StringView label, Function<void(SmallVectorImpl<MenuEntry>&)>&& builder);
		/**
		 * @brief Opens the menu whose underlined letter is a letter, as <kbd>Alt</kbd> with it does, returns `false` if there's none
		 *
		 * Each menu has the first letter of its label underlined that no menu before it has.
		 */
		bool OpenByMnemonic(char letter);

		void Draw(Painter& painter) override;
		void Update(float timeMult) override;

	private:
		MenuBarItem* _openItem;
		MenuBarItem* _justClosedItem;
		PopupMenu* _openMenu;

		void AssignMnemonics();
		void SwitchMenu(MenuBarItem* from, std::int32_t direction);
	};
}

#endif
