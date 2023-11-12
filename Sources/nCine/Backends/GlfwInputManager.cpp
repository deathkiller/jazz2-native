#if defined(WITH_GLFW)

#include "GlfwInputManager.h"
#include "../Input/IInputEventHandler.h"
#include "../Input/JoyMapping.h"
#include "../Application.h"

#include <cstring>	// for memset() and memcpy()
#include <cmath>	// for fabsf()

#if defined(WITH_IMGUI)
#	include "ImGuiGlfwInput.h"
#endif

namespace nCine
{
	const int IInputManager::MaxNumJoysticks = GLFW_JOYSTICK_LAST - GLFW_JOYSTICK_1 + 1;

	bool GlfwInputManager::windowHasFocus_ = true;
	GlfwMouseState GlfwInputManager::mouseState_;
	GlfwMouseEvent GlfwInputManager::mouseEvent_;
	GlfwScrollEvent GlfwInputManager::scrollEvent_;
	GlfwKeyboardState GlfwInputManager::keyboardState_;
	KeyboardEvent GlfwInputManager::keyboardEvent_;
	TextInputEvent GlfwInputManager::textInputEvent_;

	GlfwJoystickState GlfwInputManager::nullJoystickState_;
	SmallVector<GlfwJoystickState, GlfwInputManager::MaxNumJoysticks> GlfwInputManager::joystickStates_(GlfwInputManager::MaxNumJoysticks);
	JoyButtonEvent GlfwInputManager::joyButtonEvent_;
	JoyHatEvent GlfwInputManager::joyHatEvent_;
	JoyAxisEvent GlfwInputManager::joyAxisEvent_;
	JoyConnectionEvent GlfwInputManager::joyConnectionEvent_;
	const float GlfwInputManager::JoystickEventsSimulator::AxisEventTolerance = 0.001f;
	GlfwInputManager::JoystickEventsSimulator GlfwInputManager::joyEventsSimulator_;

	int GlfwInputManager::preScalingWidth_ = 0;
	int GlfwInputManager::preScalingHeight_ = 0;
	unsigned long int GlfwInputManager::lastFrameWindowSizeChanged_ = 0;

	GlfwInputManager::GlfwInputManager()
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().gfxDevice());
		preScalingWidth_ = gfxDevice.width_;
		preScalingHeight_ = gfxDevice.height_;

		glfwSetMonitorCallback(monitorCallback);
		glfwSetWindowCloseCallback(GlfwGfxDevice::windowHandle(), windowCloseCallback);
#if (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 >= 3300)
		glfwSetWindowContentScaleCallback(GlfwGfxDevice::windowHandle(), windowContentScaleCallback);
#endif
		glfwSetWindowSizeCallback(GlfwGfxDevice::windowHandle(), windowSizeCallback);
		glfwSetFramebufferSizeCallback(GlfwGfxDevice::windowHandle(), framebufferSizeCallback);
		glfwSetKeyCallback(GlfwGfxDevice::windowHandle(), keyCallback);
		glfwSetCharCallback(GlfwGfxDevice::windowHandle(), charCallback);
		glfwSetCursorPosCallback(GlfwGfxDevice::windowHandle(), cursorPosCallback);
		glfwSetMouseButtonCallback(GlfwGfxDevice::windowHandle(), mouseButtonCallback);
		glfwSetScrollCallback(GlfwGfxDevice::windowHandle(), scrollCallback);
		glfwSetJoystickCallback(joystickCallback);

		joyMapping_.init(this);

#if defined(DEATH_TARGET_EMSCRIPTEN)
		emscripten_set_touchstart_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, GlfwInputManager::emscriptenHandleTouch);
		emscripten_set_touchend_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, GlfwInputManager::emscriptenHandleTouch);
		emscripten_set_touchmove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, GlfwInputManager::emscriptenHandleTouch);
		emscripten_set_touchcancel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, GlfwInputManager::emscriptenHandleTouch);
#endif

#if defined(WITH_IMGUI)
		ImGuiGlfwInput::init(GlfwGfxDevice::windowHandle(), true);
