#if defined(WITH_SDL2) || defined(WITH_SDL3)

#include "SdlGfxDevice.h"
#include "../Graphics/ITextureLoader.h"
#include "SdlAuxiliaryWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(DEATH_TARGET_IOS) && !defined(WITH_RHI_SOFTWARE) && !defined(WITH_RHI_METAL)
	// The GL view's framebuffer and renderbuffer handles (see initDevice)
#	include <SDL_syswm.h>
#endif

#include "../Graphics/RHI/Rhi.h"
#if defined(DEATH_TARGET_IOS) && defined(WITH_SDL2)
#	include "IosSceneDelegate.h"
#endif
#if defined(WITH_RHI_GXM)
#	include "../Application.h"	// setDrawableSize() re-lays the application out for the resized frame surface
#endif

#if defined(WITH_RHI_D3D11) && defined(WITH_SDL2)
// SDL2 only: the native HWND the DXGI swap chain is created for is obtained through SDL_syswm.h.
// SDL3 removed SDL_syswm.h entirely (the HWND is read from the window property store instead, see initDevice).
#	if defined(__HAS_LOCAL_SDL)
#		include "SDL2/SDL_syswm.h"
#	else
#		include <SDL_syswm.h>
#	endif
#endif

#if defined(WITH_RHI_VULKAN)
// SDL_WINDOW_VULKAN + the drawable-size query for the Vulkan swap-chain window (the surface itself is
// created inside VulkanDevice from this window via SDL_Vulkan_CreateSurface)
#	if defined(WITH_SDL3)
#		if defined(__HAS_LOCAL_SDL3)
#			include "SDL3/SDL_vulkan.h"
#		else
#			include <SDL3/SDL_vulkan.h>
#		endif
#	elif defined(__HAS_LOCAL_SDL)
#		include "SDL2/SDL_vulkan.h"
#	else
#		include <SDL_vulkan.h>
#	endif
#elif defined(WITH_RHI_METAL)
// SDL_WINDOW_METAL + the drawable-size query for the Metal window (the Metal view and its CAMetalLayer are
// created inside MetalDevice from this window via SDL_Metal_CreateView)
#	if defined(WITH_SDL3)
#		if defined(__HAS_LOCAL_SDL3)
#			include "SDL3/SDL_metal.h"
#		else
#			include <SDL3/SDL_metal.h>
#		endif
#	elif defined(__HAS_LOCAL_SDL)
#		include "SDL2/SDL_metal.h"
#	else
#		include <SDL_metal.h>
#	endif
#endif

#if defined(WITH_GLEW)
#	define GLEW_NO_GLU
#	include <GL/glew.h>
#elif defined(WITH_RHI_GL) && defined(RHI_GL_PROFILE_ES)
// No GLEW on the OpenGL|ES (ANGLE) path; pull in the GL/ES headers (GLubyte, glGetString, ...) directly
#	define NCINE_INCLUDE_OPENGL
#	include "../CommonHeaders.h"
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/html5.h>
#endif

// A handful of SDL2 window flags/helpers were renamed in SDL3; these keep the shared window-creation code
// readable without an #if at every use site
#if defined(WITH_SDL3)
#	define NCINE_SDL_WINDOW_HIGHDPI SDL_WINDOW_HIGH_PIXEL_DENSITY
#	define NCINE_SDL_WINDOW_FULLSCREEN_DESKTOP SDL_WINDOW_FULLSCREEN
#	define NCINE_SDL_BOOL(x) (x)
// SDL3 renamed the alpha-less packed pixel formats to an "X" prefix. <SDL3/SDL_oldnames.h> maps the old names
// as error sentinels (old names disabled), so #undef then remap them to keep the convertVideoModeInfo switch shared.
#	undef SDL_PIXELFORMAT_RGB444
#	undef SDL_PIXELFORMAT_RGB555
#	undef SDL_PIXELFORMAT_BGR555
#	undef SDL_PIXELFORMAT_RGB888
#	undef SDL_PIXELFORMAT_BGR888
#	define SDL_PIXELFORMAT_RGB444 SDL_PIXELFORMAT_XRGB4444
#	define SDL_PIXELFORMAT_RGB555 SDL_PIXELFORMAT_XRGB1555
#	define SDL_PIXELFORMAT_BGR555 SDL_PIXELFORMAT_XBGR1555
#	define SDL_PIXELFORMAT_RGB888 SDL_PIXELFORMAT_XRGB8888
#	define SDL_PIXELFORMAT_BGR888 SDL_PIXELFORMAT_XBGR8888
#else
#	define NCINE_SDL_WINDOW_HIGHDPI SDL_WINDOW_ALLOW_HIGHDPI
#	define NCINE_SDL_WINDOW_FULLSCREEN_DESKTOP SDL_WINDOW_FULLSCREEN_DESKTOP
#	define NCINE_SDL_BOOL(x) ((x) ? SDL_TRUE : SDL_FALSE)
#endif

namespace nCine::Backends
{
#if defined(DEATH_TARGET_MORPHOS) && defined(WITH_RHI_LEGACYGL)
	/** @brief Points MorphOS' TinyGL globals at the context SDL created (MorphOSTinyGl.cpp) */
	bool MorphOsAttachTinyGl(void* glContext);
	/** @brief Releases what @ref MorphOsAttachTinyGl() acquired */
	void MorphOsDetachTinyGl();
#endif

	SDL_Window* SdlGfxDevice::_windowHandle = nullptr;
	SDL_GLContext SdlGfxDevice::_glContextHandle;
#if defined(DEATH_TARGET_IOS)
	unsigned int SdlGfxDevice::_iosColorRenderbuffer = 0;
#endif

#if defined(WITH_SDL3) && !defined(DEATH_TARGET_VITA)
	namespace
	{
		// SDL3 identifies displays by an opaque SDL_DisplayID rather than a 0-based index. The engine's _monitors
		// array is still index-based, so these translate between a DisplayID and its position in SDL_GetDisplays()
		// order (which is exactly the order updateMonitors() enumerates _monitors in).
		unsigned int displayIndexFromId(SDL_DisplayID id)
		{
			int count = 0;
			SDL_DisplayID* displays = SDL_GetDisplays(&count);
			unsigned int index = 0;
			if (displays != nullptr) {
				for (int i = 0; i < count; i++) {
					if (displays[i] == id) {
						index = static_cast<unsigned int>(i);
						break;
					}
				}
				SDL_free(displays);
			}
			return index;
		}

		SDL_DisplayID displayIdFromIndex(unsigned int index)
		{
			int count = 0;
			SDL_DisplayID* displays = SDL_GetDisplays(&count);
			SDL_DisplayID id = 0;
			if (displays != nullptr) {
				if (static_cast<int>(index) < count) {
					id = displays[index];
				} else if (count > 0) {
					id = displays[0];
				}
				SDL_free(displays);
			}
			return id;
		}
	}
#endif

