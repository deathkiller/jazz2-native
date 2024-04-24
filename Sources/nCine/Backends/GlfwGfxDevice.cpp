#if defined(WITH_GLFW)

#include "GlfwGfxDevice.h"
#include "GlfwInputManager.h"
#include "../Graphics/ITextureLoader.h"

#define GLFW_VERSION_COMBINED (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)

namespace nCine
{
	GLFWwindow* GlfwGfxDevice::windowHandle_ = nullptr;
	GLFWmonitor* GlfwGfxDevice::monitorPointers_[MaxMonitors];
	int GlfwGfxDevice::fsMonitorIndex_ = -1;
	int GlfwGfxDevice::fsModeIndex_ = -1;

	GlfwGfxDevice::GlfwGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, glContextInfo, displayMode)
	{
		initGraphics();
		updateMonitors();
		initDevice(windowMode.isResizable, windowMode.hasWindowScaling);
	}

	GlfwGfxDevice::~GlfwGfxDevice()
	{
		glfwDestroyWindow(windowHandle_);
		windowHandle_ = nullptr;
		glfwTerminate();
	}

	void GlfwGfxDevice::setSwapInterval(int interval)
	{
		glfwSwapInterval(interval);
	}

	void GlfwGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		// The windows goes in full screen on the same monitor
		fsMonitorIndex_ = windowMonitorIndex();

		GLFWmonitor* monitor = monitorPointers_[fsMonitorIndex_];
		const GLFWvidmode* currentMode = glfwGetVideoMode(monitorPointers_[fsMonitorIndex_]);

		bool wasFullscreen = isFullscreen_;
		isFullscreen_ = fullscreen;

		if (fullscreen) {
			int width = (monitor != nullptr ? currentMode->width : width_);
			int height = (monitor != nullptr ? currentMode->height : height_);
			int refreshRate = (monitor != nullptr ? currentMode->refreshRate : GLFW_DONT_CARE);

			if (fsModeIndex_ >= 0 && fsModeIndex_ < monitors_[fsMonitorIndex_].numVideoModes) {
				const IGfxDevice::VideoMode& mode = monitors_[fsMonitorIndex_].videoModes[fsModeIndex_];
				width = mode.width;
				height = mode.height;
				refreshRate = (int)mode.refreshRate;
			}

#if defined(DEATH_TARGET_EMSCRIPTEN)
			// On Emscripten, requesting full screen on GLFW is done by changing the window size to the screen size
			EmscriptenFullscreenChangeEvent fsce;
			emscripten_get_fullscreen_status(&fsce);
			glfwSetWindowSize(windowHandle_, fsce.screenWidth, fsce.screenHeight);
#else
			glfwSetWindowMonitor(windowHandle_, monitor, 0, 0, width, height, refreshRate);

#	if defined(DEATH_TARGET_WINDOWS)
			// Swap internal must be set again after glfwSetWindowMonitor, otherwise V-Sync is turned off
			const int interval = (displayMode_.hasVSync() ? 1 : 0);
			glfwSwapInterval(interval);
#	endif
#endif
		} else {
			if (width == 0 || height == 0) {
				width_ = lastWindowWidth_;
				height_ = lastWindowHeight_;
			} else {
				width_ = width;
				height_ = height;
			}

#if defined(DEATH_TARGET_EMSCRIPTEN)
			if (wasFullscreen) {
				emscripten_exit_fullscreen();
			}
#else
			glfwSetWindowMonitor(windowHandle_, nullptr, 0, 0, width_, height_, GLFW_DONT_CARE);
			if (wasFullscreen) {
				glfwSetWindowPos(windowHandle_, monitors_[fsMonitorIndex_].position.X + (currentMode->width - width_) / 2,
					monitors_[fsMonitorIndex_].position.Y + (currentMode->height - height_) / 2);
			}
#endif
		}

		glfwGetWindowSize(windowHandle_, &width_, &height_);
		glfwGetFramebufferSize(windowHandle_, &drawableWidth_, &drawableHeight_);

		if (!fullscreen) {
			lastWindowWidth_ = width_;
			lastWindowHeight_ = height_;
		}
	}

	void GlfwGfxDevice::setResolutionInternal(int width, int height)
	{
		glfwSetWindowSize(windowHandle_, width, height);
		glfwGetWindowSize(windowHandle_, &width_, &height_);
		glfwGetFramebufferSize(windowHandle_, &drawableWidth_, &drawableHeight_);
	}

	void GlfwGfxDevice::setWindowIcon(const StringView& windowIconFilename)
	{
		std::unique_ptr<ITextureLoader> image = ITextureLoader::createFromFile(windowIconFilename);
		GLFWimage glfwImage;
		glfwImage.width = image->width();
		glfwImage.height = image->height();
		glfwImage.pixels = const_cast<unsigned char*>(image->pixels());

		glfwSetWindowIcon(windowHandle_, 1, &glfwImage);
	}

	const Vector2i GlfwGfxDevice::windowPosition() const
	{
		Vector2i position(0, 0);
		glfwGetWindowPos(windowHandle_, &position.X, &position.Y);
		return position;
	}

	void GlfwGfxDevice::setWindowPosition(int x, int y)
	{
		int width = width_;
		int height = height_;
		glfwGetWindowSize(windowHandle_, &width_, &height_);

		glfwSetWindowSizeCallback(windowHandle_, nullptr);
		glfwSetFramebufferSizeCallback(windowHandle_, nullptr);

		glfwSetWindowPos(windowHandle_, x, y);
		glfwSetWindowSize(windowHandle_, width, height);

		glfwSetWindowSizeCallback(windowHandle_, GlfwInputManager::windowSizeCallback);
		glfwSetFramebufferSizeCallback(windowHandle_, GlfwInputManager::framebufferSizeCallback);
	}

	void GlfwGfxDevice::setWindowSize(int width, int height)
	{
		// change resolution only in case it is valid and it really changes
		if (width == 0 || height == 0 || (width == width_ && height == height_)) {
			return;
		}

		if (!isFullscreen_) {
			glfwSetWindowSize(windowHandle_, width, height);
			glfwGetWindowSize(windowHandle_, &width_, &height_);
			glfwGetFramebufferSize(windowHandle_, &drawableWidth_, &drawableHeight_);
		}
	}

	void GlfwGfxDevice::flashWindow() const
	{
#if GLFW_VERSION_COMBINED >= 3300
		glfwRequestWindowAttention(windowHandle_);
#endif
	}

	unsigned int GlfwGfxDevice::primaryMonitorIndex() const
	{
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();

		const int retrievedIndex = retrieveMonitorIndex(monitor);
		return (retrievedIndex >= 0 ? static_cast<unsigned int>(retrievedIndex) : 0);
	}

	unsigned int GlfwGfxDevice::windowMonitorIndex() const
	{
		if (numMonitors_ == 1 || windowHandle_ == nullptr) {
			return 0;
		}

		GLFWmonitor* monitor = glfwGetWindowMonitor(windowHandle_);
		if (monitor == nullptr) {
			// Fallback value if a monitor containing the window cannot be found
			monitor = glfwGetPrimaryMonitor();

			Vector2i position(0, 0);
			glfwGetWindowPos(windowHandle_, &position.X, &position.Y);
			Vector2i size(0, 0);
			glfwGetWindowSize(windowHandle_, &size.X, &size.Y);
			const Vector2i windowCenter = position + size / 2;

			for (unsigned int i = 0; i < numMonitors_; i++) {
				const VideoMode& videoMode = currentVideoMode(i);
				const Recti surface(monitors_[i].position, Vector2i(videoMode.width, videoMode.height));
				if (surface.Contains(windowCenter)) {
					monitor = monitorPointers_[i];
					break;
				}
			}
		}

		const int index = retrieveMonitorIndex(monitor);
		return (index < 0 ? 0 : static_cast<unsigned int>(index));
	}

	const IGfxDevice::VideoMode& GlfwGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		// Fallback if the index is not valid
		GLFWmonitor* monitor = (windowHandle_ != nullptr ? glfwGetWindowMonitor(windowHandle_) : nullptr);
		if (monitor == nullptr)
			monitor = glfwGetPrimaryMonitor();

		if (monitorIndex < numMonitors_)
			monitor = monitorPointers_[monitorIndex];

		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		if (mode != nullptr) {
			convertVideoModeInfo(*mode, currentVideoMode_);
		}

		return currentVideoMode_;
	}

	bool GlfwGfxDevice::setVideoMode(unsigned int modeIndex)
	{
		const int monitorIndex = windowMonitorIndex();
		ASSERT(monitorIndex >= 0);

		const unsigned int numVideoModes = monitors_[monitorIndex].numVideoModes;
		ASSERT(modeIndex < numVideoModes);

		if (modeIndex < numVideoModes) {
			GLFWmonitor* monitor = monitorPointers_[monitorIndex];
			const IGfxDevice::VideoMode& mode = monitors_[monitorIndex].videoModes[modeIndex];
			glfwSetWindowMonitor(windowHandle_, monitor, 0, 0, mode.width, mode.height, static_cast<int>(mode.refreshRate));

			fsMonitorIndex_ = monitorIndex;
			fsModeIndex_ = modeIndex;

			return true;
		}
		return false;
	}

	void GlfwGfxDevice::initGraphics()
	{
#if GLFW_VERSION_COMBINED >= 3300 && !defined(DEATH_TARGET_EMSCRIPTEN)
		glfwInitHint(GLFW_JOYSTICK_HAT_BUTTONS, GLFW_FALSE);
#endif
		glfwSetErrorCallback(errorCallback);
		FATAL_ASSERT_MSG(glfwInit() == GL_TRUE, "glfwInit() failed");
	}

	void GlfwGfxDevice::initDevice(bool isResizable, bool enableWindowScaling)
	{
		GLFWmonitor* monitor = nullptr;
		if (isFullscreen_) {
			monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* vidMode = glfwGetVideoMode(monitor);
			glfwWindowHint(GLFW_REFRESH_RATE, vidMode->refreshRate);
			if (width_ == 0 || height_ == 0) {
				width_ = vidMode->width;
				height_ = vidMode->height;
			}
			lastWindowWidth_ = width_ * 3 / 4;
			lastWindowHeight_ = height_ * 3 / 4;
		} else if (width_ <= 0 || height_ <= 0) {
			const GLFWvidmode* vidMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
			width_ = vidMode->width;
			height_ = vidMode->height;
			lastWindowWidth_ = width_ * 3 / 4;
			lastWindowHeight_ = height_ * 3 / 4;
		} else {
			lastWindowWidth_ = width_;
			lastWindowHeight_ = height_;
		}

		// setting window hints and creating a window with GLFW
		glfwWindowHint(GLFW_RESIZABLE, isResizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, static_cast<int>(glContextInfo_.majorVersion));
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(glContextInfo_.minorVersion));
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, glContextInfo_.debugContext ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RED_BITS, static_cast<int>(displayMode_.redBits()));
		glfwWindowHint(GLFW_GREEN_BITS, static_cast<int>(displayMode_.greenBits()));
		glfwWindowHint(GLFW_BLUE_BITS, static_cast<int>(displayMode_.blueBits()));
		glfwWindowHint(GLFW_ALPHA_BITS, static_cast<int>(displayMode_.alphaBits()));
		glfwWindowHint(GLFW_DEPTH_BITS, static_cast<int>(displayMode_.depthBits()));
		glfwWindowHint(GLFW_STENCIL_BITS, static_cast<int>(displayMode_.stencilBits()));
