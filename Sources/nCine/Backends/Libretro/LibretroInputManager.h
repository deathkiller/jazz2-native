#pragma once

#if defined(WITH_LIBRETRO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Input/IInputManager.h"

namespace nCine::Backends
{
	/**
		@brief Input manager exposing the RetroPad of the frontend as a standard mapped gamepad

		The frontend provides neither a mouse nor a keyboard, every port is reported as a connected
		gamepad instead, so that local splitscreen multiplayer works out of the box.
	*/
	class LibretroInputManager : public IInputManager
	{
	public:
		/** @brief Number of gamepads exposed to the engine */
		static constexpr std::int32_t NumPads = 4;
		/** @brief Number of buttons of a RetroPad */
		static constexpr std::int32_t NumButtons = 16;
		/** @brief Number of axes of a RetroPad */
		static constexpr std::int32_t NumAxes = 4;

		LibretroInputManager();

		/** @brief Reports every port as a connected gamepad */
		void ConnectJoystick();
		/** @brief Polls the frontend and turns the changes into engine input events */
		void ProcessFrame();

		const MouseState& GetMouseState() const override {
			return _mouseState;
		}
		const KeyboardState& GetKeyboardState() const override {
			return _keyState;
		}

		bool IsJoyPresent(int joyId) const override {
			return (joyId >= 0 && joyId < NumPads);
		}
		const char* GetJoyName(int joyId) const override {
			return "RetroPad";
		}
		const JoystickGuid GetJoyGuid(int joyId) const override;
		int GetJoyButtonCount(int joyId) const override {
			return NumButtons;
		}
		int GetJoyHatCount(int joyId) const override {
			return 0;
		}
		int GetJoyAxisCount(int joyId) const override {
			return NumAxes;
		}
		const JoystickState& GetJoystickState(int joyId) const override {
			return _joyStates[(joyId >= 0 && joyId < NumPads) ? joyId : 0];
		}
		bool JoystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) override {
			return false;
		}
		bool JoystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override {
			return false;
		}
		void SetCursor(Cursor cursor) override {}

	private:
		class RetroMouseState : public MouseState
		{
		public:
			bool IsButtonDown(MouseButton button) const override {
				return false;
			}
		};

		class RetroKeyboardState : public KeyboardState
		{
		public:
			bool IsKeyDown(Keys key) const override {
				return false;
			}
		};

		class RetroJoystickState : public JoystickState
		{
		public:
			bool buttons[NumButtons] = {};
			float axes[NumAxes] = {};

			bool IsButtonPressed(int buttonId) const override {
				return (buttonId >= 0 && buttonId < NumButtons && buttons[buttonId]);
			}
			unsigned char GetHatState(int hatId) const override {
				return 0;
			}
			float GetAxisValue(int axisId) const override {
				return (axisId >= 0 && axisId < NumAxes ? axes[axisId] : 0.0f);
			}
		};

		RetroMouseState _mouseState;
		RetroKeyboardState _keyState;
		RetroJoystickState _joyStates[NumPads];
	};
}

#endif
