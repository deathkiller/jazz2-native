#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiElement.h"
#include "UiSkin.h"

namespace Jazz2::Rendering
{
	class PolylineRenderer;
}

namespace Jazz2::Editor
{
	class UiScrollView;
	class UiMenuBar;
	class UiReadableFont;

	/** @brief What is shown by the pointer, as the cursor of a window would show it, see @ref UiSurface::SetCursorOverlay() */
	enum class UiCursorOverlay : std::uint8_t
	{
		None,					/**< Nothing */
		ResizeHorizontal,		/**< Left and right, a vertical edge */
		ResizeVertical,			/**< Up and down, a horizontal edge */
		ResizeDiagonalNWSE,		/**< Top left and bottom right corners */
		ResizeDiagonalNESW,		/**< Top right and bottom left corners */
		Move					/**< All four directions, something moved as a whole */
	};

	/**
		@brief How an element shown over the interface behaves

		An element that closes by a click outside of it and isn't modal is a popup --- a menu, a list dropped down,
		a picker. The rest are windows, which stay until they're closed, see @ref UiSurface::ClosePopups().
	*/
	struct UiOverlayOptions
	{
		/** @brief Position of the top left corner, it's moved to fit the screen */
		Vector2f Position;
		/** @brief Whether it's centered on the screen instead */
		bool Centered = false;
		/** @brief Whether nothing else can be used while it's shown */
		bool Modal = false;
		/** @brief Whether a click outside of it closes it, which makes it a popup unless it's modal */
		bool CloseOnOutsideClick = true;
		/** @brief Whether the virtual cursor is hidden while it's shown, it's driven by the stick directly */
		bool HidesVirtualCursor = false;
		/** @brief Element shown over the interface it's shown from, it's closed with it, as a submenu with its menu */
		UiElement* Owner = nullptr;
		/** @brief Right edge it's put at instead if it doesn't fit right of its position, as a submenu goes left of its menu then, not used if negative */
		float FallbackRight = -1.0f;
		/** @brief Called when it's closed */
		Function<void()> OnClosed;

		/** @brief Returns `true` if it's a popup, not a window */
		bool IsPopup() const {
			return (CloseOnOutsideClick && !Modal);
		}
	};

	/**
		@brief Shows the interface of the level editor and passes input to it

		The surface is drawn over the whole window at its own scale, in pixels of the screen. It holds the tree of
		the interface and the elements shown over it --- menus, dialogs, tooltips. Whatever the pointer does over the
		interface goes to it, the rest is left to the view of the level.
	*/
	class UiSurface : public UI::Canvas
	{
	public:
		/** @brief Number of slots the root draws in, the one it's given and those after it, for panels floating over the others */
		static constexpr std::int32_t RootSlots = 4;

		UiSurface();
		~UiSurface() override;

		UiSurface(const UiSurface&) = delete;
		UiSurface& operator=(const UiSurface&) = delete;

		/** @brief Sets the root of the interface */
		template<class T, class... Args>
		T* SetRoot(Args&&... args) {
			return static_cast<T*>(SetRoot(std::make_unique<T>(std::forward<Args>(args)...)));
		}
		/** @brief Sets the root of the interface */
		UiElement* SetRoot(std::unique_ptr<UiElement> root);
		/** @brief Returns the root of the interface */
		UiElement* GetRoot() const {
			return _root.get();
		}

		/**
		 * @brief Shows an element over the interface
		 *
		 * A modal element takes the keyboard focus away from everything under it, a text being typed there is
		 * confirmed first.
		 */
		UiElement* PushOverlay(std::unique_ptr<UiElement> element, UiOverlayOptions&& options);
		/** @brief Closes an element shown over the interface, it's destroyed at the end of the frame */
		void CloseOverlay(UiElement* element);
		/**
		 * @brief Moves an element shown over the interface, its top left corner, it's no longer centered then
		 *
		 * See @ref UiOverlayOptions::FallbackRight for @p fallbackRight.
		 */
		void MoveOverlay(UiElement* element, Vector2f position, float fallbackRight = -1.0f);
		/**
		 * @brief Brings a window shown over the interface in front of the other windows and gives it the keyboard
		 *
		 * It stays under modal elements shown after it. A focus outside of it is taken away, so keys go to it. Returns
		 * `false` if the element isn't shown over the interface.
		 */
		bool RaiseOverlay(UiElement* element);
		/** @brief Closes all popups --- menus, lists and pickers --- windows stay, see @ref UiOverlayOptions */
		void ClosePopups();
		/** @brief Returns `true` if a modal element is shown */
		bool HasModal() const;
		/** @brief Returns `true` if any element is shown over the interface */
		bool HasOverlays() const;
		/** @brief Returns `true` if an element is shown over the interface and isn't closing */
		bool IsOverlayShown(const UiElement* element) const;

