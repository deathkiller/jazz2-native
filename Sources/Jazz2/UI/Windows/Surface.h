#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "DetachedWindow.h"
#include "Element.h"
#include "Skin.h"

namespace Jazz2::Rendering
{
	class PolylineRenderer;
}

namespace Jazz2::UI::Windows
{
	class ScrollView;
	class MenuBar;
	class ReadableFont;

	/** @brief What is shown by the pointer, as the cursor of a window would show it, see @ref Surface::SetCursorOverlay() */
	enum class CursorOverlay : std::uint8_t
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
		a picker. The rest are windows, which stay until they're closed, see @ref Surface::ClosePopups().
	*/
	struct OverlayOptions
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
		Element* Owner = nullptr;
		/** @brief Right edge it's put at instead if it doesn't fit right of its position, as a submenu goes left of its menu then, not used if negative */
		float FallbackRight = -1.0f;
		/**
		 * @brief Whether it can be shown in a window of its own outside of the main window, see @ref Surface::IsDetached()
		 *
		 * A window dragged out of the main window gets a window of the system, a popup gets one if it doesn't fit the main
		 * window or would be covered by a window shown that way. A modal element never does, it stays in the main window.
		 */
		bool Detachable = true;
		/** @brief Called when it's closed */
		Function<void()> OnClosed;

