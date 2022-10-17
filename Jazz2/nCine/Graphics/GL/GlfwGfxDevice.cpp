#if defined(WITH_GLFW)

#include "GlfwGfxDevice.h"
#include "../ITextureLoader.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	GLFWwindow* GlfwGfxDevice::windowHandle_ = nullptr;
	GLFWmonitor* GlfwGfxDevice::monitorPointers_[MaxMonitors];
	int GlfwGfxDevice::fsMonitorIndex_ = -1;
	int GlfwGfxDevice::fsModeIndex_ = -1;

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	GlfwGfxDevice::GlfwGfxDevice(const WindowMode& windowMode, const GLContextInfo& glContextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, glContextInfo, displayMode)
	{
		initGraphics();
		initDevice(windowMode.isFullscreen, windowMode.isResizable);
	}

	GlfwGfxDevice::~GlfwGfxDevice()
	{
		glfwDestroyWindow(windowHandle_);
		windowHandle_ = nullptr;
		glfwTerminate();
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void GlfwGfxDevice::setSwapInterval(int interval)
	{
		glfwSwapInterval(interval);
	}

	void GlfwGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		if (fsMonitorIndex_ < 0 || fsMonitorIndex_ > numMonitors_) {
			fsMonitorIndex_ = windowMonitorIndex();
		}

		// The windows goes in full screen on the monitor it was on on the last call to `setVideoMode()`
		GLFWmonitor* monitor = monitorPointers_[fsMonitorIndex_];
		const GLFWvidmode* currentMode = glfwGetVideoMode(monitorPointers_[fsMonitorIndex_]);

		if (fullscreen) {
			int width = (monitor != nullptr ? currentMode->width : width_);
			int height = (monitor != nullptr ? currentMode->height : height_);
			int refreshRate = (monitor != nullptr ? currentMode->refreshRate : GLFW_DONT_CARE);

			if (fsModeIndex_ >= 0 && fsModeIndex_ < monitors_[fsMonitorIndex_].numVideoModes) {
				const IGfxDevice::VideoMode& mode = monitors_[fsMonitorIndex_].videoModes[fsModeIndex_];
				width = mode.width;
				height = mode.height;
				refreshRate = mode.refreshRate;
			}

			glfwSetWindowMonitor(windowHandle_, monitor, 0, 0, width, height, refreshRate);

#if defined(DEATH_TARGET_WINDOWS)
			// Swap internal must be set again after glfwSetWindowMonitor, otherwise V-Sync is turned off
			const int interval = (displayMode_.hasVSync() ? 1 : 0);
			glfwSwapInterval(interval);
#endif
		} else {
			if (width == 0 || height == 0) {
				width_ = lastWindowWidth_;
				height_ = lastWindowHeight_;
			} else {
				width_ = width;
				height_ = height;
			}
			glfwSetWindowMonitor(windowHandle_, nullptr, 0, 0, width_, height_, GLFW_DONT_CARE);
			glfwSetWindowPos(windowHandle_, (currentMode->width - width_) / 2, (currentMode->height - height_) / 2);
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

	int GlfwGfxDevice::windowPositionX() const
	{
		int posX = 0;
		glfwGetWindowPos(windowHandle_, &posX, nullptr);
		return posX;
	}

	int GlfwGfxDevice::windowPositionY() const
	{
		int posY = 0;
		glfwGetWindowPos(windowHandle_, nullptr, &posY);
		return posY;
	}

	const Vector2i GlfwGfxDevice::windowPosition() const
	{
		Vector2i position(0, 0);
		glfwGetWindowPos(windowHandle_, &position.X, &position.Y);
		return position;
	}

	void GlfwGfxDevice::flashWindow() const
	{
#if (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3) && !defined(DEATH_TARGET_EMSCRIPTEN)
		glfwRequestWindowAttention(windowHandle_);
#endif
	}

	int GlfwGfxDevice::primaryMonitorIndex() const
	{
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();

		const int index = retrieveMonitorIndex(monitor);
		return index;
	}

	int GlfwGfxDevice::windowMonitorIndex() const
	{
		if (numMonitors_ == 1)
			return 0;

		GLFWmonitor* monitor = glfwGetWindowMonitor(windowHandle_);
		if (monitor == nullptr) {
			// Fallback value if a monitor containing the window cannot be found
			monitor = glfwGetPrimaryMonitor();

			Vector2i position(0, 0);
			glfwGetWindowPos(windowHandle_, &position.X, &position.Y);
			Vector2i size(0, 0);
			glfwGetWindowSize(windowHandle_, &size.X, &size.Y);
			const Vector2i center = position + size / 2;

			for (unsigned int i = 0; i < numMonitors_; i++) {
				const VideoMode& videoMode = currentVideoMode(i);
				const Recti surface(monitors_[i].position, Vector2i(videoMode.width, videoMode.height));
				if (surface.Contains(center)) {
					monitor = monitorPointers_[i];
					break;
				}
			}
		}

		const int index = retrieveMonitorIndex(monitor);
		return index;
	}

	const IGfxDevice::VideoMode& GlfwGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		// Fallback if the index is not valid
		GLFWmonitor* monitor = glfwGetWindowMonitor(windowHandle_);
		if (monitor == nullptr)
			monitor = glfwGetPrimaryMonitor();

		if (monitorIndex < numMonitors_)
			monitor = monitorPointers_[monitorIndex];

		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		convertVideoModeInfo(*mode, currentVideoMode_);

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

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void GlfwGfxDevice::initGraphics()
	{
#if GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3
		glfwInitHint(GLFW_JOYSTICK_HAT_BUTTONS, GLFW_FALSE);
#endif
		glfwSetErrorCallback(errorCallback);
		FATAL_ASSERT_MSG(glfwInit() == GL_TRUE, "glfwInit() failed");
	}

	void GlfwGfxDevice::initDevice(bool isFullscreen, bool isResizable)
	{
		updateMonitors();

		GLFWmonitor* monitor = nullptr;
		if (isFullscreen) {
			monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* vidMode = glfwGetVideoMode(monitor);
			glfwWindowHint(GLFW_REFRESH_RATE, vidMode->refreshRate);
			if (width_ == 0 || height_ == 0) {
				width_ = vidMode->width;
				height_ = vidMode->height;
			}
			lastWindowWidth_ = width_ * 3 / 4;
			lastWindowHeight_ = height_ * 3 / 4;
		} else if (width_ == 0 || height_ == 0) {
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

		windowHandle_ = glfwCreateWindow(width_, height_, "", monitor, nullptr);
		FATAL_ASSERT_MSG(windowHandle_, "glfwCreateWindow() failed");
		glfwGetFramebufferSize(windowHandle_, &drawableWidth_, &drawableHeight_);
		initGLViewport();

		glfwSetWindowSizeLimits(windowHandle_, 200, 160, GLFW_DONT_CARE, GLFW_DONT_CARE);

		glfwMakeContextCurrent(windowHandle_);

		const int interval = (displayMode_.hasVSync() ? 1 : 0);
		glfwSwapInterval(interval);

#ifdef WITH_GLEW
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG_X(err == GLEW_OK, "GLEW error: %s", glewGetErrorString(err));

		glContextInfo_.debugContext = glContextInfo_.debugContext && glewIsSupported("GL_ARB_debug_output");
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
#if (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 >= 3300)
			glfwGetMonitorContentScale(monitor, &monitors_[i].scale.X, &monitors_[i].scale.Y);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
			monitors_[i].scale.X = emscripten_get_device_pixel_ratio();
			monitors_[i].scale.Y = monitors_[i].scale.X;
#endif
			monitors_[i].dpi.X = DefaultDPI * monitors_[i].scale.X;
			monitors_[i].dpi.Y = DefaultDPI * monitors_[i].scale.Y;

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
		LOGE_X("GLFW error %d: \"%s\"", error, description);
	}

}

#endif