#pragma once

#if defined(WITH_SDL) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Primitives/Vector2.h"
#include "../Graphics/IGfxDevice.h"
#include "../Graphics/DisplayMode.h"

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("SDL2/SDL.h")
#		define __HAS_LOCAL_SDL
#	endif
#endif
#if defined(__HAS_LOCAL_SDL)
#	include "SDL2/SDL.h"
#else
#	include <SDL.h>
#endif

namespace nCine::Backends
{
	/**
		@brief SDL2-based graphics device
		
		Manages the application window and OpenGL context using the SDL2 library.
	*/
	class SdlGfxDevice : public IGfxDevice
	{
	public:
		SdlGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);
		~SdlGfxDevice() override;

		void setSwapInterval(int interval) override;

		void setResolution(bool fullscreen, int width = 0, int height = 0) override;

		void update() override;

		inline void setWindowPosition(int x, int y) override { SDL_SetWindowPosition(windowHandle_, x, y); }

		void setWindowSize(int width, int height) override;

		inline void setWindowTitle(StringView windowTitle) override {
			SDL_SetWindowTitle(windowHandle_, String::nullTerminatedView(windowTitle).data());
		}
		void setWindowIcon(StringView windowIconFilename) override;

		const Vector2i windowPosition() const override;

		void flashWindow() const override;

		unsigned int windowMonitorIndex() const override;

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;
		bool setVideoMode(unsigned int modeIndex) override;

		/**
		 * @brief Returns `true` if the specified window ID refers to the main window
		 *
		 * @param windowId	SDL2 window identifier to compare against the main window
		 */
		static inline bool isMainWindow(std::uint32_t windowId) {
			SDL_Window* windowHandle = SDL_GetWindowFromID(windowId);
			return (windowHandle == windowHandle_);
		}

		/** @brief Returns the SDL2 handle of the main window */
		static inline SDL_Window* windowHandle() {
			return windowHandle_;
		}

		/** @brief Returns the SDL2 OpenGL context handle */
		static inline SDL_GLContext glContextHandle() {
			return glContextHandle_;
		}

	protected:
		void setResolutionInternal(int width, int height) override;

		void updateMonitors() override;

	private:
		/** @brief SDL2 window handle */
		static SDL_Window* windowHandle_;
		/** @brief SDL2 OpenGL context handle */
		static SDL_GLContext glContextHandle_;

		/** @brief Deleted copy constructor */
		SdlGfxDevice(const SdlGfxDevice&) = delete;
		/** @brief Deleted assignment operator */
		SdlGfxDevice& operator=(const SdlGfxDevice&) = delete;

		/** @brief Initializes the SDL2 video subsystem */
		void initGraphics(bool enableWindowScaling);
		/** @brief Initializes the OpenGL graphics context */
		void initDevice(int windowPosX, int windowPosY, bool isResizable);

		void convertVideoModeInfo(const SDL_DisplayMode& sdlVideoMode, IGfxDevice::VideoMode& videoMode) const;

		friend class SdlInputManager;
	};
}

#endif