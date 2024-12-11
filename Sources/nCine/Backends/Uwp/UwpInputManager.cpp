#include "UwpInputManager.h"
#include "UwpApplication.h"
#include "../../Input/IInputEventHandler.h"
#include "../../Input/JoyMapping.h"

#include <winrt/Windows.Foundation.Collections.h>

#include <Environment.h>

using namespace Death;

namespace nCine
{
	const int IInputManager::MaxNumJoysticks = 8;

	UwpMouseState UwpInputManager::mouseState_;
	UwpKeyboardState UwpInputManager::keyboardState_;
	KeyboardEvent UwpInputManager::keyboardEvent_;
	UwpJoystickState UwpInputManager::nullJoystickState_;
	JoyButtonEvent UwpJoystickState::joyButtonEvent_;
	JoyHatEvent UwpJoystickState::joyHatEvent_;
	JoyAxisEvent UwpJoystickState::joyAxisEvent_;
	JoyConnectionEvent UwpInputManager::joyConnectionEvent_;

	UwpInputManager::UwpGamepadInfo UwpInputManager::_gamepads[MaxNumJoysticks];
	ReadWriteLock UwpInputManager::_gamepadsSync;

	UwpInputManager::UwpInputManager(winrtWUC::CoreWindow window)
	{
		joyMapping_.Init(this);

		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			_gamepads[i].State.resetJoystickState(i);
		}

		window.KeyDown({ this, &UwpInputManager::OnKey });
		window.KeyUp({ this, &UwpInputManager::OnKey });
		window.Dispatcher().AcceleratorKeyActivated({ this, &UwpInputManager::OnAcceleratorKeyActivated });

		winrtWGI::Gamepad::GamepadAdded({ this, &UwpInputManager::OnGamepadAdded });
		winrtWGI::Gamepad::GamepadRemoved({ this, &UwpInputManager::OnGamepadRemoved });

		_gamepadsSync.EnterWriteLock();

		auto gamepads = winrtWGI::Gamepad::Gamepads();
		auto gamepadsSize = std::min((std::int32_t)gamepads.Size(), MaxNumJoysticks);
		for (std::int32_t i = 0; i < gamepadsSize; i++) {
			auto& info = _gamepads[i];
			info.Gamepad = gamepads.GetAt((uint32_t)i);
			info.Connected = true;
		}

