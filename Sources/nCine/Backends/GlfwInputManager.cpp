#if defined(WITH_GLFW)

#include "GlfwInputManager.h"
#include "GlfwAuxiliaryWindow.h"
#include "../Input/IInputEventHandler.h"
#include "../Input/JoyMapping.h"
#include "../Application.h"

#include <cstring>	// for memset() and memcpy()
#include <cmath>	// for fabsf()

#include <Utf8.h>

#if defined(WITH_IMGUI)
#	include "ImGuiGlfwInput.h"
#endif

#define GLFW_VERSION_COMBINED (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = GLFW_JOYSTICK_LAST - GLFW_JOYSTICK_1 + 1;
}

namespace nCine::Backends
{
	bool GlfwInputManager::_windowHasFocus = true;
	GlfwMouseState GlfwInputManager::_mouseState;
	MouseEvent GlfwInputManager::_mouseEvent;
	GlfwScrollEvent GlfwInputManager::_scrollEvent;
	GlfwKeyboardState GlfwInputManager::_keyboardState;
	KeyboardEvent GlfwInputManager::_keyboardEvent;
	TextInputEvent GlfwInputManager::_textInputEvent;

	GlfwJoystickState GlfwInputManager::_nullJoystickState;
	SmallVector<GlfwJoystickState, GlfwInputManager::MaxNumJoysticks> GlfwInputManager::_joystickStates(GlfwInputManager::MaxNumJoysticks);
	JoyButtonEvent GlfwInputManager::_joyButtonEvent;
	JoyHatEvent GlfwInputManager::_joyHatEvent;
	JoyAxisEvent GlfwInputManager::_joyAxisEvent;
	JoyConnectionEvent GlfwInputManager::_joyConnectionEvent;
	const float GlfwInputManager::JoystickEventsSimulator::AxisEventTolerance = 0.001f;
	GlfwInputManager::JoystickEventsSimulator GlfwInputManager::_joyEventsSimulator;

	int GlfwInputManager::_preScalingWidth = 0;
	int GlfwInputManager::_preScalingHeight = 0;
	unsigned long int GlfwInputManager::_lastFrameWindowSizeChanged = 0;

	namespace
	{
		MouseButton glfwToNcineMouseButton(int button)
		{
			switch (button) {
				case GLFW_MOUSE_BUTTON_LEFT: return MouseButton::Left;
				case GLFW_MOUSE_BUTTON_RIGHT: return MouseButton::Right;
				case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
				case GLFW_MOUSE_BUTTON_4: return MouseButton::Fourth;
				case GLFW_MOUSE_BUTTON_5: return MouseButton::Fifth;
				default: return MouseButton::Left;
			}
		}

		int ncineToGlfwMouseButton(MouseButton button)
		{
			switch (button) {
				case MouseButton::Left: return GLFW_MOUSE_BUTTON_LEFT;
				case MouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
				case MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
				case MouseButton::Fourth: return GLFW_MOUSE_BUTTON_4;
				case MouseButton::Fifth: return GLFW_MOUSE_BUTTON_5;
				default: return GLFW_MOUSE_BUTTON_LEFT;
			}
		}
	}

	GlfwMouseState::GlfwMouseState()
	{
	}

	bool GlfwMouseState::IsButtonDown(MouseButton button) const
	{
		const int glfwButton = ncineToGlfwMouseButton(button);
		return glfwGetMouseButton(GlfwGfxDevice::GetWindowHandle(), glfwButton) == GLFW_PRESS;
	}

	GlfwInputManager::GlfwInputManager()
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().GetGfxDevice());
		_preScalingWidth = gfxDevice._width;
		_preScalingHeight = gfxDevice._height;

		glfwSetMonitorCallback(MonitorCallback);
		glfwSetWindowCloseCallback(GlfwGfxDevice::GetWindowHandle(), WindowCloseCallback);
