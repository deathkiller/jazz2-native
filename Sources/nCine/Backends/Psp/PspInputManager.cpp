#if defined(WITH_PSP)

#include "PspInputManager.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cmath>
#include <cstring>

#include <pspctrl.h>

namespace nCine
{
	// One built-in pad, and no way to attach another one
	const std::int32_t IInputManager::MaxNumJoysticks = 1;
}

namespace nCine::Backends
{
	namespace
	{
		// Raw button indices of the built-in "xinput" mapping this platform resolves to (see JoyMappingDb.h).
		// These are the indices that mapping reads, NOT the order of the SDL_GameControllerButton enum -
		// see the same table in the Dc backend.
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

		inline float NormalizeStick(unsigned char value)
		{
			// SceCtrlData Lx/Ly are 0..255 with 128 nominally centered
			return (float(value) - 128.0f) / 127.0f;
		}
	}

	bool PspInputManager::_connected = false;
	PspJoystickState PspInputManager::_state;

	PspJoystickState::PspJoystickState()
		: _joyId(-1), _hatState(HatState::Centered)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	bool PspJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char PspJoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float PspJoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void PspJoystickState::resetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void PspJoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::handler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.joyId = _joyId;
			_joyButtonEvent.buttonId = buttonId;
			if (pressed) {
				PspInputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				PspInputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void PspJoystickState::simulateHatEvent(unsigned char state)
	{
		if (IInputManager::handler() != nullptr && _hatState != state) {
			_joyHatEvent.joyId = _joyId;
			_joyHatEvent.hatId = 0;
			_joyHatEvent.hatState = state;
			PspInputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::handler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	void PspJoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::handler() != nullptr && std::abs(_axesValuesState[axisId] - value) > AxisEventTolerance) {
			_joyAxisEvent.joyId = _joyId;
			_joyAxisEvent.axisId = axisId;
			_joyAxisEvent.value = value;
			PspInputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
			IInputManager::handler()->OnJoyAxisMoved(_joyAxisEvent);
		}
		_axesValuesState[axisId] = value;
	}

	PspInputManager::PspInputManager()
	{
		_joyMapping.Init(this);

		_state.resetJoystickState(0);

		// The analog stick is only sampled into SceCtrlData when the controller service runs in analog mode
		sceCtrlSetSamplingCycle(0);
		sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

		// Poll once so the built-in pad connects before the first frame
		updateJoystickStates();
	}

	PspInputManager::~PspInputManager() = default;

	const MouseState& PspInputManager::mouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& PspInputManager::keyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool PspInputManager::isJoyPresent(int joyId) const
	{
		return (joyId == 0 && _connected);
	}

	const char* PspInputManager::joyName(int joyId) const
	{
		static_cast<void>(joyId);
		return "PlayStation Portable Controller";
	}

	const JoystickGuid PspInputManager::joyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int PspInputManager::joyNumButtons(int joyId) const
	{
		static_cast<void>(joyId);
		return PspJoystickState::MaxNumButtons;
	}

	int PspInputManager::joyNumHats(int joyId) const
	{
		static_cast<void>(joyId);
		return PspJoystickState::MaxNumHats;
	}

	int PspInputManager::joyNumAxes(int joyId) const
	{
		static_cast<void>(joyId);
		return PspJoystickState::MaxNumAxes;
	}

	const JoystickState& PspInputManager::joystickState(int joyId) const
	{
		static PspJoystickState nullJoystickState;
		if (isJoyPresent(joyId)) {
			return _state;
		}
		return nullJoystickState;
	}

	bool PspInputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// The PSP has no rumble hardware at all (which is why NCINE_HAS_GAMEPAD_RUMBLE is not defined for it)
		static_cast<void>(joyId);
		static_cast<void>(lowFrequency);
		static_cast<void>(highFrequency);
		static_cast<void>(durationMs);
		return false;
	}

	bool PspInputManager::joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void PspInputManager::updateJoystickStates()
	{
		SceCtrlData pad;
		if (sceCtrlPeekBufferPositive(&pad, 1) < 1) {
			return;
		}

		if (!_connected) {
			_connected = true;
			_joyConnectionEvent.joyId = 0;
			_joyMapping.OnJoyConnected(_joyConnectionEvent);
			if (_inputEventHandler != nullptr) {
				_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
			}
		}

		// The PSP face buttons sit where a DualShock's do, so they map onto the XInput names the same way
		// the engine's PlayStation gamepad handling already assumes: Cross = A, Circle = B, Square = X,
		// Triangle = Y
		_state.simulateButtonEvent(ButtonA, (pad.Buttons & PSP_CTRL_CROSS) != 0);
		_state.simulateButtonEvent(ButtonB, (pad.Buttons & PSP_CTRL_CIRCLE) != 0);
		_state.simulateButtonEvent(ButtonX, (pad.Buttons & PSP_CTRL_SQUARE) != 0);
		_state.simulateButtonEvent(ButtonY, (pad.Buttons & PSP_CTRL_TRIANGLE) != 0);
		_state.simulateButtonEvent(ButtonLShoulder, (pad.Buttons & PSP_CTRL_LTRIGGER) != 0);
		_state.simulateButtonEvent(ButtonRShoulder, (pad.Buttons & PSP_CTRL_RTRIGGER) != 0);
		_state.simulateButtonEvent(ButtonBack, (pad.Buttons & PSP_CTRL_SELECT) != 0);
		_state.simulateButtonEvent(ButtonStart, (pad.Buttons & PSP_CTRL_START) != 0);
		// Nothing on the console clicks a stick, and the Home button belongs to the firmware's exit dialog
		_state.simulateButtonEvent(ButtonLStick, false);
		_state.simulateButtonEvent(ButtonRStick, false);
		_state.simulateButtonEvent(ButtonGuide, false);

		unsigned char hat = HatState::Centered;
		if (pad.Buttons & PSP_CTRL_UP) hat |= HatState::Up;
		if (pad.Buttons & PSP_CTRL_RIGHT) hat |= HatState::Right;
		if (pad.Buttons & PSP_CTRL_DOWN) hat |= HatState::Down;
		if (pad.Buttons & PSP_CTRL_LEFT) hat |= HatState::Left;
		_state.simulateHatEvent(hat);

		_state.simulateAxisEvent(0, NormalizeStick(pad.Lx));
		_state.simulateAxisEvent(1, NormalizeStick(pad.Ly));
		// No right stick and no analog triggers: the shoulder buttons are digital, so their axes stay at the
		// released end of the [-1, 1] range the trigger mapping expects
		_state.simulateAxisEvent(2, 0.0f);
		_state.simulateAxisEvent(3, 0.0f);
		_state.simulateAxisEvent(4, -1.0f);
		_state.simulateAxisEvent(5, -1.0f);
	}
}

#endif
