#pragma once

#if defined(WITH_PS3)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Joystick state of one PlayStation 3 controller port, XInput-shaped

		Publishes the SDL/XInput button and axis order (like the Dc, Ogc and Ps2 backends), so the engine's
		built-in gamepad mapping applies without a per-console database entry.
	*/
	class Ps3JoystickState : public JoystickState
	{
	public:
		static constexpr std::int32_t MaxNumButtons = 11;
		static constexpr std::int32_t MaxNumHats = 1;
		static constexpr std::int32_t MaxNumAxes = 6;
		static constexpr float AxisEventTolerance = 0.001f;

		Ps3JoystickState();

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

		friend class Ps3InputManager;
	};

	/**
		@brief The `IInputManager` implementation for the PlayStation 3 (PSL1GHT libio)

		Gamepad-only: @ref updateJoystickStates() is polled once per frame from
		`MainApplication::ProcessStep()` and diffs each port's `padData` into engine joystick events (the same
		poll-and-dispatch pattern as the Dc/Ogc/Ps2 backends).

		Three PlayStation 3 particulars are handled here rather than left to the caller.

		**The pads report through one flat port array, not through physical ports.** `ioPadGetInfo2()`
		publishes a `port_status` bit per port and the pads occupy the low ports in connection order, so a
		port's index is a stable joystick ID for as long as that pad stays connected - which is what the
		engine's connection events mean.

		**The buttons are active high, unlike the PS2's.** A `padData` field is 1 when pressed, so no
		inversion happens here; the raw bits are read the obvious way round.

		**L2/R2 are real analogue triggers.** Where the PS2 backend has to synthesize the trigger axes from
		digital button bits, the DualShock 3 reports 0..255 pressures for them - but only after the port has
		been put into pressure-sensitive mode, which is what the constructor's `ioPadSetPortSetting()` does.
		If a controller does not support that (the Bluray remote, third-party pads), the pressures stay zero
		and the digital bits drive the axes instead, so both kinds of pad behave.

		The XMB's own events (quit requested, the in-game menu opening) are not pad input, but they arrive on
		the same once-per-frame poll: @ref updateJoystickStates() drains the sysutil callback queue, because
		that is the one place in the frame where dispatching them is safe.
	*/
	class Ps3InputManager : public IInputManager
	{
	public:
		Ps3InputManager();
		~Ps3InputManager() override;

		Ps3InputManager(const Ps3InputManager&) = delete;
		Ps3InputManager& operator=(const Ps3InputManager&) = delete;

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

		/** @brief Polls the pad ports, drains the XMB event queue and dispatches the resulting events (once per frame) */
		static void updateJoystickStates();

		/** @brief Returns `true` once the XMB has asked the title to exit (read by `Ps3GfxDevice::update()`) */
		static bool HasExitRequested();

	private:
		// Up to seven pads can be paired, but polling four matches what the other console backends expose
		// and covers every splitscreen mode the game has
		static const std::int32_t MaxJoysticks = 4;

		struct PadInfo
		{
			bool Connected = false;
			/** @brief Set once the port has been put into pressure-sensitive mode, so L2/R2 read as analogue */
			bool PressureMode = false;
			Ps3JoystickState State;
		};

		static PadInfo _pads[MaxJoysticks];

		static void handleConnection(std::int32_t joyId, bool connected);

		// The per-pad state objects dispatch through the protected shared _joyMapping/_inputEventHandler
		friend class Ps3JoystickState;
	};
}

#endif
