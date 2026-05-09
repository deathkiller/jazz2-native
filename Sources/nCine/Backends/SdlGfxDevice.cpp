#if defined(WITH_SDL)

#include "SdlGfxDevice.h"
#include "../Graphics/ITextureLoader.h"

#if defined(WITH_GLEW)
#	define GLEW_NO_GLU
#	include <GL/glew.h>
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/html5.h>
#endif

namespace nCine::Backends
{
	SDL_Window* SdlGfxDevice::windowHandle_ = nullptr;
#if !defined(WITH_RHI_SW)
	SDL_GLContext SdlGfxDevice::glContextHandle_;
#endif

	SdlGfxDevice::SdlGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, glContextInfo, displayMode)
	{
		initGraphics(windowMode.hasWindowScaling);
		updateMonitors();
		initDevice(windowMode.windowPositionX, windowMode.windowPositionY, windowMode.isResizable);
	}

	SdlGfxDevice::~SdlGfxDevice()
	{
		LOGD("Disposing graphics context...");

#if defined(WITH_RHI_SW)
		if (swTexture_ != nullptr) {
			SDL_DestroyTexture(swTexture_);
			swTexture_ = nullptr;
		}
		if (swRenderer_ != nullptr) {
			SDL_DestroyRenderer(swRenderer_);
			swRenderer_ = nullptr;
		}
#else
		SDL_GL_DeleteContext(glContextHandle_);
		glContextHandle_ = nullptr;
#endif

		SDL_DestroyWindow(windowHandle_);
		windowHandle_ = nullptr;

#if !defined(DEATH_TARGET_VITA)
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_Quit();
#endif
	}

	void SdlGfxDevice::setSwapInterval(int interval)
	{
#if !defined(WITH_RHI_SW)
		SDL_GL_SetSwapInterval(interval);
#else
		(void)interval;
#endif
	}

	void SdlGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
#if !defined(DEATH_TARGET_VITA)
		isFullscreen_ = fullscreen;

#	if defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_SetWindowFullscreen(windowHandle_, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		if (width > 0 && height > 0) {
			width_ = width;
			height_ = height;
		}
#	else
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
#	endif

		SDL_GetWindowSize(windowHandle_, &width_, &height_);
		SDL_GL_GetDrawableSize(windowHandle_, &drawableWidth_, &drawableHeight_);
#endif
	}

	void SdlGfxDevice::update()
	{
#if defined(WITH_RHI_SW)
		if (swRenderer_ != nullptr && swTexture_ != nullptr) {
			const std::int32_t w   = RHI::GetColorBufferWidth();
			const std::int32_t h   = RHI::GetColorBufferHeight();
			const std::uint8_t* buf = RHI::GetColorBuffer();
			if (buf != nullptr && w > 0 && h > 0) {
				void* texPixels = nullptr;
				int texPitch = 0;
				if (SDL_LockTexture(swTexture_, nullptr, &texPixels, &texPitch) == 0) {
					const std::int32_t srcPitch = w * 4;
					if (texPitch == srcPitch) {
						std::memcpy(texPixels, buf, static_cast<std::size_t>(srcPitch) * h);
					} else {
						const std::uint8_t* srcRow = buf;
						std::uint8_t* dstRow = static_cast<std::uint8_t*>(texPixels);
						const std::int32_t copyBytes = std::min(srcPitch, texPitch);
						for (std::int32_t y = 0; y < h; ++y, srcRow += srcPitch, dstRow += texPitch) {
							std::memcpy(dstRow, srcRow, copyBytes);
						}
					}
					SDL_UnlockTexture(swTexture_);
				}
			}
			SDL_RenderCopyEx(swRenderer_, swTexture_, nullptr, nullptr, 0.0, nullptr, SDL_FLIP_VERTICAL);
			SDL_RenderPresent(swRenderer_);
		}
#elif defined(DEATH_TARGET_VITA)
		vglSwapBuffers(GL_FALSE);
#else
		SDL_GL_SwapWindow(windowHandle_);
#endif
	}

	void SdlGfxDevice::setResolutionInternal(int width, int height)
	{
#if !defined(DEATH_TARGET_VITA)
		width_ = width;
		height_ = height;
		SDL_SetWindowSize(windowHandle_, width, height);
#	if defined(WITH_RHI_SW)
		resizeSwBuffer(width, height);
#	endif
#endif
	}