#if GLFW_VERSION_COMBINED >= 3300
		glfwSetWindowContentScaleCallback(GlfwGfxDevice::GetWindowHandle(), WindowContentScaleCallback);
#endif
		glfwSetWindowSizeCallback(GlfwGfxDevice::GetWindowHandle(), WindowSizeCallback);
		glfwSetFramebufferSizeCallback(GlfwGfxDevice::GetWindowHandle(), FramebufferSizeCallback);
		glfwSetKeyCallback(GlfwGfxDevice::GetWindowHandle(), KeyCallback);
		glfwSetCharCallback(GlfwGfxDevice::GetWindowHandle(), CharCallback);
		glfwSetCursorPosCallback(GlfwGfxDevice::GetWindowHandle(), CursorPosCallback);
		glfwSetMouseButtonCallback(GlfwGfxDevice::GetWindowHandle(), MouseButtonCallback);
		glfwSetScrollCallback(GlfwGfxDevice::GetWindowHandle(), ScrollCallback);
		glfwSetJoystickCallback(JoystickCallback);

#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
		for (std::int32_t i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++) {
			if (glfwJoystickPresent(i)) {
				const int joyId = i - GLFW_JOYSTICK_1;

				int numButtons = -1;
				int numAxes = -1;
				int numHats = -1;
				glfwGetJoystickButtons(i, &numButtons);
				glfwGetJoystickAxes(i, &numAxes);
#	if GLFW_VERSION_COMBINED >= 3300
				glfwGetJoystickHats(i, &numHats);
#	else
				numHats = 0;
#	endif
				if (numButtons <= 0 && numAxes <= 0 && numHats <= 0) {
					LOGI("Gamepad {} has been connected, but reports no axes/buttons/hats - skipping", joyId);
					continue;
				}

#	if GLFW_VERSION_COMBINED >= 3300
				// It seems `glfwGetJoystickGUID` can cause crash if the gamepad is quickly disconnected
				const char* guid = glfwGetJoystickGUID(i);
#	else
				const char* guid = "default";
#	endif
				LOGI("Gamepad {} \"{}\" [{}] has been connected - {} axes, {} buttons, {} hats",
					   joyId, glfwGetJoystickName(i), guid, numAxes, numButtons, numHats);
			}
		}
#endif

		_joyMapping.Init(this);

#if defined(DEATH_TARGET_EMSCRIPTEN)
		emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, GlfwInputManager::EmscriptenHandleTouch);
		emscripten_set_touchend_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, GlfwInputManager::EmscriptenHandleTouch);
		emscripten_set_touchmove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, GlfwInputManager::EmscriptenHandleTouch);
		emscripten_set_touchcancel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, GlfwInputManager::EmscriptenHandleTouch);
#endif

#if defined(WITH_IMGUI)
		ImGuiGlfwInput::Init(GlfwGfxDevice::GetWindowHandle(), true);
#endif
	}

	GlfwInputManager::~GlfwInputManager()
	{
#if defined(WITH_IMGUI)
		ImGuiGlfwInput::Shutdown();
#endif
	}

	bool GlfwJoystickState::IsButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < _numButtons && _buttons[buttonId] != GLFW_RELEASE);
	}

	unsigned char GlfwJoystickState::GetHatState(int hatId) const
	{
		return (hatId >= 0 && hatId < _numHats ? _hats[hatId] : HatState::Centered);
	}

	float GlfwJoystickState::GetAxisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < _numAxes ? _axesValues[axisId] : 0.0f);
	}

	bool GlfwInputManager::HasFocus()
	{
		// The focus going over to a window besides the main one isn't a loss of it
#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
		const bool glfwFocused = (glfwGetWindowAttrib(GlfwGfxDevice::GetWindowHandle(), GLFW_FOCUSED) != 0 || AuxiliaryWindow::IsAnyFocused());
#else
		const bool glfwFocused = (glfwGetWindowAttrib(GlfwGfxDevice::GetWindowHandle(), GLFW_FOCUSED) != 0);
#endif

		// A focus event has occurred (either gain or loss)
		if (_windowHasFocus != glfwFocused) {
			_windowHasFocus = glfwFocused;
		}

		return _windowHasFocus;
	}

