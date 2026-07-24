#pragma once

#include "../Primitives/Vector2.h"
#include "../Primitives/Rect.h"
#include "DisplayMode.h"
#include "../AppConfiguration.h"
#include "../../Main.h"

#include <Containers/SmallVector.h>

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
				: width(0), height(0), windowPositionX(AppConfiguration::WindowPositionIgnore), windowPositionY(AppConfiguration::WindowPositionIgnore), isFullscreen(false), isResizable(false), hasWindowScaling(true) { }
			WindowMode(std::int32_t w, std::int32_t h, std::int32_t posX, std::int32_t posY, bool fullscreen, bool resizable, bool windowScaling)
				: width(w), height(h), windowPositionX(posX), windowPositionY(posY), isFullscreen(fullscreen), isResizable(resizable), hasWindowScaling(windowScaling) { }

			std::int32_t width;
			std::int32_t height;
			std::int32_t windowPositionX;
			std::int32_t windowPositionY;
			bool isFullscreen;
			bool isResizable;
			bool hasWindowScaling;
		};

		/** @brief Video mode supported by a monitor */
		struct VideoMode
		{
			VideoMode()
				: width(0), height(0), refreshRate(0.0f), redBits(8), greenBits(8), blueBits(8) { }

			inline bool operator==(const VideoMode& mode) const
			{
				return (width == mode.width && height == mode.height && refreshRate == mode.refreshRate &&
						redBits == mode.redBits && greenBits == mode.greenBits && blueBits == mode.blueBits);
			}
			inline bool operator!=(const VideoMode& mode) const {
				return !operator==(mode);
			}

			std::uint32_t width;
			std::uint32_t height;
			float refreshRate;
			std::uint32_t redBits;
			std::uint32_t greenBits;
			std::uint32_t blueBits;
		};

		/** @brief Connected monitor */
		struct Monitor
		{
			/** @brief Monitor name */
			const char* name;
			/** @brief Position of the monitor's viewport on the virtual screen */
			Vector2i position;
			/** @brief Content scale factor */
			Vector2f scale;

			/** @brief Number of valid entries in @ref videoModes */
			std::int32_t numVideoModes;
			/** @brief Video modes supported by the monitor */
			VideoMode videoModes[MaxVideoModes];
		};

		/** @brief Attributes used to create an OpenGL context */
		struct ContextInfo
		{
			ContextInfo()
				: majorVersion(0), minorVersion(0), coreProfile(false), forwardCompatible(false), debugContext(false) { }

			explicit ContextInfo(const AppConfiguration& appCfg)
				: majorVersion(appCfg.glMajorVersion()), minorVersion(appCfg.glMinorVersion()),
				  coreProfile(appCfg.glCoreProfile()), forwardCompatible(appCfg.glForwardCompatible()),
				  debugContext(appCfg.withGlDebugContext) { }

			std::uint32_t majorVersion;
			std::uint32_t minorVersion;
			bool coreProfile;
			bool forwardCompatible;
			bool debugContext;
		};

		IGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		virtual ~IGfxDevice() { }

		/**
		 * @brief Sets the number of vertical blanks to occur before a buffer swap
		 *
		 * An interval of `-1` will enable adaptive v-sync if available.
		 */
		virtual void setSwapInterval(int interval) = 0;

		/** @brief Returns `true` if the device renders in full screen */
		inline bool isFullscreen() const { return isFullscreen_; }
		/** @brief Sets the screen resolution with two integers */
		virtual void setResolution(bool fullscreen, int width = 0, int height = 0) = 0;

		/** @brief Sets the position of the application window with two integers */
		virtual void setWindowPosition(int x, int y) = 0;
		/** @brief Sets the position of the application window with a `Vector2i` object */
		inline void setWindowPosition(Vector2i position) {
			setWindowPosition(position.X, position.Y);
		}
		/** @brief Sets the application window title */
		virtual void setWindowTitle(StringView windowTitle) = 0;
		/** @brief Sets the application window icon */
		virtual void setWindowIcon(StringView iconFilename) = 0;

		/** @brief Returns the window or video mode width in screen coordinates */
		inline int width() const { return width_; }
		/** @brief Returns the window or video mode height in screen coordinates */
		inline int height() const { return height_; }
		/** @brief Returns the window or video mode resolution in screen coordinates as a `Vector2i` object */
		inline Vector2i resolution() const { return Vector2i(width_, height_); }
		/** @brief Returns the window or video mode resolution in screen coordinates as a `Rectf` object */
		inline Rectf screenRect() const { return Rectf(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)); }
		/** @brief Returns the window or video mode resolution aspect ratio */
		inline float aspect() const { return width_ / static_cast<float>(height_); }
		/**
		 * @brief Sets the window size with two integers
		 *
		 * @note If the application is in full screen this method will have no effect.
		 */
		virtual void setWindowSize(int width, int height) = 0;

		/** @brief Returns the window position as a `Vector2i` object */
		inline virtual const Vector2i windowPosition() const { return Vector2i(0, 0); }

		/**
		 * @brief Returns the window width in pixels
		 *
		 * It may differ from @ref width() on HiDPI screens.
		 */
		inline int drawableWidth() const { return drawableWidth_; }
		/**
		 * @brief Returns the window height in pixels
		 *
		 * It may differ from @ref height() on HiDPI screens.
		 */
		inline int drawableHeight() const { return drawableHeight_; }
		/** @brief Returns the window resolution in pixels as a `Vector2i` object */
		inline Vector2i drawableResolution() const { return Vector2i(drawableWidth_, drawableHeight_); }
		/** @brief Returns the window resolution in pixels as a `Rectf` object */
		inline Rectf drawableScreenRect() const { return Rectf(0.0f, 0.0f, static_cast<float>(drawableWidth_), static_cast<float>(drawableHeight_)); }
		/** @brief Returns the window drawable resolution aspect ratio */
		inline float drawableAspect() const { return drawableWidth_ / static_cast<float>(drawableHeight_); }

		/** @brief Highlights the application window to notify the user */
		inline virtual void flashWindow() const { }

		/** @brief Returns the OpenGL context creation attributes */
		inline const ContextInfo& contextInfo() const { return contextInfo_; }
		/** @brief Returns the display mode */
		inline const DisplayMode& displayMode() const { return displayMode_; }

		/** @brief Returns the number of connected monitors */
		unsigned int numMonitors() const;
		/** @brief Returns the array index of the primary monitor */
		inline virtual unsigned int primaryMonitorIndex() const { return 0; }
		/** @brief Returns the array index of the monitor associated with the window */
		inline virtual unsigned int windowMonitorIndex() const { return 0; }
		/** @brief Returns the specified monitor */
		const Monitor &monitor(unsigned int index) const;
		/** @brief Returns the monitor that hosts the window */
		inline const Monitor& monitor() const { return monitor(windowMonitorIndex()); }

		/** @brief Returns the current video mode for the specified monitor */
		virtual const VideoMode& currentVideoMode(unsigned int monitorIndex) const = 0;
		/** @brief Returns the current video mode for the monitor that hosts the window */
		inline const VideoMode& currentVideoMode() const { return currentVideoMode(windowMonitorIndex()); }
		/**
		 * @brief Sets the video mode used in full screen by the monitor that hosts the window
		 *
		 * @note Call this method <b>before</b> enabling full screen.
		 */
		inline virtual bool setVideoMode(unsigned int modeIndex) { return false; }

		/** @brief Returns the scaling factor for the application window */
		float windowScalingFactor() const;

	protected:
		static constexpr float DefaultDPI = 96.0f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		/** @brief Window width in screen coordinates */
		std::int32_t width_;
		/** @brief Window height in screen coordinates */
		std::int32_t height_;
		/** @brief Window width in pixels (for HiDPI screens) */
		std::int32_t drawableWidth_;
		/** @brief Window height in pixels (for HiDPI screens) */
		std::int32_t drawableHeight_;
		/** @brief Whether rendering occurs in full screen */
		bool isFullscreen_;
		/** @brief OpenGL context creation attributes */
		ContextInfo contextInfo_;
		/** @brief Display properties */
		DisplayMode displayMode_;

		Monitor monitors_[MaxMonitors];
		std::uint32_t numMonitors_;
		/** @brief Cache to avoid searching the current video mode in a monitor's array */
		mutable VideoMode currentVideoMode_;
