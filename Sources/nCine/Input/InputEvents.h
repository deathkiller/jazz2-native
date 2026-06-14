#pragma once

#include "Keys.h"
#include "../../Main.h"

namespace nCine
{
	/**
		@brief Gamepad buttons
		
		Unified button names a mapped joystick can report, regardless of the physical controller layout.
	*/
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

	/**
		@brief Gamepad axes
		
		Unified axis names a mapped joystick can report, regardless of the physical controller layout.
	*/
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

	/**
		@brief Bit flags describing the direction of a joystick hat (D-pad)
		
		Diagonal directions combine the orthogonal flags.
	*/
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

	/** @brief Type of a touch event */
	enum class TouchEventType
	{
		Down,			/**< The first pointer touched the screen */
		Up,				/**< The last pointer was released */
		Move,			/**< A pointer moved */
		PointerDown,	/**< An additional pointer touched the screen */
		PointerUp		/**< An additional pointer was released */
	};

	/**
		@brief Screen touch event
		
		Holds the snapshot of all active pointers together with the action that triggered the event.
	*/
	class TouchEvent
	{
	public:
		/** @brief Maximum number of simultaneous pointers tracked */
		static constexpr std::uint32_t MaxPointers = 10;

		/** @brief State of a single touch pointer */
		struct Pointer
		{
			/** @brief Pointer identifier */
			std::int32_t id;
			/** @brief Pointer position */
			float x, y;
			/** @brief Pointer pressure */
			float pressure;
		};

		TouchEvent()
			: count(0), actionIndex(-1) {}

		/** @brief Type of the event */
		TouchEventType type;
		/** @brief Number of active pointers */
		std::uint32_t count;
		/** @brief Index into @ref pointers of the pointer that triggered the event, or -1 */
		std::int32_t actionIndex;
		/** @brief Active pointers */
		Pointer pointers[MaxPointers];

		/** @brief Returns the index of the pointer with the specified identifier, or -1 if not found */
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

	/** @brief Accelerometer sensor reading */
	class AccelerometerEvent
	{
	public:
		AccelerometerEvent()
			: x(0.0f), y(0.0f), z(0.0f) {}

		/** @brief Acceleration on each axis */
		float x, y, z;
	};

	/** @brief Mouse buttons */
	enum class MouseButton : short int
	{
		Left,
		Right,
		Middle,
		Fourth,
		Fifth
	};

	/** @brief Current state of the mouse */
	class MouseState
	{
	public:
		/** @brief Pointer position on the X axis */
		std::int32_t x;
		/** @brief Pointer position on the Y axis */
		std::int32_t y;

		/** @brief Returns `true` if the specified button is currently down */
		virtual bool isButtonDown(MouseButton button) const = 0;

	protected:
		static const unsigned int NumButtons = 5;
	};

	/** @brief Mouse button event */
	class MouseEvent
	{
	public:
		/** @brief Pointer position on the X axis */
		std::int32_t x;
		/** @brief Pointer position on the Y axis */
		std::int32_t y;
		/** @brief Button that was pressed or released */
		MouseButton button;
	};

	/** @brief Scroll event (mouse wheel, touchpad gesture, etc.) */
	class ScrollEvent
	{
	public:
		/** @brief Scroll offset on the X axis */
		float x;
		/** @brief Scroll offset on the Y axis */
		float y;
	};

	/** @brief Current state of the keyboard */
	class KeyboardState
	{
	public:
		/** @brief Returns `true` if the specified key is currently down */
		virtual bool isKeyDown(Keys key) const = 0;
	};

	/** @brief Keyboard key event */
	class KeyboardEvent
	{
	public:
		/** @brief Key scan code */
		std::int32_t scancode;
		/** @brief Key symbol code */
		Keys sym;
		/** @brief Key modifiers mask */
		std::int32_t mod;

		KeyboardEvent()
			: scancode(0), sym(Keys::Unknown), mod(0) {}
	};

	/** @brief Text input event */
	class TextInputEvent
	{
	public:
		/** @brief Unicode code point encoded in UTF-8 */
		char text[4];
		/** @brief Length in bytes of the encoded code point */
		std::int32_t length;

		TextInputEvent()
			: length(0)
		{
			text[0] = '\0';
		}
	};

	/** @brief Current raw state of a joystick */
	class JoystickState
	{
	public:
		virtual ~JoystickState() { }

		/** @brief Returns `true` if the specified button is pressed */
		virtual bool isButtonPressed(int buttonId) const = 0;
		/** @brief Returns the state of the specified hat */
		virtual unsigned char hatState(int hatId) const = 0;
		/** @brief Returns the value of the specified axis, normalized between -1.0 and 1.0 */
		virtual float axisValue(int axisId) const = 0;
	};

	/** @brief Joystick button event */
	class JoyButtonEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t joyId;
		/** @brief Button identifier */
		std::int32_t buttonId;
	};

	/** @brief Joystick hat event */
	class JoyHatEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t joyId;
		/** @brief Hat identifier */
		std::int32_t hatId;
		/** @brief Hat direction state (a combination of @ref HatState flags) */
		unsigned char hatState;
	};

	/** @brief Joystick axis event */
	class JoyAxisEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t joyId;
		/** @brief Axis identifier */
		std::int32_t axisId;
		/** @brief Axis value normalized between -1.0f and 1.0f */
		float value;
	};

	/** @brief Joystick connection or disconnection event */
	class JoyConnectionEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t joyId;
	};

	/** @brief Current state of a joystick translated through its mapping into unified buttons and axes */
	class JoyMappedState
	{
		friend class JoyMapping;

	public:
		/** @brief Number of mapped buttons */
		static constexpr std::uint32_t NumButtons = (std::uint32_t)ButtonName::Count;
		/** @brief Number of mapped axes */
		static constexpr std::uint32_t NumAxes = 6;

		JoyMappedState()
		{
			for (std::uint32_t i = 0; i < NumButtons; i++)
				buttons_[i] = false;
			for (std::uint32_t i = 0; i < NumAxes; i++)
				axesValues_[i] = 0.0f;
			lastHatState_ = HatState::Centered;
		}

		/** @brief Returns `true` if the specified mapped button is pressed */
		bool isButtonPressed(ButtonName name) const
		{
			bool pressed = false;
			if (name != ButtonName::Unknown)
				pressed = buttons_[static_cast<std::int32_t>(name)];
			return pressed;
		}

		/** @brief Returns the value of the specified mapped axis */
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

	/** @brief Mapped joystick button event */
	class JoyMappedButtonEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t joyId;
		/** @brief Unified button name */
		ButtonName buttonName;
	};

	/** @brief Mapped joystick axis event */
	class JoyMappedAxisEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t joyId;
		/** @brief Unified axis name */
		AxisName axisName;
		/** @brief Axis value normalized between -1.0f and 1.0f */
		float value;
	};

}
