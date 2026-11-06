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

		bool IsButtonPressed(int buttonId) const override;
		unsigned char GetHatState(int hatId) const override;
		float GetAxisValue(int axisId) const override;

		void ResetJoystickState(int joyId);
		void SimulateButtonEvent(int buttonId, bool pressed);
		void SimulateHatEvent(unsigned char state);
		void SimulateAxisEvent(int axisId, float value);

	private:
		int _joyId;
		bool _buttonsState[MaxNumButtons];
		unsigned char _hatState;
		float _axesValuesState[MaxNumAxes];

		friend class N64InputManager;
	};

	/**
		@brief The `IInputManager` implementation for the Nintendo 64 (libdragon joypad)

		Gamepad-only: `UpdateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs each port's `joypad_inputs_t` into engine joystick
		events (the same poll-and-dispatch pattern as the Ogc/Dc backends). GameCube controllers on a
		passive adapter are supported through the same API and get their extra inputs published.
	*/
	class N64InputManager : public IInputManager
	{
	public:
		N64InputManager();
		~N64InputManager() override;

		const MouseState& GetMouseState() const override;
		const KeyboardState& GetKeyboardState() const override;

		bool IsJoyPresent(int joyId) const override;
		const char* GetJoyName(int joyId) const override;
		const JoystickGuid GetJoyGuid(int joyId) const override;
		int GetJoyButtonCount(int joyId) const override;
		int GetJoyHatCount(int joyId) const override;
		int GetJoyAxisCount(int joyId) const override;
		const JoystickState& GetJoystickState(int joyId) const override;
		bool JoystickRumble(int joyId, float lowFrequency, float highFrequency, std::uint32_t durationMs) override;
		bool JoystickRumbleTriggers(int joyId, float left, float right, std::uint32_t durationMs) override;

		/** @brief Polls the joypad ports and dispatches the resulting joystick events (once per frame) */
		static void UpdateJoystickStates();

	private:
		static const std::int32_t MaxJoysticks = 4;

		struct PadInfo
		{
			bool Connected = false;
			N64JoystickState State;
			// The Rumble Pak has no firmware timer or strength, so the duration and strength the engine asks for
			// are kept here and applied from the per-frame poll as a duty cycle - see joystickRumble()
			bool RumbleActive = false;
			float RumbleStrength = 0.0f;
			float RumbleAccumulator = 0.5f;
			std::uint64_t RumbleUntilMs = 0;
		};

		static PadInfo _pads[MaxJoysticks];

		static void HandleConnection(std::int32_t joyId, bool connected);

		// The per-pad state objects dispatch through the protected shared _joyMapping/_inputEventHandler
		friend class N64JoystickState;
	};
}

#endif
