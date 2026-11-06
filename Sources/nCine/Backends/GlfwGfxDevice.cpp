#if defined(WITH_GLFW)

#include "GlfwGfxDevice.h"
#include "GlfwInputManager.h"
#include "GlfwAuxiliaryWindow.h"
#include "../Graphics/ITextureLoader.h"

#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
#	include "GLFW/emscripten_glfw3.h"
#endif
#include <algorithm>
#include <cstdint>

#if defined(DEATH_TARGET_WINDOWS) && defined(NCINE_HAS_AUXILIARY_WINDOWS)
#	undef APIENTRY
#	include <CommonWindows.h>
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
		InitGraphics();
		UpdateMonitors();
		InitDevice(windowMode.WindowPositionX, windowMode.WindowPositionY, windowMode.IsResizable, windowMode.HasWindowScaling);
	}

	GlfwGfxDevice::~GlfwGfxDevice()
	{
		LOGD("Disposing OpenGL context...");

		glfwDestroyWindow(_windowHandle);
		_windowHandle = nullptr;
		glfwTerminate();
	}

	void GlfwGfxDevice::SetSwapInterval(int interval)
	{
		glfwSwapInterval(interval);
	}

	void GlfwGfxDevice::SetResolution(bool fullscreen, int width, int height)
	{
		// The windows goes in full screen on the same monitor
		_fsMonitorIndex = GetWindowMonitorIndex();

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

			if (_fsModeIndex >= 0 && _fsModeIndex < _monitors[_fsMonitorIndex].VideoModeCount) {
				const IGfxDevice::VideoMode& mode = _monitors[_fsMonitorIndex].VideoModes[_fsModeIndex];
				width = mode.Width;
				height = mode.Height;
				refreshRate = (int)mode.RefreshRate;
			}

			glfwSetWindowMonitor(_windowHandle, monitor, 0, 0, width, height, refreshRate);

#	if defined(DEATH_TARGET_WINDOWS)
			// Swap internal must be set again after glfwSetWindowMonitor, otherwise V-Sync is turned off
			const int interval = (_displayMode.HasVSync() ? 1 : 0);
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
				glfwSetWindowPos(_windowHandle, _monitors[_fsMonitorIndex].Position.X + (currentMode->width - _width) / 2,
								 _monitors[_fsMonitorIndex].Position.Y + (currentMode->height - _height) / 2);
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

	void GlfwGfxDevice::Update()
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN) // Buffers are swapped implicitly in WebGL
		glfwSwapBuffers(_windowHandle);
#endif
	}

	void GlfwGfxDevice::SetResolutionInternal(int width, int height)
	{
		glfwSetWindowSize(_windowHandle, width, height);
		glfwGetWindowSize(_windowHandle, &_width, &_height);
		glfwGetFramebufferSize(_windowHandle, &_drawableWidth, &_drawableHeight);
	}

	void GlfwGfxDevice::SetWindowIcon(StringView windowIconFilename)
	{
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		std::unique_ptr<ITextureLoader> image = ITextureLoader::CreateFromFile(windowIconFilename);
		GLFWimage glfwImage;
		glfwImage.width = image->GetWidth();
		glfwImage.height = image->GetHeight();
		glfwImage.pixels = const_cast<unsigned char*>(image->GetPixels());

		glfwSetWindowIcon(_windowHandle, 1, &glfwImage);
#endif
	}

	const Vector2i GlfwGfxDevice::GetWindowPosition() const
	{
		Vector2i position(0, 0);
		glfwGetWindowPos(_windowHandle, &position.X, &position.Y);
		return position;
	}

