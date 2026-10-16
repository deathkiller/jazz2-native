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
	/** @brief How an element shown over the interface behaves */
	struct UiOverlayOptions
	{
		/** @brief Position of the top left corner, it's moved to fit the screen */
		Vector2f Position;
		/** @brief Whether it's centered on the screen instead */
		bool Centered = false;
		/** @brief Whether nothing else can be used while it's shown */
		bool Modal = false;
		/** @brief Whether a click outside of it closes it */
		bool CloseOnOutsideClick = true;
		/** @brief Whether the virtual cursor is hidden while it's shown, it's driven by the stick directly */
		bool HidesVirtualCursor = false;
		/** @brief Called when it's closed */
		Function<void()> OnClosed;
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

		/** @brief Shows an element over the interface */
		UiElement* PushOverlay(std::unique_ptr<UiElement> element, UiOverlayOptions&& options);
		/** @brief Closes an element shown over the interface, it's destroyed at the end of the frame */
		void CloseOverlay(UiElement* element);
		/** @brief Moves an element shown over the interface, its top left corner, it's no longer centered then */
		void MoveOverlay(UiElement* element, Vector2f position);
		/** @brief Closes all elements shown over the interface that aren't modal */
		void ClosePopups();
		/** @brief Returns `true` if a modal element is shown */
		bool HasModal() const;
		/** @brief Returns `true` if any element is shown over the interface */
		bool HasOverlays() const;

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
		/** @brief Returns the size of the screen in pixels */
		Vector2f GetScreenSize() const;

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
		/** @brief Returns the last position of the pointer */
		Vector2f GetPointerPos() const {
			return _pointerPos;
		}
		/** @brief Returns `true` if the interface holds the pointer */
		bool HasCapture() const {
			return (_capture != nullptr);
		}

		/** @brief Gives the keyboard focus to an element, or takes it away */
		void SetFocus(UiElement* element);
		/** @brief Returns the element with the keyboard focus, or `nullptr` */
		UiElement* GetFocus() const {
			return _focus;
		}
		/** @brief Returns the element under the pointer, or `nullptr` */
		UiElement* GetHover() const {
			return _hover;
		}

		/** @brief Shows a short message for a while */
		void ShowToast(StringView text, EditorIcon icon = EditorIcon::Info);

		/** @brief Draws over all of the interface, in a slot of its own and the slot after it, with the size of the screen */
		Function<void(UiPainter&, Vector2f)> DrawOver;

		/** @brief Whether a cursor moved by a gamepad is shown */
		bool ShowVirtualCursor = false;
		/** @brief Position of the cursor moved by a gamepad, in pixels of the screen */
		Vector2f VirtualCursor;

		/** @brief Forgets an element that is being destroyed */
		static void NotifyDestroyed(UiElement* element);

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
		SmallVector<Toast, 4> _toasts;
		UiSkin _skin;
		// Slanted lines of the interface, antialiased
		std::unique_ptr<Rendering::PolylineRenderer> _lines;
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

		UiElement* HitTestAll(Vector2f pos, bool& blocked);
		void SetHover(UiElement* element);
		void RemoveClosedOverlays();
		void Forget(UiElement* element);
		void DrawTooltip(UiPainter& painter);
		void DrawToasts(UiPainter& painter);
	};
}

#endif
