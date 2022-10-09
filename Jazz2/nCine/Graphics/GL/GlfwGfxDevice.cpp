#if defined(WITH_GLFW)

#include "GlfwGfxDevice.h"
#include "../ITextureLoader.h"

namespace nCine
{
	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	GLFWwindow* GlfwGfxDevice::windowHandle_ = nullptr;

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
		GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

		if (fullscreen) {
			if (width == 0 || height == 0) {
				width_ = mode->width;
				height_ = mode->height;
			} else {
				width_ = width;
				height_ = height;
			}
			glfwSetWindowMonitor(windowHandle_, primaryMonitor, 0, 0, width_, height_, mode->refreshRate);
		} else {
			if (width == 0 || height == 0) {
				if (width_ > 400 && height_ > 400) {
					width_ = width_ * 3 / 4;
					height_ = height_ * 3 / 4;
				}
			} else {
				width_ = width;
				height_ = height;
			}
			glfwSetWindowMonitor(windowHandle_, nullptr, 0, 0, width_, height_, GLFW_DONT_CARE);
			glfwSetWindowPos(windowHandle_, (mode->width - width_) / 2, (mode->height - height_) / 2);
		}

		// Swap internal must be set again after glfwSetWindowMonitor, otherwise V-Sync is turned off
		const int interval = (displayMode_.hasVSync() ? 1 : 0);
		glfwSwapInterval(interval);
	}

	void GlfwGfxDevice::setResolutionInternal(int width, int height)
	{
		width_ = width;
		height_ = height;
		glfwSetWindowSize(windowHandle_, width, height);
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

	const IGfxDevice::VideoMode& GlfwGfxDevice::currentVideoMode() const
	{
		const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
		convertVideoModeInfo(*mode, currentVideoMode_);

		return currentVideoMode_;
	}

	bool GlfwGfxDevice::setVideoMode(unsigned int index)
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		ASSERT(index < videoModes_.size());

		if (index >= videoModes_.size()) {
			return false;
		}

		glfwSetWindowMonitor(windowHandle_, glfwGetPrimaryMonitor(), 0, 0,
							 videoModes_[index].width, videoModes_[index].height, videoModes_[index].refreshRate);
#endif
		return true;
	}

	void GlfwGfxDevice::updateVideoModes()
	{
		int count;
		const GLFWvidmode* modes = glfwGetVideoModes(glfwGetPrimaryMonitor(), &count);
		videoModes_.resize_for_overwrite(count);

		for (int i = 0; i < count; i++) {
			// Reverse GLFW video mode array to be consistent with SDL
			const int srcIndex = count - 1 - i;
			convertVideoModeInfo(modes[srcIndex], videoModes_[i]);
		}
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
		GLFWmonitor* monitor = nullptr;
		if (isFullscreen) {
			monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* vidMode = glfwGetVideoMode(monitor);
			glfwWindowHint(GLFW_REFRESH_RATE, vidMode->refreshRate);
			if (width_ == 0 || height_ == 0) {
				width_ = vidMode->width;
				height_ = vidMode->height;
			}
		} else if (width_ == 0 || height_ == 0) {
			const GLFWvidmode* vidMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
			width_ = vidMode->width;
			height_ = vidMode->height;
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

		glfwSetWindowSizeLimits(windowHandle_, 200, 160, GLFW_DONT_CARE, GLFW_DONT_CARE);

		glfwMakeContextCurrent(windowHandle_);

		const int interval = (displayMode_.hasVSync() ? 1 : 0);
		glfwSwapInterval(interval);

#ifdef WITH_GLEW
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG_X(err == GLEW_OK, "GLEW error: %s", glewGetErrorString(err));

		glContextInfo_.debugContext = glContextInfo_.debugContext && glewIsSupported("GL_ARB_debug_output");
#endif

		updateVideoModes();
	}

	void GlfwGfxDevice::convertVideoModeInfo(const GLFWvidmode& glfwVideoMode, IGfxDevice::VideoMode& videoMode) const
	{
		videoMode.width = static_cast<unsigned int>(glfwVideoMode.width);
		videoMode.height = static_cast<unsigned int>(glfwVideoMode.height);
		videoMode.redBits = static_cast<unsigned int>(glfwVideoMode.redBits);
		videoMode.greenBits = static_cast<unsigned int>(glfwVideoMode.greenBits);
		videoMode.blueBits = static_cast<unsigned int>(glfwVideoMode.blueBits);
		videoMode.refreshRate = static_cast<unsigned int>(glfwVideoMode.refreshRate);
	}

	void GlfwGfxDevice::errorCallback(int error, const char* description)
	{
		LOGE_X("GLFW error %d: \"%s\"", error, description);
	}

}

#endif