#endif
	}

	GlfwInputManager::~GlfwInputManager()
	{
#if defined(WITH_IMGUI)
		ImGuiGlfwInput::shutdown();
#endif
	}

	bool GlfwJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < numButtons_ && buttons_[buttonId] != GLFW_RELEASE);
	}

	unsigned char GlfwJoystickState::hatState(int hatId) const
	{
		return (hatId >= 0 && hatId < numHats_ ? hats_[hatId] : HatState::CENTERED);
	}

	float GlfwJoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < numAxes_ ? axesValues_[axisId] : 0.0f);
	}

	bool GlfwInputManager::hasFocus()
	{
		const bool glfwFocused = (glfwGetWindowAttrib(GlfwGfxDevice::windowHandle(), GLFW_FOCUSED) != 0);

		// A focus event has occurred (either gain or loss)
		if (windowHasFocus_ != glfwFocused) {
			windowHasFocus_ = glfwFocused;
		}

		return windowHasFocus_;
	}

	void GlfwInputManager::updateJoystickStates()
	{
		for (unsigned int joyId = 0; joyId < MaxNumJoysticks; joyId++) {
			if (glfwJoystickPresent(GLFW_JOYSTICK_1 + joyId)) {
				joystickStates_[joyId].buttons_ = glfwGetJoystickButtons(joyId, &joystickStates_[joyId].numButtons_);
#if GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3
				joystickStates_[joyId].hats_ = glfwGetJoystickHats(joyId, &joystickStates_[joyId].numHats_);
#else
				joystickStates_[joyId].hats_ = 0;
#endif
				joystickStates_[joyId].axesValues_ = glfwGetJoystickAxes(joyId, &joystickStates_[joyId].numAxes_);

				joyEventsSimulator_.simulateButtonsEvents(joyId, joystickStates_[joyId].numButtons_, joystickStates_[joyId].buttons_);
				joyEventsSimulator_.simulateHatsEvents(joyId, joystickStates_[joyId].numHats_, joystickStates_[joyId].hats_);
				joyEventsSimulator_.simulateAxesEvents(joyId, joystickStates_[joyId].numAxes_, joystickStates_[joyId].axesValues_);
			}
		}
	}

	bool GlfwInputManager::isJoyPresent(int joyId) const
	{
		ASSERT(joyId >= 0);
		ASSERT_MSG(GLFW_JOYSTICK_1 + joyId <= GLFW_JOYSTICK_LAST,
					 "joyId is %d and the maximum is %d", joyId, GLFW_JOYSTICK_LAST - GLFW_JOYSTICK_1);
		return (glfwJoystickPresent(GLFW_JOYSTICK_1 + joyId) != 0);
	}

	const char* GlfwInputManager::joyName(int joyId) const
	{
		return (isJoyPresent(joyId) ? glfwGetJoystickName(joyId) : nullptr);
	}

	const JoystickGuid GlfwInputManager::joyGuid(int joyId) const
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		return JoystickGuidType::Default;
#elif GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3
		if (isJoyPresent(joyId)) {
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

	int GlfwInputManager::joyNumButtons(int joyId) const
	{
		int numButtons = -1;
		if (isJoyPresent(joyId)) {
			glfwGetJoystickButtons(GLFW_JOYSTICK_1 + joyId, &numButtons);
		}
		return numButtons;
	}

	int GlfwInputManager::joyNumHats(int joyId) const
	{
		int numHats = -1;
		if (isJoyPresent(joyId)) {
#if GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3
			glfwGetJoystickHats(GLFW_JOYSTICK_1 + joyId, &numHats);
#else
			numHats = 0;
#endif
		}
		return numHats;
	}

	int GlfwInputManager::joyNumAxes(int joyId) const
	{
		int numAxes = -1;
		if (isJoyPresent(joyId)) {
			glfwGetJoystickAxes(GLFW_JOYSTICK_1 + joyId, &numAxes);
		}
		return numAxes;
	}

	const JoystickState& GlfwInputManager::joystickState(int joyId) const
	{
		return (isJoyPresent(joyId) ? joystickStates_[joyId] : nullJoystickState_);
	}

	bool GlfwInputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, uint32_t durationMs)
	{
		// TODO
		return false;
	}

	bool GlfwInputManager::joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs)
	{
		// TODO
		return false;
	}

	void GlfwInputManager::setCursor(Cursor cursor)
	{
		if (cursor != cursor_) {
			switch (cursor) {
				case Cursor::Arrow: glfwSetInputMode(GlfwGfxDevice::windowHandle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL); break;
				case Cursor::Hidden: glfwSetInputMode(GlfwGfxDevice::windowHandle(), GLFW_CURSOR, GLFW_CURSOR_HIDDEN); break;
				case Cursor::HiddenLocked: glfwSetInputMode(GlfwGfxDevice::windowHandle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED); break;
			}

#if GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3
			// Enable raw mouse motion (if supported) when disabling the cursor
			const bool enableRawMouseMotion = (cursor == Cursor::HiddenLocked && glfwRawMouseMotionSupported() == GLFW_TRUE);
			glfwSetInputMode(GlfwGfxDevice::windowHandle(), GLFW_RAW_MOUSE_MOTION, enableRawMouseMotion ? GLFW_TRUE : GLFW_FALSE);
#endif

			// Handling ImGui cursor changes
			IInputManager::setCursor(cursor);

			cursor_ = cursor;
		}
	}

	void GlfwInputManager::monitorCallback(GLFWmonitor* monitor, int event)
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().gfxDevice());
		gfxDevice.updateMonitors();
	}

	void GlfwInputManager::windowCloseCallback(GLFWwindow* window)
	{
		bool shouldQuit = true;
		if (inputEventHandler_ != nullptr) {
			shouldQuit = inputEventHandler_->OnQuitRequest();
		}

		if (shouldQuit) {
			theApplication().quit();
		} else {
			glfwSetWindowShouldClose(window, GLFW_FALSE);
		}
	}

	void GlfwInputManager::windowContentScaleCallback(GLFWwindow* window, float xscale, float yscale)
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().gfxDevice());

		// Revert the window size change if it happened the same frame its scale also changed
		if (lastFrameWindowSizeChanged_ == theApplication().numFrames()) {
			gfxDevice.width_ = preScalingWidth_;
			gfxDevice.height_ = preScalingHeight_;
		}

		gfxDevice.updateMonitorScaling(gfxDevice.windowMonitorIndex());
	}

	void GlfwInputManager::windowSizeCallback(GLFWwindow* window, int width, int height)
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().gfxDevice());

		// Save previous resolution for if a content scale event is coming just after a resize
		preScalingWidth_ = gfxDevice.width_;
		preScalingHeight_ = gfxDevice.height_;
		lastFrameWindowSizeChanged_ = theApplication().numFrames();

		gfxDevice.width_ = width;
		gfxDevice.height_ = height;

		bool isFullscreen = (glfwGetWindowMonitor(window) != nullptr);
		if (!isFullscreen) {
			gfxDevice.lastWindowWidth_ = width;
			gfxDevice.lastWindowHeight_ = height;
		}
	}

	void GlfwInputManager::framebufferSizeCallback(GLFWwindow* window, int width, int height)
	{
		GlfwGfxDevice& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().gfxDevice());
		gfxDevice.drawableWidth_ = width;
		gfxDevice.drawableHeight_ = height;

		theApplication().resizeScreenViewport(width, height);
	}

	void GlfwInputManager::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		if (inputEventHandler_ == nullptr) {
			return;
		}

		keyboardEvent_.scancode = scancode;
		keyboardEvent_.sym = GlfwKeys::keySymValueToEnum(key);
		keyboardEvent_.mod = GlfwKeys::keyModMaskToEnumMask(mods);

		if (action == GLFW_PRESS) {
			inputEventHandler_->OnKeyPressed(keyboardEvent_);
		} else if (action == GLFW_RELEASE) {
			inputEventHandler_->OnKeyReleased(keyboardEvent_);
		}
	}

	void GlfwInputManager::charCallback(GLFWwindow* window, unsigned int c)
	{
		if (inputEventHandler_ == nullptr) {
			return;
		}

		// TODO: text input
		// Current GLFW version does not return an UTF-8 string (https://github.com/glfw/glfw/issues/837)
		//nctl::Utf8::codePointToUtf8(c, textInputEvent_.text, nullptr);
		//inputEventHandler_->OnTextInput(textInputEvent_);
	}

	void GlfwInputManager::cursorPosCallback(GLFWwindow* window, double x, double y)
	{
		if (inputEventHandler_ == nullptr) {
			return;
		}

		mouseState_.x = static_cast<int>(x);
		mouseState_.y = theApplication().height() - static_cast<int>(y);
		inputEventHandler_->OnMouseMove(mouseState_);
	}

	void GlfwInputManager::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
	{
		if (inputEventHandler_ == nullptr) {
			return;
		}

		double xCursor, yCursor;
		glfwGetCursorPos(window, &xCursor, &yCursor);
		mouseEvent_.x = static_cast<int>(xCursor);
		mouseEvent_.y = theApplication().height() - static_cast<int>(yCursor);
		mouseEvent_.button_ = button;

		if (action == GLFW_PRESS) {
			inputEventHandler_->OnMouseDown(mouseEvent_);
		} else if (action == GLFW_RELEASE) {
			inputEventHandler_->OnMouseUp(mouseEvent_);
		}
	}

	void GlfwInputManager::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
	{
		if (inputEventHandler_ == nullptr) {
			return;
		}

		scrollEvent_.x = static_cast<float>(xoffset);
		scrollEvent_.y = static_cast<float>(yoffset);
		inputEventHandler_->OnMouseWheel(scrollEvent_);
	}

	void GlfwInputManager::joystickCallback(int joy, int event)
	{
		const int joyId = joy - GLFW_JOYSTICK_1;
		joyConnectionEvent_.joyId = joyId;

		if (event == GLFW_CONNECTED) {
#if defined(DEATH_TRACE)
			int numButtons = -1;
			int numAxes = -1;
			int numHats = -1;
			glfwGetJoystickButtons(joy, &numButtons);
#	if defined(DEATH_TARGET_EMSCRIPTEN)
			numHats = 0;
			const char* guid = "default";
#	elif GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 3
			glfwGetJoystickHats(joy, &numHats);
			const char* guid = glfwGetJoystickGUID(joy);
#	else
			numHats = 0;
			const char* guid = nullptr;
#	endif
			glfwGetJoystickAxes(joy, &numAxes);
			LOGI("Joystick %d \"%s\" (%s) has been connected - %d axes, %d buttons, %d hats",
			       joyId, glfwGetJoystickName(joy), guid, numAxes, numButtons, numHats);
#endif
			updateJoystickStates();
			
			if (inputEventHandler_ != nullptr) {
				joyMapping_.onJoyConnected(joyConnectionEvent_);
				inputEventHandler_->OnJoyConnected(joyConnectionEvent_);
			}
		} else if (event == GLFW_DISCONNECTED) {
			joyEventsSimulator_.resetJoystickState(joyId);
			LOGI("Joystick %d has been disconnected", joyId);
			if (inputEventHandler_ != nullptr) {
				inputEventHandler_->OnJoyDisconnected(joyConnectionEvent_);
				joyMapping_.onJoyDisconnected(joyConnectionEvent_);
			}
		}
	}