	SdlGfxDevice::SdlGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode)
	{
		InitGraphics(windowMode.HasWindowScaling);
		UpdateMonitors();
		InitDevice(windowMode.WindowPositionX, windowMode.WindowPositionY, windowMode.IsResizable);

		if (_windowHandle != nullptr) {
			_isWindowFullscreen = (SDL_GetWindowFlags(_windowHandle) & SDL_WINDOW_FULLSCREEN) != 0;
		}
	}

	SdlGfxDevice::~SdlGfxDevice()
	{
		LOGD("Disposing graphics device...");

		// Uniform across backends: tears down the D3D11 / Vulkan / Metal device + swap chain, no-op on OpenGL / software
		RHI::Device::DestroySwapchain();
#if defined(WITH_RHI_SOFTWARE)
		if (_softwareTexture != nullptr) {
			SDL_DestroyTexture(_softwareTexture);
			_softwareTexture = nullptr;
		}
		if (_softwareRenderer != nullptr) {
			SDL_DestroyRenderer(_softwareRenderer);
			_softwareRenderer = nullptr;
		}
#elif defined(DEATH_TARGET_VITA)
		// Vita renders through vitaGL (brought up with vglInit() in initDevice), not an SDL-managed GL context.
		// No explicit teardown is issued here.
#elif !defined(WITH_RHI_D3D11) && !defined(WITH_RHI_VULKAN) && !defined(WITH_RHI_METAL)
#	if defined(WITH_RHI_LEGACYGL)
		// Leave the context idle before it is destroyed - anything still batched refers to memory that is
		// about to go away with the backend's frame arena
		RHI::Device::ShutdownGl();
#		if defined(DEATH_TARGET_MORPHOS)
		Backends::MorphOsDetachTinyGl();
#		endif
#	endif
#	if defined(WITH_SDL3)
		SDL_GL_DestroyContext(_glContextHandle);
#	else
		SDL_GL_DeleteContext(_glContextHandle);
#	endif
		_glContextHandle = nullptr;
#endif
		SDL_DestroyWindow(_windowHandle);
		_windowHandle = nullptr;

		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_Quit();
	}

	void SdlGfxDevice::QueryDrawableSize(SDL_Window* windowHandle, int fallbackWidth, int fallbackHeight, int& width, int& height)
	{
		// The one place that knows how the active backend measures a window's pixel size
#if defined(WITH_RHI_GXM)
		// The sceGxm backend renders the frame into an intermediate surface of its own and stretches that onto
		// the panel at present time, so the drawable size is the surface's - smaller than the panel, and nothing
		// SDL knows about (see GxmDevice::ScreenWidth). Before the session is up this is the default size the
		// surface is then created with; afterwards it is whatever setDrawableSize() last made it.
		static_cast<void>(windowHandle);
		width = RHI::Device::GetScreenWidth();
		height = RHI::Device::GetScreenHeight();
#elif defined(DEATH_TARGET_VITA)
		// vitaGL owns the framebuffer and renders to the fixed panel; SDL's idea of the window size can be
		// skewed by HighDPI scaling, and a frame sized after it lands in a corner of the screen
		static_cast<void>(windowHandle);
		width = 960;
		height = 544;
#elif defined(WITH_RHI_SOFTWARE)
		SDL_Renderer* renderer = SDL_GetRenderer(windowHandle);
		if (renderer != nullptr) {
#	if defined(WITH_SDL3)
			SDL_GetRenderOutputSize(renderer, &width, &height);
#	else
			SDL_GetRendererOutputSize(renderer, &width, &height);
#	endif
		} else {
			width = 0;
			height = 0;
		}
#elif defined(WITH_SDL3)
		// SDL3 removed SDL_GL_GetDrawableSize / SDL_Vulkan_GetDrawableSize; the pixel (drawable) size of any
		// window is now queried uniformly through SDL_GetWindowSizeInPixels
		SDL_GetWindowSizeInPixels(windowHandle, &width, &height);
#elif defined(WITH_RHI_VULKAN)
		SDL_Vulkan_GetDrawableSize(windowHandle, &width, &height);
#elif defined(WITH_RHI_METAL)
		// The layer's drawable size once the Metal view exists, the window's pixel size before
		SDL_Metal_GetDrawableSize(windowHandle, &width, &height);
#else
		SDL_GL_GetDrawableSize(windowHandle, &width, &height);
#endif
		if (width <= 0 || height <= 0) {
			width = fallbackWidth;
			height = fallbackHeight;
		}
	}

	void SdlGfxDevice::SetSwapInterval(int interval)
	{
#if defined(WITH_RHI_SOFTWARE) || defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN) || defined(WITH_RHI_METAL)
		// No GL context; vsync is fixed at device/swap-chain creation time (see the present path)
		static_cast<void>(interval);
#else
		SDL_GL_SetSwapInterval(interval);
#endif
	}

	void SdlGfxDevice::SetResolution(bool fullscreen, int width, int height)
	{
		_isFullscreen = fullscreen;

#if defined(WITH_SDL3)
		// SDL3: SDL_SetWindowFullscreen takes a bool; the fullscreen video mode (or desktop/borderless when
		// NULL) is chosen separately via SDL_SetWindowFullscreenMode. SDL_WINDOW_FULLSCREEN_DESKTOP is gone.
#	if defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_SetWindowFullscreen(_windowHandle, fullscreen);
		if (width > 0 && height > 0) {
			_width = width;
			_height = height;
		}
#	else
		if (fullscreen) {
			if (width <= 0 || height <= 0) {
				SDL_SetWindowFullscreenMode(_windowHandle, nullptr);	// desktop (borderless) fullscreen
				SDL_SetWindowFullscreen(_windowHandle, true);
			} else {
				_width = width;
				_height = height;
				SDL_SetWindowFullscreen(_windowHandle, true);
				SDL_SetWindowSize(_windowHandle, width, height);
			}
		} else {
			SDL_SetWindowFullscreen(_windowHandle, false);
			if (width > 0 && height > 0) {
				_width = width;
				_height = height;
				SDL_SetWindowSize(_windowHandle, width, height);
			}
		}
#	endif
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_SetWindowFullscreen(_windowHandle, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		if (width > 0 && height > 0) {
			_width = width;
			_height = height;
		}
#else
		if (fullscreen) {
			if (width <= 0 || height <= 0) {
				SDL_SetWindowFullscreen(_windowHandle, SDL_WINDOW_FULLSCREEN_DESKTOP);
			} else {
				_width = width;
				_height = height;
				SDL_SetWindowFullscreen(_windowHandle, SDL_WINDOW_FULLSCREEN);
				SDL_SetWindowSize(_windowHandle, width, height);
			}
		} else {
			SDL_SetWindowFullscreen(_windowHandle, 0);
			if (width > 0 && height > 0) {
				_width = width;
				_height = height;
				SDL_SetWindowSize(_windowHandle, width, height);
			}
		}
#endif

		SDL_GetWindowSize(_windowHandle, &_width, &_height);
		QueryDrawableSize(_windowHandle, _width, _height, _drawableWidth, _drawableHeight);
		RHI::Device::ResizeSwapchain(_drawableWidth, _drawableHeight);	// no-op on OpenGL / software
#if defined(WITH_RHI_SOFTWARE)
		ResizeSoftwareTarget(_drawableWidth, _drawableHeight);
#endif
	}

	void SdlGfxDevice::SyncFullscreenState()
	{
		// Fullscreen is asynchronous on SDL3 (incl. `sdl2-compat`), so the flag can lag behind the request
		// or never be set at all - only its changes are followed, otherwise a resize would undo the request
		const bool isWindowFullscreen = (SDL_GetWindowFlags(_windowHandle) & SDL_WINDOW_FULLSCREEN) != 0;
		if (_isWindowFullscreen != isWindowFullscreen) {
			_isWindowFullscreen = isWindowFullscreen;
			_isFullscreen = isWindowFullscreen;
		}
	}

	void SdlGfxDevice::Update()
	{
#if defined(WITH_RHI_SOFTWARE)
		PresentSoftware();
#elif defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN) || defined(WITH_RHI_METAL)
		// When the window is minimized there is no visible surface to present to: unlike a visible swap chain
		// (whose present blocks on vsync), the D3D11/Vulkan/Metal present becomes a non-blocking no-op, so the main
		// loop would otherwise spin at 100% CPU rendering frames nobody sees. PresentFrame() is still called so
		// the backend can tidy up any partially-recorded frame, then the loop is throttled to a low rate while
		// minimized. (The OpenGL and software arms are not compiled here, so they are unaffected.)
		RHI::Device::PresentFrame();
		if ((SDL_GetWindowFlags(_windowHandle) & SDL_WINDOW_MINIMIZED) != 0) {
			SDL_Delay(12);
		}
#elif defined(WITH_RHI_GXM)
		// The native sceGxm backend owns the Vita's display queue: it flips the frame's screen surface into the
		// next display buffer and hands that to the display controller
		RHI::Device::PresentFrame();
#elif defined(DEATH_TARGET_VITA)
		// Vita renders through vitaGL, which owns the GXM display; present its backbuffer directly (SDL neither
		// created nor manages this GL context). GL_FALSE = do not pump the SceCommonDialog overlay here.
		vglSwapBuffers(GL_FALSE);
#else
#	if defined(WITH_RHI_LEGACYGL)
		// The fixed-function backend batches draws and only submits them when it must, so the frame's tail
		// is still unsubmitted at this point; the swap below is what makes it visible
		RHI::Device::PresentFrame();
#	endif
#	if defined(DEATH_TARGET_IOS)
		// SDL presents whatever renderbuffer is bound, and the engine's own render-target renderbuffers may have been
		// bound since the last frame (see initDevice for the view's framebuffer)
		glBindRenderbuffer(GL_RENDERBUFFER, _iosColorRenderbuffer);
#	endif
		SDL_GL_SwapWindow(_windowHandle);
#	if defined(DEATH_TARGET_IOS)
		// SDL rebinds framebuffers of its own during the present, so the engine's cached bindings are stale now
		RHI::GL::GLFramebuffer::InvalidateCachedBindings();
#	endif
#endif
	}

	void SdlGfxDevice::SetResolutionInternal(int width, int height)
	{
		_width = width;
		_height = height;
		SDL_SetWindowSize(_windowHandle, width, height);
	}

	void SdlGfxDevice::SetWindowTitle(StringView windowTitle)
	{
#if !defined(DEATH_TARGET_VITA)
		SDL_SetWindowTitle(_windowHandle, String::nullTerminatedView(windowTitle).data());
#endif
	}

	void SdlGfxDevice::SetWindowIcon(StringView windowIconFilename)
	{
#if !defined(DEATH_TARGET_VITA)
		std::unique_ptr<ITextureLoader> image = ITextureLoader::CreateFromFile(windowIconFilename);
		const unsigned int bytesPerPixel = image->GetTexFormat().GetChannelCount();
		const int pitch = image->GetWidth() * bytesPerPixel;
		void* pixels = reinterpret_cast<void*>(const_cast<std::uint8_t*>(image->GetPixels()));
#	if defined(WITH_SDL3)
		// SDL3: SDL_CreateRGBSurfaceWithFormatFrom -> SDL_CreateSurfaceFrom (reordered args, no bit-depth
		// parameter), SDL_FreeSurface -> SDL_DestroySurface, and SDL_PIXELFORMAT_BGR888 -> SDL_PIXELFORMAT_XBGR8888
		// RGBA32/RGB24 rather than the packed names, so the channel order holds on a big-endian machine too
		const SDL_PixelFormat pixelFormat = (bytesPerPixel == 4) ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24;
		SDL_Surface* surface = SDL_CreateSurfaceFrom(image->GetWidth(), image->GetHeight(), pixelFormat, pixels, pitch);
		SDL_SetWindowIcon(_windowHandle, surface);
		SDL_DestroySurface(surface);
#	else
		const Uint32 pixelFormat = (bytesPerPixel == 4) ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24;
		SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, image->GetWidth(), image->GetHeight(), bytesPerPixel * 8, pitch, pixelFormat);
		SDL_SetWindowIcon(_windowHandle, surface);
		SDL_FreeSurface(surface);
#	endif
#endif
	}

	void SdlGfxDevice::SetWindowPosition(int x, int y)
	{
#if !defined(DEATH_TARGET_VITA)
		SDL_SetWindowPosition(_windowHandle, x, y);
#endif
	}

	void SdlGfxDevice::SetWindowSize(int width, int height)
	{
#if !defined(DEATH_TARGET_VITA)
		// Change resolution only in case it is valid and it really changes
		if (width == 0 || height == 0 || (width == _width && height == _height)) {
			return;
		}

		if (!_isFullscreen) {
			SDL_SetWindowSize(_windowHandle, width, height);
			SDL_GetWindowSize(_windowHandle, &_width, &_height);
			QueryDrawableSize(_windowHandle, _width, _height, _drawableWidth, _drawableHeight);
			RHI::Device::ResizeSwapchain(_drawableWidth, _drawableHeight);	// no-op on OpenGL / software
#	if defined(WITH_RHI_SOFTWARE)
			ResizeSoftwareTarget(_drawableWidth, _drawableHeight);
#	endif
		}
#endif
	}

	void SdlGfxDevice::SetDrawableSize(int width, int height)
	{
#if defined(WITH_RHI_GXM)
		// The only backend here with a drawable of its own: the frame surface is recreated at the new size
		// (clamped to the panel), and the application lays its viewports out again for it exactly as it does
		// for a resized window - the swap chain itself (the panel) is untouched, so ResizeSwapchain() has
		// nothing to do
		if (RHI::Device::ResizeScreenSurface(width, height)) {
			_drawableWidth = RHI::Device::GetScreenWidth();
			_drawableHeight = RHI::Device::GetScreenHeight();
			theApplication().ResizeScreenViewport(_drawableWidth, _drawableHeight);
		}
#else
		static_cast<void>(width);
		static_cast<void>(height);
#endif
	}

	const Vector2i SdlGfxDevice::GetWindowPosition() const
	{
		Vector2i position(0, 0);
#if !defined(DEATH_TARGET_VITA)
		SDL_GetWindowPosition(_windowHandle, &position.X, &position.Y);
#endif
		return position;
	}

	void SdlGfxDevice::FlashWindow() const
	{
#if SDL_VERSION_ATLEAST(2, 0, 16) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_VITA)
		SDL_FlashWindow(_windowHandle, SDL_FLASH_UNTIL_FOCUSED);
#endif
	}

	unsigned int SdlGfxDevice::GetWindowMonitorIndex() const
	{
#if defined(DEATH_TARGET_VITA)
		return 0;
#elif defined(WITH_SDL3)
		return (_windowHandle != nullptr ? displayIndexFromId(SDL_GetDisplayForWindow(_windowHandle)) : 0);
#else
		const int retrievedIndex = (_windowHandle != nullptr ? SDL_GetWindowDisplayIndex(_windowHandle) : 0);
		return (retrievedIndex >= 0 ? static_cast<unsigned int>(retrievedIndex) : 0);
#endif
	}

	const IGfxDevice::VideoMode& SdlGfxDevice::GetCurrentVideoMode(unsigned int monitorIndex) const
	{
		if (monitorIndex >= _numMonitors)
			monitorIndex = 0;

#if defined(WITH_SDL3)
		const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayIdFromIndex(monitorIndex));
		if (mode != nullptr) {
			ConvertVideoModeInfo(*mode, _currentVideoMode);
		}
