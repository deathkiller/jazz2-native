#include "AndroidInputManager.h"
#include "../../Input/IInputEventHandler.h"
#include "AndroidJniHelper.h"
#include "AndroidApplication.h"
#include "../../Base/Timer.h"
#include "../../Input/JoyMapping.h"

#include <android/input.h>
#include <android/sensor.h>
#include <cstring> // for memset()

#include <Utf8.h>
#include <Containers/StringUtils.h>

using namespace Death;
using namespace Death::Containers::Literals;

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 4;
}

namespace nCine::Backends
{
	ASensorManager* AndroidInputManager::_sensorManager = nullptr;
	const ASensor* AndroidInputManager::_accelerometerSensor = nullptr;
	ASensorEventQueue* AndroidInputManager::_sensorEventQueue = nullptr;
	bool AndroidInputManager::_accelerometerEnabled = false;
	AccelerometerEvent AndroidInputManager::_accelerometerEvent;
	TouchEvent AndroidInputManager::_touchEvent;
	AndroidKeyboardState AndroidInputManager::_keyboardState;
	KeyboardEvent AndroidInputManager::_keyboardEvent;
	TextInputEvent AndroidInputManager::_textInputEvent;
	AndroidMouseState AndroidInputManager::_mouseState;
	MouseEvent AndroidInputManager::_mouseEvent;
	ScrollEvent AndroidInputManager::_scrollEvent;
	int AndroidInputManager::_simulatedMouseButtonState = 0;

	AndroidJoystickState AndroidInputManager::_nullJoystickState;
	AndroidJoystickState AndroidInputManager::_joystickStates[MaxNumJoysticks];
	JoyButtonEvent AndroidInputManager::_joyButtonEvent;
	JoyHatEvent AndroidInputManager::_joyHatEvent;
	JoyAxisEvent AndroidInputManager::_joyAxisEvent;
	JoyConnectionEvent AndroidInputManager::_joyConnectionEvent;
	Timer AndroidInputManager::_joyCheckTimer;

	// TODO: Implement new axis order - https://github.com/libsdl-org/SDL/commit/de3909a190f6e1a3f11776ce42927f99b0381675
	const int AndroidJoystickState::AxesToMap[AndroidJoystickState::NumAxesToMap] = {
		AMOTION_EVENT_AXIS_X, AMOTION_EVENT_AXIS_Y, AMOTION_EVENT_AXIS_Z,
		AMOTION_EVENT_AXIS_RX, AMOTION_EVENT_AXIS_RY, AMOTION_EVENT_AXIS_RZ,
		AMOTION_EVENT_AXIS_LTRIGGER, AMOTION_EVENT_AXIS_RTRIGGER,
		AMOTION_EVENT_AXIS_BRAKE, AMOTION_EVENT_AXIS_GAS,
		AMOTION_EVENT_AXIS_HAT_X, AMOTION_EVENT_AXIS_HAT_Y
	};

	namespace
	{
		MouseButton androidToNcineMouseButton(int button)
		{
			if (button == AMOTION_EVENT_BUTTON_PRIMARY)
				return MouseButton::Left;
			else if (button == AMOTION_EVENT_BUTTON_SECONDARY)
				return MouseButton::Right;
			else if (button == AMOTION_EVENT_BUTTON_TERTIARY)
				return MouseButton::Middle;
			else if (button == AMOTION_EVENT_BUTTON_BACK)
				return MouseButton::Fourth;
			else if (button == AMOTION_EVENT_BUTTON_FORWARD)
				return MouseButton::Fifth;
			else
				return MouseButton::Left;
		}

		bool checkMouseButton(int buttonState, MouseButton button)
		{
			switch (button) {
				case MouseButton::Left: return ((buttonState & AMOTION_EVENT_BUTTON_PRIMARY) != 0);
				case MouseButton::Right: return ((buttonState & AMOTION_EVENT_BUTTON_SECONDARY) != 0);
				case MouseButton::Middle: return ((buttonState & AMOTION_EVENT_BUTTON_TERTIARY) != 0);
				case MouseButton::Fourth: return ((buttonState & AMOTION_EVENT_BUTTON_BACK) != 0);
				case MouseButton::Fifth: return ((buttonState & AMOTION_EVENT_BUTTON_FORWARD) != 0);
				default: return false;
			}
		}
	}

