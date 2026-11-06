#pragma once

#include "../Primitives/Vector2.h"
#include "../Primitives/Rect.h"
#include "DisplayMode.h"
#include "AuxiliaryWindow.h"
#include "../AppConfiguration.h"
#include "../../Main.h"

#include <Containers/SmallVector.h>

#include <memory>

using namespace Death::Containers;

#if defined(DEATH_TARGET_EMSCRIPTEN)
class EmscriptenUiEvent;
class EmscriptenFullscreenChangeEvent;
class EmscriptenFocusEvent;
#endif

namespace nCine
{
	/**
		@brief Represents the interface to the graphics device where everything is rendered
		
		Abstracts the platform window and OpenGL context: it owns the window mode, the chosen display mode and
		the context creation attributes, exposes the window/drawable resolution used to set up the viewport, and
		enumerates the connected monitors and their video modes. Concrete back-ends (SDL, GLFW, Qt5, Android, ...)
		implement the platform-specific operations such as resolution changes and buffer swapping.
	*/
	class IGfxDevice
	{
		friend class Application;
#if (defined(WITH_SDL2) || defined(WITH_SDL3))
		friend class MainApplication;
#endif

	public:
		static constexpr std::uint32_t MaxMonitors = 4;
#if defined(WITH_QT5)
		// Qt5 cannot query the list of supported video modes of a monitor
		static constexpr std::uint32_t MaxVideoModes = 1;
#elif defined(DEATH_TARGET_ANDROID)
		static constexpr std::uint32_t MaxVideoModes = 16;
#else
		static constexpr std::uint32_t MaxVideoModes = 128;
#endif

		/** @brief Initial properties of the application window */
		struct WindowMode
		{
			WindowMode()
				: Width(0), Height(0), WindowPositionX(AppConfiguration::WindowPositionIgnore), WindowPositionY(AppConfiguration::WindowPositionIgnore), IsFullscreen(false), IsResizable(false), HasWindowScaling(true) { }
			WindowMode(std::int32_t w, std::int32_t h, std::int32_t posX, std::int32_t posY, bool fullscreen, bool resizable, bool windowScaling)
				: Width(w), Height(h), WindowPositionX(posX), WindowPositionY(posY), IsFullscreen(fullscreen), IsResizable(resizable), HasWindowScaling(windowScaling) { }

			std::int32_t Width;
			std::int32_t Height;
			std::int32_t WindowPositionX;
			std::int32_t WindowPositionY;
			bool IsFullscreen;
			bool IsResizable;
			bool HasWindowScaling;
		};

		/** @brief Video mode supported by a monitor */
		struct VideoMode
		{
			VideoMode()
				: Width(0), Height(0), RefreshRate(0.0f), RedBits(8), GreenBits(8), BlueBits(8) { }

			inline bool operator==(const VideoMode& mode) const
			{
				return (Width == mode.Width && Height == mode.Height && RefreshRate == mode.RefreshRate &&
						RedBits == mode.RedBits && GreenBits == mode.GreenBits && BlueBits == mode.BlueBits);
			}
			inline bool operator!=(const VideoMode& mode) const {
				return !operator==(mode);
			}

			std::uint32_t Width;
			std::uint32_t Height;
			float RefreshRate;
			std::uint32_t RedBits;
			std::uint32_t GreenBits;
			std::uint32_t BlueBits;
		};

		/** @brief Connected monitor */
		struct Monitor
		{
			/** @brief Monitor name */
			const char* Name;
			/** @brief Position of the monitor's viewport on the virtual screen */
			Vector2i Position;
			/** @brief Content scale factor */
			Vector2f Scale;

			/** @brief Number of valid entries in @ref videoModes */
			std::int32_t VideoModeCount;
			/** @brief Video modes supported by the monitor */
			VideoMode VideoModes[MaxVideoModes];
		};

		/** @brief Attributes used to create an OpenGL context */
		struct ContextInfo
		{
			ContextInfo()
				: MajorVersion(0), MinorVersion(0), CoreProfile(false), ForwardCompatible(false), DebugContext(false) { }

