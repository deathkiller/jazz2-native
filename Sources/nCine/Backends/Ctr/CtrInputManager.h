#pragma once

#if defined(WITH_CTR)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Joystick state of the Nintendo 3DS's built-in controls, XInput-shaped

		Publishes the SDL/XInput button and axis order (like the Dc, Ogc and Psp backends), so the engine's
		built-in gamepad mapping applies without a per-console database entry. The button INDICES follow the
		console's own labels - the button labelled A on the console is the mapping's "a" - which together with
		the Switch label set the preferences default to is what puts the right glyph next to each action.
	*/
	class CtrJoystickState : public JoystickState
	{
	public:
		static constexpr std::int32_t MaxNumButtons = 11;
		static constexpr std::int32_t MaxNumHats = 1;
		// The Circle Pad is the left stick, the New 3DS's C-Stick the right one, and ZL/ZR - digital buttons,
		// New 3DS only - are reported as the trigger axes; the layout stays the full XInput one so the shared
		// mapping does not need a variant, an absent control simply reads as centered
		static constexpr std::int32_t MaxNumAxes = 6;
		static constexpr float AxisEventTolerance = 0.001f;

		CtrJoystickState();

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

		friend class CtrInputManager;
	};

	/**
		@brief The `IInputManager` implementation for the Nintendo 3DS (libctru HID)

		Gamepad-only: `UpdateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs the HID state into engine joystick events (the same
		poll-and-dispatch pattern as the Dc/Ogc/Psp backends). The single built-in pad is always present, so
		it connects during construction and never disconnects. The touch screen is not read - the game draws
		nothing on the bottom screen, so there is nothing there to touch.
	*/
	class CtrInputManager : public IInputManager
	{
	public:
		CtrInputManager();
		~CtrInputManager() override;

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

		/** @brief Polls the HID service and dispatches the resulting joystick events (once per frame) */
		static void UpdateJoystickStates();
		/**
			@brief Reports the pad as idle until every button has been released once

			Called when a library applet that read the pad itself - the software keyboard - returns: the button
			that closed it is usually still held, and would otherwise reach the game as a press on whatever sits
			under the field the keyboard was opened for.
		*/
		static void DiscardInputUntilReleased();

	private:
		static bool _connected;
		static bool _discardUntilReleased;
		static CtrJoystickState _state;

		// The state object dispatches through the protected shared _joyMapping/_inputEventHandler
		friend class CtrJoystickState;
	};
}

#endif
