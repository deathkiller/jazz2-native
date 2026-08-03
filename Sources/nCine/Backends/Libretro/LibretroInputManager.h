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
		void connectJoystick();
		/** @brief Polls the frontend and turns the changes into engine input events */
		void processFrame();

		const MouseState& mouseState() const override {
			return mouseState_;
		}
		const KeyboardState& keyboardState() const override {
			return keyState_;
		}

		bool isJoyPresent(int joyId) const override {
			return (joyId >= 0 && joyId < NumPads);
		}
		const char* joyName(int joyId) const override {
			return "RetroPad";
		}
		const JoystickGuid joyGuid(int joyId) const override;
		int joyNumButtons(int joyId) const override {
			return NumButtons;
		}
		int joyNumHats(int joyId) const override {
			return 0;
		}
		int joyNumAxes(int joyId) const override {
			return NumAxes;
		}
		const JoystickState& joystickState(int joyId) const override {
			return joyStates_[(joyId >= 0 && joyId < NumPads) ? joyId : 0];
		}
		bool joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) override {
			return false;
		}
		bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override {
			return false;
		}
		void setCursor(Cursor cursor) override {}

	private:
		class RetroMouseState : public MouseState
		{
		public:
			bool isButtonDown(MouseButton button) const override {
				return false;
			}
		};

		class RetroKeyboardState : public KeyboardState
		{
		public:
			bool isKeyDown(Keys key) const override {
				return false;
			}
		};

		class RetroJoystickState : public JoystickState
		{
		public:
			bool buttons[NumButtons] = {};
			float axes[NumAxes] = {};

			bool isButtonPressed(int buttonId) const override {
				return (buttonId >= 0 && buttonId < NumButtons && buttons[buttonId]);
			}
			unsigned char hatState(int hatId) const override {
				return 0;
			}
			float axisValue(int axisId) const override {
				return (axisId >= 0 && axisId < NumAxes ? axes[axisId] : 0.0f);
			}
		};

		RetroMouseState mouseState_;
		RetroKeyboardState keyState_;
		RetroJoystickState joyStates_[NumPads];
	};
}

#endif
