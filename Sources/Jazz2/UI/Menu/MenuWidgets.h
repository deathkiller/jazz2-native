#pragma once

#include "IMenuContainer.h"
#include "Tweening.h"
#include "TextInputBuffer.h"
#include "../Canvas.h"

#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Input/InputEvents.h"

#include <functional>
#include <memory>
#include <utility>

namespace Jazz2::UI::Menu
{
	/**
		@brief Snapshot of navigation actions newly pressed this frame

		Built by @ref WidgetSection from the container's action state and passed to the widget tree, so widgets react
		to navigation without querying the input system directly.
	*/
	struct WidgetInput {
		bool Up;
		bool Down;
		bool Left;
		bool Right;
		bool Fire;
		/** @brief Whether Left is held (not just newly pressed), for hold-to-accelerate widgets such as @ref Slider */
		bool LeftHeld;
		/** @brief Whether Right is held (not just newly pressed) */
		bool RightHeld;
		/** @brief Whether a selection move should play its click sound (throttled during hold-to-repeat scrolling) */
		bool PlayNavSound = true;
	};

	/**
		@brief Base class of a menu widget

		Building block of the declarative menu layout. A widget draws itself within a given rectangle, advances its
		animations, and optionally reacts to navigation or touch. Containers such as @ref StackLayout and @ref ScrollView
		arrange and drive their children, while leaves such as @ref ListItem render content through the shared menu
		painters.
	*/
	class Widget
	{
	public:
		Widget() : Focusable(false), Selected(false), Visible(true) {}
		virtual ~Widget() {}

		/** @brief Rectangle the widget was last drawn in (used for touch hit-testing) */
		Rectf Bounds;
		/** @brief Whether the widget can receive selection */
		bool Focusable;
		/** @brief Whether the widget is currently selected (set by the parent before drawing) */
		bool Selected;
		/** @brief Whether the widget is laid out and drawn */
		bool Visible;