		/** @brief Returns the scale of the interface, pixels per unit */
		float GetScale() const {
			return _scale;
		}
		/** @brief Sets the scale of the interface */
		void SetScale(float scale);
		/** @brief Returns what the interface is measured with */
		UiContext GetContext() const;
		/** @brief Returns the font of the interface, filtered as the scale needs, see @ref UiContext::GetFont() */
		UI::Font* GetFont() const;
		/**
		 * @brief Returns the readable font for text drawn at a scale, see @ref UiReadableFont::Get()
		 *
		 * The surface keeps it. Returns `nullptr` if there's no surface or the font can't be read.
		 */
		static UI::Font* GetReadableFont(float textScale);
		/** @brief Returns the size of the screen in pixels */
		Vector2f GetScreenSize() const;
		/**
		 * @brief Returns the part of the screen the interface is kept in, in pixels, see @ref SafeMargins
		 *
		 * The whole screen unless the margins are set. Windows, menus and messages are placed inside of it, and the root
		 * is expected to place its panels there too, it's given the whole screen.
		 */
		Rectf GetSafeBounds() const;

		/** @brief Passes a press of the pointer, returns `true` if the interface took it */
		bool OnPointerDown(const UiPointerEvent& event);
		/** @brief Passes a move of the pointer, returns `true` if it's over the interface or the interface holds it */
		bool OnPointerMove(const UiPointerEvent& event);
		/** @brief Passes a release of the pointer, returns `true` if the interface took it */
		bool OnPointerUp(const UiPointerEvent& event);
		/** @brief Passes scrolling, returns `true` if the interface took it */
		bool OnWheel(const UiPointerEvent& event);
		/** @brief Passes a key, returns `true` if the interface took it */
		bool OnKeyPressed(const KeyboardEvent& event);
		/** @brief Passes typed text, returns `true` if the interface took it */
		bool OnTextInput(const TextInputEvent& event);
		/** @brief Returns `true` if the interface is under a position */
		bool IsOverInterface(Vector2f pos);
		/** @brief Returns `true` if a finger at a position is the interface's at once, not one that may still pan the view, see @ref UiElement::YieldsToViewGestures() */
		bool TakesTouchAt(Vector2f pos);
		/**
		 * @brief Follows the pointer while its moves go elsewhere
		 *
		 * Called for every move, also while the view or a tool holds the pointer and the interface doesn't get the moves,
		 * so what is shown by the pointer, such as the cursor overlay, stays with it.
		 */
		void TrackPointer(Vector2f pos);
		/** @brief Returns the last position of the pointer */
		Vector2f GetPointerPos() const {
			return _pointerPos;
		}
		/** @brief Returns `true` if the interface holds the pointer */
		bool HasCapture() const {
			return (_capture != nullptr);
		}
		/** @brief Lets go of the pointer the interface holds, the element that holds it is told it was cancelled, not released */
		void CancelPointerCapture();

		/** @brief Gives the keyboard focus to an element, or takes it away */
		void SetFocus(UiElement* element);
		/** @brief Returns the element with the keyboard focus, or `nullptr` */
		UiElement* GetFocus() const {
			return _focus;
		}
		/**
		 * @brief Gives the focus to the next element <kbd>Tab</kbd> reaches, or the previous one
		 *
		 * Only inside of a modal element if one is shown, otherwise inside of where the focus is, of the top element
		 * shown over the interface, or of the panels. Returns `false` if there's nothing to go to.
		 */
		bool MoveFocus(bool backwards);
		/** @brief Scrolls the list an element is in so the element is seen */
		void EnsureVisible(UiElement* element);
		/** @brief Returns the element under the pointer, or `nullptr` */
		UiElement* GetHover() const {
			return _hover;
		}

