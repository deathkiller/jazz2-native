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

		bool isButtonDown(MouseButton button) const override;

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

		bool isKeyDown(Keys key) const override
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

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

		void resetJoystickState(int joyId);
		void simulateButtonEvent(int buttonId, bool pressed);
		void simulateHatEvent(unsigned char state);

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

		void setCursor(Cursor cursor) override;

		/** @brief Drains the window's IDCMP queue and polls the joyports (once per frame) */
		static void updateJoystickStates();

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

		static void handleConnection(std::int32_t joyId, bool connected);
		static void processIdcmpMessages();
		static void pollJoyPorts();

		friend class AmigaJoystickState;
	};
}

#endif
