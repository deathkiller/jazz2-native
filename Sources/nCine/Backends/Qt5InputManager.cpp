#if defined(WITH_QT5)

#include "../../Main.h"
#include "Qt5InputManager.h"
#include "Qt5Widget.h"
#include "../Input/IInputEventHandler.h"
#include "../Input/JoyMapping.h"
#include "../MainApplication.h"

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
	TouchEvent Qt5InputManager::_touchEvent;
	Qt5MouseState Qt5InputManager::_mouseState;
	Qt5MouseEvent Qt5InputManager::_mouseEvent;
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

	Qt5InputManager::Qt5InputManager(Qt5Widget& widget)
		: _widget(widget)
	{
#if defined(WITH_QT5GAMEPAD)
		for (int i = 0; i < MaxNumJoysticks; i++) {
			_joystickStates[i]._gamepad = std::make_unique<QGamepad>(-1);
		}
#endif

		_joyMapping.Init(this);

#if defined(WITH_IMGUI)
		ImGuiQt5Input::init(&widget);
#endif
	}

	Qt5InputManager::~Qt5InputManager()
	{
	}

#if defined(WITH_QT5GAMEPAD)
	Qt5JoystickState::Qt5JoystickState()
	{
		for (unsigned int i = 0; i < NumButtons; i++) {
			_buttonState[i] = false;
		}
		_hatState = HatState::CENTERED;
		for (unsigned int i = 0; i < NumAxes; i++) {
			_axesValuesState[i] = 0.0f;
		}
	}

	bool Qt5JoystickState::isButtonPressed(int buttonId) const
	{
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
		unsigned char state = HatState::CENTERED;

		if (hatId == 0) {
			if (_gamepad->buttonUp())
				state += HatState::UP;
			else if (_gamepad->buttonDown())
				state += HatState::DOWN;

			if (_gamepad->buttonRight())
				state += HatState::RIGHT;
			else if (_gamepad->buttonLeft())
				state += HatState::LEFT;
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
			case 4: return static_cast<float>(2.0 * _gamepad->buttonL2() - 1.0);
			case 5: return static_cast<float>(2.0 * _gamepad->buttonR2() - 1.0);
			default: return 0.0f;
		}
	}

	void Qt5InputManager::updateJoystickStates()
	{
		// Compacting the array of Qt5 connected gamepads
		int notConnectedIndex = 0;
		int connectedIndex = 0;
		while (notConnectedIndex < MaxNumJoysticks) {
			QGamepad& notConnected = *_joystickStates[notConnectedIndex]._gamepad;
			QGamepad& connected = *_joystickStates[connectedIndex]._gamepad;
			if (notConnected.isConnected() && connected.isConnected() == false) {
				const int notConnectedId = notConnected.deviceId();
				notConnected.setDeviceId(connected.deviceId());
				connected.setDeviceId(notConnectedId);

				connectedIndex++;
			}

			notConnectedIndex++;
		}

		if (_inputEventHandler) {
			for (int i = 0; i < MaxNumJoysticks; i++) {
				if (_joystickStates[i]._gamepad->deviceId() > -1 &&
					_joystickStates[i]._gamepad->isConnected() == false) {
					_joystickStates[i]._gamepad->setDeviceId(-1);
					_joyConnectionEvent.joyId = i;
					_inputEventHandler->OnJoyDisconnected(_joyConnectionEvent);
					_joyMapping.OnJoyDisconnected(_joyConnectionEvent);
				}
			}
		}

		const QList<int> gamepads = QGamepadManager::instance()->connectedGamepads();
		int index = 0;
		for (int deviceId : gamepads) {
			const int oldDeviceId = _joystickStates[index]._gamepad->deviceId();

			if (_inputEventHandler && oldDeviceId != deviceId) {
				_joystickStates[index]._gamepad->setDeviceId(deviceId);
				_joyConnectionEvent.joyId = index;
				_joyMapping.OnJoyConnected(_joyConnectionEvent);
				_inputEventHandler->onJoyConnected(_joyConnectionEvent);
			}

			index++;
			if (index >= MaxNumJoysticks)
				break;
		}

		for (int joyId = 0; joyId < MaxNumJoysticks; joyId++) {
			Qt5JoystickState& state = _joystickStates[joyId];
			if (state._gamepad == nullptr)
				continue;

			for (int buttonId = 0; buttonId < Qt5JoystickState::NumButtons; buttonId++) {
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

			for (int axisId = 0; axisId < Qt5JoystickState::NumAxes; axisId++) {
				const float newAxisValue = state.axisValue(axisId);
				if (fabsf(state._axesValuesState[axisId] - newAxisValue) > Qt5JoystickState::AxisEventTolerance) {
					state._axesValuesState[axisId] = newAxisValue;
					if (_inputEventHandler) {
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
		return (_inputEventHandler != nullptr && _inputEventHandler->OnQuitRequest());
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
		if (_inputEventHandler) {
			_keyboardEvent.scancode = static_cast<int>(event->nativeScanCode());
			_keyboardEvent.sym = Qt5Keys::keySymValueToEnum(event->key());
			_keyboardEvent.mod = Qt5Keys::keyModMaskToEnumMask(event->modifiers());
			if (_keyboardEvent.sym != Keys::Unknown) {
				const unsigned int keySym = static_cast<unsigned int>(_keyboardEvent.sym);
				_keyboardState._keys[keySym] = 1;
			}
			_inputEventHandler->OnKeyPressed(_keyboardEvent);

			if (event->text().length() > 0) {
				nctl::strncpy(_textInputEvent.text, event->text().toUtf8().constData(), 4);
				_inputEventHandler->OnTextInput(_textInputEvent);
			}
		}
	}

	void Qt5InputManager::keyReleaseEvent(QKeyEvent* event)
	{
		if (_inputEventHandler) {
			_keyboardEvent.scancode = static_cast<int>(event->nativeScanCode());
			_keyboardEvent.sym = Qt5Keys::keySymValueToEnum(event->key());
			_keyboardEvent.mod = Qt5Keys::keyModMaskToEnumMask(event->modifiers());
			if (_keyboardEvent.sym != Keys::Unknown) {
				const unsigned int keySym = static_cast<unsigned int>(_keyboardEvent.sym);
				_keyboardState._keys[keySym] = 0;
			}
			_inputEventHandler->OnKeyReleased(_keyboardEvent);
		}
	}

	void Qt5InputManager::mousePressEvent(QMouseEvent* event)
	{
		if (_inputEventHandler) {
			_mouseEvent.x = event->x();
			_mouseEvent.y = theApplication().heightInt() - event->y();
			_mouseEvent._button = event->button();
			_mouseState._buttons = event->buttons();
			_inputEventHandler->OnMouseDown(_mouseEvent);
		}
	}

	void Qt5InputManager::mouseReleaseEvent(QMouseEvent* event)
	{
		if (_inputEventHandler) {
			_mouseEvent.x = event->x();
			_mouseEvent.y = theApplication().heightInt() - event->y();
			_mouseEvent._button = event->button();
			_mouseState._buttons = event->buttons();
			_inputEventHandler->OnMouseUp(_mouseEvent);
		}
	}

	void Qt5InputManager::mouseMoveEvent(QMouseEvent* event)
	{
		if (_inputEventHandler) {
			_mouseState.x = event->x();
			_mouseState.y = theApplication().heightInt() - event->y();
			_mouseState._buttons = event->buttons();
			_inputEventHandler->OnMouseMove(_mouseState);
		}
	}

	void Qt5InputManager::touchBeginEvent(QTouchEvent* event)
	{
		if (_inputEventHandler) {
			updateTouchEvent(event);
			_inputEventHandler->onTouchDown(_touchEvent);
		}
	}

	void Qt5InputManager::touchUpdateEvent(QTouchEvent* event)
	{
		if (_inputEventHandler) {
			const unsigned int previousCount = _touchEvent.count;
			updateTouchEvent(event);
			if (previousCount < _touchEvent.count)
				_inputEventHandler->onPointerDown(_touchEvent);
			else if (previousCount > _touchEvent.count)
				_inputEventHandler->onPointerUp(_touchEvent);
			else if (previousCount == _touchEvent.count)
				_inputEventHandler->onTouchMove(_touchEvent);
		}
	}

	void Qt5InputManager::touchEndEvent(QTouchEvent* event)
	{
		if (_inputEventHandler) {
			updateTouchEvent(event);
			_inputEventHandler->onTouchUp(_touchEvent);
		}
	}

	void Qt5InputManager::wheelEvent(QWheelEvent* event)
	{
		if (_inputEventHandler) {
			_scrollEvent.x = event->angleDelta().x() / 60.0f;
			_scrollEvent.y = event->angleDelta().y() / 60.0f;
			_inputEventHandler->OnScrollInput(_scrollEvent);
		}
	}

#ifdef WITH_QT5GAMEPAD
	bool Qt5InputManager::isJoyPresent(int joyId) const
	{
		return _joystickStates[joyId]._gamepad->isConnected();
	}

	const char* Qt5InputManager::joyName(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return _joystickStates[joyId]._name;
		} else {
			return nullptr;
		}
	}

	const JoystickState& Qt5InputManager::joystickState(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return _joystickStates[joyId];
		} else {
			return _nullJoystickState;
		}
	}
#endif

	void Qt5InputManager::setCursor(Cursor cursor)
	{
		if (cursor != _cursor) {
			switch (cursor) {
				case MouseCursorMode::Arrow:
					_widget.unsetCursor();
					_widget.releaseMouse();
					break;
				case MouseCursorMode::Hidden:
					_widget.setCursor(QCursor(Qt::BlankCursor));
					_widget.releaseMouse();
					break;
				case MouseCursorMode::HiddenLocked:
					_widget.grabMouse(QCursor(Qt::BlankCursor));
					break;
			}

			// Handling ImGui cursor changes
			IInputManager::setCursor(cursor);

			_cursor = cursor;
		}
	}

	void Qt5InputManager::updateTouchEvent(const QTouchEvent* event)
	{
		_touchEvent.count = event->touchPoints().size();
		for (unsigned int i = 0; i < _touchEvent.count && i < TouchEvent::MaxPointers; i++) {
			TouchEvent::Pointer& pointer = _touchEvent.pointers[i];
			const QTouchEvent::TouchPoint& touchPoint = event->touchPoints().at(i);

			pointer.id = touchPoint.id();
			pointer.x = touchPoint.pos().x();
			pointer.y = theApplication().height() - touchPoint.pos().y();
			pointer.pressure = touchPoint.pressure();
		}
	}
}

#endif
