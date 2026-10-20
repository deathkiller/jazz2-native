#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "EditorIcons.h"
#include "UiWidgets.h"

namespace Jazz2::Editor
{
	/**
		@brief Item of a menu
	*/
	struct UiMenuEntry
	{
		/** @brief Text, an entry without it is a separator */
		String Label;
		/** @brief Icon */
		EditorIcon Icon = EditorIcon::None;
		/** @brief Shortcut shown next to the text */
		String ShortcutText;
		/** @brief What it does */
		Function<void()> Execute;
		/** @brief Returns whether it can be used now, always if not set */
		Function<bool()> IsEnabled;
		/** @brief Returns whether it's shown checked */
		Function<bool()> IsChecked;

		/** @brief Returns `true` if it's a separator */
		bool IsSeparator() const {
			return Label.empty();
		}

		/** @brief Makes a separator */
		static UiMenuEntry Separator();
	};

	class UiMenuRow;

	/**
		@brief Menu shown over the interface

		<kbd>Up</kbd> and <kbd>Down</kbd> go through its items, <kbd>Enter</kbd> runs one, a letter goes to the next
		item starting with it.
	*/
	class UiPopupMenu : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		/** @brief Creates a menu of entries */
		explicit UiPopupMenu(SmallVectorImpl<UiMenuEntry>&& entries);

		/** @brief Shows a menu at a position */
		static UiPopupMenu* Show(UiSurface& surface, SmallVectorImpl<UiMenuEntry>&& entries, Vector2f pos, Function<void()>&& onClosed = {});

		/** @brief Called with -1 or 1 when <kbd>Left</kbd> or <kbd>Right</kbd> asks for the menu next to this one, as on the menu bar */
		Function<void(std::int32_t)> OnSwitchMenu;

		/** @brief Highlights the item <kbd>Enter</kbd> runs, the one shown checked or the first one that can be used */
		void HighlightFirst();

		void Arrange(const UiContext& context, const Rectf& bounds) override;
		void Update(float timeMult) override;
		/** @brief Scrolls a menu taller than the screen, anywhere over it */
		bool OnWheel(const UiPointerEvent& event) override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
		bool OnTextInput(const TextInputEvent& event) override;

	private:
		std::int32_t _highlight;
		// The rows scroll in it if they don't fit the screen
		UiScrollView* _scroll;
		UiStack* _rows;
		// The highlighted row is scrolled to once the rows are placed
		bool _revealHighlight;

		// Rows that can be run, in their order, separators are left out
		void GetRows(SmallVectorImpl<UiMenuRow*>& rows) const;
		// Highlights a row, and scrolls to it if it's to be revealed, as by the keys, not by the pointer
		void SetHighlight(std::int32_t index, bool reveal);
	};

	/**
		@brief Row of a menu
	*/
	class UiMenuRow : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		explicit UiMenuRow(UiMenuEntry&& entry);

		/** @brief The entry */
		UiMenuEntry Entry;
		/** @brief Whether it's highlighted as the item the keys of the keyboard are at */
		bool Highlighted = false;

		/** @brief Runs the entry, after the menu it's in is closed */
		void Run();
		/** @brief Returns `true` if the entry can be used now */
		bool IsEntryEnabled();

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		bool YieldsToScroll() const override;
	};

	/**
		@brief Shows a value and opens a menu to choose another one
	*/
	class UiDropdown : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance, the builder fills the menu when it's opened */
		UiDropdown(Function<String()>&& textFunc, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder);

		/** @brief Returns the text shown */
		Function<String()> TextFunc;
		/** @brief Fills the menu when it's opened, it may also do something else and leave the menu empty */
		Function<void(SmallVectorImpl<UiMenuEntry>&)> Builder;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		void Update(float timeMult) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		bool YieldsToScroll() const override;
		bool IsTabStop() const override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
		/** @brief The wheel over it chooses the choice before or after the one shown checked, without going around */
		bool OnWheel(const UiPointerEvent& event) override;

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

	class UiMenuBar;

	/**
		@brief Item of the menu bar, opens its menu
	*/
	class UiMenuBarItem : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

		friend class UiMenuBar;

	public:
		/** @brief Creates a new instance, the builder fills the menu when it's opened */
		UiMenuBarItem(UiMenuBar* bar, StringView label, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder);

		/** @brief Text */
		String Label;
		/** @brief Fills the menu when it's opened */
		Function<void(SmallVectorImpl<UiMenuEntry>&)> Builder;

		/** @brief Opens the menu, @p fromKeyboard highlights its first item */
		void Open(bool fromKeyboard = false);
		/** @brief Returns the index of the letter of the label that opens the menu with <kbd>Alt</kbd>, or -1 */
		std::int32_t GetMnemonicIndex() const {
			return _mnemonic;
		}

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;

	private:
		UiMenuBar* _bar;
		std::int32_t _mnemonic = -1;
	};

	/**
		@brief Menu bar at the top of the window
	*/
	class UiMenuBar : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

		friend class UiMenuBarItem;

	public:
		UiMenuBar();

		/** @brief Adds a menu */
		UiMenuBarItem* AddMenu(StringView label, Function<void(SmallVectorImpl<UiMenuEntry>&)>&& builder);
		/**
		 * @brief Opens the menu whose underlined letter is a letter, as <kbd>Alt</kbd> with it does, returns `false` if there's none
		 *
		 * Each menu has the first letter of its label underlined that no menu before it has.
		 */
		bool OpenByMnemonic(char letter);

		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;

	private:
		UiMenuBarItem* _openItem;
		UiMenuBarItem* _justClosedItem;
		UiPopupMenu* _openMenu;

		void AssignMnemonics();
		void SwitchMenu(UiMenuBarItem* from, std::int32_t direction);
	};
}

#endif