#else
		SDL_DisplayMode mode;
		SDL_GetCurrentDisplayMode(monitorIndex, &mode);
		ConvertVideoModeInfo(mode, _currentVideoMode);
#endif

		return _currentVideoMode;
	}

	bool SdlGfxDevice::SetVideoMode(unsigned int modeIndex)
	{
#if defined(WITH_SDL3)
		SDL_DisplayID displayId = SDL_GetDisplayForWindow(_windowHandle);
		unsigned int displayIndex = displayIndexFromId(displayId);
		if (displayIndex >= _numMonitors) {
			displayIndex = 0;
			displayId = displayIdFromIndex(0);
		}

		if ((std::int32_t)modeIndex < _monitors[displayIndex].VideoModeCount) {
			int count = 0;
			// SDL3: SDL_GetDisplayMode(index,...) -> SDL_GetFullscreenDisplayModes (array of mode pointers);
			// SDL_SetWindowDisplayMode -> SDL_SetWindowFullscreenMode
			SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayId, &count);
			bool result = false;
			if (modes != nullptr && (std::int32_t)modeIndex < count) {
				result = SDL_SetWindowFullscreenMode(_windowHandle, modes[modeIndex]);
			}
			SDL_free(modes);
			return result;
		}
#elif !defined(DEATH_TARGET_VITA)
		std::int32_t displayIndex = SDL_GetWindowDisplayIndex(_windowHandle);
		if (displayIndex < 0 || displayIndex >= (std::int32_t)_numMonitors) {
			displayIndex = 0;
		}

		if ((std::int32_t)modeIndex < _monitors[displayIndex].VideoModeCount) {
			SDL_DisplayMode mode;
			SDL_GetDisplayMode(displayIndex, modeIndex, &mode);
			return SDL_SetWindowDisplayMode(_windowHandle, &mode);
		}
