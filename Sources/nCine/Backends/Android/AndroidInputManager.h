#pragma once

#include "../../Input/IInputManager.h"

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

	/// Utility functions to convert between engine key enumerations and Android ones
	class AndroidKeys
	{
	public:
		static KeySym keySymValueToEnum(int keysym);
		static int keyModMaskToEnumMask(int keymod);
	};

	/// Simulated information about Android keyboard state
	class AndroidKeyboardState : public KeyboardState
	{
	public:
		AndroidKeyboardState()
		{
			for (unsigned int i = 0; i < NumKeys; i++)
				keys_[i] = 0;
		}

		inline bool isKeyDown(KeySym key) const override
		{
			if (key == KeySym::UNKNOWN)
				return false;
			else
				return keys_[static_cast<unsigned int>(key)] != 0;
		}

	private:
		static const unsigned int NumKeys = static_cast<unsigned int>(KeySym::COUNT);
		unsigned char keys_[NumKeys];

		friend class AndroidInputManager;
	};

	/// Information about Android mouse state
	class AndroidMouseState : public MouseState
	{
	public:
		AndroidMouseState()
			: buttonState_(0) {}

		bool isLeftButtonDown() const override;
		bool isMiddleButtonDown() const override;
		bool isRightButtonDown() const override;
		bool isFourthButtonDown() const override;
		bool isFifthButtonDown() const override;

	private:
		int buttonState_;

		friend class AndroidInputManager;
	};

	/// Information about an Android mouse event
	class AndroidMouseEvent : public MouseEvent
	{
	public:
		AndroidMouseEvent()
			: button_(0) {}

		bool isLeftButton() const override;
		bool isMiddleButton() const override;
		bool isRightButton() const override;
		bool isFourthButton() const override;
		bool isFifthButton() const override;

	private:
		int button_;

		friend class AndroidInputManager;
	};

	/// Information about Android joystick state
	class AndroidJoystickState : JoystickState
	{
	public:
		AndroidJoystickState();

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

	private:
		static constexpr unsigned int MaxNameLength = 256;
		/// All AKEYCODE_BUTTON_* plus AKEYCODE_BACK
		static constexpr int MaxButtons = AKEYCODE_ESCAPE - AKEYCODE_BUTTON_A + 1;
		static constexpr int MaxAxes = 10;
		static constexpr int NumAxesToMap = 12;
		static const int AxesToMap[NumAxesToMap];

		int deviceId_;
		JoystickGuid guid_;
		char name_[MaxNameLength];

		int numButtons_;
		int numHats_;
		int numAxes_;
		int numAxesMapped_;
		bool hasDPad_;
		bool hasHatAxes_;
		short int buttonsMapping_[MaxButtons];
		short int axesMapping_[MaxAxes];
		bool buttons_[MaxButtons];
		/// Normalized value in the -1..1 range
		float axesValues_[MaxAxes];
		/// Minimum value for every available axis (used for -1..1 range remapping)
		float axesMinValues_[MaxAxes];
		/// Range value for every available axis (used for -1..1 range remapping)
		float axesRangeValues_[MaxAxes];
		unsigned char hatState_; // no more than one hat is supported

		friend class AndroidInputManager;
	};

	/// The class for parsing and dispatching Android input events
	class AndroidInputManager : public IInputManager
	{
		friend class AndroidApplication;

	public:
		explicit AndroidInputManager(struct android_app* state);
		~AndroidInputManager() override;

		/// Enables the accelerometer sensor
		static void enableAccelerometerSensor();
		/// Disables the accelerometer sensor
		static void disableAccelerometerSensor();

		/// Allows the application to make use of the accelerometer
		static void enableAccelerometer(bool enabled);

		inline const MouseState& mouseState() const override {
			return mouseState_;
		}
		inline const KeyboardState& keyboardState() const override {
			return keyboardState_;
		}

		/// Parses an Android sensor event related to the accelerometer
		static void parseAccelerometerEvent();
		/// Parses an Android input event
		static bool parseEvent(const AInputEvent* event);

		bool isJoyPresent(int joyId) const override;
		const char* joyName(int joyId) const override;
		const JoystickGuid joyGuid(int joyId) const override;
		int joyNumButtons(int joyId) const override;
		int joyNumHats(int joyId) const override;
		int joyNumAxes(int joyId) const override;
		const JoystickState& joystickState(int joyId) const override;
		bool joystickRumble(int joyId, float lowFrequency, float highFrequency, uint32_t durationMs) override;
		bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override;

		void setCursor(Cursor cursor) override {
			cursor_ = cursor;
		}

	private:
		static constexpr int MaxNumJoysticks = 6;

		static ASensorManager* sensorManager_;
		static const ASensor* accelerometerSensor_;
		static ASensorEventQueue* sensorEventQueue_;
		static bool accelerometerEnabled_;

		static AccelerometerEvent accelerometerEvent_;
		static TouchEvent touchEvent_;
		static AndroidKeyboardState keyboardState_;
		static KeyboardEvent keyboardEvent_;
		static TextInputEvent textInputEvent_;
		static AndroidMouseState mouseState_;
		static AndroidMouseEvent mouseEvent_;
		static ScrollEvent scrollEvent_;
		/// Back and forward key events triggered by the mouse are simulated as right and middle button
		static int simulatedMouseButtonState_;

		static AndroidJoystickState nullJoystickState_;
		static AndroidJoystickState joystickStates_[MaxNumJoysticks];
		static JoyButtonEvent joyButtonEvent_;
		static JoyHatEvent joyHatEvent_;
		static JoyAxisEvent joyAxisEvent_;
		static JoyConnectionEvent joyConnectionEvent_;
		/// Update rate of `updateJoystickConnections()` in seconds
		static constexpr float JoyCheckRateSecs = 2.0f;
		static Timer joyCheckTimer_;

		/// Processes a gamepad event
		static bool processGamepadEvent(const AInputEvent* event);
		/// Processes a keyboard event
		static bool processKeyboardEvent(const AInputEvent* event);
		/// Processes a touch event
		static bool processTouchEvent(const AInputEvent* event);
		/// Processes a mouse event
		static bool processMouseEvent(const AInputEvent* event);
		/// Processes a keycode event generated by the mouse, like the back key on right mouse click
		static bool processMouseKeyEvent(const AInputEvent* event);

		/// Initializes the accelerometer sensor
		static void initAccelerometerSensor(struct android_app* state);

		/// Updates joystick states after connections and disconnections
		static void updateJoystickConnections();
		/// Checks if a previously connected joystick has been disconnected
		static void checkDisconnectedJoysticks();
		/// Checks if a new joystick has been connected
		static void checkConnectedJoysticks();

		static int findJoyId(int deviceId);
		static bool isDeviceConnected(int deviceId);
		static void deviceInfo(int deviceId, int joyId);
	};

}