			explicit ContextInfo(const AppConfiguration& appCfg)
				: MajorVersion(appCfg.GetGlMajorVersion()), MinorVersion(appCfg.GetGlMinorVersion()),
				  CoreProfile(appCfg.IsGlCoreProfile()), ForwardCompatible(appCfg.IsGlForwardCompatible()),
				  DebugContext(appCfg.WithGlDebugContext) { }

			std::uint32_t MajorVersion;
			std::uint32_t MinorVersion;
			bool CoreProfile;
			bool ForwardCompatible;
			bool DebugContext;
		};

		IGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		virtual ~IGfxDevice() { }

		/**
		 * @brief Sets the number of vertical blanks to occur before a buffer swap
		 *
		 * An interval of `-1` will enable adaptive v-sync if available.
		 */
		virtual void SetSwapInterval(int interval) = 0;

		/** @brief Returns `true` if the device renders in full screen */
		inline bool IsFullscreen() const { return _isFullscreen; }
		/** @brief Sets the screen resolution with two integers */
		virtual void SetResolution(bool fullscreen, int width = 0, int height = 0) = 0;

		/** @brief Sets the position of the application window with two integers */
		virtual void SetWindowPosition(int x, int y) = 0;
		/** @brief Sets the position of the application window with a `Vector2i` object */
		inline void SetWindowPosition(Vector2i position) {
			SetWindowPosition(position.X, position.Y);
		}
		/** @brief Sets the application window title */
		virtual void SetWindowTitle(StringView windowTitle) = 0;
		/** @brief Sets the application window icon */
		virtual void SetWindowIcon(StringView iconFilename) = 0;

		/** @brief Returns the window or video mode width in screen coordinates */
		inline int GetWidth() const { return _width; }
		/** @brief Returns the window or video mode height in screen coordinates */
		inline int GetHeight() const { return _height; }
		/** @brief Returns the window or video mode resolution in screen coordinates as a `Vector2i` object */
		inline Vector2i GetResolution() const { return Vector2i(_width, _height); }
		/** @brief Returns the window or video mode resolution in screen coordinates as a `Rectf` object */
		inline Rectf GetScreenRect() const { return Rectf(0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height)); }
		/** @brief Returns the window or video mode resolution aspect ratio */
		inline float GetAspect() const { return _width / static_cast<float>(_height); }
		/**
		 * @brief Sets the window size with two integers
		 *
		 * @note If the application is in full screen this method will have no effect.
		 */
		virtual void SetWindowSize(int width, int height) = 0;
		/**
		 * @brief Sets the pixel size of the drawable (the size the frame is rendered at) without touching the window
		 *
		 * Honoured only by a backend that renders the frame into an intermediate surface of its own and
		 * stretches it onto the display at present time (the sceGxm backend on PS Vita, see
		 * `GxmDevice::ScreenWidth`), where the surface is recreated at the new size and the application is
		 * notified through `OnResizeWindow()`. Everywhere else the drawable follows the window and this does
		 * nothing. Out-of-range sizes are clamped to what the display can show.
		 */
		inline virtual void SetDrawableSize(int width, int height) {
			static_cast<void>(width);
			static_cast<void>(height);
		}

		/** @brief Returns the window position as a `Vector2i` object */
		inline virtual const Vector2i GetWindowPosition() const { return Vector2i(0, 0); }

