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

	bool DcJoystickState::IsButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char DcJoystickState::GetHatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float DcJoystickState::GetAxisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void DcJoystickState::ResetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void DcJoystickState::SimulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::GetHandler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.JoyId = _joyId;
			_joyButtonEvent.ButtonId = buttonId;
			if (pressed) {
				DcInputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::GetHandler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				DcInputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::GetHandler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void DcJoystickState::SimulateHatEvent(unsigned char state)
	{
		if (IInputManager::GetHandler() != nullptr && _hatState != state) {
			_joyHatEvent.JoyId = _joyId;
			_joyHatEvent.HatId = 0;
			_joyHatEvent.HatState = state;
			DcInputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::GetHandler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	void DcJoystickState::SimulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::GetHandler() != nullptr && std::abs(_axesValuesState[axisId] - value) > AxisEventTolerance) {
			_joyAxisEvent.JoyId = _joyId;
			_joyAxisEvent.AxisId = axisId;
			_joyAxisEvent.Value = value;
			DcInputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
			IInputManager::GetHandler()->OnJoyAxisMoved(_joyAxisEvent);
		}
		_axesValuesState[axisId] = value;
	}

	DcInputManager::DcInputManager()
	{
		_joyMapping.Init(this);

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			_pads[i].State.ResetJoystickState(i);
		}
		// Poll once so an already-plugged controller connects before the first frame
		UpdateJoystickStates();
	}

	DcInputManager::~DcInputManager() = default;

	const MouseState& DcInputManager::GetMouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& DcInputManager::GetKeyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool DcInputManager::IsJoyPresent(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxJoysticks && _pads[joyId].Connected);
	}

	const char* DcInputManager::GetJoyName(int joyId) const
	{
		static_cast<void>(joyId);
		return "Sega Dreamcast Controller";
	}

	const JoystickGuid DcInputManager::GetJoyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int DcInputManager::GetJoyButtonCount(int joyId) const
	{
		static_cast<void>(joyId);
		return DcJoystickState::MaxNumButtons;
	}

	int DcInputManager::GetJoyHatCount(int joyId) const
	{
		static_cast<void>(joyId);
		return DcJoystickState::MaxNumHats;
	}

	int DcInputManager::GetJoyAxisCount(int joyId) const
	{
		static_cast<void>(joyId);
		return DcJoystickState::MaxNumAxes;
	}

	const JoystickState& DcInputManager::GetJoystickState(int joyId) const
	{
		static DcJoystickState nullJoystickState;
		if (IsJoyPresent(joyId)) {
			return _pads[joyId].State;
		}
		return nullJoystickState;
	}

	bool DcInputManager::JoystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// TODO(DC): drive a Puru Puru (jump) pack when present
		static_cast<void>(joyId);
		static_cast<void>(lowFrequency);
		static_cast<void>(highFrequency);
		static_cast<void>(durationMs);
		return false;
	}

	bool DcInputManager::JoystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void DcInputManager::HandleConnection(std::int32_t joyId, bool connected)
	{
		if (_pads[joyId].Connected == connected) {
			return;
		}
		_pads[joyId].Connected = connected;
		_joyConnectionEvent.JoyId = joyId;
		if (connected) {
			_joyMapping.OnJoyConnected(_joyConnectionEvent);
			if (_inputEventHandler != nullptr) {
				_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
			}
		} else {
			_pads[joyId].State.ResetJoystickState(joyId);
			if (_inputEventHandler != nullptr) {
				_inputEventHandler->OnJoyDisconnected(_joyConnectionEvent);
			}
			_joyMapping.OnJoyDisconnected(_joyConnectionEvent);
		}
	}

	void DcInputManager::UpdateJoystickStates()
	{
		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			maple_device_t* device = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
			HandleConnection(i, device != nullptr);
			if (device == nullptr) {
				continue;
			}
			const cont_state_t* st = static_cast<const cont_state_t*>(maple_dev_status(device));
			if (st == nullptr) {
				continue;
			}

			DcJoystickState& state = _pads[i].State;

			state.SimulateButtonEvent(ButtonA, (st->buttons & CONT_A) != 0);
			state.SimulateButtonEvent(ButtonB, (st->buttons & CONT_B) != 0);
			state.SimulateButtonEvent(ButtonX, (st->buttons & CONT_X) != 0);
			state.SimulateButtonEvent(ButtonY, (st->buttons & CONT_Y) != 0);
			state.SimulateButtonEvent(ButtonStart, (st->buttons & CONT_START) != 0);
			// The digital clicks of the analog triggers double as the shoulder buttons
			state.SimulateButtonEvent(ButtonLShoulder, st->ltrig > 224);
			state.SimulateButtonEvent(ButtonRShoulder, st->rtrig > 224);
			// The pad has no Back or Guide button, so C and Z stand in for them. Both are absent from a
			// standard controller - their bits simply never set there - but arcade sticks and the six-button
			// pads have them, and without this they would do nothing at all.
			state.SimulateButtonEvent(ButtonBack, (st->buttons & CONT_C) != 0);
			state.SimulateButtonEvent(ButtonGuide, (st->buttons & CONT_Z) != 0);
			// Nothing on the pad clicks the stick
			state.SimulateButtonEvent(ButtonLStick, false);
			state.SimulateButtonEvent(ButtonRStick, false);

			unsigned char hat = HatState::Centered;
			if (st->buttons & CONT_DPAD_UP) hat |= HatState::Up;
			if (st->buttons & CONT_DPAD_RIGHT) hat |= HatState::Right;
			if (st->buttons & CONT_DPAD_DOWN) hat |= HatState::Down;
			if (st->buttons & CONT_DPAD_LEFT) hat |= HatState::Left;
			state.SimulateHatEvent(hat);

			state.SimulateAxisEvent(0, NormalizeStick(st->joyx));
			state.SimulateAxisEvent(1, NormalizeStick(st->joyy));
			state.SimulateAxisEvent(2, NormalizeStick(st->joy2x));
			state.SimulateAxisEvent(3, NormalizeStick(st->joy2y));
			state.SimulateAxisEvent(4, NormalizeTrigger(st->ltrig));
			state.SimulateAxisEvent(5, NormalizeTrigger(st->rtrig));
		}
	}
}

#endif
