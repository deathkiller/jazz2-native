#if defined(WITH_QT5)

#include "../../Main.h"
#include "Qt5InputManager.h"
#include "Qt5Widget.h"
#include "../Input/IInputEventHandler.h"
#include "../Input/JoyMapping.h"

#include <algorithm>
#include <cstring>

#include <qevent.h>
#include <QWidget>

#if defined(WITH_QT5GAMEPAD)
#	include <QtGamepad/QGamepadManager>
#	include <QtGamepad/QGamepad>
#endif

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 4;
}

namespace nCine::Backends
{
	namespace
	{
		MouseButton qtToNcineMouseButton(Qt::MouseButton button)
		{
			switch (button) {
				default:
				case Qt::LeftButton: return MouseButton::Left;
				case Qt::RightButton: return MouseButton::Right;
				case Qt::MiddleButton: return MouseButton::Middle;
				case Qt::BackButton: return MouseButton::Fourth;
				case Qt::ForwardButton: return MouseButton::Fifth;
			}
		}

		Qt::MouseButton ncineToQtMouseButton(MouseButton button)
		{
			switch (button) {
				default:
				case MouseButton::Left: return Qt::LeftButton;
				case MouseButton::Right: return Qt::RightButton;
				case MouseButton::Middle: return Qt::MiddleButton;
				case MouseButton::Fourth: return Qt::BackButton;
				case MouseButton::Fifth: return Qt::ForwardButton;
			}
		}
	}

	TouchEvent Qt5InputManager::_touchEvent;
	Qt5MouseState Qt5InputManager::_mouseState;
	MouseEvent Qt5InputManager::_mouseEvent;
	Qt5ScrollEvent Qt5InputManager::_scrollEvent;
	Qt5KeyboardState Qt5InputManager::_keyboardState;
	KeyboardEvent Qt5InputManager::_keyboardEvent;
	TextInputEvent Qt5InputManager::_textInputEvent;
	Qt5JoystickState Qt5InputManager::_nullJoystickState;

#if defined(WITH_QT5GAMEPAD)
	Qt5JoystickState Qt5InputManager::_joystickStates[MaxNumJoysticks];
	JoyButtonEvent Qt5InputManager::_joyButtonEvent;
	JoyHatEvent Qt5InputManager::_joyHatEvent;
	JoyAxisEvent Qt5InputManager::_joyAxisEvent;
	JoyConnectionEvent Qt5InputManager::_joyConnectionEvent;
	const float Qt5JoystickState::AxisEventTolerance = 0.001f;
#endif

	bool Qt5MouseState::isButtonDown(MouseButton button) const
	{
		return _buttons.testFlag(ncineToQtMouseButton(button));
	}

	Qt5InputManager::Qt5InputManager(Qt5Widget& widget)
		: _widget(widget)
	{
#if defined(WITH_QT5GAMEPAD)
		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			_joystickStates[i]._gamepad = std::make_unique<QGamepad>(-1);
			_joystickStates[i]._name[0] = '\0';
		}
#endif

		_joyMapping.Init(this);
	}

	Qt5InputManager::~Qt5InputManager()
	{
	}