#endif
		return false;
	}

	void SdlGfxDevice::InitGraphics(bool enableWindowScaling)
	{
/*#if defined(DEATH_DEBUG) && defined(DEATH_TRACE)
		SDL_LogSetOutputFunction([](void* userdata, int category, SDL_LogPriority priority, const char* message) {
			TraceLevel level;
			switch (priority) {
				default:
				case SDL_LOG_PRIORITY_VERBOSE:
				case SDL_LOG_PRIORITY_DEBUG: level = TraceLevel::Debug; break;
				case SDL_LOG_PRIORITY_INFO: level = TraceLevel::Info; break;
				case SDL_LOG_PRIORITY_WARN: level = TraceLevel::Warning; break;
				case SDL_LOG_PRIORITY_ERROR: level = TraceLevel::Error; break;
				case SDL_LOG_PRIORITY_CRITICAL: level = TraceLevel::Fatal; break;
			}
			DEATH_TRACE(level, "SDL2!", "{}", message);
		}, nullptr);
		SDL_SetHint(SDL_HINT_EVENT_LOGGING, "1");
#endif*/

#if defined(SDL_HINT_APP_NAME)
		SDL_SetHint(SDL_HINT_APP_NAME, NCINE_APP_NAME);
#endif

#if defined(DEATH_TARGET_IOS)
		// Landscape only: the game's layout assumes a wide screen
		SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
		// SDL would otherwise register the accelerometer as a three-axis joystick, which the input manager opens
		// and the game then takes for a connected gamepad
		SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
		// Touches drive the on-screen controls through the touch events; a synthesized mouse click on top of them
		// would reach the menus a second time
		SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif

#if !defined(DEATH_TARGET_VITA)
#	if SDL_VERSION_ATLEAST(2, 24, 0) && defined(SDL_HINT_WINDOWS_DPI_SCALING)
		// Scaling is handled automatically by SDL (since v2.24.0)
		if (enableWindowScaling) {
			SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
		}
#	endif
#endif
#if defined(WITH_SDL3)
		// SDL3 return convention: functions report success as a bool (true) instead of SDL2's int 0
		const bool ok = SDL_InitSubSystem(SDL_INIT_VIDEO);
		FATAL_ASSERT_MSG(ok, "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: {}", SDL_GetError());
#else
		const int err = SDL_InitSubSystem(SDL_INIT_VIDEO);
		FATAL_ASSERT_MSG(!err, "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: {}", SDL_GetError());
#endif
	}

	void SdlGfxDevice::InitDevice(int windowPosX, int windowPosY, bool isResizable)
	{
#if defined(DEATH_TARGET_IOS)
		// One screen, and the application owns all of it: whatever size the configuration carries, the window is the
		// display (the zero size takes the fullscreen-desktop path below, which then reads the real size back)
		_width = 0;
		_height = 0;
#endif
#if defined(WITH_RHI_SOFTWARE) || defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN) || defined(WITH_RHI_METAL)
		// Non-OpenGL backends share one window-creation path: a plain SDL window (no GL context), from which
		// the backend presenter is then created (SDL renderer blit / DXGI swap chain / VkSwapchainKHR / CAMetalLayer)
#	if defined(WITH_RHI_SOFTWARE)
		LOGD("Initializing window (software renderer)...");
		Uint32 windowFlags = 0;
#	elif defined(WITH_RHI_D3D11)
		LOGD("Initializing window (Direct3D 11 renderer)...");
		Uint32 windowFlags = 0;
#	elif defined(WITH_RHI_METAL)
		LOGD("Initializing window (Metal renderer)...");
		Uint32 windowFlags = SDL_WINDOW_METAL;
#	else
		LOGD("Initializing window (Vulkan renderer)...");
		Uint32 windowFlags = SDL_WINDOW_VULKAN;
#	endif
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		windowFlags |= NCINE_SDL_WINDOW_HIGHDPI;
#	endif
		if (_width <= 0 || _height <= 0) {
			windowFlags |= NCINE_SDL_WINDOW_FULLSCREEN_DESKTOP;
			_isFullscreen = true;
		} else if (_isFullscreen) {
			windowFlags |= SDL_WINDOW_FULLSCREEN;
		}
		if (windowPosX == AppConfiguration::WindowPositionIgnore) {
			windowPosX = SDL_WINDOWPOS_UNDEFINED;
		}
		if (windowPosY == AppConfiguration::WindowPositionIgnore) {
			windowPosY = SDL_WINDOWPOS_UNDEFINED;
		}

#	if defined(WITH_SDL3)
		// SDL3 dropped the x/y parameters from SDL_CreateWindow; the position is applied separately afterwards
		_windowHandle = SDL_CreateWindow("", _width, _height, windowFlags);
		FATAL_ASSERT_MSG(_windowHandle, "SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_SetWindowPosition(_windowHandle, windowPosX, windowPosY);
#	else
		_windowHandle = SDL_CreateWindow("", windowPosX, windowPosY, _width, _height, windowFlags);
		FATAL_ASSERT_MSG(_windowHandle, "SDL_CreateWindow failed: {}", SDL_GetError());
#		if defined(DEATH_TARGET_IOS)
		Backends::IosAttachToWindowScene(_windowHandle);
#		endif
#	endif
		SDL_SetWindowResizable(_windowHandle, NCINE_SDL_BOOL(isResizable));
		// Resolution should be set to current screen size when it was left unspecified
		if (_width <= 0 || _height <= 0) {
			SDL_GetWindowSize(_windowHandle, &_width, &_height);
		}

#	if defined(WITH_RHI_SOFTWARE)
		InitSoftwarePresent(_displayMode.HasVSync());
#	else
		QueryDrawableSize(_windowHandle, _width, _height, _drawableWidth, _drawableHeight);
#		if defined(WITH_RHI_D3D11)
		{
			// The DXGI swap chain is created for the window's native HWND
#			if defined(WITH_SDL3)
			// SDL3 removed SDL_syswm.h; the native HWND is read from the window's property store instead
			void* nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(_windowHandle),
				SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
			FATAL_ASSERT_MSG(nativeWindow != nullptr, "SDL_GetWindowProperties(HWND) failed: {}", SDL_GetError());
			const bool created = RHI::Device::CreateSwapchain(nativeWindow,
				_drawableWidth, _drawableHeight, _displayMode.HasVSync());
#			else
			SDL_SysWMinfo wmInfo;
			SDL_VERSION(&wmInfo.version);
			const SDL_bool gotInfo = SDL_GetWindowWMInfo(_windowHandle, &wmInfo);
			FATAL_ASSERT_MSG(gotInfo == SDL_TRUE, "SDL_GetWindowWMInfo failed: {}", SDL_GetError());
			const bool created = RHI::Device::CreateSwapchain(reinterpret_cast<void*>(wmInfo.info.win.window),
				_drawableWidth, _drawableHeight, _displayMode.HasVSync());
#			endif
			FATAL_ASSERT_MSG(created, "Failed to create the Direct3D 11 device and swap chain");
		}
#		elif defined(WITH_RHI_METAL)
		{
			// The Metal backend takes the SDL_Window* directly (it attaches the Metal view to it via the SDL Metal
			// API and presents through the view's CAMetalLayer)
			const bool created = RHI::Device::CreateSwapchain(reinterpret_cast<void*>(_windowHandle),
				_drawableWidth, _drawableHeight, _displayMode.HasVSync());
			FATAL_ASSERT_MSG(created, "Failed to create the Metal device (no Metal-capable GPU is available on this machine)");
		}
#		else
		{
			// The Vulkan backend takes the SDL_Window* directly (it queries the required instance extensions
			// and creates the presentation surface from it via the SDL Vulkan API)
			const bool created = RHI::Device::CreateSwapchain(reinterpret_cast<void*>(_windowHandle),
				_drawableWidth, _drawableHeight, _displayMode.HasVSync());
			FATAL_ASSERT_MSG(created, "Failed to create the Vulkan device and swap chain");
		}
#		endif
#	endif

		InitDeviceViewport();
		return;
#endif
		// Setting OpenGL attributes
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, _displayMode.GetRedBits());
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, _displayMode.GetGreenBits());
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, _displayMode.GetBlueBits());
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, _displayMode.GetAlphaBits());
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, _displayMode.IsDoubleBuffered());
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, _displayMode.GetDepthBits());
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, _displayMode.GetStencilBits());
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, _contextInfo.MajorVersion);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, _contextInfo.MinorVersion);
#if defined(RHI_GL_PROFILE_ES)
		SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, _contextInfo.CoreProfile
															 ? SDL_GL_CONTEXT_PROFILE_CORE
															 : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