#if defined(WITH_OPENGLES)
		glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
		glfwWindowHint(GLFW_FOCUSED, 1);
#else
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, glContextInfo_.forwardCompatible ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, glContextInfo_.coreProfile ? GLFW_OPENGL_CORE_PROFILE : GLFW_OPENGL_COMPAT_PROFILE);
#endif
#if GLFW_VERSION_COMBINED >= 3400
		//const int windowPosX = (windowMode.windowPositionX != AppConfiguration::WindowPositionIgnore && windowPositionIsValid
		//						? windowMode.windowPositionX : GLFW_ANY_POSITION);
		//const int windowPosY = (windowMode.windowPositionY != AppConfiguration::WindowPositionIgnore && windowPositionIsValid
		//						? windowMode.windowPositionY : GLFW_ANY_POSITION);
		//glfwWindowHint(GLFW_POSITION_X, windowPosX);
		//glfwWindowHint(GLFW_POSITION_Y, windowPosY);
#endif
#if defined(GLFW_SCALE_TO_MONITOR) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Scaling is handled automatically by GLFW
		if (enableWindowScaling) {
			glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
		}
#endif

		windowHandle_ = glfwCreateWindow(width_, height_, "", monitor, nullptr);
		FATAL_ASSERT_MSG(windowHandle_, "glfwCreateWindow() failed");
		
