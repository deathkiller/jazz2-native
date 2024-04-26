#if defined(WITH_QT5)

#include "../../Common.h"
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
	const int IInputManager::MaxNumJoysticks = 4;

	TouchEvent Qt5InputManager::touchEvent_;
	Qt5MouseState Qt5InputManager::mouseState_;
	Qt5MouseEvent Qt5InputManager::mouseEvent_;
	Qt5ScrollEvent Qt5InputManager::scrollEvent_;
	Qt5KeyboardState Qt5InputManager::keyboardState_;
	KeyboardEvent Qt5InputManager::keyboardEvent_;
	TextInputEvent Qt5InputManager::textInputEvent_;
	Qt5JoystickState Qt5InputManager::nullJoystickState_;

#if defined(WITH_QT5GAMEPAD)
	Qt5JoystickState Qt5InputManager::joystickStates_[MaxNumJoysticks];
	JoyButtonEvent Qt5InputManager::joyButtonEvent_;
	JoyHatEvent Qt5InputManager::joyHatEvent_;
	JoyAxisEvent Qt5InputManager::joyAxisEvent_;
	JoyConnectionEvent Qt5InputManager::joyConnectionEvent_;
	const float Qt5JoystickState::AxisEventTolerance = 0.001f;
#endif

	Qt5InputManager::Qt5InputManager(Qt5Widget& widget)
		: widget_(widget)
	{
#if defined(WITH_QT5GAMEPAD)
		for (int i = 0; i < MaxNumJoysticks; i++) {
			joystickStates_[i].gamepad_ = std::make_unique<QGamepad>(-1);
		}
#endif

		joyMapping_.init(this);

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
			buttonState_[i] = false;
		}
		hatState_ = HatState::CENTERED;
		for (unsigned int i = 0; i < NumAxes; i++) {
			axesValuesState_[i] = 0.0f;
		}
	}

	bool Qt5JoystickState::isButtonPressed(int buttonId) const
	{
		switch (buttonId) {
			case 0: return gamepad_->buttonL1();
			case 1: return gamepad_->buttonL3();
			case 2: return gamepad_->buttonR1();
			case 3: return gamepad_->buttonR3();
			case 4: return gamepad_->buttonA();
			case 5: return gamepad_->buttonB();
			case 6: return gamepad_->buttonCenter();
			case 7: return gamepad_->buttonGuide();
			case 8: return gamepad_->buttonSelect();
			case 9: return gamepad_->buttonStart();
			case 10: return gamepad_->buttonX();
			case 11: return gamepad_->buttonY();
			default: return false;
		}
	}

	unsigned char Qt5JoystickState::hatState(int hatId) const
	{
		unsigned char state = HatState::CENTERED;

		if (hatId == 0) {
			if (gamepad_->buttonUp())
				state += HatState::UP;
			else if (gamepad_->buttonDown())
				state += HatState::DOWN;

			if (gamepad_->buttonRight())
				state += HatState::RIGHT;
			else if (gamepad_->buttonLeft())
				state += HatState::LEFT;
		}

		return state;
	}

	float Qt5JoystickState::axisValue(int axisId) const
	{
		switch (axisId) {
			case 0: return static_cast<float>(gamepad_->axisLeftX());
			case 1: return static_cast<float>(gamepad_->axisLeftY());
			case 2: return static_cast<float>(gamepad_->axisRightX());
			case 3: return static_cast<float>(gamepad_->axisRightY());
			case 4: return static_cast<float>(2.0 * gamepad_->buttonL2() - 1.0);
			case 5: return static_cast<float>(2.0 * gamepad_->buttonR2() - 1.0);
			default: return 0.0f;
		}
	}

	void Qt5InputManager::updateJoystickStates()
	{
		// Compacting the array of Qt5 connected gamepads
		int notConnectedIndex = 0;
		int connectedIndex = 0;
		while (notConnectedIndex < MaxNumJoysticks) {
			QGamepad& notConnected = *joystickStates_[notConnectedIndex].gamepad_;
			QGamepad& connected = *joystickStates_[connectedIndex].gamepad_;
			if (notConnected.isConnected() && connected.isConnected() == false) {
				const int notConnectedId = notConnected.deviceId();
				notConnected.setDeviceId(connected.deviceId());
				connected.setDeviceId(notConnectedId);

				connectedIndex++;
			}

			notConnectedIndex++;
		}

		if (inputEventHandler_) {
			for (int i = 0; i < MaxNumJoysticks; i++) {
				if (joystickStates_[i].gamepad_->deviceId() > -1 &&
					joystickStates_[i].gamepad_->isConnected() == false) {
					joystickStates_[i].gamepad_->setDeviceId(-1);
					joyConnectionEvent_.joyId = i;
					inputEventHandler_->OnJoyDisconnected(joyConnectionEvent_);
					joyMapping_.onJoyDisconnected(joyConnectionEvent_);
				}
			}
		}

		const QList<int> gamepads = QGamepadManager::instance()->connectedGamepads();
		int index = 0;
		for (int deviceId : gamepads) {
			const int oldDeviceId = joystickStates_[index].gamepad_->deviceId();

			if (inputEventHandler_ && oldDeviceId != deviceId) {
				joystickStates_[index].gamepad_->setDeviceId(deviceId);
				joyConnectionEvent_.joyId = index;
				joyMapping_.OnJoyConnected(joyConnectionEvent_);
				inputEventHandler_->onJoyConnected(joyConnectionEvent_);
			}

			index++;
			if (index >= MaxNumJoysticks)
				break;
		}

		for (int joyId = 0; joyId < MaxNumJoysticks; joyId++) {
			Qt5JoystickState& state = joystickStates_[joyId];
			if (state.gamepad_ == nullptr)
				continue;

			for (int buttonId = 0; buttonId < Qt5JoystickState::NumButtons; buttonId++) {
				const bool newButtonState = state.isButtonPressed(buttonId);
				if (state.buttonState_[buttonId] != newButtonState) {
					state.buttonState_[buttonId] = newButtonState;
					if (inputEventHandler_ != nullptr) {
						joyButtonEvent_.joyId = joyId;
						joyButtonEvent_.buttonId = buttonId;
						if (newButtonState) {
							joyMapping_.onJoyButtonPressed(joyButtonEvent_);
							inputEventHandler_->OnJoyButtonPressed(joyButtonEvent_);
						} else {
							joyMapping_.onJoyButtonReleased(joyButtonEvent_);
							inputEventHandler_->OnJoyButtonReleased(joyButtonEvent_);
						}
					}
				}
			}

			const unsigned char newHatState = state.hatState(0);
			if (state.hatState_ != newHatState) {
				state.hatState_ = newHatState;
				if (inputEventHandler_ != nullptr) {
					joyHatEvent_.joyId = joyId;
					joyHatEvent_.hatId = 0;
					joyHatEvent_.hatState = newHatState;
					joyMapping_.onJoyHatMoved(joyHatEvent_);
					inputEventHandler_->OnJoyHatMoved(joyHatEvent_);
				}
			}

			for (int axisId = 0; axisId < Qt5JoystickState::NumAxes; axisId++) {
				const float newAxisValue = state.axisValue(axisId);
				if (fabsf(state.axesValuesState_[axisId] - newAxisValue) > Qt5JoystickState::AxisEventTolerance) {
					state.axesValuesState_[axisId] = newAxisValue;
					if (inputEventHandler_) {
						joyAxisEvent_.joyId = joyId;
						joyAxisEvent_.axisId = axisId;
						joyAxisEvent_.value = newAxisValue;
						joyMapping_.onJoyAxisMoved(joyAxisEvent_);
						inputEventHandler_->OnJoyAxisMoved(joyAxisEvent_);
					}
				}
			}
		}
	}
