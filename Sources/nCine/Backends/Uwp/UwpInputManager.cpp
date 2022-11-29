#include "UwpInputManager.h"
#include "UwpApplication.h"
#include "../../Input/IInputEventHandler.h"
#include "../../Input/JoyMapping.h"

#include <winrt/Windows.Foundation.Collections.h>

namespace nCine
{
	const int IInputManager::MaxNumJoysticks = 8;

	UwpMouseState UwpInputManager::mouseState_;
	UwpKeyboardState UwpInputManager::keyboardState_;
	KeyboardEvent UwpInputManager::keyboardEvent_;
	UwpJoystickState UwpInputManager::nullJoystickState_;
	JoyButtonEvent UwpInputManager::joyButtonEvent_;
	JoyHatEvent UwpInputManager::joyHatEvent_;
	JoyAxisEvent UwpInputManager::joyAxisEvent_;
	JoyConnectionEvent UwpInputManager::joyConnectionEvent_;
	const float UwpInputManager::JoystickEventsSimulator::AxisEventTolerance = 0.001f;
	UwpInputManager::JoystickEventsSimulator UwpInputManager::joyEventsSimulator_;

	UwpInputManager::UwpGamepadInfo UwpInputManager::_gamepads[MaxNumJoysticks];
	Mutex UwpInputManager::_gamepadsSync;

	UwpInputManager::UwpInputManager(winrtWUC::CoreWindow window)
	{
		joyMapping_.init(this);

		window.KeyDown({ this, &UwpInputManager::OnKeyDown });
		window.KeyUp({ this, &UwpInputManager::OnKeyUp });

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
		//UwpApplication::GetDispatcher().RunIdleAsync([cursor](auto args) {
			winrtWUC::CoreWindow::GetForCurrentThread().PointerCursor(cursor == Cursor::Arrow ? winrtWUC::CoreCursor(winrtWUC::CoreCursorType::Arrow, 0) : nullptr);
		//});
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
				joyEventsSimulator_.simulateAxisEvent(i, 4, ((float)reading.LeftTrigger * 2.0f) - 1.0f);
				joyEventsSimulator_.simulateAxisEvent(i, 5, ((float)reading.RightTrigger * 2.0f) - 1.0f);
			}
		}

		_gamepadsSync.Unlock();
	}

	void UwpInputManager::OnKeyDown(const winrtWF::IInspectable& sender, const winrtWUC::KeyEventArgs& args)
	{
		if (inputEventHandler_ != nullptr) {
			args.Handled(true);

			keyboardEvent_.scancode = args.KeyStatus().ScanCode;
			keyboardEvent_.sym = keySymValueToEnum(args.VirtualKey());
			// TODO: Key modifiers
			keyboardEvent_.mod = /*GlfwKeys::keyModMaskToEnumMask(mods)*/0;

			if (keyboardEvent_.sym < KeySym::COUNT) {
				keyboardState_._pressedKeys[(int)keyboardEvent_.sym] = true;
			}

			// TODO: Probably called from UI thread
			inputEventHandler_->onKeyPressed(keyboardEvent_);
		}
	}

	void UwpInputManager::OnKeyUp(const winrtWF::IInspectable& sender, const winrtWUC::KeyEventArgs& args)
	{
		if (inputEventHandler_ != nullptr) {
			args.Handled(true);

			keyboardEvent_.scancode = args.KeyStatus().ScanCode;
			keyboardEvent_.sym = keySymValueToEnum(args.VirtualKey());
			// TODO: Key modifiers
			keyboardEvent_.mod = /*keyModMaskToEnumMask(mods)*/0;

			if (keyboardEvent_.sym < KeySym::COUNT) {
				keyboardState_._pressedKeys[(int)keyboardEvent_.sym] = false;
			}

			// TODO: Probably called from UI thread
			inputEventHandler_->onKeyReleased(keyboardEvent_);
		}
	}

