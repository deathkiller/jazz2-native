#if defined(WITH_AMIGA)

#include "AmigaInputManager.h"
#include "AmigaPlatform.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cstring>

#include <exec/exec.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>
#include <libraries/lowlevel.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/lowlevel.h>
#include <proto/keymap.h>

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 2;
}

namespace nCine::Backends
{
	namespace
	{
		// Raw button indices of the built-in "xinput" mapping this platform resolves to (see JoyMappingDb.h
		// and the Dc/N64 backends, which share the layout)
		constexpr std::int32_t ButtonA = 0;
		constexpr std::int32_t ButtonB = 1;
		constexpr std::int32_t ButtonX = 2;
		constexpr std::int32_t ButtonY = 3;
		constexpr std::int32_t ButtonLShoulder = 4;
		constexpr std::int32_t ButtonRShoulder = 5;
		constexpr std::int32_t ButtonBack = 6;
		constexpr std::int32_t ButtonStart = 7;

		// Event scratch (single-threaded pump)
		JoyButtonEvent _joyButtonEvent;
		JoyHatEvent _joyHatEvent;
		JoyConnectionEvent _joyConnectionEvent;
		KeyboardEvent _keyboardEvent;
		TextInputEvent _textInputEvent;
		MouseEvent _mouseEvent;
		ScrollEvent _scrollEvent;

		// A 1x1 fully transparent pointer sprite in chip RAM, for Cursor::Hidden
		UWORD* _blankPointer = nullptr;

		// The Amiga rawkey matrix is positional - the codes below are the same on every hardware and
		// national layout; only the CHARACTERS differ, which is what MapRawKey is for (text input).
		// NewMouse wheel codes (0x7A/0x7B) are handled separately in the pump.
		constexpr std::int32_t MaxRawKey = 0x68;
		const Keys RawKeyMap[MaxRawKey] = {
			Keys::Backquote,     // 0x00 ` ~
			Keys::D1, Keys::D2, Keys::D3, Keys::D4, Keys::D5, Keys::D6, Keys::D7, Keys::D8, Keys::D9, Keys::D0,
			Keys::Minus,         // 0x0B - _
			Keys::Equals,        // 0x0C = +
			Keys::Backslash,     // 0x0D
			Keys::Unknown,       // 0x0E
			Keys::NumPad0,       // 0x0F
			Keys::Q, Keys::W, Keys::E, Keys::R, Keys::T, Keys::Y, Keys::U, Keys::I, Keys::O, Keys::P,
			Keys::LeftBracket,   // 0x1A
			Keys::RightBracket,  // 0x1B
			Keys::Unknown,       // 0x1C
			Keys::NumPad1, Keys::NumPad2, Keys::NumPad3,
			Keys::A, Keys::S, Keys::D, Keys::F, Keys::G, Keys::H, Keys::J, Keys::K, Keys::L,
			Keys::Semicolon,     // 0x29
			Keys::Quote,         // 0x2A
			Keys::Unknown,       // 0x2B (international)
			Keys::Unknown,       // 0x2C
			Keys::NumPad4, Keys::NumPad5, Keys::NumPad6,
			Keys::Unknown,       // 0x30 (international <>)
			Keys::Z, Keys::X, Keys::C, Keys::V, Keys::B, Keys::N, Keys::M,
			Keys::Comma,         // 0x38
			Keys::Period,        // 0x39
			Keys::Slash,         // 0x3A
			Keys::Unknown,       // 0x3B
			Keys::NumPadPeriod,  // 0x3C
			Keys::NumPad7, Keys::NumPad8, Keys::NumPad9,
			Keys::Space,         // 0x40
			Keys::Backspace,     // 0x41
			Keys::Tab,           // 0x42
			Keys::NumPadEnter,   // 0x43
			Keys::Return,        // 0x44
			Keys::Escape,        // 0x45
			Keys::Delete,        // 0x46
			Keys::Insert,        // 0x47 (some keyboards)
			Keys::PageUp,        // 0x48 (some keyboards)
			Keys::PageDown,      // 0x49 (some keyboards)
			Keys::NumPadMinus,   // 0x4A
			Keys::F11,           // 0x4B (some keyboards)
			Keys::Up,            // 0x4C
			Keys::Down,          // 0x4D
			Keys::Right,         // 0x4E
			Keys::Left,          // 0x4F
			Keys::F1, Keys::F2, Keys::F3, Keys::F4, Keys::F5, Keys::F6, Keys::F7, Keys::F8, Keys::F9, Keys::F10,
			Keys::NumPadLeftParen,  // 0x5A ( [
			Keys::NumPadRightParen, // 0x5B ) ]
			Keys::NumPadDivide,  // 0x5C
			Keys::NumPadMultiply,// 0x5D
			Keys::NumPadPlus,    // 0x5E
			Keys::Help,          // 0x5F
			Keys::LShift,        // 0x60
			Keys::RShift,        // 0x61
			Keys::CapsLock,      // 0x62
			Keys::LCtrl,         // 0x63
			Keys::LAlt,          // 0x64
			Keys::RAlt,          // 0x65
			Keys::LSuper,        // 0x66 (left Amiga)
			Keys::RSuper         // 0x67 (right Amiga)
		};