		/**
		 * @brief Returns the window width in pixels
		 *
		 * It may differ from @ref width() on HiDPI screens.
		 */
		inline int GetDrawableWidth() const { return _drawableWidth; }
		/**
		 * @brief Returns the window height in pixels
		 *
		 * It may differ from @ref height() on HiDPI screens.
		 */
		inline int GetDrawableHeight() const { return _drawableHeight; }
		/** @brief Returns the window resolution in pixels as a `Vector2i` object */
		inline Vector2i GetDrawableResolution() const { return Vector2i(_drawableWidth, _drawableHeight); }
		/** @brief Returns the window resolution in pixels as a `Rectf` object */
		inline Rectf GetDrawableScreenRect() const { return Rectf(0.0f, 0.0f, static_cast<float>(_drawableWidth), static_cast<float>(_drawableHeight)); }
		/** @brief Returns the window drawable resolution aspect ratio */
		inline float GetDrawableAspect() const { return _drawableWidth / static_cast<float>(_drawableHeight); }
		/**
		 * @brief Returns the aspect ratio the drawable is actually DISPLAYED at
		 *
		 * The same as @ref GetDrawableAspect() wherever a pixel of the drawable is square on the display, which
		 * is every desktop and handheld target and most consoles. It is not the same on hardware whose video
		 * mode does not have square pixels: the PlayStation 2's 640x448 NTSC field pair (1.43) fills a 4:3
		 * television, so a scene composed for the drawable's own ratio comes out stretched by 7% vertically.
		 * Anything that decides how much of the world to show - see
		 * @ref Jazz2::Rendering::UpscaleRenderPass::CalculateViewSize() - has to use this ratio instead.
		 */
		inline virtual float GetDisplayAspect() const { return GetDrawableAspect(); }

		/** @brief Highlights the application window to notify the user */
		inline virtual void FlashWindow() const { }

		/** @brief Returns the OpenGL context creation attributes */
		inline const ContextInfo& GetContextInfo() const { return _contextInfo; }
		/** @brief Returns the display mode */
		inline const DisplayMode& GetDisplayMode() const { return _displayMode; }

		/** @brief Returns the number of connected monitors */
		unsigned int GetMonitorCount() const;
		/** @brief Returns the array index of the primary monitor */
		inline virtual unsigned int GetPrimaryMonitorIndex() const { return 0; }
		/** @brief Returns the array index of the monitor associated with the window */
		inline virtual unsigned int GetWindowMonitorIndex() const { return 0; }
		/** @brief Returns the specified monitor */
		const Monitor &GetMonitor(unsigned int index) const;
		/** @brief Returns the monitor that hosts the window */
		inline const Monitor& GetMonitor() const { return GetMonitor(GetWindowMonitorIndex()); }

		/** @brief Returns the current video mode for the specified monitor */
		virtual const VideoMode& GetCurrentVideoMode(unsigned int monitorIndex) const = 0;
		/** @brief Returns the current video mode for the monitor that hosts the window */
		inline const VideoMode& GetCurrentVideoMode() const { return GetCurrentVideoMode(GetWindowMonitorIndex()); }
		/**
		 * @brief Sets the video mode used in full screen by the monitor that hosts the window
		 *
		 * @note Call this method <b>before</b> enabling full screen.
		 */
		inline virtual bool SetVideoMode(unsigned int modeIndex) { return false; }

