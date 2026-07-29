#pragma once

#if defined(WITH_DC)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Joystick state of one Dreamcast controller port, XInput-shaped

		Publishes the SDL/XInput button and axis order (like the Ogc backend), so the engine's built-in
		gamepad mapping applies without a per-console database entry.
	*/
	class DcJoystickState : public JoystickState
	{
	public:
		static constexpr std::int32_t MaxNumButtons = 11;
		static constexpr std::int32_t MaxNumHats = 1;
		static constexpr std::int32_t MaxNumAxes = 6;
		static constexpr float AxisEventTolerance = 0.001f;

		DcJoystickState();

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

		void resetJoystickState(int joyId);
		void simulateButtonEvent(int buttonId, bool pressed);
		void simulateHatEvent(unsigned char state);
		void simulateAxisEvent(int axisId, float value);

	private:
		int joyId_;
		bool buttonsState_[MaxNumButtons];
		unsigned char hatState_;
		float axesValuesState_[MaxNumAxes];

		friend class DcInputManager;
	};

	/**
		@brief The `IInputManager` implementation for the Sega Dreamcast (KallistiOS maple bus)

		Gamepad-only: `updateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs each controller's `cont_state_t` into engine joystick
		events (the same poll-and-dispatch pattern as the Ogc/UWP backends).
	*/
	class DcInputManager : public IInputManager
	{
	public:
		DcInputManager();
		~DcInputManager() override;

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

		/** @brief Polls the maple bus and dispatches the resulting joystick events (once per frame) */
		static void updateJoystickStates();

	private:
		static const std::int32_t MaxJoysticks = 4;

		struct PadInfo
		{
			bool Connected = false;
			DcJoystickState State;
		};

		static PadInfo pads_[MaxJoysticks];

		static void handleConnection(std::int32_t joyId, bool connected);

		// The per-pad state objects dispatch through the protected shared joyMapping_/inputEventHandler_
		friend class DcJoystickState;
	};
}

#endif