		_gamepadsSync.ExitWriteLock();
	}

	UwpInputManager::~UwpInputManager()
	{
		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			auto& info = _gamepads[i];
			if (info.Connected && (info.RumbleExpiration > 0 || info.RumbleTriggersExpiration > 0)) {
				auto vibration = info.Gamepad.Vibration();
				vibration.LeftMotor = 0.0f;
				vibration.RightMotor = 0.0f;
				vibration.RightTrigger = 0.0f;
				vibration.RightTrigger = 0.0f;
				info.Gamepad.Vibration(vibration);
			}
		}
	}
	
	bool UwpInputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, uint32_t durationMs)
	{
		bool result = false;

		_gamepadsSync.EnterWriteLock();

		if (joyId < MaxNumJoysticks && _gamepads[joyId].Connected) {
			auto& info = _gamepads[joyId];
			if (info.RumbleLowFrequency != lowFrequency || info.RumbleHighFrequency != highFrequency) {
				info.RumbleLowFrequency = lowFrequency;
				info.RumbleHighFrequency = highFrequency;

				auto vibration = info.Gamepad.Vibration();
				vibration.LeftMotor = lowFrequency;
				vibration.RightMotor = highFrequency;
				info.Gamepad.Vibration(vibration);
			}
			if (lowFrequency > 0.0f || highFrequency > 0.0f) {
				if (durationMs <= 0) {
					durationMs = 1;
				}
				info.RumbleExpiration = Environment::QueryUnbiasedInterruptTime() + (durationMs * 10000LL);
			} else {
				info.RumbleExpiration = 0;
			}
			result = true;
		}

		_gamepadsSync.ExitWriteLock();

		return result;
	}

	bool UwpInputManager::joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs)
	{
		bool result = false;

		_gamepadsSync.EnterWriteLock();

		if (joyId < MaxNumJoysticks && _gamepads[joyId].Connected) {
			auto& info = _gamepads[joyId];
			if (info.RumbleLeftTrigger != left || info.RumbleRightTrigger != right) {
				info.RumbleLeftTrigger = left;
				info.RumbleRightTrigger = right;

				auto vibration = info.Gamepad.Vibration();
				vibration.LeftTrigger = left;
				vibration.RightTrigger = right;
				info.Gamepad.Vibration(vibration);
			}
			if (left > 0.0f || right > 0.0f) {
				if (durationMs <= 0) {
					durationMs = 1;
				}
				info.RumbleTriggersExpiration = Environment::QueryUnbiasedInterruptTime() + (durationMs * 10000LL);
			} else {
				info.RumbleTriggersExpiration = 0;
			}
			result = true;
		}

		_gamepadsSync.ExitWriteLock();

		return result;
	}

	void UwpInputManager::setCursor(Cursor cursor)
	{
		//UwpApplication::GetDispatcher().RunIdleAsync([cursor](auto args) {
			winrtWUC::CoreWindow::GetForCurrentThread().PointerCursor(cursor == Cursor::Arrow ? winrtWUC::CoreCursor(winrtWUC::CoreCursorType::Arrow, 0) : nullptr);
		//});
	}

	void UwpInputManager::updateJoystickStates()
	{
		_gamepadsSync.EnterReadLock();

		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			auto& info = _gamepads[i];
			if (info.Connected) {
				winrtWGI::GamepadReading reading = info.Gamepad.GetCurrentReading();

				info.State.simulateButtonsEvents(reading.Buttons);
				info.State.simulateHatsEvents(reading.Buttons);

				info.State.simulateAxisEvent(0, (float)reading.LeftThumbstickX);
				info.State.simulateAxisEvent(1, -(float)reading.LeftThumbstickY);
				info.State.simulateAxisEvent(2, (float)reading.RightThumbstickX);
				info.State.simulateAxisEvent(3, -(float)reading.RightThumbstickY);
				info.State.simulateAxisEvent(4, ((float)reading.LeftTrigger * 2.0f) - 1.0f);
				info.State.simulateAxisEvent(5, ((float)reading.RightTrigger * 2.0f) - 1.0f);

				if (info.RumbleExpiration > 0 || info.RumbleTriggersExpiration > 0) {
					uint64_t now = Environment::QueryUnbiasedInterruptTime();
					if (now >= info.RumbleExpiration || now >= info.RumbleTriggersExpiration) {
						auto vibration = info.Gamepad.Vibration();

						if (info.RumbleExpiration > 0 && now >= info.RumbleExpiration) {
							info.RumbleLowFrequency = 0.0f;
							info.RumbleHighFrequency = 0.0f;
							info.RumbleExpiration = 0;

							vibration.LeftMotor = 0.0f;
							vibration.RightMotor = 0.0f;
						}
						if (info.RumbleTriggersExpiration > 0 && now >= info.RumbleTriggersExpiration) {
							info.RumbleLeftTrigger = 0.0f;
							info.RumbleRightTrigger = 0.0f;
							info.RumbleTriggersExpiration = 0;

							vibration.LeftTrigger = 0.0f;
							vibration.RightTrigger = 0.0f;
						}

						info.Gamepad.Vibration(vibration);
					}
				}
			}
		}

		_gamepadsSync.ExitReadLock();
	}

	void UwpInputManager::OnKey(const winrtWUC::CoreWindow& sender, const winrtWUC::KeyEventArgs& args)
	{
		if (inputEventHandler_ != nullptr) {
			args.Handled(true);

			keyboardEvent_.scancode = args.KeyStatus().ScanCode;
			keyboardEvent_.sym = keySymValueToEnum(args.VirtualKey());
			if (keyboardEvent_.sym >= KeySym::COUNT) {
				return;
			}

			std::int32_t mod = 0;
			if ((sender.GetKeyState(winrtWS::VirtualKey::Shift) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::LShift;
			if ((sender.GetKeyState(winrtWS::VirtualKey::Control) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::LCtrl;
			if ((sender.GetKeyState(winrtWS::VirtualKey::Menu) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::LAlt;
			if ((sender.GetKeyState(winrtWS::VirtualKey::LeftWindows) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked ||
				(sender.GetKeyState(winrtWS::VirtualKey::RightWindows) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::LSuper;
			if ((sender.GetKeyState(winrtWS::VirtualKey::CapitalLock) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::CapsLock;
			if ((sender.GetKeyState(winrtWS::VirtualKey::NumberKeyLock) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::NumLock;
			if ((sender.GetKeyState(winrtWS::VirtualKey::Scroll) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::Mode;
			keyboardEvent_.mod = mod;

			bool isPressed = !args.KeyStatus().IsKeyReleased;
			if (isPressed != keyboardState_._pressedKeys[(std::int32_t)keyboardEvent_.sym]) {
				keyboardState_._pressedKeys[(std::int32_t)keyboardEvent_.sym] = isPressed;
				if (isPressed) {
					inputEventHandler_->OnKeyPressed(keyboardEvent_);
				} else {
					inputEventHandler_->OnKeyReleased(keyboardEvent_);
				}
			}
		}
	}

	void UwpInputManager::OnAcceleratorKeyActivated(const winrtWUC::CoreDispatcher& sender, const winrtWUC::AcceleratorKeyEventArgs& args)
	{
		// AcceleratorKeyActivated event is required to handle Alt keys, 
		// Also this callback has higher priority than OnKeyDown/Up, OnKeyDown/Up were never called after this one
		if (inputEventHandler_ != nullptr) {
			args.Handled(true);

			keyboardEvent_.scancode = args.KeyStatus().ScanCode;
			keyboardEvent_.sym = keySymValueToEnum(args.VirtualKey());
			if (keyboardEvent_.sym >= KeySym::COUNT) {
				return;
			}

			auto coreWindow = winrtWUC::CoreWindow::GetForCurrentThread();
			std::int32_t mod = 0;
			if ((coreWindow.GetKeyState(winrtWS::VirtualKey::Shift) & winrtWUC::CoreVirtualKeyStates::Down) == winrtWUC::CoreVirtualKeyStates::Down)
				mod |= KeyMod::LShift;
			if ((coreWindow.GetKeyState(winrtWS::VirtualKey::Control) & winrtWUC::CoreVirtualKeyStates::Down) == winrtWUC::CoreVirtualKeyStates::Down)
				mod |= KeyMod::LCtrl;
			if ((coreWindow.GetKeyState(winrtWS::VirtualKey::Menu) & winrtWUC::CoreVirtualKeyStates::Down) == winrtWUC::CoreVirtualKeyStates::Down)
				mod |= KeyMod::LAlt;
			if ((coreWindow.GetKeyState(winrtWS::VirtualKey::LeftWindows) & winrtWUC::CoreVirtualKeyStates::Down) == winrtWUC::CoreVirtualKeyStates::Down ||
				(coreWindow.GetKeyState(winrtWS::VirtualKey::RightWindows) & winrtWUC::CoreVirtualKeyStates::Down) == winrtWUC::CoreVirtualKeyStates::Down)
				mod |= KeyMod::LSuper;
			if ((coreWindow.GetKeyState(winrtWS::VirtualKey::CapitalLock) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::CapsLock;
			if ((coreWindow.GetKeyState(winrtWS::VirtualKey::NumberKeyLock) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::NumLock;
			if ((coreWindow.GetKeyState(winrtWS::VirtualKey::Scroll) & winrtWUC::CoreVirtualKeyStates::Locked) == winrtWUC::CoreVirtualKeyStates::Locked)
				mod |= KeyMod::Mode;
			keyboardEvent_.mod = mod;

			bool isPressed = (args.EventType() == winrtWUC::CoreAcceleratorKeyEventType::KeyDown || args.EventType() == winrtWUC::CoreAcceleratorKeyEventType::SystemKeyDown);
			if (isPressed != keyboardState_._pressedKeys[(std::int32_t)keyboardEvent_.sym]) {
				keyboardState_._pressedKeys[(std::int32_t)keyboardEvent_.sym] = isPressed;
				if (isPressed) {
					inputEventHandler_->OnKeyPressed(keyboardEvent_);
				} else {
					inputEventHandler_->OnKeyReleased(keyboardEvent_);
				}
			}
		}
	}

	void UwpInputManager::OnGamepadAdded(const winrtWF::IInspectable& sender, const winrtWGI::Gamepad& gamepad)
	{
		_gamepadsSync.EnterWriteLock();

		bool found = false;
		std::int32_t firstFreeId = -1;
		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
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

			if (inputEventHandler_ != nullptr) {
				joyMapping_.OnJoyConnected(joyConnectionEvent_);
				inputEventHandler_->OnJoyConnected(joyConnectionEvent_);
			}
		}

		_gamepadsSync.ExitWriteLock();

		if (!found) {
			updateJoystickStates();
		}
	}

	void UwpInputManager::OnGamepadRemoved(const winrtWF::IInspectable& sender, const winrtWGI::Gamepad& gamepad)
	{
		_gamepadsSync.EnterWriteLock();

		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			auto& info = _gamepads[i];
			if (info.Connected && info.Gamepad == gamepad) {
				info.Connected = false;

				_gamepads[i].State.resetJoystickState(i);

				if (inputEventHandler_ != nullptr) {
					inputEventHandler_->OnJoyDisconnected(joyConnectionEvent_);
					joyMapping_.OnJoyDisconnected(joyConnectionEvent_);
				}
				break;
			}
		}

		_gamepadsSync.ExitWriteLock();
	}

	KeySym UwpInputManager::keySymValueToEnum(winrtWS::VirtualKey virtualKey)
	{
		switch (virtualKey) {
			case winrtWS::VirtualKey::Back:				return KeySym::Backspace;
			case winrtWS::VirtualKey::Tab:				return KeySym::Tab;
			case winrtWS::VirtualKey::Enter:			return KeySym::Return;
			case winrtWS::VirtualKey::Escape:			return KeySym::Escape;
			case winrtWS::VirtualKey::Space:			return KeySym::Space;
			case (winrtWS::VirtualKey)VK_OEM_7:			return KeySym::Quote;
			case (winrtWS::VirtualKey)VK_OEM_PLUS:		return KeySym::Plus;
			case (winrtWS::VirtualKey)VK_OEM_COMMA:		return KeySym::Comma;
			case (winrtWS::VirtualKey)VK_OEM_MINUS:		return KeySym::Minus;
			case (winrtWS::VirtualKey)VK_OEM_PERIOD:	return KeySym::Period;
			case (winrtWS::VirtualKey)VK_OEM_2:			return KeySym::Slash;
			case winrtWS::VirtualKey::Number0:			return KeySym::D0;
			case winrtWS::VirtualKey::Number1:			return KeySym::D1;
			case winrtWS::VirtualKey::Number2:			return KeySym::D2;
			case winrtWS::VirtualKey::Number3:			return KeySym::D3;
			case winrtWS::VirtualKey::Number4:			return KeySym::D4;
			case winrtWS::VirtualKey::Number5:			return KeySym::D5;
			case winrtWS::VirtualKey::Number6:			return KeySym::D6;
			case winrtWS::VirtualKey::Number7:			return KeySym::D7;
			case winrtWS::VirtualKey::Number8:			return KeySym::D8;
			case winrtWS::VirtualKey::Number9:			return KeySym::D9;
			case (winrtWS::VirtualKey)VK_OEM_1:			return KeySym::Semicolon;

			case (winrtWS::VirtualKey)VK_OEM_4:			return KeySym::LeftBracket;
			case (winrtWS::VirtualKey)VK_OEM_5:			return KeySym::Backslash;
			case (winrtWS::VirtualKey)VK_OEM_6:			return KeySym::RightBracket;
			case (winrtWS::VirtualKey)VK_OEM_3:			return KeySym::Backquote;
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
			case winrtWS::VirtualKey::Delete:			return KeySym::Delete;

			case winrtWS::VirtualKey::NumberPad0:		return KeySym::NumPad0;
			case winrtWS::VirtualKey::NumberPad1:		return KeySym::NumPad1;
			case winrtWS::VirtualKey::NumberPad2:		return KeySym::NumPad2;
			case winrtWS::VirtualKey::NumberPad3:		return KeySym::NumPad3;
			case winrtWS::VirtualKey::NumberPad4:		return KeySym::NumPad4;
			case winrtWS::VirtualKey::NumberPad5:		return KeySym::NumPad5;
			case winrtWS::VirtualKey::NumberPad6:		return KeySym::NumPad6;
			case winrtWS::VirtualKey::NumberPad7:		return KeySym::NumPad7;
			case winrtWS::VirtualKey::NumberPad8:		return KeySym::NumPad8;
			case winrtWS::VirtualKey::NumberPad9:		return KeySym::NumPad9;
			case winrtWS::VirtualKey::Decimal:			return KeySym::NumPadPeriod;
			case winrtWS::VirtualKey::Divide:			return KeySym::NumPadDivide;
			case winrtWS::VirtualKey::Multiply:			return KeySym::NumPadMultiply;
			case winrtWS::VirtualKey::Subtract:			return KeySym::NumPadMinus;
			case winrtWS::VirtualKey::Add:				return KeySym::NumPadPlus;
			//case (winrtWS::VirtualKey)VK_RETURN:		return KeySym::NumPadEnter;
			//case winrtWS::VirtualKey::GLFW_KEY_KP_EQUAL:	return KeySym::NumPadEquals;

			case winrtWS::VirtualKey::Up:				return KeySym::Up;
			case winrtWS::VirtualKey::Down:				return KeySym::Down;
			case winrtWS::VirtualKey::Right:			return KeySym::Right;
			case winrtWS::VirtualKey::Left:				return KeySym::Left;
			case winrtWS::VirtualKey::Insert:			return KeySym::Insert;
			case winrtWS::VirtualKey::Home:				return KeySym::Home;
			case winrtWS::VirtualKey::End:				return KeySym::End;
			case winrtWS::VirtualKey::PageUp:			return KeySym::PageUp;
			case winrtWS::VirtualKey::PageDown:			return KeySym::PageDown;

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

			case winrtWS::VirtualKey::NumberKeyLock:	return KeySym::NumLock;
			case winrtWS::VirtualKey::CapitalLock:		return KeySym::CapsLock;
			case winrtWS::VirtualKey::Scroll:			return KeySym::ScrollLock;
			case winrtWS::VirtualKey::RightShift:		return KeySym::RShift;
			case winrtWS::VirtualKey::LeftShift:		return KeySym::LShift;
			case winrtWS::VirtualKey::RightControl:		return KeySym::RCtrl;
			case winrtWS::VirtualKey::LeftControl:		return KeySym::LCtrl;
			case winrtWS::VirtualKey::RightMenu:		return KeySym::RAlt;
			case winrtWS::VirtualKey::Menu:
			case winrtWS::VirtualKey::LeftMenu:			return KeySym::LAlt;
			case winrtWS::VirtualKey::RightWindows:		return KeySym::RSuper;
			case winrtWS::VirtualKey::LeftWindows:		return KeySym::LSuper;

			case winrtWS::VirtualKey::Print:			return KeySym::PrintScreen;
			case winrtWS::VirtualKey::Pause:			return KeySym::Pause;
			case winrtWS::VirtualKey::Application:		return KeySym::Menu;

			case winrtWS::VirtualKey::ModeChange:		return KeySym::Mode;
			case winrtWS::VirtualKey::Help:				return KeySym::Help;

			default:									return KeySym::Unknown;
		}
	}

	UwpJoystickState::UwpJoystickState()
		: joyId_(-1)
	{
	}

	bool UwpJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && buttonsState_[buttonId]);
	}

	unsigned char UwpJoystickState::hatState(int hatId) const
	{
		return (hatId >= 0 && hatId < MaxNumHats ? hatsState_[hatId] : HatState::Centered);
	}

	float UwpJoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? axesValuesState_[axisId] : 0.0f);
	}

	void UwpJoystickState::resetJoystickState(int joyId)
	{
		joyId_ = joyId;
		std::memset(buttonsState_, 0, sizeof(buttonsState_));
		std::memset(hatsState_, 0, sizeof(hatsState_));
		std::memset(axesValuesState_, 0, sizeof(axesValuesState_));
	}

	void UwpJoystickState::simulateButtonsEvents(winrtWGI::GamepadButtons buttons)
	{
		constexpr winrtWGI::GamepadButtons Mapping[MaxNumButtons] = {
			winrtWGI::GamepadButtons::A, winrtWGI::GamepadButtons::B, winrtWGI::GamepadButtons::X, winrtWGI::GamepadButtons::Y,
			winrtWGI::GamepadButtons::LeftShoulder, winrtWGI::GamepadButtons::RightShoulder,
			winrtWGI::GamepadButtons::View, winrtWGI::GamepadButtons::Menu,
			winrtWGI::GamepadButtons::LeftThumbstick, winrtWGI::GamepadButtons::RightThumbstick,
			winrtWGI::GamepadButtons::None /*Guide*/
		};

		for (std::int32_t i = 0; i < countof(Mapping); i++) {
			if (Mapping[i] == winrtWGI::GamepadButtons::None) {
				continue;
			}

			bool isPressed = (buttons & Mapping[i]) != winrtWGI::GamepadButtons::None;
			if (UwpInputManager::inputEventHandler_ != nullptr && isPressed != buttonsState_[i]) {
				joyButtonEvent_.joyId = joyId_;
				joyButtonEvent_.buttonId = i;
				if (isPressed) {
					UwpInputManager::joyMapping_.OnJoyButtonPressed(joyButtonEvent_);
					UwpInputManager::inputEventHandler_->OnJoyButtonPressed(joyButtonEvent_);
				} else {
					UwpInputManager::joyMapping_.OnJoyButtonReleased(joyButtonEvent_);
					UwpInputManager::inputEventHandler_->OnJoyButtonReleased(joyButtonEvent_);
				}
			}

			buttonsState_[i] = isPressed;
		}
	}

	void UwpJoystickState::simulateHatsEvents(winrtWGI::GamepadButtons buttons)
	{
		constexpr winrtWGI::GamepadButtons Mapping[4] = {
			winrtWGI::GamepadButtons::DPadUp, winrtWGI::GamepadButtons::DPadRight,
			winrtWGI::GamepadButtons::DPadDown, winrtWGI::GamepadButtons::DPadLeft
		};

		unsigned char hatState = 0;
		for (std::int32_t i = 0; i < countof(Mapping); i++) {
			bool isPressed = (buttons & Mapping[i]) != winrtWGI::GamepadButtons::None;
			if (isPressed) {
				hatState |= (1 << i);
			}
		}

		if (UwpInputManager::inputEventHandler_ != nullptr && hatsState_[0] != hatState) {
			joyHatEvent_.joyId = joyId_;
			joyHatEvent_.hatId = 0;
			joyHatEvent_.hatState = hatState;

			UwpInputManager::joyMapping_.OnJoyHatMoved(joyHatEvent_);
			UwpInputManager::inputEventHandler_->OnJoyHatMoved(joyHatEvent_);
		}

		hatsState_[0] = hatState;
	}

	void UwpJoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (UwpInputManager::inputEventHandler_ != nullptr && std::abs(axesValuesState_[axisId] - value) > AxisEventTolerance) {
			joyAxisEvent_.joyId = joyId_;
			joyAxisEvent_.axisId = axisId;
			joyAxisEvent_.value = value;
			UwpInputManager::joyMapping_.OnJoyAxisMoved(joyAxisEvent_);
			UwpInputManager::inputEventHandler_->OnJoyAxisMoved(joyAxisEvent_);
		}

		axesValuesState_[axisId] = value;
	}
}
