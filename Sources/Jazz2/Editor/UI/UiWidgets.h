#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiElement.h"

namespace Jazz2::Editor
{
	/**
		@brief Text
	*/
	class UiLabel : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		explicit UiLabel(StringView text = {}, const Colorf& color = UiTheme::Text, Alignment align = Alignment::Left);

		/** @brief Text */
		String Text;
		/** @brief Returns the text, replaces @ref Text when set */
		Function<String()> TextFunc;
		/** @brief Color */
		Colorf Color;
		/** @brief Horizontal alignment */
		Alignment Align;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;

	protected:
		String GetText();
	};

	/** @brief How a button is drawn */
	enum class UiButtonStyle : std::uint8_t
	{
		Normal,			/**< With a background */
		Flat,			/**< With a background only when it's pointed at or checked */
		Accent			/**< Highlighted, for the main action */
	};

	/**
		@brief Button with a text, an icon or both
	*/
	class UiButton : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		UiButton(StringView text, EditorIcon icon = EditorIcon::None, Function<void()>&& onClick = {});

		/** @brief Text */
		String Text;
		/** @brief Icon */
		EditorIcon Icon;
		/** @brief How the button is drawn */
		UiButtonStyle Style;
		/** @brief Called when the button is clicked */
		Function<void()> OnClick;
		/** @brief Returns whether the button is shown checked */
		Function<bool()> IsCheckedFunc;
		/** @brief Color of the icon */
		Colorf IconColor;
		/** @brief Smallest width in units */
		float MinWidth;
		/** @brief Where the text and the icon are in a button wider than them, centered or at the left */
		Alignment TextAlign;
		/** @brief Color of the text instead of the usual one, a text color (see @ref UiTheme::Text), unused if its alpha is 0 */
		Colorf TextColor;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;

		/** @brief Returns `true` if the button is shown checked */
		bool IsChecked();
	};

	/**
		@brief Check box with a text
	*/
	class UiCheckBox : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		UiCheckBox(StringView text, Function<bool()>&& getter, Function<void(bool)>&& setter);

		/** @brief Text */
		String Text;
		/** @brief Returns whether it's checked */
		Function<bool()> Getter;
		/** @brief Called when it's checked or unchecked */
		Function<void(bool)> Setter;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
	};

	/**
		@brief Line between parts of a panel
	*/
	class UiSeparator : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance, a vertical separator divides a row */
		explicit UiSeparator(UiOrientation orientation = UiOrientation::Horizontal);

		/** @brief Direction of the line */
		UiOrientation Orientation;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
	};

	/**
		@brief Shows a part of its content and lets the rest be scrolled to
	*/
	class UiScrollView : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		UiScrollView();

		/** @brief Returns the content, the only child */
		UiElement* GetContent() const;
		/** @brief Sets the content */
		template<class T, class... Args>
		T* SetContent(Args&&... args) {
			ClearChildren();
			return Add<T>(std::forward<Args>(args)...);
		}
		/** @brief Returns how far the content is scrolled in pixels */
		float GetScroll() const {
			return _scroll;
		}
		/** @brief Scrolls so that a part of the content in pixels from its top is visible */
		void ScrollIntoView(float top, float bottom);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;
		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;
		UiElement* HitTest(Vector2f pos) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		bool OnWheel(const UiPointerEvent& event) override;

	private:
		float _scroll;
		float _targetScroll;
		float _contentHeight;
		float _dragOffset;
		bool _draggingThumb;

		Rectf GetThumbRect() const;
		float GetMaxScroll() const;
	};

	/**
		@brief Edge of a panel that is dragged to resize it
	*/
	class UiSplitter : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance, a vertical splitter is dragged horizontally */
		UiSplitter(UiOrientation orientation, Function<void()>&& onDragStart, Function<void(float)>&& onDrag, Function<void()>&& onDragEnd = {});

		/** @brief Direction of the edge */
		UiOrientation Orientation;
		/** @brief Called when the edge is pressed */
		Function<void()> OnDragStart;
		/** @brief Called with how far the edge was dragged since it was pressed, in pixels */
		Function<void(float)> OnDrag;
		/** @brief Called when the edge is let go */
		Function<void()> OnDragEnd;
		/** @brief Whether a line like a separator is shown along it all the time, not only while it's pointed at */
		bool ShowLine = false;

		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;

	private:
		float _startPos;
	};

	/**
		@brief Row that moves what it belongs to when dragged, such as the title of a window

		Only the parts of the row that don't take the pointer themselves start a drag, a button in the row is
		still clicked as usual.
	*/
	class UiDragBar : public UiStack
	{
		DEATH_RUNTIME_OBJECT(UiStack);

	public:
		/** @brief Creates a new instance */
		explicit UiDragBar(float spacing = UiTheme::Spacing);

		/** @brief Called when the row is pressed, with the position of the pointer */
		Function<void(Vector2f)> OnDragStart;
		/** @brief Called with how far the pointer moved since the row was pressed and where it is now, in pixels */
		Function<void(Vector2f, Vector2f)> OnDrag;
		/** @brief Called when the row is let go, with the position of the pointer */
		Function<void(Vector2f)> OnDragEnd;
		/** @brief Called when the row is double clicked */
		Function<void()> OnDoubleClick;

		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;

	private:
		Vector2f _startPos;
	};

	class UiFormRow;

	/**
		@brief Rows of a label and what it's about, with the labels in a column as wide as the longest of them

		A label that would make the column too wide is put above what it's about instead, so a long label doesn't
		take space from all the rows and isn't cut off either. Other elements, such as check boxes, can be added
		between the rows, they take the whole width.
	*/
	class UiForm : public UiStack
	{
		DEATH_RUNTIME_OBJECT(UiStack);

	public:
		/** @brief Creates a new instance */
		explicit UiForm(float spacing = UiTheme::Spacing, UiThickness padding = {});

		/** @brief Widest the column of labels gets in units */
		float MaxLabelWidth;
		/** @brief Widest the column of labels gets as a part of the width of the form */
		float MaxLabelFraction;

		/** @brief Adds a row with a label, what it's about is added to the returned stack */
		UiStack* AddRow(StringView label, StringView tooltip = {});
		/** @brief Returns the width of the column of labels in pixels */
		float GetLabelColumn() const {
			return _labelColumn;
		}
		/** @brief Returns the widest a label can be in pixels before it's put above what it's about */
		float GetMaxLabelColumn() const {
			return _maxLabelColumn;
		}

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;

	private:
		float _labelColumn;
		float _maxLabelColumn;

		void UpdateLabelColumn(const UiContext& context, float width);
	};

	/**
		@brief Row of a @ref UiForm
	*/
	class UiFormRow : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		UiFormRow(UiForm& form, StringView label, StringView tooltip);

		/** @brief Returns the label */
		UiLabel* GetLabel() const {
			return _label;
		}
		/** @brief Returns the stack of what the row is about */
		UiStack* GetContent() const {
			return _content;
		}

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;

	private:
		UiForm& _form;
		UiLabel* _label;
		UiStack* _content;

		bool IsStacked(const UiContext& context) const;
	};

	/**
		@brief Single line of text that can be edited
	*/
	class UiTextBox : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		explicit UiTextBox(StringView text = {});

		/** @brief Text */
		String Text;
		/** @brief Text shown while it's empty */
		String Placeholder;
		/** @brief Returns the text shown while it's empty, replaces @ref Placeholder when set */
		Function<String()> PlaceholderFunc;
		/** @brief Returns the text to show while it's not being edited, replaces @ref Text then */
		Function<String()> TextFunc;
		/** @brief Called when the edit is confirmed, by Enter or by leaving it */
		Function<void(StringView)> OnCommit;
		/** @brief Called whenever the text changes while it's edited */
		Function<void(StringView)> OnChanged;
		/** @brief Longest text in bytes */
		std::int32_t MaxLength;
		/** @brief Whether the text can have more lines, it grows with them --- Enter breaks a line then, Ctrl+Enter confirms */
		bool Multiline;

		/** @brief Starts editing with all of the text selected */
		void BeginEdit();
		/**
		 * @brief Starts editing with the screen keyboard of the system, for a gamepad or a touch screen
		 *
		 * Returns `false` if the system has none. Where the keyboard collects the whole text, the text is replaced
		 * by it and confirmed.
		 */
		bool ShowScreenKeyboard();

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
		bool OnTextInput(const TextInputEvent& event) override;
		bool IsFocusable() const override;
		void OnFocusChanged(bool focused) override;

	protected:
		std::int32_t _caret;
		std::int32_t _selectionStart;
		float _scrollX;
		float _blinkTime;
		bool _cancelled;

		/** @brief Returns the byte offset in the text closest to a position */
		std::int32_t GetOffsetAt(Vector2f pos) const;
		/** @brief Returns the byte offset in a line of the text closest to a horizontal position from the start of the line */
		std::int32_t GetOffsetInLine(std::int32_t line, float x) const;
		/** @brief Fills the byte offsets the lines of a text start at, a single line unless it's @ref Multiline */
		void GetLineStarts(StringView text, SmallVectorImpl<std::int32_t>& starts) const;
		/** @brief Returns the line of a byte offset */
		static std::int32_t GetLineOf(ArrayView<const std::int32_t> starts, std::int32_t offset);
		/** @brief Returns the end of a line, before its line break */
		std::int32_t GetLineEnd(ArrayView<const std::int32_t> starts, std::int32_t line) const;
		/** @brief Returns the height of a line in pixels and where the first line is */
		void GetLineLayout(const UiContext& context, std::int32_t lineCount, float& lineHeight, float& top) const;
		/** @brief Deletes the selection, returns `false` if nothing was selected */
		bool DeleteSelection();
		/** @brief Inserts a text at the caret */
		void Insert(StringView text);
		/** @brief Called after the text changed */
		void NotifyChanged();

		mutable UiContext _lastContext;
	};

	/**
		@brief Number that can be dragged or typed
	*/
	class UiNumberBox : public UiTextBox
	{
		DEATH_RUNTIME_OBJECT(UiTextBox);

	public:
		/** @brief Creates a new instance */
		UiNumberBox(Function<float()>&& getter, Function<void(float, bool)>&& setter, float minValue, float maxValue, float step = 1.0f, std::int32_t decimals = 0);

		/** @brief Returns the value */
		Function<float()> Getter;
		/** @brief Sets the value, the flag tells whether it's final or only shown while dragged */
		Function<void(float, bool)> Setter;
		/** @brief Smallest value */
		float MinValue;
		/** @brief Largest value */
		float MaxValue;
		/** @brief Change of the value for one unit of dragging */
		float Step;
		/** @brief Number of decimal places shown */
		std::int32_t Decimals;

		void Draw(UiPainter& painter) override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		bool OnWheel(const UiPointerEvent& event) override;

		/** @brief Changes the value by a number of steps, as the arrows at its edge do */
		void StepBy(float steps) {
			StepValue(steps);
		}

	private:
		float _dragStartX;
		float _dragStartValue;
		bool _dragging;

		String FormatValue(float value) const;
		// Arrows at the right edge, which step the value by a click or the wheel
		Rectf GetSpinRect() const;
		void StepValue(float steps);
	};
}

#endif