#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
	bool GlfwGfxDevice::SupportsAuxiliaryWindows() const
	{
		return GlfwAuxiliaryWindow::IsSupported();
	}

	std::unique_ptr<AuxiliaryWindow> GlfwGfxDevice::CreateAuxiliaryWindow(const AuxiliaryWindowDesc& desc)
	{
		return GlfwAuxiliaryWindow::Create(desc);
	}

	bool GlfwGfxDevice::GetGlobalMousePosition(Vector2i& position) const
	{
		if (!GlfwAuxiliaryWindow::IsSupported()) {
			return false;
		}
		// GLFW knows the pointer only relative to a window, the main one is as good as any
		double x = 0.0, y = 0.0;
		glfwGetCursorPos(_windowHandle, &x, &y);
		Vector2i windowPos = GetWindowPosition();
		position = Vector2i(windowPos.X + std::int32_t(x), windowPos.Y + std::int32_t(y));
		return true;
	}

	bool GlfwGfxDevice::GetGlobalMouseButtons(std::uint32_t& buttons) const
	{
#if defined(DEATH_TARGET_WINDOWS)
		// GLFW knows only the buttons of its own events, the system is asked, with the buttons swapped if they're set so
		bool swapped = (::GetSystemMetrics(SM_SWAPBUTTON) != 0);
		auto isDown = [](int key) { return (::GetAsyncKeyState(key) & 0x8000) != 0; };
		buttons = (isDown(swapped ? VK_RBUTTON : VK_LBUTTON) ? 0x01 : 0) | (isDown(swapped ? VK_LBUTTON : VK_RBUTTON) ? 0x02 : 0) | (isDown(VK_MBUTTON) ? 0x04 : 0);
		return true;
#else
		static_cast<void>(buttons);
		return false;
#endif
	}

	bool GlfwGfxDevice::GetDesktopWorkArea(Vector2i point, Recti& area) const
	{
		// The monitor at the point, or the nearest one by the distance to its edges
		int count = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&count);
		std::int64_t best = INT64_MAX;
		bool found = false;
		for (int i = 0; i < count; i++) {
			int x, y;
			glfwGetMonitorPos(monitors[i], &x, &y);
			const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
			if (mode == nullptr) {
				continue;
			}
			std::int64_t dx = std::max<std::int64_t>({ std::int64_t(x) - point.X, 0, std::int64_t(point.X) - (std::int64_t(x) + mode->width) });
			std::int64_t dy = std::max<std::int64_t>({ std::int64_t(y) - point.Y, 0, std::int64_t(point.Y) - (std::int64_t(y) + mode->height) });
			std::int64_t distance = dx * dx + dy * dy;
			if (distance < best) {
				best = distance;
				int wx, wy, ww, wh;
				glfwGetMonitorWorkarea(monitors[i], &wx, &wy, &ww, &wh);
				area = Recti(wx, wy, ww, wh);
				found = true;
			}
		}
		return found;
	}

	float GlfwGfxDevice::GetWindowContentScale() const
	{
		float xScale = 1.0f, yScale = 1.0f;
		glfwGetWindowContentScale(_windowHandle, &xScale, &yScale);
		return (xScale > 0.0f ? xScale : 1.0f);
	}