#endif

	bool Qt5InputManager::shouldQuitOnRequest()
	{
		return (inputEventHandler_ != nullptr && inputEventHandler_->OnQuitRequest());
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
		if (inputEventHandler_) {
			keyboardEvent_.scancode = static_cast<int>(event->nativeScanCode());
			keyboardEvent_.sym = Qt5Keys::keySymValueToEnum(event->key());
			keyboardEvent_.mod = Qt5Keys::keyModMaskToEnumMask(event->modifiers());
			if (keyboardEvent_.sym != KeySym::UNKNOWN) {
				const unsigned int keySym = static_cast<unsigned int>(keyboardEvent_.sym);
				keyboardState_.keys_[keySym] = 1;
			}
			inputEventHandler_->OnKeyPressed(keyboardEvent_);

			if (event->text().length() > 0) {
				nctl::strncpy(textInputEvent_.text, event->text().toUtf8().constData(), 4);
				inputEventHandler_->OnTextInput(textInputEvent_);
			}
		}
	}

	void Qt5InputManager::keyReleaseEvent(QKeyEvent* event)
	{
		if (inputEventHandler_) {
			keyboardEvent_.scancode = static_cast<int>(event->nativeScanCode());
			keyboardEvent_.sym = Qt5Keys::keySymValueToEnum(event->key());
			keyboardEvent_.mod = Qt5Keys::keyModMaskToEnumMask(event->modifiers());
			if (keyboardEvent_.sym != KeySym::UNKNOWN) {
				const unsigned int keySym = static_cast<unsigned int>(keyboardEvent_.sym);
				keyboardState_.keys_[keySym] = 0;
			}
			inputEventHandler_->OnKeyReleased(keyboardEvent_);
		}
	}

	void Qt5InputManager::mousePressEvent(QMouseEvent* event)
	{
		if (inputEventHandler_) {
			mouseEvent_.x = event->x();
			mouseEvent_.y = theApplication().heightInt() - event->y();
			mouseEvent_.button_ = event->button();
			mouseState_.buttons_ = event->buttons();
			inputEventHandler_->OnMouseDown(mouseEvent_);
		}
	}

	void Qt5InputManager::mouseReleaseEvent(QMouseEvent* event)
	{
		if (inputEventHandler_) {
			mouseEvent_.x = event->x();
			mouseEvent_.y = theApplication().heightInt() - event->y();
			mouseEvent_.button_ = event->button();
			mouseState_.buttons_ = event->buttons();
			inputEventHandler_->OnMouseUp(mouseEvent_);
		}
	}

	void Qt5InputManager::mouseMoveEvent(QMouseEvent* event)
	{
		if (inputEventHandler_) {
			mouseState_.x = event->x();
			mouseState_.y = theApplication().heightInt() - event->y();
			mouseState_.buttons_ = event->buttons();
			inputEventHandler_->OnMouseMove(mouseState_);
		}
	}

	void Qt5InputManager::touchBeginEvent(QTouchEvent* event)
	{
		if (inputEventHandler_) {
			updateTouchEvent(event);
			inputEventHandler_->onTouchDown(touchEvent_);
		}
	}

	void Qt5InputManager::touchUpdateEvent(QTouchEvent* event)
	{
		if (inputEventHandler_) {
			const unsigned int previousCount = touchEvent_.count;
			updateTouchEvent(event);
			if (previousCount < touchEvent_.count)
				inputEventHandler_->onPointerDown(touchEvent_);
			else if (previousCount > touchEvent_.count)
				inputEventHandler_->onPointerUp(touchEvent_);
			else if (previousCount == touchEvent_.count)
				inputEventHandler_->onTouchMove(touchEvent_);
		}
	}

	void Qt5InputManager::touchEndEvent(QTouchEvent* event)
	{
		if (inputEventHandler_) {
			updateTouchEvent(event);
			inputEventHandler_->onTouchUp(touchEvent_);
		}
	}

	void Qt5InputManager::wheelEvent(QWheelEvent* event)
	{
		if (inputEventHandler_) {
			scrollEvent_.x = event->angleDelta().x() / 60.0f;
			scrollEvent_.y = event->angleDelta().y() / 60.0f;
			inputEventHandler_->OnScrollInput(scrollEvent_);
		}
	}

