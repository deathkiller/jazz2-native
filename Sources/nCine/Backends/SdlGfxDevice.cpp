#if defined(WITH_SDL2) || defined(WITH_SDL3)

#include "SdlGfxDevice.h"
#include "../Graphics/ITextureLoader.h"

#include "../Graphics/RHI/Rhi.h"

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
		initGraphics(windowMode.hasWindowScaling);
		updateMonitors();
		initDevice(windowMode.windowPositionX, windowMode.windowPositionY, windowMode.isResizable);
	}

	SdlGfxDevice::~SdlGfxDevice()
	{
		LOGD("Disposing graphics device...");

		// Uniform across backends: tears down the D3D11 / Vulkan device + swap chain, no-op on OpenGL / software
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
#elif !defined(WITH_RHI_D3D11) && !defined(WITH_RHI_VULKAN)
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

	void SdlGfxDevice::queryDrawableSize(SDL_Window* windowHandle, int fallbackWidth, int fallbackHeight, int& width, int& height)
	{
		// The one place that knows how the active backend measures a window's pixel size
#if defined(WITH_RHI_SOFTWARE)
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
#else
		SDL_GL_GetDrawableSize(windowHandle, &width, &height);
#endif
		if (width <= 0 || height <= 0) {
			width = fallbackWidth;
			height = fallbackHeight;
		}
	}

	void SdlGfxDevice::setSwapInterval(int interval)
	{
#if defined(WITH_RHI_SOFTWARE) || defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN)
		// No GL context; vsync is fixed at device/swap-chain creation time (see the present path)
		static_cast<void>(interval);
#else
		SDL_GL_SetSwapInterval(interval);
#endif
	}

	void SdlGfxDevice::setResolution(bool fullscreen, int width, int height)
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
		queryDrawableSize(_windowHandle, _width, _height, _drawableWidth, _drawableHeight);
		RHI::Device::ResizeSwapchain(_drawableWidth, _drawableHeight);	// no-op on OpenGL / software
#if defined(WITH_RHI_SOFTWARE)
		resizeSoftwareTarget(_drawableWidth, _drawableHeight);
#endif
	}

	void SdlGfxDevice::update()
	{
#if defined(WITH_RHI_SOFTWARE)
		presentSoftware();
#elif defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN)
		// When the window is minimized there is no visible surface to present to: unlike a visible swap chain
		// (whose present blocks on vsync), the D3D11/Vulkan present becomes a non-blocking no-op, so the main
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
		SDL_GL_SwapWindow(_windowHandle);
#endif
	}

	void SdlGfxDevice::setResolutionInternal(int width, int height)
	{
		_width = width;
		_height = height;
		SDL_SetWindowSize(_windowHandle, width, height);
	}

	void SdlGfxDevice::setWindowTitle(StringView windowTitle)
	{
#if !defined(DEATH_TARGET_VITA)
		SDL_SetWindowTitle(_windowHandle, String::nullTerminatedView(windowTitle).data());
#endif
	}

	void SdlGfxDevice::setWindowIcon(StringView windowIconFilename)
	{
#if !defined(DEATH_TARGET_VITA)
		std::unique_ptr<ITextureLoader> image = ITextureLoader::createFromFile(windowIconFilename);
		const unsigned int bytesPerPixel = image->texFormat().numChannels();
		const int pitch = image->width() * bytesPerPixel;
		void* pixels = reinterpret_cast<void*>(const_cast<std::uint8_t*>(image->pixels()));
#	if defined(WITH_SDL3)
		// SDL3: SDL_CreateRGBSurfaceWithFormatFrom -> SDL_CreateSurfaceFrom (reordered args, no bit-depth
		// parameter), SDL_FreeSurface -> SDL_DestroySurface, and SDL_PIXELFORMAT_BGR888 -> SDL_PIXELFORMAT_XBGR8888
		// RGBA32/RGB24 rather than the packed names, so the channel order holds on a big-endian machine too
		const SDL_PixelFormat pixelFormat = (bytesPerPixel == 4) ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24;
		SDL_Surface* surface = SDL_CreateSurfaceFrom(image->width(), image->height(), pixelFormat, pixels, pitch);
		SDL_SetWindowIcon(_windowHandle, surface);
		SDL_DestroySurface(surface);
#	else
		const Uint32 pixelFormat = (bytesPerPixel == 4) ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24;
		SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, image->width(), image->height(), bytesPerPixel * 8, pitch, pixelFormat);
		SDL_SetWindowIcon(_windowHandle, surface);
		SDL_FreeSurface(surface);
