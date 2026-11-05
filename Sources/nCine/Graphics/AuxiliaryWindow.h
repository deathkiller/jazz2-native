#pragma once

#include "../../Main.h"

// Windows besides the main one exist only on desktop systems, with a backend that can make them, a build may leave them
// out with NCINE_NO_AUXILIARY_WINDOWS
#if ((defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || (defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_IOS)) || \
	(defined(DEATH_TARGET_UNIX) && !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && \
	 !defined(DEATH_TARGET_VITA) && !defined(DEATH_TARGET_MORPHOS))) && (defined(WITH_SDL2) || defined(WITH_SDL3) || defined(WITH_GLFW)) && \
	!defined(DEDICATED_SERVER) && !defined(NCINE_NO_AUXILIARY_WINDOWS)
#	define NCINE_HAS_AUXILIARY_WINDOWS
#endif

#if defined(NCINE_HAS_AUXILIARY_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Primitives/Rect.h"
#include "../Primitives/Vector2.h"

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

#include <memory>

namespace nCine
{
	class Texture;
	class AuxiliaryWindowPresenter;

	/** @brief How a window besides the main one is made, see @ref IGfxDevice::createAuxiliaryWindow() */
	struct AuxiliaryWindowDesc
	{
		/** @brief Where it is and its size, in units of the desktop, see @ref AuxiliaryWindow::GetBounds() */
		Recti Bounds;
		/**
		 * @brief Whether it's a popup, a menu or a list
		 *
		 * A popup isn't shown on the taskbar and never takes the focus from the window it was shown from.
		 */
		bool Popup = false;
		/**
		 * @brief Whether the pointer goes through it to what is below, as through a tooltip
		 *
		 * Where the system can't do that, input over it comes with its identifier, see @ref AuxiliaryWindow::GetId().
		 */
		bool PassThrough = false;
	};

	/**
		@brief Window of the application besides the main one, as a panel dragged out of it

		Made by @ref IGfxDevice::createAuxiliaryWindow(), it has no border or title bar of the system, whatever it shows
		is drawn by the application into a texture, which is shown in the window every frame, see @ref SetContents().
		It stays in front of the main window, and is shown on the taskbar unless it's a popup.

		Input over it goes to the input handler of the application as input over the main window would, with the
		position relative to the main window and @ref MouseState::windowId set to @ref GetId(). Keys typed while it
		has the focus go there too, and the application doesn't count as having lost the focus.

		The platform backend makes the window, @ref AuxiliaryWindowPresenter shows the contents with the graphics backend.
	*/
	class AuxiliaryWindow
	{
	public:
		virtual ~AuxiliaryWindow();

		AuxiliaryWindow(const AuxiliaryWindow&) = delete;
		AuxiliaryWindow& operator=(const AuxiliaryWindow&) = delete;

		/** @brief Returns its identifier in input events, never 0 */
		std::uint32_t GetId() const {
			return _id;
		}

		/** @brief Moves and resizes it, in units of the desktop */
		virtual void SetBounds(const Recti& bounds) = 0;
		/** @brief Returns where it is and its size, in units of the desktop, which may be larger than pixels on a scaled display */
		virtual Recti GetBounds() const = 0;
		/** @brief Returns the size of what is drawn into it in pixels */
		virtual Vector2i GetPixelSize() const = 0;
		/** @brief Returns `true` if it has the keyboard focus */
		virtual bool IsFocused() const = 0;
		/** @brief Brings it in front of other windows and gives it the focus */
		virtual void Raise() = 0;
		/** @brief Sets its title, the system shows it on the taskbar */
		virtual void SetTitle(Death::Containers::StringView title) = 0;
		/**
		 * @brief Returns the scale of the display it's on, 1 for 100 %
		 *
		 * The ratio to the scale of the main window (see @ref IGfxDevice::windowContentScale()) is how much larger what
		 * is shown in it has to be drawn on another display. By default it's how many pixels it has for a unit of the
		 * desktop, which is the scale of the display where the system scales the units of windows by it.
		 */
		virtual float GetContentScale() const;
		/**
		 * @brief Sets how many units of the main window a unit of this window takes, 1 by default
		 *
		 * Positions of the pointer over it are made relative to the main window with it, what it shows may be drawn at
		 * another scale than the main window, on a display with another scale.
		 */
		void SetInputScale(float scale) {
			_inputScale = scale;
		}
		/** @brief Returns how many units of the main window a unit of this window takes, see @ref SetInputScale() */
		float GetInputScale() const {
			return _inputScale;
		}
		/** @brief Returns `true` if the system rounds its corners, what is drawn into it should have the same corners */
		bool HasRoundedCorners() const {
			return _roundedCorners;
		}

		/**
		 * @brief Shows it, @p activate gives it the focus too
		 *
		 * It's shown once its contents were shown in it the first time, see @ref Present(), so it never shows something
		 * that wasn't drawn for it.
		 */
		void Show(bool activate);
		/** @brief Hides it */
		void Hide();
		/** @brief Returns `true` if it's shown or will be once its contents are, see @ref Show() */
		bool IsShown() const {
			return _shown;
		}

		/**
		 * @brief Sets the texture with what it shows, it's shown after the frame is drawn
		 *
		 * The texture has to stay until the next call or until the window is destroyed, and should be as large as the
		 * window in pixels, a larger one is cut and a smaller one leaves the rest black. Nothing is shown with `nullptr`.
		 */
		void SetContents(Texture* texture) {
			_contents = texture;
		}
		/** @brief Returns the texture with what it shows */
		Texture* GetContents() const {
			return _contents;
		}

		/** @brief Shows the contents in the window, called once a frame after it's drawn, see @ref IGfxDevice::presentAuxiliaryWindows() */
		void Present();

		/** @brief Returns all windows besides the main one */
		static Death::Containers::ArrayView<AuxiliaryWindow* const> GetAll();
		/** @brief Returns the window of an identifier, `nullptr` if there's none */
		static AuxiliaryWindow* FindById(std::uint32_t id);
		/** @brief Returns `true` if any window besides the main one has the keyboard focus */
		static bool IsAnyFocused();

	protected:
		AuxiliaryWindow();

		/** @brief Makes the window seen, or hides it */
		virtual void SetVisible(bool visible, bool activate) = 0;

		std::uint32_t _id = 0;
		Texture* _contents = nullptr;
		/** @brief Whether the system rounds its corners, see @ref HasRoundedCorners() */
		bool _roundedCorners = false;
		float _inputScale = 1.0f;
		/** @brief Shows the contents with the graphics backend, set by the platform backend once the window is made */
		std::unique_ptr<AuxiliaryWindowPresenter> _presenter;

	private:
		bool _shown = false;
		bool _visible = false;
		bool _activateWhenShown = false;
	};
}

#endif