#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
	void GlfwInputManager::AttachAuxiliaryWindow(GLFWwindow* window)
	{
		glfwSetKeyCallback(window, KeyCallback);
		glfwSetCharCallback(window, CharCallback);
		glfwSetCursorPosCallback(window, CursorPosCallback);
		glfwSetMouseButtonCallback(window, MouseButtonCallback);
		glfwSetScrollCallback(window, ScrollCallback);
		// Closed only by the application, the main window closes it
		glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
			glfwSetWindowShouldClose(w, GLFW_FALSE);
		});
	}
#endif

	Vector2i GlfwInputManager::ToMainWindowPosition(GLFWwindow* window, double x, double y, std::uint32_t& windowId)
	{
		windowId = 0;
		Vector2i position(static_cast<int>(x), static_cast<int>(y));
#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
		if (window != GlfwGfxDevice::GetWindowHandle()) {
			if (GlfwAuxiliaryWindow* auxiliary = GlfwAuxiliaryWindow::FromHandle(window)) {
				windowId = auxiliary->GetId();
				// What it shows may be drawn at another scale, on a display with another scale
				float scale = auxiliary->GetInputScale();
				position = Vector2i(static_cast<int>(std::round(x * scale)), static_cast<int>(std::round(y * scale)));
				Vector2i windowPos, mainPos;
				glfwGetWindowPos(window, &windowPos.X, &windowPos.Y);
				glfwGetWindowPos(GlfwGfxDevice::GetWindowHandle(), &mainPos.X, &mainPos.Y);
				position += windowPos - mainPos;
			}
		}
#else
		static_cast<void>(window);
#endif
		return position;
	}

	void GlfwInputManager::UpdateJoystickStates()
	{
		for (unsigned int joyId = 0; joyId < MaxNumJoysticks; joyId++) {
			if (glfwJoystickPresent(GLFW_JOYSTICK_1 + joyId)) {
				_joystickStates[joyId]._buttons = glfwGetJoystickButtons(joyId, &_joystickStates[joyId]._numButtons);
#if GLFW_VERSION_COMBINED >= 3300
				_joystickStates[joyId]._hats = glfwGetJoystickHats(joyId, &_joystickStates[joyId]._numHats);
#else
				_joystickStates[joyId]._hats = 0;
#endif
				_joystickStates[joyId]._axesValues = glfwGetJoystickAxes(joyId, &_joystickStates[joyId]._numAxes);

				_joyEventsSimulator.SimulateButtonsEvents(joyId, _joystickStates[joyId]._numButtons, _joystickStates[joyId]._buttons);
				_joyEventsSimulator.SimulateHatsEvents(joyId, _joystickStates[joyId]._numHats, _joystickStates[joyId]._hats);
				_joyEventsSimulator.SimulateAxesEvents(joyId, _joystickStates[joyId]._numAxes, _joystickStates[joyId]._axesValues);
			}
		}
	}

	String GlfwInputManager::GetClipboardText() const
	{
		return glfwGetClipboardString(GlfwGfxDevice::GetWindowHandle());
	}

	bool GlfwInputManager::SetClipboardText(StringView text)
	{
		glfwSetClipboardString(GlfwGfxDevice::GetWindowHandle(), String::nullTerminatedView(text).data());
		return true;
	}

	bool GlfwInputManager::IsJoyPresent(int joyId) const
	{
		DEATH_ASSERT(joyId >= 0);
		return (GLFW_JOYSTICK_1 + joyId <= GLFW_JOYSTICK_LAST && glfwJoystickPresent(GLFW_JOYSTICK_1 + joyId) != 0);
	}

	const char* GlfwInputManager::GetJoyName(int joyId) const
	{
		return (IsJoyPresent(joyId) ? glfwGetJoystickName(joyId) : nullptr);
	}

	const JoystickGuid GlfwInputManager::GetJoyGuid(int joyId) const
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return JoystickGuidType::Default;
#elif GLFW_VERSION_COMBINED >= 3300
		if (IsJoyPresent(joyId)) {
			static const char XinputPrefix[] = "78696e707574";
			const char* guid = glfwGetJoystickGUID(joyId);
			if (strncmp(guid, XinputPrefix, sizeof(XinputPrefix) - 1) == 0) {
				return JoystickGuidType::Xinput;
			} else {
				return StringView(guid);
			}

		} else {
			return JoystickGuidType::Unknown;
		}