	AndroidJoystickState::AndroidJoystickState()
		: _deviceId(-1), _numButtons(0), _numAxes(0), _numAxesMapped(0),
			_hasDPad(false), _hasHatAxes(false), _hatState(HatState::Centered)
	{
		_name[0] = '\0';
		for (int i = 0; i < MaxButtons; i++) {
			_buttonsMapping[i] = 0;
			_buttons[i] = false;
		}
		for (int i = 0; i < MaxAxes; i++) {
			_axesMapping[i] = 0;
			_axesMinValues[i] = -1.0f;
			_axesRangeValues[i] = 2.0f;
			_axesValues[i] = 0.0f;
		}
		for (int i = 0; i < MaxVibrators; i++) {
			_vibratorsIds[i] = 0;
		}
	}

	AndroidInputManager::AndroidInputManager(struct android_app* state)
	{
		initAccelerometerSensor(state);
		_joyMapping.Init(this);
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

	AndroidMouseState::AndroidMouseState()
		: _buttonState(0)
	{
	}

	bool AndroidMouseState::isButtonDown(MouseButton button) const
	{
		return checkMouseButton(_buttonState, button);
	}

	bool AndroidJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < _numButtons && _buttons[buttonId]);
	}

	unsigned char AndroidJoystickState::hatState(int hatId) const
	{
		return (hatId >= 0 && hatId < _numHats ? _hatState : HatState::Centered);
	}

	float AndroidJoystickState::axisValue(int axisId) const
	{
		// The value has already been remapped from min..max to -1.0f..1.0f
		return (axisId >= 0 && axisId < _numAxesMapped ? _axesValues[axisId] : 0.0f);
	}

	/** @brief Enables the accelerometer sensor; called by `enableAccelerometer()` and when the application gains focus */
	void AndroidInputManager::enableAccelerometerSensor()
	{
		if (_accelerometerEnabled && _accelerometerSensor != nullptr) {
			ASensorEventQueue_enableSensor(_sensorEventQueue, _accelerometerSensor);
			// 60 events per second
			ASensorEventQueue_setEventRate(_sensorEventQueue, _accelerometerSensor, (1000L / 60) * 1000);
		}
	}

	/** @brief Disables the accelerometer sensor; called by `enableAccelerometer()` and when the application loses focus */
	void AndroidInputManager::disableAccelerometerSensor()
	{
		if (_accelerometerEnabled && _accelerometerSensor != nullptr) {
			ASensorEventQueue_disableSensor(_sensorEventQueue, _accelerometerSensor);
		}
	}

	/** @brief Activates the sensor and raises the flag needed for application focus handling */
	void AndroidInputManager::enableAccelerometer(bool enabled)
	{
		if (enabled) {
			enableAccelerometerSensor();
		} else {
			disableAccelerometerSensor();
		}
		_accelerometerEnabled = enabled;
	}

	void AndroidInputManager::parseAccelerometerEvent()
	{
		if (_inputEventHandler != nullptr && _accelerometerEnabled && _accelerometerSensor != nullptr) {
			ASensorEvent event;
			while (ASensorEventQueue_getEvents(_sensorEventQueue, &event, 1) > 0) {
				_accelerometerEvent.x = event.acceleration.x;
				_accelerometerEvent.y = event.acceleration.y;
				_accelerometerEvent.z = event.acceleration.z;
				_inputEventHandler->OnAcceleration(_accelerometerEvent);
			}
		}
	}

	bool AndroidInputManager::parseEvent(const AInputEvent* event)
	{
		// Early out if there is no input event handler
		if (_inputEventHandler == nullptr) {
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
		DEATH_ASSERT(joyId >= 0);
		return (joyId < MaxNumJoysticks && _joystickStates[joyId]._deviceId != -1);
	}

	const char* AndroidInputManager::joyName(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return _joystickStates[joyId]._name;
		} else {
			return nullptr;
		}
	}

	const JoystickGuid AndroidInputManager::joyGuid(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return _joystickStates[joyId]._guid;
		} else {
			return JoystickGuidType::Unknown;
		}
	}

	int AndroidInputManager::joyNumButtons(int joyId) const
	{
		return (isJoyPresent(joyId) ? _joystickStates[joyId]._numButtons : -1);
	}

	int AndroidInputManager::joyNumHats(int joyId) const
	{
		return (isJoyPresent(joyId) ? _joystickStates[joyId]._numHats : -1);
	}

	int AndroidInputManager::joyNumAxes(int joyId) const
	{
		return (isJoyPresent(joyId) ? _joystickStates[joyId]._numAxesMapped : -1);
	}

	const JoystickState& AndroidInputManager::joystickState(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return _joystickStates[joyId];
		} else {
			return _nullJoystickState;
		}
	}
	
	bool AndroidInputManager::joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs)
	{
		if (isJoyPresent(joyId)) {
			if (_joystickStates[joyId]._numVibrators > 0) {
				const unsigned char amplitude = static_cast<unsigned char>(std::clamp(lowFreqIntensity, 0.0f, 1.0f) * 255);

				_joystickStates[joyId]._vibrators[0].cancel();
				// `amplitude` must either be `DEFAULT_AMPLITUDE`, or between 1 and 255 inclusive
				if (amplitude > 0) {
					const AndroidJniClass_VibrationEffect vibration = AndroidJniClass_VibrationEffect::createOneShot(durationMs, amplitude);
					_joystickStates[joyId]._vibrators[0].vibrate(vibration);
				}
			}
			if (_joystickStates[joyId]._numVibrators > 1) {
				// Clamp intensity between 0.0f and 1.0f
				const unsigned char amplitude = static_cast<unsigned char>(std::clamp(highFreqIntensity, 0.0f, 1.0f) * 255);

				_joystickStates[joyId]._vibrators[1].cancel();
				// `amplitude` must either be `DEFAULT_AMPLITUDE`, or between 1 and 255 inclusive
				if (amplitude > 0) {
					const AndroidJniClass_VibrationEffect vibration = AndroidJniClass_VibrationEffect::createOneShot(durationMs, amplitude);
					_joystickStates[joyId]._vibrators[1].vibrate(vibration);
				}
			}
			return true;
		}

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
		if (joyId > -1 && _joystickStates[joyId]._guid.isValid()) {
			switch (AInputEvent_getType(event)) {
				case AINPUT_EVENT_TYPE_KEY: {
					const int keyCode = AKeyEvent_getKeyCode(event);
					int buttonIndex = -1;
					if (keyCode >= AKEYCODE_BUTTON_A && keyCode < AKEYCODE_ESCAPE) {
						buttonIndex = _joystickStates[joyId]._buttonsMapping[keyCode - AKEYCODE_BUTTON_A];
					} else if (keyCode == AKEYCODE_BACK) {
						// Back button is always the last one
						const unsigned int lastIndex = AndroidJoystickState::MaxButtons - 1;
						buttonIndex = _joystickStates[joyId]._buttonsMapping[lastIndex];
					}

					if (buttonIndex != -1) {
						_joyButtonEvent.joyId = joyId;
						_joyButtonEvent.buttonId = buttonIndex;
						switch (AKeyEvent_getAction(event)) {
							case AKEY_EVENT_ACTION_DOWN:
								_joystickStates[joyId]._buttons[buttonIndex] = true;
								_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
								_inputEventHandler->OnJoyButtonPressed(_joyButtonEvent);
								break;
							case AKEY_EVENT_ACTION_UP:
								_joystickStates[joyId]._buttons[buttonIndex] = false;
								_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
								_inputEventHandler->OnJoyButtonReleased(_joyButtonEvent);
								break;
							case AKEY_EVENT_ACTION_MULTIPLE:
								break;
						}
					}

					if (keyCode >= AKEYCODE_DPAD_UP && keyCode < AKEYCODE_DPAD_CENTER) {
						_joyHatEvent.joyId = joyId;
						_joyHatEvent.hatId = 0; // No more than one hat is supported

						unsigned char hatState = _joystickStates[joyId]._hatState;
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

						if (_joystickStates[joyId]._hatState != hatState) {
							_joystickStates[joyId]._hatState = hatState;
							_joyHatEvent.hatState = _joystickStates[joyId]._hatState;

							_joyMapping.OnJoyHatMoved(_joyHatEvent);
							_inputEventHandler->OnJoyHatMoved(_joyHatEvent);
						}
					}
					break;
				}
				case AINPUT_EVENT_TYPE_MOTION: {
					_joyAxisEvent.joyId = joyId;

					auto& joyState = _joystickStates[joyId];
					unsigned char hatState = 0;
					for (int i = 0; i < joyState._numAxes; i++) {
						const int axis = joyState._axesMapping[i];
						const float axisRawValue = AMotionEvent_getAxisValue(event, axis, 0);
						const float axisValue = -1.0f + 2.0f * (axisRawValue - joyState._axesMinValues[i]) / joyState._axesRangeValues[i];

						if (axis == AMOTION_EVENT_AXIS_HAT_X || axis == AMOTION_EVENT_AXIS_HAT_Y) {
							_joyHatEvent.joyId = joyId;
							_joyHatEvent.hatId = 0; // No more than one hat is supported

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
							joyState._axesValues[i] = axisValue;
							
							_joyAxisEvent.axisId = i;
							_joyAxisEvent.value = axisValue;
							_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
							_inputEventHandler->OnJoyAxisMoved(_joyAxisEvent);
						}
					}

					if (joyState._hatState != hatState) {
						joyState._hatState = hatState;
						_joyHatEvent.hatState = joyState._hatState;
						_joyMapping.OnJoyHatMoved(_joyHatEvent);
						_inputEventHandler->OnJoyHatMoved(_joyHatEvent);
					}
					break;
				}
			}
		} else {
			LOGW("No available joystick ID for device {}, dropping button event", deviceId);
		}

		return true;
	}

	bool AndroidInputManager::processKeyboardEvent(const AInputEvent* event)
	{
		const int keyCode = AKeyEvent_getKeyCode(event);

		// Hardware volume keys are not handled by the engine
		if (keyCode == AKEYCODE_VOLUME_UP || keyCode == AKEYCODE_VOLUME_DOWN || keyCode == AKEYCODE_POWER) {
			return false;
		}

		// Native activities receive key events before the input method does, so the Back key has to be left
		// alone while the screen keyboard is shown, otherwise there would be no way to dismiss it
		if (keyCode == AKEYCODE_BACK && theApplication().IsScreenKeyboardVisible()) {
			return false;
		}

		int metaState = AKeyEvent_getMetaState(event);

		_keyboardEvent.scancode = AKeyEvent_getScanCode(event);
		_keyboardEvent.sym = AndroidKeys::keySymValueToEnum(keyCode);
		_keyboardEvent.mod = AndroidKeys::keyModMaskToEnumMask(metaState);

		const unsigned int keySym = static_cast<unsigned int>(_keyboardEvent.sym);
		const int action = AKeyEvent_getAction(event);
		switch (action) {
			case AKEY_EVENT_ACTION_DOWN:
				if (_keyboardEvent.sym != Keys::Unknown) {
					_keyboardState._keys[keySym] = 1;
				}
				_inputEventHandler->OnKeyPressed(_keyboardEvent);

				if ((metaState & AMETA_CTRL_ON) == 0) {
					AndroidJniClass_KeyEvent keyEvent(AInputEvent_getType(event), keyCode);
					if (keyEvent.isPrintingKey() || keyCode == AKEYCODE_SPACE) {
						const int unicodeKey = keyEvent.getUnicodeChar(metaState);
						_textInputEvent.length = Utf8::FromCodePoint(unicodeKey, _textInputEvent.text);
						if (_textInputEvent.length > 0) {
							_inputEventHandler->OnTextInput(_textInputEvent);
						}
					}
				}
				break;
			case AKEY_EVENT_ACTION_UP:
				if (_keyboardEvent.sym != Keys::Unknown) {
					_keyboardState._keys[keySym] = 0;
				}
				_inputEventHandler->OnKeyReleased(_keyboardEvent);
				break;
			case AKEY_EVENT_ACTION_MULTIPLE:
				// AKEY_EVENT_ACTION_MULTIPLE should be deprecated, but it seems it's still used even on Android 13.
				// It can also carry a string of characters, but software keyboards commit their text through
				// an input connection instead, which arrives in `injectTextInput()`.
				if (_keyboardEvent.sym != Keys::Unknown) {
					_inputEventHandler->OnKeyPressed(_keyboardEvent);
				}
				break;
		}

		return true;
	}

	void AndroidInputManager::injectTextInput(char32_t codePoint)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		_textInputEvent.length = static_cast<std::int32_t>(Utf8::FromCodePoint(codePoint, _textInputEvent.text));
		if (_textInputEvent.length > 0) {
			_inputEventHandler->OnTextInput(_textInputEvent);
		}
	}

	void AndroidInputManager::injectKeyEvent(bool pressed, std::int32_t keyCode)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		_keyboardEvent.scancode = 0;
		_keyboardEvent.sym = AndroidKeys::keySymValueToEnum(keyCode);
		_keyboardEvent.mod = 0;

		if (_keyboardEvent.sym == Keys::Unknown) {
			return;
		}

		const unsigned int keySym = static_cast<unsigned int>(_keyboardEvent.sym);
		if (pressed) {
			_keyboardState._keys[keySym] = 1;
			_inputEventHandler->OnKeyPressed(_keyboardEvent);
		} else {
			_keyboardState._keys[keySym] = 0;
			_inputEventHandler->OnKeyReleased(_keyboardEvent);
		}
	}

	bool AndroidInputManager::processTouchEvent(const AInputEvent* event)
	{
		const int action = AMotionEvent_getAction(event);
		
		Vector2i res = theApplication().GetResolution();
		float w = static_cast<float>(res.X);
		float h = static_cast<float>(res.Y);

		_touchEvent.count = AMotionEvent_getPointerCount(event);
		_touchEvent.actionIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
		for (unsigned int i = 0; i < _touchEvent.count && i < TouchEvent::MaxPointers; i++) {
			TouchEvent::Pointer& pointer = _touchEvent.pointers[i];
			pointer.id = AMotionEvent_getPointerId(event, i);
			pointer.x = AMotionEvent_getX(event, i) / w;
			pointer.y = AMotionEvent_getY(event, i) / h;
			pointer.pressure = AMotionEvent_getPressure(event, i);
		}

		switch (action & AMOTION_EVENT_ACTION_MASK) {
			case AMOTION_EVENT_ACTION_DOWN:
				_touchEvent.type = TouchEventType::Down;
				break;
			case AMOTION_EVENT_ACTION_UP:
				_touchEvent.type = TouchEventType::Up;
				break;
			case AMOTION_EVENT_ACTION_MOVE:
				_touchEvent.type = TouchEventType::Move;
				break;
			case AMOTION_EVENT_ACTION_POINTER_DOWN:
				_touchEvent.type = TouchEventType::PointerDown;
				break;
			case AMOTION_EVENT_ACTION_POINTER_UP:
				_touchEvent.type = TouchEventType::PointerUp;
				break;
		}

		_inputEventHandler->OnTouchEvent(_touchEvent);
		return true;
	}

	bool AndroidInputManager::processMouseEvent(const AInputEvent* event)
	{
		const int action = AMotionEvent_getAction(event);
		int buttonState = 0;

		_mouseEvent.x = static_cast<int>(AMotionEvent_getX(event, 0));
		_mouseEvent.y = static_cast<int>(theApplication().GetHeight() - AMotionEvent_getY(event, 0));
		_mouseState.x = _mouseEvent.x;
		_mouseState.y = _mouseEvent.y;

		// Mask out back and forward buttons in the detected state
		// as those are simulated as right and middle buttons
		int maskOutButtons = 0;
		if (_simulatedMouseButtonState & AMOTION_EVENT_BUTTON_SECONDARY) {
			maskOutButtons |= AMOTION_EVENT_BUTTON_BACK;
		}
		if (_simulatedMouseButtonState & AMOTION_EVENT_BUTTON_TERTIARY) {
			maskOutButtons |= AMOTION_EVENT_BUTTON_FORWARD;
		}

		switch (action) {
			case AMOTION_EVENT_ACTION_DOWN:
				buttonState = AMotionEvent_getButtonState(event);
				buttonState &= ~maskOutButtons;
				buttonState |= _simulatedMouseButtonState;

				_mouseEvent.button = androidToNcineMouseButton(_mouseState._buttonState ^ buttonState); // pressed button mask
				_mouseState._buttonState = buttonState;
				_inputEventHandler->OnMouseDown(_mouseEvent);
				break;
			case AMOTION_EVENT_ACTION_UP:
				buttonState = AMotionEvent_getButtonState(event);
				buttonState &= ~maskOutButtons;
				buttonState |= _simulatedMouseButtonState;

				_mouseEvent.button = androidToNcineMouseButton(_mouseState._buttonState ^ buttonState); // released button mask
				_mouseState._buttonState = buttonState;
				_inputEventHandler->OnMouseUp(_mouseEvent);
				break;
			case AMOTION_EVENT_ACTION_MOVE:
			case AMOTION_EVENT_ACTION_HOVER_MOVE:
				_inputEventHandler->OnMouseMove(_mouseState);
				break;
		}

		_scrollEvent.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0);
		_scrollEvent.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
		if (fabsf(_scrollEvent.x) > 0.0f || fabsf(_scrollEvent.y) > 0.0f) {
			_inputEventHandler->OnMouseWheel(_scrollEvent);
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
				_simulatedMouseButtonState |= simulatedButton;
				_mouseEvent.button = androidToNcineMouseButton(simulatedButton);
				_mouseState._buttonState |= simulatedButton;
				_inputEventHandler->OnMouseDown(_mouseEvent);
			} else if (action == AKEY_EVENT_ACTION_UP && oldAction == AKEY_EVENT_ACTION_DOWN) {
				oldAction = action;
				_simulatedMouseButtonState &= ~simulatedButton;
				_mouseEvent.button = androidToNcineMouseButton(simulatedButton);
				_mouseState._buttonState &= ~simulatedButton;
				_inputEventHandler->OnMouseUp(_mouseEvent);
			}
		}

		return true;
	}

	void AndroidInputManager::initAccelerometerSensor(android_app* state)
	{
		// Prepare to monitor accelerometer
#if __ANDROID_API__ >= 26
		AndroidApplication& application = static_cast<AndroidApplication&>(theApplication());
		_sensorManager = ASensorManager_getInstanceForPackage(application.packageName());
#else
		_sensorManager = ASensorManager_getInstance();
#endif
		_accelerometerSensor = ASensorManager_getDefaultSensor(_sensorManager, ASENSOR_TYPE_ACCELEROMETER);
		_sensorEventQueue = ASensorManager_createEventQueue(_sensorManager, state->looper, LOOPER_ID_USER, nullptr, nullptr);

		if (_accelerometerSensor == nullptr) {
			LOGW("No accelerometer sensor available");
		}
	}

	void AndroidInputManager::updateJoystickConnections()
	{
		if (_joyCheckTimer.interval() >= JoyCheckRateSecs) {
			checkDisconnectedJoysticks();
			checkConnectedJoysticks();
			_joyCheckTimer.start();
		}
	}

	void AndroidInputManager::checkDisconnectedJoysticks()
	{
		for (unsigned int i = 0; i < MaxNumJoysticks; i++) {
			const int deviceId = _joystickStates[i]._deviceId;
			if (deviceId > -1 && !isDeviceConnected(deviceId)) {
				LOGI("Gamepad {} \"{}\" (device {}) has been disconnected", i, _joystickStates[i]._name, deviceId);
				_joystickStates[i]._deviceId = -1;

				if (_inputEventHandler != nullptr && _joystickStates[i]._guid.isValid()) {
					_joyConnectionEvent.joyId = i;
					_inputEventHandler->OnJoyDisconnected(_joyConnectionEvent);
					_joyMapping.OnJoyDisconnected(_joyConnectionEvent);
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
			if (_joystickStates[i]._deviceId > -1) {
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
					_joystickStates[joyId]._deviceId = deviceIds[i];
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
			if (_joystickStates[i]._deviceId < 0 && joyId == -1) {
				joyId = i;
			} else if (_joystickStates[i]._deviceId == deviceId) {
				// If the joystick is already known then the loop ends
				joyId = i;
				break;
			}
		}

		if (joyId > -1 && _joystickStates[joyId]._deviceId != deviceId) {
			deviceInfo(deviceId, joyId);

			const uint8_t* g = _joystickStates[joyId]._guid.data;
			LOGI("Device {} \"{}\" [{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}] has been connected as gamepad {} - {} axes, {} buttons, {} vibs",
				deviceId, _joystickStates[joyId]._name, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15],
				joyId, _joystickStates[joyId]._numAxes, _joystickStates[joyId]._numButtons, _joystickStates[joyId]._numVibrators);
			
			_joystickStates[joyId]._deviceId = deviceId;

			if (_inputEventHandler != nullptr && _joystickStates[joyId]._guid.isValid()) {
				_joyConnectionEvent.joyId = joyId;
				_joyMapping.OnJoyConnected(_joyConnectionEvent);
				_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
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
			auto& joyState = _joystickStates[joyId];

			inputDevice.getName(joyState._name, AndroidJoystickState::MaxNameLength);
			auto nameLower = StringUtils::lowercase(StringView(joyState._name));
			// Internal Android TV devices, NVIDIA Shield devices, WSA devices are not valid controllers
			if (nameLower == "virtual-remote"_s || nameLower == "virtual-search"_s || nameLower == "virtual_keyboard"_s ||
				nameLower == "shield-ask-remote"_s ||
				nameLower == "uinput-fpc"_s /* Fingerprint Sensor */ ||
				nameLower == "tpv_smtrc"_s || nameLower == "tpv_multirc"_s || nameLower == "tpv_mutilrc"_s /* TP Vision (Philips TV) Smart Remote */) {
				// Marking as invalid controller
				joyState._guid = JoystickGuidType::Unknown;
				joyState._numButtons = 0;
				joyState._numHats = 0;
				joyState._numAxes = 0;
				LOGI("Device ({}, {}) - Invalid controller", deviceId, joyId);
				return;
			}

			const int vendorId = inputDevice.getVendorId();
			const int productId = inputDevice.getProductId();
			inputDevice.getDescriptor(deviceInfoString, MaxStringLength);
			joyState._guid = JoyMapping::CreateJoystickGuid(/*SDL_HARDWARE_BUS_BLUETOOTH*/0x05, vendorId, productId, 0, deviceInfoString, 0, 0);

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
			deviceInfoString[0] = '\0';
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
					joyState._buttonsMapping[i] = (int)ButtonNames[i];
#if defined(DEATH_TRACE)
					sprintf(&deviceInfoString[strlen(deviceInfoString)], " %d:%d", (int)ButtonNames[i], keyCode);
#endif
					buttonMask |= ButtonMasks[i];
					numFoundButtons++;
				} else {
					joyState._buttonsMapping[i] = -1;
				}
			}
			joyState._numButtons = numFoundButtons;
#if defined(DEATH_TRACE)
			if (numFoundButtons == 0) {
				sprintf(&deviceInfoString[strlen(deviceInfoString)], " not detected");
			}
			LOGI("Device ({}, {}) - Buttons{}", deviceId, joyId, deviceInfoString);
#endif

			joyState._hasDPad = true;
			if (__ANDROID_API__ >= 19 && AndroidJniHelper::SdkVersion() >= 19) {
				int buttonsToCheck[4];
				for (int i = 0; i < arraySize(buttonsToCheck); i++) {
					buttonsToCheck[i] = AKEYCODE_DPAD_UP + i;
				}

				inputDevice.hasKeys(buttonsToCheck, arraySize(buttonsToCheck), checkedButtons);

				for (int i = 0; i < arraySize(buttonsToCheck); i++) {
					if (!checkedButtons[i]) {
						joyState._hasDPad = false;
						LOGI("Device ({}, {}) - D-Pad not detected", deviceId, joyId);
						break;
					}
				}
			} else {
				for (int button = AKEYCODE_DPAD_UP; button < AKEYCODE_DPAD_CENTER; button++) {
					const bool hasKey = AndroidJniClass_KeyCharacterMap::deviceHasKey(button);
					if (!hasKey) {
						joyState._hasDPad = false;
						LOGI("Device ({}, {}) - D-Pad not detected", deviceId, joyId);
						break;
					}
				}
			}

#if defined(DEATH_TRACE)
			deviceInfoString[0] = '\0';
#endif
			joyState._hasHatAxes = true;

			int numAxes = 0;
			int numAxesMapped = 0;
			for (int i = 0; i < AndroidJoystickState::NumAxesToMap; i++) {
				const int axis = AndroidJoystickState::AxesToMap[i];
				AndroidJniClass_MotionRange motionRange = inputDevice.getMotionRange(axis);

				if (!motionRange.IsNull()) {
					const float minValue = motionRange.getMin();
					const float rangeValue = motionRange.getRange();
					
					if (numAxes < AndroidJoystickState::MaxAxes) {
						joyState._axesMapping[numAxes] = axis;
						// Avoid a division by zero by only assigning valid range values
						if (rangeValue != 0.0f) {
							joyState._axesMinValues[numAxes] = minValue;
							joyState._axesRangeValues[numAxes] = rangeValue;
						} else {
							joyState._axesMinValues[numAxes] = -1.0f;
							joyState._axesRangeValues[numAxes] = 2.0f;
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
					if ((axis == AMOTION_EVENT_AXIS_HAT_X || axis == AMOTION_EVENT_AXIS_HAT_Y) && joyState._hasHatAxes) {
						joyState._hasHatAxes = false;
						LOGI("Device ({}, {}) - Axis hats not detected", deviceId, joyId);
					}
				}
			}
#if defined(DEATH_TRACE)
			if (numAxes == 0) {
				sprintf(&deviceInfoString[strlen(deviceInfoString)], " not detected");
			}
			LOGI("Device ({}, {}) - Axes{}", deviceId, joyId, deviceInfoString);
#endif
			if (numAxes >= 4) {
				// Android sometimes returns strange range for the first two axes, all other axes are fine
				if (std::abs(joyState._axesMinValues[0]) < 0.01f && joyState._axesRangeValues[0] > 128.0f &&
					std::abs(joyState._axesMinValues[1]) < 0.01f && joyState._axesRangeValues[1] > 128.0f &&
					joyState._axesMinValues[2] == -1.0f && joyState._axesRangeValues[2] == 2.0f &&
					joyState._axesMinValues[3] == -1.0f && joyState._axesRangeValues[3] == 2.0f) {
					LOGW("Device ({}, {}) - Axis {}:{} reported strange range {:.2f}, using {:.2f} to {:.2f} instead", deviceId, joyId, 0, joyState._axesMapping[0], joyState._axesRangeValues[0], -1.0f, 1.0f);
					LOGW("Device ({}, {}) - Axis {}:{} reported strange range {:.2f}, using {:.2f} to {:.2f} instead", deviceId, joyId, 1, joyState._axesMapping[1], joyState._axesRangeValues[1], -1.0f, 1.0f);
					joyState._axesMinValues[0] = -1.0f;
					joyState._axesRangeValues[0] = 2.0f;
					joyState._axesMinValues[1] = -1.0f;
					joyState._axesRangeValues[1] = 2.0f;
				}
			}

			joyState._numAxes = numAxes;
			joyState._numAxesMapped = numAxesMapped;

			joyState._numHats = 0;
			if (joyState._hasDPad || joyState._hasHatAxes) {
				joyState._numHats = 1; // No more than one hat is supported
			}

			if (AndroidJniHelper::SdkVersion() >= 31) {
#if defined(DEATH_TRACE)
				deviceInfoString[0] = '\0';
#endif
				AndroidJniClass_VibratorManager vibratorManager = inputDevice.getVibratorManager();
				// There might be more vibrators available than the maximum number supported
				joyState._numVibrators = vibratorManager.getVibratorIds(joyState._vibratorsIds, AndroidJoystickState::MaxVibrators);

				if (joyState._numVibrators == 0) {
#if defined(DEATH_TRACE)
					sprintf(&deviceInfoString[strlen(deviceInfoString)], " not detected");
#endif
				} else {
					for (int i = 0; i < AndroidJoystickState::MaxVibrators; i++) {
						joyState._vibrators[i] = vibratorManager.getVibrator(joyState._vibratorsIds[i]);
#if defined(DEATH_TRACE)
						sprintf(&deviceInfoString[strlen(deviceInfoString)], " %d", joyState._vibratorsIds[i]);
#endif
					}
				}
#if defined(DEATH_TRACE)
				LOGI("Device ({}, {}) - Vibs{} ({})", deviceId, joyId, deviceInfoString, joyState._numVibrators);
#endif
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
				if (joyState._hasDPad || joyState._hasHatAxes) {
					buttonMask |= 0x800 | 0x1000 | 0x2000 | 0x4000;
				}

				joyState._guid.data[12] = buttonMask & 0xff;
				joyState._guid.data[13] = (buttonMask >> 8) & 0xff;
				joyState._guid.data[14] = axisMask & 0xff;
				joyState._guid.data[15] = (axisMask >> 8) & 0xff;
			}
		}
	}
}
