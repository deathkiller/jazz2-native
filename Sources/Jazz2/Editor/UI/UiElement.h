#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiPainter.h"
#include "../../../nCine/Input/InputEvents.h"

#include <Base/TypeInfo.h>
#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>

#include <memory>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::String;
#endif

	class UiSurface;

	/** @brief What the interface is measured with */
	struct UiContext
	{
		/** @brief Pixels per unit */
		float Scale;

		/**
		 * @brief Returns the font of the interface at a scale of the interface
		 *
		 * The readable font drawn for the scale if @ref UiTheme::ReadableFont is set and it can be read, otherwise the
		 * pixel font of the game. It's not kept anywhere, the fonts that have the palette baked in are dropped when a
		 * level with another palette is loaded, and the readable font of a scale no longer used goes too.
		 */
		static UI::Font* GetFont(float scale);
		/** @brief Converts units to whole pixels */
		float Px(float units) const;
		/** @brief Returns the width of a text in pixels */
		float MeasureText(StringView text) const;
		/** @brief Returns the height of a line of text in pixels */
		float GetTextHeight() const;
	};

	/** @brief A pointer --- the mouse, a finger or a virtual cursor --- over the interface */
	struct UiPointerEvent
	{
		/** @brief Position in pixels of the screen */
		Vector2f Pos;
		/** @brief Button that was pressed or released */
		MouseButton Button = MouseButton::Left;
		/** @brief Number of clicks in a row, 2 for a double click */
		std::int32_t Clicks = 1;
		/** @brief Scrolled distance, positive is up or right */
		Vector2f Wheel;
		/** @brief Modifier keys held */
		std::int32_t Modifiers = 0;
	};

	/** @brief Orientation of a stack */
	enum class UiOrientation : std::uint8_t
	{
		Horizontal,
		Vertical
	};

	/** @brief Space around the content of an element, in units */
	struct UiThickness
	{
		float Left = 0.0f, Top = 0.0f, Right = 0.0f, Bottom = 0.0f;

		constexpr UiThickness() {}
		constexpr UiThickness(float all) : Left(all), Top(all), Right(all), Bottom(all) {}
		constexpr UiThickness(float horizontal, float vertical) : Left(horizontal), Top(vertical), Right(horizontal), Bottom(vertical) {}
		constexpr UiThickness(float left, float top, float right, float bottom) : Left(left), Top(top), Right(right), Bottom(bottom) {}
	};

	/**
		@brief Element of the interface of the level editor

		Elements form a tree. A parent measures and places its children every frame, so whatever an element shows
		is always read from the editor as it is --- nothing has to be told to update. Sizes set on an element are in
		units, its bounds are in pixels of the screen.
	*/
	class UiElement
	{
		DEATH_RUNTIME_OBJECT();

		friend class UiSurface;

	public:
		/** @brief Size that is decided by the content */
		static constexpr float Auto = -1.0f;

		UiElement();
		virtual ~UiElement();

		UiElement(const UiElement&) = delete;
		UiElement& operator=(const UiElement&) = delete;

		/** @brief Bounds in pixels of the screen, set when the element is placed */
		Rectf Bounds;
		/** @brief Whether the element is shown */
		bool Visible;
		/** @brief Whether the element can be used, a disabled element is dimmed */
		bool Enabled;
		/** @brief Width in units, or @ref Auto */
		float Width;
		/** @brief Height in units, or @ref Auto */
		float Height;
		/** @brief How much of the space left in a stack the element takes, relative to the others */
		float Flex;
		/** @brief Text shown when the pointer rests over the element */
		String Tooltip;
		/** @brief Returns whether the element is shown, replaces @ref Visible when set */
		Function<bool()> IsVisibleFunc;
		/** @brief Returns whether the element can be used, replaces @ref Enabled when set */
		Function<bool()> IsEnabledFunc;

		/** @brief Returns the parent, or `nullptr` */
		UiElement* GetParent() const {
			return _parent;
		}
		/** @brief Returns the number of children */
		std::int32_t GetChildCount() const {
			return std::int32_t(_children.size());
		}
		/** @brief Returns a child */
		UiElement* GetChild(std::int32_t index) const {
			return _children[index].get();
		}
		/** @brief Adds a child */
		UiElement* AddChild(std::unique_ptr<UiElement> child);
		/** @brief Creates and adds a child */
		template<class T, class... Args>
		T* Add(Args&&... args) {
			return static_cast<T*>(AddChild(std::make_unique<T>(std::forward<Args>(args)...)));
		}
		/**
		 * @brief Removes all children
		 *
		 * They're taken out of the interface at once, but destroyed only at the start of the next frame (see
		 * @ref UiSurface::Retire()), so a callback of a child may rebuild what the child is in and the child still
		 * finishes what it was doing.
		 */
		void ClearChildren();
		/** @brief Returns the surface the element is shown on, or `nullptr` */
		UiSurface* GetSurface() const;

		/** @brief Returns `true` if the element is shown */
		bool IsShown();
		/** @brief Returns `true` if the element and all its parents can be used */
		bool IsUsable();
		/** @brief Returns `true` if the pointer is over the element */
		bool IsHovered() const {
			return _hovered;
		}
		/** @brief Returns `true` if the element is being pressed */
		bool IsPressed() const {
			return _pressed;
		}
		/** @brief Returns `true` if the element has the keyboard focus */
		bool IsFocused() const {
			return _focused;
		}
		/** @brief Returns how much the element is highlighted by the pointer over it, from 0 to 1, animated */
		float GetHoverAmount() const {
			return _hoverAmount;
		}

		/**
		 * @brief Returns the size the element wants, in pixels
		 *
		 * The available size is what the parent can give, a size without limit is a very large number.
		 */
		virtual Vector2f Measure(const UiContext& context, Vector2f available);
		/**
		 * @brief Returns the size the element wants as @ref Measure() does, measured at most once a frame for the same space
		 *
		 * The layout of a frame asks for the size of an element again and again, once by each of its parents. While
		 * the surface lays out the interface, the size is kept for the rest of the frame, nothing changes then.
		 * Parents should measure their children by this.
		 */
		Vector2f MeasureCached(const UiContext& context, Vector2f available);
		/** @brief Returns `false` if what @ref Measure() returns doesn't depend on the available size, it's measured once a frame then */
		virtual bool MeasureDependsOnAvailable() const;
		/** @brief Places the element and its children */
		virtual void Arrange(const UiContext& context, const Rectf& bounds);
		/** @brief Draws the element and its children */
		virtual void Draw(UiPainter& painter);
		/** @brief Advances animations of the element and its children */
		virtual void Update(float timeMult);

		/** @brief Returns the element under a position that takes the pointer, or `nullptr` */
		virtual UiElement* HitTest(Vector2f pos);
		/** @brief Returns the area the element takes the pointer in, its bounds unless it's grabbed a little outside of them, as a window by its edges */
		virtual Rectf GetHitBounds() const;
		/** @brief Returns `true` if the element takes the pointer itself */
		virtual bool AcceptsPointer() const;
		/** @brief Called when the pointer is pressed over the element, returns `true` to keep getting it until released */
		virtual bool OnPointerDown(const UiPointerEvent& event);
		/** @brief Called before the pointer is pressed over an element inside of this one, on each of its parents */
		virtual void OnPointerDownInside(const UiPointerEvent& event);
		/** @brief Called when the pointer moves while pressed over the element */
		virtual void OnPointerMove(const UiPointerEvent& event);
		/** @brief Called when the pointer is released, `inside` tells whether it's still over the element */
		virtual void OnPointerUp(const UiPointerEvent& event, bool inside);
		/** @brief Called instead of @ref OnPointerUp() when the press was taken away, such as by a finger scrolling the list it's in */
		virtual void OnPointerCancel();
		/**
		 * @brief Returns `true` if a press on the element may become scrolling of the list it's in
		 *
		 * A finger dragged over a button scrolls the list then, the button isn't clicked. Elements that are dragged
		 * themselves keep the press.
		 */
		virtual bool YieldsToScroll() const;
		/**
		 * @brief Returns `true` if a finger has to rest on the element for a moment before it drags it
		 *
		 * A finger dragged at once scrolls the list the element is in instead, and the element gets no moves until it
		 * was held, see @ref OnPointerHold(). A quick tap still clicks it. The mouse drags it at once.
		 */
		virtual bool DragsAfterHold() const;
		/** @brief Called when a finger rested on an element that drags after it's held, its moves come from now on, see @ref DragsAfterHold() */
		virtual void OnPointerHold(const UiPointerEvent& event);
		/** @brief Called when the pointer is scrolled over the element, returns `true` if it was used */
		virtual bool OnWheel(const UiPointerEvent& event);
		/** @brief Called when a key is pressed while the element has the focus, returns `true` if it was used */
		virtual bool OnKeyPressed(const KeyboardEvent& event);
		/** @brief Called when text is typed while the element has the focus */
		virtual bool OnTextInput(const TextInputEvent& event);
		/** @brief Returns `true` if the element can take the keyboard focus */
		virtual bool IsFocusable() const;
		/**
		 * @brief Returns `true` if <kbd>Tab</kbd> can give the element the keyboard focus
		 *
		 * Buttons and other controls are reached by <kbd>Tab</kbd> too, though a click doesn't focus them. Every
		 * element that can take the focus can, by default.
		 */
		virtual bool IsTabStop() const;
		/** @brief Returns `true` if a click on the element, or inside of it, leaves the focus where it is, as the keys of a keyboard on the screen */
		virtual bool KeepsFocus() const;
		/** @brief Called when the element gets or loses the keyboard focus */
		virtual void OnFocusChanged(bool focused);
		/** @brief Returns the tooltip at a position */
		virtual StringView GetTooltip(Vector2f pos) const;

		/** @brief Starts a frame of layout, sizes measured until @ref EndLayout() are kept for it */
		static void BeginLayout();
		/** @brief Ends a frame of layout */
		static void EndLayout();
		/** @brief Returns a number of the current frame of layout, 0 outside of it */
		static std::uint32_t GetLayoutFrame() {
			return _layoutActive ? _layoutFrame : 0;
		}

	protected:
		SmallVector<std::unique_ptr<UiElement>, 0> _children;
		UiElement* _parent;
		UiSurface* _surface;
		bool _hovered;
		bool _pressed;
		bool _focused;
		float _hoverAmount;

		/** @brief Returns the fixed size in pixels, or a negative value for @ref Auto */
		Vector2f GetFixedSize(const UiContext& context) const;

	private:
		static std::uint32_t _layoutFrame;
		static bool _layoutActive;

		// The size measured last, for the frame of layout, the scale and the space it was measured for
		std::uint32_t _measureFrame;
		float _measureScale;
		Vector2f _measureAvailable;
		Vector2f _measureFixed;
		Vector2f _measureSize;
	};

	/**
		@brief Places its children in a row or a column
	*/
	class UiStack : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		explicit UiStack(UiOrientation orientation = UiOrientation::Vertical, float spacing = 0.0f, UiThickness padding = {});

		/** @brief Direction the children are placed in */
		UiOrientation Orientation;
		/** @brief Space between the children in units */
		float Spacing;
		/** @brief Space around the children in units */
		UiThickness Padding;

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;

	private:
		// The space the children were measured in by Measure(), the room they took together and the frame of layout of
		// it, Arrange() measures them in the same space if it gives the same sizes, so they aren't measured again
		Vector2f _childAvailable;
		Vector2f _childContent;
		std::uint32_t _childAvailableFrame = 0;
		// Sizes of the children along the stack, kept so they aren't allocated every frame
		SmallVector<float, 16> _childSizes;
	};

	/** @brief How a panel is drawn */
	enum class UiPanelStyle : std::uint8_t
	{
		None,			/**< Not at all, only the children */
		Panel,			/**< As a panel with a border */
		Floating,		/**< As a panel with a border and a shadow */
		Header,			/**< As the header of a panel */
		Inset			/**< As a sunken area, such as a list */
	};

	/**
		@brief A stack with a background
	*/
	class UiPanel : public UiStack
	{
		DEATH_RUNTIME_OBJECT(UiStack);

	public:
		/** @brief Creates a new instance */
		explicit UiPanel(UiPanelStyle style = UiPanelStyle::Panel, UiOrientation orientation = UiOrientation::Vertical,
			float spacing = UiTheme::Spacing, UiThickness padding = UiThickness(UiTheme::Padding));

		/** @brief How the panel is drawn */
		UiPanelStyle Style;
		/** @brief Whether children that don't fit are cut off at the bounds of the panel */
		bool ClipContent;

		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
	};
}

#endif