#else
		return JoystickGuidType::Unknown;
#endif
	}

	int GlfwInputManager::GetJoyButtonCount(int joyId) const
	{
		int numButtons = -1;
		if (IsJoyPresent(joyId)) {
			glfwGetJoystickButtons(GLFW_JOYSTICK_1 + joyId, &numButtons);
		}
		return numButtons;
	}

	int GlfwInputManager::GetJoyHatCount(int joyId) const
	{
		int numHats = -1;
		if (IsJoyPresent(joyId)) {
#if GLFW_VERSION_COMBINED >= 3300
			glfwGetJoystickHats(GLFW_JOYSTICK_1 + joyId, &numHats);
#else
			numHats = 0;
#endif
		}
		return numHats;
	}

	int GlfwInputManager::GetJoyAxisCount(int joyId) const
	{
		int numAxes = -1;
		if (IsJoyPresent(joyId)) {
			glfwGetJoystickAxes(GLFW_JOYSTICK_1 + joyId, &numAxes);
		}
		return numAxes;
	}

	const JoystickState& GlfwInputManager::GetJoystickState(int joyId) const
	{
		return (IsJoyPresent(joyId) ? _joystickStates[joyId] : _nullJoystickState);
	}

	bool GlfwInputManager::JoystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs)
	{
		// TODO
		return false;
	}

	bool GlfwInputManager::JoystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs)
	{
		// TODO
		return false;
	}

	void GlfwInputManager::SetCursor(Cursor cursor)
	{
		if (cursor != _cursor) {
			switch (cursor) {
				case Cursor::Arrow: glfwSetInputMode(GlfwGfxDevice::GetWindowHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL); break;
				case Cursor::Hidden: glfwSetInputMode(GlfwGfxDevice::GetWindowHandle(), GLFW_CURSOR, GLFW_CURSOR_HIDDEN); break;
				case Cursor::HiddenLocked: glfwSetInputMode(GlfwGfxDevice::GetWindowHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED); break;
			}

#if GLFW_VERSION_COMBINED >= 3300 && !defined(DEATH_TARGET_EMSCRIPTEN)
			// Enable raw mouse motion (if supported) when disabling the cursor
			const bool enableRawMouseMotion = (cursor == Cursor::HiddenLocked && glfwRawMouseMotionSupported() == GLFW_TRUE);
			glfwSetInputMode(GlfwGfxDevice::GetWindowHandle(), GLFW_RAW_MOUSE_MOTION, enableRawMouseMotion ? GLFW_TRUE : GLFW_FALSE);
#endif

			// Handling ImGui cursor changes
			IInputManager::SetCursor(cursor);

			_cursor = cursor;
		}
	}

	void GlfwInputManager::MonitorCallback(GLFWmonitor* monitor, int event)
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().GetGfxDevice());
		gfxDevice.UpdateMonitors();
	}

	void GlfwInputManager::WindowCloseCallback(GLFWwindow* window)
	{
		bool shouldQuit = true;
		if (_inputEventHandler != nullptr) {
			shouldQuit = _inputEventHandler->OnQuitRequest();
		}

		if (shouldQuit) {
			theApplication().Quit();
		} else {
			glfwSetWindowShouldClose(window, GLFW_FALSE);
		}
	}

	void GlfwInputManager::WindowContentScaleCallback(GLFWwindow* window, float xscale, float yscale)
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().GetGfxDevice());

		// Revert the window size change if it happened the same frame its scale also changed
		if (_lastFrameWindowSizeChanged == theApplication().GetFrameCount()) {
			gfxDevice._width = _preScalingWidth;
			gfxDevice._height = _preScalingHeight;
		}

		gfxDevice.UpdateMonitorScaling(gfxDevice.GetWindowMonitorIndex());
	}

	void GlfwInputManager::WindowSizeCallback(GLFWwindow* window, int width, int height)
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().GetGfxDevice());

		// Save previous resolution for if a content scale event is coming just after a resize
		_preScalingWidth = gfxDevice._width;
		_preScalingHeight = gfxDevice._height;
		_lastFrameWindowSizeChanged = theApplication().GetFrameCount();

		gfxDevice._width = width;
		gfxDevice._height = height;

		bool isFullscreen = (glfwGetWindowMonitor(window) != nullptr);
		if (!isFullscreen) {
			gfxDevice._lastWindowWidth = width;
			gfxDevice._lastWindowHeight = height;
		}
	}

	void GlfwInputManager::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().GetGfxDevice());
		gfxDevice._drawableWidth = width;
		gfxDevice._drawableHeight = height;

		theApplication().ResizeScreenViewport(width, height);
	}

	void GlfwInputManager::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		_keyboardEvent.Scancode = scancode;
		_keyboardEvent.Sym = GlfwKeys::KeySymValueToEnum(key);
		_keyboardEvent.Mod = GlfwKeys::KeyModMaskToEnumMask(mods);

		if (action == GLFW_PRESS) {
			_inputEventHandler->OnKeyPressed(_keyboardEvent);
		} else if (action == GLFW_RELEASE) {
			_inputEventHandler->OnKeyReleased(_keyboardEvent);
		}
	}

	void GlfwInputManager::CharCallback(GLFWwindow* window, unsigned int c)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		// Current GLFW version does not return an UTF-8 string (https://github.com/glfw/glfw/issues/837)
		_textInputEvent.Length = (std::int32_t)Utf8::FromCodePoint(c, _textInputEvent.Text);
		if (_textInputEvent.Length > 0) {
			_inputEventHandler->OnTextInput(_textInputEvent);
		}
	}

	void GlfwInputManager::CursorPosCallback(GLFWwindow* window, double x, double y)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		// Over a window besides the main one, the position is made relative to the main window
		Vector2i position = ToMainWindowPosition(window, x, y, _mouseState.WindowId);
		_mouseState.X = position.X;
		_mouseState.Y = position.Y;
		_inputEventHandler->OnMouseMove(_mouseState);
	}

	void GlfwInputManager::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		double xCursor, yCursor;
		glfwGetCursorPos(window, &xCursor, &yCursor);
		Vector2i position = ToMainWindowPosition(window, xCursor, yCursor, _mouseEvent.WindowId);
		_mouseEvent.X = position.X;
		_mouseEvent.Y = position.Y;
		_mouseEvent.Button = glfwToNcineMouseButton(button);

		if (action == GLFW_PRESS) {
			_inputEventHandler->OnMouseDown(_mouseEvent);
		} else if (action == GLFW_RELEASE) {
			_inputEventHandler->OnMouseUp(_mouseEvent);
		}
	}

	void GlfwInputManager::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		// GLFW reports the horizontal offset positive to the left on every platform (for consistency with macOS and X11),
		// the opposite of the other backends
		_scrollEvent.X = static_cast<float>(-xoffset);
		_scrollEvent.Y = static_cast<float>(yoffset);
		_scrollEvent.WindowId = 0;