#	endif
#endif
	}

	void SdlGfxDevice::setWindowPosition(int x, int y)
	{
#if !defined(DEATH_TARGET_VITA)
		SDL_SetWindowPosition(_windowHandle, x, y);
#endif
	}

	void SdlGfxDevice::setWindowSize(int width, int height)
	{
#if !defined(DEATH_TARGET_VITA)
		// Change resolution only in case it is valid and it really changes
		if (width == 0 || height == 0 || (width == _width && height == _height)) {
			return;
		}

		if (!_isFullscreen) {
			SDL_SetWindowSize(_windowHandle, width, height);
			SDL_GetWindowSize(_windowHandle, &_width, &_height);
			queryDrawableSize(_windowHandle, _width, _height, _drawableWidth, _drawableHeight);
			RHI::Device::ResizeSwapchain(_drawableWidth, _drawableHeight);	// no-op on OpenGL / software
#	if defined(WITH_RHI_SOFTWARE)
			resizeSoftwareTarget(_drawableWidth, _drawableHeight);
#	endif
		}
#endif
	}

	const Vector2i SdlGfxDevice::windowPosition() const
	{
		Vector2i position(0, 0);
#if !defined(DEATH_TARGET_VITA)
		SDL_GetWindowPosition(_windowHandle, &position.X, &position.Y);
#endif
		return position;
	}

	void SdlGfxDevice::flashWindow() const
	{
#if SDL_VERSION_ATLEAST(2, 0, 16) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_VITA)
		SDL_FlashWindow(_windowHandle, SDL_FLASH_UNTIL_FOCUSED);
#endif
	}

	unsigned int SdlGfxDevice::windowMonitorIndex() const
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

	const IGfxDevice::VideoMode& SdlGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		if (monitorIndex >= _numMonitors)
			monitorIndex = 0;

#if defined(WITH_SDL3)
		const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayIdFromIndex(monitorIndex));
		if (mode != nullptr) {
			convertVideoModeInfo(*mode, _currentVideoMode);
		}
#else
		SDL_DisplayMode mode;
		SDL_GetCurrentDisplayMode(monitorIndex, &mode);
		convertVideoModeInfo(mode, _currentVideoMode);