#if defined(WITH_RHI_LEGACYGL)
		// The fixed-function backend draws with what a forward-compatible context is defined to have
		// removed (the fixed-function pipeline itself), so that flag is never set for it
#else
		if (!_contextInfo.ForwardCompatible) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		}
#endif
		if (_contextInfo.DebugContext) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
		}

		LOGD("Initializing window...");

#if defined(WITH_RHI_GXM)
		// The native sceGxm backend owns the display itself, so asking SDL for an OpenGL-capable window makes
		// SDL_CreateWindow fail outright on this platform ("OpenGL support is either not configured in SDL or
		// not available in current SDL video driver"). A plain window is all the input and event handling needs.
		Uint32 flags = 0;
#else
		Uint32 flags = SDL_WINDOW_OPENGL;
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		flags |= NCINE_SDL_WINDOW_HIGHDPI;
#endif
		if (_width <= 0 || _height <= 0) {
			flags |= NCINE_SDL_WINDOW_FULLSCREEN_DESKTOP;
			_isFullscreen = true;
		} else if (_isFullscreen) {
			flags |= SDL_WINDOW_FULLSCREEN;
		}

		if (windowPosX == AppConfiguration::WindowPositionIgnore) {
			windowPosX = SDL_WINDOWPOS_UNDEFINED;
		}
		if (windowPosY == AppConfiguration::WindowPositionIgnore) {
			windowPosY = SDL_WINDOWPOS_UNDEFINED;
		}

		// Creating the window
#if defined(WITH_SDL3)
		// SDL3 dropped the x/y parameters from SDL_CreateWindow; the position is applied separately afterwards
		_windowHandle = SDL_CreateWindow("", _width, _height, flags);
		FATAL_ASSERT_MSG(_windowHandle, "SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_SetWindowPosition(_windowHandle, windowPosX, windowPosY);
#else
		_windowHandle = SDL_CreateWindow("", windowPosX, windowPosY, _width, _height, flags);
		FATAL_ASSERT_MSG(_windowHandle, "SDL_CreateWindow failed: {}", SDL_GetError());
#endif
#if defined(WITH_RHI_GXM) || defined(DEATH_TARGET_VITA)
		// On the Vita the frame's pixel size comes from the rendering backend rather than from the window, and
		// queryDrawableSize() is where that is decided - going through it here as well is what keeps the size
		// the game initializes with and the one the first window event resizes it to from disagreeing
		QueryDrawableSize(_windowHandle, _width, _height, _drawableWidth, _drawableHeight);
#elif defined(WITH_SDL3)
		SDL_GetWindowSizeInPixels(_windowHandle, &_drawableWidth, &_drawableHeight);
#else
		SDL_GL_GetDrawableSize(_windowHandle, &_drawableWidth, &_drawableHeight);
#endif
		InitDeviceViewport();

		SDL_SetWindowResizable(_windowHandle, NCINE_SDL_BOOL(isResizable));

		// resolution should be set to current screen size
		if (_width <= 0 || _height <= 0) {
			SDL_GetWindowSize(_windowHandle, &_width, &_height);
		}

#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		LOGD("Initializing OpenGL|ES {}.{} context...", _contextInfo.MajorVersion, _contextInfo.MinorVersion);
#else
		LOGD("Initializing OpenGL {}.{} {} context...", _contextInfo.MajorVersion, _contextInfo.MinorVersion,
			_contextInfo.CoreProfile ? "Core" : "Compatibility");
#endif

#if defined(WITH_RHI_GXM)
		// The native sceGxm backend drives the console's graphics API directly, so there is no GL context to
		// create at all: this brings up sceGxm itself, the display buffers and the shader patcher. It is not an
		// SDL-managed surface either - the Vita has one fixed 960x544 panel, which the window handle and the
		// requested size below say nothing about, so they are ignored.
		FATAL_ASSERT_MSG(RHI::Device::CreateSwapchain(nullptr, _drawableWidth, _drawableHeight, true),
			"Failed to initialize the sceGxm rendering backend");
#elif defined(DEATH_TARGET_VITA)
		// PS Vita renders through vitaGL, a static OpenGL|ES 2.0 implementation layered over SceGxm. This is not
		// an SDL/EGL-managed context - SDL_GL_CreateContext() does not initialize vitaGL's internal GXM state - so
		// it must be brought up explicitly here. Otherwise the very first gl* call (GLDevice::SetupInitialState's
		// glEnable(GL_DEPTH_TEST)) dereferences vitaGL's still-null internal state and faults with a read
		// violation at a tiny address. vglInit() sets up GXM, the GPU memory pools and the 960x544 display
		// framebuffers, frames are presented with vglSwapBuffers() in update(). The argument is the size of the
		// legacy immediate-mode vertex pool - this engine is fully shader/VBO based, so 1 MB is ample (the main
		// GPU pools are reserved internally from free RAM).
		vglInit(0x100000);
#else
	Retry:
		_glContextHandle = SDL_GL_CreateContext(_windowHandle);

		if (!_glContextHandle && _contextInfo.MinorVersion > 0) {
			// Retry with lower minor version
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
			LOGW("SDL_GL_CreateContext() with OpenGL|ES {}.{} failed, retrying with lower version: {}",
				_contextInfo.MajorVersion, _contextInfo.MinorVersion, SDL_GetError());
#else
			LOGW(_contextInfo.CoreProfile ? "SDL_GL_CreateContext() with OpenGL Core {}.{} failed, retrying with lower version: {}" : "SDL_GL_CreateContext() with OpenGL {}.{} failed, retrying with lower version: {}",
				_contextInfo.MajorVersion, _contextInfo.MinorVersion, SDL_GetError());
#endif
			_contextInfo.MinorVersion--;
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, _contextInfo.MinorVersion);
			goto Retry;
		}