#endif

	void GlfwGfxDevice::SetWindowPosition(int x, int y)
	{
		int width = _width;
		int height = _height;
		glfwGetWindowSize(_windowHandle, &_width, &_height);

		glfwSetWindowSizeCallback(_windowHandle, nullptr);
		glfwSetFramebufferSizeCallback(_windowHandle, nullptr);

		glfwSetWindowPos(_windowHandle, x, y);
		glfwSetWindowSize(_windowHandle, width, height);

		glfwSetWindowSizeCallback(_windowHandle, GlfwInputManager::WindowSizeCallback);
		glfwSetFramebufferSizeCallback(_windowHandle, GlfwInputManager::FramebufferSizeCallback);
	}

	void GlfwGfxDevice::SetWindowSize(int width, int height)
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

	void GlfwGfxDevice::FlashWindow() const
	{
#if GLFW_VERSION_COMBINED >= 3300 && !defined(DEATH_TARGET_EMSCRIPTEN)
		glfwRequestWindowAttention(_windowHandle);
#endif
	}

	unsigned int GlfwGfxDevice::GetPrimaryMonitorIndex() const
	{
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();

		const int retrievedIndex = RetrieveMonitorIndex(monitor);
		return (retrievedIndex >= 0 ? static_cast<unsigned int>(retrievedIndex) : 0);
	}

	unsigned int GlfwGfxDevice::GetWindowMonitorIndex() const
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
				const VideoMode& videoMode = GetCurrentVideoMode(i);
				const Recti surface(_monitors[i].Position, Vector2i(videoMode.Width, videoMode.Height));
				if (surface.Contains(windowCenter)) {
					monitor = _monitorPointers[i];
					break;
				}
			}
		}

		const int index = RetrieveMonitorIndex(monitor);
		return (index < 0 ? 0 : static_cast<unsigned int>(index));
	}

	const IGfxDevice::VideoMode& GlfwGfxDevice::GetCurrentVideoMode(unsigned int monitorIndex) const
	{
		// Fallback if the index is not valid
		GLFWmonitor* monitor = (_windowHandle != nullptr ? glfwGetWindowMonitor(_windowHandle) : nullptr);
		if (monitor == nullptr)
			monitor = glfwGetPrimaryMonitor();

		if (monitorIndex < _numMonitors)
			monitor = _monitorPointers[monitorIndex];

		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		if (mode != nullptr) {
			ConvertVideoModeInfo(*mode, _currentVideoMode);
		}

		return _currentVideoMode;
	}

	bool GlfwGfxDevice::SetVideoMode(unsigned int modeIndex)
	{
		const int monitorIndex = GetWindowMonitorIndex();
		DEATH_ASSERT(monitorIndex >= 0);

		const unsigned int numVideoModes = _monitors[monitorIndex].VideoModeCount;
		DEATH_ASSERT(modeIndex < numVideoModes);

		if (modeIndex < numVideoModes) {
			GLFWmonitor* monitor = _monitorPointers[monitorIndex];
			const IGfxDevice::VideoMode& mode = _monitors[monitorIndex].VideoModes[modeIndex];
			glfwSetWindowMonitor(_windowHandle, monitor, 0, 0, mode.Width, mode.Height, static_cast<int>(mode.RefreshRate));

			_fsMonitorIndex = monitorIndex;
			_fsModeIndex = modeIndex;

			return true;
		}
		return false;
	}

	void GlfwGfxDevice::InitGraphics()
	{
#if GLFW_VERSION_COMBINED >= 3300 && !defined(DEATH_TARGET_EMSCRIPTEN)
		glfwInitHint(GLFW_JOYSTICK_HAT_BUTTONS, GLFW_FALSE);
#endif
		glfwSetErrorCallback(ErrorCallback);
		FATAL_ASSERT_MSG(glfwInit() == GLFW_TRUE, "glfwInit() failed");
	}

	void GlfwGfxDevice::InitDevice(int windowPosX, int windowPosY, bool isResizable, bool enableWindowScaling)
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
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, static_cast<int>(_contextInfo.MajorVersion));
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(_contextInfo.MinorVersion));
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, _contextInfo.DebugContext ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RED_BITS, static_cast<int>(_displayMode.GetRedBits()));
		glfwWindowHint(GLFW_GREEN_BITS, static_cast<int>(_displayMode.GetGreenBits()));
		glfwWindowHint(GLFW_BLUE_BITS, static_cast<int>(_displayMode.GetBlueBits()));
		glfwWindowHint(GLFW_ALPHA_BITS, static_cast<int>(_displayMode.GetAlphaBits()));
		glfwWindowHint(GLFW_DEPTH_BITS, static_cast<int>(_displayMode.GetDepthBits()));
		glfwWindowHint(GLFW_STENCIL_BITS, static_cast<int>(_displayMode.GetStencilBits()));
#if defined(DEATH_TARGET_EMSCRIPTEN)
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
		glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
#elif defined(RHI_GL_PROFILE_ES)
		glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#else
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, _contextInfo.ForwardCompatible ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_OPENGL_PROFILE, _contextInfo.CoreProfile ? GLFW_OPENGL_CORE_PROFILE : GLFW_OPENGL_COMPAT_PROFILE);
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

		if (!_windowHandle && _contextInfo.MinorVersion > 0) {
			// Retry with lower minor version
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
			LOGW("glfwCreateWindow() with OpenGL|ES {}.{} failed, retrying with lower version",
				_contextInfo.MajorVersion, _contextInfo.MinorVersion);
#else
			LOGW(_contextInfo.CoreProfile ? "glfwCreateWindow() with OpenGL Core {}.{} failed, retrying with lower version" : "glfwCreateWindow() with OpenGL {}.{} failed, retrying with lower version",
				_contextInfo.MajorVersion, _contextInfo.MinorVersion);
#endif
			_contextInfo.MinorVersion--;
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, static_cast<int>(_contextInfo.MinorVersion));
			goto Retry;
		}

#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		FATAL_ASSERT_MSG(_windowHandle, "glfwCreateWindow() with OpenGL|ES {}.{} failed",
			_contextInfo.MajorVersion, _contextInfo.MinorVersion);
