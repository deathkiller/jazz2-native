#if defined(WITH_GLFW)

#include "GlfwGfxDevice.h"
#include "GlfwInputManager.h"
#include "../Graphics/ITextureLoader.h"

#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
#	include "GLFW/emscripten_glfw3.h"
#endif

#define GLFW_VERSION_COMBINED (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)

namespace nCine::Backends
{
	GLFWwindow* GlfwGfxDevice::_windowHandle = nullptr;
	GLFWmonitor* GlfwGfxDevice::_monitorPointers[MaxMonitors];
	int GlfwGfxDevice::_fsMonitorIndex = -1;
	int GlfwGfxDevice::_fsModeIndex = -1;

	GlfwGfxDevice::GlfwGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
			: IGfxDevice(windowMode, contextInfo, displayMode)
	{
		initGraphics();
		updateMonitors();
		initDevice(windowMode.windowPositionX, windowMode.windowPositionY, windowMode.isResizable, windowMode.hasWindowScaling);
	}

	GlfwGfxDevice::~GlfwGfxDevice()
	{
		LOGD("Disposing OpenGL context...");

		glfwDestroyWindow(_windowHandle);
		_windowHandle = nullptr;
		glfwTerminate();
	}

	void GlfwGfxDevice::setSwapInterval(int interval)
	{
		glfwSwapInterval(interval);
	}

	void GlfwGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		// The windows goes in full screen on the same monitor
		_fsMonitorIndex = windowMonitorIndex();

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		GLFWmonitor* monitor = _monitorPointers[_fsMonitorIndex];
		const GLFWvidmode* currentMode = glfwGetVideoMode(_monitorPointers[_fsMonitorIndex]);
#endif

		bool wasFullscreen = _isFullscreen;
		_isFullscreen = fullscreen;

		if (fullscreen) {
#if defined(DEATH_TARGET_EMSCRIPTEN)
#	if defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
			emscripten_glfw_request_fullscreen(nullptr, false, false);
#	else
			// On Emscripten, requesting full screen on GLFW is done by changing the window size to the screen size
			EmscriptenFullscreenChangeEvent fsce;
			emscripten_get_fullscreen_status(&fsce);
			glfwSetWindowSize(_windowHandle, fsce.screenWidth, fsce.screenHeight);
#	endif
#else
			int width = (monitor != nullptr ? currentMode->width : _width);
			int height = (monitor != nullptr ? currentMode->height : _height);
			int refreshRate = (monitor != nullptr ? currentMode->refreshRate : GLFW_DONT_CARE);

			if (_fsModeIndex >= 0 && _fsModeIndex < _monitors[_fsMonitorIndex].numVideoModes) {
				const IGfxDevice::VideoMode& mode = _monitors[_fsMonitorIndex].videoModes[_fsModeIndex];
				width = mode.width;
				height = mode.height;
				refreshRate = (int)mode.refreshRate;
			}

			glfwSetWindowMonitor(_windowHandle, monitor, 0, 0, width, height, refreshRate);

#	if defined(DEATH_TARGET_WINDOWS)
			// Swap internal must be set again after glfwSetWindowMonitor, otherwise V-Sync is turned off
			const int interval = (_displayMode.hasVSync() ? 1 : 0);
			glfwSwapInterval(interval);
#	endif
#endif
		} else {
			if (width == 0 || height == 0) {
				_width = _lastWindowWidth;
				_height = _lastWindowHeight;
			} else {
				_width = width;
				_height = height;
			}

#if defined(DEATH_TARGET_EMSCRIPTEN)
			if (wasFullscreen) {
				emscripten_exit_fullscreen();
			}
#else
			glfwSetWindowMonitor(_windowHandle, nullptr, 0, 0, _width, _height, GLFW_DONT_CARE);
			if (wasFullscreen) {
				glfwSetWindowPos(_windowHandle, _monitors[_fsMonitorIndex].position.X + (currentMode->width - _width) / 2,
								 _monitors[_fsMonitorIndex].position.Y + (currentMode->height - _height) / 2);
			}
#endif
		}

		glfwGetWindowSize(_windowHandle, &_width, &_height);
		glfwGetFramebufferSize(_windowHandle, &_drawableWidth, &_drawableHeight);

		if (!fullscreen) {
			_lastWindowWidth = _width;
			_lastWindowHeight = _height;
		}
	}

	void GlfwGfxDevice::update()
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN) // Buffers are swapped implicitly in WebGL
		glfwSwapBuffers(_windowHandle);
