#include "AndroidInputManager.h"
#include "../../Input/IInputEventHandler.h"
#include "AndroidJniHelper.h"
#include "AndroidApplication.h"
#include "../../Base/Timer.h"
#include "../../Input/JoyMapping.h"

#include <android/input.h>
#include <android/sensor.h>
#include <cstring> // for memset()

using namespace Death::Containers::Literals;

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 4;

	ASensorManager* AndroidInputManager::sensorManager_ = nullptr;
	const ASensor* AndroidInputManager::accelerometerSensor_ = nullptr;
	ASensorEventQueue* AndroidInputManager::sensorEventQueue_ = nullptr;
	bool AndroidInputManager::accelerometerEnabled_ = false;
	AccelerometerEvent AndroidInputManager::accelerometerEvent_;
	TouchEvent AndroidInputManager::touchEvent_;
	AndroidKeyboardState AndroidInputManager::keyboardState_;
	KeyboardEvent AndroidInputManager::keyboardEvent_;
	TextInputEvent AndroidInputManager::textInputEvent_;
	AndroidMouseState AndroidInputManager::mouseState_;
	AndroidMouseEvent AndroidInputManager::mouseEvent_;
	ScrollEvent AndroidInputManager::scrollEvent_;
	int AndroidInputManager::simulatedMouseButtonState_ = 0;

	AndroidJoystickState AndroidInputManager::nullJoystickState_;
	AndroidJoystickState AndroidInputManager::joystickStates_[MaxNumJoysticks];
	JoyButtonEvent AndroidInputManager::joyButtonEvent_;
	JoyHatEvent AndroidInputManager::joyHatEvent_;
	JoyAxisEvent AndroidInputManager::joyAxisEvent_;
	JoyConnectionEvent AndroidInputManager::joyConnectionEvent_;
	Timer AndroidInputManager::joyCheckTimer_;

	// TODO: Implement new axis order - https://github.com/libsdl-org/SDL/commit/de3909a190f6e1a3f11776ce42927f99b0381675
	const int AndroidJoystickState::AxesToMap[AndroidJoystickState::NumAxesToMap] = {
		AMOTION_EVENT_AXIS_X, AMOTION_EVENT_AXIS_Y, AMOTION_EVENT_AXIS_Z,
		AMOTION_EVENT_AXIS_RX, AMOTION_EVENT_AXIS_RY, AMOTION_EVENT_AXIS_RZ,
		AMOTION_EVENT_AXIS_LTRIGGER, AMOTION_EVENT_AXIS_RTRIGGER,
		AMOTION_EVENT_AXIS_BRAKE, AMOTION_EVENT_AXIS_GAS,
		AMOTION_EVENT_AXIS_HAT_X, AMOTION_EVENT_AXIS_HAT_Y
	};

	AndroidJoystickState::AndroidJoystickState()
		: deviceId_(-1), numButtons_(0), numAxes_(0), numAxesMapped_(0),
			hasDPad_(false), hasHatAxes_(false), hatState_(HatState::Centered)
	{
		name_[0] = '\0';
		for (int i = 0; i < MaxButtons; i++) {
			buttonsMapping_[i] = 0;
			buttons_[i] = false;
		}
		for (int i = 0; i < MaxAxes; i++) {
			axesMapping_[i] = 0;
			axesMinValues_[i] = -1.0f;
			axesRangeValues_[i] = 2.0f;
			axesValues_[i] = 0.0f;
		}
	}

	AndroidInputManager::AndroidInputManager(struct android_app* state)
	{
		initAccelerometerSensor(state);
		joyMapping_.Init(this);
		checkConnectedJoysticks();

#if defined(WITH_IMGUI)
		ImGuiAndroidInput::init(state->window);
#endif
	}

	AndroidInputManager::~AndroidInputManager()
	{
#if defined(WITH_IMGUI)
		ImGuiAndroidInput::shutdown();
#endif
	}

	bool AndroidMouseState::isLeftButtonDown() const
	{
		return (buttonState_ & AMOTION_EVENT_BUTTON_PRIMARY) != 0;
	}

	bool AndroidMouseState::isMiddleButtonDown() const
	{
		return (buttonState_ & AMOTION_EVENT_BUTTON_TERTIARY) != 0;
	}

	bool AndroidMouseState::isRightButtonDown() const
	{
		return (buttonState_ & AMOTION_EVENT_BUTTON_SECONDARY) != 0;
	}

	bool AndroidMouseState::isFourthButtonDown() const
	{
		return (buttonState_ & AMOTION_EVENT_BUTTON_BACK) != 0;
	}

	bool AndroidMouseState::isFifthButtonDown() const
	{
		return (buttonState_ & AMOTION_EVENT_BUTTON_FORWARD) != 0;
	}

	bool AndroidMouseEvent::isLeftButton() const
	{
		return (button_ & AMOTION_EVENT_BUTTON_PRIMARY) != 0;
	}

	bool AndroidMouseEvent::isMiddleButton() const
	{
		return (button_ & AMOTION_EVENT_BUTTON_TERTIARY) != 0;
	}

	bool AndroidMouseEvent::isRightButton() const
	{
		return (button_ & AMOTION_EVENT_BUTTON_SECONDARY) != 0;
	}

	bool AndroidMouseEvent::isFourthButton() const
	{
		return (button_ & AMOTION_EVENT_BUTTON_BACK) != 0;
	}

	bool AndroidMouseEvent::isFifthButton() const
	{
		return (button_ & AMOTION_EVENT_BUTTON_FORWARD) != 0;
	}

	bool AndroidJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < numButtons_ && buttons_[buttonId]);
	}

	unsigned char AndroidJoystickState::hatState(int hatId) const
	{
		return (hatId >= 0 && hatId < numHats_ ? hatState_ : HatState::Centered);
	}

	float AndroidJoystickState::axisValue(int axisId) const
	{
		// The value has already been remapped from min..max to -1.0f..1.0f
		return (axisId >= 0 && axisId < numAxesMapped_ ? axesValues_[axisId] : 0.0f);
	}

	/*! This method is called by `enableAccelerometer()` and when the application gains focus */
	void AndroidInputManager::enableAccelerometerSensor()
	{
		if (accelerometerEnabled_ && accelerometerSensor_ != nullptr) {
			ASensorEventQueue_enableSensor(sensorEventQueue_, accelerometerSensor_);
			// 60 events per second
			ASensorEventQueue_setEventRate(sensorEventQueue_, accelerometerSensor_, (1000L / 60) * 1000);
		}
	}

	/*! This method is called by `enableAccelerometer()` and when the application loses focus */
	void AndroidInputManager::disableAccelerometerSensor()
	{
		if (accelerometerEnabled_ && accelerometerSensor_ != nullptr) {
			ASensorEventQueue_disableSensor(sensorEventQueue_, accelerometerSensor_);
		}
	}

	/*! Activates the sensor and raises the flag needed for application focus handling */
	void AndroidInputManager::enableAccelerometer(bool enabled)
	{
		if (enabled) {
			enableAccelerometerSensor();
		} else {
			disableAccelerometerSensor();
		}
		accelerometerEnabled_ = enabled;
	}

	void AndroidInputManager::parseAccelerometerEvent()
	{
		if (inputEventHandler_ != nullptr && accelerometerEnabled_ && accelerometerSensor_ != nullptr) {
			ASensorEvent event;
			while (ASensorEventQueue_getEvents(sensorEventQueue_, &event, 1) > 0) {
				accelerometerEvent_.x = event.acceleration.x;
				accelerometerEvent_.y = event.acceleration.y;
				accelerometerEvent_.z = event.acceleration.z;
				inputEventHandler_->OnAcceleration(accelerometerEvent_);
			}
		}
	}

	bool AndroidInputManager::parseEvent(const AInputEvent* event)
	{
		// Early out if there is no input event handler
		if (inputEventHandler_ == nullptr) {
			return false;
		}

		bool isEventHandled = false;

#if defined(WITH_IMGUI)
		isEventHandled |= ImGuiAndroidInput::processEvent(event);
#endif

		// Checking for gamepad events first
		if (((AInputEvent_getSource(event) & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
			(AInputEvent_getSource(event) & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK ||
			(AInputEvent_getSource(event) & AINPUT_SOURCE_DPAD) == AINPUT_SOURCE_DPAD)) {
			isEventHandled = processGamepadEvent(event);
		} else if (((AInputEvent_getSource(event) & AINPUT_SOURCE_KEYBOARD) == AINPUT_SOURCE_KEYBOARD ||
			(AKeyEvent_getFlags(event) & AKEY_EVENT_FLAG_SOFT_KEYBOARD) == AKEY_EVENT_FLAG_SOFT_KEYBOARD) &&
				 AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
			isEventHandled = processKeyboardEvent(event);
		} else if ((AInputEvent_getSource(event) & AINPUT_SOURCE_TOUCHSCREEN) == AINPUT_SOURCE_TOUCHSCREEN &&
				 AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
			isEventHandled = processTouchEvent(event);
		} else if ((AInputEvent_getSource(event) & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE &&
				 AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
			isEventHandled = processMouseEvent(event);
		} else if ((AInputEvent_getSource(event) & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE &&
				 AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
			isEventHandled = processMouseKeyEvent(event);
		}

		return isEventHandled;
	}

	bool AndroidInputManager::isJoyPresent(int joyId) const
	{
		ASSERT(joyId >= 0);
		ASSERT_MSG(joyId < int(MaxNumJoysticks), "joyId is %d and the maximum is %u", joyId, MaxNumJoysticks - 1);

		return (joystickStates_[joyId].deviceId_ != -1);
	}

	const char* AndroidInputManager::joyName(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return joystickStates_[joyId].name_;
		} else {
			return nullptr;
		}
	}

	const JoystickGuid AndroidInputManager::joyGuid(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return joystickStates_[joyId].guid_;
		} else {
			return JoystickGuidType::Unknown;
		}
	}

	int AndroidInputManager::joyNumButtons(int joyId) const
	{
		return (isJoyPresent(joyId) ? joystickStates_[joyId].numButtons_ : -1);
	}

	int AndroidInputManager::joyNumHats(int joyId) const
	{
		return (isJoyPresent(joyId) ? joystickStates_[joyId].numHats_ : -1);
	}

	int AndroidInputManager::joyNumAxes(int joyId) const
	{
		return (isJoyPresent(joyId) ? joystickStates_[joyId].numAxesMapped_ : -1);
	}

	const JoystickState& AndroidInputManager::joystickState(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return joystickStates_[joyId];
		} else {
			return nullJoystickState_;
		}
	}
	
	bool AndroidInputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, uint32_t durationMs)
	{
		// TODO: Rumble on Android
		return false;
	}

	bool AndroidInputManager::joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs)
	{
		// TODO: Rumble on Android
		return false;
	}

	bool AndroidInputManager::processGamepadEvent(const AInputEvent* event)
	{
		const int deviceId = AInputEvent_getDeviceId(event);
		const int joyId = findJoyId(deviceId);

		// If the index is valid and device is not blacklisted then the structure can be updated
		if (joyId > -1 && joystickStates_[joyId].guid_.isValid()) {
			switch (AInputEvent_getType(event)) {
				case AINPUT_EVENT_TYPE_KEY: {
					const int keyCode = AKeyEvent_getKeyCode(event);
					int buttonIndex = -1;
					if (keyCode >= AKEYCODE_BUTTON_A && keyCode < AKEYCODE_ESCAPE) {
						buttonIndex = joystickStates_[joyId].buttonsMapping_[keyCode - AKEYCODE_BUTTON_A];
					} else if (keyCode == AKEYCODE_BACK) {
						// Back button is always the last one
						const unsigned int lastIndex = AndroidJoystickState::MaxButtons - 1;
						buttonIndex = joystickStates_[joyId].buttonsMapping_[lastIndex];
					}

					if (buttonIndex != -1) {
						joyButtonEvent_.joyId = joyId;
						joyButtonEvent_.buttonId = buttonIndex;
						switch (AKeyEvent_getAction(event)) {
							case AKEY_EVENT_ACTION_DOWN:
								joystickStates_[joyId].buttons_[buttonIndex] = true;
								joyMapping_.OnJoyButtonPressed(joyButtonEvent_);
								inputEventHandler_->OnJoyButtonPressed(joyButtonEvent_);
								break;
							case AKEY_EVENT_ACTION_UP:
								joystickStates_[joyId].buttons_[buttonIndex] = false;
								joyMapping_.OnJoyButtonReleased(joyButtonEvent_);
								inputEventHandler_->OnJoyButtonReleased(joyButtonEvent_);
								break;
							case AKEY_EVENT_ACTION_MULTIPLE:
								break;
						}
					}

					if (keyCode >= AKEYCODE_DPAD_UP && keyCode < AKEYCODE_DPAD_CENTER) {
						joyHatEvent_.joyId = joyId;
						joyHatEvent_.hatId = 0; // No more than one hat is supported

						unsigned char hatState = joystickStates_[joyId].hatState_;
						unsigned char hatValue = 0;

						switch (keyCode) {
							case AKEYCODE_DPAD_UP: hatValue = HatState::Up; break;
							case AKEYCODE_DPAD_DOWN: hatValue = HatState::Down; break;
							case AKEYCODE_DPAD_LEFT: hatValue = HatState::Left; break;
							case AKEYCODE_DPAD_RIGHT: hatValue = HatState::Right; break;
						}
						if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
							hatState |= hatValue;
						} else {
							hatState &= ~hatValue;
						}

						if (joystickStates_[joyId].hatState_ != hatState) {
							joystickStates_[joyId].hatState_ = hatState;
							joyHatEvent_.hatState = joystickStates_[joyId].hatState_;

							joyMapping_.OnJoyHatMoved(joyHatEvent_);
							inputEventHandler_->OnJoyHatMoved(joyHatEvent_);
						}
					}
					break;
				}
				case AINPUT_EVENT_TYPE_MOTION: {
					joyAxisEvent_.joyId = joyId;

					auto& joyState = joystickStates_[joyId];
					unsigned char hatState = 0;
					for (int i = 0; i < joyState.numAxes_; i++) {
						const int axis = joyState.axesMapping_[i];
						const float axisRawValue = AMotionEvent_getAxisValue(event, axis, 0);
						const float axisValue = -1.0f + 2.0f * (axisRawValue - joyState.axesMinValues_[i]) / joyState.axesRangeValues_[i];

						if (axis == AMOTION_EVENT_AXIS_HAT_X || axis == AMOTION_EVENT_AXIS_HAT_Y) {
							joyHatEvent_.joyId = joyId;
							joyHatEvent_.hatId = 0; // No more than one hat is supported

							constexpr float HatThresholdValue = 0.99f;
							if (axis == AMOTION_EVENT_AXIS_HAT_X) {
								if (axisValue > HatThresholdValue) {
									hatState |= HatState::Right;
								} else if (axisValue < -HatThresholdValue) {
									hatState |= HatState::Left;
								}
							} else {
								if (axisValue > HatThresholdValue) {
									hatState |= HatState::Down;
								} else if (axisValue < -HatThresholdValue) {
									hatState |= HatState::Up;
								}
							}
						} else {
							joyState.axesValues_[i] = axisValue;
							
							joyAxisEvent_.axisId = i;
							joyAxisEvent_.value = axisValue;
							joyMapping_.OnJoyAxisMoved(joyAxisEvent_);
							inputEventHandler_->OnJoyAxisMoved(joyAxisEvent_);
						}
					}

					if (joyState.hatState_ != hatState) {
						joyState.hatState_ = hatState;
						joyHatEvent_.hatState = joyState.hatState_;
						joyMapping_.OnJoyHatMoved(joyHatEvent_);
						inputEventHandler_->OnJoyHatMoved(joyHatEvent_);
					}
					break;
				}
			}
		} else {
			LOGW("No available joystick ID for device %d, dropping button event", deviceId);
		}

		return true;
	}

	bool AndroidInputManager::processKeyboardEvent(const AInputEvent* event)
	{
		const int keyCode = AKeyEvent_getKeyCode(event);

		// Hardware volume keys are not handled by the engine
		if (keyCode == AKEYCODE_VOLUME_UP || keyCode == AKEYCODE_VOLUME_DOWN) {
			return false;
		}

		keyboardEvent_.scancode = AKeyEvent_getScanCode(event);
		keyboardEvent_.sym = AndroidKeys::keySymValueToEnum(AKeyEvent_getKeyCode(event));
		keyboardEvent_.mod = AndroidKeys::keyModMaskToEnumMask(AKeyEvent_getMetaState(event));

		const unsigned int keySym = static_cast<unsigned int>(keyboardEvent_.sym);
		switch (AKeyEvent_getAction(event)) {
			case AKEY_EVENT_ACTION_DOWN:
				if (keyboardEvent_.sym != KeySym::UNKNOWN) {
					keyboardState_.keys_[keySym] = 1;
				}
				inputEventHandler_->OnKeyPressed(keyboardEvent_);
				break;
			case AKEY_EVENT_ACTION_UP:
				if (keyboardEvent_.sym != KeySym::UNKNOWN) {
					keyboardState_.keys_[keySym] = 0;
				}
				inputEventHandler_->OnKeyReleased(keyboardEvent_);
				break;
			case AKEY_EVENT_ACTION_MULTIPLE:
				inputEventHandler_->OnKeyPressed(keyboardEvent_);
				break;
		}

		// TODO: Text input
		/*if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN &&
			(AKeyEvent_getMetaState(event) & AMETA_CTRL_ON) == 0) {
			AndroidJniClass_KeyEvent keyEvent(AInputEvent_getType(event), keyCode);
			if (keyEvent.isPrintingKey()) {
				const int unicodeKey = keyEvent.getUnicodeChar(AKeyEvent_getMetaState(event));
				nctl::Utf8::codePointToUtf8(unicodeKey, textInputEvent_.text, nullptr);
				inputEventHandler_->OnTextInput(textInputEvent_);
			}
		}*/

		return true;
	}

	bool AndroidInputManager::processTouchEvent(const AInputEvent* event)
	{
		const int action = AMotionEvent_getAction(event);
		
		Vector2i res = theApplication().GetResolution();
		float w = static_cast<float>(res.X);
		float h = static_cast<float>(res.Y);

		touchEvent_.count = AMotionEvent_getPointerCount(event);
		touchEvent_.actionIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
		for (unsigned int i = 0; i < touchEvent_.count && i < TouchEvent::MaxPointers; i++) {
			TouchEvent::Pointer& pointer = touchEvent_.pointers[i];
			pointer.id = AMotionEvent_getPointerId(event, i);
			pointer.x = AMotionEvent_getX(event, i) / w;
			pointer.y = AMotionEvent_getY(event, i) / h;
			pointer.pressure = AMotionEvent_getPressure(event, i);
		}

		switch (action & AMOTION_EVENT_ACTION_MASK) {
			case AMOTION_EVENT_ACTION_DOWN:
				touchEvent_.type = TouchEventType::Down;
				break;
			case AMOTION_EVENT_ACTION_UP:
				touchEvent_.type = TouchEventType::Up;
				break;
			case AMOTION_EVENT_ACTION_MOVE:
				touchEvent_.type = TouchEventType::Move;
				break;
			case AMOTION_EVENT_ACTION_POINTER_DOWN:
				touchEvent_.type = TouchEventType::PointerDown;
				break;
			case AMOTION_EVENT_ACTION_POINTER_UP:
				touchEvent_.type = TouchEventType::PointerUp;
				break;
		}

		inputEventHandler_->OnTouchEvent(touchEvent_);
		return true;
	}

	bool AndroidInputManager::processMouseEvent(const AInputEvent* event)
	{
		const int action = AMotionEvent_getAction(event);
		int buttonState = 0;

		mouseEvent_.x = static_cast<int>(AMotionEvent_getX(event, 0));
		mouseEvent_.y = static_cast<int>(theApplication().GetHeight() - AMotionEvent_getY(event, 0));
		mouseState_.x = mouseEvent_.x;
		mouseState_.y = mouseEvent_.y;

		// Mask out back and forward buttons in the detected state
		// as those are simulated as right and middle buttons
		int maskOutButtons = 0;
		if (simulatedMouseButtonState_ & AMOTION_EVENT_BUTTON_SECONDARY) {
			maskOutButtons |= AMOTION_EVENT_BUTTON_BACK;
		}
		if (simulatedMouseButtonState_ & AMOTION_EVENT_BUTTON_TERTIARY) {
			maskOutButtons |= AMOTION_EVENT_BUTTON_FORWARD;
		}

		switch (action) {
			case AMOTION_EVENT_ACTION_DOWN:
				buttonState = AMotionEvent_getButtonState(event);
				buttonState &= ~maskOutButtons;
				buttonState |= simulatedMouseButtonState_;

				mouseEvent_.button_ = mouseState_.buttonState_ ^ buttonState; // pressed button mask
				mouseState_.buttonState_ = buttonState;
				inputEventHandler_->OnMouseDown(mouseEvent_);
				break;
			case AMOTION_EVENT_ACTION_UP:
				buttonState = AMotionEvent_getButtonState(event);
				buttonState &= ~maskOutButtons;
				buttonState |= simulatedMouseButtonState_;

				mouseEvent_.button_ = mouseState_.buttonState_ ^ buttonState; // released button mask
				mouseState_.buttonState_ = buttonState;
				inputEventHandler_->OnMouseUp(mouseEvent_);
				break;
			case AMOTION_EVENT_ACTION_MOVE:
			case AMOTION_EVENT_ACTION_HOVER_MOVE:
				inputEventHandler_->OnMouseMove(mouseState_);
				break;
		}

		scrollEvent_.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0);
		scrollEvent_.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
		if (fabsf(scrollEvent_.x) > 0.0f || fabsf(scrollEvent_.y) > 0.0f) {
			inputEventHandler_->OnMouseWheel(scrollEvent_);
		}

		return true;
	}

	bool AndroidInputManager::processMouseKeyEvent(const AInputEvent* event)
	{
		const int keyCode = AKeyEvent_getKeyCode(event);
		if (keyCode == AKEYCODE_BACK || keyCode == AKEYCODE_FORWARD) {
			const int simulatedButton = (keyCode == AKEYCODE_BACK ? AMOTION_EVENT_BUTTON_SECONDARY : AMOTION_EVENT_BUTTON_TERTIARY);
			static int oldAction = AKEY_EVENT_ACTION_UP;
			const int action = AKeyEvent_getAction(event);

			// checking previous action to avoid key repeat events
			if (action == AKEY_EVENT_ACTION_DOWN && oldAction == AKEY_EVENT_ACTION_UP) {
				oldAction = action;
				simulatedMouseButtonState_ |= simulatedButton;
				mouseEvent_.button_ = simulatedButton;
				mouseState_.buttonState_ |= simulatedButton;
				inputEventHandler_->OnMouseDown(mouseEvent_);
			} else if (action == AKEY_EVENT_ACTION_UP && oldAction == AKEY_EVENT_ACTION_DOWN) {
				oldAction = action;
				simulatedMouseButtonState_ &= ~simulatedButton;
				mouseEvent_.button_ = simulatedButton;
				mouseState_.buttonState_ &= ~simulatedButton;
				inputEventHandler_->OnMouseUp(mouseEvent_);
			}
		}

		return true;
	}

	void AndroidInputManager::initAccelerometerSensor(android_app* state)
	{
		// Prepare to monitor accelerometer
#if __ANDROID_API__ >= 26
		AndroidApplication& application = static_cast<AndroidApplication&>(theApplication());
		sensorManager_ = ASensorManager_getInstanceForPackage(application.packageName());
#else
		sensorManager_ = ASensorManager_getInstance();
#endif
		accelerometerSensor_ = ASensorManager_getDefaultSensor(sensorManager_, ASENSOR_TYPE_ACCELEROMETER);
		sensorEventQueue_ = ASensorManager_createEventQueue(sensorManager_, state->looper, LOOPER_ID_USER, nullptr, nullptr);

		if (accelerometerSensor_ == nullptr) {
			LOGW("No accelerometer sensor available");
		}
	}

	void AndroidInputManager::updateJoystickConnections()
	{
		if (joyCheckTimer_.interval() >= JoyCheckRateSecs) {
			checkDisconnectedJoysticks();
			checkConnectedJoysticks();
			joyCheckTimer_.start();
		}
	}

	void AndroidInputManager::checkDisconnectedJoysticks()
	{
		for (unsigned int i = 0; i < MaxNumJoysticks; i++) {
			const int deviceId = joystickStates_[i].deviceId_;
			if (deviceId > -1 && !isDeviceConnected(deviceId)) {
				LOGI("Gamepad %d \"%s\" (device %d) has been disconnected", i, joystickStates_[i].name_, deviceId);
				joystickStates_[i].deviceId_ = -1;

				if (inputEventHandler_ != nullptr && joystickStates_[i].guid_.isValid()) {
					joyConnectionEvent_.joyId = i;
					inputEventHandler_->OnJoyDisconnected(joyConnectionEvent_);
					joyMapping_.OnJoyDisconnected(joyConnectionEvent_);
				}
			}
		}
	}

	void AndroidInputManager::checkConnectedJoysticks()
	{
		// InputDevice.getDeviceIds() will not fill an array longer than MaxDevices
		const int MaxDevices = MaxNumJoysticks * 2;
		int deviceIds[MaxDevices];

		int connectedJoys = 0;
		for (unsigned int i = 0; i < MaxNumJoysticks; i++) {
			if (joystickStates_[i].deviceId_ > -1) {
				connectedJoys++;
			}
		}

		const int connectedDevices = AndroidJniClass_InputDevice::getDeviceIds(deviceIds, MaxDevices);
		for (int i = 0; i < MaxDevices && i < connectedDevices; i++) {
			AndroidJniClass_InputDevice inputDevice = AndroidJniClass_InputDevice::getDevice(deviceIds[i]);
			const int sources = inputDevice.getSources();

			if (((sources & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD) ||
				((sources & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK)) {
				const int joyId = findJoyId(deviceIds[i]);
				if (joyId > -1) {
					joystickStates_[joyId].deviceId_ = deviceIds[i];
				}

				connectedJoys++;
				if (connectedJoys >= int(MaxNumJoysticks)) {
					break;
				}
			}
		}
	}

	int AndroidInputManager::findJoyId(int deviceId)
	{
		int joyId = -1;

		for (unsigned int i = 0; i < MaxNumJoysticks; i++) {
			// Keeping track of the first unused joystick id, in case this is the first event from a new joystick
			if (joystickStates_[i].deviceId_ < 0 && joyId == -1) {
				joyId = i;
			} else if (joystickStates_[i].deviceId_ == deviceId) {
				// If the joystick is already known then the loop ends
				joyId = i;
				break;
			}
		}

		if (joyId > -1 && joystickStates_[joyId].deviceId_ != deviceId) {
			deviceInfo(deviceId, joyId);

			const uint8_t* g = joystickStates_[joyId].guid_.data;
			LOGI("Device %d \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] has been connected as gamepad %d - %d axes, %d buttons",
				deviceId, joystickStates_[joyId].name_, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], joyId, joystickStates_[joyId].numAxes_, joystickStates_[joyId].numButtons_);
			
			joystickStates_[joyId].deviceId_ = deviceId;

			if (inputEventHandler_ != nullptr && joystickStates_[joyId].guid_.isValid()) {
				joyConnectionEvent_.joyId = joyId;
				joyMapping_.OnJoyConnected(joyConnectionEvent_);
				inputEventHandler_->OnJoyConnected(joyConnectionEvent_);
			}
		}

		return joyId;
	}

	bool AndroidInputManager::isDeviceConnected(int deviceId)
	{
		AndroidJniClass_InputDevice inputDevice = AndroidJniClass_InputDevice::getDevice(deviceId);
		return !inputDevice.IsNull();
	}

	void AndroidInputManager::deviceInfo(int deviceId, int joyId)
	{
		constexpr int MaxStringLength = 256;
		char deviceInfoString[MaxStringLength];

		AndroidJniClass_InputDevice inputDevice = AndroidJniClass_InputDevice::getDevice(deviceId);
		if (!inputDevice.IsNull()) {
			auto& joyState = joystickStates_[joyId];

			inputDevice.getName(joyState.name_, AndroidJoystickState::MaxNameLength);
			if (StringView(joyState.name_) == "uinput-fpc"_s) {
				// Fingerprint Sensor is sometimes incorrectly recognized as joystick, disable it
				joyState.guid_ = JoystickGuidType::Unknown;
				joyState.numButtons_ = 0;
				joyState.numHats_ = 0;
				joyState.numAxes_ = 0;
				return;
			}

			const int vendorId = inputDevice.getVendorId();
			const int productId = inputDevice.getProductId();
			inputDevice.getDescriptor(deviceInfoString, MaxStringLength);
			joyState.guid_ = JoyMapping::CreateJoystickGuid(/*SDL_HARDWARE_BUS_BLUETOOTH*/0x05, vendorId, productId, 0, deviceInfoString, 0, 0);

			// Checking all AKEYCODE_BUTTON_* plus AKEYCODE_BACK
			constexpr int maxButtons = AndroidJoystickState::MaxButtons;
			int numFoundButtons = 0;
			bool checkedButtons[maxButtons];
			if (__ANDROID_API__ >= 19 && AndroidJniHelper::SdkVersion() >= 19) {
				int buttonsToCheck[maxButtons];
				for (int i = 0; i < maxButtons - 1; i++) {
					buttonsToCheck[i] = AKEYCODE_BUTTON_A + i;
				}
				// Back button is always the last one
				buttonsToCheck[maxButtons - 1] = AKEYCODE_BACK;

				inputDevice.hasKeys(buttonsToCheck, maxButtons, checkedButtons);
			}

			constexpr ButtonName ButtonNames[maxButtons] = {
				ButtonName::A, ButtonName::B, ButtonName::Unknown, ButtonName::X, ButtonName::Y, ButtonName::Unknown,
				ButtonName::LeftBumper, ButtonName::RightBumper, ButtonName::Unknown, ButtonName::Unknown, ButtonName::LeftStick,
				ButtonName::RightStick, ButtonName::Start, ButtonName::Back, ButtonName::Guide, ButtonName::Back
			};
			constexpr int ButtonMasks[maxButtons] = {
				(1 << 0), (1 << 1), (1 << 17), (1 << 2), (1 << 3), (1 << 18), (1 << 9), (1 << 10), (1 << 15),
				(1 << 16), (1 << 7), (1 << 8), (1 << 6), (1 << 4), (1 << 5)
			};

			int buttonMask = 0;
#if defined(DEATH_TRACE)
			std::memset(deviceInfoString, 0, MaxStringLength);
#endif
			for (int i = 0; i < maxButtons; i++) {
				bool hasKey = false;
				const int keyCode = (i < maxButtons - 1) ? AKEYCODE_BUTTON_A + i : AKEYCODE_BACK;

				if (__ANDROID_API__ >= 19 && AndroidJniHelper::SdkVersion() >= 19) {
					hasKey = checkedButtons[i];
				} else { // KeyCharacterMap.deviceHasKey()
					hasKey = AndroidJniClass_KeyCharacterMap::deviceHasKey(keyCode);
				}

				if (hasKey) {
					joyState.buttonsMapping_[i] = (int)ButtonNames[i];
#if defined(DEATH_TRACE)
					sprintf(&deviceInfoString[strlen(deviceInfoString)], " %d:%d", (int)ButtonNames[i], keyCode);
#endif
					buttonMask |= ButtonMasks[i];
					numFoundButtons++;
				} else {
					joyState.buttonsMapping_[i] = -1;
				}
			}
			joyState.numButtons_ = numFoundButtons;
#if defined(DEATH_TRACE)
			if (numFoundButtons == 0) {
				sprintf(&deviceInfoString[strlen(deviceInfoString)], " not detected");
			}
			LOGI("Device (%d, %d) - Buttons%s", deviceId, joyId, deviceInfoString);
#endif

			joyState.hasDPad_ = true;
			if (__ANDROID_API__ >= 19 && AndroidJniHelper::SdkVersion() >= 19) {
				int buttonsToCheck[4];
				for (int i = 0; i < countof(buttonsToCheck); i++) {
					buttonsToCheck[i] = AKEYCODE_DPAD_UP + i;
				}

				inputDevice.hasKeys(buttonsToCheck, countof(buttonsToCheck), checkedButtons);

				for (int i = 0; i < countof(buttonsToCheck); i++) {
					if (!checkedButtons[i]) {
						joyState.hasDPad_ = false;
						LOGI("Device (%d, %d) - D-Pad not detected", deviceId, joyId);
						break;
					}
				}
			} else {
				for (int button = AKEYCODE_DPAD_UP; button < AKEYCODE_DPAD_CENTER; button++) {
					const bool hasKey = AndroidJniClass_KeyCharacterMap::deviceHasKey(button);
					if (!hasKey) {
						joyState.hasDPad_ = false;
						LOGI("Device (%d, %d) - D-Pad not detected", deviceId, joyId);
						break;
					}
				}
			}

#if defined(DEATH_TRACE)
			std::memset(deviceInfoString, 0, MaxStringLength);
#endif
			joyState.hasHatAxes_ = true;

			int numAxes = 0;
			int numAxesMapped = 0;
			for (int i = 0; i < AndroidJoystickState::NumAxesToMap; i++) {
				const int axis = AndroidJoystickState::AxesToMap[i];
				AndroidJniClass_MotionRange motionRange = inputDevice.getMotionRange(axis);

				if (!motionRange.IsNull()) {
					const float minValue = motionRange.getMin();
					const float rangeValue = motionRange.getRange();
					
					if (numAxes < AndroidJoystickState::MaxAxes) {
						joyState.axesMapping_[numAxes] = axis;
						// Avoid a division by zero by only assigning valid range values
						if (rangeValue != 0.0f) {
							joyState.axesMinValues_[numAxes] = minValue;
							joyState.axesRangeValues_[numAxes] = rangeValue;
						} else {
							joyState.axesMinValues_[numAxes] = -1.0f;
							joyState.axesRangeValues_[numAxes] = 2.0f;
						}
					}
#if defined(DEATH_TRACE)
					sprintf(&deviceInfoString[strlen(deviceInfoString)], " %d:%d (%.2f to %.2f)", numAxes, axis, minValue, minValue + rangeValue);
#endif
					if (axis != AMOTION_EVENT_AXIS_HAT_X && axis != AMOTION_EVENT_AXIS_HAT_Y) {
						numAxesMapped++;
					}
					numAxes++;
				} else {
					if ((axis == AMOTION_EVENT_AXIS_HAT_X || axis == AMOTION_EVENT_AXIS_HAT_Y) && joyState.hasHatAxes_) {
						joyState.hasHatAxes_ = false;
						LOGI("Device (%d, %d) - Axis hats not detected", deviceId, joyId);
					}
				}
			}
#if defined(DEATH_TRACE)
			if (numAxes == 0) {
				sprintf(&deviceInfoString[strlen(deviceInfoString)], " not detected");
			}
			LOGI("Device (%d, %d) - Axes%s", deviceId, joyId, deviceInfoString);
#endif
			if (numAxes >= 4) {
				// Android sometimes returns strange range for the first two axes, all other axes are fine
				if (std::abs(joyState.axesMinValues_[0]) < 0.01f && joyState.axesRangeValues_[0] > 128.0f &&
					std::abs(joyState.axesMinValues_[1]) < 0.01f && joyState.axesRangeValues_[1] > 128.0f &&
					joyState.axesMinValues_[2] == -1.0f && joyState.axesRangeValues_[2] == 2.0f &&
					joyState.axesMinValues_[3] == -1.0f && joyState.axesRangeValues_[3] == 2.0f) {
					LOGW("Device (%d, %d) - Axis %d:%d reported strange range %.2f, using %.2f to %.2f instead", deviceId, joyId, 0, joyState.axesMapping_[0], joyState.axesRangeValues_[0], -1.0f, 1.0f);
					LOGW("Device (%d, %d) - Axis %d:%d reported strange range %.2f, using %.2f to %.2f instead", deviceId, joyId, 1, joyState.axesMapping_[1], joyState.axesRangeValues_[1], -1.0f, 1.0f);
					joyState.axesMinValues_[0] = -1.0f;
					joyState.axesRangeValues_[0] = 2.0f;
					joyState.axesMinValues_[1] = -1.0f;
					joyState.axesRangeValues_[1] = 2.0f;
				}
			}

			joyState.numAxes_ = numAxes;
			joyState.numAxesMapped_ = numAxesMapped;

			joyState.numHats_ = 0;
			if (joyState.hasDPad_ || joyState.hasHatAxes_) {
				joyState.numHats_ = 1; // No more than one hat is supported
			}

			// Update the GUID with capability bits
			{
				uint16_t axisMask = 0;
				if (numAxes >= 2) {
					axisMask |= 0x01 | 0x02;
				}
				if (numAxes >= 4) {
					axisMask |= 0x04 | 0x08;
				}
				if (numAxes >= 6) {
					axisMask |= 0x10 | 0x20;
				}
				if (joyState.hasDPad_ || joyState.hasHatAxes_) {
					buttonMask |= 0x800 | 0x1000 | 0x2000 | 0x4000;
				}

				joyState.guid_.data[12] = buttonMask & 0xff;
				joyState.guid_.data[13] = (buttonMask >> 8) & 0xff;
				joyState.guid_.data[14] = axisMask & 0xff;
				joyState.guid_.data[15] = (axisMask >> 8) & 0xff;
			}
		}
	}
}
