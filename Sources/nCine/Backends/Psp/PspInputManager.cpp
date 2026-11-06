#if defined(WITH_PSP)

#include "PspInputManager.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cmath>
#include <cstring>

#include <pspctrl.h>
#include <psputility.h>

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

	bool PspJoystickState::IsButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char PspJoystickState::GetHatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float PspJoystickState::GetAxisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void PspJoystickState::ResetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void PspJoystickState::SimulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::GetHandler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.JoyId = _joyId;
			_joyButtonEvent.ButtonId = buttonId;
			if (pressed) {
				PspInputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::GetHandler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				PspInputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::GetHandler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void PspJoystickState::SimulateHatEvent(unsigned char state)
	{
		if (IInputManager::GetHandler() != nullptr && _hatState != state) {
			_joyHatEvent.JoyId = _joyId;
			_joyHatEvent.HatId = 0;
			_joyHatEvent.HatState = state;
			PspInputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::GetHandler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	void PspJoystickState::SimulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::GetHandler() != nullptr && std::abs(_axesValuesState[axisId] - value) > AxisEventTolerance) {
			_joyAxisEvent.JoyId = _joyId;
			_joyAxisEvent.AxisId = axisId;
			_joyAxisEvent.Value = value;
			PspInputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
			IInputManager::GetHandler()->OnJoyAxisMoved(_joyAxisEvent);
		}
		_axesValuesState[axisId] = value;
	}

	PspInputManager::PspInputManager()
	{
		_joyMapping.Init(this);

		_state.ResetJoystickState(0);

		// The analog stick is only sampled into SceCtrlData when the controller service runs in analog mode
		sceCtrlSetSamplingCycle(0);
		sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

		// Poll once so the built-in pad connects before the first frame
		UpdateJoystickStates();
	}

	PspInputManager::~PspInputManager() = default;

	const MouseState& PspInputManager::GetMouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& PspInputManager::GetKeyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool PspInputManager::IsJoyPresent(int joyId) const
	{
		return (joyId == 0 && _connected);
	}

	const char* PspInputManager::GetJoyName(int joyId) const
	{
		static_cast<void>(joyId);
		return "PlayStation Portable Controller";
	}

	const JoystickGuid PspInputManager::GetJoyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int PspInputManager::GetJoyButtonCount(int joyId) const
	{
		static_cast<void>(joyId);
		return PspJoystickState::MaxNumButtons;
	}

	int PspInputManager::GetJoyHatCount(int joyId) const
	{
		static_cast<void>(joyId);
		return PspJoystickState::MaxNumHats;
	}

	int PspInputManager::GetJoyAxisCount(int joyId) const
	{
		static_cast<void>(joyId);
		return PspJoystickState::MaxNumAxes;
	}

	const JoystickState& PspInputManager::GetJoystickState(int joyId) const
	{
		static PspJoystickState nullJoystickState;
		if (IsJoyPresent(joyId)) {
			return _state;
		}
		return nullJoystickState;
	}

	bool PspInputManager::JoystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// The PSP has no rumble hardware at all (which is why NCINE_HAS_GAMEPAD_RUMBLE is not defined for it)
		static_cast<void>(joyId);
		static_cast<void>(lowFrequency);
		static_cast<void>(highFrequency);
		static_cast<void>(durationMs);
		return false;
	}

	bool PspInputManager::JoystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void PspInputManager::UpdateJoystickStates()
	{
		SceCtrlData pad;
		if (sceCtrlPeekBufferPositive(&pad, 1) < 1) {
			return;
		}

		// The firmware's on-screen keyboard (see MainApplication::ShowScreenKeyboard) reads the pad itself but
		// does not take it away from the application, so while it is up every button it is being driven with
		// would also reach the game - Cross to confirm a key would fire the menu item behind it. The pad
		// reads as idle for as long as the dialog exists, its final state included, so the press that closes
		// it does not land in the game either. Matched against the dialog states rather than "not none": with
		// no keyboard ever started the status call returns an error code, which is not "none" either.
		const std::int32_t oskStatus = sceUtilityOskGetStatus();
		if (oskStatus >= PSP_UTILITY_DIALOG_INIT && oskStatus <= PSP_UTILITY_DIALOG_FINISHED) {
			pad.Buttons = 0;
			pad.Lx = 128;
			pad.Ly = 128;
		}

		if (!_connected) {
			_connected = true;
			_joyConnectionEvent.JoyId = 0;
			_joyMapping.OnJoyConnected(_joyConnectionEvent);
			if (_inputEventHandler != nullptr) {
				_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
			}
		}

		// The PSP face buttons sit where a DualShock's do, so they map onto the XInput names the same way
		// the engine's PlayStation gamepad handling already assumes: Cross = A, Circle = B, Square = X,
		// Triangle = Y
		_state.SimulateButtonEvent(ButtonA, (pad.Buttons & PSP_CTRL_CROSS) != 0);
		_state.SimulateButtonEvent(ButtonB, (pad.Buttons & PSP_CTRL_CIRCLE) != 0);
		_state.SimulateButtonEvent(ButtonX, (pad.Buttons & PSP_CTRL_SQUARE) != 0);
		_state.SimulateButtonEvent(ButtonY, (pad.Buttons & PSP_CTRL_TRIANGLE) != 0);
		_state.SimulateButtonEvent(ButtonLShoulder, (pad.Buttons & PSP_CTRL_LTRIGGER) != 0);
		_state.SimulateButtonEvent(ButtonRShoulder, (pad.Buttons & PSP_CTRL_RTRIGGER) != 0);
		_state.SimulateButtonEvent(ButtonBack, (pad.Buttons & PSP_CTRL_SELECT) != 0);
		_state.SimulateButtonEvent(ButtonStart, (pad.Buttons & PSP_CTRL_START) != 0);
		// Nothing on the console clicks a stick, and the Home button belongs to the firmware's exit dialog
		_state.SimulateButtonEvent(ButtonLStick, false);
		_state.SimulateButtonEvent(ButtonRStick, false);
		_state.SimulateButtonEvent(ButtonGuide, false);

		unsigned char hat = HatState::Centered;
		if (pad.Buttons & PSP_CTRL_UP) hat |= HatState::Up;
		if (pad.Buttons & PSP_CTRL_RIGHT) hat |= HatState::Right;
		if (pad.Buttons & PSP_CTRL_DOWN) hat |= HatState::Down;
		if (pad.Buttons & PSP_CTRL_LEFT) hat |= HatState::Left;
		_state.SimulateHatEvent(hat);

		_state.SimulateAxisEvent(0, NormalizeStick(pad.Lx));
		_state.SimulateAxisEvent(1, NormalizeStick(pad.Ly));
		// No right stick and no analog triggers: the shoulder buttons are digital, so their axes stay at the
		// released end of the [-1, 1] range the trigger mapping expects
		_state.SimulateAxisEvent(2, 0.0f);
		_state.SimulateAxisEvent(3, 0.0f);
		_state.SimulateAxisEvent(4, -1.0f);
		_state.SimulateAxisEvent(5, -1.0f);
	}
}

#endif