		/** @brief Returns the scaling factor for the application window */
		float GetWindowScalingFactor() const;

#if defined(NCINE_HAS_AUXILIARY_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Returns `true` if windows besides the main one can be made, see @ref CreateAuxiliaryWindow()
		 *
		 * Only desktop systems where windows can be placed anywhere have them, with a graphics backend that can present
		 * into another window.
		 */
		inline virtual bool SupportsAuxiliaryWindows() const { return false; }
		/** @brief Makes a hidden window besides the main one, `nullptr` if it can't be made, see @ref AuxiliaryWindow */
		inline virtual std::unique_ptr<AuxiliaryWindow> CreateAuxiliaryWindow(const AuxiliaryWindowDesc& desc) { return nullptr; }
		/** @brief Shows the contents of every auxiliary window, called once a frame after it's drawn */
		void PresentAuxiliaryWindows() {
			for (AuxiliaryWindow* window : AuxiliaryWindow::GetAll()) {
				window->Present();
			}
		}
		/** @brief Returns the position of the pointer on the desktop in pixels, `false` if it isn't known */
		inline virtual bool GetGlobalMousePosition(Vector2i& position) const { return false; }
		/**
		 * @brief Returns the mouse buttons held now as the system has them, `false` if it isn't known
		 *
		 * Bits 0, 1 and 2 are the left, the right and the middle button. Unlike the events, it's never left behind when a
		 * release goes to a window of the application that was just shown or moved under the pointer.
		 */
		inline virtual bool GetGlobalMouseButtons(std::uint32_t& buttons) const { return false; }
		/**
		 * @brief Returns the part of the display at a point of the desktop that windows may take, without the taskbar
		 *
		 * The display nearest to the point if it's on none. In units of the desktop, as positions of windows are.
		 * Returns `false` if it isn't known.
		 */
		inline virtual bool GetDesktopWorkArea(Vector2i point, Recti& area) const { return false; }
		/**
		 * @brief Returns the scale of the display the main window is on, 1 for 100 %, see @ref AuxiliaryWindow::GetContentScale()
		 *
		 * By default it's how many pixels the main window has for a unit of the desktop.
		 */
		inline virtual float GetWindowContentScale() const { return (_width > 0 ? float(_drawableWidth) / float(_width) : 1.0f); }
#endif

	protected:
		static constexpr float DefaultDPI = 96.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Window width in screen coordinates */
		std::int32_t _width;
		/** @brief Window height in screen coordinates */
		std::int32_t _height;
		/** @brief Window width in pixels (for HiDPI screens) */
		std::int32_t _drawableWidth;
		/** @brief Window height in pixels (for HiDPI screens) */
		std::int32_t _drawableHeight;
		/** @brief Whether rendering occurs in full screen */
		bool _isFullscreen;
		/** @brief OpenGL context creation attributes */
		ContextInfo _contextInfo;
		/** @brief Display properties */
		DisplayMode _displayMode;

		Monitor _monitors[MaxMonitors];
		std::uint32_t _numMonitors;
		/** @brief Cache to avoid searching the current video mode in a monitor's array */
		mutable VideoMode _currentVideoMode;
#endif

		/** @brief Initializes the OpenGL viewport based on the drawable resolution */
		void InitDeviceViewport();

		/** @brief Updates the array of connected monitors */
		inline virtual void UpdateMonitors() { }

		virtual void SetResolutionInternal(int width, int height) = 0;

	private:
		/** @brief Sets up the initial OpenGL state for the scene graph */
		virtual void SetupDevice();

		/** @brief Updates the screen by swapping the back and front buffers */
		virtual void Update() = 0;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		static bool EmscriptenHandleResize(int eventType, const EmscriptenUiEvent* event, void* userData);
		static bool EmscriptenHandleFullscreen(int eventType, const EmscriptenFullscreenChangeEvent* event, void* userData);

#	if defined(WITH_GLFW)
		static bool EmscriptenHandleFocus(int eventType, const EmscriptenFocusEvent* event, void* userData);
#	endif
#endif
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	/**
		@brief Fake graphics device which doesn't render anything
		
		Null implementation of @ref IGfxDevice used when no real window or OpenGL context is available (for
		example in headless or server builds); all operations are no-ops.
	*/
	class NullGfxDevice : public IGfxDevice
	{
	public:
		NullGfxDevice()
			: IGfxDevice(WindowMode{}, ContextInfo{}, DisplayMode{}) {}

		void SetSwapInterval(int interval) override {}

		void SetResolution(bool fullscreen, int width = 0, int height = 0) override {}

		void SetWindowPosition(int x, int y) override {}
		void SetWindowTitle(StringView windowTitle) override {}
		void SetWindowIcon(StringView iconFilename) override {}

		void SetWindowSize(int width, int height) override {}

		void FlashWindow() const override {}

		const VideoMode& GetCurrentVideoMode(unsigned int monitorIndex) const override { return _currentVideoMode; }

	protected:
		void SetResolutionInternal(int width, int height) override {}

	private:
		void SetupDevice() override {}
		void Update() override {}
	};
#endif
}
