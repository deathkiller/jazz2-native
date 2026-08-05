#pragma once

#if defined(WITH_QT5) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <qevent.h>
#include "../Input/IInputManager.h"

#if defined(WITH_QT5GAMEPAD)
class QGamepadManager;
class QGamepad;
#endif

class QKeyEvent;
class QMouseEvent;
class QTouchEvent;
class QWheelEvent;

namespace nCine::Backends
{
	class Qt5Widget;

	/**
		@brief Utility functions to convert between engine key enumerations and Qt5 ones
	*/
	class Qt5Keys
	{
	public:
		/** @brief Maps a Qt5 key symbol to the matching engine @ref Keys value */
		static Keys keySymValueToEnum(int keysym);
		/** @brief Maps Qt5 keyboard modifier flags to the engine modifier mask */
		static int keyModMaskToEnumMask(Qt::KeyboardModifiers keymod);
	};

	/**
		@brief Information about Qt5 mouse state

		Wraps the `Qt::MouseButtons` flags last reported by the widget.
	*/
	class Qt5MouseState : public MouseState
	{
	public:
		Qt5MouseState()
			: _buttons(Qt::NoButton) {}

		bool isButtonDown(MouseButton button) const override;

	private:
		Qt::MouseButtons _buttons;

		friend class Qt5InputManager;
	};

	/**
		@brief Information about a Qt5 scroll event
	*/
	class Qt5ScrollEvent : public ScrollEvent
	{
	public:
		Qt5ScrollEvent() {}

		friend class Qt5InputManager;
	};

	/**
		@brief Simulated information about Qt5 keyboard state

		Qt5 delivers key press and release events rather than exposing a pollable
		keyboard array, so the down state of every key is tracked in a simulated
		array updated from those events.
	*/
	class Qt5KeyboardState : public KeyboardState
	{
	public:
		Qt5KeyboardState()
		{
			for (std::int32_t i = 0; i < NumKeys; i++) {
				_keys[i] = 0;
			}
		}

		inline bool isKeyDown(Keys key) const override
		{
			if (key == Keys::Unknown) {
				return false;
			} else {
				return _keys[static_cast<std::int32_t>(key)] != 0;
			}
		}

	private:
		static constexpr std::int32_t NumKeys = static_cast<std::int32_t>(Keys::Count);
		std::uint8_t _keys[NumKeys];

		friend class Qt5InputManager;
	};

	/**
		@brief Information about Qt5 joystick state

		Backed by a `QGamepad` when @ref WITH_QT5GAMEPAD is enabled; otherwise a
		no-op stub that always reports no input.
	*/
#if defined(WITH_QT5GAMEPAD)
	class Qt5JoystickState : public JoystickState
	{
	public:
		Qt5JoystickState();

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

	private:
		static constexpr std::int32_t MaxNameLength = 256;
		/** @brief Number of buttons reported in the order @ref isButtonPressed() polls them */
		static constexpr std::int32_t NumButtons = 12;
		static constexpr std::int32_t NumAxes = 6;
		/** @brief Minimum difference between two axis readings in order to trigger an event */
		static const float AxisEventTolerance;
		bool _buttonState[NumButtons];
		unsigned char _hatState;
		float _axesValuesState[NumAxes];

		char _name[MaxNameLength];
		std::unique_ptr<QGamepad> _gamepad;

		friend class Qt5InputManager;
	};
#else
	class Qt5JoystickState : public JoystickState
	{
	public:
		Qt5JoystickState() {}

		inline bool isButtonPressed(int buttonId) const override {
			return false;
		}
		inline unsigned char hatState(int hatId) const override {
			return HatState::Centered;
		}
		inline float axisValue(int axisId) const override {
			return 0.0f;
		}

	private:
		friend class Qt5InputManager;
	};
#endif

	/**
		@brief Parses and dispatches Qt5 input events

		Translates the Qt5 events forwarded by @ref Qt5Widget into engine input
		state and events. Joystick support is provided through Qt5Gamepad when
		@ref WITH_QT5GAMEPAD is enabled.
	*/
	class Qt5InputManager : public IInputManager
	{
	public:
		/** @brief The constructor takes care of opening available joysticks */
		Qt5InputManager(Qt5Widget& widget);
		/** @brief The destructor releases every opened joystick */
		~Qt5InputManager();

#if defined(WITH_QT5GAMEPAD)
		/** @brief Polls every connected gamepad and emits state-change events */
		void updateJoystickStates();
#endif

