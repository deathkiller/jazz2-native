#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiElement.h"

namespace Jazz2::Editor
{
	/**
		@brief List of rows of the same height that draws only the rows that are seen

		It has no element for a row, it asks for the number of rows and draws each one seen by a function, so a list
		of thousands of rows costs as much as what fits on the screen. It's meant to be the content of a
		@ref UiScrollView, it's as high as all of its rows.

		A click picks a row, a double click or <kbd>Enter</kbd> activates it. <kbd>Up</kbd>, <kbd>Down</kbd>,
		<kbd>Page Up</kbd>, <kbd>Page Down</kbd>, <kbd>Home</kbd> and <kbd>End</kbd> move the pick, letters typed go to
		the next row whose text starts with them.

		Rows that hold widgets, such as buttons, get elements of their own from @ref CreateRow, only as many of them as
		rows are seen at once, each bound by @ref BindRow to the row it's placed over.
	*/
	class UiVirtualList : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		UiVirtualList();

		/** @brief Height of a row in units */
		float RowHeight;
		/** @brief Returns the number of rows */
		mutable Function<std::int32_t()> CountFunc;
		/** @brief Draws a row into its rectangle, with whether it's picked and how much it's pointed at, from 0 to 1 */
		Function<void(UiPainter&, std::int32_t, const Rectf&, bool, float)> DrawRow;
		/** @brief Returns the text of a row, which letters typed are looked for in, optional */
		Function<String(std::int32_t)> RowText;
		/** @brief Returns the tooltip of a row, optional */
		mutable Function<String(std::int32_t)> RowTooltip;
		/** @brief Called when the picked row changed, by a click or by the keys */
		Function<void(std::int32_t)> OnSelectionChanged;
		/** @brief Called when a row is activated, by a double click or <kbd>Enter</kbd> */
		Function<void(std::int32_t)> OnActivated;
		/** @brief Called when a row is pressed, with the position in it relative to its top left corner, optional */
		Function<void(std::int32_t, Vector2f, const UiPointerEvent&)> OnRowPressed;
		/** @brief Whether a row can be picked, a list only to be read takes neither the pointer nor the keys, the widgets of its rows still do */
		bool Selectable;
		/** @brief Text shown in place of the rows while there are none, optional */
		String EmptyText;
		/** @brief Color of @ref EmptyText */
		Colorf EmptyColor;
		/**
		 * @brief Makes the element of a row, for rows that hold widgets, optional
		 *
		 * There are only as many of them as rows fit in the list it's in. Each is placed over a row that is seen and
		 * bound to it by @ref BindRow, and to another row when that one takes its place as the list is scrolled, so
		 * its widgets have to act on the row it's bound to now. It's drawn over what @ref DrawRow draws. The list
		 * shows and hides it, it shouldn't be hidden by itself.
		 */
		Function<std::unique_ptr<UiElement>()> CreateRow;
		/** @brief Binds an element made by @ref CreateRow to a row */
		Function<void(UiElement&, std::int32_t)> BindRow;

		/** @brief Returns the picked row, -1 if none */
		std::int32_t GetSelected() const {
			return _selected;
		}
		/** @brief Picks a row, -1 for none, it's scrolled to if @p scrollIntoView, @ref OnSelectionChanged isn't called */
		void SetSelected(std::int32_t index, bool scrollIntoView = true);
		/** @brief Scrolls the list it's in so a row is seen, once it's laid out */
		void ScrollToRow(std::int32_t index);
		/** @brief Returns the row under a position, or -1 */
		std::int32_t GetRowAt(Vector2f pos) const;
		/** @brief Returns the number of rows */
		std::int32_t GetCount() const;
		/** @brief Binds the elements of the rows to their rows again, for rows that changed while their number didn't, see @ref BindRow */
		void RebindRows();

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;
		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
		bool OnTextInput(const TextInputEvent& event) override;
		bool IsFocusable() const override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		std::int32_t _selected;
		float _rowPixels;
		// Row to scroll to once the list is laid out, -1 for none
		std::int32_t _scrollTo;
		// What was typed lately to look for, and how long until it's forgotten, in frames
		String _typed;
		float _typedTime;
		mutable String _tooltip;
		// Row pressed last, a second click is a double click only on the same row
		std::int32_t _pressedRow;
		// Row each element made by CreateRow is bound to, -1 for none, a row has the element of its index modulo their
		// number, so the element stays bound to it while it's seen
		SmallVector<std::int32_t, 0> _rowIndices;

		void Select(std::int32_t index);
		// Places the elements made by CreateRow over the rows that are seen, binds those placed over another row
		void ArrangeRows(const UiContext& context);
	};
}

#endif