#endif

		/** @brief Initializes the OpenGL viewport based on the drawable resolution */
		void initDeviceViewport();

		/** @brief Updates the array of connected monitors */
		inline virtual void updateMonitors() { }

		virtual void setResolutionInternal(int width, int height) = 0;

	private:
		/** @brief Sets up the initial OpenGL state for the scene graph */
		virtual void setupDevice();

		/** @brief Updates the screen by swapping the back and front buffers */
		virtual void update() = 0;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		static bool emscriptenHandleResize(int eventType, const EmscriptenUiEvent* event, void* userData);
		static bool emscriptenHandleFullscreen(int eventType, const EmscriptenFullscreenChangeEvent* event, void* userData);

#	if defined(WITH_GLFW)
		static bool emscriptenHandleFocus(int eventType, const EmscriptenFocusEvent* event, void* userData);
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

		void setSwapInterval(int interval) override {}

		void setResolution(bool fullscreen, int width = 0, int height = 0) override {}

		void setWindowPosition(int x, int y) override {}
		void setWindowTitle(StringView windowTitle) override {}
		void setWindowIcon(StringView iconFilename) override {}

		void setWindowSize(int width, int height) override {}

		void flashWindow() const override {}

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override { return currentVideoMode_; }

	protected:
		void setResolutionInternal(int width, int height) override {}

	private:
		void setupDevice() override {}
		void update() override {}
	};
#endif
}