		/** @brief Returns the height the widget occupies in a vertical layout */
		virtual float GetHeight() const {
			return 0.0f;
		}
		/** @brief Advances the widget's animations */
		virtual void OnUpdate(float timeMult) {}
		/** @brief Draws the widget within the given bounds */
		virtual void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) {}
		/** @brief Handles navigation input; returns `true` if it was consumed */
		virtual bool OnNavigate(const WidgetInput& input, IMenuContainer* root) {
			return false;
		}
		/** @brief Handles a touch event; returns `true` if it was consumed */
		virtual bool OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root) {
			return false;
		}
		/** @brief Called when the widget is activated (e.g., by Fire or a tap) */
		virtual void Activate(IMenuContainer* root) {}
		/** @brief Called when the widget becomes selected */
		virtual void OnSelected() {}
		/** @brief Handles a key press while capturing text input; returns `true` if consumed */
		virtual bool OnKeyPressed(const nCine::KeyboardEvent& event, IMenuContainer* root) {
			return false;
		}
		/** @brief Handles a text input event while capturing text input */
		virtual void OnTextInput(const nCine::TextInputEvent& event) {}
		/** @brief Returns `true` while the widget (or a descendant) is capturing raw text input, which restricts navigation */
		virtual bool IsCapturingInput() const {
			return false;
		}
	};

	/**
		@brief Selectable text item

		Single line of text that can be selected and activated. The selected item animates with the standard elastic
		pop and is drawn through @ref IMenuContainer::DrawMenuListItem.
	*/
	class ListItem : public Widget
	{
	public:
		/** @brief Displayed text */
		String Text;
		/** @brief Invoked when the item is activated */
		std::function<void()> OnActivate;
		/** @brief Selection animation progress */
		AnimatedValue Animation;
		/** @brief Height of the item row */
		float Height;

		ListItem(StringView text, std::function<void()> onActivate, float height = 40.0f)
			: Text(text), OnActivate(std::move(onActivate)), Height(height)
		{
			Focusable = true;
		}

		float GetHeight() const override {
			return Height;
		}
		void OnUpdate(float timeMult) override;
		void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override;
		void Activate(IMenuContainer* root) override;
		void OnSelected() override {
			Animation.Restart(0.0f);
		}
	};

	/**
		@brief Setting row that cycles through values

		Draws a label with the current value (and `<` `>` arrows) below it. Left/Right/Fire change the value through the
		@ref OnChange callback (a no-op when read-only). Used for boolean toggles and multi-value options.
	*/
	class ChoiceItem : public Widget
	{
	public:
		/** @brief Setting label */
		String Label;
		/** @brief Whether the value can be changed */
		bool ReadOnly;
		/** @brief Returns the current value text */
		std::function<StringView()> Value;
		/** @brief Changes the value (direction is -1 for left, +1 for right/fire) */
		std::function<void(std::int32_t)> OnChange;
		/** @brief Selection animation progress */
		AnimatedValue Animation;
		/** @brief Extra horizontal spacing of the `<` `>` arrows, for rows with wider values */
		float ArrowSpacing = 0.0f;
		/** @brief Height of the item row */
		float Height = 52.0f;

		ChoiceItem(StringView label, std::function<StringView()> value, std::function<void(std::int32_t)> onChange, bool readOnly = false)
			: Label(label), ReadOnly(readOnly), Value(std::move(value)), OnChange(std::move(onChange))
		{
			Focusable = true;
		}

		float GetHeight() const override {
			return Height;
		}
		void OnUpdate(float timeMult) override;
		void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override;
		bool OnNavigate(const WidgetInput& input, IMenuContainer* root) override;
		bool OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root) override;
		void OnSelected() override {
			Animation.Restart(0.0f);
		}
	};

	/**
		@brief Editable text row

		Shows a label with the current text value below it. Activating it (Fire) starts editing (unless read-only or
		not editable), capturing raw key/text input and showing a blinking caret; Enter commits through @ref OnCommit
		and Escape cancels. Used for the player name field.
	*/
	class TextInput : public Widget
	{
	public:
		/** @brief Setting label */
		String Label;
		/** @brief Returns the value to display when not editing */
		std::function<String()> Value;
		/** @brief Returns `true` if the value can be edited (e.g. `false` when overridden by an external account) */
		std::function<bool()> CanEdit;
		/** @brief Returns `true` if the external-account icon should be drawn next to the value */
		std::function<bool()> ShowIcon;
		/** @brief Invoked with the new text when editing is committed */
		std::function<void(StringView)> OnCommit;
		/** @brief Invoked when editing starts (`true`) or ends (`false`), e.g. to manage the on-screen keyboard */
		std::function<void(bool)> OnEditStateChanged;
		/** @brief Glyph drawn to the left of the value when @ref ShowIcon returns `true` */
		String Icon;
		/** @brief Whether the value is read-only (greyed, not editable) */
		bool ReadOnly = false;
		/** @brief Height of the item row */
		float Height = 56.0f;
		/** @brief Selection animation progress */
		AnimatedValue Animation;

		TextInput(StringView label, std::uint32_t maxLength)
			: Label(label), _buffer(maxLength), _active(false)
		{
			Focusable = true;
		}

		/** @brief Returns `true` while the field is in editing mode */
		bool IsActive() const {
			return _active;
		}
		/** @brief Returns the text currently being edited */
		StringView GetText() const {
			return _buffer.GetText();
		}
		/** @brief Returns the caret position */
		std::size_t GetCursor() const {
			return _buffer.GetCursor();
		}
		/** @brief Returns the caret blink animation phase */
		float GetCaretAnim() const {
			return _buffer.GetCaretAnim();
		}
		/** @brief Cancels editing without committing */
		void Cancel(IMenuContainer* root);
		/** @brief Commits the current text if non-empty */
		void Commit(IMenuContainer* root);

		float GetHeight() const override {
			return Height;
		}
		void OnUpdate(float timeMult) override;
		void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override;
		void Activate(IMenuContainer* root) override;
		bool OnNavigate(const WidgetInput& input, IMenuContainer* root) override;
		bool OnKeyPressed(const nCine::KeyboardEvent& event, IMenuContainer* root) override;
		void OnTextInput(const nCine::TextInputEvent& event) override;
		bool IsCapturingInput() const override {
			return _active;
		}
		void OnSelected() override {
			Animation.Restart(0.0f);
		}

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		TextInputBuffer _buffer;
		bool _active;
#endif
	};

	/**
		@brief Setting row with a custom-drawn value

		Like @ref ChoiceItem but draws its value through a caller-provided callback instead of plain text, for rows such
		as colour swatch pickers or a formatted identifier. Optionally cycles via @ref OnChange (showing arrows) or
		activates via @ref OnActivate (e.g. copying to the clipboard).
	*/
	class CustomValueItem : public Widget
	{
	public:
		/** @brief Setting label */
		String Label;
		/** @brief Whether the row is read-only (greyed, non-interactive) */
		bool ReadOnly = false;
		/** @brief Extra horizontal spacing of the `<` `>` arrows */
		float ArrowSpacing = 0.0f;
		/** @brief Draws the value area (already offset below the label) */
		std::function<void(IMenuContainer* root, float centerX, float y, std::int32_t& charOffset, bool selected, bool readOnly)> DrawValue;
		/** @brief If set, Left/Right/Fire cycle the value (direction -1/+1) and arrows are shown */
		std::function<void(std::int32_t)> OnChange;
		/** @brief If set (and no @ref OnChange), Fire activates the row */
		std::function<void()> OnActivate;
		/** @brief Selection animation progress */
		AnimatedValue Animation;
		/** @brief Height of the item row */
		float Height;

		CustomValueItem(StringView label, float height = 52.0f)
			: Label(label), Height(height)
		{
			Focusable = true;
		}

		float GetHeight() const override {
			return Height;
		}
		void OnUpdate(float timeMult) override;
		void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override;
		bool OnNavigate(const WidgetInput& input, IMenuContainer* root) override;
		bool OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root) override;
		void OnSelected() override {
			Animation.Restart(0.0f);
		}
	};

	/**
		@brief Row drawn entirely by a caller-provided callback

		Escape hatch for bespoke screens: the row's drawing and activation are delegated to callbacks, so a section can
		render arbitrary content (icons, multi-column rows, custom glows) while still living in the widget tree and
		getting selection, navigation, scrolling and transitions for free.
	*/
	class CanvasWidget : public Widget
	{
	public:
		/** @brief Draws the row content within the given bounds */
		std::function<void(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool selected, float animation)> OnDrawContent;
		/** @brief Invoked when the row is activated (Fire or tap) */
		std::function<void()> OnActivate;
		/** @brief Invoked when the row becomes selected (e.g. for a live preview) */
		std::function<void()> OnSelectedChanged;
		/** @brief Invoked when the row is tapped, with the touch position in pixels (for bespoke hit-testing such as columns) */
		std::function<void(Vector2i touchPos)> OnTouch;
		/** @brief Selection animation progress */
		AnimatedValue Animation;
		/** @brief Height of the row */
		float Height;

		CanvasWidget(float height, bool focusable = true)
			: Height(height)
		{
			Focusable = focusable;
		}

		float GetHeight() const override {
			return Height;
		}
		void OnUpdate(float timeMult) override;
		void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override;
		void Activate(IMenuContainer* root) override;
		bool OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root) override;
		void OnSelected() override;
	};

	/**
		@brief Setting row with a value bar adjusted left/right

		Draws a label above a 33-block bar showing a `[0, 1]` value. Left/Right (held, with hold-to-accelerate) adjust
		the value through the @ref OnAdjust callback; touching the left/right half of the bar nudges it. Used for the
		audio volume sliders.
	*/
	class Slider : public Widget
	{
	public:
		/** @brief Setting label */
		String Label;
		/** @brief Returns the current value in the `[0, 1]` range */
		std::function<float()> Value;
		/** @brief Applies a clamped delta to the value (the callback persists/applies the change) */
		std::function<void(float)> OnAdjust;
		/** @brief Selection animation progress */
		AnimatedValue Animation;

		Slider(StringView label, std::function<float()> value, std::function<void(float)> onAdjust)
			: Label(label), Value(std::move(value)), OnAdjust(std::move(onAdjust)), _pressedCooldown(0.0f), _pressedCount(0)
		{
			Focusable = true;
		}

		float GetHeight() const override {
			return 70.0f;
		}
		void OnUpdate(float timeMult) override;
		void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override;
		bool OnNavigate(const WidgetInput& input, IMenuContainer* root) override;
		bool OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root) override;
		void OnSelected() override {
			Animation.Restart(0.0f);
		}

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr float StepSize = 0.03f;
		static constexpr std::int32_t BlockCount = 33;

		float _pressedCooldown;
		std::int32_t _pressedCount;
