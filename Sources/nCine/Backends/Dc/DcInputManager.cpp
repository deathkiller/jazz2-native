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
		JoyButtonEvent joyButtonEvent_;
		JoyHatEvent joyHatEvent_;
		JoyAxisEvent joyAxisEvent_;
		JoyConnectionEvent joyConnectionEvent_;

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

	DcInputManager::PadInfo DcInputManager::pads_[DcInputManager::MaxJoysticks];

	DcJoystickState::DcJoystickState()
		: joyId_(-1), hatState_(HatState::Centered)
	{
		std::memset(buttonsState_, 0, sizeof(buttonsState_));
		std::memset(axesValuesState_, 0, sizeof(axesValuesState_));
	}

	bool DcJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && buttonsState_[buttonId]);
	}

	unsigned char DcJoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? hatState_ : static_cast<unsigned char>(HatState::Centered));
	}

	float DcJoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? axesValuesState_[axisId] : 0.0f);
	}

	void DcJoystickState::resetJoystickState(int joyId)
	{
		joyId_ = joyId;
		hatState_ = HatState::Centered;
		std::memset(buttonsState_, 0, sizeof(buttonsState_));
		std::memset(axesValuesState_, 0, sizeof(axesValuesState_));
	}

	void DcJoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::handler() != nullptr && buttonsState_[buttonId] != pressed) {
			joyButtonEvent_.joyId = joyId_;
			joyButtonEvent_.buttonId = buttonId;
			if (pressed) {
				DcInputManager::joyMapping_.OnJoyButtonPressed(joyButtonEvent_);
				IInputManager::handler()->OnJoyButtonPressed(joyButtonEvent_);
			} else {
				DcInputManager::joyMapping_.OnJoyButtonReleased(joyButtonEvent_);
				IInputManager::handler()->OnJoyButtonReleased(joyButtonEvent_);
			}
		}
		buttonsState_[buttonId] = pressed;
	}

	void DcJoystickState::simulateHatEvent(unsigned char state)
	{
		if (IInputManager::handler() != nullptr && hatState_ != state) {
			joyHatEvent_.joyId = joyId_;
			joyHatEvent_.hatId = 0;
			joyHatEvent_.hatState = state;
			DcInputManager::joyMapping_.OnJoyHatMoved(joyHatEvent_);
			IInputManager::handler()->OnJoyHatMoved(joyHatEvent_);
		}
		hatState_ = state;
	}

	void DcJoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::handler() != nullptr && std::abs(axesValuesState_[axisId] - value) > AxisEventTolerance) {
			joyAxisEvent_.joyId = joyId_;
			joyAxisEvent_.axisId = axisId;
			joyAxisEvent_.value = value;
			DcInputManager::joyMapping_.OnJoyAxisMoved(joyAxisEvent_);
			IInputManager::handler()->OnJoyAxisMoved(joyAxisEvent_);
		}
		axesValuesState_[axisId] = value;
	}

	DcInputManager::DcInputManager()
	{
		joyMapping_.Init(this);

		for (std::int32_t i = 0; i < MaxJoysticks; i++) {
			pads_[i].State.resetJoystickState(i);
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
		return (joyId >= 0 && joyId < MaxJoysticks && pads_[joyId].Connected);
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
			return pads_[joyId].State;
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
		if (pads_[joyId].Connected == connected) {
			return;
		}
		pads_[joyId].Connected = connected;
		joyConnectionEvent_.joyId = joyId;
		if (connected) {
			joyMapping_.OnJoyConnected(joyConnectionEvent_);
			if (inputEventHandler_ != nullptr) {
				inputEventHandler_->OnJoyConnected(joyConnectionEvent_);
			}
		} else {
			pads_[joyId].State.resetJoystickState(joyId);
			if (inputEventHandler_ != nullptr) {
				inputEventHandler_->OnJoyDisconnected(joyConnectionEvent_);
			}
			joyMapping_.OnJoyDisconnected(joyConnectionEvent_);
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

			DcJoystickState& state = pads_[i].State;

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
