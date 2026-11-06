#pragma once

#if defined(WITH_PS2)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Joystick state of one PlayStation 2 controller port, XInput-shaped

		Publishes the SDL/XInput button and axis order (like the Dc and Ogc backends), so the engine's
		built-in gamepad mapping applies without a per-console database entry.
	*/
	class Ps2JoystickState : public JoystickState
	{
	public:
		static constexpr std::int32_t MaxNumButtons = 11;
		static constexpr std::int32_t MaxNumHats = 1;
		static constexpr std::int32_t MaxNumAxes = 6;
		static constexpr float AxisEventTolerance = 0.001f;

		Ps2JoystickState();

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

		friend class Ps2InputManager;
	};

	/**
		@brief The `IInputManager` implementation for the PlayStation 2 (PS2SDK libpad)

		Gamepad-only: `UpdateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs each port's `padButtonStatus` into engine joystick events
		(the same poll-and-dispatch pattern as the Dc/Ogc/UWP backends).

		Two PS2 particulars are handled here rather than left to the caller: the pad libraries live in IRX
		modules that have to be loaded from ROM before `padInit()` will do anything, and the button word is
		<b>active low</b> - a pressed button reads 0, so the raw value is inverted before use.
	*/
	class Ps2InputManager : public IInputManager
	{
	public:
		Ps2InputManager();
		~Ps2InputManager() override;

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

		/** @brief Polls the pad ports and dispatches the resulting joystick events (once per frame) */
		static void UpdateJoystickStates();

	private:
		// Two controller ports, each of which could carry a multitap - only the base ports are polled, which
		// matches what the other console backends expose and what the splitscreen modes need
		static const std::int32_t MaxJoysticks = 2;

		struct PadInfo
		{
			bool Connected = false;
			bool PortOpen = false;
			/**
				@brief Whether this pad has already been asked to switch its sticks on

				A DualShock powers up in DIGITAL mode and stays there until something asks otherwise, so the
				request has to be made per pad and re-made for one plugged in later. Kept so it is made once
				rather than every frame: `padSetMainMode()` is an asynchronous request to the controller
				itself, not a local flag.
			*/
			bool AnalogRequested = false;
			Ps2JoystickState State;
		};

		static PadInfo _pads[MaxJoysticks];

		static void HandleConnection(std::int32_t joyId, bool connected);
		/** @brief Asks the pad in @p joyId for analogue mode, once it is ready to be asked */
		static void RequestAnalogMode(std::int32_t joyId);

		// The per-pad state objects dispatch through the protected shared _joyMapping/_inputEventHandler
		friend class Ps2JoystickState;
	};
}

#endif