#endif
	}

	void GlfwGfxDevice::setResolutionInternal(int width, int height)
	{
		glfwSetWindowSize(_windowHandle, width, height);
		glfwGetWindowSize(_windowHandle, &_width, &_height);
		glfwGetFramebufferSize(_windowHandle, &_drawableWidth, &_drawableHeight);
	}

	void GlfwGfxDevice::setWindowIcon(StringView windowIconFilename)
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		std::unique_ptr<ITextureLoader> image = ITextureLoader::createFromFile(windowIconFilename);
		GLFWimage glfwImage;
		glfwImage.width = image->width();
		glfwImage.height = image->height();
		glfwImage.pixels = const_cast<unsigned char*>(image->pixels());

		glfwSetWindowIcon(_windowHandle, 1, &glfwImage);
#endif
	}

	const Vector2i GlfwGfxDevice::windowPosition() const
	{
		Vector2i position(0, 0);
		glfwGetWindowPos(_windowHandle, &position.X, &position.Y);
		return position;
	}

	void GlfwGfxDevice::setWindowPosition(int x, int y)
	{
		int width = _width;
		int height = _height;
		glfwGetWindowSize(_windowHandle, &_width, &_height);

		glfwSetWindowSizeCallback(_windowHandle, nullptr);
		glfwSetFramebufferSizeCallback(_windowHandle, nullptr);

		glfwSetWindowPos(_windowHandle, x, y);
		glfwSetWindowSize(_windowHandle, width, height);

		glfwSetWindowSizeCallback(_windowHandle, GlfwInputManager::windowSizeCallback);
		glfwSetFramebufferSizeCallback(_windowHandle, GlfwInputManager::framebufferSizeCallback);
	}

	void GlfwGfxDevice::setWindowSize(int width, int height)
	{
		// change resolution only in case it is valid and it really changes
		if (width == 0 || height == 0 || (width == _width && height == _height)) {
			return;
		}

		if (!_isFullscreen) {
			glfwSetWindowSize(_windowHandle, width, height);
			glfwGetWindowSize(_windowHandle, &_width, &_height);
			glfwGetFramebufferSize(_windowHandle, &_drawableWidth, &_drawableHeight);
		}
	}

	void GlfwGfxDevice::flashWindow() const
	{
#if GLFW_VERSION_COMBINED >= 3300 && !defined(DEATH_TARGET_EMSCRIPTEN)
		glfwRequestWindowAttention(_windowHandle);
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
		if (_numMonitors == 1 || _windowHandle == nullptr) {
			return 0;
		}

		GLFWmonitor* monitor = glfwGetWindowMonitor(_windowHandle);
		if (monitor == nullptr) {
			// Fallback value if a monitor containing the window cannot be found
			monitor = glfwGetPrimaryMonitor();

			Vector2i position(0, 0);
			glfwGetWindowPos(_windowHandle, &position.X, &position.Y);
			Vector2i size(0, 0);
			glfwGetWindowSize(_windowHandle, &size.X, &size.Y);
			const Vector2i windowCenter = position + size / 2;

			for (unsigned int i = 0; i < _numMonitors; i++) {
				const VideoMode& videoMode = currentVideoMode(i);
				const Recti surface(_monitors[i].position, Vector2i(videoMode.width, videoMode.height));
				if (surface.Contains(windowCenter)) {
					monitor = _monitorPointers[i];
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
		GLFWmonitor* monitor = (_windowHandle != nullptr ? glfwGetWindowMonitor(_windowHandle) : nullptr);
		if (monitor == nullptr)
			monitor = glfwGetPrimaryMonitor();

		if (monitorIndex < _numMonitors)
			monitor = _monitorPointers[monitorIndex];

		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		if (mode != nullptr) {
			convertVideoModeInfo(*mode, _currentVideoMode);
		}

		return _currentVideoMode;
	}

	bool GlfwGfxDevice::setVideoMode(unsigned int modeIndex)
	{
		const int monitorIndex = windowMonitorIndex();
		DEATH_ASSERT(monitorIndex >= 0);

		const unsigned int numVideoModes = _monitors[monitorIndex].numVideoModes;
		DEATH_ASSERT(modeIndex < numVideoModes);

		if (modeIndex < numVideoModes) {
			GLFWmonitor* monitor = _monitorPointers[monitorIndex];
			const IGfxDevice::VideoMode& mode = _monitors[monitorIndex].videoModes[modeIndex];
			glfwSetWindowMonitor(_windowHandle, monitor, 0, 0, mode.width, mode.height, static_cast<int>(mode.refreshRate));

			_fsMonitorIndex = monitorIndex;
			_fsModeIndex = modeIndex;

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
		FATAL_ASSERT_MSG(glfwInit() == GLFW_TRUE, "glfwInit() failed");
	}

	void GlfwGfxDevice::initDevice(int windowPosX, int windowPosY, bool isResizable, bool enableWindowScaling)
	{
		GLFWmonitor* monitor = nullptr;
		if (_isFullscreen) {
			monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* vidMode = glfwGetVideoMode(monitor);
			glfwWindowHint(GLFW_REFRESH_RATE, vidMode->refreshRate);
			if (_width == 0 || _height == 0) {
				_width = vidMode->width;
				_height = vidMode->height;
			}
			_lastWindowWidth = _width * 3 / 4;
			_lastWindowHeight = _height * 3 / 4;
		} else if (_width <= 0 || _height <= 0) {
			const GLFWvidmode* vidMode = glfwGetVideoMode(glfwGetPrimaryMonitor());
			_width = vidMode->width;
			_height = vidMode->height;
			_lastWindowWidth = _width * 3 / 4;
			_lastWindowHeight = _height * 3 / 4;
		} else {
			_lastWindowWidth = _width;
			_lastWindowHeight = _height;
		}

		// Setting window hints and creating a window with GLFW
		glfwWindowHint(GLFW_RESIZABLE, isResizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, static_cast<int>(_contextInfo.majorVersion));
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(_contextInfo.minorVersion));
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, _contextInfo.debugContext ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RED_BITS, static_cast<int>(_displayMode.redBits()));
		glfwWindowHint(GLFW_GREEN_BITS, static_cast<int>(_displayMode.greenBits()));
		glfwWindowHint(GLFW_BLUE_BITS, static_cast<int>(_displayMode.blueBits()));
		glfwWindowHint(GLFW_ALPHA_BITS, static_cast<int>(_displayMode.alphaBits()));
		glfwWindowHint(GLFW_DEPTH_BITS, static_cast<int>(_displayMode.depthBits()));
		glfwWindowHint(GLFW_STENCIL_BITS, static_cast<int>(_displayMode.stencilBits()));
#if defined(DEATH_TARGET_EMSCRIPTEN)
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
		glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
#elif defined(RHI_GL_PROFILE_ES)
		glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#else
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, _contextInfo.forwardCompatible ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, _contextInfo.coreProfile ? GLFW_OPENGL_CORE_PROFILE : GLFW_OPENGL_COMPAT_PROFILE);
#endif
#if GLFW_VERSION_COMBINED >= 3400
		if (windowPosX != AppConfiguration::WindowPositionIgnore) {
			glfwWindowHint(GLFW_POSITION_X, windowPosX);
		}
		if (windowPosY != AppConfiguration::WindowPositionIgnore) {
			glfwWindowHint(GLFW_POSITION_Y, windowPosY);
		}
#endif
#if defined(GLFW_SCALE_TO_MONITOR) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Scaling is handled automatically by GLFW
		if (enableWindowScaling) {
			glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
		}
#endif

		LOGD("Initializing window...");

	Retry:
		_windowHandle = glfwCreateWindow(_width, _height, "", monitor, nullptr);

		if (!_windowHandle && _contextInfo.minorVersion > 0) {
			// Retry with lower minor version
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
			LOGW("glfwCreateWindow() with OpenGL|ES {}.{} failed, retrying with lower version",
				_contextInfo.majorVersion, _contextInfo.minorVersion);
#else
			LOGW(_contextInfo.coreProfile ? "glfwCreateWindow() with OpenGL Core {}.{} failed, retrying with lower version" : "glfwCreateWindow() with OpenGL {}.{} failed, retrying with lower version",
				_contextInfo.majorVersion, _contextInfo.minorVersion);
#endif
			_contextInfo.minorVersion--;
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(_contextInfo.minorVersion));
			goto Retry;
		}

#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		FATAL_ASSERT_MSG(_windowHandle, "glfwCreateWindow() with OpenGL|ES {}.{} failed",
			_contextInfo.majorVersion, _contextInfo.minorVersion);
