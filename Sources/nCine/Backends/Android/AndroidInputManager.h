#pragma once

#include "../../Input/IInputManager.h"
#include "AndroidJniHelper.h"

#include <android_native_app_glue.h>
#include <android/keycodes.h>

struct AInputEvent;
struct ASensorManager;
struct ASensor;
struct ASensorEventQueue;

namespace nCine
{
	class AndroidApplication;
	class Timer;
}

namespace nCine::Backends
{
	/**
		@brief Utility functions to convert between engine key enumerations and Android ones
	*/
	class AndroidKeys
	{
	public:
		static Keys keySymValueToEnum(int keysym);
		static int keyModMaskToEnumMask(int keymod);
	};

	/**
		@brief Simulated information about Android keyboard state
		
		Android does not expose a global keyboard state, so the pressed-key table is reconstructed
		from the parsed key events instead of being queried from the system.
	*/
	class AndroidKeyboardState : public KeyboardState
	{
	public:
		AndroidKeyboardState()
		{
			for (unsigned int i = 0; i < NumKeys; i++) {
				_keys[i] = 0;
			}
		}

		inline bool isKeyDown(Keys key) const override
		{
			return (key != Keys::Unknown && _keys[static_cast<unsigned int>(key)] != 0);
		}

	private:
		static const unsigned int NumKeys = static_cast<unsigned int>(Keys::Count);
		unsigned char _keys[NumKeys];

		friend class AndroidInputManager;
	};

	/**
		@brief Information about Android mouse state
	*/
	class AndroidMouseState : public MouseState
	{
	public:
		AndroidMouseState();

		bool isButtonDown(MouseButton button) const override;

	private:
		int _buttonState;

		friend class AndroidInputManager;
	};

	/**
		@brief Information about Android joystick state
	*/
	class AndroidJoystickState : JoystickState
	{
	public:
		/** @brief Maximum number of vibrators, supporting no more than a left and a right one */
		static const int MaxVibrators = 2;

		AndroidJoystickState();

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

	private:
		static constexpr unsigned int MaxNameLength = 256;
		/** @brief Maximum number of buttons, covering all `AKEYCODE_BUTTON_*` plus `AKEYCODE_BACK` */
		static constexpr int MaxButtons = AKEYCODE_ESCAPE - AKEYCODE_BUTTON_A + 1;
		static constexpr int MaxAxes = 10;
		static constexpr int NumAxesToMap = 12;
		static const int AxesToMap[NumAxesToMap];

		int _deviceId;
		JoystickGuid _guid;
		char _name[MaxNameLength];

		int _numButtons;
		int _numHats;
		int _numAxes;
		int _numAxesMapped;
		bool _hasDPad;
		bool _hasHatAxes;
		short int _buttonsMapping[MaxButtons];
		short int _axesMapping[MaxAxes];
		bool _buttons[MaxButtons];
		/** @brief Normalized value in the -1..1 range for every available axis */
		float _axesValues[MaxAxes];
		/** @brief Minimum value for every available axis (used for -1..1 range remapping) */
		float _axesMinValues[MaxAxes];
		/** @brief Range value for every available axis (used for -1..1 range remapping) */
		float _axesRangeValues[MaxAxes];
		unsigned char _hatState; // no more than one hat is supported
		int _numVibrators;
		int _vibratorsIds[MaxVibrators];
		AndroidJniClass_Vibrator _vibrators[MaxVibrators];

		friend class AndroidInputManager;
	};

	/**
		@brief Parses and dispatches Android input events
		
		Reads NDK input events (touch, keyboard, mouse and gamepad) plus the accelerometer sensor,
		then translates them into engine events forwarded to the registered input event handler.
	*/
	class AndroidInputManager : public IInputManager
	{
		friend class nCine::AndroidApplication;

	public:
		explicit AndroidInputManager(struct android_app* state);
		~AndroidInputManager() override;