#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
		if (window != GlfwGfxDevice::GetWindowHandle()) {
			if (GlfwAuxiliaryWindow* auxiliary = GlfwAuxiliaryWindow::FromHandle(window)) {
				_scrollEvent.WindowId = auxiliary->GetId();
			}
		}
#endif
		_inputEventHandler->OnMouseWheel(_scrollEvent);
	}

	void GlfwInputManager::JoystickCallback(int joy, int event)
	{
		const int joyId = joy - GLFW_JOYSTICK_1;
		_joyConnectionEvent.JoyId = joyId;

		if (event == GLFW_CONNECTED) {
#if defined(DEATH_TARGET_EMSCRIPTEN) && defined(EMSCRIPTEN_USE_PORT_CONTRIB_GLFW3)
			// `contrib.glfw3` is polling gamepads asynchronously, number of buttons/axes is usually 0 here, so skip these checks instead
#	if defined(DEATH_TRACE)
#		if GLFW_VERSION_COMBINED >= 3300
			// It seems `glfwGetJoystickGUID` can cause crash if the gamepad is quickly disconnected
			const char* guid = glfwGetJoystickGUID(joy);
#		else
			const char* guid = "default";
#		endif
			LOGI("Gamepad {} \"{}\" [{}] has been connected", joyId, glfwGetJoystickName(joy), guid);
#	endif
#else
			int numButtons = -1;
			int numAxes = -1;
			int numHats = -1;
			glfwGetJoystickButtons(joy, &numButtons);
			glfwGetJoystickAxes(joy, &numAxes);
#	if GLFW_VERSION_COMBINED >= 3300
			glfwGetJoystickHats(joy, &numHats);
#	else
			numHats = 0;
#	endif

			if (numButtons <= 0 && numAxes <= 0 && numHats <= 0) {
				LOGI("Gamepad {} has been connected, but reports no axes/buttons/hats - skipping", joyId);
				return;
			}

#	if defined(DEATH_TRACE)
#		if GLFW_VERSION_COMBINED >= 3300
			// It seems `glfwGetJoystickGUID` can cause crash if the gamepad is quickly disconnected
			const char* guid = glfwGetJoystickGUID(joy);
#		else
			const char* guid = "default";
#		endif
			LOGI("Gamepad {} \"{}\" [{}] has been connected - {} axes, {} buttons, {} hats",
			       joyId, glfwGetJoystickName(joy), guid, numAxes, numButtons, numHats);
#	endif
#endif

			UpdateJoystickStates();
			
			if (_inputEventHandler != nullptr) {
				_joyMapping.OnJoyConnected(_joyConnectionEvent);
				_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
			}
		} else if (event == GLFW_DISCONNECTED) {
			_joyEventsSimulator.ResetJoystickState(joyId);
			LOGI("Gamepad {} has been disconnected", joyId);
			if (_inputEventHandler != nullptr) {
				_inputEventHandler->OnJoyDisconnected(_joyConnectionEvent);
				_joyMapping.OnJoyDisconnected(_joyConnectionEvent);
			}
		}
	}