#if defined(WITH_RHI_SW)
	void SdlGfxDevice::resizeSwBuffer(int width, int height)
	{
		if (swRenderer_ == nullptr) return;
		if (swTexture_ != nullptr) {
			SDL_DestroyTexture(swTexture_);
		}
		swTexture_ = SDL_CreateTexture(swRenderer_, SDL_PIXELFORMAT_RGBA32,
		                               SDL_TEXTUREACCESS_STREAMING, width, height);
		RHI::ResizeColorBuffer(width, height);
	}

	void SdlGfxDevice::resizeSwBufferToLogical(int logicalWidth, int logicalHeight)
	{
		if (swRenderer_ == nullptr) return;
		if (logicalWidth <= 0 || logicalHeight <= 0) return;

		// Only resize if different from current buffer
		if (RHI::GetColorBufferWidth() == logicalWidth && RHI::GetColorBufferHeight() == logicalHeight) {
			return;
		}

		if (swTexture_ != nullptr) {
			SDL_DestroyTexture(swTexture_);
		}
		// Create SDL texture at logical resolution - SDL_RenderCopy stretches to window
		swTexture_ = SDL_CreateTexture(swRenderer_, SDL_PIXELFORMAT_RGBA32,
		                               SDL_TEXTUREACCESS_STREAMING, logicalWidth, logicalHeight);
		RHI::ResizeColorBuffer(logicalWidth, logicalHeight);
	}
#endif

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
		const unsigned int bytesPerPixel = image->numChannels();
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
			SDL_GL_GetDrawableSize(windowHandle_, &drawableWidth_, &drawableHeight_);
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
		const int err = SDL_InitSubSystem(SDL_INIT_VIDEO);
		FATAL_ASSERT_MSG(!err, "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: {}", SDL_GetError());
#endif
	}

	void SdlGfxDevice::initDevice(int windowPosX, int windowPosY, bool isResizable)
	{
#if defined(DEATH_TARGET_VITA)
		/*LOGD("Initializing OpenGL context...");

		// Vita OpenGL context is initialized in vglInitExtended() and doesn't use windowing system
		std::int32_t result = vglInitExtended(0, 960, 544, 0x1800000, SCE_GXM_MULTISAMPLE_NONE);
		DEATH_ASSERT(result >= 0, ("vglInitExtended() failed with error 0x{:.8x}", -result));

		std::uint8_t interval = (displayMode_.hasVSync() ? 1 : 0);
		vglWaitVblankStart(interval);*/

		// Force Vita rendering resolution to 480x272 (50% scale) for performance.
		// The SW color buffer is later resized to the logical resolution via resizeSwBufferToLogical().
		drawableWidth_ = 480;
		drawableHeight_ = 272;
		width_ = 960;   // Physical window width (Vita native)
		height_ = 544;  // Physical window height (Vita native)

		LOGD("Initializing SDL2 software renderer...");

		SDL_Window* windowHandle_ = SDL_CreateWindow(NCINE_APP, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width_, height_, 0);

		swRenderer_ = SDL_CreateRenderer(windowHandle_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (swRenderer_ == nullptr) {
			swRenderer_ = SDL_CreateRenderer(windowHandle_, -1, SDL_RENDERER_SOFTWARE);
		}
		FATAL_ASSERT_MSG(swRenderer_, "SDL_CreateRenderer failed: {}", SDL_GetError());

		// Scale internal 480x272 buffer to fill the Vita's 960x544 display
		SDL_RenderSetLogicalSize(swRenderer_, drawableWidth_, drawableHeight_);

		swTexture_ = SDL_CreateTexture(swRenderer_, SDL_PIXELFORMAT_RGBA32,
									   SDL_TEXTUREACCESS_STREAMING, drawableWidth_, drawableHeight_);
		FATAL_ASSERT_MSG(swTexture_, "SDL_CreateTexture failed: {}", SDL_GetError());

		RHI::ResizeColorBuffer(drawableWidth_, drawableHeight_);
#else
#	if !defined(WITH_RHI_SW)
		// Setting OpenGL attributes
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, displayMode_.redBits());
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, displayMode_.greenBits());
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, displayMode_.blueBits());
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, displayMode_.alphaBits());
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, displayMode_.isDoubleBuffered());
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, displayMode_.depthBits());
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, displayMode_.stencilBits());
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, glContextInfo_.majorVersion);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, glContextInfo_.minorVersion);
#		if defined(WITH_OPENGLES)
		SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#		elif defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#		else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, glContextInfo_.coreProfile
															 ? SDL_GL_CONTEXT_PROFILE_CORE
															 : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#		endif
		if (!glContextInfo_.forwardCompatible) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		}
		if (glContextInfo_.debugContext) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
		}