		/** @brief Enables the accelerometer sensor */
		static void enableAccelerometerSensor();
		/** @brief Disables the accelerometer sensor */
		static void disableAccelerometerSensor();

		/** @brief Allows the application to make use of the accelerometer */
		static void enableAccelerometer(bool enabled);

		inline const MouseState& mouseState() const override {
			return _mouseState;
		}
		inline const KeyboardState& keyboardState() const override {
			return _keyboardState;
		}

		/** @brief Parses an Android sensor event related to the accelerometer */
		static void parseAccelerometerEvent();
		/** @brief Parses an Android input event */
		static bool parseEvent(const AInputEvent* event);

		bool isJoyPresent(int joyId) const override;
		const char* joyName(int joyId) const override;
		const JoystickGuid joyGuid(int joyId) const override;
		int joyNumButtons(int joyId) const override;
		int joyNumHats(int joyId) const override;
		int joyNumAxes(int joyId) const override;
		const JoystickState& joystickState(int joyId) const override;
		bool joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) override;
		bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override;

		void setCursor(Cursor cursor) override {
			_cursor = cursor;
		}

	private:
		static constexpr int MaxNumJoysticks = 8;

		static ASensorManager* _sensorManager;
		static const ASensor* _accelerometerSensor;
		static ASensorEventQueue* _sensorEventQueue;
		static bool _accelerometerEnabled;

		static AccelerometerEvent _accelerometerEvent;
		static TouchEvent _touchEvent;
		static AndroidKeyboardState _keyboardState;
		static KeyboardEvent _keyboardEvent;
		static TextInputEvent _textInputEvent;
		static AndroidMouseState _mouseState;
		static MouseEvent _mouseEvent;
		static ScrollEvent _scrollEvent;
		/** @brief Back and forward key events triggered by the mouse, simulated as right and middle button */
		static int _simulatedMouseButtonState;

		static AndroidJoystickState _nullJoystickState;
		static AndroidJoystickState _joystickStates[MaxNumJoysticks];
		static JoyButtonEvent _joyButtonEvent;
		static JoyHatEvent _joyHatEvent;
		static JoyAxisEvent _joyAxisEvent;
		static JoyConnectionEvent _joyConnectionEvent;
		/** @brief Update rate of @ref updateJoystickConnections() in seconds */
		static constexpr float JoyCheckRateSecs = 2.0f;
		static Timer _joyCheckTimer;

		/** @brief Processes a gamepad event */
		static bool processGamepadEvent(const AInputEvent* event);
		/** @brief Processes a keyboard event */
		static bool processKeyboardEvent(const AInputEvent* event);
		/** @brief Processes a touch event */
		static bool processTouchEvent(const AInputEvent* event);
		/** @brief Processes a mouse event */
		static bool processMouseEvent(const AInputEvent* event);
		/** @brief Processes a keycode event generated by the mouse, like the back key on right mouse click */
		static bool processMouseKeyEvent(const AInputEvent* event);

		/**
		 * @brief Dispatches a Unicode character entered on the screen keyboard
		 *
		 * Software keyboards commit text through an input connection instead of sending key events,
		 * so the text has to be injected separately.
		 */
		static void injectTextInput(char32_t codePoint);
		/** @brief Dispatches a key event generated by the screen keyboard */
		static void injectKeyEvent(bool pressed, std::int32_t keyCode);

		/** @brief Initializes the accelerometer sensor */
		static void initAccelerometerSensor(struct android_app* state);

		/** @brief Updates joystick states after connections and disconnections */
		static void updateJoystickConnections();
		/** @brief Checks if a previously connected joystick has been disconnected */
		static void checkDisconnectedJoysticks();
		/** @brief Checks if a new joystick has been connected */
		static void checkConnectedJoysticks();

		static int findJoyId(int deviceId);
		static bool isDeviceConnected(int deviceId);
		static void deviceInfo(int deviceId, int joyId);
	};

}
