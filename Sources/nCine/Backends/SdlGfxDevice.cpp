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

namespace nCine
{
	SDL_Window* SdlGfxDevice::windowHandle_ = nullptr;

	SdlGfxDevice::SdlGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, glContextInfo, displayMode)
	{
		initGraphics(windowMode.hasWindowScaling);
		updateMonitors();
		initDevice(windowMode.isResizable);
	}

	SdlGfxDevice::~SdlGfxDevice()
	{
		SDL_GL_DeleteContext(glContextHandle_);
		glContextHandle_ = nullptr;
		SDL_DestroyWindow(windowHandle_);
		windowHandle_ = nullptr;
		SDL_Quit();
	}

	void SdlGfxDevice::setSwapInterval(int interval)
	{
		SDL_GL_SetSwapInterval(interval);
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
		SDL_GL_GetDrawableSize(windowHandle_, &drawableWidth_, &drawableHeight_);
	}

	void SdlGfxDevice::setResolutionInternal(int width, int height)
	{
		width_ = width;
		height_ = height;
		SDL_SetWindowSize(windowHandle_, width, height);
	}

	void SdlGfxDevice::setWindowIcon(const StringView& windowIconFilename)
	{
		std::unique_ptr<ITextureLoader> image = ITextureLoader::createFromFile(windowIconFilename);
		const unsigned int bytesPerPixel = image->texFormat().numChannels();
		const Uint32 pixelFormat = (bytesPerPixel == 4) ? SDL_PIXELFORMAT_ABGR8888 : SDL_PIXELFORMAT_BGR888;

		SDL_Surface* surface = nullptr;
		const int pitch = image->width() * bytesPerPixel;
		void* pixels = reinterpret_cast<void*>(const_cast<GLubyte*>(image->pixels()));
		surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, image->width(), image->height(), bytesPerPixel * 8, pitch, pixelFormat);
		SDL_SetWindowIcon(windowHandle_, surface);
		SDL_FreeSurface(surface);
	}

	void SdlGfxDevice::setWindowSize(int width, int height)
	{
		// Change resolution only in case it is valid and it really changes
		if (width == 0 || height == 0 || (width == width_ && height == height_)) {
			return;
		}

		if (!isFullscreen_) {
			SDL_SetWindowSize(windowHandle_, width, height);
			SDL_GetWindowSize(windowHandle_, &width_, &height_);
			SDL_GL_GetDrawableSize(windowHandle_, &drawableWidth_, &drawableHeight_);
		}
	}

	const Vector2i SdlGfxDevice::windowPosition() const
	{
		Vector2i position(0, 0);
		SDL_GetWindowPosition(windowHandle_, &position.X, &position.Y);
		return position;
	}

	void SdlGfxDevice::flashWindow() const
	{
#if SDL_VERSION_ATLEAST(2, 0, 16) && !defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_FlashWindow(windowHandle_, SDL_FLASH_UNTIL_FOCUSED);
#endif
	}

	unsigned int SdlGfxDevice::windowMonitorIndex() const
	{
		const int retrievedIndex = (windowHandle_ != nullptr ? SDL_GetWindowDisplayIndex(windowHandle_) : 0);
		return (retrievedIndex >= 0 ? static_cast<unsigned int>(retrievedIndex) : 0);
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
		int displayIndex = SDL_GetWindowDisplayIndex(windowHandle_);
		if (displayIndex < 0 || displayIndex >= numMonitors_) {
			displayIndex = 0;
		}

		if (modeIndex < monitors_[displayIndex].numVideoModes) {
			SDL_DisplayMode mode;
			SDL_GetDisplayMode(displayIndex, modeIndex, &mode);
			return SDL_SetWindowDisplayMode(windowHandle_, &mode);
		}
		return false;
	}

	void SdlGfxDevice::initGraphics(bool enableWindowScaling)
	{
#if SDL_VERSION_ATLEAST(2, 24, 0) && defined(SDL_HINT_WINDOWS_DPI_SCALING)
		// Scaling is handled automatically by SDL (since v2.24.0)
		if (enableWindowScaling) {
			SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
		}
#endif
		const int err = SDL_Init(SDL_INIT_VIDEO);
		FATAL_ASSERT_MSG(!err, "SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
	}

	void SdlGfxDevice::initDevice(bool isResizable)
	{
		updateMonitors();

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
#if defined(WITH_OPENGLES)
		SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, glContextInfo_.coreProfile
															 ? SDL_GL_CONTEXT_PROFILE_CORE
															 : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
		if (!glContextInfo_.forwardCompatible) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		}
		if (glContextInfo_.debugContext) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
		}

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

		// Creating a window with SDL2
		windowHandle_ = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width_, height_, flags);
		FATAL_ASSERT_MSG(windowHandle_, "SDL_CreateWindow failed: %s", SDL_GetError());
		SDL_GL_GetDrawableSize(windowHandle_, &drawableWidth_, &drawableHeight_);
		initGLViewport();

		SDL_SetWindowResizable(windowHandle_, isResizable ? SDL_TRUE : SDL_FALSE);

		// resolution should be set to current screen size
		if (width_ <= 0 || height_ <= 0) {
			SDL_GetWindowSize(windowHandle_, &width_, &height_);
		}

		glContextHandle_ = SDL_GL_CreateContext(windowHandle_);
		FATAL_ASSERT_MSG(glContextHandle_, "SDL_GL_CreateContext failed: %s", SDL_GetError());

		const int interval = displayMode_.hasVSync() ? 1 : 0;
		SDL_GL_SetSwapInterval(interval);

#if defined(WITH_GLEW)
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: %s", glewGetErrorString(err));

		glContextInfo_.debugContext = (glContextInfo_.debugContext && glewIsSupported("GL_ARB_debug_output"));
#endif
	}

	void SdlGfxDevice::updateMonitors()
	{
		const int monitorCount = SDL_GetNumVideoDisplays();
		ASSERT(monitorCount >= 1);
		numMonitors_ = (monitorCount < MaxMonitors) ? monitorCount : MaxMonitors;

		for (unsigned int i = 0; i < numMonitors_; i++) {
			monitors_[i].name = SDL_GetDisplayName(i);
			ASSERT(monitors_[i].name != nullptr);

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
			for (unsigned int j = 0; j < monitors_[i].numVideoModes; j++) {
				SDL_GetDisplayMode(i, j, &mode);
				convertVideoModeInfo(mode, monitors_[i].videoModes[j]);
			}
		}
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