#pragma push_macro("DELETE")
#undef DELETE

	KeySym UwpInputManager::keySymValueToEnum(winrtWS::VirtualKey virtualKey)
	{
		switch (virtualKey) {
			case winrtWS::VirtualKey::Back:				return KeySym::BACKSPACE;
			case winrtWS::VirtualKey::Tab:				return KeySym::TAB;
			case winrtWS::VirtualKey::Enter:			return KeySym::RETURN;
			case winrtWS::VirtualKey::Escape:			return KeySym::ESCAPE;
			case winrtWS::VirtualKey::Space:			return KeySym::SPACE;
			case (winrtWS::VirtualKey)VK_OEM_7:			return KeySym::QUOTE;
			case (winrtWS::VirtualKey)VK_OEM_PLUS:		return KeySym::PLUS;
			case (winrtWS::VirtualKey)VK_OEM_COMMA:		return KeySym::COMMA;
			case (winrtWS::VirtualKey)VK_OEM_MINUS:		return KeySym::MINUS;
			case (winrtWS::VirtualKey)VK_OEM_PERIOD:	return KeySym::PERIOD;
			case (winrtWS::VirtualKey)VK_OEM_2:			return KeySym::SLASH;
			case winrtWS::VirtualKey::Number0:			return KeySym::N0;
			case winrtWS::VirtualKey::Number1:			return KeySym::N1;
			case winrtWS::VirtualKey::Number2:			return KeySym::N2;
			case winrtWS::VirtualKey::Number3:			return KeySym::N3;
			case winrtWS::VirtualKey::Number4:			return KeySym::N4;
			case winrtWS::VirtualKey::Number5:			return KeySym::N5;
			case winrtWS::VirtualKey::Number6:			return KeySym::N6;
			case winrtWS::VirtualKey::Number7:			return KeySym::N7;
			case winrtWS::VirtualKey::Number8:			return KeySym::N8;
			case winrtWS::VirtualKey::Number9:			return KeySym::N9;
			case (winrtWS::VirtualKey)VK_OEM_1:			return KeySym::SEMICOLON;

			case (winrtWS::VirtualKey)VK_OEM_4:			return KeySym::LEFTBRACKET;
			case (winrtWS::VirtualKey)VK_OEM_5:			return KeySym::BACKSLASH;
			case (winrtWS::VirtualKey)VK_OEM_6:			return KeySym::RIGHTBRACKET;
			case (winrtWS::VirtualKey)VK_OEM_3:			return KeySym::BACKQUOTE;
			//case winrtWS::VirtualKey::GLFW_KEY_WORLD_1:	return KeySym::WORLD1;
			//case winrtWS::VirtualKey::GLFW_KEY_WORLD_2:	return KeySym::WORLD2;
			case winrtWS::VirtualKey::A:				return KeySym::A;
			case winrtWS::VirtualKey::B:				return KeySym::B;
			case winrtWS::VirtualKey::C:				return KeySym::C;
			case winrtWS::VirtualKey::D:				return KeySym::D;
			case winrtWS::VirtualKey::E:				return KeySym::E;
			case winrtWS::VirtualKey::F:				return KeySym::F;
			case winrtWS::VirtualKey::G:				return KeySym::G;
			case winrtWS::VirtualKey::H:				return KeySym::H;
			case winrtWS::VirtualKey::I:				return KeySym::I;
			case winrtWS::VirtualKey::J:				return KeySym::J;
			case winrtWS::VirtualKey::K:				return KeySym::K;
			case winrtWS::VirtualKey::L:				return KeySym::L;
			case winrtWS::VirtualKey::M:				return KeySym::M;
			case winrtWS::VirtualKey::N:				return KeySym::N;
			case winrtWS::VirtualKey::O:				return KeySym::O;
			case winrtWS::VirtualKey::P:				return KeySym::P;
			case winrtWS::VirtualKey::Q:				return KeySym::Q;
			case winrtWS::VirtualKey::R:				return KeySym::R;
			case winrtWS::VirtualKey::S:				return KeySym::S;
			case winrtWS::VirtualKey::T:				return KeySym::T;
			case winrtWS::VirtualKey::U:				return KeySym::U;
			case winrtWS::VirtualKey::V:				return KeySym::V;
			case winrtWS::VirtualKey::W:				return KeySym::W;
			case winrtWS::VirtualKey::X:				return KeySym::X;
			case winrtWS::VirtualKey::Y:				return KeySym::Y;
			case winrtWS::VirtualKey::Z:				return KeySym::Z;
			case winrtWS::VirtualKey::Delete:			return KeySym::DELETE;

			case winrtWS::VirtualKey::NumberPad0:		return KeySym::KP0;
			case winrtWS::VirtualKey::NumberPad1:		return KeySym::KP1;
			case winrtWS::VirtualKey::NumberPad2:		return KeySym::KP2;
			case winrtWS::VirtualKey::NumberPad3:		return KeySym::KP3;
			case winrtWS::VirtualKey::NumberPad4:		return KeySym::KP4;
			case winrtWS::VirtualKey::NumberPad5:		return KeySym::KP5;
			case winrtWS::VirtualKey::NumberPad6:		return KeySym::KP6;
			case winrtWS::VirtualKey::NumberPad7:		return KeySym::KP7;
			case winrtWS::VirtualKey::NumberPad8:		return KeySym::KP8;
			case winrtWS::VirtualKey::NumberPad9:		return KeySym::KP9;
			case winrtWS::VirtualKey::Decimal:			return KeySym::KP_PERIOD;
			case winrtWS::VirtualKey::Divide:			return KeySym::KP_DIVIDE;
			case winrtWS::VirtualKey::Multiply:			return KeySym::KP_MULTIPLY;
			case winrtWS::VirtualKey::Subtract:			return KeySym::KP_MINUS;
			case winrtWS::VirtualKey::Add:				return KeySym::KP_PLUS;
			//case (winrtWS::VirtualKey)VK_RETURN:		return KeySym::KP_ENTER;
			//case winrtWS::VirtualKey::GLFW_KEY_KP_EQUAL:	return KeySym::KP_EQUALS;

			case winrtWS::VirtualKey::Up:				return KeySym::UP;
			case winrtWS::VirtualKey::Down:				return KeySym::DOWN;
			case winrtWS::VirtualKey::Right:			return KeySym::RIGHT;
			case winrtWS::VirtualKey::Left:				return KeySym::LEFT;
			case winrtWS::VirtualKey::Insert:			return KeySym::INSERT;
			case winrtWS::VirtualKey::Home:				return KeySym::HOME;
			case winrtWS::VirtualKey::End:				return KeySym::END;
			case winrtWS::VirtualKey::PageUp:			return KeySym::PAGEUP;
			case winrtWS::VirtualKey::PageDown:			return KeySym::PAGEDOWN;

			case winrtWS::VirtualKey::F1:				return KeySym::F1;
			case winrtWS::VirtualKey::F2:				return KeySym::F2;
			case winrtWS::VirtualKey::F3:				return KeySym::F3;
			case winrtWS::VirtualKey::F4:				return KeySym::F4;
			case winrtWS::VirtualKey::F5:				return KeySym::F5;
			case winrtWS::VirtualKey::F6:				return KeySym::F6;
			case winrtWS::VirtualKey::F7:				return KeySym::F7;
			case winrtWS::VirtualKey::F8:				return KeySym::F8;
			case winrtWS::VirtualKey::F9:				return KeySym::F9;
			case winrtWS::VirtualKey::F10:				return KeySym::F10;
			case winrtWS::VirtualKey::F11:				return KeySym::F11;
			case winrtWS::VirtualKey::F12:				return KeySym::F12;
			case winrtWS::VirtualKey::F13:				return KeySym::F13;
			case winrtWS::VirtualKey::F14:				return KeySym::F14;
			case winrtWS::VirtualKey::F15:				return KeySym::F15;

			case winrtWS::VirtualKey::NumberKeyLock:	return KeySym::NUM_LOCK;
			case winrtWS::VirtualKey::CapitalLock:		return KeySym::CAPS_LOCK;
			case winrtWS::VirtualKey::Scroll:			return KeySym::SCROLL_LOCK;
			case winrtWS::VirtualKey::RightShift:		return KeySym::RSHIFT;
			case winrtWS::VirtualKey::LeftShift:		return KeySym::LSHIFT;
			case winrtWS::VirtualKey::RightControl:		return KeySym::RCTRL;
			case winrtWS::VirtualKey::LeftControl:		return KeySym::LCTRL;
			case winrtWS::VirtualKey::RightMenu:		return KeySym::RALT;
			case winrtWS::VirtualKey::LeftMenu:			return KeySym::LALT;
			case winrtWS::VirtualKey::RightWindows:		return KeySym::RSUPER;
			case winrtWS::VirtualKey::LeftWindows:		return KeySym::LSUPER;

			case winrtWS::VirtualKey::Print:			return KeySym::PRINTSCREEN;
			case winrtWS::VirtualKey::Pause:			return KeySym::PAUSE;
			case winrtWS::VirtualKey::Application:		return KeySym::MENU;

			case winrtWS::VirtualKey::ModeChange:		return KeySym::MODE;
			case winrtWS::VirtualKey::Help:				return KeySym::HELP;

			default:									return KeySym::UNKNOWN;
		}
	}

#pragma pop_macro("DELETE")

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
