#if defined(WITH_SDL)

#include "SdlGfxDevice.h"
#include "../Graphics/ITextureLoader.h"

#include "../Graphics/RHI/Rhi.h"

#if defined(WITH_RHI_D3D11)
// Needed to obtain the native HWND the DXGI swap chain is created for
#	if defined(__HAS_LOCAL_SDL)
#		include "SDL2/SDL_syswm.h"
#	else
#		include <SDL_syswm.h>
#	endif
#endif

#if defined(WITH_RHI_VULKAN)
// SDL_WINDOW_VULKAN + SDL_Vulkan_GetDrawableSize for the Vulkan swap-chain window (the surface itself is
// created inside VulkanDevice from this window via SDL_Vulkan_CreateSurface)
#	if defined(__HAS_LOCAL_SDL)
#		include "SDL2/SDL_vulkan.h"
#	else
#		include <SDL_vulkan.h>
#	endif
#endif

#if defined(WITH_GLEW)
#	define GLEW_NO_GLU
#	include <GL/glew.h>
#elif defined(WITH_RHI_GL) && defined(WITH_OPENGLES)
// No GLEW on the OpenGL|ES (ANGLE) path; pull in the GL/ES headers (GLubyte, glGetString, ...) directly
#	define NCINE_INCLUDE_OPENGL
#	include "../CommonHeaders.h"
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/html5.h>
#endif