#else
		FATAL_ASSERT_MSG(_windowHandle, _contextInfo.CoreProfile ? "glfwCreateWindow() with OpenGL Core {}.{} failed" : "glfwCreateWindow() with OpenGL {}.{} failed",
			_contextInfo.MajorVersion, _contextInfo.MinorVersion);
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
		InitDeviceViewport();

		glfwSetWindowSizeLimits(_windowHandle, 200, 160, GLFW_DONT_CARE, GLFW_DONT_CARE);

		LOGD("Initializing OpenGL context...");

		glfwMakeContextCurrent(_windowHandle);

		const int interval = (_displayMode.HasVSync() ? 1 : 0);
		glfwSwapInterval(interval);

#if defined(WITH_GLEW)
		const GLenum err = glewInit();
		FATAL_ASSERT_MSG(err == GLEW_OK, "GLEW error: {}", (const char*)glewGetErrorString(err));

		_contextInfo.DebugContext = (_contextInfo.DebugContext && glewIsSupported("GL_ARB_debug_output"));
#endif
	}

	void GlfwGfxDevice::UpdateMonitors()
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
			_monitors[i].Name = glfwGetMonitorName(monitor);
			DEATH_ASSERT(_monitors[i].Name != nullptr);
			glfwGetMonitorPos(monitor, &_monitors[i].Position.X, &_monitors[i].Position.Y);
#if GLFW_VERSION_COMBINED >= 3300
			glfwGetMonitorContentScale(monitor, &_monitors[i].Scale.X, &_monitors[i].Scale.Y);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
			_monitors[i].Scale.X = emscripten_get_device_pixel_ratio();
			_monitors[i].Scale.Y = _monitors[i].Scale.X;
#endif

			int modeCount = 0;
			const GLFWvidmode* modes = glfwGetVideoModes(monitor, &modeCount);
			_monitors[i].VideoModeCount = (modeCount < MaxVideoModes) ? modeCount : MaxVideoModes;

			for (unsigned int j = 0; j < _monitors[i].VideoModeCount; j++) {
				// Reverse GLFW video mode array to be consistent with SDL
				const int srcIndex = modeCount - 1 - j;
				ConvertVideoModeInfo(modes[srcIndex], _monitors[i].VideoModes[j]);
			}

#if defined(DEATH_TARGET_EMSCRIPTEN)
			if (_monitors[0].VideoModeCount == 0) {
				_monitors[0].VideoModeCount = 1;
				_monitors[0].VideoModes[0] = _currentVideoMode;
			}
#endif
		}

		_fsMonitorIndex = -1;
		_fsModeIndex = -1;
	}

	void GlfwGfxDevice::UpdateMonitorScaling(unsigned int monitorIndex)
	{
		int monitorCount = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);

		if (monitorIndex < monitorCount) {
			IGfxDevice::Monitor& monitor = _monitors[monitorIndex];

#if GLFW_VERSION_COMBINED >= 3300
			glfwGetMonitorContentScale(monitors[monitorIndex], &monitor.Scale.X, &monitor.Scale.Y);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
			monitor.scale.X = emscripten_get_device_pixel_ratio();
			monitor.scale.Y = monitor.scale.X;
#endif
		}
	}

	int GlfwGfxDevice::RetrieveMonitorIndex(GLFWmonitor* monitor) const
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

	void GlfwGfxDevice::ConvertVideoModeInfo(const GLFWvidmode& glfwVideoMode, IGfxDevice::VideoMode& videoMode) const
	{
		videoMode.Width = static_cast<unsigned int>(glfwVideoMode.width);
		videoMode.Height = static_cast<unsigned int>(glfwVideoMode.height);
		videoMode.RefreshRate = static_cast<float>(glfwVideoMode.refreshRate);
		videoMode.RedBits = static_cast<unsigned char>(glfwVideoMode.redBits);
		videoMode.GreenBits = static_cast<unsigned char>(glfwVideoMode.greenBits);
		videoMode.BlueBits = static_cast<unsigned char>(glfwVideoMode.blueBits);
	}

	void GlfwGfxDevice::ErrorCallback(int error, const char* description)
	{
		LOGE("GLFW error {}: \"{}\"", error, description);
	}
}

#endif