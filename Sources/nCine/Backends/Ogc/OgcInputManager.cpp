#if defined(WITH_OGC)

#include "OgcInputManager.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cstring>

#include <gccore.h>
#include <ogc/pad.h>
#if defined(DEATH_TARGET_WII)
#	include <wiiuse/wpad.h>
#endif

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 4;
}

namespace nCine::Backends
{
	namespace
	{
		// XInput-shaped button indices (the SDL "standard gamepad" order the built-in mapping consumes)
		constexpr std::int32_t ButtonA = 0;
		constexpr std::int32_t ButtonB = 1;
		constexpr std::int32_t ButtonX = 2;
		constexpr std::int32_t ButtonY = 3;
		constexpr std::int32_t ButtonBack = 4;
		constexpr std::int32_t ButtonGuide = 5;
		constexpr std::int32_t ButtonStart = 6;
		constexpr std::int32_t ButtonLStick = 7;
		constexpr std::int32_t ButtonRStick = 8;
		constexpr std::int32_t ButtonLShoulder = 9;
		constexpr std::int32_t ButtonRShoulder = 10;

		// Event scratch (single-threaded poll, mirroring the UWP backend's static event objects)
		JoyButtonEvent _joyButtonEvent;
		JoyHatEvent _joyHatEvent;
		JoyAxisEvent _joyAxisEvent;
		JoyConnectionEvent _joyConnectionEvent;

		inline float NormalizeStick(std::int8_t value)
		{
			return (value < 0 ? float(value) / 128.0f : float(value) / 127.0f);
		}

		inline float NormalizeTrigger(std::uint8_t value)
		{
			return (float(value) / 255.0f) * 2.0f - 1.0f;
		}
	}

	OgcInputManager::PadInfo OgcInputManager::_pads[OgcInputManager::MaxJoysticks];

	OgcJoystickState::OgcJoystickState()
		: _joyId(-1), _hatState(HatState::Centered)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	bool OgcJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char OgcJoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float OgcJoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void OgcJoystickState::resetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void OgcJoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::handler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.joyId = _joyId;
			_joyButtonEvent.buttonId = buttonId;
			if (pressed) {
				OgcInputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				OgcInputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void OgcJoystickState::simulateHatEvent(unsigned char state)
	{
		if (IInputManager::handler() != nullptr && _hatState != state) {
			_joyHatEvent.joyId = _joyId;
			_joyHatEvent.hatId = 0;
			_joyHatEvent.hatState = state;
			OgcInputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::handler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	void OgcJoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::handler() != nullptr && std::abs(_axesValuesState[axisId] - value) > AxisEventTolerance) {
			_joyAxisEvent.joyId = _joyId;
			_joyAxisEvent.axisId = axisId;
			_joyAxisEvent.value = value;
			OgcInputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
			IInputManager::handler()->OnJoyAxisMoved(_joyAxisEvent);
		}
		_axesValuesState[axisId] = value;
	}

	OgcInputManager::OgcInputManager()
	{
		_joyMapping.Init(this);

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			_pads[i].State.resetJoystickState(i);
		}
		// PAD_Init()/WPAD_Init() ran in MainApplication::Run(); poll once so an already-plugged pad
		// connects before the first frame
		updateJoystickStates();
	}

	OgcInputManager::~OgcInputManager() = default;