#ifdef DEATH_TARGET_EMSCRIPTEN
	EM_BOOL GlfwInputManager::emscriptenHandleTouch(int eventType, const EmscriptenTouchEvent* event, void* userData)
	{
		GlfwInputManager* inputManager = static_cast<GlfwInputManager*>(userData);

		double cssWidth = 0.0;
		double cssHeight = 0.0;
		emscripten_get_element_css_size("canvas", &cssWidth, &cssHeight);

		TouchEvent touchEvent;
		touchEvent.count = std::min((unsigned int)event->numTouches, TouchEvent::MaxPointers);
		switch (eventType) {
			case EMSCRIPTEN_EVENT_TOUCHSTART:
				touchEvent.type = (touchEvent.count >= 2 ? TouchEventType::PointerDown : TouchEventType::Down);
				break;
			case EMSCRIPTEN_EVENT_TOUCHMOVE:
				touchEvent.type = TouchEventType::Move;
				break;
			default:
				touchEvent.type = (touchEvent.count >= 0 ? TouchEventType::PointerUp : TouchEventType::Up);
				break;
		}

		for (int i = 0; i < touchEvent.count; i++) {
			auto& pointer = touchEvent.pointers[i];
			pointer.id = event->touches[i].identifier;
			pointer.x = (float)(event->touches[i].targetX / cssWidth);
			pointer.y = (float)(event->touches[i].targetY / cssHeight);
			pointer.pressure = 1.0f;

			if (!event->touches[i].isChanged) {
				continue;
			}

			touchEvent.actionIndex = pointer.id;
			inputManager->inputEventHandler_->OnTouchEvent(touchEvent);
		}

		return 1;
	}