#if GLFW_VERSION_COMBINED < 3400
		//const bool ignoreBothWindowPosition = (windowMode.windowPositionX == AppConfiguration::WindowPositionIgnore &&
		//										windowMode.windowPositionY == AppConfiguration::WindowPositionIgnore);
		//if (!isFullscreen_ && windowPositionIsValid && !ignoreBothWindowPosition) {
		//	Vector2i windowPos;
		//	glfwGetWindowPos(windowHandle_, &windowPos.x, &windowPos.y);
		//	if (windowMode.windowPositionX != AppConfiguration::WindowPositionIgnore)
		//		windowPos.x = windowMode.windowPositionX;
		//	if (windowMode.windowPositionY != AppConfiguration::WindowPositionIgnore)
		//		windowPos.y = windowMode.windowPositionY;
		//	glfwSetWindowPos(windowHandle_, windowPos.x, windowPos.y);
		//}
#endif

		glfwGetFramebufferSize(windowHandle_, &drawableWidth_, &drawableHeight_);
		initGLViewport();

		glfwSetWindowSizeLimits(windowHandle_, 200, 160, GLFW_DONT_CARE, GLFW_DONT_CARE);

		glfwMakeContextCurrent(windowHandle_);

		const int interval = (displayMode_.hasVSync() ? 1 : 0);
		glfwSwapInterval(interval);

