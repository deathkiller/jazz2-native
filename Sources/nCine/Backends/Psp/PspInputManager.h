#pragma once

#if defined(WITH_PSP)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Joystick state of the PlayStation Portable's built-in pad, XInput-shaped

		Publishes the SDL/XInput button and axis order (like the Dc and Ogc backends), so the engine's
		built-in gamepad mapping applies without a per-console database entry.
	*/
	class PspJoystickState : public JoystickState
	{
	public:
		static constexpr std::int32_t MaxNumButtons = 11;
		static constexpr std::int32_t MaxNumHats = 1;
		// The PSP has one analog stick and two digital triggers, but the layout stays the full XInput one
		// (six axes) so the shared mapping does not need a variant - the absent axes simply read as centered
		static constexpr std::int32_t MaxNumAxes = 6;
		static constexpr float AxisEventTolerance = 0.001f;

		PspJoystickState();

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

		void resetJoystickState(int joyId);
		void simulateButtonEvent(int buttonId, bool pressed);
		void simulateHatEvent(unsigned char state);
		void simulateAxisEvent(int axisId, float value);

	private:
		int _joyId;
		bool _buttonsState[MaxNumButtons];
		unsigned char _hatState;
		float _axesValuesState[MaxNumAxes];

		friend class PspInputManager;
	};

	/**
		@brief The `IInputManager` implementation for the PlayStation Portable (sceCtrl)

		Gamepad-only: `updateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs the pad's `SceCtrlData` into engine joystick events
		(the same poll-and-dispatch pattern as the Dc/Ogc backends). The single built-in pad is always
		present, so it connects during construction and never disconnects.
	*/
	class PspInputManager : public IInputManager
	{
	public:
		PspInputManager();
		~PspInputManager() override;

		const MouseState& mouseState() const override;
		const KeyboardState& keyboardState() const override;

		bool isJoyPresent(int joyId) const override;
		const char* joyName(int joyId) const override;
		const JoystickGuid joyGuid(int joyId) const override;
		int joyNumButtons(int joyId) const override;
		int joyNumHats(int joyId) const override;
		int joyNumAxes(int joyId) const override;
		const JoystickState& joystickState(int joyId) const override;
		bool joystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs) override;
		bool joystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs) override;

		/** @brief Polls the pad and dispatches the resulting joystick events (once per frame) */
		static void updateJoystickStates();

	private:
		static bool _connected;
		static PspJoystickState _state;

		// The state object dispatches through the protected shared _joyMapping/_inputEventHandler
		friend class PspJoystickState;
	};
}

#endif