		std::int32_t KeyModsFromQualifier(UWORD qualifier)
		{
			std::int32_t mods = 0;
			if (qualifier & IEQUALIFIER_LSHIFT) mods |= KeyMod::LShift;
			if (qualifier & IEQUALIFIER_RSHIFT) mods |= KeyMod::RShift;
			if (qualifier & IEQUALIFIER_CAPSLOCK) mods |= KeyMod::CapsLock;
			if (qualifier & IEQUALIFIER_CONTROL) mods |= KeyMod::LCtrl;
			if (qualifier & IEQUALIFIER_LALT) mods |= KeyMod::LAlt;
			if (qualifier & IEQUALIFIER_RALT) mods |= KeyMod::RAlt;
			if (qualifier & IEQUALIFIER_LCOMMAND) mods |= KeyMod::LSuper;
			if (qualifier & IEQUALIFIER_RCOMMAND) mods |= KeyMod::RSuper;
			return mods;
		}
	}

	AmigaInputManager::PadInfo AmigaInputManager::_pads[AmigaInputManager::MaxJoysticks];
	AmigaMouseState AmigaInputManager::_mouseState;
	AmigaKeyboardState AmigaInputManager::_keyboardState;

	bool AmigaMouseState::isButtonDown(MouseButton button) const
	{
		return (_buttons & (1u << std::uint32_t(button))) != 0;
	}