#if defined(WITH_GLEW)
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: %s", glewGetErrorString(err));

		glContextInfo_.debugContext = (glContextInfo_.debugContext && glewIsSupported("GL_ARB_debug_output"));
#endif
	}

	void GlfwGfxDevice::updateMonitors()
	{
		int monitorCount = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
		ASSERT(monitorCount >= 1);
		numMonitors_ = (monitorCount < MaxMonitors ? monitorCount : MaxMonitors);

		for (unsigned int i = 0; i < MaxMonitors; i++) {
			monitorPointers_[i] = (i < numMonitors_ ? monitors[i] : nullptr);
		}

		for (unsigned int i = 0; i < numMonitors_; i++) {
			GLFWmonitor* monitor = monitors[i];
			monitors_[i].name = glfwGetMonitorName(monitor);
			ASSERT(monitors_[i].name != nullptr);
			glfwGetMonitorPos(monitor, &monitors_[i].position.X, &monitors_[i].position.Y);
#if GLFW_VERSION_COMBINED >= 3300
			glfwGetMonitorContentScale(monitor, &monitors_[i].scale.X, &monitors_[i].scale.Y);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
			monitors_[i].scale.X = emscripten_get_device_pixel_ratio();
			monitors_[i].scale.Y = monitors_[i].scale.X;
#endif

			int modeCount = 0;
			const GLFWvidmode* modes = glfwGetVideoModes(monitor, &modeCount);
			monitors_[i].numVideoModes = (modeCount < MaxVideoModes) ? modeCount : MaxVideoModes;

			for (unsigned int j = 0; j < monitors_[i].numVideoModes; j++) {
				// Reverse GLFW video mode array to be consistent with SDL
				const int srcIndex = modeCount - 1 - j;
				convertVideoModeInfo(modes[srcIndex], monitors_[i].videoModes[j]);
			}

#if defined(DEATH_TARGET_EMSCRIPTEN)
			if (monitors_[0].numVideoModes == 0) {
				monitors_[0].numVideoModes = 1;
				monitors_[0].videoModes[0] = currentVideoMode_;
			}
#endif
		}

		fsMonitorIndex_ = -1;
		fsModeIndex_ = -1;
	}

	void GlfwGfxDevice::updateMonitorScaling(unsigned int monitorIndex)
	{
		int monitorCount = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

		if (monitorIndex < monitorCount) {
			IGfxDevice::Monitor& monitor = monitors_[monitorIndex];

#if GLFW_VERSION_COMBINED >= 3300
			glfwGetMonitorContentScale(monitors[monitorIndex], &monitor.scale.X, &monitor.scale.Y);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
			monitor.scale.X = emscripten_get_device_pixel_ratio();
			monitor.scale.Y = monitor.scale.X;
#endif
		}
	}

	int GlfwGfxDevice::retrieveMonitorIndex(GLFWmonitor* monitor) const
	{
		int index = -1;
		for (unsigned int i = 0; i < numMonitors_; i++) {
			if (monitorPointers_[i] == monitor) {
				index = i;
				break;
			}
		}
		return index;
	}

	void GlfwGfxDevice::convertVideoModeInfo(const GLFWvidmode& glfwVideoMode, IGfxDevice::VideoMode& videoMode) const
	{
		videoMode.width = static_cast<unsigned int>(glfwVideoMode.width);
		videoMode.height = static_cast<unsigned int>(glfwVideoMode.height);
		videoMode.refreshRate = static_cast<float>(glfwVideoMode.refreshRate);
		videoMode.redBits = static_cast<unsigned char>(glfwVideoMode.redBits);
		videoMode.greenBits = static_cast<unsigned char>(glfwVideoMode.greenBits);
		videoMode.blueBits = static_cast<unsigned char>(glfwVideoMode.blueBits);
	}

	void GlfwGfxDevice::errorCallback(int error, const char* description)
	{
		LOGE("GLFW error %d: \"%s\"", error, description);
	}
}

#endif