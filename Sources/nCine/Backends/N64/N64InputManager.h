#pragma once

#if defined(WITH_N64)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Joystick state of one Nintendo 64 controller port, XInput-shaped

		Publishes the SDL/XInput button and axis order (like the Ogc and Dc backends), so the engine's
		built-in gamepad mapping applies without a per-console database entry.
	*/
	class N64JoystickState : public JoystickState
	{
	public:
		static constexpr std::int32_t MaxNumButtons = 11;
		static constexpr std::int32_t MaxNumHats = 1;
		static constexpr std::int32_t MaxNumAxes = 6;
		static constexpr float AxisEventTolerance = 0.001f;

		N64JoystickState();

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

		friend class N64InputManager;
	};

	/**
		@brief The `IInputManager` implementation for the Nintendo 64 (libdragon joypad)

		Gamepad-only: `updateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs each port's `joypad_inputs_t` into engine joystick
		events (the same poll-and-dispatch pattern as the Ogc/Dc backends). GameCube controllers on a
		passive adapter are supported through the same API and get their extra inputs published.
	*/
	class N64InputManager : public IInputManager
	{
	public:
		N64InputManager();
		~N64InputManager() override;

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

		/** @brief Polls the joypad ports and dispatches the resulting joystick events (once per frame) */
		static void updateJoystickStates();

	private:
		static const std::int32_t MaxJoysticks = 4;

		struct PadInfo
		{
			bool Connected = false;
			N64JoystickState State;
		};

		static PadInfo _pads[MaxJoysticks];

		static void handleConnection(std::int32_t joyId, bool connected);

		// The per-pad state objects dispatch through the protected shared _joyMapping/_inputEventHandler
		friend class N64JoystickState;
	};
}

#endif
