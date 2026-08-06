#if defined(WITH_PS2)

#include "Ps2InputManager.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cmath>
#include <cstring>

extern "C" {
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>
}

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 2;
}

namespace nCine::Backends
{
	namespace
	{
		// Raw button indices of the built-in "xinput" mapping this platform resolves to (see JoyMappingDb.h).
		// These are the indices that mapping reads, NOT the order of the SDL_GameControllerButton enum.
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

		// Hat bitmask, the SDL order the mapping database expects ("dpup:h0.1" ... "dpleft:h0.8")
		constexpr unsigned char HatUp = 1;
		constexpr unsigned char HatRight = 2;
		constexpr unsigned char HatDown = 4;
		constexpr unsigned char HatLeft = 8;

		// Event scratch (single-threaded poll)
		JoyButtonEvent _joyButtonEvent;
		JoyHatEvent _joyHatEvent;
		JoyAxisEvent _joyAxisEvent;
		JoyConnectionEvent _joyConnectionEvent;

		/** @brief Pad ports polled; kept here as well so the buffers below can be sized without the class */
		constexpr std::int32_t PadPortCount = 2;

		/**
			@brief DMA-visible pad buffers, one per port

			`padPortOpen()` keeps writing into whatever it is given for the lifetime of the port, and the
			transfer is a DMA, so each buffer must stay alive and be 64-byte aligned.
		*/
		alignas(64) std::uint8_t _padBuffer[PadPortCount][256];

		inline float NormalizeStick(std::uint8_t value)
		{
			// libpad's stick axes are 0..255 with 128 at rest; map onto -1..1 with an exact zero at centre
			const std::int32_t centered = std::int32_t(value) - 128;
			return (centered < 0 ? float(centered) / 128.0f : float(centered) / 127.0f);
		}

		inline float NormalizeTrigger(std::uint8_t value)
		{
			// The analogue shoulder pressures are 0..255; the engine's trigger axes run -1..1
			return (float(value) / 255.0f) * 2.0f - 1.0f;
		}
	}

	Ps2InputManager::PadInfo Ps2InputManager::_pads[Ps2InputManager::MaxJoysticks];

