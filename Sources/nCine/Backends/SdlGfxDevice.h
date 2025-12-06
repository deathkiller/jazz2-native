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
	/// The SDL based graphics device
	class SdlGfxDevice : public IGfxDevice
	{
	public:
		SdlGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode);
		~SdlGfxDevice() override;

		void setSwapInterval(int interval) override;

		void setResolution(bool fullscreen, int width = 0, int height = 0) override;

		void update() override;

		void setWindowPosition(int x, int y) override;
		void setWindowSize(int width, int height) override;

		void setWindowTitle(StringView windowTitle) override;
		void setWindowIcon(StringView windowIconFilename) override;

		const Vector2i windowPosition() const override;

		void flashWindow() const override;

		unsigned int windowMonitorIndex() const override;

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;
		bool setVideoMode(unsigned int modeIndex) override;

#if !defined(DEATH_TARGET_VITA)
		static inline bool isMainWindow(std::uint32_t windowId) {
			SDL_Window* windowHandle = SDL_GetWindowFromID(windowId);
			return (windowHandle == windowHandle_);
		}

		static inline SDL_Window* windowHandle() {
			return windowHandle_;
		}

		static inline SDL_GLContext glContextHandle() {
			return glContextHandle_;
		}
#endif

	protected:
		void setResolutionInternal(int width, int height) override;
		void updateMonitors() override;

	private:
#if !defined(DEATH_TARGET_VITA)
		/// SDL2 window handle
		static SDL_Window* windowHandle_;
		/// SDL2 OpenGL context handle
		static SDL_GLContext glContextHandle_;
#endif

		/// Deleted copy constructor
		SdlGfxDevice(const SdlGfxDevice&) = delete;
		/// Deleted assignment operator
		SdlGfxDevice& operator=(const SdlGfxDevice&) = delete;

		/// Initilizes the video subsystem (SDL)
		void initGraphics(bool enableWindowScaling);
		/// Initilizes the OpenGL graphic context
		void initDevice(int windowPosX, int windowPosY, bool isResizable);

		void convertVideoModeInfo(const SDL_DisplayMode& sdlVideoMode, IGfxDevice::VideoMode& videoMode) const;

		friend class SdlInputManager;
	};
}

#endif