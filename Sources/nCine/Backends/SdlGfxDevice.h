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

#if defined(WITH_RHI_SW)
#	include "../Graphics/RHI/RHI.h"
#endif

namespace nCine::Backends
{
	/// The SDL based graphics device
	class SdlGfxDevice : public IGfxDevice
	{
		friend class SdlInputManager;

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

		static inline bool isMainWindow(std::uint32_t windowId) {
			SDL_Window* windowHandle = SDL_GetWindowFromID(windowId);
			return (windowHandle == windowHandle_);
		}

		static inline SDL_Window* windowHandle() {
			return windowHandle_;
		}

#if !defined(WITH_RHI_SW)
		static inline SDL_GLContext glContextHandle() {
			return glContextHandle_;
		}
#endif

#if defined(WITH_RHI_SW)
		/// Recreates the streaming texture and resizes the SW color buffer after a window resize
		void resizeSwBuffer(int width, int height);
		/// Resizes the SW color buffer to a logical resolution (SDL stretches to window)
		void resizeSwBufferToLogical(int logicalWidth, int logicalHeight);
#endif

	protected:
		void setResolutionInternal(int width, int height) override;

		void updateMonitors() override;

	private:
		/// SDL2 window handle
		static SDL_Window* windowHandle_;
#if !defined(WITH_RHI_SW)
		/// SDL2 OpenGL context handle
		static SDL_GLContext glContextHandle_;
#endif

#if defined(WITH_RHI_SW)
		/// SDL2 hardware-accelerated renderer used to present the SW color buffer
		SDL_Renderer* swRenderer_ = nullptr;
		/// Streaming SDL_Texture that receives the SW color buffer each frame
		SDL_Texture* swTexture_ = nullptr;
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
	};
}

#endif