namespace nCine::Backends
{
	SDL_Window* SdlGfxDevice::windowHandle_ = nullptr;
	SDL_GLContext SdlGfxDevice::glContextHandle_;

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
		Rhi::Device::DestroySwapchain();
#if defined(WITH_RHI_SOFTWARE)
		if (softwareTexture_ != nullptr) {
			SDL_DestroyTexture(softwareTexture_);
			softwareTexture_ = nullptr;
		}
		if (softwareRenderer_ != nullptr) {
			SDL_DestroyRenderer(softwareRenderer_);
			softwareRenderer_ = nullptr;
		}
#elif defined(DEATH_TARGET_VITA)
		// Vita renders through vitaGL (brought up with vglInit() in initDevice), not an SDL-managed GL context.
		// No explicit teardown is issued here.
#elif !defined(WITH_RHI_D3D11) && !defined(WITH_RHI_VULKAN)
		SDL_GL_DeleteContext(glContextHandle_);
		glContextHandle_ = nullptr;
#endif
		SDL_DestroyWindow(windowHandle_);
		windowHandle_ = nullptr;

		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_Quit();
	}

	void SdlGfxDevice::queryDrawableSize(SDL_Window* windowHandle, int fallbackWidth, int fallbackHeight, int& width, int& height)
	{
		// The one place that knows how the active backend measures a window's pixel size
#if defined(WITH_RHI_SOFTWARE)
		SDL_Renderer* renderer = SDL_GetRenderer(windowHandle);
		if (renderer != nullptr) {
			SDL_GetRendererOutputSize(renderer, &width, &height);
		} else {
			width = 0;
			height = 0;
		}
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
		isFullscreen_ = fullscreen;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_SetWindowFullscreen(windowHandle_, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		if (width > 0 && height > 0) {
			width_ = width;
			height_ = height;
		}
#else
		if (fullscreen) {
			if (width <= 0 || height <= 0) {
				SDL_SetWindowFullscreen(windowHandle_, SDL_WINDOW_FULLSCREEN_DESKTOP);
			} else {
				width_ = width;
				height_ = height;
				SDL_SetWindowFullscreen(windowHandle_, SDL_WINDOW_FULLSCREEN);
				SDL_SetWindowSize(windowHandle_, width, height);
			}
		} else {
			SDL_SetWindowFullscreen(windowHandle_, 0);
			if (width > 0 && height > 0) {
				width_ = width;
				height_ = height;
				SDL_SetWindowSize(windowHandle_, width, height);
			}
		}
#endif

		SDL_GetWindowSize(windowHandle_, &width_, &height_);
		queryDrawableSize(windowHandle_, width_, height_, drawableWidth_, drawableHeight_);
		Rhi::Device::ResizeSwapchain(drawableWidth_, drawableHeight_);	// no-op on OpenGL / software
#if defined(WITH_RHI_SOFTWARE)
		resizeSoftwareTarget(drawableWidth_, drawableHeight_);
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
		Rhi::Device::PresentFrame();
		if ((SDL_GetWindowFlags(windowHandle_) & SDL_WINDOW_MINIMIZED) != 0) {
			SDL_Delay(12);
		}
#elif defined(DEATH_TARGET_VITA)
		// Vita renders through vitaGL, which owns the GXM display; present its backbuffer directly (SDL neither
		// created nor manages this GL context). GL_FALSE = do not pump the SceCommonDialog overlay here.
		vglSwapBuffers(GL_FALSE);
#else
		SDL_GL_SwapWindow(windowHandle_);
#endif
	}

	void SdlGfxDevice::setResolutionInternal(int width, int height)
	{
		width_ = width;
		height_ = height;
		SDL_SetWindowSize(windowHandle_, width, height);
	}

	void SdlGfxDevice::setWindowTitle(StringView windowTitle)
	{
#if !defined(DEATH_TARGET_VITA)
		SDL_SetWindowTitle(windowHandle_, String::nullTerminatedView(windowTitle).data());
#endif
	}

	void SdlGfxDevice::setWindowIcon(StringView windowIconFilename)
	{
#if !defined(DEATH_TARGET_VITA)
		std::unique_ptr<ITextureLoader> image = ITextureLoader::createFromFile(windowIconFilename);
		const unsigned int bytesPerPixel = image->texFormat().numChannels();
		const Uint32 pixelFormat = (bytesPerPixel == 4) ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_BGR888;

		SDL_Surface* surface = nullptr;
		const int pitch = image->width() * bytesPerPixel;
		void* pixels = reinterpret_cast<void*>(const_cast<std::uint8_t*>(image->pixels()));
		surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, image->width(), image->height(), bytesPerPixel * 8, pitch, pixelFormat);
		SDL_SetWindowIcon(windowHandle_, surface);
		SDL_FreeSurface(surface);
#endif
	}

	void SdlGfxDevice::setWindowPosition(int x, int y)
	{
#if !defined(DEATH_TARGET_VITA)
		SDL_SetWindowPosition(windowHandle_, x, y);
#endif
	}

	void SdlGfxDevice::setWindowSize(int width, int height)
	{
#if !defined(DEATH_TARGET_VITA)
		// Change resolution only in case it is valid and it really changes
		if (width == 0 || height == 0 || (width == width_ && height == height_)) {
			return;
		}

		if (!isFullscreen_) {
			SDL_SetWindowSize(windowHandle_, width, height);
			SDL_GetWindowSize(windowHandle_, &width_, &height_);
			queryDrawableSize(windowHandle_, width_, height_, drawableWidth_, drawableHeight_);
			Rhi::Device::ResizeSwapchain(drawableWidth_, drawableHeight_);	// no-op on OpenGL / software
#	if defined(WITH_RHI_SOFTWARE)
			resizeSoftwareTarget(drawableWidth_, drawableHeight_);
#	endif
		}
#endif
	}

	const Vector2i SdlGfxDevice::windowPosition() const
	{
		Vector2i position(0, 0);
#if !defined(DEATH_TARGET_VITA)
		SDL_GetWindowPosition(windowHandle_, &position.X, &position.Y);
#endif
		return position;
	}

	void SdlGfxDevice::flashWindow() const
	{
#if SDL_VERSION_ATLEAST(2, 0, 16) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_VITA)
		SDL_FlashWindow(windowHandle_, SDL_FLASH_UNTIL_FOCUSED);
#endif
	}

	unsigned int SdlGfxDevice::windowMonitorIndex() const
	{
#if defined(DEATH_TARGET_VITA)
		return 0;
#else
		const int retrievedIndex = (windowHandle_ != nullptr ? SDL_GetWindowDisplayIndex(windowHandle_) : 0);
		return (retrievedIndex >= 0 ? static_cast<unsigned int>(retrievedIndex) : 0);
#endif
	}

	const IGfxDevice::VideoMode& SdlGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		if (monitorIndex >= numMonitors_)
			monitorIndex = 0;

		SDL_DisplayMode mode;
		SDL_GetCurrentDisplayMode(monitorIndex, &mode);
		convertVideoModeInfo(mode, currentVideoMode_);

		return currentVideoMode_;
	}

	bool SdlGfxDevice::setVideoMode(unsigned int modeIndex)
	{
#if !defined(DEATH_TARGET_VITA)
		std::int32_t displayIndex = SDL_GetWindowDisplayIndex(windowHandle_);
		if (displayIndex < 0 || displayIndex >= (std::int32_t)numMonitors_) {
			displayIndex = 0;
		}

		if ((std::int32_t)modeIndex < monitors_[displayIndex].numVideoModes) {
			SDL_DisplayMode mode;
			SDL_GetDisplayMode(displayIndex, modeIndex, &mode);
			return SDL_SetWindowDisplayMode(windowHandle_, &mode);
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
		const int err = SDL_InitSubSystem(SDL_INIT_VIDEO);
		FATAL_ASSERT_MSG(!err, "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: {}", SDL_GetError());
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
		windowFlags |= SDL_WINDOW_ALLOW_HIGHDPI;
#	endif
		if (width_ <= 0 || height_ <= 0) {
			windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
			isFullscreen_ = true;
		} else if (isFullscreen_) {
			windowFlags |= SDL_WINDOW_FULLSCREEN;
		}
		if (windowPosX == AppConfiguration::WindowPositionIgnore) {
			windowPosX = SDL_WINDOWPOS_UNDEFINED;
		}
		if (windowPosY == AppConfiguration::WindowPositionIgnore) {
			windowPosY = SDL_WINDOWPOS_UNDEFINED;
		}

		windowHandle_ = SDL_CreateWindow("", windowPosX, windowPosY, width_, height_, windowFlags);
		FATAL_ASSERT_MSG(windowHandle_, "SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_SetWindowResizable(windowHandle_, isResizable ? SDL_TRUE : SDL_FALSE);
		// Resolution should be set to current screen size when it was left unspecified
		if (width_ <= 0 || height_ <= 0) {
			SDL_GetWindowSize(windowHandle_, &width_, &height_);
		}

#	if defined(WITH_RHI_SOFTWARE)
		initSoftwarePresent(displayMode_.hasVSync());
#	else
		queryDrawableSize(windowHandle_, width_, height_, drawableWidth_, drawableHeight_);
#		if defined(WITH_RHI_D3D11)
		{
			// The DXGI swap chain is created for the window's native HWND
			SDL_SysWMinfo wmInfo;
			SDL_VERSION(&wmInfo.version);
			const SDL_bool gotInfo = SDL_GetWindowWMInfo(windowHandle_, &wmInfo);
			FATAL_ASSERT_MSG(gotInfo == SDL_TRUE, "SDL_GetWindowWMInfo failed: {}", SDL_GetError());
			const bool created = Rhi::Device::CreateSwapchain(reinterpret_cast<void*>(wmInfo.info.win.window),
				drawableWidth_, drawableHeight_, displayMode_.hasVSync());
			FATAL_ASSERT_MSG(created, "Failed to create the Direct3D 11 device and swap chain");
		}
#		else
		{
			// The Vulkan backend takes the SDL_Window* directly (it queries the required instance extensions
			// and creates the presentation surface from it via the SDL Vulkan API)
			const bool created = Rhi::Device::CreateSwapchain(reinterpret_cast<void*>(windowHandle_),
				drawableWidth_, drawableHeight_, displayMode_.hasVSync());
			FATAL_ASSERT_MSG(created, "Failed to create the Vulkan device and swap chain");
		}
#		endif
#	endif

		initDeviceViewport();
		return;
#endif
		// Setting OpenGL attributes
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, displayMode_.redBits());
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, displayMode_.greenBits());
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, displayMode_.blueBits());
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, displayMode_.alphaBits());
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, displayMode_.isDoubleBuffered());
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, displayMode_.depthBits());
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, displayMode_.stencilBits());
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, contextInfo_.majorVersion);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, contextInfo_.minorVersion);
#if defined(WITH_OPENGLES)
		SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, contextInfo_.coreProfile
															 ? SDL_GL_CONTEXT_PROFILE_CORE
															 : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
		if (!contextInfo_.forwardCompatible) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		}
		if (contextInfo_.debugContext) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
		}

		LOGD("Initializing window...");

		Uint32 flags = SDL_WINDOW_OPENGL;
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif
		if (width_ <= 0 || height_ <= 0) {
			flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
			isFullscreen_ = true;
		} else if (isFullscreen_) {
			flags |= SDL_WINDOW_FULLSCREEN;
		}

		if (windowPosX == AppConfiguration::WindowPositionIgnore) {
			windowPosX = SDL_WINDOWPOS_UNDEFINED;
		}
		if (windowPosY == AppConfiguration::WindowPositionIgnore) {
			windowPosY = SDL_WINDOWPOS_UNDEFINED;
		}

		// Creating a window with SDL2
		windowHandle_ = SDL_CreateWindow("", windowPosX, windowPosY, width_, height_, flags);
		FATAL_ASSERT_MSG(windowHandle_, "SDL_CreateWindow failed: {}", SDL_GetError());
