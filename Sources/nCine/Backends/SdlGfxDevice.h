#pragma once

#if defined(WITH_SDL2) || defined(WITH_SDL3) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Primitives/Vector2.h"
#include "../Graphics/IGfxDevice.h"
#include "../Graphics/DisplayMode.h"

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if defined(WITH_SDL3) && __has_include("SDL3/SDL.h")
#		define __HAS_LOCAL_SDL3
#	elif __has_include("SDL2/SDL.h")
#		define __HAS_LOCAL_SDL
#	endif
#endif
#if defined(WITH_SDL3)
#	if defined(__HAS_LOCAL_SDL3)
#		include "SDL3/SDL.h"
#	else
#		include <SDL3/SDL.h>
#	endif
#elif defined(__HAS_LOCAL_SDL)
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

		void SetSwapInterval(int interval) override;

		void SetResolution(bool fullscreen, int width = 0, int height = 0) override;

		void Update() override;

		void SetWindowPosition(int x, int y) override;
		void SetWindowSize(int width, int height) override;
		void SetDrawableSize(int width, int height) override;

		void SetWindowTitle(StringView windowTitle) override;
		void SetWindowIcon(StringView windowIconFilename) override;

		const Vector2i GetWindowPosition() const override;

		void FlashWindow() const override;

		unsigned int GetWindowMonitorIndex() const override;

		const VideoMode& GetCurrentVideoMode(unsigned int monitorIndex) const override;
		bool SetVideoMode(unsigned int modeIndex) override;

		/**
		 * @brief Returns `true` if the specified window ID refers to the main window
		 *
		 * @param windowId	SDL2 window identifier to compare against the main window
		 */
		static inline bool IsMainWindow(std::uint32_t windowId) {
			SDL_Window* windowHandle = SDL_GetWindowFromID(windowId);
			return (windowHandle == _windowHandle);
		}

		/** @brief Returns the SDL2 handle of the main window */
		static inline SDL_Window* GetWindowHandle() {
			return _windowHandle;
		}

		/** @brief Returns the SDL2 OpenGL context handle */
		static inline SDL_GLContext GetGlContextHandle() {
			return _glContextHandle;
		}

		/**
		 * @brief Queries a window's drawable (pixel) size the way the active RHI backend measures it
		 *
		 * Confines the per-backend drawable-size query (SDL renderer output size on the software backend,
		 * `SDL_GL_GetDrawableSize` elsewhere) to one place; @p fallbackWidth / @p fallbackHeight are used
		 * when the query returns a non-positive size.
		 */
		static void QueryDrawableSize(SDL_Window* windowHandle, int fallbackWidth, int fallbackHeight, int& width, int& height);

#if defined(NCINE_HAS_AUXILIARY_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
		bool SupportsAuxiliaryWindows() const override;
		std::unique_ptr<AuxiliaryWindow> CreateAuxiliaryWindow(const AuxiliaryWindowDesc& desc) override;
		bool GetGlobalMousePosition(Vector2i& position) const override;
		float GetWindowContentScale() const override;
		bool GetGlobalMouseButtons(std::uint32_t& buttons) const override;
		bool GetDesktopWorkArea(Vector2i point, Recti& area) const override;

		/** @brief Returns `true` if the SDL window ID is one of the auxiliary windows of the application, see @ref CreateAuxiliaryWindow() */
		static bool IsAuxiliaryWindow(std::uint32_t windowId);
		/**
		 * @brief Converts a position over a window of the application to one relative to the main window
		 *
		 * A position over an auxiliary window can be outside of the main window then. A position over the main window
		 * stays as it is.
		 */
		static Vector2i ToMainWindowPosition(std::uint32_t windowId, Vector2i position);
#else
		static bool IsAuxiliaryWindow(std::uint32_t windowId) {
			return false;
		}
		static Vector2i ToMainWindowPosition(std::uint32_t windowId, Vector2i position) {
			return position;
		}
#endif
		/** @brief Returns `true` if the SDL window ID is the main window or an auxiliary window of the application */
		static bool IsOwnWindow(std::uint32_t windowId) {
			return (IsMainWindow(windowId) || IsAuxiliaryWindow(windowId));
		}

		/**
		 * @brief Follows a fullscreen change of the main window that the window system reports
		 *
		 * Only a change of the reported state is taken over, so a request still pending in the window system
		 * is not undone by an event that arrives before it was confirmed.
		 */
		void SyncFullscreenState();

	protected:
		void SetResolutionInternal(int width, int height) override;

		void UpdateMonitors() override;

	private:
		/** @brief SDL2 window handle */
		static SDL_Window* _windowHandle;
		/** @brief SDL2 OpenGL context handle */
		static SDL_GLContext _glContextHandle;
#if defined(DEATH_TARGET_IOS)
		/** @brief Color renderbuffer of SDL's GL view, which must be bound when a frame is presented (see @ref update()) */
		static unsigned int _iosColorRenderbuffer;
#endif
		/** @brief Fullscreen state of the main window as the window system last reported it (see @ref SyncFullscreenState()) */
		bool _isWindowFullscreen = false;

		/** @brief Deleted copy constructor */
		SdlGfxDevice(const SdlGfxDevice&) = delete;
		/** @brief Deleted assignment operator */
		SdlGfxDevice& operator=(const SdlGfxDevice&) = delete;

		/** @brief Initializes the SDL2 video subsystem */
		void InitGraphics(bool enableWindowScaling);
		/** @brief Initializes the OpenGL graphics context */
		void InitDevice(int windowPosX, int windowPosY, bool isResizable);

#if defined(WITH_RHI_SOFTWARE)
		/** @brief SDL2 renderer used to present the CPU framebuffer (software backend, no GL context) */
		SDL_Renderer* _softwareRenderer = nullptr;
		/** @brief Streaming texture the software backend's screen framebuffer is uploaded into each frame */
		SDL_Texture* _softwareTexture = nullptr;
		/** @brief Current width of @ref _softwareTexture and the backend screen framebuffer */
		std::int32_t _softwareTextureWidth = 0;
		/** @brief Current height of @ref _softwareTexture and the backend screen framebuffer */
		std::int32_t _softwareTextureHeight = 0;

		/** @brief Creates the SDL2 renderer and the initial streaming target for the software present path */
		void InitSoftwarePresent(bool hasVSync);
		/** @brief (Re)creates the streaming texture and resizes the backend screen framebuffer to match */
		void ResizeSoftwareTarget(int width, int height);
		/** @brief Uploads and blits the backend screen framebuffer to the window (replaces the GL buffer swap) */
		void PresentSoftware();
#endif

		void ConvertVideoModeInfo(const SDL_DisplayMode& sdlVideoMode, IGfxDevice::VideoMode& videoMode) const;

		friend class SdlInputManager;
	};
}

#endif