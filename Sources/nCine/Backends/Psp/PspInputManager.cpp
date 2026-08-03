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
		JoyButtonEvent joyButtonEvent_;
		JoyHatEvent joyHatEvent_;
		JoyAxisEvent joyAxisEvent_;
		JoyConnectionEvent joyConnectionEvent_;

		inline float NormalizeStick(unsigned char value)
		{
			// SceCtrlData Lx/Ly are 0..255 with 128 nominally centered
			return (float(value) - 128.0f) / 127.0f;
		}
	}

	bool PspInputManager::connected_ = false;
	PspJoystickState PspInputManager::state_;

	PspJoystickState::PspJoystickState()
		: joyId_(-1), hatState_(HatState::Centered)
	{
		std::memset(buttonsState_, 0, sizeof(buttonsState_));
		std::memset(axesValuesState_, 0, sizeof(axesValuesState_));
	}

	bool PspJoystickState::isButtonPressed(int buttonId) const
	{
		return (buttonId >= 0 && buttonId < MaxNumButtons && buttonsState_[buttonId]);
	}

	unsigned char PspJoystickState::hatState(int hatId) const
	{
		return (hatId == 0 ? hatState_ : static_cast<unsigned char>(HatState::Centered));
	}

	float PspJoystickState::axisValue(int axisId) const
	{
		return (axisId >= 0 && axisId < MaxNumAxes ? axesValuesState_[axisId] : 0.0f);
	}

	void PspJoystickState::resetJoystickState(int joyId)
	{
		joyId_ = joyId;
		hatState_ = HatState::Centered;
		std::memset(buttonsState_, 0, sizeof(buttonsState_));
		std::memset(axesValuesState_, 0, sizeof(axesValuesState_));
	}

	void PspJoystickState::simulateButtonEvent(int buttonId, bool pressed)
	{
		if (buttonId < 0 || buttonId >= MaxNumButtons) {
			return;
		}
		if (IInputManager::handler() != nullptr && buttonsState_[buttonId] != pressed) {
			joyButtonEvent_.joyId = joyId_;
			joyButtonEvent_.buttonId = buttonId;
			if (pressed) {
				PspInputManager::joyMapping_.OnJoyButtonPressed(joyButtonEvent_);
				IInputManager::handler()->OnJoyButtonPressed(joyButtonEvent_);
			} else {
				PspInputManager::joyMapping_.OnJoyButtonReleased(joyButtonEvent_);
				IInputManager::handler()->OnJoyButtonReleased(joyButtonEvent_);
			}
		}
		buttonsState_[buttonId] = pressed;
	}

	void PspJoystickState::simulateHatEvent(unsigned char state)
	{
		if (IInputManager::handler() != nullptr && hatState_ != state) {
			joyHatEvent_.joyId = joyId_;
			joyHatEvent_.hatId = 0;
			joyHatEvent_.hatState = state;
			PspInputManager::joyMapping_.OnJoyHatMoved(joyHatEvent_);
			IInputManager::handler()->OnJoyHatMoved(joyHatEvent_);
		}
		hatState_ = state;
	}

	void PspJoystickState::simulateAxisEvent(int axisId, float value)
	{
		if (axisId < 0 || axisId >= MaxNumAxes) {
			return;
		}
		if (IInputManager::handler() != nullptr && std::abs(axesValuesState_[axisId] - value) > AxisEventTolerance) {
			joyAxisEvent_.joyId = joyId_;
			joyAxisEvent_.axisId = axisId;
			joyAxisEvent_.value = value;
			PspInputManager::joyMapping_.OnJoyAxisMoved(joyAxisEvent_);
			IInputManager::handler()->OnJoyAxisMoved(joyAxisEvent_);
		}
		axesValuesState_[axisId] = value;
	}

	PspInputManager::PspInputManager()
	{
		joyMapping_.Init(this);

		state_.resetJoystickState(0);

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
		return (joyId == 0 && connected_);
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
			return state_;
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

		if (!connected_) {
			connected_ = true;
			joyConnectionEvent_.joyId = 0;
			joyMapping_.OnJoyConnected(joyConnectionEvent_);
			if (inputEventHandler_ != nullptr) {
				inputEventHandler_->OnJoyConnected(joyConnectionEvent_);
			}
		}

		// The PSP face buttons sit where a DualShock's do, so they map onto the XInput names the same way
		// the engine's PlayStation gamepad handling already assumes: Cross = A, Circle = B, Square = X,
		// Triangle = Y
		state_.simulateButtonEvent(ButtonA, (pad.Buttons & PSP_CTRL_CROSS) != 0);
		state_.simulateButtonEvent(ButtonB, (pad.Buttons & PSP_CTRL_CIRCLE) != 0);
		state_.simulateButtonEvent(ButtonX, (pad.Buttons & PSP_CTRL_SQUARE) != 0);
		state_.simulateButtonEvent(ButtonY, (pad.Buttons & PSP_CTRL_TRIANGLE) != 0);
		state_.simulateButtonEvent(ButtonLShoulder, (pad.Buttons & PSP_CTRL_LTRIGGER) != 0);
		state_.simulateButtonEvent(ButtonRShoulder, (pad.Buttons & PSP_CTRL_RTRIGGER) != 0);
		state_.simulateButtonEvent(ButtonBack, (pad.Buttons & PSP_CTRL_SELECT) != 0);
		state_.simulateButtonEvent(ButtonStart, (pad.Buttons & PSP_CTRL_START) != 0);
		// Nothing on the console clicks a stick, and the Home button belongs to the firmware's exit dialog
		state_.simulateButtonEvent(ButtonLStick, false);
		state_.simulateButtonEvent(ButtonRStick, false);
		state_.simulateButtonEvent(ButtonGuide, false);

		unsigned char hat = HatState::Centered;
		if (pad.Buttons & PSP_CTRL_UP) hat |= HatState::Up;
		if (pad.Buttons & PSP_CTRL_RIGHT) hat |= HatState::Right;
		if (pad.Buttons & PSP_CTRL_DOWN) hat |= HatState::Down;
		if (pad.Buttons & PSP_CTRL_LEFT) hat |= HatState::Left;
		state_.simulateHatEvent(hat);

		state_.simulateAxisEvent(0, NormalizeStick(pad.Lx));
		state_.simulateAxisEvent(1, NormalizeStick(pad.Ly));
		// No right stick and no analog triggers: the shoulder buttons are digital, so their axes stay at the
		// released end of the [-1, 1] range the trigger mapping expects
		state_.simulateAxisEvent(2, 0.0f);
		state_.simulateAxisEvent(3, 0.0f);
		state_.simulateAxisEvent(4, -1.0f);
		state_.simulateAxisEvent(5, -1.0f);
	}
}

#endif