		/** @brief Returns `true` if the application should quit on a close request */
		bool shouldQuitOnRequest();
		/** @brief Handles a generic Qt5 event, returning `true` if it was consumed */
		bool event(QEvent* event);
		/** @brief Handles a Qt5 key press event */
		void keyPressEvent(QKeyEvent* event);
		/** @brief Handles a Qt5 key release event */
		void keyReleaseEvent(QKeyEvent* event);
		/** @brief Handles a Qt5 mouse button press event */
		void mousePressEvent(QMouseEvent* event);
		/** @brief Handles a Qt5 mouse button release event */
		void mouseReleaseEvent(QMouseEvent* event);
		/** @brief Handles a Qt5 mouse move event */
		void mouseMoveEvent(QMouseEvent* event);
		/** @brief Handles the start of a Qt5 touch sequence */
		void touchBeginEvent(QTouchEvent* event);
		/** @brief Handles an update within a Qt5 touch sequence */
		void touchUpdateEvent(QTouchEvent* event);
		/** @brief Handles the end of a Qt5 touch sequence */
		void touchEndEvent(QTouchEvent* event);
		/** @brief Handles a Qt5 mouse wheel event */
		void wheelEvent(QWheelEvent* event);

		inline const MouseState& mouseState() const override {
			return _mouseState;
		}
		inline const KeyboardState& keyboardState() const override {
			return _keyboardState;
		}

#if defined(WITH_QT5GAMEPAD)
		bool isJoyPresent(int joyId) const override;
		const char* joyName(int joyId) const override;
		/** @brief Qt5Gamepad exposes no device identity, so joysticks are never matched by GUID (see @ref JoyMapping) */
		inline const JoystickGuid joyGuid(int joyId) const override {
			return {};
		}
		inline int joyNumButtons(int joyId) const override {
			return Qt5JoystickState::NumButtons;
		}
		inline int joyNumHats(int joyId) const override {
			return 1;
		}
		inline int joyNumAxes(int joyId) const override {
			return Qt5JoystickState::NumAxes;
		}
		const JoystickState& joystickState(int joyId) const override;
#else
		inline bool isJoyPresent(int joyId) const override {
			return false;
		}
		inline const char* joyName(int joyId) const override {
			return nullptr;
		}
		inline const JoystickGuid joyGuid(int joyId) const override {
			return {};
		}
		inline int joyNumButtons(int joyId) const override {
			return 0;
		}
		inline int joyNumHats(int joyId) const override {
			return 0;
		}
		inline int joyNumAxes(int joyId) const override {
			return 0;
		}
		inline const JoystickState& joystickState(int joyId) const override {
			return _nullJoystickState;
		}
#endif
		/** @brief Qt5Gamepad exposes no rumble interface */
		inline bool joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, std::uint32_t durationMs) override {
			return false;
		}
		/** @brief Qt5Gamepad exposes no rumble interface */
		inline bool joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs) override {
			return false;
		}

		void setCursor(Cursor cursor) override;

	private:
		static constexpr std::int32_t MaxNumJoysticks = 4;

		static TouchEvent _touchEvent;
		static Qt5MouseState _mouseState;
		static MouseEvent _mouseEvent;
		static Qt5ScrollEvent _scrollEvent;
		static Qt5KeyboardState _keyboardState;
		static KeyboardEvent _keyboardEvent;
		static TextInputEvent _textInputEvent;
		static Qt5JoystickState _nullJoystickState;
#if defined(WITH_QT5GAMEPAD)
		static Qt5JoystickState _joystickStates[MaxNumJoysticks];
		static JoyButtonEvent _joyButtonEvent;
		static JoyHatEvent _joyHatEvent;
		static JoyAxisEvent _joyAxisEvent;
		static JoyConnectionEvent _joyConnectionEvent;
#endif

		Qt5Widget& _widget;

		void updateTouchEvent(const QTouchEvent* event, TouchEventType type);

		/** @brief Deleted copy constructor */
		Qt5InputManager(const Qt5InputManager&) = delete;
		/** @brief Deleted assignment operator */
		Qt5InputManager& operator=(const Qt5InputManager&) = delete;
	};

}

#endif
