#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Canvas.h"
#include "../../../nCine/Graphics/Camera.h"
#include "../../../nCine/Graphics/AuxiliaryWindow.h"

#include <Containers/Function.h>
#include <Containers/StringView.h>

#include <memory>

namespace nCine
{
	class Texture;
	class Viewport;
}

namespace Jazz2::Rendering
{
	class PolylineRenderer;
}

namespace Jazz2::UI::Windows
{
	class Element;
	class Painter;

#if defined(NCINE_HAS_AUXILIARY_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
		@brief Window of the system an element of the interface is shown in, outside of the main window

		The element keeps its position relative to the main window, as everything else of the interface, so it's laid
		out, drawn and pointed at the same way --- the window only shows the part of the interface where the element is.
		Where the window is on the desktop is what counts though: the window stays there when the main window moves,
		and the element follows it, see @ref GetDesktopPosition().

		What it shows is drawn into a texture by a viewport of its own every frame, and shown in the window after the
		frame is drawn, see @ref AuxiliaryWindow. On a display with another scale than the main window, the texture has
		more or fewer pixels than the element has, so it's as large there as it would be in the main window, see
		@ref GetRaster().
	*/
	class DetachedWindow
	{
	public:
		/** @brief Draws the element into the picture of the window, given the canvas to draw into */
		using DrawFunc = Death::Containers::Function<void(UI::Canvas&, DetachedWindow&)>;

		/**
		 * @brief Makes a window for an element, `nullptr` if it can't be made
		 *
		 * @p bounds are relative to the main window in pixels, @p desktopPosition where the window is on the desktop.
		 * With @p passThrough, the pointer goes through it, as through a tooltip. The element may be `nullptr` if the
		 * function draws something else.
		 */
		static std::unique_ptr<DetachedWindow> Create(Element* element, const Rectf& bounds, Vector2i desktopPosition, float toDesktop,
			bool popup, bool passThrough, DrawFunc&& draw);

		~DetachedWindow();

		DetachedWindow(const DetachedWindow&) = delete;
		DetachedWindow& operator=(const DetachedWindow&) = delete;

		/** @brief Returns the element shown in it */
		Element* GetElement() const {
			return _element;
		}
		/** @brief Returns the identifier of the window in input events, see @ref PointerEvent::WindowId */
		std::uint32_t GetId() const {
			return _window->GetId();
		}
		/** @brief Returns `true` if the system rounds the corners of the window, see @ref AuxiliaryWindow::HasRoundedCorners() */
		bool HasRoundedCorners() const {
			return _window->HasRoundedCorners();
		}
		/** @brief Returns `true` if it's a popup, a menu or a list */
		bool IsPopup() const {
			return _popup;
		}
		/** @brief Returns where the element is relative to the main window and its size, in pixels */
		const Rectf& GetBounds() const {
			return _bounds;
		}
		/** @brief Returns where the window is on the desktop, in units of the desktop */
		Vector2i GetDesktopPosition() const {
			return _desktopPosition;
		}
		/** @brief Returns the size of the window on the desktop, in units of the desktop */
		Vector2i GetDesktopSize() const;
		/**
		 * @brief Returns the pixels of the window for a pixel of the main window, 1 on a display with the same scale
		 *
		 * Rounded, so a few scales are drawn, see @ref Painter::SetRaster().
		 */
		float GetRaster() const {
			return _raster;
		}
		/** @brief Returns where a point relative to the main window in pixels is on the desktop, as the window shows it */
		Vector2i ToDesktop(Vector2f point) const;
		/** @brief Returns the polyline renderer of the window, it draws only into it */
		Rendering::PolylineRenderer* GetLines() const {
			return _lines.get();
		}

		/**
		 * @brief Puts the window on the desktop and the element relative to the main window
		 *
		 * The size is in pixels, the position on the desktop in units of the desktop, @p toDesktop is how many units of the
		 * desktop a pixel takes. The viewport of the window is added to the chain of viewports again if it was taken out.
		 */
		void Place(const Rectf& bounds, Vector2i desktopPosition, float toDesktop);
		/** @brief Shows the window, it's seen once its contents are drawn */
		void Show(bool activate);
		/** @brief Hides the window */
		void Hide();
		/** @brief Returns `true` if the window is shown */
		bool IsShown() const;
		/** @brief Brings the window in front of the others and gives it the focus */
		void Raise();
		/** @brief Sets the title of the window, shown on the taskbar */
		void SetTitle(StringView title);

	private:
		class WindowCanvas;

		Element* _element;
		bool _popup;
		Rectf _bounds;
		Vector2i _desktopPosition;
		Vector2i _targetSize;
		float _raster = 1.0f;
		// Units of the desktop a pixel of the window takes
		float _unitsPerPixel = 1.0f;
		DrawFunc _draw;
		std::unique_ptr<AuxiliaryWindow> _window;
		std::unique_ptr<Texture> _target;
		std::unique_ptr<WindowCanvas> _canvas;
		std::unique_ptr<Rendering::PolylineRenderer> _lines;
		Camera _camera;
		// Destroyed first, before the texture and the canvas it draws
		std::unique_ptr<Viewport> _view;

		DetachedWindow(Element* element, bool popup, DrawFunc&& draw);
	};
#else
	// The system has no windows besides the main one, nothing ever gets a window of its own, see Surface::CanDetach()
	class DetachedWindow
	{
	public:
		using DrawFunc = Death::Containers::Function<void(UI::Canvas&, DetachedWindow&)>;

		static std::unique_ptr<DetachedWindow> Create(Element*, const Rectf&, Vector2i, float, bool, bool, DrawFunc&&) {
			return nullptr;
		}

		Element* GetElement() const { return nullptr; }
		std::uint32_t GetId() const { return 0; }
		bool HasRoundedCorners() const { return false; }
		bool IsPopup() const { return false; }
		const Rectf& GetBounds() const { return _bounds; }
		Vector2i GetDesktopPosition() const { return {}; }
		Vector2i GetDesktopSize() const { return {}; }
		float GetRaster() const { return 1.0f; }
		Vector2i ToDesktop(Vector2f) const { return {}; }
		Rendering::PolylineRenderer* GetLines() const { return nullptr; }
		void Place(const Rectf&, Vector2i, float) {}
		void Show(bool) {}
		void Hide() {}
		bool IsShown() const { return false; }
		void Raise() {}
		void SetTitle(StringView) {}

	private:
		Rectf _bounds;

		DetachedWindow() = default;
	};
#endif
}

#endif