#ifdef DEATH_TARGET_EMSCRIPTEN
	EM_BOOL GlfwInputManager::EmscriptenHandleTouch(int eventType, const EmscriptenTouchEvent* event, void* userData)
	{
		GlfwInputManager* inputManager = static_cast<GlfwInputManager*>(userData);

		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);

		TouchEvent touchEvent;
		touchEvent.Count = std::min((unsigned int)event->numTouches, TouchEvent::MaxPointers);
		switch (eventType) {
			case EMSCRIPTEN_EVENT_TOUCHSTART:
				touchEvent.Type = (touchEvent.Count >= 2 ? TouchEventType::PointerDown : TouchEventType::Down);
				break;
			case EMSCRIPTEN_EVENT_TOUCHMOVE:
				touchEvent.Type = TouchEventType::Move;
				break;
			default:
				touchEvent.Type = (touchEvent.Count >= 2 ? TouchEventType::PointerUp : TouchEventType::Up);
				break;
		}

		for (int i = 0; i < touchEvent.Count; i++) {
			auto& pointer = touchEvent.Pointers[i];
			pointer.id = event->touches[i].identifier;
			pointer.x = (float)(event->touches[i].targetX / cssWidth);
			pointer.y = (float)(event->touches[i].targetY / cssHeight);
			pointer.pressure = 1.0f;

			if (!event->touches[i].isChanged) {
				continue;
			}

			touchEvent.ActionIndex = pointer.id;
		}

		inputManager->_inputEventHandler->OnTouchEvent(touchEvent);

		return 1;
	}
