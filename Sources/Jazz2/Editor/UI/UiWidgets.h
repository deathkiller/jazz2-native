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

		/** @brief Returns the text shown, @ref TextFunc is called at most once a frame of layout */
		StringView GetText();

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		void Draw(UiPainter& painter) override;

	private:
		String _frameText;
		std::uint32_t _frameTextLayout = 0;
	};

	/**
		@brief Text on as many lines as it needs in the width it gets

		A label would be cut off. Lines are broken at spaces and new lines, a word wider than the width gets a line of
		its own. It's always aligned to the left.
	*/
	class UiWrappedLabel : public UiLabel
	{
		DEATH_RUNTIME_OBJECT(UiLabel);

	public:
		/** @brief Creates a new instance */
		explicit UiWrappedLabel(StringView text = {}, const Colorf& color = UiTheme::Text);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		void Draw(UiPainter& painter) override;

	private:
		// Lines of a text broken in a width, as byte ranges of the text, kept until the text, the width, the size of the
		// text or the font changes
		struct WrappedLines
		{
			String Text;
			float Width = -1.0f;
			float TextHeight = -1.0f;
			std::uint32_t FontVersion = 0;
			SmallVector<std::pair<std::uint32_t, std::uint32_t>, 4> Lines;
			float Widest = 0.0f;
		};

		// One for measuring and one for drawing, they may be in other widths
		WrappedLines _measured;
		WrappedLines _drawn;

		template<class TMeasurer>
		const WrappedLines& GetLines(WrappedLines& cache, const TMeasurer& measurer, float width);
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
		/** @brief Room added between the icon and the text, in units */
		float IconGap = 0.0f;
		/** @brief Where the text and the icon are in a button wider than them, centered, at the left or at the right */
		Alignment TextAlign;
		/** @brief Color of the text instead of the usual one, a text color (see @ref UiTheme::Text), unused if its alpha is 0 */
		Colorf TextColor;
		/**
		 * @brief Returns whether only the icon is shown, instead of when the text doesn't fit (see @ref IsTextCut())
		 *
		 * So buttons next to each other show their texts or only their icons all alike.
		 */
		Function<bool()> IconOnlyFunc;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		bool MeasureDependsOnAvailable() const override;
		/** @brief Shows only the icon of a button with both that is narrower than them, centered, the text is its tooltip then */
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		bool YieldsToScroll() const override;
		/** @brief Reached by <kbd>Tab</kbd>, <kbd>Enter</kbd> or <kbd>Space</kbd> clicks it then */
		bool IsTabStop() const override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
		StringView GetTooltip(Vector2f pos) const override;

		/** @brief Returns `true` if the button is shown checked */
		bool IsChecked();
		/** @brief Returns `true` if the icon and the text didn't fit into the button when it was drawn last */
		bool IsTextCut() const {
			return (_neededWidth > Bounds.W + 0.5f);
		}

	private:
		// Only the icon was shown when the button was drawn last
		bool _textHidden;
		// How wide the icon and the text are with the padding, as of the last time the button was drawn
		float _neededWidth;
		// Width of the text, for the text, the size of text and the font it was measured for, it's measured and drawn every frame
		String _measuredText;
		float _measuredTextHeight = -1.0f;
		float _measuredTextWidth = 0.0f;
		std::uint32_t _measuredFontVersion = 0;

		template<class TMeasurer>
		float GetTextWidth(const TMeasurer& measurer);
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
		bool MeasureDependsOnAvailable() const override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		bool YieldsToScroll() const override;
		/** @brief Reached by <kbd>Tab</kbd>, <kbd>Space</kbd> checks or unchecks it then */
		bool IsTabStop() const override;
		bool OnKeyPressed(const KeyboardEvent& event) override;
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
		bool MeasureDependsOnAvailable() const override;
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
		/** @brief Returns `true` if the content is larger than what is shown of it */
		bool IsScrollable() const {
			return (_contentHeight > Bounds.H);
		}
		/** @brief Scrolls so that a part of the content in pixels from its top is visible */
		void ScrollIntoView(float top, float bottom);
		/** @brief Scrolls to a distance from the top in pixels, at once or gliding there */
		void ScrollTo(float scroll, bool immediate);
		/** @brief Goes on scrolling at a speed in pixels a frame, slowing down by itself, as after a flick of a finger, nothing with reduced motion */
		void Fling(float velocity);

		/**
		 * @brief Whether the view stays at the end of the content while it grows, as a log does
		 *
		 * It starts at the end, and follows the content only while it's scrolled to the end, so a part scrolled back to
		 * stays in view.
		 */
		bool FollowEnd = false;
		/**
		 * @brief Whether the bar is only a thin mark over the edge of the content, which keeps the whole width
		 *
		 * For a strip too narrow to give the bar room of its own. The bar can't be dragged then, the content is scrolled
		 * by the wheel or a finger.
		 */
		bool OverlayBar = false;
		/** @brief How far right of the edge of the content the thin mark of @ref OverlayBar is moved, in units, into the padding around it */
		float OverlayBarOffset = 0.0f;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;
		void Draw(UiPainter& painter) override;
		void Update(float timeMult) override;
		UiElement* HitTest(Vector2f pos) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		void OnPointerCancel() override;
		bool OnWheel(const UiPointerEvent& event) override;

	private:
		float _scroll;
		float _targetScroll;
		// Speed of a flick in pixels a frame, toward the start of the content when positive
		float _velocity = 0.0f;
		float _contentHeight;
		float _dragOffset;
		bool _draggingThumb;
		// The width the content was measured in by Measure(), its height then and the frame of layout of it
		float _measuredWidth = 0.0f;
		float _measuredHeight = 0.0f;
		std::uint32_t _measuredFrame = 0;

		Rectf GetThumbRect() const;
		float GetMaxScroll() const;
		// Width of the edge that takes the thumb, wider for a finger
		float GetThumbZoneWidth() const;
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
		void Update(float timeMult) override;
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
		/** @brief Whether the arrows of moving are shown by the pointer while it's over the row, `false` when dragging it doesn't move anything now */
		bool ShowMoveArrows = true;

		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		void Update(float timeMult) override;

	private:
		Vector2f _startPos;
		// Whether it was dragged since it was pressed
		bool _moved = false;
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
		/** @brief Narrowest the column of labels is in units, so it doesn't change as rows are shown and hidden, or between forms */
		float MinLabelWidth;

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
		/** @brief Returns the width of the label in whole pixels */
		float GetLabelWidth(const UiContext& context) const;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;

	private:
		UiForm& _form;
		UiLabel* _label;
		UiStack* _content;
		// The width of the label, measured once a frame of layout
		mutable float _labelWidth = 0.0f;
		mutable float _labelWidthScale = 0.0f;
		mutable std::uint32_t _labelWidthLayout = 0;

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
		~UiTextBox() override;

		/** @brief Text */
		String Text;
		/** @brief Text shown while it's empty */
		String Placeholder;
		/** @brief Returns the text shown while it's empty, replaces @ref Placeholder when set */
		Function<String()> PlaceholderFunc;
		/** @brief Returns the text to show while it's not being edited, replaces @ref Text then */
		Function<String()> TextFunc;
		/** @brief Called when the edit is confirmed, by Enter or by leaving it, only if something was typed since the edit started */
		Function<void(StringView)> OnCommit;
		/** @brief Called whenever the text changes while it's edited */
		Function<void(StringView)> OnChanged;
		/** @brief Gets the keys first while it's edited, returns `true` for a key it used, such as a dialog moving through a list by <kbd>Up</kbd> */
		Function<bool(const KeyboardEvent&)> OnKeyPreview;
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
		bool MeasureDependsOnAvailable() const override;
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
		// The text when the edit started, the edit is confirmed only if the text is another one when it ends
		String _editStartText;
		// The text from TextFunc for the frame of layout, see GetShownText()
		String _shownText;
		std::uint32_t _shownTextLayout = 0;

		/** @brief Returns the text shown, the one being edited, or the one from @ref TextFunc made at most once a frame of layout */
		StringView GetShownText();

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
		/** @brief Inserts a text at the caret, characters a text can't have are left out */
		void Insert(StringView text);
		/** @brief Called after the text changed */
		void NotifyChanged();
		/** @brief Keeps the caret and the start of the selection inside of the text, it may have been replaced */
		void ClampCaret();
		/** @brief Returns the byte offset of the start of the next word, or of the word before */
		std::int32_t FindWordBoundary(std::int32_t offset, bool forward) const;

		mutable UiContext _lastContext;

	private:
		// What an edit does, edits of the same kind in a row are undone together
		enum class EditKind : std::uint8_t
		{
			None,
			Typing,
			Deleting,
			Other
		};

		struct EditState
		{
			String Text;
			std::int32_t Caret;
			std::int32_t SelectionStart;
		};

		static constexpr std::size_t MaxUndoSteps = 100;

		SmallVector<EditState, 0> _undo;
		SmallVector<EditState, 0> _redo;
		EditKind _lastEdit = EditKind::None;

		void RecordUndo(EditKind kind);
		bool Undo();
		bool Redo();
		// Cleared when the box is destroyed, what finishes later --- a keyboard of the system --- tells by it
		std::shared_ptr<bool> _alive;
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
		/** @brief Returns the value the middle button (the wheel pressed) resets it to, it isn't reset if empty */
		Function<float()> DefaultValue;

		void Draw(UiPainter& painter) override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		void OnPointerCancel() override;
		bool OnWheel(const UiPointerEvent& event) override;
		/** @brief A click doesn't type into it, the number is dragged or stepped then, it's typed into by a click without moving, see @ref BeginEdit() */
		bool IsFocusable() const override;
		/** @brief Reached by <kbd>Tab</kbd> to type the number, <kbd>Up</kbd> and <kbd>Down</kbd> step it then */
		bool IsTabStop() const override;
		bool OnKeyPressed(const KeyboardEvent& event) override;

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

	/**
		@brief Edges a window is resized by, for the windows that can be

		An edge is grabbed a little outside of the window too, by half of its width. The corner at the bottom right,
		which has the dots, is easier to grab. A corner is two edges.
	*/
	struct UiResizeEdges
	{
		enum : std::uint8_t {
			Left = 0x01,		/**< Left edge */
			Top = 0x02,			/**< Top edge */
			Right = 0x04,		/**< Right edge */
			Bottom = 0x08		/**< Bottom edge */
		};

		/** @brief Width of an edge in units */
		static constexpr float GripWidth = 5.0f;

		/** @brief Returns the area a window takes the pointer in with its edges, a little larger than it */
		static Rectf GetHitBounds(const Rectf& bounds, float scale);
		/** @brief Returns the edges of a window at a position, @p cornerSize is how far the corner reaches, in widths of an edge */
		static std::uint8_t GetAt(const Rectf& bounds, float scale, Vector2f pos, float cornerSize);
		/** @brief Draws the edges being pointed at or dragged lit up, and the dots in the corner */
		static void Draw(UiPainter& painter, const Rectf& bounds, std::uint8_t edges);
		/** @brief Shows the arrows of resizing by the pointer for the edges being pointed at or dragged, nothing for none */
		static void ShowCursor(UiSurface& surface, std::uint8_t edges);
	};

	/** @brief Returns a color between two others, 0 for the first and 1 for the second */
	Colorf UiLerpColor(const Colorf& a, const Colorf& b, float t);
}

#endif
