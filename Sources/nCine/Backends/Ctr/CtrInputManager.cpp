#if defined(WITH_CTR)

#include "CtrInputManager.h"
#include "../../Input/JoyMapping.h"
#include "../../Input/IInputEventHandler.h"
#include "../../../Main.h"

#include <cmath>
#include <cstring>

#include <3ds.h>

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

		// The Circle Pad reports about -156..156 on each axis at full deflection (the HID service's own
		// documented range is +-0x9C), the C-Stick of a New 3DS about -146..146; anything past that is
		// clamped rather than trusted, a worn pad occasionally reads a little beyond its nominal range
		inline float NormalizeStick(std::int16_t value, float range)
		{
			const float normalized = float(value) / range;
			return (normalized < -1.0f ? -1.0f : (normalized > 1.0f ? 1.0f : normalized));
		}
	}

	bool CtrInputManager::_connected = false;
	CtrJoystickState CtrInputManager::_state;

	CtrJoystickState::CtrJoystickState()
		: _joyId(-1), _hatState(HatState::Centered)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	bool CtrJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char CtrJoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float CtrJoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void CtrJoystickState::resetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void CtrJoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::handler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.joyId = _joyId;
			_joyButtonEvent.buttonId = buttonId;
			if (pressed) {
				CtrInputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				CtrInputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::handler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void CtrJoystickState::simulateHatEvent(unsigned char state)
	{
		if (IInputManager::handler() != nullptr && _hatState != state) {
			_joyHatEvent.joyId = _joyId;
			_joyHatEvent.hatId = 0;
			_joyHatEvent.hatState = state;
			CtrInputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::handler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	void CtrJoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::handler() != nullptr && std::abs(_axesValuesState[axisId] - value) > AxisEventTolerance) {
			_joyAxisEvent.joyId = _joyId;
			_joyAxisEvent.axisId = axisId;
			_joyAxisEvent.value = value;
			CtrInputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
			IInputManager::handler()->OnJoyAxisMoved(_joyAxisEvent);
		}
		_axesValuesState[axisId] = value;
	}

	CtrInputManager::CtrInputManager()
	{
		_joyMapping.Init(this);

		// The HID service is up from libctru's own startup (hidInit() runs before main()), so the pad is
		// present from the first frame and connects right away
		_state.resetJoystickState(0);
		_connected = true;
		_joyConnectionEvent.joyId = 0;
		_joyMapping.OnJoyConnected(_joyConnectionEvent);
		if (_inputEventHandler != nullptr) {
			_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
		}

		updateJoystickStates();
	}

	CtrInputManager::~CtrInputManager() = default;

	const MouseState& CtrInputManager::mouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& CtrInputManager::keyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool CtrInputManager::isJoyPresent(int joyId) const
	{
		return (joyId == 0 && _connected);
	}

	const char* CtrInputManager::joyName(int joyId) const
	{
		static_cast<void>(joyId);
		return "Nintendo 3DS";
	}

	const JoystickGuid CtrInputManager::joyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int CtrInputManager::joyNumButtons(int joyId) const
	{
		static_cast<void>(joyId);
		return CtrJoystickState::MaxNumButtons;
	}

	int CtrInputManager::joyNumHats(int joyId) const
	{
		static_cast<void>(joyId);
		return CtrJoystickState::MaxNumHats;
	}

	int CtrInputManager::joyNumAxes(int joyId) const
	{
		static_cast<void>(joyId);
		return CtrJoystickState::MaxNumAxes;
	}

	const JoystickState& CtrInputManager::joystickState(int joyId) const
	{
		static CtrJoystickState nullJoystickState;
		if (isJoyPresent(joyId)) {
			return _state;
		}
		return nullJoystickState;
	}

	bool CtrInputManager::joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// The console has no rumble
		static_cast<void>(joyId);
		static_cast<void>(lowFrequency);
		static_cast<void>(highFrequency);
		static_cast<void>(durationMs);
		return false;
	}

	bool CtrInputManager::joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void CtrInputManager::updateJoystickStates()
	{
		if (!_connected) {
			return;
		}

		hidScanInput();
		const std::uint32_t held = hidKeysHeld();

		// The buttons under the console's own labels (A on the right, B at the bottom, X on top, Y on the
		// left), SELECT as Back and START as Start; there is no Guide button and no stick buttons
		_state.simulateButtonEvent(ButtonA, (held & KEY_A) != 0);
		_state.simulateButtonEvent(ButtonB, (held & KEY_B) != 0);
		_state.simulateButtonEvent(ButtonX, (held & KEY_X) != 0);
		_state.simulateButtonEvent(ButtonY, (held & KEY_Y) != 0);
		_state.simulateButtonEvent(ButtonLShoulder, (held & KEY_L) != 0);
		_state.simulateButtonEvent(ButtonRShoulder, (held & KEY_R) != 0);
		_state.simulateButtonEvent(ButtonBack, (held & KEY_SELECT) != 0);
		_state.simulateButtonEvent(ButtonStart, (held & KEY_START) != 0);

		// The D-pad alone (KEY_UP and friends fold the Circle Pad in, which would move the hat with the stick)
		unsigned char hat = HatState::Centered;
		if (held & KEY_DUP) hat |= HatState::Up;
		if (held & KEY_DRIGHT) hat |= HatState::Right;
		if (held & KEY_DDOWN) hat |= HatState::Down;
		if (held & KEY_DLEFT) hat |= HatState::Left;
		_state.simulateHatEvent(hat);

		// The Circle Pad's Y grows upwards, the XInput layout's downwards
		circlePosition circle {};
		hidCircleRead(&circle);
		_state.simulateAxisEvent(0, NormalizeStick(circle.dx, 156.0f));
		_state.simulateAxisEvent(1, -NormalizeStick(circle.dy, 156.0f));

		// The C-Stick and ZL/ZR only exist on a New 3DS; the HID service answers zeros for them elsewhere,
		// which reads as a centered stick and released triggers
		circlePosition cstick {};
		hidCstickRead(&cstick);
		_state.simulateAxisEvent(2, NormalizeStick(cstick.dx, 146.0f));
		_state.simulateAxisEvent(3, -NormalizeStick(cstick.dy, 146.0f));
		// A trigger axis rests at -1 and reads +1 when fully pressed; ZL/ZR are digital, so those are the
		// only two values they ever take
		_state.simulateAxisEvent(4, (held & KEY_ZL) != 0 ? 1.0f : -1.0f);
		_state.simulateAxisEvent(5, (held & KEY_ZR) != 0 ? 1.0f : -1.0f);
	}
}

#endif