#if defined(DEATH_TARGET_VITA)
		// vitaGL renders to the Vita's fixed 960x544 panel and owns the framebuffer, so SDL's drawable size (which
		// can be skewed by the window size or HighDPI scaling) is not authoritative here - pin it to the panel so
		// the device viewport matches vitaGL's backbuffer exactly. Otherwise the scene renders into a screen corner.
		drawableWidth_ = 960;
		drawableHeight_ = 544;
#else
		SDL_GL_GetDrawableSize(windowHandle_, &drawableWidth_, &drawableHeight_);
#endif
		initDeviceViewport();

		SDL_SetWindowResizable(windowHandle_, isResizable ? SDL_TRUE : SDL_FALSE);

		// resolution should be set to current screen size
		if (width_ <= 0 || height_ <= 0) {
			SDL_GetWindowSize(windowHandle_, &width_, &height_);
		}

#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		LOGD("Initializing OpenGL|ES {}.{} context...", contextInfo_.majorVersion, contextInfo_.minorVersion);
#else
		LOGD("Initializing OpenGL {}.{} {} context...", contextInfo_.majorVersion, contextInfo_.minorVersion,
			contextInfo_.coreProfile ? "Core" : "Compatibility");
#endif

#if defined(DEATH_TARGET_VITA)
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
		glContextHandle_ = SDL_GL_CreateContext(windowHandle_);

		if (!glContextHandle_ && contextInfo_.minorVersion > 0) {
			// Retry with lower minor version
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
			LOGW("SDL_GL_CreateContext() with OpenGL|ES {}.{} failed, retrying with lower version: {}",
				contextInfo_.majorVersion, contextInfo_.minorVersion, SDL_GetError());
#else
			LOGW(contextInfo_.coreProfile ? "SDL_GL_CreateContext() with OpenGL Core {}.{} failed, retrying with lower version: {}" : "SDL_GL_CreateContext() with OpenGL {}.{} failed, retrying with lower version: {}",
				contextInfo_.majorVersion, contextInfo_.minorVersion, SDL_GetError());
#endif
			contextInfo_.minorVersion--;
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, contextInfo_.minorVersion);
			goto Retry;
		}