		/** @brief Shows a short message for a while */
		void ShowToast(StringView text, EditorIcon icon = EditorIcon::Info);

		/**
		 * @brief Shows an icon by the pointer for this frame, as the cursor of a window would show it
		 *
		 * Called every frame by whatever is pointed at or dragged --- in @ref UiElement::Update() or in
		 * @ref UiElement::Draw() --- it's forgotten after the frame is drawn, so it goes away by itself once nothing
		 * asks for it. The last call in a frame wins. It's drawn over everything at a distance from the pointer that
		 * grows with the scale of the interface, and not at all for a finger.
		 */
		void SetCursorOverlay(UiCursorOverlay overlay);
		/** @brief Returns what is shown by the pointer in this frame so far */
		UiCursorOverlay GetCursorOverlay() const {
			return _cursorOverlay;
		}
		/** @brief Returns the arrows for resizing by edges, a corner if one edge of each direction is set, @ref UiCursorOverlay::None if none is */
		static UiCursorOverlay GetResizeOverlay(bool left, bool top, bool right, bool bottom);
		/**
		 * @brief Returns how far below the pointer a label shown by it starts, in pixels
		 *
		 * Below the cursor overlay set so far in this frame, so it doesn't cover the label, or @p distance if none is.
		 */
		float GetPointerLabelOffset(float distance) const;
		/**
		 * @brief Shows a tooltip by the pointer for this frame, at once
		 *
		 * Called every frame by whatever is dragged and tells something of it, such as the size it gives --- as
		 * @ref SetCursorOverlay(), it's forgotten after the frame is drawn and the last call in a frame wins. It looks
		 * and is placed as the tooltip of an element, which it's shown instead of, and also while the pointer is held.
		 */
		void SetPointerTooltip(StringView text);

		/** @brief Draws over all of the interface, in a slot of its own and the slot after it, with the size of the screen */
		Function<void(UiPainter&, Vector2f)> DrawOver;

		/** @brief Whether a cursor moved by a gamepad is shown */
		bool ShowVirtualCursor = false;
		/** @brief Position of the cursor moved by a gamepad, in pixels of the screen */
		Vector2f VirtualCursor;
		/**
		 * @brief Whether the pointer is a finger
		 *
		 * Nothing is pointed at without touching then. A tooltip is shown by holding a finger still, and a finger
		 * dragged over a list scrolls it.
		 */
		bool TouchInput = false;
		/** @brief Whether windows that aren't modal are hidden, as while a level is played, they're shown again after */
		bool WindowsHidden = false;
		/**
		 * @brief Part of each edge of the screen the interface keeps clear of, as a fraction of its width or height
		 *
		 * For a display that cuts off the edges of the picture, as a television can. What is behind the interface, the
		 * level, still takes the whole screen. See @ref GetSafeBounds().
		 */
		UiThickness SafeMargins;
		/**
		 * @brief Returns `true` for a key that runs a shortcut even while a text is typed, such as saving
		 *
		 * The text is confirmed first, then the key goes on to the shortcuts as if nothing was typed into.
		 */
		Function<bool(const KeyboardEvent&)> IsPassThroughKey;
		/**
		 * @brief Returns `true` for a key a text being typed doesn't take, it goes on to the shortcuts, such as showing the help
		 *
		 * Unlike @ref IsPassThroughKey, the text stays as it is being typed. Only keys that type nothing should be
		 * left, such as function keys. If it's not set, <kbd>F1</kbd> and <kbd>F5</kbd> are. <kbd>Tab</kbd> is always
		 * left, it moves the focus.
		 */
		Function<bool(const KeyboardEvent&)> IsKeyLeftByText;

		/** @brief Returns `true` if a text box being typed into leaves a key to the shortcuts, see @ref IsKeyLeftByText */
		bool LeavesKeyToShortcuts(const KeyboardEvent& event);

