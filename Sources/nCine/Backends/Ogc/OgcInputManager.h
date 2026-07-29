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

		friend class OgcInputManager;
	};

	/**
		@brief The `IInputManager` implementation for the libogc consoles

		Gamepad-only (no mouse/keyboard): GameCube controller ports on both consoles, plus Wii Classic
		Controllers on the Wii. `updateJoystickStates()` is polled once per frame from
		`MainApplication::ProcessStep()` and diffs the pad state into engine joystick events (mirroring the
		UWP backend's poll-and-dispatch pattern).
	*/
	class OgcInputManager : public IInputManager
	{
	public:
		OgcInputManager();
		~OgcInputManager() override;

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

		/** @brief Polls the controller ports and dispatches the resulting joystick events (once per frame) */
		static void updateJoystickStates();

	private:
		static const std::int32_t MaxJoysticks = 4;

		struct PadInfo
		{
			bool Connected = false;
			OgcJoystickState State;
		};

		static PadInfo pads_[MaxJoysticks];

		static void handleConnection(std::int32_t joyId, bool connected);

		// The per-pad state objects dispatch through the protected shared joyMapping_/inputEventHandler_
		friend class OgcJoystickState;
	};
}

#endif
