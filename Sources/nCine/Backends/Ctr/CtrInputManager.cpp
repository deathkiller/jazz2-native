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
	bool CtrInputManager::_discardUntilReleased = false;
	CtrJoystickState CtrInputManager::_state;

	CtrJoystickState::CtrJoystickState()
		: _joyId(-1), _hatState(HatState::Centered)
	{
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	bool CtrJoystickState::IsButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && _buttonsState[buttonId]);
	}

	unsigned char CtrJoystickState::GetHatState(int hatId) const
	{
		return (hatId == 0 ? _hatState : static_cast<unsigned char>(HatState::Centered));
	}

	float CtrJoystickState::GetAxisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? _axesValuesState[axisId] : 0.0f);
	}

	void CtrJoystickState::ResetJoystickState(int joyId)
	{
		_joyId = joyId;
		_hatState = HatState::Centered;
		std::memset(_buttonsState, 0, sizeof(_buttonsState));
		std::memset(_axesValuesState, 0, sizeof(_axesValuesState));
	}

	void CtrJoystickState::SimulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::GetHandler() != nullptr && _buttonsState[buttonId] != pressed) {
			_joyButtonEvent.JoyId = _joyId;
			_joyButtonEvent.ButtonId = buttonId;
			if (pressed) {
				CtrInputManager::_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				IInputManager::GetHandler()->OnJoyButtonPressed(_joyButtonEvent);
			} else {
				CtrInputManager::_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				IInputManager::GetHandler()->OnJoyButtonReleased(_joyButtonEvent);
			}
		}
		_buttonsState[buttonId] = pressed;
	}

	void CtrJoystickState::SimulateHatEvent(unsigned char state)
	{
		if (IInputManager::GetHandler() != nullptr && _hatState != state) {
			_joyHatEvent.JoyId = _joyId;
			_joyHatEvent.HatId = 0;
			_joyHatEvent.HatState = state;
			CtrInputManager::_joyMapping.OnJoyHatMoved(_joyHatEvent);
			IInputManager::GetHandler()->OnJoyHatMoved(_joyHatEvent);
		}
		_hatState = state;
	}

	void CtrJoystickState::SimulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::GetHandler() != nullptr && std::abs(_axesValuesState[axisId] - value) > AxisEventTolerance) {
			_joyAxisEvent.JoyId = _joyId;
			_joyAxisEvent.AxisId = axisId;
			_joyAxisEvent.Value = value;
			CtrInputManager::_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
			IInputManager::GetHandler()->OnJoyAxisMoved(_joyAxisEvent);
		}
		_axesValuesState[axisId] = value;
	}

	CtrInputManager::CtrInputManager()
	{
		_joyMapping.Init(this);

		// The HID service is up from libctru's own startup (hidInit() runs before main()), so the pad is
		// present from the first frame and connects right away
		_state.ResetJoystickState(0);
		_connected = true;
		_joyConnectionEvent.JoyId = 0;
		_joyMapping.OnJoyConnected(_joyConnectionEvent);
		if (_inputEventHandler != nullptr) {
			_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
		}

		UpdateJoystickStates();
	}

	CtrInputManager::~CtrInputManager() = default;

	const MouseState& CtrInputManager::GetMouseState() const
	{
		static NullInputManager::NullMouseState nullMouseState;
		return nullMouseState;
	}

	const KeyboardState& CtrInputManager::GetKeyboardState() const
	{
		static NullInputManager::NullKeyboardState nullKeyboardState;
		return nullKeyboardState;
	}

	bool CtrInputManager::IsJoyPresent(int joyId) const
	{
		return (joyId == 0 && _connected);
	}

	const char* CtrInputManager::GetJoyName(int joyId) const
	{
		static_cast<void>(joyId);
		return "Nintendo 3DS";
	}

	const JoystickGuid CtrInputManager::GetJoyGuid(int joyId) const
	{
		static_cast<void>(joyId);
		// The state publishes the XInput-shaped layout, so the built-in default mapping applies
		return JoystickGuidType::Xinput;
	}

	int CtrInputManager::GetJoyButtonCount(int joyId) const
	{
		static_cast<void>(joyId);
		return CtrJoystickState::MaxNumButtons;
	}

	int CtrInputManager::GetJoyHatCount(int joyId) const
	{
		static_cast<void>(joyId);
		return CtrJoystickState::MaxNumHats;
	}

	int CtrInputManager::GetJoyAxisCount(int joyId) const
	{
		static_cast<void>(joyId);
		return CtrJoystickState::MaxNumAxes;
	}

	const JoystickState& CtrInputManager::GetJoystickState(int joyId) const
	{
		static CtrJoystickState nullJoystickState;
		if (IsJoyPresent(joyId)) {
			return _state;
		}
		return nullJoystickState;
	}

	bool CtrInputManager::JoystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs)
	{
		// The console has no rumble
		static_cast<void>(joyId);
		static_cast<void>(lowFrequency);
		static_cast<void>(highFrequency);
		static_cast<void>(durationMs);
		return false;
	}

	bool CtrInputManager::JoystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs)
	{
		static_cast<void>(joyId);
		static_cast<void>(left);
		static_cast<void>(right);
		static_cast<void>(durationMs);
		return false;
	}

	void CtrInputManager::DiscardInputUntilReleased()
	{
		_discardUntilReleased = true;
	}

	void CtrInputManager::UpdateJoystickStates()
	{
		if (!_connected) {
			return;
		}

		hidScanInput();
		std::uint32_t held = hidKeysHeld();
		if (_discardUntilReleased) {
			if (held == 0) {
				_discardUntilReleased = false;
			} else {
				held = 0;
			}
		}

		// The face buttons by position, not by their labels: the XInput names are positions (A at the bottom,
		// B on the right, X on the left, Y on top), and so are the Switch labels the menus draw for them - the
		// icon of A shows a "B", because that is what a Nintendo pad has at the bottom. The console's own A is
		// on the right, so publishing it as A put every label on the wrong button (confirmed on hardware).
		// SELECT is Back and START is Start; there is no Guide button and no stick buttons
		_state.SimulateButtonEvent(ButtonA, (held & KEY_B) != 0);
		_state.SimulateButtonEvent(ButtonB, (held & KEY_A) != 0);
		_state.SimulateButtonEvent(ButtonX, (held & KEY_Y) != 0);
		_state.SimulateButtonEvent(ButtonY, (held & KEY_X) != 0);
		_state.SimulateButtonEvent(ButtonLShoulder, (held & KEY_L) != 0);
		_state.SimulateButtonEvent(ButtonRShoulder, (held & KEY_R) != 0);
		_state.SimulateButtonEvent(ButtonBack, (held & KEY_SELECT) != 0);
		_state.SimulateButtonEvent(ButtonStart, (held & KEY_START) != 0);

		// The D-pad alone (KEY_UP and friends fold the Circle Pad in, which would move the hat with the stick)
		unsigned char hat = HatState::Centered;
		if (held & KEY_DUP) hat |= HatState::Up;
		if (held & KEY_DRIGHT) hat |= HatState::Right;
		if (held & KEY_DDOWN) hat |= HatState::Down;
		if (held & KEY_DLEFT) hat |= HatState::Left;
		_state.SimulateHatEvent(hat);

		// The Circle Pad's Y grows upwards, the XInput layout's downwards
		circlePosition circle {};
		hidCircleRead(&circle);
		_state.SimulateAxisEvent(0, NormalizeStick(circle.dx, 156.0f));
		_state.SimulateAxisEvent(1, -NormalizeStick(circle.dy, 156.0f));

		// The C-Stick and ZL/ZR only exist on a New 3DS; the HID service answers zeros for them elsewhere,
		// which reads as a centered stick and released triggers
		circlePosition cstick {};
		hidCstickRead(&cstick);
		_state.SimulateAxisEvent(2, NormalizeStick(cstick.dx, 146.0f));
		_state.SimulateAxisEvent(3, -NormalizeStick(cstick.dy, 146.0f));
		// A trigger axis rests at -1 and reads +1 when fully pressed; ZL/ZR are digital, so those are the
		// only two values they ever take
		_state.SimulateAxisEvent(4, (held & KEY_ZL) != 0 ? 1.0f : -1.0f);
		_state.SimulateAxisEvent(5, (held & KEY_ZR) != 0 ? 1.0f : -1.0f);
	}
}

#endif