#else
		FATAL_ASSERT_MSG(_windowHandle, _contextInfo.coreProfile ? "glfwCreateWindow() with OpenGL Core {}.{} failed" : "glfwCreateWindow() with OpenGL {}.{} failed",
			_contextInfo.majorVersion, _contextInfo.minorVersion);
#endif

#if GLFW_VERSION_COMBINED < 3400
		const bool ignoreBothWindowPosition = (windowPosX == AppConfiguration::WindowPositionIgnore &&
											   windowPosY == AppConfiguration::WindowPositionIgnore);
		if (!_isFullscreen && !ignoreBothWindowPosition) {
			Vector2i windowPos;
			glfwGetWindowPos(_windowHandle, &windowPos.X, &windowPos.Y);
			if (windowPosX != AppConfiguration::WindowPositionIgnore)
				windowPos.X = windowPosX;
			if (windowPosY != AppConfiguration::WindowPositionIgnore)
				windowPos.Y = windowPosY;
			glfwSetWindowPos(_windowHandle, windowPos.X, windowPos.Y);
		}
#endif

		glfwGetFramebufferSize(_windowHandle, &_drawableWidth, &_drawableHeight);
		initDeviceViewport();

		glfwSetWindowSizeLimits(_windowHandle, 200, 160, GLFW_DONT_CARE, GLFW_DONT_CARE);

		LOGD("Initializing OpenGL context...");

		glfwMakeContextCurrent(_windowHandle);

		const int interval = (_displayMode.hasVSync() ? 1 : 0);
		glfwSwapInterval(interval);

