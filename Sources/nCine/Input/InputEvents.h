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
			std::int32_t Id;
			/** @brief Pointer position */
			float X, Y;
			/** @brief Pointer pressure */
			float Pressure;
		};

		TouchEvent()
			: Count(0), ActionIndex(-1) {}

		/** @brief Type of the event */
		TouchEventType Type;
		/** @brief Number of active pointers */
		std::uint32_t Count;
		/** @brief Index into @ref pointers of the pointer that triggered the event, or -1 */
		std::int32_t ActionIndex;
		/** @brief Active pointers */
		Pointer Pointers[MaxPointers];

		/** @brief Returns the index of the pointer with the specified identifier, or -1 if not found */
		inline std::int32_t FindPointerIndex(std::int32_t pointerId) const
		{
			std::int32_t pointerIndex = -1;
			for (std::uint32_t i = 0; i < Count && i < MaxPointers; i++) {
				if (Pointers[i].Id == pointerId) {
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
			: X(0.0f), Y(0.0f), Z(0.0f) {}

		/** @brief Acceleration on each axis */
		float X, Y, Z;
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
		std::int32_t X;
		/** @brief Pointer position on the Y axis */
		std::int32_t Y;
		/**
		 * @brief Window the pointer is over, 0 for the main one
		 *
		 * Over an auxiliary window (see @ref IGfxDevice::CreateAuxiliaryWindow()), the position is still relative to the
		 * main window, so it can be outside of it.
		 */
		std::uint32_t WindowId = 0;

		/** @brief Returns `true` if the specified button is currently down */
		virtual bool IsButtonDown(MouseButton button) const = 0;

	protected:
		static const unsigned int NumButtons = 5;
	};

	/** @brief Mouse button event */
	class MouseEvent
	{
	public:
		/** @brief Pointer position on the X axis */
		std::int32_t X;
		/** @brief Pointer position on the Y axis */
		std::int32_t Y;
		/** @brief Button that was pressed or released */
		MouseButton Button;
		/** @brief Window the pointer is over, 0 for the main one, see @ref MouseState::WindowId */
		std::uint32_t WindowId = 0;
	};

	/** @brief Scroll event (mouse wheel, touchpad gesture, etc.) */
	class ScrollEvent
	{
	public:
		/** @brief Scroll offset on the X axis, positive to the right (as SDL reports it, every backend is made to match) */
		float X;
		/** @brief Scroll offset on the Y axis, positive away from the user */
		float Y;
		/** @brief Window the pointer is over, 0 for the main one, see @ref MouseState::WindowId */
		std::uint32_t WindowId = 0;
	};

	/** @brief Current state of the keyboard */
	class KeyboardState
	{
	public:
		/** @brief Returns `true` if the specified key is currently down */
		virtual bool IsKeyDown(Keys key) const = 0;
	};

	/** @brief Keyboard key event */
	class KeyboardEvent
	{
	public:
		/** @brief Key scan code */
		std::int32_t Scancode;
		/** @brief Key symbol code */
		Keys Sym;
		/** @brief Key modifiers mask */
		std::int32_t Mod;

		KeyboardEvent()
			: Scancode(0), Sym(Keys::Unknown), Mod(0) {}
	};

	/** @brief Text input event */
	class TextInputEvent
	{
	public:
		/** @brief Unicode code point encoded in UTF-8 */
		char Text[4];
		/** @brief Length in bytes of the encoded code point */
		std::int32_t Length;

		TextInputEvent()
			: Length(0)
		{
			Text[0] = '\0';
		}
	};

	/** @brief Current raw state of a joystick */
	class JoystickState
	{
	public:
		virtual ~JoystickState() { }

		/** @brief Returns `true` if the specified button is pressed */
		virtual bool IsButtonPressed(int buttonId) const = 0;
		/** @brief Returns the state of the specified hat */
		virtual unsigned char GetHatState(int hatId) const = 0;
		/** @brief Returns the value of the specified axis, normalized between -1.0 and 1.0 */
		virtual float GetAxisValue(int axisId) const = 0;
	};

	/** @brief Joystick button event */
	class JoyButtonEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t JoyId;
		/** @brief Button identifier */
		std::int32_t ButtonId;
	};

	/** @brief Joystick hat event */
	class JoyHatEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t JoyId;
		/** @brief Hat identifier */
		std::int32_t HatId;
		/** @brief Hat direction state (a combination of @ref HatState flags) */
		unsigned char HatState;
	};

	/** @brief Joystick axis event */
	class JoyAxisEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t JoyId;
		/** @brief Axis identifier */
		std::int32_t AxisId;
		/** @brief Axis value normalized between -1.0f and 1.0f */
		float Value;
	};

	/** @brief Joystick connection or disconnection event */
	class JoyConnectionEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t JoyId;
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
				_buttons[i] = false;
			for (std::uint32_t i = 0; i < NumAxes; i++)
				_axesValues[i] = 0.0f;
			_lastHatState = HatState::Centered;
		}

		/** @brief Returns `true` if the specified mapped button is pressed */
		bool IsButtonPressed(ButtonName name) const
		{
			bool pressed = false;
			if (name != ButtonName::Unknown)
				pressed = _buttons[static_cast<std::int32_t>(name)];
			return pressed;
		}

		/** @brief Returns the value of the specified mapped axis */
		float GetAxisValue(AxisName name) const
		{
			float value = 0.0f;
			if (name != AxisName::Unknown)
				value = _axesValues[static_cast<std::int32_t>(name)];
			return value;
		}

	private:
		bool _buttons[JoyMappedState::NumButtons];
		float _axesValues[JoyMappedState::NumAxes];
		unsigned char _lastHatState;
	};

	/** @brief Mapped joystick button event */
	class JoyMappedButtonEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t JoyId;
		/** @brief Unified button name */
		ButtonName ButtonName;
	};

	/** @brief Mapped joystick axis event */
	class JoyMappedAxisEvent
	{
	public:
		/** @brief Joystick identifier */
		std::int32_t JoyId;
		/** @brief Unified axis name */
		AxisName AxisName;
		/** @brief Axis value normalized between -1.0f and 1.0f */
		float Value;
	};

}
