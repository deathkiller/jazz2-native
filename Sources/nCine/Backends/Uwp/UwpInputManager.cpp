#include "UwpInputManager.h"
#include "UwpApplication.h"
#include "../../Input/IInputEventHandler.h"
#include "../../Input/JoyMapping.h"

#include <winrt/Windows.Foundation.Collections.h>

#include <Environment.h>
#include <Utf8.h>

using namespace Death;

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 8;
}

namespace nCine::Backends
{
	UwpMouseState UwpInputManager::mouseState_;
	UwpKeyboardState UwpInputManager::keyboardState_;
	KeyboardEvent UwpInputManager::keyboardEvent_;
	TextInputEvent UwpInputManager::textInputEvent_;
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
		window.CharacterReceived({ this, &UwpInputManager::OnCharacterReceived });
		window.Dispatcher().AcceleratorKeyActivated({ this, &UwpInputManager::OnAcceleratorKeyActivated });

		winrtWGI::Gamepad::GamepadAdded({ this, &UwpInputManager::OnGamepadAdded });
		winrtWGI::Gamepad::GamepadRemoved({ this, &UwpInputManager::OnGamepadRemoved });

		// Screen keyboard handling
		/*auto textManager = winrtWUTC::CoreTextServicesManager::GetForCurrentView();
		_editContext = textManager.CreateEditContext();

		_editContext.TextRequested([this](auto const&, winrtWUTC::CoreTextTextRequestedEventArgs const& args) {
			args.Request().Text(_text);
		});

		_editContext.SelectionRequested([this](auto const&, winrtWUTC::CoreTextSelectionRequestedEventArgs const& args) {
			args.Request().Selection(_selection);
		});

		_editContext.TextUpdating([this](auto const&, winrtWUTC::CoreTextTextUpdatingEventArgs const& args) {
			_text = args.Text();
			_selection = args.Range();
		});

		_editContext.FocusRemoved([this](auto const&, auto const&) {
			// TODO: Handle blur
		});*/