#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		FATAL_ASSERT_MSG(_glContextHandle, "SDL_GL_CreateContext() with OpenGL|ES {}.{} failed: {}",
			_contextInfo.MajorVersion, _contextInfo.MinorVersion, SDL_GetError());
#	if defined(DEATH_TARGET_IOS)
		// iOS has no system framebuffer: framebuffer object 0 is nothing, and the screen is a framebuffer SDL's GL view
		// owns (a color renderbuffer backed by the CAEAGLLayer). Every "unbind" of the engine, which is how it returns
		// from a render target to the screen, has to bind that object instead, and its renderbuffer has to be the
		// bound one when the frame is presented (see update()). Both handles come from SDL's window-manager info.
		{
			SDL_SysWMinfo wmInfo;
			SDL_VERSION(&wmInfo.version);
			if (SDL_GetWindowWMInfo(_windowHandle, &wmInfo) == SDL_TRUE && wmInfo.subsystem == SDL_SYSWM_UIKIT) {
				RHI::GL::GLFramebuffer::SetDefaultHandle(wmInfo.info.uikit.framebuffer);
				_iosColorRenderbuffer = wmInfo.info.uikit.colorbuffer;
				LOGI("Using the UIKit view's framebuffer {} (renderbuffer {}) as the screen", wmInfo.info.uikit.framebuffer, wmInfo.info.uikit.colorbuffer);
			} else {
				LOGE("SDL_GetWindowWMInfo() failed, nothing can be drawn to the screen: {}", SDL_GetError());
			}
		}
#	endif
#else
		FATAL_ASSERT_MSG(_glContextHandle, _contextInfo.CoreProfile ? "SDL_GL_CreateContext() with OpenGL Core {}.{} failed: {}" : "SDL_GL_CreateContext() with OpenGL {}.{} failed: {}",
			_contextInfo.MajorVersion, _contextInfo.MinorVersion, SDL_GetError());
#endif

		const int interval = (_displayMode.HasVSync() ? 1 : 0);
		SDL_GL_SetSwapInterval(interval);
#endif

#if defined(WITH_GLEW)
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: {}", (const char*)glewGetErrorString(err));

		_contextInfo.DebugContext = (_contextInfo.DebugContext && glewIsSupported("GL_ARB_debug_output"));
#endif

#if defined(WITH_RHI_LEGACYGL)
#	if defined(DEATH_TARGET_MORPHOS)
		// TinyGL is reached through two globals that live in the program rather than in sdl2.library, so
		// the context SDL just created has to be handed to them before any `gl*` call is made
		FATAL_ASSERT_MSG(Backends::MorphOsAttachTinyGl(_glContextHandle),
			"Cannot open tinygl.library - the legacy OpenGL backend needs a 3D driver TinyGL supports");
#	endif
		// Unlike the shader backend, the fixed-function one keeps state of its own that has to be
		// programmed once per context, and it renders the screen pass in the drawable's pixels - so it is
		// told how large that is here rather than only when the window is resized later
		RHI::Device::ResizeSwapchain(_drawableWidth, _drawableHeight);
		RHI::Device::InitializeGl();
#endif
	}

