#pragma once

#if defined(WITH_AMIGA)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Mouse state fed from the game window's IDCMP messages
	*/
	class AmigaMouseState : public MouseState
	{
	public:
		AmigaMouseState() : _buttons(0) { }

		bool IsButtonDown(MouseButton button) const override;

	private:
		std::uint32_t _buttons;

		friend class AmigaInputManager;
	};

	/**
		@brief Keyboard state fed from RAWKEY IDCMP messages
	*/
	class AmigaKeyboardState : public KeyboardState
	{
	public:
		AmigaKeyboardState()
		{
			for (bool& key : _keys) {
				key = false;
			}
		}

		bool IsKeyDown(Keys key) const override
		{
			return (key != Keys::Unknown && _keys[std::int32_t(key)]);
		}

	private:
		bool _keys[std::int32_t(Keys::Count)];

		friend class AmigaInputManager;
	};

	/**
		@brief Joystick state of one Amiga joyport, XInput-shaped

		Publishes the SDL/XInput button order like the other console backends, so the engine's built-in
		gamepad mapping applies. A CD32 pad fills seven buttons; a plain one-button joystick only the first.
	*/
	class AmigaJoystickState : public JoystickState
	{
	public:
		static constexpr std::int32_t MaxNumButtons = 11;
		static constexpr std::int32_t MaxNumHats = 1;
		static constexpr std::int32_t MaxNumAxes = 0;

		AmigaJoystickState();

		bool IsButtonPressed(int buttonId) const override;
		unsigned char GetHatState(int hatId) const override;
		float GetAxisValue(int axisId) const override;

		void ResetJoystickState(int joyId);
		void SimulateButtonEvent(int buttonId, bool pressed);
		void SimulateHatEvent(unsigned char state);

	private:
		int _joyId;
		bool _buttonsState[MaxNumButtons];
		unsigned char _hatState;

		friend class AmigaInputManager;
	};

	/**
		@brief The `IInputManager` implementation for classic Amiga

		The event pump has two halves, both drained once per frame from
		`MainApplication::ProcessStep()`: the game window's IDCMP queue (RAWKEY keyboard with
		keymap-driven text input, mouse buttons and movement) and `lowlevel.library`'s joyports
		(CD32 game pads and plain joysticks on either port; a port answering as a mouse is left to
		Intuition).
	*/
	class AmigaInputManager : public IInputManager
	{
	public:
		AmigaInputManager();
		~AmigaInputManager() override;

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

		void SetCursor(Cursor cursor) override;

		/** @brief Drains the window's IDCMP queue and polls the joyports (once per frame) */
		static void UpdateJoystickStates();

	private:
		static const std::int32_t MaxJoysticks = 2;

		struct PadInfo
		{
			bool Connected = false;
			bool IsGameController = false;
			AmigaJoystickState State;
		};

		static PadInfo _pads[MaxJoysticks];
		static AmigaMouseState _mouseState;
		static AmigaKeyboardState _keyboardState;

		static void HandleConnection(std::int32_t joyId, bool connected);
		static void ProcessIdcmpMessages();
		static void PollJoyPorts();

		friend class AmigaJoystickState;
	};
}

#endif
