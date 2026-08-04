#if defined(WITH_DC)

#include "DcInputManager.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cmath>
#include <cstring>

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 4;
}

namespace nCine::Backends
{
	namespace
	{
		// Raw button indices of the built-in "xinput" mapping this platform resolves to (see JoyMappingDb.h).
		// These are the indices that mapping reads, NOT the order of the SDL_GameControllerButton enum -
		// the two differ from the shoulders onwards, which used to report Start as Back and both shoulders
		// as stick clicks.
		constexpr std::int32_t ButtonA = 0;
		constexpr std::int32_t ButtonB = 1;
		constexpr std::int32_t ButtonX = 2;
		constexpr std::int32_t ButtonY = 3;
		constexpr std::int32_t ButtonLShoulder = 4;
		constexpr std::int32_t ButtonRShoulder = 5;
		constexpr std::int32_t ButtonBack = 6;
		constexpr std::int32_t ButtonStart = 7;
		constexpr std::int32_t ButtonLStick = 8;
		constexpr std::int32_t ButtonRStick = 9;
		constexpr std::int32_t ButtonGuide = 10;

		// Event scratch (single-threaded poll)
		JoyButtonEvent _joyButtonEvent;
		JoyHatEvent _joyHatEvent;
		JoyAxisEvent _joyAxisEvent;
		JoyConnectionEvent _joyConnectionEvent;

		inline float NormalizeStick(int value)
		{
			// cont_state_t joyx/joyy are -128..127
			return (value < 0 ? float(value) / 128.0f : float(value) / 127.0f);
		}

		inline float NormalizeTrigger(int value)
		{
			// cont_state_t ltrig/rtrig are 0..255
			return (float(value) / 255.0f) * 2.0f - 1.0f;
		}
	}

	DcInputManager::PadInfo DcInputManager::_pads[DcInputManager::MaxJoysticks];

	DcJoystickState::DcJoystickState()
		: _joyId(-1), _hatState(HatState::Centered)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	bool DcJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char DcJoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float DcJoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void DcJoystickState::resetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void DcJoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::handler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.joyId = _joyId;
			_joyButtonEvent.buttonId = buttonId;
			if (pressed) {
				DcInputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				DcInputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void DcJoystickState::simulateHatEvent(unsigned char state)
	{
		if (IInputManager::handler() != nullptr && _hatState != state) {
			_joyHatEvent.joyId = _joyId;
			_joyHatEvent.hatId = 0;
			_joyHatEvent.hatState = state;
			DcInputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::handler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	void DcJoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::handler() != nullptr && std::abs(_axesValuesState[axisId] - value) > AxisEventTolerance) {
			_joyAxisEvent.joyId = _joyId;
			_joyAxisEvent.axisId = axisId;
			_joyAxisEvent.value = value;
			DcInputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
			IInputManager::handler()->OnJoyAxisMoved(_joyAxisEvent);
		}
		_axesValuesState[axisId] = value;
	}

	DcInputManager::DcInputManager()
	{
		_joyMapping.Init(this);

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			_pads[i].State.resetJoystickState(i);
		}
		// Poll once so an already-plugged controller connects before the first frame
		updateJoystickStates();
	}

	DcInputManager::~DcInputManager() = default;

	const MouseState& DcInputManager::mouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& DcInputManager::keyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool DcInputManager::isJoyPresent(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxJoysticks && _pads[joyId].Connected);
	}

	const char* DcInputManager::joyName(int joyId) const
	{
		static_cast<void>(joyId);
		return "Sega Dreamcast Controller";
	}

	const JoystickGuid DcInputManager::joyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int DcInputManager::joyNumButtons(int joyId) const
	{
		static_cast<void>(joyId);
		return DcJoystickState::MaxNumButtons;
	}

	int DcInputManager::joyNumHats(int joyId) const
	{
		static_cast<void>(joyId);
		return DcJoystickState::MaxNumHats;
	}

	int DcInputManager::joyNumAxes(int joyId) const
	{
		static_cast<void>(joyId);
		return DcJoystickState::MaxNumAxes;
	}

	const JoystickState& DcInputManager::joystickState(int joyId) const
	{
		static DcJoystickState nullJoystickState;
		if (isJoyPresent(joyId)) {
			return _pads[joyId].State;
		}
		return nullJoystickState;
	}

	bool DcInputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// TODO(DC): drive a Puru Puru (jump) pack when present
		static_cast<void>(joyId);
		static_cast<void>(lowFrequency);
		static_cast<void>(highFrequency);
		static_cast<void>(durationMs);
		return false;
	}

	bool DcInputManager::joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void DcInputManager::handleConnection(std::int32_t joyId, bool connected)
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

	void DcInputManager::updateJoystickStates()
	{
		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			maple_device_t* device = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
			handleConnection(i, device != nullptr);
			if (device == nullptr) {
				continue;
			}
			const cont_state_t* st = static_cast<const cont_state_t*>(maple_dev_status(device));
			if (st == nullptr) {
				continue;
			}

			DcJoystickState& state = _pads[i].State;

			state.simulateButtonEvent(ButtonA, (st->buttons & CONT_A) != 0);
			state.simulateButtonEvent(ButtonB, (st->buttons & CONT_B) != 0);
			state.simulateButtonEvent(ButtonX, (st->buttons & CONT_X) != 0);
			state.simulateButtonEvent(ButtonY, (st->buttons & CONT_Y) != 0);
			state.simulateButtonEvent(ButtonStart, (st->buttons & CONT_START) != 0);
			// The digital clicks of the analog triggers double as the shoulder buttons
			state.simulateButtonEvent(ButtonLShoulder, st->ltrig > 224);
			state.simulateButtonEvent(ButtonRShoulder, st->rtrig > 224);
			// The pad has no Back or Guide button, so C and Z stand in for them. Both are absent from a
			// standard controller - their bits simply never set there - but arcade sticks and the six-button
			// pads have them, and without this they would do nothing at all.
			state.simulateButtonEvent(ButtonBack, (st->buttons & CONT_C) != 0);
			state.simulateButtonEvent(ButtonGuide, (st->buttons & CONT_Z) != 0);
			// Nothing on the pad clicks the stick
			state.simulateButtonEvent(ButtonLStick, false);
			state.simulateButtonEvent(ButtonRStick, false);

			unsigned char hat = HatState::Centered;
			if (st->buttons & CONT_DPAD_UP) hat |= HatState::Up;
			if (st->buttons & CONT_DPAD_RIGHT) hat |= HatState::Right;
			if (st->buttons & CONT_DPAD_DOWN) hat |= HatState::Down;
			if (st->buttons & CONT_DPAD_LEFT) hat |= HatState::Left;
			state.simulateHatEvent(hat);

			state.simulateAxisEvent(0, NormalizeStick(st->joyx));
			state.simulateAxisEvent(1, NormalizeStick(st->joyy));
			state.simulateAxisEvent(2, NormalizeStick(st->joy2x));
			state.simulateAxisEvent(3, NormalizeStick(st->joy2y));
			state.simulateAxisEvent(4, NormalizeTrigger(st->ltrig));
			state.simulateAxisEvent(5, NormalizeTrigger(st->rtrig));
		}
	}
}

#endif