#if defined(WITH_RHI_SOFTWARE)
	void SdlGfxDevice::InitSoftwarePresent(bool hasVSync)
	{
#	if defined(WITH_SDL3)
		// SDL3: SDL_CreateRenderer takes only (window, driver-name); the accelerated/vsync flags are gone.
		// Passing a null driver name lets SDL pick a suitable (accelerated when available) renderer, and vsync
		// is configured afterwards through SDL_SetRenderVSync.
		_softwareRenderer = SDL_CreateRenderer(_windowHandle, nullptr);
		FATAL_ASSERT_MSG(_softwareRenderer, "SDL_CreateRenderer failed: {}", SDL_GetError());
		SDL_SetRenderVSync(_softwareRenderer, hasVSync ? 1 : 0);

		SDL_GetRenderOutputSize(_softwareRenderer, &_drawableWidth, &_drawableHeight);
		ResizeSoftwareTarget(_drawableWidth, _drawableHeight);
#	else
		Uint32 rendererFlags = SDL_RENDERER_ACCELERATED;
		if (hasVSync) {
			rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
		}
		_softwareRenderer = SDL_CreateRenderer(_windowHandle, -1, rendererFlags);
		if (_softwareRenderer == nullptr) {
			// Fall back to a non-accelerated renderer if no accelerated one is available
			_softwareRenderer = SDL_CreateRenderer(_windowHandle, -1, SDL_RENDERER_SOFTWARE);
		}
		FATAL_ASSERT_MSG(_softwareRenderer, "SDL_CreateRenderer failed: {}", SDL_GetError());

		SDL_GetRendererOutputSize(_softwareRenderer, &_drawableWidth, &_drawableHeight);
		ResizeSoftwareTarget(_drawableWidth, _drawableHeight);
#	endif
	}

	void SdlGfxDevice::ResizeSoftwareTarget(int width, int height)
	{
		if (width <= 0 || height <= 0 || _softwareRenderer == nullptr) {
			return;
		}
		if (_softwareTexture != nullptr && width == _softwareTextureWidth && height == _softwareTextureHeight) {
			return;
		}
		if (_softwareTexture != nullptr) {
			SDL_DestroyTexture(_softwareTexture);
			_softwareTexture = nullptr;
		}
#	if defined(RHI_USE_FB16)
		// 16-bit mode: the backend framebuffer stores native-endian RGB565 texels, SDL's RGB565 packed format
		_softwareTexture = SDL_CreateTexture(_softwareRenderer, SDL_PIXELFORMAT_RGB565,
			SDL_TEXTUREACCESS_STREAMING, width, height);
#	else
		// The backend framebuffer is laid out as R,G,B,A bytes per texel. RGBA32 is SDL's name for exactly
		// that byte order whatever the machine's endianness - the packed names (ABGR8888 here) describe a
		// 32-bit value instead, so on a big-endian machine like the PowerPC Amigas one of them would put
		// the channels in the wrong places
		_softwareTexture = SDL_CreateTexture(_softwareRenderer, SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_STREAMING, width, height);
#	endif
		FATAL_ASSERT_MSG(_softwareTexture, "SDL_CreateTexture failed: {}", SDL_GetError());
		_softwareTextureWidth = width;
		_softwareTextureHeight = height;
		// Give the root screen viewport a CPU framebuffer of the same size to render into
		RHI::Device::ResizeScreenFramebuffer(width, height);
	}

	void SdlGfxDevice::PresentSoftware()
	{
		if (_softwareRenderer == nullptr) {
			return;
		}
		// Render any draws the tile renderer deferred this frame into the screen buffer before we read it
		RHI::Device::FlushSoftwareRenderer();
		// All of this frame's Combine draws have run by now, so any lighting entries still queued are leftovers
		RHI::Device::EndFrame();
		const auto fb = RHI::Device::GetScreenFramebuffer();
		// The render pipeline sizes the screen framebuffer to the internal/logical resolution (see
		// UpscaleRenderPass, which resizes it on the software backend); keep the streaming texture matched to
		// that size so SDL_RenderCopyEx below stretches the low-resolution image up to the window. The window
		// (drawable) size no longer drives the framebuffer size — this is what makes the software renderer draw
		// the scene at the cheap internal resolution instead of the full window resolution.
		if (fb.pixels != nullptr && fb.width > 0 && fb.height > 0 &&
			(fb.width != _softwareTextureWidth || fb.height != _softwareTextureHeight)) {
			ResizeSoftwareTarget(fb.width, fb.height);
		}
		if (_softwareTexture == nullptr) {
			return;
		}
		if (fb.pixels != nullptr && fb.width == _softwareTextureWidth && fb.height == _softwareTextureHeight) {
			SDL_UpdateTexture(_softwareTexture, nullptr, fb.pixels, fb.strideBytes);
		}
		SDL_RenderClear(_softwareRenderer);
		// The screen color buffer is top-down like every store (see RhiFwd.h), exactly as the window scans out,
		// so it is presented as it is
#	if defined(WITH_SDL3)
		// SDL3: SDL_RenderCopy -> SDL_RenderTexture (float rects; null = whole texture / whole target)
		SDL_RenderTexture(_softwareRenderer, _softwareTexture, nullptr, nullptr);
#	else
		SDL_RenderCopy(_softwareRenderer, _softwareTexture, nullptr, nullptr);
#	endif
		SDL_RenderPresent(_softwareRenderer);
	}
#endif

	void SdlGfxDevice::UpdateMonitors()
	{
#if defined(DEATH_TARGET_VITA)
		// One fixed "monitor" (the built-in panel), so windowScalingFactor() and the monitor queries stay valid
		_numMonitors = 1;
		_monitors[0].Name = "LCD";
		_monitors[0].Position = Vector2i(0, 0);
		_monitors[0].Scale = Vector2f(1.0f, 1.0f);
		_monitors[0].VideoModeCount = 1;
#	if defined(WITH_SDL3)
		if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())) {
			ConvertVideoModeInfo(*mode, _monitors[0].VideoModes[0]);
		}
#	else
		SDL_DisplayMode mode;
		SDL_GetCurrentDisplayMode(0, &mode);
		ConvertVideoModeInfo(mode, _monitors[0].VideoModes[0]);
#	endif
#elif defined(WITH_SDL3)
		LOGD("Updating list of monitors...");

		// SDL3 identifies displays by opaque SDL_DisplayID (enumerated via SDL_GetDisplays), not a 0-based index
		int monitorCount = 0;
		SDL_DisplayID* displays = SDL_GetDisplays(&monitorCount);
		if (displays == nullptr || monitorCount < 1) {
			LOGD("No monitors reported, keeping the previous list of {}", _numMonitors);
			SDL_free(displays);
			return;
		}
		_numMonitors = (monitorCount < (int)MaxMonitors) ? monitorCount : MaxMonitors;

		for (unsigned int i = 0; i < _numMonitors; i++) {
			const SDL_DisplayID displayId = displays[i];
			_monitors[i].Name = SDL_GetDisplayName(displayId);
			DEATH_ASSERT(_monitors[i].Name != nullptr);

			SDL_Rect bounds;
			SDL_GetDisplayBounds(displayId, &bounds);
			_monitors[i].Position.X = bounds.x;
			_monitors[i].Position.Y = bounds.y;

			// SDL3 replaced SDL_GetDisplayDPI with a single content-scale factor (1.0 == 100% == 96 DPI)
			const float contentScale = SDL_GetDisplayContentScale(displayId);
			_monitors[i].Scale.X = (contentScale > 0.0f ? contentScale : 1.0f);
			_monitors[i].Scale.Y = _monitors[i].Scale.X;

			int modeCount = 0;
			SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayId, &modeCount);
			_monitors[i].VideoModeCount = (modeCount < (int)MaxVideoModes) ? modeCount : MaxVideoModes;
			for (std::int32_t j = 0; j < _monitors[i].VideoModeCount; j++) {
				ConvertVideoModeInfo(*modes[j], _monitors[i].VideoModes[j]);
			}
			SDL_free(modes);
		}
		SDL_free(displays);
#else
		LOGD("Updating list of monitors...");

		const int monitorCount = SDL_GetNumVideoDisplays();
		if (monitorCount < 1) {
			LOGD("No monitors reported, keeping the previous list of {}", _numMonitors);
			return;
		}
		_numMonitors = (monitorCount < MaxMonitors) ? monitorCount : MaxMonitors;

		for (unsigned int i = 0; i < _numMonitors; i++) {
			_monitors[i].Name = SDL_GetDisplayName(i);
			DEATH_ASSERT(_monitors[i].Name != nullptr);

			SDL_Rect bounds;
			SDL_GetDisplayBounds(i, &bounds);
			_monitors[i].Position.X = bounds.x;
			_monitors[i].Position.Y = bounds.y;

			float hDpi, vDpi;
			SDL_GetDisplayDPI(i, nullptr, &hDpi, &vDpi);
			_monitors[i].Scale.X = hDpi / DefaultDPI;
			_monitors[i].Scale.Y = vDpi / DefaultDPI;

			const int modeCount = SDL_GetNumDisplayModes(i);
			_monitors[i].VideoModeCount = (modeCount < MaxVideoModes) ? modeCount : MaxVideoModes;

			SDL_DisplayMode mode;
			for (std::int32_t j = 0; j < _monitors[i].VideoModeCount; j++) {
				SDL_GetDisplayMode(i, j, &mode);
				ConvertVideoModeInfo(mode, _monitors[i].VideoModes[j]);
			}
		}
#endif
	}

	void SdlGfxDevice::ConvertVideoModeInfo(const SDL_DisplayMode& sdlVideoMode, IGfxDevice::VideoMode& videoMode) const
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		videoMode.Width = static_cast<unsigned int>(sdlVideoMode.w);
		videoMode.Height = static_cast<unsigned int>(sdlVideoMode.h);
#else
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		videoMode.Width = static_cast<unsigned int>(cssWidth);
		videoMode.Height = static_cast<unsigned int>(cssHeight);