#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		FATAL_ASSERT_MSG(glContextHandle_, "SDL_GL_CreateContext() with OpenGL|ES {}.{} failed: {}",
			contextInfo_.majorVersion, contextInfo_.minorVersion, SDL_GetError());
#else
		FATAL_ASSERT_MSG(glContextHandle_, contextInfo_.coreProfile ? "SDL_GL_CreateContext() with OpenGL Core {}.{} failed: {}" : "SDL_GL_CreateContext() with OpenGL {}.{} failed: {}",
			contextInfo_.majorVersion, contextInfo_.minorVersion, SDL_GetError());
#endif

		const int interval = (displayMode_.hasVSync() ? 1 : 0);
		SDL_GL_SetSwapInterval(interval);
#endif

#if defined(WITH_GLEW)
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: {}", (const char*)glewGetErrorString(err));

		contextInfo_.debugContext = (contextInfo_.debugContext && glewIsSupported("GL_ARB_debug_output"));
#endif
	}

#if defined(WITH_RHI_SOFTWARE)
	void SdlGfxDevice::initSoftwarePresent(bool hasVSync)
	{
		Uint32 rendererFlags = SDL_RENDERER_ACCELERATED;
		if (hasVSync) {
			rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
		}
		softwareRenderer_ = SDL_CreateRenderer(windowHandle_, -1, rendererFlags);
		if (softwareRenderer_ == nullptr) {
			// Fall back to a non-accelerated renderer if no accelerated one is available
			softwareRenderer_ = SDL_CreateRenderer(windowHandle_, -1, SDL_RENDERER_SOFTWARE);
		}
		FATAL_ASSERT_MSG(softwareRenderer_, "SDL_CreateRenderer failed: {}", SDL_GetError());

		SDL_GetRendererOutputSize(softwareRenderer_, &drawableWidth_, &drawableHeight_);
		resizeSoftwareTarget(drawableWidth_, drawableHeight_);
	}

	void SdlGfxDevice::resizeSoftwareTarget(int width, int height)
	{
		if (width <= 0 || height <= 0 || softwareRenderer_ == nullptr) {
			return;
		}
		if (softwareTexture_ != nullptr && width == softwareTextureWidth_ && height == softwareTextureHeight_) {
			return;
		}
		if (softwareTexture_ != nullptr) {
			SDL_DestroyTexture(softwareTexture_);
			softwareTexture_ = nullptr;
		}
		// The backend framebuffer is laid out as R,G,B,A bytes per texel; on a little-endian host that byte
		// order is SDL's ABGR8888 packed format
		softwareTexture_ = SDL_CreateTexture(softwareRenderer_, SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STREAMING, width, height);
		FATAL_ASSERT_MSG(softwareTexture_, "SDL_CreateTexture failed: {}", SDL_GetError());
		softwareTextureWidth_ = width;
		softwareTextureHeight_ = height;
		// Give the root screen viewport a CPU framebuffer of the same size to render into
		Rhi::Device::ResizeScreenFramebuffer(width, height);
	}

	void SdlGfxDevice::presentSoftware()
	{
		if (softwareRenderer_ == nullptr) {
			return;
		}
		// Render any draws the tile renderer deferred this frame into the screen buffer before we read it
		Rhi::Device::FlushSoftwareRenderer();
		// All of this frame's Combine draws have run by now, so any lighting entries still queued are leftovers
		Rhi::Device::EndFrame();
		const auto fb = Rhi::Device::GetScreenFramebuffer();
		// The render pipeline sizes the screen framebuffer to the internal/logical resolution (see
		// UpscaleRenderPass, which resizes it on the software backend); keep the streaming texture matched to
		// that size so SDL_RenderCopyEx below stretches the low-resolution image up to the window. The window
		// (drawable) size no longer drives the framebuffer size — this is what makes the software renderer draw
		// the scene at the cheap internal resolution instead of the full window resolution.
		if (fb.pixels != nullptr && fb.width > 0 && fb.height > 0 &&
			(fb.width != softwareTextureWidth_ || fb.height != softwareTextureHeight_)) {
			resizeSoftwareTarget(fb.width, fb.height);
		}
		if (softwareTexture_ == nullptr) {
			return;
		}
		if (fb.pixels != nullptr && fb.width == softwareTextureWidth_ && fb.height == softwareTextureHeight_) {
			SDL_UpdateTexture(softwareTexture_, nullptr, fb.pixels, fb.strideBytes);
		}
		SDL_RenderClear(softwareRenderer_);
		// The SwRaster engine renders the screen color buffer bottom-up (OpenGL framebuffer convention), so
		// present it flipped vertically into the top-left-origin window
		SDL_RenderCopyEx(softwareRenderer_, softwareTexture_, nullptr, nullptr, 0.0, nullptr, SDL_FLIP_VERTICAL);
		SDL_RenderPresent(softwareRenderer_);
	}