#if defined(WITH_GLEW)
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: {}", (const char*)glewGetErrorString(err));

		_contextInfo.debugContext = (_contextInfo.debugContext && glewIsSupported("GL_ARB_debug_output"));
#endif
	}

	void GlfwGfxDevice::updateMonitors()
	{
		LOGD("Updating list of monitors...");

		int monitorCount = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
		DEATH_ASSERT(monitorCount >= 1);
		_numMonitors = (monitorCount < MaxMonitors ? monitorCount : MaxMonitors);

		for (unsigned int i = 0; i < MaxMonitors; i++) {
			_monitorPointers[i] = (i < _numMonitors ? monitors[i] : nullptr);
		}

		for (unsigned int i = 0; i < _numMonitors; i++) {
			GLFWmonitor* monitor = monitors[i];
			_monitors[i].name = glfwGetMonitorName(monitor);
			DEATH_ASSERT(_monitors[i].name != nullptr);
			glfwGetMonitorPos(monitor, &_monitors[i].position.X, &_monitors[i].position.Y);
#if GLFW_VERSION_COMBINED >= 3300
			glfwGetMonitorContentScale(monitor, &_monitors[i].scale.X, &_monitors[i].scale.Y);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
			_monitors[i].scale.X = emscripten_get_device_pixel_ratio();
			_monitors[i].scale.Y = _monitors[i].scale.X;
#endif

			int modeCount = 0;
			const GLFWvidmode* modes = glfwGetVideoModes(monitor, &modeCount);
			_monitors[i].numVideoModes = (modeCount < MaxVideoModes) ? modeCount : MaxVideoModes;

			for (unsigned int j = 0; j < _monitors[i].numVideoModes; j++) {
				// Reverse GLFW video mode array to be consistent with SDL
				const int srcIndex = modeCount - 1 - j;
				convertVideoModeInfo(modes[srcIndex], _monitors[i].videoModes[j]);
			}

#if defined(DEATH_TARGET_EMSCRIPTEN)
			if (_monitors[0].numVideoModes == 0) {
				_monitors[0].numVideoModes = 1;
				_monitors[0].videoModes[0] = _currentVideoMode;
			}
#endif
		}

		_fsMonitorIndex = -1;
		_fsModeIndex = -1;
	}

	void GlfwGfxDevice::updateMonitorScaling(unsigned int monitorIndex)
	{
		int monitorCount = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

		if (monitorIndex < monitorCount) {
			IGfxDevice::Monitor& monitor = _monitors[monitorIndex];

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
		for (unsigned int i = 0; i < _numMonitors; i++) {
			if (_monitorPointers[i] == monitor) {
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
		LOGE("GLFW error {}: \"{}\"", error, description);
	}
}

#endif