		/** @brief Returns `true` if it's a popup, not a window */
		bool IsPopup() const {
			return (CloseOnOutsideClick && !Modal);
		}
	};

	/** @brief Part of the interface a blurred picture is drawn behind, see @ref Surface::GetBackdrop */
	enum class BackdropKind : std::uint8_t
	{
		Panels,		/**< The tree of the interface, over the level and what is shown over the level */
		Overlays,	/**< Windows, menus and messages, also over the panels */
		UnderModal	/**< Windows under a modal window, drawn into the picture of the level with the panels then, see @ref Surface::HasStackedModal() */
	};

	/**
		@brief Shows the interface and passes input to it

		The surface is drawn over the whole window at its own scale, in pixels of the screen. It holds the tree of
		the interface and the elements shown over it --- menus, dialogs, tooltips. Whatever the pointer does over the
		interface goes to it, the rest is left to the view of the level.
	*/
	class Surface : public UI::Canvas
	{
	public:
		/** @brief Number of slots the root draws in, the one it's given and those after it, for panels floating over the others */
		static constexpr std::int32_t RootSlots = 4;

		Surface();
		~Surface() override;

		Surface(const Surface&) = delete;
		Surface& operator=(const Surface&) = delete;

		/** @brief Sets the root of the interface */
		template<class T, class... Args>
		T* SetRoot(Args&&... args) {
			return static_cast<T*>(SetRoot(std::make_unique<T>(std::forward<Args>(args)...)));
		}
		/** @brief Sets the root of the interface */
		Element* SetRoot(std::unique_ptr<Element> root);
		/** @brief Returns the root of the interface */
		Element* GetRoot() const {
			return _root.get();
		}

		/**
		 * @brief Shows an element over the interface
		 *
		 * A modal element takes the keyboard focus away from everything under it, a text being typed there is
		 * confirmed first.
		 */
		Element* PushOverlay(std::unique_ptr<Element> element, OverlayOptions&& options);
		/** @brief Closes an element shown over the interface, it's destroyed at the end of the frame */
		void CloseOverlay(Element* element);
		/**
		 * @brief Moves an element shown over the interface, its top left corner, it's no longer centered then
		 *
		 * See @ref OverlayOptions::FallbackRight for @p fallbackRight.
		 */
		void MoveOverlay(Element* element, Vector2f position, float fallbackRight = -1.0f);
		/**
		 * @brief Brings a window shown over the interface in front of the other windows and gives it the keyboard
		 *
		 * It stays under modal elements shown after it. A focus outside of it is taken away, so keys go to it. Returns
		 * `false` if the element isn't shown over the interface.
		 */
		bool RaiseOverlay(Element* element);
		/** @brief Closes all popups --- menus, lists and pickers --- windows stay, see @ref OverlayOptions */
		void ClosePopups();
		/** @brief Returns `true` if a modal element is shown */
		bool HasModal() const;
		/** @brief Returns `true` if any element is shown over the interface */
		bool HasOverlays() const;
		/**
		 * @brief Returns `true` if a modal element is shown over another element shown over the interface, as a file picker over a dialog
		 *
		 * While the tree of the interface is drawn into the picture of the level, the elements under the top modal one are
		 * drawn there too, over @ref BackdropKind::UnderModal, so the picture behind the modal one has them.
		 */
		bool HasStackedModal() const;
		/** @brief Returns `true` if an element is shown over the interface and isn't closing */
		bool IsOverlayShown(const Element* element) const;
		/**
		 * @brief Returns `true` if an element is in a window of its own, outside of the main window
		 *
		 * An element shown over the interface (see @ref OverlayOptions::Detachable) or one of the tree of the interface
		 * (see @ref PlaceDetachable()). Its position is still relative to the main window, as the positions of the pointer
		 * over it are, see @ref PointerEvent::WindowId.
		 */
		bool IsDetached(const Element* element) const;
		/** @brief Returns `true` if elements can be shown in windows of their own, it depends on the system and @ref AllowDetaching */
		bool CanDetach() const;
		/**
		 * @brief Lets an element of the tree of the interface get a window of its own once it's moved out of the main window
		 *
		 * As a floating panel. Called every frame while the element may have one, with where it wants to be relative to the
		 * main window in pixels and whether the user is moving it now. Returns where it is: in a window of its own it stays
		 * where it is on the desktop while it isn't moved, also when the main window moves, so it moves relative to the main
		 * window. Once it's moved back into the main window, or @ref ReleaseDetachable() is called, it's shown in the main
		 * window again. While this isn't called for it, as while it's hidden, its window is hidden and comes back with it.
		 * With @p restore, it gets a window of its own also when it isn't moved, if it wants to be outside of the main
		 * window --- as it was the last time. Its parent doesn't draw it while it's in a window of its own, see
		 * @ref Element::IsDetached().
		 */
		Rectf PlaceDetachable(Element* element, const Rectf& wanted, bool moving, bool restore = false);
		/** @brief Shows an element placed by @ref PlaceDetachable() in the main window again, as when it was docked */
		void ReleaseDetachable(Element* element);

		/** @brief Whether elements may get windows of their own outside of the main window */
		bool AllowDetaching = true;

		/** @brief Returns the scale of the interface, pixels per unit */
		float GetScale() const {
			return _scale;
		}
		/** @brief Sets the scale of the interface */
		void SetScale(float scale);
		/** @brief Returns what the interface is measured with */
		Context GetContext() const;
		/** @brief Returns the font of the interface, filtered as the scale needs, see @ref Context::GetFont() */
		UI::Font* GetFont() const;
		/**
		 * @brief Returns the readable font for text drawn at a scale, see @ref ReadableFont::Get()
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
		bool OnPointerDown(const PointerEvent& event);
		/** @brief Passes a move of the pointer, returns `true` if it's over the interface or the interface holds it */
		bool OnPointerMove(const PointerEvent& event);
		/** @brief Passes a release of the pointer, returns `true` if the interface took it */
		bool OnPointerUp(const PointerEvent& event);
		/** @brief Passes scrolling, returns `true` if the interface took it */
		bool OnWheel(const PointerEvent& event);
		/** @brief Passes a key, returns `true` if the interface took it */
		bool OnKeyPressed(const KeyboardEvent& event);
		/** @brief Passes typed text, returns `true` if the interface took it */
		bool OnTextInput(const TextInputEvent& event);
		/** @brief Returns `true` if the interface is under a position */
		bool IsOverInterface(Vector2f pos);
		/** @brief Returns `true` if a finger at a position is the interface's at once, not one that may still pan the view, see @ref Element::YieldsToViewGestures() */
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
		/** @brief Hides the tooltip and forgets what is pointed at, until the pointer moves again */
		void DismissTooltip();

		/** @brief Gives the keyboard focus to an element, or takes it away */
		void SetFocus(Element* element);
		/** @brief Returns the element with the keyboard focus, or `nullptr` */
		Element* GetFocus() const {
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
		void EnsureVisible(Element* element);
		/** @brief Returns the element under the pointer, or `nullptr` */
		Element* GetHover() const {
			return _hover;
		}

		/** @brief Shows a short message for a while */
		void ShowToast(StringView text, IconId icon = StockIcon::Info);

		/**
		 * @brief Shows an icon by the pointer for this frame, as the cursor of a window would show it
		 *
		 * Called every frame by whatever is pointed at or dragged --- in @ref Element::Update() or in
		 * @ref Element::Draw() --- it's forgotten after the frame is drawn, so it goes away by itself once nothing
		 * asks for it. The last call in a frame wins. It's drawn over everything at a distance from the pointer that
		 * grows with the scale of the interface, and not at all for a finger.
		 */
		void SetCursorOverlay(CursorOverlay overlay);
		/** @brief Returns what is shown by the pointer in this frame so far */
		CursorOverlay GetCursorOverlay() const {
			return _cursorOverlay;
		}
		/** @brief Returns the arrows for resizing by edges, a corner if one edge of each direction is set, @ref CursorOverlay::None if none is */
		static CursorOverlay GetResizeOverlay(bool left, bool top, bool right, bool bottom);
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
		Function<void(Painter&, Vector2f)> DrawOver;
		/**
		 * @brief Returns a blurred picture of what is behind a part of the interface and the part of the screen it covers, or `nullptr`
		 *
		 * Asked for every frame with the size of the screen, before the part is drawn, which is drawn over the picture then,
		 * see @ref Painter::Backdrop(). The last argument tells whether the tree of the interface is drawn into the
		 * picture of the level in this frame, see @ref DrawsPanelsInLevel.
		 */
		Function<const Texture*(BackdropKind, Vector2f, Rectf&, bool)> GetBackdrop;
		/**
		 * @brief Returns `true` if the tree of the interface is drawn into the picture of the level in this frame
		 *
		 * Asked for every frame while the picture is drawn, if the node of @ref GetPanelsNode() is drawn into it. The tree
		 * is drawn with the rest otherwise.
		 */
		Function<bool()> DrawsPanelsInLevel;

		/** @brief Returns the node that draws the tree of the interface into the picture of the level, the surface owns it, see @ref DrawsPanelsInLevel */
		SceneNode* GetPanelsNode();
		/** @brief Returns the depth the panels are drawn from, what is shown over the level is drawn below it */
		static std::uint16_t GetPanelsDepth();
		/** @brief Returns the depth elements shown over the interface are drawn from, see @ref HasStackedModal() */
		static std::uint16_t GetOverlaysDepth();
		/**
		 * @brief Returns `true` while the tree of the interface is drawn into the picture of the level
		 *
		 * The panels show what is under them blurred then, so what is shown over the level is seen under them too.
		 */
		bool IsDrawingInLevel() const {
			return _drawingInLevel;
		}

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
		 * @brief Height of the bottom of the screen a keyboard of the system covers, in pixels, set every frame
		 *
		 * A text box typed into stays above it --- a window it's in is moved up, the panels are laid out above the
		 * keyboard if it's in them and would be covered.
		 */
		float KeyboardInset = 0.0f;
		/**
		 * @brief Part of each edge of the screen the interface keeps clear of, as a fraction of its width or height
		 *
		 * For a display that cuts off the edges of the picture, as a television can. What is behind the interface, the
		 * level, still takes the whole screen. See @ref GetSafeBounds().
		 */
		Thickness SafeMargins;
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
		static void NotifyDestroyed(Element* element);
		/**
		 * @brief Takes an element out of the interface, it's destroyed at the start of the next frame
		 *
		 * Nothing points at it or anything in it from then on --- the pointer, the focus --- a press held by it is
		 * taken away (see @ref Element::OnPointerCancel()). It's destroyed at once if there's no surface.
		 */
		static void Retire(std::unique_ptr<Element> element);

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		// The window of its own an element has while it's out of the main window, see IsDetached()
		struct Detachment
		{
			std::unique_ptr<DetachedWindow> Window;
			// Where the owner wanted it the last time, a change of it is a move
			Rectf LastWanted;
		};

		struct Overlay
		{
			std::unique_ptr<Windows::Element> Element;
			OverlayOptions Options;
			bool Closing = false;
			// Whether it was moved, only a window moved out of the main window gets a window of its own
			bool MovedByUser = false;
			// Whether it was moved since the last update
			bool MovedNow = false;
			Detachment Detached;
		};

		// An element of the tree of the interface that may have a window of its own, see PlaceDetachable()
		struct DetachableElement
		{
			Windows::Element* Element;
			Detachment Detached;
			// Whether its owner asked for it since the last update
			bool Placed;
		};

		struct Toast
		{
			String Text;
			IconId Icon;
			float TimeLeft;
		};

		// Draws the tree of the interface into the picture of the level, see GetPanelsNode()
		class PanelsCanvas;

		static Surface* _instance;

		std::unique_ptr<Element> _root;
		SmallVector<Overlay, 4> _overlays;
		SmallVector<DetachableElement, 2> _detachables;
		// Elements taken out of the interface in this frame, see Retire()
		SmallVector<std::unique_ptr<Element>, 0> _retired;
		SmallVector<Toast, 4> _toasts;
		Skin _skin;
		// A skin made for a display with another scale, see GetRasterSkin()
		struct RasterSkin
		{
			float Scale = 0.0f;
			std::unique_ptr<Skin> Atlas;
			std::uint32_t LastUsed = 0;
		};
		SmallVector<RasterSkin, 1> _rasterSkins;
		std::uint32_t _updateFrame = 0;
		// The tooltip of an element in a window of its own that doesn't fit in that window, see UpdateTooltipWindow()
		std::unique_ptr<DetachedWindow> _tooltipWindow;
		String _tooltipWindowText;
		// Slanted lines of the interface, antialiased
		std::unique_ptr<Rendering::PolylineRenderer> _lines;
		// Made when it's first asked for, see GetReadableFont()
		std::unique_ptr<ReadableFont> _readableFont;
		float _scale;
		Element* _hover;
		Element* _capture;
		Element* _focus;
		Vector2f _pointerPos;
		float _tooltipTime;
		bool _tooltipShown;
		std::int64_t _lastClickTime;
		Vector2f _lastClickPos;
		Element* _lastClickElement;
		// A finger on the screen, where it was put down, and whether a tooltip was shown by holding it
		bool _touchDown;
		Vector2f _touchStart;
		bool _touchTooltip;
		// How long the tooltip shown by a finger stays after it was lifted, in frames
		float _tooltipLinger;
		// A list scrolled by a finger dragged over it, from where it was scrolled to when the finger was put down
		ScrollView* _touchScroll;
		bool _touchScrolling;
		float _touchScrollOrigin;
		float _touchVelocity;
		Vector2f _touchLastPos;
		std::int64_t _touchLastTime = 0;
		// A finger on an element that drags only once it was held, how long it has rested there, see Element::DragsAfterHold()
		enum class TouchHold : std::uint8_t
		{
			None,
			Waiting,
			Held
		};
		TouchHold _touchHold = TouchHold::None;
		float _touchHoldTime = 0.0f;
		// Asked for by the pointer in this frame, forgotten after it's drawn
		CursorOverlay _cursorOverlay = CursorOverlay::None;
		String _pointerTooltip;
		std::unique_ptr<PanelsCanvas> _panelsCanvas;
		// Window the pointer is over, 0 for the main one, see PointerEvent::WindowId
		std::uint32_t _pointerWindow = 0;
		// Part of the screen a window out of the main window takes while it's drawn, what is drawn by the pointer stays in it
		Rectf _detachedDrawBounds;
		// Frame the tree of the interface was drawn into the picture of the level in
		std::uint32_t _panelsDrawnFrame = 0;
		bool _drawingInLevel = false;
		// Elements shown over the interface drawn into the picture of the level in that frame, from the first one
		std::size_t _overlaysInLevel = 0;
		// Kept above the keyboard of the system, see KeyboardInset: a window moved up, or the panels laid out above it for a box
		const Element* _keyboardWindow = nullptr;
		float _keyboardShift = 0.0f;
		const Element* _keyboardRootFocus = nullptr;
		float _keyboardRootInset = 0.0f;
		std::int32_t _keyboardScrollFrames = 0;

		void DrawPanelsInto(UI::Canvas& canvas);
		// Gives elements shown over the interface that are out of the main window windows of their own, and takes them away
		// from those back in, also from elements of the tree that weren't placed since the last update
		void UpdateDetached();
		// Forgets what the pointer is over once it left the window it was over
		void UpdatePointerLeave();
		/**
		 * Gives an element a window of its own or takes it away by where it's wanted, returns where it is relative to the
		 * main window. Only one moved now, a popup, or one with @p restore (it had a window the last time), gets a window,
		 * see PlaceDetachable(), @p force gives it one also inside of the main window. A popup or one restored is put all on a display.
		 */
		Rectf UpdateDetachment(Detachment& detachment, Windows::Element* element, Rectf wanted, bool moving, bool popup, bool overlay, bool force, bool restore = false);
		// Draws an element in a window of its own into the picture of that window, one shown over the interface is laid out first
		void DrawDetached(UI::Canvas& canvas, DetachedWindow& window, bool layout);
		// Where an element shown over the interface wants to be, relative to the main window
		Rectf GetWantedBounds(const Overlay& overlay);
		// Whether an element shown over the interface is in the window the pointer is over
		bool IsInPointerWindow(const Overlay& overlay) const;
		// Whether a rectangle relative to the main window covers a window of its own an element has, but those of a list from an index
		bool OverlapsDetached(const Rectf& rect, std::size_t overlayCount) const;
		// Units of the desktop a pixel of the interface takes, see AuxiliaryWindow::GetBounds()
		float GetDesktopScale() const;
		void DrawRoot(Painter& painter, Vector2f screenSize, bool inLevel);
		void DrawOverlay(Painter& painter, std::size_t index, std::int32_t slot, Vector2f screenSize, const Rectf& safe);
		// Number of elements under the top modal one if any of them is active, 0 otherwise
		std::size_t GetStackedOverlayCount() const;
		void UpdateKeyboardAvoidance();

		Element* HitTestAll(Vector2f pos, bool& blocked);
		void SetHover(Element* element);
		void RemoveClosedOverlays();
		void Forget(Element* element);
		// Forgets everything inside of an element taken out of the interface, see Retire()
		void ForgetInside(Element* element);
		// Takes the window of its own away from an element destroyed, or from those inside of it
		void ForgetDetachables(Element* element, bool inside);
		// Takes the press away from the element holding it if it's inside of an element, as if a finger started scrolling
		void CancelCaptureInside(Element* element);
		// Whether an element is inside of another one, or is it
		static bool IsInside(const Element* element, const Element* ancestor);
		// Whether an element and all of its parents are shown
		static bool IsShownInTree(Element* element);
		void DrawTooltip(Painter& painter, StringView text);
		// The text of the tooltip shown now, the one set for the pointer or that of the element pointed at
		StringView GetTooltipText() const;
		Vector2f GetTooltipSize(StringView text) const;
		// Puts a tooltip by the pointer in an area, returns false if it doesn't fit there, it's where it would be without its edges then
		bool PlaceTooltip(Vector2f size, const Rectf& safe, Rectf& rect) const;
		void DrawTooltipBox(Painter& painter, const Rectf& rect, StringView text);
		void UpdateTooltipWindow();
		void DrawTooltipWindow(UI::Canvas& canvas, DetachedWindow& window);
		// Returns the window of its own of an element by its identifier, nullptr for the main window or one of none
		DetachedWindow* FindDetachedWindow(std::uint32_t windowId) const;
		// Returns where a point relative to the main window is on the desktop, as the window it's in shows it
		Vector2i ToDesktop(Vector2f point) const;
		// Moves a window on the desktop onto a display, all of it, or so its top can still be grabbed
		Vector2i KeepOnDesktop(Vector2i position, Vector2i size, bool whole) const;
		// Returns the skin for a window drawn with more or fewer pixels than the main window, see DetachedWindow::GetRaster()
		Skin& GetRasterSkin(float raster);
		UI::Font* GetRasterFont(float raster) const;
		void DrawToasts(Painter& painter);
		void DrawCursorOverlay(Painter& painter);
		// Whether an element shown over the interface takes input and is drawn
		bool IsActive(const Overlay& overlay) const;
		// Index of the element shown over the interface an element is in, -1 for the tree of the interface, -2 if it's in none
		std::int32_t GetOverlayIndexOf(Element* element) const;
		// Whether keys can go to an element, nothing modal is shown over it
		bool AcceptsInput(Element* element) const;
		// Distance a finger moves before it's a drag, in pixels
		float GetTouchSlop() const;
		// Where Tab looks for elements to focus
		Element* GetFocusScope() const;
		// Elements Tab reaches inside of an element, in their order, `scrolled` inside of a list on the screen
		void CollectTabStops(Element* element, const Rectf& screen, bool scrolled, SmallVectorImpl<Element*>& stops);
		static MenuBar* FindMenuBar(Element* element);
	};
}

#endif