#endif
		videoMode.RefreshRate = static_cast<float>(sdlVideoMode.refresh_rate);

		switch (sdlVideoMode.format) {
			case SDL_PIXELFORMAT_RGB332:
				videoMode.RedBits = 3;
				videoMode.GreenBits = 3;
				videoMode.BlueBits = 2;
				break;
			case SDL_PIXELFORMAT_RGB444:
			case SDL_PIXELFORMAT_ARGB4444:
			case SDL_PIXELFORMAT_RGBA4444:
			case SDL_PIXELFORMAT_ABGR4444:
			case SDL_PIXELFORMAT_BGRA4444:
				videoMode.RedBits = 4;
				videoMode.GreenBits = 4;
				videoMode.BlueBits = 4;
				break;
			case SDL_PIXELFORMAT_RGB555:
			case SDL_PIXELFORMAT_BGR555:
			case SDL_PIXELFORMAT_ARGB1555:
			case SDL_PIXELFORMAT_RGBA5551:
			case SDL_PIXELFORMAT_ABGR1555:
			case SDL_PIXELFORMAT_BGRA5551:
				videoMode.RedBits = 5;
				videoMode.GreenBits = 5;
				videoMode.BlueBits = 5;
				break;
			case SDL_PIXELFORMAT_RGB565:
			case SDL_PIXELFORMAT_BGR565:
				videoMode.RedBits = 5;
				videoMode.GreenBits = 6;
				videoMode.BlueBits = 5;
				break;
			case SDL_PIXELFORMAT_RGB24:
			case SDL_PIXELFORMAT_BGR24:
			case SDL_PIXELFORMAT_RGB888:	// remapped to SDL_PIXELFORMAT_XRGB8888 on SDL3 (see the shims above)
			case SDL_PIXELFORMAT_BGR888:	// remapped to SDL_PIXELFORMAT_XBGR8888 on SDL3
			case SDL_PIXELFORMAT_RGBX8888:
			case SDL_PIXELFORMAT_BGRX8888:
			case SDL_PIXELFORMAT_ARGB8888:
			case SDL_PIXELFORMAT_RGBA8888:
			case SDL_PIXELFORMAT_ABGR8888:
			case SDL_PIXELFORMAT_BGRA8888:
			default:
				videoMode.RedBits = 8;
				videoMode.GreenBits = 8;
				videoMode.BlueBits = 8;
				break;
			case SDL_PIXELFORMAT_ARGB2101010:
				videoMode.RedBits = 10;
				videoMode.GreenBits = 10;
				videoMode.BlueBits = 10;
				break;
		}
	}

#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
	bool SdlGfxDevice::SupportsAuxiliaryWindows() const
	{
		return SdlAuxiliaryWindow::IsSupported();
	}

	std::unique_ptr<AuxiliaryWindow> SdlGfxDevice::CreateAuxiliaryWindow(const AuxiliaryWindowDesc& desc)
	{
		return SdlAuxiliaryWindow::Create(desc);
	}

	bool SdlGfxDevice::GetGlobalMousePosition(Vector2i& position) const
	{
		if (!SdlAuxiliaryWindow::IsSupported()) {
			return false;
		}
#if defined(WITH_SDL3)
		float x = 0.0f, y = 0.0f;
		SDL_GetGlobalMouseState(&x, &y);
		position = Vector2i(std::int32_t(x), std::int32_t(y));
#else
		SDL_GetGlobalMouseState(&position.X, &position.Y);
#endif
		return true;
	}

	bool SdlGfxDevice::GetGlobalMouseButtons(std::uint32_t& buttons) const
	{
		if (!SdlAuxiliaryWindow::IsSupported()) {
			return false;
		}
#if defined(WITH_SDL3)
		float x = 0.0f, y = 0.0f;
		std::uint32_t state = SDL_GetGlobalMouseState(&x, &y);
#else
		int x = 0, y = 0;
		std::uint32_t state = SDL_GetGlobalMouseState(&x, &y);
#endif
		buttons = ((state & SDL_BUTTON_LMASK) != 0 ? 0x01 : 0) | ((state & SDL_BUTTON_RMASK) != 0 ? 0x02 : 0) | ((state & SDL_BUTTON_MMASK) != 0 ? 0x04 : 0);
		return true;
	}

	bool SdlGfxDevice::GetDesktopWorkArea(Vector2i point, Recti& area) const
	{
		// The display at the point, or the nearest one by the distance to its edges
		auto distanceTo = [&point](const SDL_Rect& r) {
			std::int64_t dx = std::max<std::int64_t>({ std::int64_t(r.x) - point.X, 0, std::int64_t(point.X) - (std::int64_t(r.x) + r.w) });
			std::int64_t dy = std::max<std::int64_t>({ std::int64_t(r.y) - point.Y, 0, std::int64_t(point.Y) - (std::int64_t(r.y) + r.h) });
			return dx * dx + dy * dy;
		};
		SDL_Rect usable;
		bool found = false;
#if defined(WITH_SDL3)
		int count = 0;
		SDL_DisplayID* displays = SDL_GetDisplays(&count);
		std::int64_t best = INT64_MAX;
		for (int i = 0; i < count; i++) {
			SDL_Rect bounds;
			if (SDL_GetDisplayBounds(displays[i], &bounds)) {
				std::int64_t distance = distanceTo(bounds);
				if (distance < best && SDL_GetDisplayUsableBounds(displays[i], &usable)) {
					best = distance;
					area = Recti(usable.x, usable.y, usable.w, usable.h);
					found = true;
				}
			}
		}
		SDL_free(displays);
#else
		std::int64_t best = INT64_MAX;
		for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
			SDL_Rect bounds;
			if (SDL_GetDisplayBounds(i, &bounds) == 0) {
				std::int64_t distance = distanceTo(bounds);
				if (distance < best && SDL_GetDisplayUsableBounds(i, &usable) == 0) {
					best = distance;
					area = Recti(usable.x, usable.y, usable.w, usable.h);
					found = true;
				}
			}
		}
#endif
		return found;
	}

	float SdlGfxDevice::GetWindowContentScale() const
	{
#if defined(WITH_SDL3)
		float scale = SDL_GetWindowDisplayScale(_windowHandle);
		return (scale > 0.0f ? scale : 1.0f);
#else
		return IGfxDevice::GetWindowContentScale();
#endif
	}

	bool SdlGfxDevice::IsAuxiliaryWindow(std::uint32_t windowId)
	{
		// The identifier of an auxiliary window made with SDL is the one SDL gave it
		return (AuxiliaryWindow::FindById(windowId) != nullptr);
	}

	Vector2i SdlGfxDevice::ToMainWindowPosition(std::uint32_t windowId, Vector2i position)
	{
		if (AuxiliaryWindow* window = AuxiliaryWindow::FindById(windowId)) {
			Recti bounds = window->GetBounds();
			Vector2i mainPosition;
			SDL_GetWindowPosition(_windowHandle, &mainPosition.X, &mainPosition.Y);
			// What it shows may be drawn at another scale, on a display with another scale
			float scale = window->GetInputScale();
			return Vector2i(std::int32_t(std::round(float(position.X) * scale)) + bounds.X - mainPosition.X,
				std::int32_t(std::round(float(position.Y) * scale)) + bounds.Y - mainPosition.Y);
		}
		return position;
	}
#endif
}

#endif