#if defined(WITH_N64)

#include "N64InputManager.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cmath>
#include <cstring>

#include <joypad.h>

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
		// the two differ from the shoulders onwards (see the Dc backend for the history).
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

		inline float NormalizeStick(std::int32_t value, std::int32_t range)
		{
			// joypad_inputs_t sticks are nominally -127..127, but an OEM N64 stick only reaches about
			// +/-85 (worn ones less, GameCube pads about +/-100), so the hardware range counts as full
			// deflection and anything beyond it clamps
			const float result = float(value) / float(range);
			return (result < -1.0f ? -1.0f : (result > 1.0f ? 1.0f : result));
		}

		inline float NormalizeTrigger(std::int32_t value)
		{
			// joypad_inputs_t analog triggers report about 200 at full pressure
			const float result = (float(value) / 200.0f) * 2.0f - 1.0f;
			return (result > 1.0f ? 1.0f : result);
		}
	}

	N64InputManager::PadInfo N64InputManager::_pads[N64InputManager::MaxJoysticks];

	N64JoystickState::N64JoystickState()
		: _joyId(-1), _hatState(HatState::Centered)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	bool N64JoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char N64JoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float N64JoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void N64JoystickState::resetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void N64JoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::handler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.joyId = _joyId;
			_joyButtonEvent.buttonId = buttonId;
			if (pressed) {
				N64InputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				N64InputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void N64JoystickState::simulateHatEvent(unsigned char state)
	{
		if (IInputManager::handler() != nullptr && _hatState != state) {
			_joyHatEvent.joyId = _joyId;
			_joyHatEvent.hatId = 0;
			_joyHatEvent.hatState = state;
			N64InputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::handler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	void N64JoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::handler() != nullptr && std::abs(_axesValuesState[axisId] - value) > AxisEventTolerance) {
			_joyAxisEvent.joyId = _joyId;
			_joyAxisEvent.axisId = axisId;
			_joyAxisEvent.value = value;
			N64InputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
			IInputManager::handler()->OnJoyAxisMoved(_joyAxisEvent);
		}
		_axesValuesState[axisId] = value;
	}

	N64InputManager::N64InputManager()
	{
		joypad_init();

		_joyMapping.Init(this);

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			_pads[i].State.resetJoystickState(i);
		}
		// Poll once so an already-plugged controller connects before the first frame
		updateJoystickStates();
	}

	N64InputManager::~N64InputManager() = default;

	const MouseState& N64InputManager::mouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& N64InputManager::keyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool N64InputManager::isJoyPresent(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxJoysticks && _pads[joyId].Connected);
	}

	const char* N64InputManager::joyName(int joyId) const
	{
		if (joyId >= 0 && joyId < MaxJoysticks && joypad_get_style(joypad_port_t(joyId)) == JOYPAD_STYLE_GCN) {
			return "GameCube Controller";
		}
		return "Nintendo 64 Controller";
	}

	const JoystickGuid N64InputManager::joyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int N64InputManager::joyNumButtons(int joyId) const
	{
		static_cast<void>(joyId);
		return N64JoystickState::MaxNumButtons;
	}

	int N64InputManager::joyNumHats(int joyId) const
	{
		static_cast<void>(joyId);
		return N64JoystickState::MaxNumHats;
	}

	int N64InputManager::joyNumAxes(int joyId) const
	{
		static_cast<void>(joyId);
		return N64JoystickState::MaxNumAxes;
	}

	const JoystickState& N64InputManager::joystickState(int joyId) const
	{
		static N64JoystickState nullJoystickState;
		if (isJoyPresent(joyId)) {
			return _pads[joyId].State;
		}
		return nullJoystickState;
	}

	bool N64InputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// The Rumble Pak motor is on/off only and there is no firmware timer to hand a duration to, so
		// any nonzero request switches it on and a zero request switches it off - the engine refreshes
		// rumble every frame, which is what keeps the timing honest
		if (joyId < 0 || joyId >= MaxJoysticks || !_pads[joyId].Connected) {
			return false;
		}
		const joypad_port_t port = joypad_port_t(joyId);
		if (!joypad_get_rumble_supported(port)) {
			return false;
		}
		joypad_set_rumble_active(port, (lowFrequency > 0.0f || highFrequency > 0.0f));
		static_cast<void>(durationMs);
		return true;
	}

	bool N64InputManager::joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		// No controller on this console has trigger motors
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void N64InputManager::handleConnection(std::int32_t joyId, bool connected)
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

	void N64InputManager::updateJoystickStates()
	{
		joypad_poll();

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			const joypad_port_t port = joypad_port_t(i);
			const joypad_style_t style = joypad_get_style(port);
			// The N64 Mouse also answers as a joypad but has none of the inputs the game needs
			const bool connected = (style == JOYPAD_STYLE_N64 || style == JOYPAD_STYLE_GCN);
			handleConnection(i, connected);
			if (!connected) {
				continue;
			}

			const joypad_inputs_t inputs = joypad_get_inputs(port);
			N64JoystickState& state = _pads[i].State;

			state.simulateButtonEvent(ButtonA, inputs.btn.a != 0);
			state.simulateButtonEvent(ButtonStart, inputs.btn.start != 0);
			state.simulateButtonEvent(ButtonLShoulder, inputs.btn.l != 0);
			state.simulateButtonEvent(ButtonRShoulder, inputs.btn.r != 0);
			// Nothing on either pad clicks a stick, and neither has a Guide button
			state.simulateButtonEvent(ButtonLStick, false);
			state.simulateButtonEvent(ButtonRStick, false);
			state.simulateButtonEvent(ButtonGuide, false);

			unsigned char hat = HatState::Centered;
			if (inputs.btn.d_up) hat |= HatState::Up;
			if (inputs.btn.d_right) hat |= HatState::Right;
			if (inputs.btn.d_down) hat |= HatState::Down;
			if (inputs.btn.d_left) hat |= HatState::Left;
			state.simulateHatEvent(hat);

			if (style == JOYPAD_STYLE_GCN) {
				state.simulateButtonEvent(ButtonB, inputs.btn.b != 0);
				state.simulateButtonEvent(ButtonX, inputs.btn.x != 0);
				state.simulateButtonEvent(ButtonY, inputs.btn.y != 0);
				// The GameCube pad has no Back button, so Z stands in for it (like the Ogc backend)
				state.simulateButtonEvent(ButtonBack, inputs.btn.z != 0);

				state.simulateAxisEvent(0, NormalizeStick(inputs.stick_x, 100));
				state.simulateAxisEvent(1, -NormalizeStick(inputs.stick_y, 100));
				state.simulateAxisEvent(2, NormalizeStick(inputs.cstick_x, 76));
				state.simulateAxisEvent(3, -NormalizeStick(inputs.cstick_y, 76));
				state.simulateAxisEvent(4, NormalizeTrigger(inputs.analog_l));
				state.simulateAxisEvent(5, NormalizeTrigger(inputs.analog_r));
			} else {
				// B is the pad's secondary action and sits where X does on an XInput layout, keeping
				// A as jump/confirm and B as shoot/back the way the built-in mapping expects them
				state.simulateButtonEvent(ButtonX, inputs.btn.b != 0);
				state.simulateButtonEvent(ButtonB, false);
				state.simulateButtonEvent(ButtonY, false);
				state.simulateButtonEvent(ButtonBack, false);

				state.simulateAxisEvent(0, NormalizeStick(inputs.stick_x, 85));
				state.simulateAxisEvent(1, -NormalizeStick(inputs.stick_y, 85));
				// The C buttons stand in for the right stick, so camera/secondary functions keep working
				state.simulateAxisEvent(2, (inputs.btn.c_right ? 1.0f : (inputs.btn.c_left ? -1.0f : 0.0f)));
				state.simulateAxisEvent(3, (inputs.btn.c_down ? 1.0f : (inputs.btn.c_up ? -1.0f : 0.0f)));
				// Z is the pad's main trigger, published as the right trigger at full pull; the analog
				// trigger fields only mirror the digital L/R shoulders on this pad, so they stay unused
				state.simulateAxisEvent(4, -1.0f);
				state.simulateAxisEvent(5, (inputs.btn.z ? 1.0f : -1.0f));
			}
		}
	}
}

#endif