#ifdef WITH_QT5GAMEPAD
	bool Qt5InputManager::isJoyPresent(int joyId) const
	{
		return joystickStates_[joyId].gamepad_->isConnected();
	}

	const char* Qt5InputManager::joyName(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return joystickStates_[joyId].name_;
		} else {
			return nullptr;
		}
	}

	const JoystickState& Qt5InputManager::joystickState(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return joystickStates_[joyId];
		} else {
			return nullJoystickState_;
		}
	}
#endif

	void Qt5InputManager::setCursor(Cursor cursor)
	{
		if (cursor != cursor_) {
			switch (cursor) {
				case MouseCursorMode::Arrow:
					widget_.unsetCursor();
					widget_.releaseMouse();
					break;
				case MouseCursorMode::Hidden:
					widget_.setCursor(QCursor(Qt::BlankCursor));
					widget_.releaseMouse();
					break;
				case MouseCursorMode::HiddenLocked:
					widget_.grabMouse(QCursor(Qt::BlankCursor));
					break;
			}

			// Handling ImGui cursor changes
			IInputManager::setCursor(cursor);

			cursor_ = cursor;
		}
	}

	void Qt5InputManager::updateTouchEvent(const QTouchEvent* event)
	{
		touchEvent_.count = event->touchPoints().size();
		for (unsigned int i = 0; i < touchEvent_.count && i < TouchEvent::MaxPointers; i++) {
			TouchEvent::Pointer& pointer = touchEvent_.pointers[i];
			const QTouchEvent::TouchPoint& touchPoint = event->touchPoints().at(i);

			pointer.id = touchPoint.id();
			pointer.x = touchPoint.pos().x();
			pointer.y = theApplication().height() - touchPoint.pos().y();
			pointer.pressure = touchPoint.pressure();
		}
	}
}

#endif