	const MouseState& OgcInputManager::mouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& OgcInputManager::keyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool OgcInputManager::isJoyPresent(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxJoysticks && _pads[joyId].Connected);
	}

	const char* OgcInputManager::joyName(int joyId) const
	{
		static_cast<void>(joyId);
#if defined(DEATH_TARGET_WII)
		return "Nintendo Wii Controller";
#else
		return "Nintendo GameCube Controller";
#endif
	}

	const JoystickGuid OgcInputManager::joyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int OgcInputManager::joyNumButtons(int joyId) const
	{
		static_cast<void>(joyId);
		return OgcJoystickState::MaxNumButtons;
	}

	int OgcInputManager::joyNumHats(int joyId) const
	{
		static_cast<void>(joyId);
		return OgcJoystickState::MaxNumHats;
	}

	int OgcInputManager::joyNumAxes(int joyId) const
	{
		static_cast<void>(joyId);
		return OgcJoystickState::MaxNumAxes;
	}

	const JoystickState& OgcInputManager::joystickState(int joyId) const
	{
		static OgcJoystickState nullJoystickState;
		if (isJoyPresent(joyId)) {
			return _pads[joyId].State;
		}
		return nullJoystickState;
	}

	bool OgcInputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// GameCube pads only expose an on/off motor; treat any nonzero request as on for the poll period
		if (joyId < 0 || joyId >= MaxJoysticks || !_pads[joyId].Connected) {
			return false;
		}
		PAD_ControlMotor(joyId, (lowFrequency > 0.0f || highFrequency > 0.0f) ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP);
		static_cast<void>(durationMs);
		return true;
	}

	bool OgcInputManager::joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void OgcInputManager::handleConnection(std::int32_t joyId, bool connected)
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

	void OgcInputManager::updateJoystickStates()
	{
		PADStatus padStatus[PAD_CHANMAX];
		PAD_ScanPads();
		PAD_Read(padStatus);
#if defined(DEATH_TARGET_WII)
		WPAD_ScanPads();
#endif

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			bool connected = (padStatus[i].err == PAD_ERR_NONE || padStatus[i].err == PAD_ERR_NOT_READY);
			bool useGcPad = (padStatus[i].err == PAD_ERR_NONE);

#if defined(DEATH_TARGET_WII)
			// A Wii Remote (with or without a Classic Controller) on the same slot takes precedence
			std::uint32_t wpadType = 0;
			const bool wpadPresent = (WPAD_Probe(i, &wpadType) == WPAD_ERR_NONE);
			connected |= wpadPresent;
#endif
			handleConnection(i, connected);
			if (!_pads[i].Connected) {
				continue;
			}

			OgcJoystickState& state = _pads[i].State;

			if (useGcPad) {
				const std::uint16_t held = PAD_ButtonsHeld(i);
				state.simulateButtonEvent(ButtonA, (held & PAD_BUTTON_A) != 0);
				state.simulateButtonEvent(ButtonB, (held & PAD_BUTTON_B) != 0);
				state.simulateButtonEvent(ButtonX, (held & PAD_BUTTON_X) != 0);
				state.simulateButtonEvent(ButtonY, (held & PAD_BUTTON_Y) != 0);
				state.simulateButtonEvent(ButtonStart, (held & PAD_BUTTON_START) != 0);
				state.simulateButtonEvent(ButtonRShoulder, (held & PAD_TRIGGER_Z) != 0);
				state.simulateButtonEvent(ButtonLShoulder, (held & PAD_TRIGGER_L) != 0);

				unsigned char hat = HatState::Centered;
				if (held & PAD_BUTTON_UP) hat |= HatState::Up;
				if (held & PAD_BUTTON_RIGHT) hat |= HatState::Right;
				if (held & PAD_BUTTON_DOWN) hat |= HatState::Down;
				if (held & PAD_BUTTON_LEFT) hat |= HatState::Left;
				state.simulateHatEvent(hat);

				state.simulateAxisEvent(0, NormalizeStick(PAD_StickX(i)));
				state.simulateAxisEvent(1, -NormalizeStick(PAD_StickY(i)));
				state.simulateAxisEvent(2, NormalizeStick(PAD_SubStickX(i)));
				state.simulateAxisEvent(3, -NormalizeStick(PAD_SubStickY(i)));
				state.simulateAxisEvent(4, NormalizeTrigger(PAD_TriggerL(i)));
				state.simulateAxisEvent(5, NormalizeTrigger(PAD_TriggerR(i)));
				continue;
			}

#if defined(DEATH_TARGET_WII)
			if (wpadPresent) {
				const std::uint32_t held = WPAD_ButtonsHeld(i);
				WPADData* data = WPAD_Data(i);
				const bool hasClassic = (data != nullptr && data->exp.type == WPAD_EXP_CLASSIC);
				if (hasClassic) {
					state.simulateButtonEvent(ButtonA, (held & WPAD_CLASSIC_BUTTON_A) != 0);
					state.simulateButtonEvent(ButtonB, (held & WPAD_CLASSIC_BUTTON_B) != 0);
					state.simulateButtonEvent(ButtonX, (held & WPAD_CLASSIC_BUTTON_X) != 0);
					state.simulateButtonEvent(ButtonY, (held & WPAD_CLASSIC_BUTTON_Y) != 0);
					state.simulateButtonEvent(ButtonBack, (held & WPAD_CLASSIC_BUTTON_MINUS) != 0);
					state.simulateButtonEvent(ButtonGuide, (held & WPAD_CLASSIC_BUTTON_HOME) != 0);
					state.simulateButtonEvent(ButtonStart, (held & WPAD_CLASSIC_BUTTON_PLUS) != 0);
					state.simulateButtonEvent(ButtonLShoulder, (held & WPAD_CLASSIC_BUTTON_FULL_L) != 0);
					state.simulateButtonEvent(ButtonRShoulder, (held & WPAD_CLASSIC_BUTTON_FULL_R) != 0);

					unsigned char hat = HatState::Centered;
					if (held & WPAD_CLASSIC_BUTTON_UP) hat |= HatState::Up;
					if (held & WPAD_CLASSIC_BUTTON_RIGHT) hat |= HatState::Right;
					if (held & WPAD_CLASSIC_BUTTON_DOWN) hat |= HatState::Down;
					if (held & WPAD_CLASSIC_BUTTON_LEFT) hat |= HatState::Left;
					state.simulateHatEvent(hat);

					const joystick_t& left = data->exp.classic.ljs;
					const joystick_t& right = data->exp.classic.rjs;
					auto normalize = [](const joystick_t& js, bool horizontal) -> float {
						const float pos = (horizontal ? js.pos.x : js.pos.y);
						const float min = (horizontal ? js.min.x : js.min.y);
						const float max = (horizontal ? js.max.x : js.max.y);
						const float center = (horizontal ? js.center.x : js.center.y);
						const float range = (pos >= center ? max - center : center - min);
						return (range > 0.0f ? (pos - center) / range : 0.0f);
					};
					state.simulateAxisEvent(0, normalize(left, true));
					state.simulateAxisEvent(1, -normalize(left, false));
					state.simulateAxisEvent(2, normalize(right, true));
					state.simulateAxisEvent(3, -normalize(right, false));
					state.simulateAxisEvent(4, (data->exp.classic.l_shoulder * 2.0f) - 1.0f);
					state.simulateAxisEvent(5, (data->exp.classic.r_shoulder * 2.0f) - 1.0f);
				} else {
					// Sideways Wii Remote fallback: 2 for A, 1 for B, the D-pad rotated for horizontal play
					state.simulateButtonEvent(ButtonA, (held & WPAD_BUTTON_2) != 0);
					state.simulateButtonEvent(ButtonB, (held & WPAD_BUTTON_1) != 0);
					state.simulateButtonEvent(ButtonX, (held & WPAD_BUTTON_A) != 0);
					state.simulateButtonEvent(ButtonY, (held & WPAD_BUTTON_B) != 0);
					state.simulateButtonEvent(ButtonBack, (held & WPAD_BUTTON_MINUS) != 0);
					state.simulateButtonEvent(ButtonGuide, (held & WPAD_BUTTON_HOME) != 0);
					state.simulateButtonEvent(ButtonStart, (held & WPAD_BUTTON_PLUS) != 0);

					unsigned char hat = HatState::Centered;
					if (held & WPAD_BUTTON_RIGHT) hat |= HatState::Up;
					if (held & WPAD_BUTTON_DOWN) hat |= HatState::Right;
					if (held & WPAD_BUTTON_LEFT) hat |= HatState::Down;
					if (held & WPAD_BUTTON_UP) hat |= HatState::Left;
					state.simulateHatEvent(hat);
				}
			}
#endif
		}
	}
}

#endif