#endif

		return _currentVideoMode;
	}

	bool SdlGfxDevice::setVideoMode(unsigned int modeIndex)
	{
#if defined(WITH_SDL3)
		SDL_DisplayID displayId = SDL_GetDisplayForWindow(_windowHandle);
		unsigned int displayIndex = displayIndexFromId(displayId);
		if (displayIndex >= _numMonitors) {
			displayIndex = 0;
			displayId = displayIdFromIndex(0);
		}

		if ((std::int32_t)modeIndex < _monitors[displayIndex].numVideoModes) {
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

		if ((std::int32_t)modeIndex < _monitors[displayIndex].numVideoModes) {
			SDL_DisplayMode mode;
			SDL_GetDisplayMode(displayIndex, modeIndex, &mode);
			return SDL_SetWindowDisplayMode(_windowHandle, &mode);
		}
#endif
		return false;
	}

	void SdlGfxDevice::initGraphics(bool enableWindowScaling)
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

	void SdlGfxDevice::initDevice(int windowPosX, int windowPosY, bool isResizable)
	{
#if defined(WITH_RHI_SOFTWARE) || defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN)
		// Non-OpenGL backends share one window-creation path: a plain SDL window (no GL context), from which
		// the backend presenter is then created (SDL renderer blit / DXGI swap chain / VkSwapchainKHR)
#	if defined(WITH_RHI_SOFTWARE)
		LOGD("Initializing window (software renderer)...");
		Uint32 windowFlags = 0;
#	elif defined(WITH_RHI_D3D11)
		LOGD("Initializing window (Direct3D 11 renderer)...");
		Uint32 windowFlags = 0;
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
#	endif
		SDL_SetWindowResizable(_windowHandle, NCINE_SDL_BOOL(isResizable));
		// Resolution should be set to current screen size when it was left unspecified
		if (_width <= 0 || _height <= 0) {
			SDL_GetWindowSize(_windowHandle, &_width, &_height);
		}

#	if defined(WITH_RHI_SOFTWARE)
		initSoftwarePresent(_displayMode.hasVSync());
#	else
		queryDrawableSize(_windowHandle, _width, _height, _drawableWidth, _drawableHeight);
#		if defined(WITH_RHI_D3D11)
		{
			// The DXGI swap chain is created for the window's native HWND
#			if defined(WITH_SDL3)
			// SDL3 removed SDL_syswm.h; the native HWND is read from the window's property store instead
			void* nativeWindow = SDL_GetPointerProperty(SDL_GetWindowProperties(_windowHandle),
				SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
			FATAL_ASSERT_MSG(nativeWindow != nullptr, "SDL_GetWindowProperties(HWND) failed: {}", SDL_GetError());
			const bool created = RHI::Device::CreateSwapchain(nativeWindow,
				_drawableWidth, _drawableHeight, _displayMode.hasVSync());
#			else
			SDL_SysWMinfo wmInfo;
			SDL_VERSION(&wmInfo.version);
			const SDL_bool gotInfo = SDL_GetWindowWMInfo(_windowHandle, &wmInfo);
			FATAL_ASSERT_MSG(gotInfo == SDL_TRUE, "SDL_GetWindowWMInfo failed: {}", SDL_GetError());
			const bool created = RHI::Device::CreateSwapchain(reinterpret_cast<void*>(wmInfo.info.win.window),
				_drawableWidth, _drawableHeight, _displayMode.hasVSync());
#			endif
			FATAL_ASSERT_MSG(created, "Failed to create the Direct3D 11 device and swap chain");
		}
#		else
		{
			// The Vulkan backend takes the SDL_Window* directly (it queries the required instance extensions
			// and creates the presentation surface from it via the SDL Vulkan API)
			const bool created = RHI::Device::CreateSwapchain(reinterpret_cast<void*>(_windowHandle),
				_drawableWidth, _drawableHeight, _displayMode.hasVSync());
			FATAL_ASSERT_MSG(created, "Failed to create the Vulkan device and swap chain");
		}
#		endif
#	endif

		initDeviceViewport();
		return;
#endif
		// Setting OpenGL attributes
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, _displayMode.redBits());
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, _displayMode.greenBits());
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, _displayMode.blueBits());
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, _displayMode.alphaBits());
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, _displayMode.isDoubleBuffered());
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, _displayMode.depthBits());
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, _displayMode.stencilBits());
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, _contextInfo.majorVersion);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, _contextInfo.minorVersion);
#if defined(RHI_GL_PROFILE_ES)
		SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, _contextInfo.coreProfile
															 ? SDL_GL_CONTEXT_PROFILE_CORE
															 : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
#if defined(WITH_RHI_LEGACYGL)
		// The fixed-function backend draws with what a forward-compatible context is defined to have
		// removed (the fixed-function pipeline itself), so that flag is never set for it
#else
		if (!_contextInfo.forwardCompatible) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		}
#endif
		if (_contextInfo.debugContext) {
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
#if defined(DEATH_TARGET_VITA)
		// vitaGL renders to the Vita's fixed 960x544 panel and owns the framebuffer, so SDL's drawable size (which
		// can be skewed by the window size or HighDPI scaling) is not authoritative here - pin it to the panel so
		// the device viewport matches vitaGL's backbuffer exactly. Otherwise the scene renders into a screen corner.
		_drawableWidth = 960;
		_drawableHeight = 544;
#elif defined(WITH_SDL3)
		SDL_GetWindowSizeInPixels(_windowHandle, &_drawableWidth, &_drawableHeight);
#else
		SDL_GL_GetDrawableSize(_windowHandle, &_drawableWidth, &_drawableHeight);
#endif
		initDeviceViewport();

		SDL_SetWindowResizable(_windowHandle, NCINE_SDL_BOOL(isResizable));

		// resolution should be set to current screen size
		if (_width <= 0 || _height <= 0) {
			SDL_GetWindowSize(_windowHandle, &_width, &_height);
		}

#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		LOGD("Initializing OpenGL|ES {}.{} context...", _contextInfo.majorVersion, _contextInfo.minorVersion);
#else
		LOGD("Initializing OpenGL {}.{} {} context...", _contextInfo.majorVersion, _contextInfo.minorVersion,
			_contextInfo.coreProfile ? "Core" : "Compatibility");
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

		if (!_glContextHandle && _contextInfo.minorVersion > 0) {
			// Retry with lower minor version
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
			LOGW("SDL_GL_CreateContext() with OpenGL|ES {}.{} failed, retrying with lower version: {}",
				_contextInfo.majorVersion, _contextInfo.minorVersion, SDL_GetError());
#else
			LOGW(_contextInfo.coreProfile ? "SDL_GL_CreateContext() with OpenGL Core {}.{} failed, retrying with lower version: {}" : "SDL_GL_CreateContext() with OpenGL {}.{} failed, retrying with lower version: {}",
				_contextInfo.majorVersion, _contextInfo.minorVersion, SDL_GetError());
#endif
			_contextInfo.minorVersion--;
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, _contextInfo.minorVersion);
			goto Retry;
		}