	AmigaJoystickState::AmigaJoystickState()
		: _joyId(-1), _hatState(HatState::Centered)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
	}

	bool AmigaJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char AmigaJoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float AmigaJoystickState::axisValue(int axisId) const
	{
		static_cast<void>(axisId);
		return 0.0f;
	}

	void AmigaJoystickState::resetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
	}

	void AmigaJoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::handler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.joyId = _joyId;
			_joyButtonEvent.buttonId = buttonId;
			if (pressed) {
				AmigaInputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				AmigaInputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void AmigaJoystickState::simulateHatEvent(unsigned char state)
	{
		if (IInputManager::handler() != nullptr && _hatState != state) {
			_joyHatEvent.joyId = _joyId;
			_joyHatEvent.hatId = 0;
			_joyHatEvent.hatState = state;
			AmigaInputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::handler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	AmigaInputManager::AmigaInputManager()
	{
		_joyMapping.Init(this);

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			_pads[i].State.resetJoystickState(i);
		}
		// Poll once so an already-plugged pad connects before the first frame
		updateJoystickStates();
	}

	AmigaInputManager::~AmigaInputManager()
	{
		if (_blankPointer != nullptr) {
			FreeMem(_blankPointer, 12);
			_blankPointer = nullptr;
		}
	}

	const MouseState& AmigaInputManager::mouseState() const
	{
		return _mouseState;
	}

	const KeyboardState& AmigaInputManager::keyboardState() const
	{
		return _keyboardState;
	}

	bool AmigaInputManager::isJoyPresent(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxJoysticks && _pads[joyId].Connected);
	}

	const char* AmigaInputManager::joyName(int joyId) const
	{
		if (joyId >= 0 && joyId < MaxJoysticks && _pads[joyId].IsGameController) {
			return "CD32 Game Pad";
		}
		return "Amiga Joystick";
	}

	const JoystickGuid AmigaInputManager::joyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int AmigaInputManager::joyNumButtons(int joyId) const
	{
		static_cast<void>(joyId);
		return AmigaJoystickState::MaxNumButtons;
	}

	int AmigaInputManager::joyNumHats(int joyId) const
	{
		static_cast<void>(joyId);
		return AmigaJoystickState::MaxNumHats;
	}

	int AmigaInputManager::joyNumAxes(int joyId) const
	{
		static_cast<void>(joyId);
		return AmigaJoystickState::MaxNumAxes;
	}

	const JoystickState& AmigaInputManager::joystickState(int joyId) const
	{
		static AmigaJoystickState nullJoystickState;
		if (isJoyPresent(joyId)) {
			return _pads[joyId].State;
		}
		return nullJoystickState;
	}

	bool AmigaInputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// No Amiga controller has a motor
		static_cast<void>(joyId);
		static_cast<void>(lowFrequency);
		static_cast<void>(highFrequency);
		static_cast<void>(durationMs);
		return false;
	}

	bool AmigaInputManager::joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void AmigaInputManager::setCursor(Cursor cursor)
	{
		struct Window* window = AmigaPlatform::GameWindow;
		if (window == nullptr) {
			return;
		}
		if (cursor == Cursor::Arrow) {
			ClearPointer(window);
		} else {
			// A transparent 1-line sprite; sprite data must live in chip RAM
			if (_blankPointer == nullptr) {
				_blankPointer = static_cast<UWORD*>(AllocMem(12, MEMF_CHIP | MEMF_CLEAR));
			}
			if (_blankPointer != nullptr) {
				SetPointer(window, _blankPointer, 1, 16, 0, 0);
			}
		}
		IInputManager::setCursor(cursor);
	}

	void AmigaInputManager::handleConnection(std::int32_t joyId, bool connected)
	{
		if (_pads[joyId].Connected == connected) {
			return;
		}
		_pads[joyId].Connected = connected;
		_joyConnectionEvent.joyId = joyId;
		if (connected) {
			_joyMapping.OnJoyConnected(_joyConnectionEvent);
			if (_inputEventHandler != nullptr) {
				_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
			}
		} else {
			_pads[joyId].State.resetJoystickState(joyId);
			if (_inputEventHandler != nullptr) {
				_inputEventHandler->OnJoyDisconnected(_joyConnectionEvent);
			}
			_joyMapping.OnJoyDisconnected(_joyConnectionEvent);
		}
	}

	void AmigaInputManager::processIdcmpMessages()
	{
		struct Window* window = AmigaPlatform::GameWindow;
		if (window == nullptr || window->UserPort == nullptr) {
			return;
		}

		struct IntuiMessage* message;
		while ((message = reinterpret_cast<struct IntuiMessage*>(GetMsg(window->UserPort))) != nullptr) {
			const ULONG messageClass = message->Class;
			const UWORD code = message->Code;
			const UWORD qualifier = message->Qualifier;
			const WORD mouseX = message->MouseX;
			const WORD mouseY = message->MouseY;
			// What MapRawKey() needs from a RAWKEY message is read out here, before the message goes
			// back: `IAddress` points at Intuition's dead-key state, which is only guaranteed to be
			// there until the reply (the field means something else, or nothing, for other classes)
			APTR deadKeyData = nullptr;
			if (messageClass == IDCMP_RAWKEY && message->IAddress != nullptr) {
				deadKeyData = *reinterpret_cast<APTR*>(message->IAddress);
			}
			ReplyMsg(&message->ExecMessage);

			switch (messageClass) {
				case IDCMP_RAWKEY: {
					const bool released = (code & IECODE_UP_PREFIX) != 0;
					const std::int32_t rawKey = (code & ~IECODE_UP_PREFIX);

					// NewMouse wheel events arrive as rawkey codes above the keyboard matrix
					if (rawKey == 0x7A || rawKey == 0x7B) {
						if (!released && _inputEventHandler != nullptr) {
							_scrollEvent.x = 0.0f;
							_scrollEvent.y = (rawKey == 0x7A ? 1.0f : -1.0f);
							_inputEventHandler->OnMouseWheel(_scrollEvent);
						}
						break;
					}
					if (rawKey >= MaxRawKey) {
						break;
					}
					const Keys sym = RawKeyMap[rawKey];
					if (sym == Keys::Unknown) {
						break;
					}

					const bool wasDown = _keyboardState._keys[std::int32_t(sym)];
					_keyboardState._keys[std::int32_t(sym)] = !released;

					if (_inputEventHandler != nullptr) {
						_keyboardEvent.scancode = rawKey;
						_keyboardEvent.sym = sym;
						_keyboardEvent.mod = KeyModsFromQualifier(qualifier);
						if (released) {
							_inputEventHandler->OnKeyReleased(_keyboardEvent);
						} else {
							// Keyboard repeat delivers further down events; state transitions only
							if (!wasDown) {
								_inputEventHandler->OnKeyPressed(_keyboardEvent);
							}
							// Text input through the active keymap, so national layouts type correctly
							// (the dead-key state read out of the message above is what carries the
							// previous codes an accent has to combine with)
							if (AmigaPlatform::HasKeymap()) {
								struct InputEvent inputEvent = {};
								inputEvent.ie_Class = IECLASS_RAWKEY;
								inputEvent.ie_Code = code;
								inputEvent.ie_Qualifier = qualifier;
								inputEvent.ie_EventAddress = deadKeyData;
								char text[4];
								const LONG length = MapRawKey(&inputEvent, reinterpret_cast<STRPTR>(text), sizeof(text), nullptr);
								if (length == 1) {
									const unsigned char character = static_cast<unsigned char>(text[0]);
									if (character >= 32 && character != 127) {
										if (character < 128) {
											_textInputEvent.text[0] = char(character);
											_textInputEvent.text[1] = '\0';
											_textInputEvent.length = 1;
										} else {
											// Latin-1 to UTF-8 for the engine's text fields
											_textInputEvent.text[0] = char(0xC0 | (character >> 6));
											_textInputEvent.text[1] = char(0x80 | (character & 0x3F));
											_textInputEvent.text[2] = '\0';
											_textInputEvent.length = 2;
										}
										_inputEventHandler->OnTextInput(_textInputEvent);
									}
								}
							}
						}
					}
					break;
				}
				case IDCMP_MOUSEBUTTONS: {
					_mouseState.x = mouseX;
					_mouseState.y = mouseY;
					MouseButton button;
					bool pressed;
					switch (code) {
						case SELECTDOWN: button = MouseButton::Left; pressed = true; break;
						case SELECTUP: button = MouseButton::Left; pressed = false; break;
						case MENUDOWN: button = MouseButton::Right; pressed = true; break;
						case MENUUP: button = MouseButton::Right; pressed = false; break;
						case MIDDLEDOWN: button = MouseButton::Middle; pressed = true; break;
						case MIDDLEUP: button = MouseButton::Middle; pressed = false; break;
						default: continue;
					}
					if (pressed) {
						_mouseState._buttons |= (1u << std::uint32_t(button));
					} else {
						_mouseState._buttons &= ~(1u << std::uint32_t(button));
					}
					if (_inputEventHandler != nullptr) {
						_mouseEvent.x = mouseX;
						_mouseEvent.y = mouseY;
						_mouseEvent.button = button;
						if (pressed) {
							_inputEventHandler->OnMouseDown(_mouseEvent);
						} else {
							_inputEventHandler->OnMouseUp(_mouseEvent);
						}
					}
					break;
				}
				case IDCMP_MOUSEMOVE:
					_mouseState.x = mouseX;
					_mouseState.y = mouseY;
					if (_inputEventHandler != nullptr) {
						_inputEventHandler->OnMouseMove(_mouseState);
					}
					break;
			}
		}
	}

	void AmigaInputManager::pollJoyPorts()
	{
		if (!AmigaPlatform::HasLowLevel()) {
			return;
		}

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			// Engine joystick 0 is joyport 1 (the "joystick port"); joystick 1 is joyport 0, which is
			// normally the mouse and only counts when something else answers there
			const ULONG port = (i == 0 ? 1 : 0);
			const ULONG state = ReadJoyPort(port);
			const ULONG type = (state & JP_TYPE_MASK);

			const bool isPad = (type == JP_TYPE_GAMECTLR);
			const bool connected = (isPad || type == JP_TYPE_JOYSTK);
			handleConnection(i, connected);
			if (!connected) {
				continue;
			}
			_pads[i].IsGameController = isPad;
			AmigaJoystickState& pad = _pads[i].State;

			// The CD32 pad's red button is the primary action (A), blue the secondary (X), matching how
			// the N64 backend places the pad's B on the XInput X - A stays jump/confirm, X shoot
			pad.simulateButtonEvent(ButtonA, (state & JPF_BUTTON_RED) != 0);
			pad.simulateButtonEvent(ButtonX, (state & JPF_BUTTON_BLUE) != 0);
			if (isPad) {
				pad.simulateButtonEvent(ButtonB, (state & JPF_BUTTON_GREEN) != 0);
				pad.simulateButtonEvent(ButtonY, (state & JPF_BUTTON_YELLOW) != 0);
				pad.simulateButtonEvent(ButtonStart, (state & JPF_BUTTON_PLAY) != 0);
				pad.simulateButtonEvent(ButtonLShoulder, (state & JPF_BUTTON_REVERSE) != 0);
				pad.simulateButtonEvent(ButtonRShoulder, (state & JPF_BUTTON_FORWARD) != 0);
			}

			unsigned char hat = HatState::Centered;
			if (state & JPF_JOY_UP) hat |= HatState::Up;
			if (state & JPF_JOY_DOWN) hat |= HatState::Down;
			if (state & JPF_JOY_LEFT) hat |= HatState::Left;
			if (state & JPF_JOY_RIGHT) hat |= HatState::Right;
			pad.simulateHatEvent(hat);
		}
	}

	void AmigaInputManager::updateJoystickStates()
	{
		processIdcmpMessages();
		pollJoyPorts();
	}
}

#endif