#endif

		void Apply(IMenuContainer* root, float delta);
	};

	/**
		@brief Base of vertical lists of selectable children

		Owns the children and the selected index, and implements Up/Down (wrapping) navigation plus Fire activation.
		Subclasses provide the actual layout/drawing: @ref StackLayout centers a list that fits, while @ref ScrollView
		scrolls a list that overflows.
	*/
	class ListContainer : public Widget
	{
	public:
		ListContainer() : _selectedIndex(-1) {}

		/** @brief Adds a child widget and returns a pointer to it */
		template<class T, typename... Params>
		T* Add(Params&&... args) {
			auto child = std::make_unique<T>(std::forward<Params>(args)...);
			T* ptr = child.get();
			if (_selectedIndex < 0 && ptr->Focusable) {
				_selectedIndex = (std::int32_t)_children.size();
			}
			_children.push_back(std::move(child));
			return ptr;
		}

		/** @brief Returns the index of the selected child, or -1 if none */
		std::int32_t GetSelectedIndex() const {
			return _selectedIndex;
		}
		/** @brief Selects the focusable child at the given index; `centerInView` reveals it centered (for initial pre-selection), otherwise only scrolls it into view when it passes a viewport edge */
		void SetSelectedIndex(std::int32_t index, bool centerInView = true);

		float GetHeight() const override;
		void OnUpdate(float timeMult) override;
		bool OnNavigate(const WidgetInput& input, IMenuContainer* root) override;
		// Route text-entry input to the selected child (e.g. an editing TextInput)
		bool OnKeyPressed(const nCine::KeyboardEvent& event, IMenuContainer* root) override;
		void OnTextInput(const nCine::TextInputEvent& event) override;
		bool IsCapturingInput() const override;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		SmallVector<std::unique_ptr<Widget>> _children;
		std::int32_t _selectedIndex;
#endif

		void MoveSelection(std::int32_t direction, IMenuContainer* root, bool playSound = true);
		void SelectAt(std::int32_t index, IMenuContainer* root);
		/** @brief Called when the selection moves, so a scrolling container can keep it visible */
		virtual void OnSelectionMoved() {}
		/** @brief Called when the selection is set programmatically, so a scrolling container can reveal it */
		virtual void OnSelectionSet() {}
	};

	/**
		@brief Vertical list that centers its children within its bounds

		Suitable for short menus that always fit on screen. Selection is moved with Up/Down/Fire and items can be tapped.
	*/
	class StackLayout : public ListContainer
	{
	public:
		/** @brief When set, distributes the children across the full height (top-weighted) instead of centering them tightly */
		bool Spread = false;

		void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override;
		bool OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root) override;
	};

	/**
		@brief Vertical list that scrolls when its children overflow the available area

		Centers the content when it fits, otherwise clamps a scroll offset and supports kinetic touch dragging,
		keyboard scrolling (keeping the selected item visible), and scroll-edge fade glows.
	*/
	class ScrollView : public ListContainer
	{
	public:
		ScrollView();

		/** @brief Empty space reserved before the first and after the last item so they aren't flush against the frame; sections with oversized rows can raise it */
		float ContentPadding = 8.0f;

		void OnUpdate(float timeMult) override;
		void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override;
		bool OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root) override;

	protected:
		void OnSelectionMoved() override;
		void OnSelectionSet() override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr float ItemSpacing = 40.0f;
		static constexpr float TouchKineticDivider = 0.004f;
		static constexpr float TouchKineticFriction = 7000.0f;
		static constexpr float TouchKineticDamping = 0.2f;

		float _scrollY;
		float _contentHeight;
		float _availableHeight;
		float _bandTop;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		float _touchSpeed;
		std::int8_t _touchDirection;
		bool _scrollable;
		bool _ensureVisibleRequested;
#endif

		void EnsureVisibleSelected();
	};
}