#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		FATAL_ASSERT_MSG(_glContextHandle, "SDL_GL_CreateContext() with OpenGL|ES {}.{} failed: {}",
			_contextInfo.majorVersion, _contextInfo.minorVersion, SDL_GetError());
#else
		FATAL_ASSERT_MSG(_glContextHandle, _contextInfo.coreProfile ? "SDL_GL_CreateContext() with OpenGL Core {}.{} failed: {}" : "SDL_GL_CreateContext() with OpenGL {}.{} failed: {}",
			_contextInfo.majorVersion, _contextInfo.minorVersion, SDL_GetError());
#endif

		const int interval = (_displayMode.hasVSync() ? 1 : 0);
		SDL_GL_SetSwapInterval(interval);
#endif

#if defined(WITH_GLEW)
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: {}", (const char*)glewGetErrorString(err));

		_contextInfo.debugContext = (_contextInfo.debugContext && glewIsSupported("GL_ARB_debug_output"));
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
	void SdlGfxDevice::initSoftwarePresent(bool hasVSync)
	{
#	if defined(WITH_SDL3)
		// SDL3: SDL_CreateRenderer takes only (window, driver-name); the accelerated/vsync flags are gone.
		// Passing a null driver name lets SDL pick a suitable (accelerated when available) renderer, and vsync
		// is configured afterwards through SDL_SetRenderVSync.
		_softwareRenderer = SDL_CreateRenderer(_windowHandle, nullptr);
		FATAL_ASSERT_MSG(_softwareRenderer, "SDL_CreateRenderer failed: {}", SDL_GetError());
		SDL_SetRenderVSync(_softwareRenderer, hasVSync ? 1 : 0);

		SDL_GetRenderOutputSize(_softwareRenderer, &_drawableWidth, &_drawableHeight);
		resizeSoftwareTarget(_drawableWidth, _drawableHeight);
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
		resizeSoftwareTarget(_drawableWidth, _drawableHeight);
#	endif
	}

	void SdlGfxDevice::resizeSoftwareTarget(int width, int height)
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

	void SdlGfxDevice::presentSoftware()
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
			resizeSoftwareTarget(fb.width, fb.height);
		}
		if (_softwareTexture == nullptr) {
			return;
		}
		if (fb.pixels != nullptr && fb.width == _softwareTextureWidth && fb.height == _softwareTextureHeight) {
			SDL_UpdateTexture(_softwareTexture, nullptr, fb.pixels, fb.strideBytes);
		}
		SDL_RenderClear(_softwareRenderer);
		// The SwRaster engine renders the screen color buffer bottom-up (OpenGL framebuffer convention), so
		// present it flipped vertically into the top-left-origin window
#	if defined(WITH_SDL3)
		// SDL3: SDL_RenderCopyEx -> SDL_RenderTextureRotated (float rects; null = whole texture / whole target)
		SDL_RenderTextureRotated(_softwareRenderer, _softwareTexture, nullptr, nullptr, 0.0, nullptr, SDL_FLIP_VERTICAL);
#	else
		SDL_RenderCopyEx(_softwareRenderer, _softwareTexture, nullptr, nullptr, 0.0, nullptr, SDL_FLIP_VERTICAL);
#	endif
		SDL_RenderPresent(_softwareRenderer);
	}
