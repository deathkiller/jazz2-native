#pragma once

#if defined(WITH_SDL)

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

namespace nCine
{
	/// The SDL based graphics device
	class SdlGfxDevice : public IGfxDevice
	{
	public:
		SdlGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode);
		~SdlGfxDevice() override;

		void setSwapInterval(int interval) override;

		void setResolution(bool fullscreen, int width = 0, int height = 0) override;

		inline void update() override {
			SDL_GL_SwapWindow(windowHandle_);
		}

		inline void setWindowPosition(int x, int y) override { SDL_SetWindowPosition(windowHandle_, x, y); }

		void setWindowSize(int width, int height) override;

		inline void setWindowTitle(const StringView& windowTitle) override {
			SDL_SetWindowTitle(windowHandle_, String::nullTerminatedView(windowTitle).data());
		}
		void setWindowIcon(const StringView& windowIconFilename) override;

		const Vector2i windowPosition() const override;

		void flashWindow() const override;

		unsigned int windowMonitorIndex() const override;

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override;
		bool setVideoMode(unsigned int modeIndex) override;

		static inline SDL_Window* windowHandle() {
			return windowHandle_;
		}

	protected:
		void setResolutionInternal(int width, int height) override;

		void updateMonitors() override;

	private:
		/// SDL2 window handle
		static SDL_Window* windowHandle_;
		/// SDL2 OpenGL context handle
		SDL_GLContext glContextHandle_;

		/// Deleted copy constructor
		SdlGfxDevice(const SdlGfxDevice&) = delete;
		/// Deleted assignment operator
		SdlGfxDevice& operator=(const SdlGfxDevice&) = delete;

		/// Initilizes the video subsystem (SDL)
		void initGraphics(bool enableWindowScaling);
		/// Initilizes the OpenGL graphic context
		void initDevice(bool isResizable);

		void convertVideoModeInfo(const SDL_DisplayMode& sdlVideoMode, IGfxDevice::VideoMode& videoMode) const;

		friend class SdlInputManager;
	};
}

#endif