#include "UwpInputManager.h"
#include "UwpApplication.h"
#include "../../Input/IInputEventHandler.h"
#include "../../Input/JoyMapping.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>

namespace nCine
{
	const int IInputManager::MaxNumJoysticks = 8;

	UwpMouseState UwpInputManager::mouseState_;
	UwpKeyboardState UwpInputManager::keyboardState_;

	UwpJoystickState UwpInputManager::nullJoystickState_;
	JoyButtonEvent UwpInputManager::joyButtonEvent_;
	JoyHatEvent UwpInputManager::joyHatEvent_;
	JoyAxisEvent UwpInputManager::joyAxisEvent_;
	JoyConnectionEvent UwpInputManager::joyConnectionEvent_;
	const float UwpInputManager::JoystickEventsSimulator::AxisEventTolerance = 0.001f;
	UwpInputManager::JoystickEventsSimulator UwpInputManager::joyEventsSimulator_;

	UwpInputManager::UwpGamepadInfo UwpInputManager::_gamepads[MaxNumJoysticks];
	Mutex UwpInputManager::_gamepadsSync;

	UwpInputManager::UwpInputManager()
	{
		joyMapping_.init(this);

		winrtWGI::Gamepad::GamepadAdded([](const auto&, winrtWGI::Gamepad gamepad) {
			_gamepadsSync.Lock();

			bool found = false;
			int firstFreeId = -1;
			for (int i = 0; i < _countof(_gamepads); i++) {
				auto& info = _gamepads[i];
				if (info.Connected) {
					if (!found && info.Gamepad == gamepad) {
						found = true;
					}
				} else {
					if (firstFreeId == -1) {
						firstFreeId = i;
					}
				}
			}

			if (!found) {
				auto& info = _gamepads[firstFreeId];
				info.Gamepad = gamepad;
				info.Connected = true;

				joyConnectionEvent_.joyId = firstFreeId;

				updateJoystickStates();

				if (inputEventHandler_ != nullptr) {
					joyMapping_.onJoyConnected(joyConnectionEvent_);
					inputEventHandler_->onJoyConnected(joyConnectionEvent_);
				}
			}

			_gamepadsSync.Unlock();
		});

		winrtWGI::Gamepad::GamepadRemoved([](const auto&, winrtWGI::Gamepad gamepad) {
			_gamepadsSync.Lock();

			for (int i = 0; i < _countof(_gamepads); i++) {
				auto& info = _gamepads[i];
				if (info.Connected && info.Gamepad == gamepad) {
					info.Connected = false;

					joyEventsSimulator_.resetJoystickState(i);

					if (inputEventHandler_ != nullptr) {
						inputEventHandler_->onJoyDisconnected(joyConnectionEvent_);
						joyMapping_.onJoyDisconnected(joyConnectionEvent_);
					}
					break;
				}
			}

			_gamepadsSync.Unlock();
		});

		_gamepadsSync.Lock();

		auto gamepads = winrtWGI::Gamepad::Gamepads();
		auto gamepadsSize = std::min((int)gamepads.Size(), MaxNumJoysticks);
		for (int i = 0; i < gamepadsSize; i++) {
			auto& info = _gamepads[i];
			info.Gamepad = gamepads.GetAt((uint32_t)i);
			info.Connected = true;
		}

		_gamepadsSync.Unlock();
	}

	UwpInputManager::~UwpInputManager()
	{

	}

	void UwpInputManager::setCursor(Cursor cursor)
	{
		UwpApplication::GetDispatcher().RunIdleAsync([cursor](auto args) {
			winrtWUC::CoreWindow::GetForCurrentThread().PointerCursor(cursor == Cursor::Arrow ? winrtWUC::CoreCursor(winrtWUC::CoreCursorType::Arrow, 0) : nullptr);
		});
	}

	void UwpInputManager::updateJoystickStates()
	{
		_gamepadsSync.Lock();

		for (int i = 0; i < MaxNumJoysticks; i++) {
			auto& info = _gamepads[i];
			if (info.Connected) {
				winrtWGI::GamepadReading reading = info.Gamepad.GetCurrentReading();

				joyEventsSimulator_.simulateButtonsEvents(i, reading.Buttons);
				joyEventsSimulator_.simulateHatsEvents(i, reading.Buttons);

				joyEventsSimulator_.simulateAxisEvent(i, 0, (float)reading.LeftThumbstickX);
				joyEventsSimulator_.simulateAxisEvent(i, 1, -(float)reading.LeftThumbstickY);
				joyEventsSimulator_.simulateAxisEvent(i, 2, (float)reading.RightThumbstickX);
				joyEventsSimulator_.simulateAxisEvent(i, 3, -(float)reading.RightThumbstickY);
				joyEventsSimulator_.simulateAxisEvent(i, 4, (float)reading.LeftTrigger);
				joyEventsSimulator_.simulateAxisEvent(i, 5, (float)reading.RightTrigger);
			}
		}

		_gamepadsSync.Unlock();
	}

