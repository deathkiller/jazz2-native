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

		friend class DcInputManager;
	};

	/**
		@brief The `IInputManager` implementation for the Sega Dreamcast (KallistiOS maple bus)

		Gamepad-only: `UpdateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs each controller's `cont_state_t` into engine joystick
		events (the same poll-and-dispatch pattern as the Ogc/UWP backends).
	*/
	class DcInputManager : public IInputManager
	{
	public:
		DcInputManager();
		~DcInputManager() override;

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

		/** @brief Polls the maple bus and dispatches the resulting joystick events (once per frame) */
		static void UpdateJoystickStates();

	private:
		static const std::int32_t MaxJoysticks = 4;

		struct PadInfo
		{
			bool Connected = false;
			DcJoystickState State;
		};

		static PadInfo _pads[MaxJoysticks];

		static void HandleConnection(std::int32_t joyId, bool connected);

		// The per-pad state objects dispatch through the protected shared _joyMapping/_inputEventHandler
		friend class DcJoystickState;
	};
}

#endif