	Ps2JoystickState::Ps2JoystickState()
		: _joyId(-1), _hatState(0)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		for (std::int32_t i = 0; i < MaxNumAxes; i++) {
			_axesValuesState[i] = (i >= 4 ? -1.0f : 0.0f);
		}
	}

	bool Ps2JoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons ? _buttonsState[buttonId] : false);
	}

	unsigned char Ps2JoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : 0);
	}

	float Ps2JoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void Ps2JoystickState::resetJoystickState(int joyId)
	{
		_joyId = joyId;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		_hatState = 0;
		for (std::int32_t i = 0; i < MaxNumAxes; i++) {
			// The trigger axes rest at -1 (fully released), the sticks at centre
			_axesValuesState[i] = (i >= 4 ? -1.0f : 0.0f);
		}
	}

	void Ps2JoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons || _buttonsState[buttonId] == pressed) {
			return;
		}
		_buttonsState[buttonId] = pressed;

		_joyButtonEvent.joyId = _joyId;
		_joyButtonEvent.buttonId = buttonId;
		if (pressed) {
			Ps2InputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
			IInputManager::handler()->OnJoyButtonPressed(_joyButtonEvent);
		} else {
			Ps2InputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
			IInputManager::handler()->OnJoyButtonReleased(_joyButtonEvent);
		}
	}

	void Ps2JoystickState::simulateHatEvent(unsigned char state)
	{
		if (_hatState == state) {
			return;
		}
		_hatState = state;

		_joyHatEvent.joyId = _joyId;
		_joyHatEvent.hatId = 0;
		_joyHatEvent.hatState = state;
		Ps2InputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
		IInputManager::handler()->OnJoyHatMoved(_joyHatEvent);
	}

	void Ps2JoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes ||
			std::fabs(_axesValuesState[axisId] - value) < AxisEventTolerance) {
			return;
		}
		_axesValuesState[axisId] = value;

		_joyAxisEvent.joyId = _joyId;
		_joyAxisEvent.axisId = axisId;
		_joyAxisEvent.value = value;
		Ps2InputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
		IInputManager::handler()->OnJoyAxisMoved(_joyAxisEvent);
	}

	Ps2InputManager::Ps2InputManager()
	{
		_joyMapping.Init(this);

		// The pad libraries live in IRX modules; without them padInit() has nothing to talk to. Both are in
		// ROM, so nothing has to be embedded in the ELF.
		SifInitRpc(0);
		if (SifLoadModule("rom0:SIO2MAN", 0, nullptr) < 0) {
			LOGE("Cannot load rom0:SIO2MAN, gamepads will not work");
		}
		if (SifLoadModule("rom0:PADMAN", 0, nullptr) < 0) {
			LOGE("Cannot load rom0:PADMAN, gamepads will not work");
		}
		padInit(0);

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			_pads[i].State.resetJoystickState(i);
			_pads[i].PortOpen = (padPortOpen(int(i), 0, _padBuffer[i]) != 0);
			if (!_pads[i].PortOpen) {
				LOGW("Cannot open pad port {}", i);
			}
		}

		// Poll once so an already-plugged controller connects before the first frame
		updateJoystickStates();
	}

	Ps2InputManager::~Ps2InputManager()
	{
		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			if (_pads[i].PortOpen) {
				padPortClose(int(i), 0);
				_pads[i].PortOpen = false;
			}
		}
	}

	const MouseState& Ps2InputManager::mouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& Ps2InputManager::keyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool Ps2InputManager::isJoyPresent(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxJoysticks && _pads[joyId].Connected);
	}

	const char* Ps2InputManager::joyName(int joyId) const
	{
		return (isJoyPresent(joyId) ? "PlayStation 2 Controller" : nullptr);
	}

	const JoystickGuid Ps2InputManager::joyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The engine's built-in "xinput" mapping is what the button order above targets
		return JoystickGuidType::Xinput;
	}

	int Ps2InputManager::joyNumButtons(int joyId) const
	{
		return (isJoyPresent(joyId) ? Ps2JoystickState::MaxNumButtons : -1);
	}

	int Ps2InputManager::joyNumHats(int joyId) const
	{
		return (isJoyPresent(joyId) ? Ps2JoystickState::MaxNumHats : -1);
	}

	int Ps2InputManager::joyNumAxes(int joyId) const
	{
		return (isJoyPresent(joyId) ? Ps2JoystickState::MaxNumAxes : -1);
	}

	const JoystickState& Ps2InputManager::joystickState(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return _pads[joyId].State;
		}
		static NullInputManager::NullJoystickState nullJoystickState;
		return nullJoystickState;
	}

	bool Ps2InputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// DualShock 2 actuators are reachable through padSetActDirect(), but they need the pad put into a
		// mode that also changes how its buttons report; not worth it for the engine's short rumble pulses
		static_cast<void>(joyId);
		static_cast<void>(lowFrequency);
		static_cast<void>(highFrequency);
		static_cast<void>(durationMs);
		return false;
	}

	bool Ps2InputManager::joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void Ps2InputManager::handleConnection(std::int32_t joyId, bool connected)
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

	void Ps2InputManager::updateJoystickStates()
	{
		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			if (!_pads[i].PortOpen) {
				handleConnection(i, false);
				continue;
			}

			// Only a STABLE or FINDCTP1 port has a controller that can be read; everything else is a port
			// still negotiating, or empty
			const std::int32_t state = padGetState(int(i), 0);
			if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) {
				handleConnection(i, false);
				continue;
			}

			padButtonStatus status;
			if (padRead(int(i), 0, &status) == 0) {
				handleConnection(i, false);
				continue;
			}
			handleConnection(i, true);

			// The button word is ACTIVE LOW - a pressed button reads 0 - so it is inverted once here and
			// everything below tests it the obvious way round
			const std::uint32_t buttons = std::uint32_t(0xFFFFu ^ status.btns);

			Ps2JoystickState& joyState = _pads[i].State;

			joyState.simulateButtonEvent(ButtonA, (buttons & PAD_CROSS) != 0);
			joyState.simulateButtonEvent(ButtonB, (buttons & PAD_CIRCLE) != 0);
			joyState.simulateButtonEvent(ButtonX, (buttons & PAD_SQUARE) != 0);
			joyState.simulateButtonEvent(ButtonY, (buttons & PAD_TRIANGLE) != 0);
			joyState.simulateButtonEvent(ButtonLShoulder, (buttons & PAD_L1) != 0);
			joyState.simulateButtonEvent(ButtonRShoulder, (buttons & PAD_R1) != 0);
			joyState.simulateButtonEvent(ButtonBack, (buttons & PAD_SELECT) != 0);
			joyState.simulateButtonEvent(ButtonStart, (buttons & PAD_START) != 0);
			joyState.simulateButtonEvent(ButtonLStick, (buttons & PAD_L3) != 0);
			joyState.simulateButtonEvent(ButtonRStick, (buttons & PAD_R3) != 0);

			unsigned char hat = 0;
			if ((buttons & PAD_UP) != 0) {
				hat |= HatUp;
			}
			if ((buttons & PAD_RIGHT) != 0) {
				hat |= HatRight;
			}
			if ((buttons & PAD_DOWN) != 0) {
				hat |= HatDown;
			}
			if ((buttons & PAD_LEFT) != 0) {
				hat |= HatLeft;
			}
			joyState.simulateHatEvent(hat);

			// A digital pad (or one whose mode has not settled) reports its sticks at rest rather than
			// centred, so the analogue axes are only trusted once the pad says it is in analogue mode
			const bool analogue = (status.mode >> 4) == 0x7;
			if (analogue) {
				joyState.simulateAxisEvent(0, NormalizeStick(status.ljoy_h));
				joyState.simulateAxisEvent(1, NormalizeStick(status.ljoy_v));
				joyState.simulateAxisEvent(2, NormalizeStick(status.rjoy_h));
				joyState.simulateAxisEvent(3, NormalizeStick(status.rjoy_v));
			} else {
				joyState.simulateAxisEvent(0, 0.0f);
				joyState.simulateAxisEvent(1, 0.0f);
				joyState.simulateAxisEvent(2, 0.0f);
				joyState.simulateAxisEvent(3, 0.0f);
			}

			// L2/R2 are digital in the default mode, so they drive the trigger axes from the button bits
			joyState.simulateAxisEvent(4, (buttons & PAD_L2) != 0 ? 1.0f : -1.0f);
			joyState.simulateAxisEvent(5, (buttons & PAD_R2) != 0 ? 1.0f : -1.0f);
		}
	}
}

#endif