#endif

	void SdlGfxDevice::updateMonitors()
	{
#if !defined(DEATH_TARGET_VITA)
		LOGD("Updating list of monitors...");

		const int monitorCount = SDL_GetNumVideoDisplays();
		DEATH_ASSERT(monitorCount >= 1);
		numMonitors_ = (monitorCount < MaxMonitors) ? monitorCount : MaxMonitors;

		for (unsigned int i = 0; i < numMonitors_; i++) {
			monitors_[i].name = SDL_GetDisplayName(i);
			DEATH_ASSERT(monitors_[i].name != nullptr);

			SDL_Rect bounds;
			SDL_GetDisplayBounds(i, &bounds);
			monitors_[i].position.X = bounds.x;
			monitors_[i].position.Y = bounds.y;

			float hDpi, vDpi;
			SDL_GetDisplayDPI(i, nullptr, &hDpi, &vDpi);
			monitors_[i].scale.X = hDpi / DefaultDPI;
			monitors_[i].scale.Y = vDpi / DefaultDPI;

			const int modeCount = SDL_GetNumDisplayModes(i);
			monitors_[i].numVideoModes = (modeCount < MaxVideoModes) ? modeCount : MaxVideoModes;

			SDL_DisplayMode mode;
			for (std::int32_t j = 0; j < monitors_[i].numVideoModes; j++) {
				SDL_GetDisplayMode(i, j, &mode);
				convertVideoModeInfo(mode, monitors_[i].videoModes[j]);
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
			case SDL_PIXELFORMAT_RGB888:
			case SDL_PIXELFORMAT_RGBX8888:
			case SDL_PIXELFORMAT_BGR888:
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