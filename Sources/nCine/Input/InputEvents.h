#pragma once

#include "Keys.h"
#include "../../Main.h"

namespace nCine
{
	enum class ButtonName : std::int16_t
	{
		Unknown = -1,
		A = 0,
		B,
		X,
		Y,
		Back,
		Guide,
		Start,
		LeftStick,
		RightStick,
		LeftBumper,
		RightBumper,
		Up,
		Down,
		Left,
		Right,
		Misc1,
		Paddle1,
		Paddle2,
		Paddle3,
		Paddle4,
		Touchpad,

		Count
	};

	enum class AxisName : std::int16_t
	{
		Unknown = -1,
		LeftX = 0,
		LeftY,
		RightX,
		RightY,
		LeftTrigger,
		RightTrigger
	};

	/// Structure containing joystick hat values
	struct HatState
	{
		enum
		{
			Centered = 0,
			Up = 1,
			Right = 2,
			Down = 4,
			Left = 8,
			RightUp = Right | Up,
			RightDown = Right | Down,
			LeftUp = Left | Up,
			LeftDown = Left | Down
		};
	};

	enum class TouchEventType
	{
		/// Called every time the first screen touch is made
		Down,
		/// Called every time the last screen touch is released
		Up,
		/// Called every time a screen touch is moved
		Move,
		/// Called every time a screen touch different than the first one is made
		PointerDown,
		/// Called every time a screen touch different than the last one is released
		PointerUp
	};

	/// Information about a screen touch event
	class TouchEvent
	{
	public:
		static constexpr std::uint32_t MaxPointers = 10;

		/// Information about a single touch pointer
		struct Pointer
		{
			std::int32_t id;
			float x, y;
			float pressure;
		};

		TouchEvent()
			: count(0), actionIndex(-1) {}

		TouchEventType type;
		std::uint32_t count;
		std::int32_t actionIndex;
		Pointer pointers[MaxPointers];

		inline std::int32_t findPointerIndex(std::int32_t pointerId) const
		{
			std::int32_t pointerIndex = -1;
			for (std::uint32_t i = 0; i < count && i < MaxPointers; i++) {
				if (pointers[i].id == pointerId) {
					pointerIndex = i;
					break;
				}
			}
			return pointerIndex;
		}
	};

	/// Information about an accelerometer event
	class AccelerometerEvent
	{
	public:
		AccelerometerEvent()
			: x(0.0f), y(0.0f), z(0.0f) {}

		float x, y, z;
	};

	/// Information about mouse state
	class MouseState
	{
	public:
		/// Pointer position on the X axis
		std::int32_t x;
		/// Pointer position on the Y axis
		std::int32_t y;

		virtual bool isLeftButtonDown() const = 0;
		virtual bool isMiddleButtonDown() const = 0;
		virtual bool isRightButtonDown() const = 0;
		virtual bool isFourthButtonDown() const = 0;
		virtual bool isFifthButtonDown() const = 0;
	};

	/// Information about a mouse event
	class MouseEvent
	{
	public:
		/// Pointer position on the X axis
		std::int32_t x;
		/// Pointer position on the Y axis
		std::int32_t y;

		virtual bool isLeftButton() const = 0;
		virtual bool isMiddleButton() const = 0;
		virtual bool isRightButton() const = 0;
		virtual bool isFourthButton() const = 0;
		virtual bool isFifthButton() const = 0;
	};

	/// Information about a scroll event (mouse wheel, touchpad gesture, etc.)
	class ScrollEvent
	{
	public:
		/// Scroll offset on the X axis
		float x;
		/// Scroll offset on the Y axis
		float y;
	};

	/// Information about keyboard state
	class KeyboardState
	{
	public:
		/// Returns 'true' if the specified key is down
		virtual bool isKeyDown(Keys key) const = 0;
	};

	/// Information about a keyboard event
	class KeyboardEvent
	{
	public:
		/// Key scan code
		std::int32_t scancode;
		/// Key symbol code
		Keys sym;
		/// Key modifiers mask
		std::int32_t mod;

		KeyboardEvent()
			: scancode(0), sym(Keys::Unknown), mod(0) {}
	};

	/// Information about a text input event
	class TextInputEvent
	{
	public:
		/// Unicode code point encoded in UTF-8
		char text[4];
		std::int32_t length;

		TextInputEvent()
			: length(0)
		{
			text[0] = '\0';
		}
	};

	/// Information about the joystick state
	class JoystickState
	{
	public:
		virtual ~JoystickState() { }

		/// Returns 'true' if the specified button is pressed
		virtual bool isButtonPressed(int buttonId) const = 0;
		/// Returns the state of the specified hat
		virtual unsigned char hatState(int hatId) const = 0;
		/// Returns a normalized value between -1.0 and 1.0 for a joystick axis
		virtual float axisValue(int axisId) const = 0;
	};

	/// Information about a joystick button event
	class JoyButtonEvent
	{
	public:
		/// Joystick id
		std::int32_t joyId;
		/// Button id
		std::int32_t buttonId;
	};

	/// Information about a joystick hat event
	class JoyHatEvent
	{
	public:
		/// Joystick id
		std::int32_t joyId;
		/// Hat id
		std::int32_t hatId;
		/// Hat position state
		unsigned char hatState;
	};

	/// Information about a joystick axis event
	class JoyAxisEvent
	{
	public:
		/// Joystick id
		std::int32_t joyId;
		/// Axis id
		std::int32_t axisId;
		/// Axis value normalized between -1.0f and 1.0f
		float value;
	};

	/// Information about a joystick connection event
	class JoyConnectionEvent
	{
	public:
		/// Joystick id
		std::int32_t joyId;
	};

	/// Information about a mapped joystick state
	class JoyMappedState
	{
		friend class JoyMapping;

	public:
		/// The number of joystick buttons with a mapping name
		static constexpr std::uint32_t NumButtons = (std::uint32_t)ButtonName::Count;
		/// The number of joystick axes with a mapping name
		static constexpr std::uint32_t NumAxes = 6;

		JoyMappedState()
		{
			for (std::uint32_t i = 0; i < NumButtons; i++)
				buttons_[i] = false;
			for (std::uint32_t i = 0; i < NumAxes; i++)
				axesValues_[i] = 0.0f;
			lastHatState_ = HatState::Centered;
		}

		bool isButtonPressed(ButtonName name) const
		{
			bool pressed = false;
			if (name != ButtonName::Unknown)
				pressed = buttons_[static_cast<std::int32_t>(name)];
			return pressed;
		}

		float axisValue(AxisName name) const
		{
			float value = 0.0f;
			if (name != AxisName::Unknown)
				value = axesValues_[static_cast<std::int32_t>(name)];
			return value;
		}

	private:
		bool buttons_[JoyMappedState::NumButtons];
		float axesValues_[JoyMappedState::NumAxes];
		unsigned char lastHatState_;
	};

	/// Information about a joystick mapped button event
	class JoyMappedButtonEvent
	{
	public:
		/// Joystick id
		std::int32_t joyId;
		/// Button name
		ButtonName buttonName;
	};

	/// Information about a joystick mapped axis event
	class JoyMappedAxisEvent
	{
	public:
		/// Joystick id
		std::int32_t joyId;
		/// Axis name
		AxisName axisName;
		/// Axis value between its minimum and maximum
		float value;
	};

}