#if defined(WITH_QT5GAMEPAD)
	Qt5JoystickState::Qt5JoystickState()
	{
		for (std::int32_t i = 0; i < NumButtons; i++) {
			_buttonState[i] = false;
		}
		_hatState = HatState::Centered;
		for (std::int32_t i = 0; i < NumAxes; i++) {
			_axesValuesState[i] = 0.0f;
		}
		_name[0] = '\0';
	}

	bool Qt5JoystickState::isButtonPressed(int buttonId) const
	{
		// The order of the buttons is a contract with the mapping assigned in JoyMapping::OnJoyConnected()
		switch (buttonId) {
			case 0: return _gamepad->buttonL1();
			case 1: return _gamepad->buttonL3();
			case 2: return _gamepad->buttonR1();
			case 3: return _gamepad->buttonR3();
			case 4: return _gamepad->buttonA();
			case 5: return _gamepad->buttonB();
			case 6: return _gamepad->buttonCenter();
			case 7: return _gamepad->buttonGuide();
			case 8: return _gamepad->buttonSelect();
			case 9: return _gamepad->buttonStart();
			case 10: return _gamepad->buttonX();
			case 11: return _gamepad->buttonY();
			default: return false;
		}
	}

	unsigned char Qt5JoystickState::hatState(int hatId) const
	{
		unsigned char state = HatState::Centered;

		if (hatId == 0) {
			if (_gamepad->buttonUp()) {
				state |= HatState::Up;
			} else if (_gamepad->buttonDown()) {
				state |= HatState::Down;
			}

			if (_gamepad->buttonRight()) {
				state |= HatState::Right;
			} else if (_gamepad->buttonLeft()) {
				state |= HatState::Left;
			}
		}

		return state;
	}

	float Qt5JoystickState::axisValue(int axisId) const
	{
		switch (axisId) {
			case 0: return static_cast<float>(_gamepad->axisLeftX());
			case 1: return static_cast<float>(_gamepad->axisLeftY());
			case 2: return static_cast<float>(_gamepad->axisRightX());
			case 3: return static_cast<float>(_gamepad->axisRightY());
			// The triggers are reported in [0, 1] and the mapping expects the full axis range
			case 4: return static_cast<float>(2.0 * _gamepad->buttonL2() - 1.0);
			case 5: return static_cast<float>(2.0 * _gamepad->buttonR2() - 1.0);
			default: return 0.0f;
		}
	}

	void Qt5InputManager::updateJoystickStates()
	{
		// Compacting the array of Qt5 connected gamepads
		std::int32_t notConnectedIndex = 0;
		std::int32_t connectedIndex = 0;
		while (notConnectedIndex < MaxNumJoysticks) {
			QGamepad& notConnected = *_joystickStates[notConnectedIndex]._gamepad;
			QGamepad& connected = *_joystickStates[connectedIndex]._gamepad;
			if (notConnected.isConnected() && !connected.isConnected()) {
				const int notConnectedId = notConnected.deviceId();
				notConnected.setDeviceId(connected.deviceId());
				connected.setDeviceId(notConnectedId);

				connectedIndex++;
			}

			notConnectedIndex++;
		}

		if (_inputEventHandler != nullptr) {
			for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
				if (_joystickStates[i]._gamepad->deviceId() > -1 &&
					!_joystickStates[i]._gamepad->isConnected()) {
					_joystickStates[i]._gamepad->setDeviceId(-1);
					_joystickStates[i]._name[0] = '\0';
					_joyConnectionEvent.joyId = i;
					_joyMapping.OnJoyDisconnected(_joyConnectionEvent);
					_inputEventHandler->OnJoyDisconnected(_joyConnectionEvent);
				}
			}
		}

		const QList<int> gamepads = QGamepadManager::instance()->connectedGamepads();
		std::int32_t index = 0;
		for (int deviceId : gamepads) {
			const int oldDeviceId = _joystickStates[index]._gamepad->deviceId();

			if (_inputEventHandler != nullptr && oldDeviceId != deviceId) {
				_joystickStates[index]._gamepad->setDeviceId(deviceId);

				const QByteArray name = QGamepadManager::instance()->gamepadName(deviceId).toUtf8();
				const std::int32_t nameLength = std::min<std::int32_t>(name.size(), Qt5JoystickState::MaxNameLength - 1);
				std::memcpy(_joystickStates[index]._name, name.constData(), nameLength);
				_joystickStates[index]._name[nameLength] = '\0';

				_joyConnectionEvent.joyId = index;
				_joyMapping.OnJoyConnected(_joyConnectionEvent);
				_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
			}

			index++;
			if (index >= MaxNumJoysticks) {
				break;
			}
		}

		for (std::int32_t joyId = 0; joyId < MaxNumJoysticks; joyId++) {
			Qt5JoystickState& state = _joystickStates[joyId];
			if (state._gamepad == nullptr) {
				continue;
			}

			for (std::int32_t buttonId = 0; buttonId < Qt5JoystickState::NumButtons; buttonId++) {
				const bool newButtonState = state.isButtonPressed(buttonId);
				if (state._buttonState[buttonId] != newButtonState) {
					state._buttonState[buttonId] = newButtonState;
					if (_inputEventHandler != nullptr) {
						_joyButtonEvent.joyId = joyId;
						_joyButtonEvent.buttonId = buttonId;
						if (newButtonState) {
							_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
							_inputEventHandler->OnJoyButtonPressed(_joyButtonEvent);
						} else {
							_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
							_inputEventHandler->OnJoyButtonReleased(_joyButtonEvent);
						}
					}
				}
			}

			const unsigned char newHatState = state.hatState(0);
			if (state._hatState != newHatState) {
				state._hatState = newHatState;
				if (_inputEventHandler != nullptr) {
					_joyHatEvent.joyId = joyId;
					_joyHatEvent.hatId = 0;
					_joyHatEvent.hatState = newHatState;
					_joyMapping.OnJoyHatMoved(_joyHatEvent);
					_inputEventHandler->OnJoyHatMoved(_joyHatEvent);
				}
			}

			for (std::int32_t axisId = 0; axisId < Qt5JoystickState::NumAxes; axisId++) {
				const float newAxisValue = state.axisValue(axisId);
				if (std::abs(state._axesValuesState[axisId] - newAxisValue) > Qt5JoystickState::AxisEventTolerance) {
					state._axesValuesState[axisId] = newAxisValue;
					if (_inputEventHandler != nullptr) {
						_joyAxisEvent.joyId = joyId;
						_joyAxisEvent.axisId = axisId;
						_joyAxisEvent.value = newAxisValue;
						_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
						_inputEventHandler->OnJoyAxisMoved(_joyAxisEvent);
					}
				}
			}
		}
	}