#endif

	void SdlGfxDevice::updateMonitors()
	{
#if defined(WITH_SDL3)
		LOGD("Updating list of monitors...");

		// SDL3 identifies displays by opaque SDL_DisplayID (enumerated via SDL_GetDisplays), not a 0-based index
		int monitorCount = 0;
		SDL_DisplayID* displays = SDL_GetDisplays(&monitorCount);
		DEATH_ASSERT(monitorCount >= 1);
		_numMonitors = (monitorCount < (int)MaxMonitors) ? monitorCount : MaxMonitors;

		for (unsigned int i = 0; i < _numMonitors; i++) {
			const SDL_DisplayID displayId = displays[i];
			_monitors[i].name = SDL_GetDisplayName(displayId);
			DEATH_ASSERT(_monitors[i].name != nullptr);

			SDL_Rect bounds;
			SDL_GetDisplayBounds(displayId, &bounds);
			_monitors[i].position.X = bounds.x;
			_monitors[i].position.Y = bounds.y;

			// SDL3 replaced SDL_GetDisplayDPI with a single content-scale factor (1.0 == 100% == 96 DPI)
			const float contentScale = SDL_GetDisplayContentScale(displayId);
			_monitors[i].scale.X = (contentScale > 0.0f ? contentScale : 1.0f);
			_monitors[i].scale.Y = _monitors[i].scale.X;

			int modeCount = 0;
			SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(displayId, &modeCount);
			_monitors[i].numVideoModes = (modeCount < (int)MaxVideoModes) ? modeCount : MaxVideoModes;
			for (std::int32_t j = 0; j < _monitors[i].numVideoModes; j++) {
				convertVideoModeInfo(*modes[j], _monitors[i].videoModes[j]);
			}
			SDL_free(modes);
		}
		SDL_free(displays);
#elif !defined(DEATH_TARGET_VITA)
		LOGD("Updating list of monitors...");

		const int monitorCount = SDL_GetNumVideoDisplays();
		DEATH_ASSERT(monitorCount >= 1);
		_numMonitors = (monitorCount < MaxMonitors) ? monitorCount : MaxMonitors;

		for (unsigned int i = 0; i < _numMonitors; i++) {
			_monitors[i].name = SDL_GetDisplayName(i);
			DEATH_ASSERT(_monitors[i].name != nullptr);

			SDL_Rect bounds;
			SDL_GetDisplayBounds(i, &bounds);
			_monitors[i].position.X = bounds.x;
			_monitors[i].position.Y = bounds.y;

			float hDpi, vDpi;
			SDL_GetDisplayDPI(i, nullptr, &hDpi, &vDpi);
			_monitors[i].scale.X = hDpi / DefaultDPI;
			_monitors[i].scale.Y = vDpi / DefaultDPI;

			const int modeCount = SDL_GetNumDisplayModes(i);
			_monitors[i].numVideoModes = (modeCount < MaxVideoModes) ? modeCount : MaxVideoModes;

			SDL_DisplayMode mode;
			for (std::int32_t j = 0; j < _monitors[i].numVideoModes; j++) {
				SDL_GetDisplayMode(i, j, &mode);
				convertVideoModeInfo(mode, _monitors[i].videoModes[j]);
			}
		}
#endif
	}

	void SdlGfxDevice::convertVideoModeInfo(const SDL_DisplayMode& sdlVideoMode, IGfxDevice::VideoMode& videoMode) const
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		videoMode.width = static_cast<unsigned int>(sdlVideoMode.w);
		videoMode.height = static_cast<unsigned int>(sdlVideoMode.h);
#else
		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);
		videoMode.width = static_cast<unsigned int>(cssWidth);
		videoMode.height = static_cast<unsigned int>(cssHeight);
#endif
		videoMode.refreshRate = static_cast<float>(sdlVideoMode.refresh_rate);

		switch (sdlVideoMode.format) {
			case SDL_PIXELFORMAT_RGB332:
				videoMode.redBits = 3;
				videoMode.greenBits = 3;
				videoMode.blueBits = 2;
				break;
			case SDL_PIXELFORMAT_RGB444:
			case SDL_PIXELFORMAT_ARGB4444:
			case SDL_PIXELFORMAT_RGBA4444:
			case SDL_PIXELFORMAT_ABGR4444:
			case SDL_PIXELFORMAT_BGRA4444:
				videoMode.redBits = 4;
				videoMode.greenBits = 4;
				videoMode.blueBits = 4;
				break;
			case SDL_PIXELFORMAT_RGB555:
			case SDL_PIXELFORMAT_BGR555:
			case SDL_PIXELFORMAT_ARGB1555:
			case SDL_PIXELFORMAT_RGBA5551:
			case SDL_PIXELFORMAT_ABGR1555:
			case SDL_PIXELFORMAT_BGRA5551:
				videoMode.redBits = 5;
				videoMode.greenBits = 5;
				videoMode.blueBits = 5;
				break;
			case SDL_PIXELFORMAT_RGB565:
			case SDL_PIXELFORMAT_BGR565:
				videoMode.redBits = 5;
				videoMode.greenBits = 6;
				videoMode.blueBits = 5;
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
				videoMode.redBits = 8;
				videoMode.greenBits = 8;
				videoMode.blueBits = 8;
				break;
			case SDL_PIXELFORMAT_ARGB2101010:
				videoMode.redBits = 10;
				videoMode.greenBits = 10;
				videoMode.blueBits = 10;
				break;
		}
	}
}

#endif