#	endif

		LOGD("Initializing window...");

#	if defined(WITH_RHI_SW)
		Uint32 flags = 0;
#	else
		Uint32 flags = SDL_WINDOW_OPENGL;
#	endif
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#	endif
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

#	if defined(WITH_RHI_SW)
		SDL_GetWindowSize(windowHandle_, &drawableWidth_, &drawableHeight_);
#	else
		SDL_GL_GetDrawableSize(windowHandle_, &drawableWidth_, &drawableHeight_);
		initGLViewport();
#	endif

		SDL_SetWindowResizable(windowHandle_, isResizable ? SDL_TRUE : SDL_FALSE);

		// resolution should be set to current screen size
		if (width_ <= 0 || height_ <= 0) {
			SDL_GetWindowSize(windowHandle_, &width_, &height_);
		}

#	if defined(WITH_RHI_SW)
		LOGD("Initializing SDL2 software renderer...");

		swRenderer_ = SDL_CreateRenderer(windowHandle_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
		if (swRenderer_ == nullptr) {
			swRenderer_ = SDL_CreateRenderer(windowHandle_, -1, SDL_RENDERER_SOFTWARE);
		}
		FATAL_ASSERT_MSG(swRenderer_, "SDL_CreateRenderer failed: {}", SDL_GetError());

		swTexture_ = SDL_CreateTexture(swRenderer_, SDL_PIXELFORMAT_RGBA32,
		                               SDL_TEXTUREACCESS_STREAMING, drawableWidth_, drawableHeight_);
		FATAL_ASSERT_MSG(swTexture_, "SDL_CreateTexture failed: {}", SDL_GetError());

		RHI::ResizeColorBuffer(drawableWidth_, drawableHeight_);
#	else
		LOGD("Initializing OpenGL context...");

	Retry:
		glContextHandle_ = SDL_GL_CreateContext(windowHandle_);

		if (!glContextHandle_ && glContextInfo_.minorVersion > 0) {
			// Retry with lower minor version
#		if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
			LOGW("SDL_GL_CreateContext() with OpenGL|ES {}.{} failed, retrying with lower version: {}",
				glContextInfo_.majorVersion, glContextInfo_.minorVersion, SDL_GetError());
#		else
			LOGW(glContextInfo_.coreProfile ? "SDL_GL_CreateContext() with OpenGL Core {}.{} failed, retrying with lower version: {}" : "SDL_GL_CreateContext() with OpenGL {}.{} failed, retrying with lower version: {}",
				glContextInfo_.majorVersion, glContextInfo_.minorVersion, SDL_GetError());
#		endif
			glContextInfo_.minorVersion--;
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, glContextInfo_.minorVersion);
			goto Retry;
		}

#		if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		FATAL_ASSERT_MSG(glContextHandle_, "SDL_GL_CreateContext() with OpenGL|ES {}.{} failed: {}",
			glContextInfo_.majorVersion, glContextInfo_.minorVersion, SDL_GetError());
#		else
		FATAL_ASSERT_MSG(glContextHandle_, glContextInfo_.coreProfile ? "SDL_GL_CreateContext() with OpenGL Core {}.{} failed: {}" : "SDL_GL_CreateContext() with OpenGL {}.{} failed: {}",
			glContextInfo_.majorVersion, glContextInfo_.minorVersion, SDL_GetError());
#		endif

		const int interval = (displayMode_.hasVSync() ? 1 : 0);
		SDL_GL_SetSwapInterval(interval);

#		if defined(WITH_GLEW)
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: {}", (const char*)glewGetErrorString(err));

		glContextInfo_.debugContext = (glContextInfo_.debugContext && glewIsSupported("GL_ARB_debug_output"));
#		endif
#	endif
#endif
	}

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