#endif

	bool Qt5InputManager::shouldQuitOnRequest()
	{
		return (_inputEventHandler == nullptr || _inputEventHandler->OnQuitRequest());
	}

	bool Qt5InputManager::event(QEvent* event)
	{
		switch (event->type()) {
			case QEvent::KeyPress:
				keyPressEvent(static_cast<QKeyEvent*>(event));
				return true;
			case QEvent::KeyRelease:
				keyReleaseEvent(static_cast<QKeyEvent*>(event));
				return true;
			case QEvent::MouseButtonPress:
				mousePressEvent(static_cast<QMouseEvent*>(event));
				return true;
			case QEvent::MouseButtonRelease:
				mouseReleaseEvent(static_cast<QMouseEvent*>(event));
				return true;
			case QEvent::MouseMove:
				mouseMoveEvent(static_cast<QMouseEvent*>(event));
				return true;
			case QEvent::TouchBegin:
				touchBeginEvent(static_cast<QTouchEvent*>(event));
				return true;
			case QEvent::TouchUpdate:
				touchUpdateEvent(static_cast<QTouchEvent*>(event));
				return true;
			case QEvent::TouchEnd:
				touchEndEvent(static_cast<QTouchEvent*>(event));
				return true;
			case QEvent::Wheel:
				wheelEvent(static_cast<QWheelEvent*>(event));
				return true;
			default:
				return false;
		}
	}

	void Qt5InputManager::keyPressEvent(QKeyEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		_keyboardEvent.scancode = static_cast<std::int32_t>(event->nativeScanCode());
		_keyboardEvent.sym = Qt5Keys::keySymValueToEnum(event->key());
		_keyboardEvent.mod = Qt5Keys::keyModMaskToEnumMask(event->modifiers());
		if (_keyboardEvent.sym != Keys::Unknown) {
			_keyboardState._keys[static_cast<std::int32_t>(_keyboardEvent.sym)] = 1;
		}
		_inputEventHandler->OnKeyPressed(_keyboardEvent);

		const QByteArray text = event->text().toUtf8();
		if (!text.isEmpty()) {
			// The event carries one code point at a time, which is what the four bytes are sized for
			const std::int32_t length = std::min<std::int32_t>(text.size(), std::int32_t(sizeof(_textInputEvent.text)));
			std::memcpy(_textInputEvent.text, text.constData(), length);
			_textInputEvent.length = length;
			_inputEventHandler->OnTextInput(_textInputEvent);
		}
	}

	void Qt5InputManager::keyReleaseEvent(QKeyEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		_keyboardEvent.scancode = static_cast<std::int32_t>(event->nativeScanCode());
		_keyboardEvent.sym = Qt5Keys::keySymValueToEnum(event->key());
		_keyboardEvent.mod = Qt5Keys::keyModMaskToEnumMask(event->modifiers());
		if (_keyboardEvent.sym != Keys::Unknown) {
			_keyboardState._keys[static_cast<std::int32_t>(_keyboardEvent.sym)] = 0;
		}
		_inputEventHandler->OnKeyReleased(_keyboardEvent);
	}

	void Qt5InputManager::mousePressEvent(QMouseEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		// Pointer coordinates are top-down, as in every other backend
		_mouseEvent.x = event->pos().x();
		_mouseEvent.y = event->pos().y();
		_mouseEvent.button = qtToNcineMouseButton(event->button());
		_mouseState.x = _mouseEvent.x;
		_mouseState.y = _mouseEvent.y;
		_mouseState._buttons = event->buttons();
		_inputEventHandler->OnMouseDown(_mouseEvent);
	}

	void Qt5InputManager::mouseReleaseEvent(QMouseEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		_mouseEvent.x = event->pos().x();
		_mouseEvent.y = event->pos().y();
		_mouseEvent.button = qtToNcineMouseButton(event->button());
		_mouseState.x = _mouseEvent.x;
		_mouseState.y = _mouseEvent.y;
		_mouseState._buttons = event->buttons();
		_inputEventHandler->OnMouseUp(_mouseEvent);
	}

	void Qt5InputManager::mouseMoveEvent(QMouseEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		_mouseState.x = event->pos().x();
		_mouseState.y = event->pos().y();
		_mouseState._buttons = event->buttons();
		_inputEventHandler->OnMouseMove(_mouseState);
	}

	void Qt5InputManager::touchBeginEvent(QTouchEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		updateTouchEvent(event, TouchEventType::Down);
		_inputEventHandler->OnTouchEvent(_touchEvent);
	}

	void Qt5InputManager::touchUpdateEvent(QTouchEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		// Qt5 has one update event for every change within a sequence, so the pointer count decides
		// which of the three engine event types it is
		const std::uint32_t previousCount = _touchEvent.count;
		const std::uint32_t newCount = static_cast<std::uint32_t>(event->touchPoints().size());
		const TouchEventType type = (previousCount < newCount
			? TouchEventType::PointerDown
			: (previousCount > newCount ? TouchEventType::PointerUp : TouchEventType::Move));

		updateTouchEvent(event, type);
		_inputEventHandler->OnTouchEvent(_touchEvent);
	}

	void Qt5InputManager::touchEndEvent(QTouchEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		updateTouchEvent(event, TouchEventType::Up);
		_inputEventHandler->OnTouchEvent(_touchEvent);
	}

	void Qt5InputManager::wheelEvent(QWheelEvent* event)
	{
		if (_inputEventHandler == nullptr) {
			return;
		}

		_scrollEvent.x = event->angleDelta().x() / 60.0f;
		_scrollEvent.y = event->angleDelta().y() / 60.0f;
		_inputEventHandler->OnMouseWheel(_scrollEvent);
	}