#endif

	GlfwInputManager::JoystickEventsSimulator::JoystickEventsSimulator()
	{
		std::memset(buttonsState_, 0, sizeof(buttonsState_));
		std::memset(hatsState_, 0, sizeof(hatsState_));
		std::memset(axesValuesState_, 0, sizeof(axesValuesState_));
	}

	void GlfwInputManager::JoystickEventsSimulator::resetJoystickState(int joyId)
	{
		std::memset(buttonsState_[joyId], 0, sizeof(unsigned char) * MaxNumButtons);
		std::memset(hatsState_[joyId], 0, sizeof(unsigned char) * MaxNumHats);
		std::memset(axesValuesState_[joyId], 0, sizeof(float) * MaxNumAxes);
	}

	void GlfwInputManager::JoystickEventsSimulator::simulateButtonsEvents(int joyId, int numButtons, const unsigned char* buttons)
	{
		for (int buttonId = 0; buttonId < numButtons; buttonId++) {
			if (inputEventHandler_ != nullptr && buttonsState_[joyId][buttonId] != buttons[buttonId]) {
				joyButtonEvent_.joyId = joyId;
				joyButtonEvent_.buttonId = buttonId;
				if (joystickStates_[joyId].buttons_[buttonId] == GLFW_PRESS) {
					joyMapping_.onJoyButtonPressed(joyButtonEvent_);
					inputEventHandler_->OnJoyButtonPressed(joyButtonEvent_);
				} else if (joystickStates_[joyId].buttons_[buttonId] == GLFW_RELEASE) {
					joyMapping_.onJoyButtonReleased(joyButtonEvent_);
					inputEventHandler_->OnJoyButtonReleased(joyButtonEvent_);
				}
			}
		}

		if (numButtons > 0) {
			std::memcpy(buttonsState_[joyId], buttons, sizeof(unsigned char) * numButtons);
		}
	}

	void GlfwInputManager::JoystickEventsSimulator::simulateHatsEvents(int joyId, int numHats, const unsigned char* hats)
	{
		for (int hatId = 0; hatId < numHats; hatId++) {
			if (inputEventHandler_ != nullptr && hatsState_[joyId][hatId] != hats[hatId]) {
				joyHatEvent_.joyId = joyId;
				joyHatEvent_.hatId = hatId;
				joyHatEvent_.hatState = hats[hatId];

				joyMapping_.onJoyHatMoved(joyHatEvent_);
				inputEventHandler_->OnJoyHatMoved(joyHatEvent_);
			}
		}

		if (numHats > 0) {
			std::memcpy(hatsState_[joyId], hats, sizeof(unsigned char) * numHats);
		}
	}

	void GlfwInputManager::JoystickEventsSimulator::simulateAxesEvents(int joyId, int numAxes, const float* axesValues)
	{
		for (int axisId = 0; axisId < numAxes; axisId++) {
			if (inputEventHandler_ != nullptr && fabsf(axesValuesState_[joyId][axisId] - axesValues[axisId]) > AxisEventTolerance) {
				joyAxisEvent_.joyId = joyId;
				joyAxisEvent_.axisId = axisId;
				joyAxisEvent_.value = axesValues[axisId];
				joyMapping_.onJoyAxisMoved(joyAxisEvent_);
				inputEventHandler_->OnJoyAxisMoved(joyAxisEvent_);
			}
		}

		if (numAxes > 0) {
			std::memcpy(axesValuesState_[joyId], axesValues, sizeof(float) * numAxes);
		}
	}
}

#endif