#endif

	GlfwInputManager::JoystickEventsSimulator::JoystickEventsSimulator()
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_hatsState, 0, sizeof(_hatsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void GlfwInputManager::JoystickEventsSimulator::ResetJoystickState(int joyId)
	{
		std::memset(_buttonsState[joyId], 0, sizeof(unsigned char) * MaxNumButtons);
		std::memset(_hatsState[joyId], 0, sizeof(unsigned char) * MaxNumHats);
		std::memset(_axesValuesState[joyId], 0, sizeof(float) * MaxNumAxes);
	}

	void GlfwInputManager::JoystickEventsSimulator::SimulateButtonsEvents(int joyId, int numButtons, const unsigned char* buttons)
	{
		for (int buttonId = 0; buttonId < numButtons; buttonId++) {
			if (_inputEventHandler != nullptr && _buttonsState[joyId][buttonId] != buttons[buttonId]) {
				_joyButtonEvent.JoyId = joyId;
				_joyButtonEvent.ButtonId = buttonId;
				if (_joystickStates[joyId]._buttons[buttonId] == GLFW_PRESS) {
					_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
					_inputEventHandler->OnJoyButtonPressed(_joyButtonEvent);
				} else if (_joystickStates[joyId]._buttons[buttonId] == GLFW_RELEASE) {
					_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
					_inputEventHandler->OnJoyButtonReleased(_joyButtonEvent);
				}
			}
		}

		if (numButtons > 0) {
			std::memcpy(_buttonsState[joyId], buttons, sizeof(unsigned char) * numButtons);
		}
	}

	void GlfwInputManager::JoystickEventsSimulator::SimulateHatsEvents(int joyId, int numHats, const unsigned char* hats)
	{
		for (int hatId = 0; hatId < numHats; hatId++) {
			if (_inputEventHandler != nullptr && _hatsState[joyId][hatId] != hats[hatId]) {
				_joyHatEvent.JoyId = joyId;
				_joyHatEvent.HatId = hatId;
				_joyHatEvent.HatState = hats[hatId];

				_joyMapping.OnJoyHatMoved(_joyHatEvent);
				_inputEventHandler->OnJoyHatMoved(_joyHatEvent);
			}
		}

		if (numHats > 0) {
			std::memcpy(_hatsState[joyId], hats, sizeof(unsigned char) * numHats);
		}
	}

	void GlfwInputManager::JoystickEventsSimulator::SimulateAxesEvents(int joyId, int numAxes, const float* axesValues)
	{
		for (int axisId = 0; axisId < numAxes; axisId++) {
			if (_inputEventHandler != nullptr && fabsf(_axesValuesState[joyId][axisId] - axesValues[axisId]) > AxisEventTolerance) {
				_joyAxisEvent.JoyId = joyId;
				_joyAxisEvent.AxisId = axisId;
				_joyAxisEvent.Value = axesValues[axisId];
				_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
				_inputEventHandler->OnJoyAxisMoved(_joyAxisEvent);
			}
		}

		if (numAxes > 0) {
			std::memcpy(_axesValuesState[joyId], axesValues, sizeof(float) * numAxes);
		}
	}
}

#endif