	UwpInputManager::JoystickEventsSimulator::JoystickEventsSimulator()
	{
		std::memset(buttonsState_, 0, sizeof(bool) * MaxNumButtons * MaxNumJoysticks);
		std::memset(axesValuesState_, 0, sizeof(float) * MaxNumAxes * MaxNumJoysticks);
	}

	void UwpInputManager::JoystickEventsSimulator::resetJoystickState(int joyId)
	{
		std::memset(buttonsState_[joyId], 0, sizeof(bool) * MaxNumButtons);
		std::memset(axesValuesState_[joyId], 0, sizeof(float) * MaxNumAxes);
	}

	void UwpInputManager::JoystickEventsSimulator::simulateButtonsEvents(int joyId, winrtWGI::GamepadButtons buttons)
	{
		constexpr winrtWGI::GamepadButtons Mapping[MaxNumButtons] = {
			winrtWGI::GamepadButtons::A, winrtWGI::GamepadButtons::B, winrtWGI::GamepadButtons::X, winrtWGI::GamepadButtons::Y,
			winrtWGI::GamepadButtons::LeftShoulder, winrtWGI::GamepadButtons::RightShoulder,
			winrtWGI::GamepadButtons::View, winrtWGI::GamepadButtons::Menu,
			winrtWGI::GamepadButtons::LeftThumbstick, winrtWGI::GamepadButtons::RightThumbstick,
			winrtWGI::GamepadButtons::None /*Guide*/
		};

		for (int i = 0; i < _countof(Mapping); i++) {
			if (Mapping[i] == winrtWGI::GamepadButtons::None) {
				continue;
			}

			bool isPressed = (buttons & Mapping[i]) != winrtWGI::GamepadButtons::None;
			if (inputEventHandler_ != nullptr && isPressed != buttonsState_[joyId][i]) {
				joyButtonEvent_.joyId = joyId;
				joyButtonEvent_.buttonId = i;
				if (isPressed) {
					joyMapping_.onJoyButtonPressed(joyButtonEvent_);
					inputEventHandler_->onJoyButtonPressed(joyButtonEvent_);
				} else {
					joyMapping_.onJoyButtonReleased(joyButtonEvent_);
					inputEventHandler_->onJoyButtonReleased(joyButtonEvent_);
				}
			}

			buttonsState_[joyId][i] = isPressed;
		}
	}

	void UwpInputManager::JoystickEventsSimulator::simulateHatsEvents(int joyId, winrtWGI::GamepadButtons buttons)
	{
		constexpr winrtWGI::GamepadButtons Mapping[4] = {
			winrtWGI::GamepadButtons::DPadUp, winrtWGI::GamepadButtons::DPadRight,
			winrtWGI::GamepadButtons::DPadDown, winrtWGI::GamepadButtons::DPadLeft
		};

		unsigned char hatState = 0;
		for (int i = 0; i < _countof(Mapping); i++) {
			bool isPressed = (buttons & Mapping[i]) != winrtWGI::GamepadButtons::None;
			if (isPressed) {
				hatState |= (1 << i);
			}
		}

		if (inputEventHandler_ != nullptr && hatsState_[joyId][0] != hatState) {
			joyHatEvent_.joyId = joyId;
			joyHatEvent_.hatId = 0;
			joyHatEvent_.hatState = hatState;

			joyMapping_.onJoyHatMoved(joyHatEvent_);
			inputEventHandler_->onJoyHatMoved(joyHatEvent_);
		}

		hatsState_[joyId][0] = hatState;
	}

	void UwpInputManager::JoystickEventsSimulator::simulateAxisEvent(int joyId, int axisId, float value)
	{
		if (inputEventHandler_ != nullptr && std::abs(axesValuesState_[joyId][axisId] - value) > AxisEventTolerance) {
			joyAxisEvent_.joyId = joyId;
			joyAxisEvent_.axisId = axisId;
			joyAxisEvent_.value = static_cast<short int>(value * MaxAxisValue);
			joyAxisEvent_.normValue = value;
			joyMapping_.onJoyAxisMoved(joyAxisEvent_);
			inputEventHandler_->onJoyAxisMoved(joyAxisEvent_);
		}

		axesValuesState_[joyId][axisId] = value;
	}
}