#if defined(WITH_QT5GAMEPAD)
	bool Qt5InputManager::isJoyPresent(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxNumJoysticks && _joystickStates[joyId]._gamepad->isConnected());
	}

	const char* Qt5InputManager::joyName(int joyId) const
	{
		return (isJoyPresent(joyId) ? _joystickStates[joyId]._name : nullptr);
	}

	const JoystickState& Qt5InputManager::joystickState(int joyId) const
	{
		return (isJoyPresent(joyId) ? static_cast<const JoystickState&>(_joystickStates[joyId]) : _nullJoystickState);
	}
#endif

	void Qt5InputManager::setCursor(Cursor cursor)
	{
		if (cursor != _cursor) {
			switch (cursor) {
				case Cursor::Arrow:
					_widget.unsetCursor();
					_widget.releaseMouse();
					break;
				case Cursor::Hidden:
					_widget.setCursor(QCursor(Qt::BlankCursor));
					_widget.releaseMouse();
					break;
				case Cursor::HiddenLocked:
					_widget.grabMouse(QCursor(Qt::BlankCursor));
					break;
			}

			// Handling ImGui cursor changes
			IInputManager::setCursor(cursor);

			_cursor = cursor;
		}
	}

	void Qt5InputManager::updateTouchEvent(const QTouchEvent* event, TouchEventType type)
	{
		const QList<QTouchEvent::TouchPoint>& touchPoints = event->touchPoints();

		_touchEvent.type = type;
		_touchEvent.count = std::min<std::uint32_t>(static_cast<std::uint32_t>(touchPoints.size()), TouchEvent::MaxPointers);
		_touchEvent.actionIndex = -1;

		for (std::uint32_t i = 0; i < _touchEvent.count; i++) {
			TouchEvent::Pointer& pointer = _touchEvent.pointers[i];
			const QTouchEvent::TouchPoint& touchPoint = touchPoints.at(i);

			pointer.id = touchPoint.id();
			pointer.x = touchPoint.pos().x();
			pointer.y = touchPoint.pos().y();
			pointer.pressure = touchPoint.pressure();

			// The pointer that triggered the event is the one Qt5 reports as changed
			if (_touchEvent.actionIndex < 0) {
				const Qt::TouchPointStates state = touchPoint.state();
				const bool isAction = ((type == TouchEventType::Down || type == TouchEventType::PointerDown)
					? state.testFlag(Qt::TouchPointPressed)
					: ((type == TouchEventType::Up || type == TouchEventType::PointerUp)
						? state.testFlag(Qt::TouchPointReleased)
						: state.testFlag(Qt::TouchPointMoved)));
				if (isAction) {
					_touchEvent.actionIndex = static_cast<std::int32_t>(i);
				}
			}
		}
	}
}

#endif
