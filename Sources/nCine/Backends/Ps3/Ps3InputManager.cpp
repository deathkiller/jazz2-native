#if defined(WITH_PS3)

#include "Ps3InputManager.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cmath>
#include <cstring>

#include <io/pad.h>
#include <sysutil/sysutil.h>

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 4;
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

		/**
			@brief Set once the XMB has asked the title to exit

			File scope rather than a member because the sysutil callback below is a plain C function pointer
			the firmware calls back, so it has no access to the manager's private state.
		*/
		bool _sysUtilExitRequested = false;

		inline float NormalizeStick(std::uint32_t value)
		{
			// The nubs report 0..255 with 128 at rest; map onto -1..1 with an exact zero at centre
			const std::int32_t centered = std::int32_t(value) - 128;
			return (centered < 0 ? float(centered) / 128.0f : float(centered) / 127.0f);
		}

		inline float NormalizeTrigger(std::uint32_t value)
		{
			// The analogue shoulder pressures are 0..255; the engine's trigger axes run -1..1
			return (float(value) / 255.0f) * 2.0f - 1.0f;
		}

		/**
			@brief XMB event sink

			Runs on the caller of `sysUtilCheckCallback()`, which the input poll below drives once per frame,
			so it is on the main thread and may touch the manager's state directly.
		*/
		void SysUtilCallback(std::uint64_t status, std::uint64_t param, void* userData)
		{
			static_cast<void>(param);
			static_cast<void>(userData);

			switch (status) {
				case SYSUTIL_EXIT_GAME:
					// The XMB asked the title to quit (the PS button's "Quit Game", or a shutdown). There is no
					// way to refuse it, and a title that does not exit promptly is killed, so this only records
					// the request - the graphics device turns it into the engine's own quit on the next frame,
					// which lets the current frame finish and the shutdown path run normally.
					LOGI("Exit requested by the system");
					_sysUtilExitRequested = true;
					break;
				default:
					// SYSUTIL_MENU_OPEN / SYSUTIL_MENU_CLOSE and the draw notifications need no action: the game
					// keeps rendering underneath the XMB overlay, which is what the firmware expects of a title
					break;
			}
		}
	}

	Ps3InputManager::PadInfo Ps3InputManager::_pads[Ps3InputManager::MaxJoysticks];

	Ps3JoystickState::Ps3JoystickState()
		: _joyId(-1), _hatState(0)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		for (std::int32_t i = 0; i < MaxNumAxes; i++) {
			_axesValuesState[i] = (i >= 4 ? -1.0f : 0.0f);
		}
	}

	bool Ps3JoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons ? _buttonsState[buttonId] : false);
	}

	unsigned char Ps3JoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : 0);
	}

	float Ps3JoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void Ps3JoystickState::resetJoystickState(int joyId)
	{
		_joyId = joyId;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		_hatState = 0;
		for (std::int32_t i = 0; i < MaxNumAxes; i++) {
			// The trigger axes rest at -1 (fully released), the sticks at centre
			_axesValuesState[i] = (i >= 4 ? -1.0f : 0.0f);
		}
	}

	void Ps3JoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons || _buttonsState[buttonId] == pressed) {
			return;
		}
		_buttonsState[buttonId] = pressed;

		_joyButtonEvent.joyId = _joyId;
		_joyButtonEvent.buttonId = buttonId;
		if (pressed) {
			Ps3InputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
			IInputManager::handler()->OnJoyButtonPressed(_joyButtonEvent);
		} else {
			Ps3InputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
			IInputManager::handler()->OnJoyButtonReleased(_joyButtonEvent);
		}
	}

	void Ps3JoystickState::simulateHatEvent(unsigned char state)
	{
		if (_hatState == state) {
			return;
		}
		_hatState = state;

		_joyHatEvent.joyId = _joyId;
		_joyHatEvent.hatId = 0;
		_joyHatEvent.hatState = state;
		Ps3InputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
		IInputManager::handler()->OnJoyHatMoved(_joyHatEvent);
	}

	void Ps3JoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes ||
			std::fabs(_axesValuesState[axisId] - value) < AxisEventTolerance) {
			return;
		}
		_axesValuesState[axisId] = value;

		_joyAxisEvent.joyId = _joyId;
		_joyAxisEvent.axisId = axisId;
		_joyAxisEvent.value = value;
		Ps3InputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
		IInputManager::handler()->OnJoyAxisMoved(_joyAxisEvent);
	}

	Ps3InputManager::Ps3InputManager()
	{
		_joyMapping.Init(this);

		// Slot 0 is the conventional one for a title's own handler; the firmware allows several, but the
		// engine has exactly one place that cares about these events
		if (sysUtilRegisterCallback(0, SysUtilCallback, nullptr) != 0) {
			LOGE("Cannot register the system utility callback, the XMB quit request will not be seen");
		}

		if (ioPadInit(MaxJoysticks) != 0) {
			LOGE("Cannot initialize the pad library, gamepads will not work");
		}

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			_pads[i].State.resetJoystickState(i);
		}

		// Poll once so an already-connected controller connects before the first frame
		updateJoystickStates();
	}

	Ps3InputManager::~Ps3InputManager()
	{
		ioPadEnd();
		sysUtilUnregisterCallback(0);
	}

	const MouseState& Ps3InputManager::mouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& Ps3InputManager::keyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool Ps3InputManager::isJoyPresent(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxJoysticks && _pads[joyId].Connected);
	}

	const char* Ps3InputManager::joyName(int joyId) const
	{
		return (isJoyPresent(joyId) ? "PlayStation 3 Controller" : nullptr);
	}

	const JoystickGuid Ps3InputManager::joyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The engine's built-in "xinput" mapping is what the button order above targets
		return JoystickGuidType::Xinput;
	}

	int Ps3InputManager::joyNumButtons(int joyId) const
	{
		return (isJoyPresent(joyId) ? Ps3JoystickState::MaxNumButtons : -1);
	}

	int Ps3InputManager::joyNumHats(int joyId) const
	{
		return (isJoyPresent(joyId) ? Ps3JoystickState::MaxNumHats : -1);
	}

	int Ps3InputManager::joyNumAxes(int joyId) const
	{
		return (isJoyPresent(joyId) ? Ps3JoystickState::MaxNumAxes : -1);
	}

	const JoystickState& Ps3InputManager::joystickState(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return _pads[joyId].State;
		}
		static NullInputManager::NullJoystickState nullJoystickState;
		return nullJoystickState;
	}

	bool Ps3InputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		if (!isJoyPresent(joyId)) {
			return false;
		}

		// The DualShock 3 has the classic two-actuator arrangement: a variable-speed weight in the left grip
		// and a fixed-speed one in the right. That maps onto the engine's two-frequency request the usual
		// way round - the low-frequency channel is the heavy weight, so it takes the analogue value, and the
		// high-frequency one can only be on or off.
		//
		// The duration is not passed to the firmware, which has no timed mode: a call sets the motors until
		// the next call, so it is the engine's rumble bookkeeping that stops them.
		static_cast<void>(durationMs);

		padActParam actParam;
		std::memset(&actParam, 0, sizeof(actParam));
		actParam.large_motor = std::uint8_t(lowFrequency * 255.0f);
		actParam.small_motor = (highFrequency > 0.5f ? 1 : 0);
		return (ioPadSetActDirect(std::uint32_t(joyId), &actParam) == 0);
	}

	bool Ps3InputManager::joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		// No actuators in the triggers on any PlayStation 3 controller
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	bool Ps3InputManager::HasExitRequested()
	{
		return _sysUtilExitRequested;
	}

	void Ps3InputManager::handleConnection(std::int32_t joyId, bool connected)
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
			// A pad that goes away loses its pressure-sensitive mode with the port, so it is re-applied when
			// (and if) the same port comes back
			_pads[joyId].PressureMode = false;
			_pads[joyId].State.resetJoystickState(joyId);
			if (_inputEventHandler != nullptr) {
				_inputEventHandler->OnJoyDisconnected(_joyConnectionEvent);
			}
			_joyMapping.OnJoyDisconnected(_joyConnectionEvent);
		}
	}

	void Ps3InputManager::updateJoystickStates()
	{
		// Drains the XMB event queue onto SysUtilCallback above. This has to happen every frame: the firmware
		// treats a title that stops checking as unresponsive.
		sysUtilCheckCallback();

		padInfo2 info;
		if (ioPadGetInfo2(&info) != 0) {
			// Without the port table there is no way to tell a disconnected pad from an idle one, so every
			// port is reported gone rather than left reading its last state forever
			for (std::int32_t i = 0; i < MaxJoysticks; i++) {
				handleConnection(i, false);
			}
			return;
		}

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			// Bit 0 of port_status is the connection flag; the remaining bits report assignment changes
			if ((info.port_status[i] & 1) == 0) {
				handleConnection(i, false);
				continue;
			}

			padData data;
			if (ioPadGetData(std::uint32_t(i), &data) != 0 || data.len == 0) {
				// A connected port with no fresh report is not an error - the pad simply has not sent a packet
				// since the last poll - so the previous state stands rather than being cleared
				handleConnection(i, true);
				continue;
			}
			handleConnection(i, true);

			// Turn on the analogue button pressures once per connection, and only where the controller says it
			// has them; without this L2/R2 report zero and the triggers below fall back to their digital bits
			if (!_pads[i].PressureMode) {
				_pads[i].PressureMode = true;
				if (ioPadInfoPressMode(std::uint32_t(i)) == 1) {
					ioPadSetPortSetting(std::uint32_t(i), PAD_SETTINGS_PRESS_ON);
				}
			}

			Ps3JoystickState& joyState = _pads[i].State;

			// Unlike the PS2's active-low word, a padData bit is 1 when the button is pressed
			joyState.simulateButtonEvent(ButtonA, data.BTN_CROSS != 0);
			joyState.simulateButtonEvent(ButtonB, data.BTN_CIRCLE != 0);
			joyState.simulateButtonEvent(ButtonX, data.BTN_SQUARE != 0);
			joyState.simulateButtonEvent(ButtonY, data.BTN_TRIANGLE != 0);
			joyState.simulateButtonEvent(ButtonLShoulder, data.BTN_L1 != 0);
			joyState.simulateButtonEvent(ButtonRShoulder, data.BTN_R1 != 0);
			joyState.simulateButtonEvent(ButtonBack, data.BTN_SELECT != 0);
			joyState.simulateButtonEvent(ButtonStart, data.BTN_START != 0);
			joyState.simulateButtonEvent(ButtonLStick, data.BTN_L3 != 0);
			joyState.simulateButtonEvent(ButtonRStick, data.BTN_R3 != 0);

			unsigned char hat = 0;
			if (data.BTN_UP != 0) {
				hat |= HatUp;
			}
			if (data.BTN_RIGHT != 0) {
				hat |= HatRight;
			}
			if (data.BTN_DOWN != 0) {
				hat |= HatDown;
			}
			if (data.BTN_LEFT != 0) {
				hat |= HatLeft;
			}
			joyState.simulateHatEvent(hat);

			joyState.simulateAxisEvent(0, NormalizeStick(data.ANA_L_H));
			joyState.simulateAxisEvent(1, NormalizeStick(data.ANA_L_V));
			joyState.simulateAxisEvent(2, NormalizeStick(data.ANA_R_H));
			joyState.simulateAxisEvent(3, NormalizeStick(data.ANA_R_V));

			// Prefer the analogue pressure, but fall back to the digital bit for a pad that has none (the
			// Bluray remote, and third-party controllers that report no pressure capability). A held trigger
			// always reads a non-zero pressure, so a zero here with the bit set can only be the latter.
			joyState.simulateAxisEvent(4, data.PRE_L2 != 0
				? NormalizeTrigger(data.PRE_L2)
				: (data.BTN_L2 != 0 ? 1.0f : -1.0f));
			joyState.simulateAxisEvent(5, data.PRE_R2 != 0
				? NormalizeTrigger(data.PRE_R2)
				: (data.BTN_R2 != 0 ? 1.0f : -1.0f));
		}
	}
}

#endif