		/** @brief Forgets an element that is being destroyed */
		static void NotifyDestroyed(UiElement* element);
		/**
		 * @brief Takes an element out of the interface, it's destroyed at the start of the next frame
		 *
		 * Nothing points at it or anything in it from then on --- the pointer, the focus --- a press held by it is
		 * taken away (see @ref UiElement::OnPointerCancel()). It's destroyed at once if there's no surface.
		 */
		static void Retire(std::unique_ptr<UiElement> element);

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		struct Overlay
		{
			std::unique_ptr<UiElement> Element;
			UiOverlayOptions Options;
			bool Closing = false;
		};

		struct Toast
		{
			String Text;
			EditorIcon Icon;
			float TimeLeft;
		};

		static UiSurface* _instance;

		std::unique_ptr<UiElement> _root;
		SmallVector<Overlay, 4> _overlays;
		// Elements taken out of the interface in this frame, see Retire()
		SmallVector<std::unique_ptr<UiElement>, 0> _retired;
		SmallVector<Toast, 4> _toasts;
		UiSkin _skin;
		// Slanted lines of the interface, antialiased
		std::unique_ptr<Rendering::PolylineRenderer> _lines;
		// Made when it's first asked for, see GetReadableFont()
		std::unique_ptr<UiReadableFont> _readableFont;
		float _scale;
		UiElement* _hover;
		UiElement* _capture;
		UiElement* _focus;
		Vector2f _pointerPos;
		float _tooltipTime;
		bool _tooltipShown;
		std::int64_t _lastClickTime;
		Vector2f _lastClickPos;
		UiElement* _lastClickElement;
		// A finger on the screen, where it was put down, and whether a tooltip was shown by holding it
		bool _touchDown;
		Vector2f _touchStart;
		bool _touchTooltip;
		// How long the tooltip shown by a finger stays after it was lifted, in frames
		float _tooltipLinger;
		// A list scrolled by a finger dragged over it, from where it was scrolled to when the finger was put down
		UiScrollView* _touchScroll;
		bool _touchScrolling;
		float _touchScrollOrigin;
		float _touchVelocity;
		Vector2f _touchLastPos;
		std::int64_t _touchLastTime = 0;
		// A finger on an element that drags only once it was held, how long it has rested there, see UiElement::DragsAfterHold()
		enum class TouchHold : std::uint8_t
		{
			None,
			Waiting,
			Held
		};
		TouchHold _touchHold = TouchHold::None;
		float _touchHoldTime = 0.0f;
		// Asked for by the pointer in this frame, forgotten after it's drawn
		UiCursorOverlay _cursorOverlay = UiCursorOverlay::None;
		String _pointerTooltip;

		UiElement* HitTestAll(Vector2f pos, bool& blocked);
		void SetHover(UiElement* element);
		void RemoveClosedOverlays();
		void Forget(UiElement* element);
		// Forgets everything inside of an element taken out of the interface, see Retire()
		void ForgetInside(UiElement* element);
		// Takes the press away from the element holding it if it's inside of an element, as if a finger started scrolling
		void CancelCaptureInside(UiElement* element);
		// Whether an element is inside of another one, or is it
		static bool IsInside(const UiElement* element, const UiElement* ancestor);
		// Whether an element and all of its parents are shown
		static bool IsShownInTree(UiElement* element);
		void DrawTooltip(UiPainter& painter, StringView text);
		void DrawToasts(UiPainter& painter);
		void DrawCursorOverlay(UiPainter& painter);
		// Whether an element shown over the interface takes input and is drawn
		bool IsActive(const Overlay& overlay) const;
		// Index of the element shown over the interface an element is in, -1 for the tree of the interface, -2 if it's in none
		std::int32_t GetOverlayIndexOf(UiElement* element) const;
		// Whether keys can go to an element, nothing modal is shown over it
		bool AcceptsInput(UiElement* element) const;
		// Distance a finger moves before it's a drag, in pixels
		float GetTouchSlop() const;
		// Where Tab looks for elements to focus
		UiElement* GetFocusScope() const;
		// Elements Tab reaches inside of an element, in their order, `scrolled` inside of a list on the screen
		void CollectTabStops(UiElement* element, const Rectf& screen, bool scrolled, SmallVectorImpl<UiElement*>& stops);
		static UiMenuBar* FindMenuBar(UiElement* element);
	};
}

#endif