		// Gamepads
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
			if (keyboardEvent_.sym >= Keys::Count) {
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

	void UwpInputManager::OnCharacterReceived(const winrtWUC::CoreWindow& sender, const winrtWUC::CharacterReceivedEventArgs& args)
	{
		if (inputEventHandler_ != nullptr) {
			args.Handled(true);

			std::uint32_t keyCode = args.KeyCode();
			DEATH_ASSERT(keyCode <= UINT16_MAX, ("Received character %u has invalid value", keyCode), );
			wchar_t utf16character = static_cast<wchar_t>(keyCode);
			textInputEvent_.length = Utf8::FromUtf16(textInputEvent_.text, &utf16character, 1);
			if (textInputEvent_.length > 0) {
				inputEventHandler_->OnTextInput(textInputEvent_);
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
			if (keyboardEvent_.sym >= Keys::Count) {
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

			auto eventType = args.EventType();
			bool isPressed = (eventType == winrtWUC::CoreAcceleratorKeyEventType::KeyDown || eventType == winrtWUC::CoreAcceleratorKeyEventType::SystemKeyDown);
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

	Keys UwpInputManager::keySymValueToEnum(winrtWS::VirtualKey virtualKey)
	{
		switch (virtualKey) {
			case winrtWS::VirtualKey::Back:				return Keys::Backspace;
			case winrtWS::VirtualKey::Tab:				return Keys::Tab;
			case winrtWS::VirtualKey::Enter:			return Keys::Return;
			case winrtWS::VirtualKey::Escape:			return Keys::Escape;
			case winrtWS::VirtualKey::Space:			return Keys::Space;
			case (winrtWS::VirtualKey)VK_OEM_7:			return Keys::Quote;
			case (winrtWS::VirtualKey)VK_OEM_PLUS:		return Keys::Plus;
			case (winrtWS::VirtualKey)VK_OEM_COMMA:		return Keys::Comma;
			case (winrtWS::VirtualKey)VK_OEM_MINUS:		return Keys::Minus;
			case (winrtWS::VirtualKey)VK_OEM_PERIOD:	return Keys::Period;
			case (winrtWS::VirtualKey)VK_OEM_2:			return Keys::Slash;
			case winrtWS::VirtualKey::Number0:			return Keys::D0;
			case winrtWS::VirtualKey::Number1:			return Keys::D1;
			case winrtWS::VirtualKey::Number2:			return Keys::D2;
			case winrtWS::VirtualKey::Number3:			return Keys::D3;
			case winrtWS::VirtualKey::Number4:			return Keys::D4;
			case winrtWS::VirtualKey::Number5:			return Keys::D5;
			case winrtWS::VirtualKey::Number6:			return Keys::D6;
			case winrtWS::VirtualKey::Number7:			return Keys::D7;
			case winrtWS::VirtualKey::Number8:			return Keys::D8;
			case winrtWS::VirtualKey::Number9:			return Keys::D9;
			case (winrtWS::VirtualKey)VK_OEM_1:			return Keys::Semicolon;

			case (winrtWS::VirtualKey)VK_OEM_4:			return Keys::LeftBracket;
			case (winrtWS::VirtualKey)VK_OEM_5:			return Keys::Backslash;
			case (winrtWS::VirtualKey)VK_OEM_6:			return Keys::RightBracket;
			case (winrtWS::VirtualKey)VK_OEM_3:			return Keys::Backquote;
			case winrtWS::VirtualKey::A:				return Keys::A;
			case winrtWS::VirtualKey::B:				return Keys::B;
			case winrtWS::VirtualKey::C:				return Keys::C;
			case winrtWS::VirtualKey::D:				return Keys::D;
			case winrtWS::VirtualKey::E:				return Keys::E;
			case winrtWS::VirtualKey::F:				return Keys::F;
			case winrtWS::VirtualKey::G:				return Keys::G;
			case winrtWS::VirtualKey::H:				return Keys::H;
			case winrtWS::VirtualKey::I:				return Keys::I;
			case winrtWS::VirtualKey::J:				return Keys::J;
			case winrtWS::VirtualKey::K:				return Keys::K;
			case winrtWS::VirtualKey::L:				return Keys::L;
			case winrtWS::VirtualKey::M:				return Keys::M;
			case winrtWS::VirtualKey::N:				return Keys::N;
			case winrtWS::VirtualKey::O:				return Keys::O;
			case winrtWS::VirtualKey::P:				return Keys::P;
			case winrtWS::VirtualKey::Q:				return Keys::Q;
			case winrtWS::VirtualKey::R:				return Keys::R;
			case winrtWS::VirtualKey::S:				return Keys::S;
			case winrtWS::VirtualKey::T:				return Keys::T;
			case winrtWS::VirtualKey::U:				return Keys::U;
			case winrtWS::VirtualKey::V:				return Keys::V;
			case winrtWS::VirtualKey::W:				return Keys::W;
			case winrtWS::VirtualKey::X:				return Keys::X;
			case winrtWS::VirtualKey::Y:				return Keys::Y;
			case winrtWS::VirtualKey::Z:				return Keys::Z;
			case winrtWS::VirtualKey::Delete:			return Keys::Delete;

			case winrtWS::VirtualKey::NumberPad0:		return Keys::NumPad0;
			case winrtWS::VirtualKey::NumberPad1:		return Keys::NumPad1;
			case winrtWS::VirtualKey::NumberPad2:		return Keys::NumPad2;
			case winrtWS::VirtualKey::NumberPad3:		return Keys::NumPad3;
			case winrtWS::VirtualKey::NumberPad4:		return Keys::NumPad4;
			case winrtWS::VirtualKey::NumberPad5:		return Keys::NumPad5;
			case winrtWS::VirtualKey::NumberPad6:		return Keys::NumPad6;
			case winrtWS::VirtualKey::NumberPad7:		return Keys::NumPad7;
			case winrtWS::VirtualKey::NumberPad8:		return Keys::NumPad8;
			case winrtWS::VirtualKey::NumberPad9:		return Keys::NumPad9;
			case winrtWS::VirtualKey::Decimal:			return Keys::NumPadPeriod;
			case winrtWS::VirtualKey::Divide:			return Keys::NumPadDivide;
			case winrtWS::VirtualKey::Multiply:			return Keys::NumPadMultiply;
			case winrtWS::VirtualKey::Subtract:			return Keys::NumPadMinus;
			case winrtWS::VirtualKey::Add:				return Keys::NumPadPlus;
			//case (winrtWS::VirtualKey)VK_RETURN:		return Keys::NumPadEnter;
			//case winrtWS::VirtualKey::GLFW_KEY_KP_EQUAL:	return Keys::NumPadEquals;

			case winrtWS::VirtualKey::Up:				return Keys::Up;
			case winrtWS::VirtualKey::Down:				return Keys::Down;
			case winrtWS::VirtualKey::Right:			return Keys::Right;
			case winrtWS::VirtualKey::Left:				return Keys::Left;
			case winrtWS::VirtualKey::Insert:			return Keys::Insert;
			case winrtWS::VirtualKey::Home:				return Keys::Home;
			case winrtWS::VirtualKey::End:				return Keys::End;
			case winrtWS::VirtualKey::PageUp:			return Keys::PageUp;
			case winrtWS::VirtualKey::PageDown:			return Keys::PageDown;

			case winrtWS::VirtualKey::F1:				return Keys::F1;
			case winrtWS::VirtualKey::F2:				return Keys::F2;
			case winrtWS::VirtualKey::F3:				return Keys::F3;
			case winrtWS::VirtualKey::F4:				return Keys::F4;
			case winrtWS::VirtualKey::F5:				return Keys::F5;
			case winrtWS::VirtualKey::F6:				return Keys::F6;
			case winrtWS::VirtualKey::F7:				return Keys::F7;
			case winrtWS::VirtualKey::F8:				return Keys::F8;
			case winrtWS::VirtualKey::F9:				return Keys::F9;
			case winrtWS::VirtualKey::F10:				return Keys::F10;
			case winrtWS::VirtualKey::F11:				return Keys::F11;
			case winrtWS::VirtualKey::F12:				return Keys::F12;
			case winrtWS::VirtualKey::F13:				return Keys::F13;
			case winrtWS::VirtualKey::F14:				return Keys::F14;
			case winrtWS::VirtualKey::F15:				return Keys::F15;

			case winrtWS::VirtualKey::NumberKeyLock:	return Keys::NumLock;
			case winrtWS::VirtualKey::CapitalLock:		return Keys::CapsLock;
			case winrtWS::VirtualKey::Scroll:			return Keys::ScrollLock;
			case winrtWS::VirtualKey::RightShift:		return Keys::RShift;
			case winrtWS::VirtualKey::LeftShift:		return Keys::LShift;
			case winrtWS::VirtualKey::RightControl:		return Keys::RCtrl;
			case winrtWS::VirtualKey::LeftControl:		return Keys::LCtrl;
			case winrtWS::VirtualKey::RightMenu:		return Keys::RAlt;
			case winrtWS::VirtualKey::Menu:
			case winrtWS::VirtualKey::LeftMenu:			return Keys::LAlt;
			case winrtWS::VirtualKey::RightWindows:		return Keys::RSuper;
			case winrtWS::VirtualKey::LeftWindows:		return Keys::LSuper;

			case winrtWS::VirtualKey::Print:			return Keys::PrintScreen;
			case winrtWS::VirtualKey::Pause:			return Keys::Pause;
			case winrtWS::VirtualKey::Application:		return Keys::Menu;

			case winrtWS::VirtualKey::ModeChange:		return Keys::Mode;
			case winrtWS::VirtualKey::Help:				return Keys::Help;

			default:									return Keys::Unknown;
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

		for (std::int32_t i = 0; i < arraySize(Mapping); i++) {
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
		for (std::int32_t i = 0; i < arraySize(Mapping); i++) {
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
