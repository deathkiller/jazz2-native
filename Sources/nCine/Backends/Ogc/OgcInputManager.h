#pragma once

#if defined(WITH_OGC)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Joystick state of one libogc controller port, XInput-shaped

		The state publishes the SDL/XInput button and axis order (A, B, X, Y, Back, Guide, Start, LStick,
		RStick, LShoulder, RShoulder; axes LX, LY, RX, RY, LT, RT; D-pad as hat 0) and the manager reports
		the XInput GUID, so the engine's built-in gamepad mapping applies without a per-console database
		entry. GameCube pads and Wii Classic Controllers are translated onto that layout when polled.
	*/
	class OgcJoystickState : public JoystickState
	{
	public:
		static constexpr std::int32_t MaxNumButtons = 11;
		static constexpr std::int32_t MaxNumHats = 1;
		static constexpr std::int32_t MaxNumAxes = 6;
		static constexpr float AxisEventTolerance = 0.001f;

		OgcJoystickState();

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

		friend class OgcInputManager;
	};

	/**
		@brief The `IInputManager` implementation for the libogc consoles

		Gamepad-only (no mouse/keyboard): GameCube controller ports on both consoles, plus Wii Classic
		Controllers on the Wii. `UpdateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs the pad state into engine joystick events (mirroring the
		UWP backend's poll-and-dispatch pattern).
	*/
	class OgcInputManager : public IInputManager
	{
	public:
		OgcInputManager();
		~OgcInputManager() override;

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

		/** @brief Polls the controller ports and dispatches the resulting joystick events (once per frame) */
		static void UpdateJoystickStates();

	private:
		static const std::int32_t MaxJoysticks = 4;

		struct PadInfo
		{
			bool Connected = false;
			OgcJoystickState State;
		};

		static PadInfo _pads[MaxJoysticks];

		static void HandleConnection(std::int32_t joyId, bool connected);

		// The per-pad state objects dispatch through the protected shared _joyMapping/_inputEventHandler
		friend class OgcJoystickState;
	};
}

#endif
