#pragma once

#include "../../Input/IInputManager.h"
#include "../../Threading/ThreadSync.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.Core.h>

namespace winrtWF = winrt::Windows::Foundation;
namespace winrtWGI = winrt::Windows::Gaming::Input;
namespace winrtWS = winrt::Windows::System;
namespace winrtWUC = winrt::Windows::UI::Core;
namespace winrtWUTC = winrt::Windows::UI::Text::Core;

namespace nCine::Backends
{
	/// Information about UWP mouse state
	class UwpMouseState : public MouseState
	{
	public:
		bool isButtonDown(MouseButton button) const override
		{
			// TODO
			return false;
		}
	};

	/// Information about UWP keyboard state
	class UwpKeyboardState : public KeyboardState
	{
		friend class UwpInputManager;

	public:
		UwpKeyboardState() {
			std::memset(_pressedKeys, 0, sizeof(_pressedKeys));
		}

		inline bool isKeyDown(Keys key) const override {
			return (key >= (Keys)0 && key < Keys::Count ? _pressedKeys[(int)key] : false);
		}

	private:
		bool _pressedKeys[(int)Keys::Count];
	};

	/// Information about UWP joystick state
	class UwpJoystickState : public JoystickState
	{
	public:
		static constexpr unsigned int MaxNumButtons = 11;
		static constexpr unsigned int MaxNumHats = 1;
		static constexpr unsigned int MaxNumAxes = 6;

		UwpJoystickState();

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

		void resetJoystickState(int joyId);
		void simulateButtonsEvents(winrtWGI::GamepadButtons buttons);
		void simulateHatsEvents(winrtWGI::GamepadButtons buttons);
		void simulateAxisEvent(int axisId, float value);

	private:
		/// Minimum difference between two axis readings in order to trigger an event
		static constexpr float AxisEventTolerance = 0.001f;

		static JoyButtonEvent joyButtonEvent_;
		static JoyHatEvent joyHatEvent_;
		static JoyAxisEvent joyAxisEvent_;

		int joyId_;
		/// Old state used to simulate joystick buttons events
		bool buttonsState_[MaxNumButtons];
		/// Old state used to simulate joystick hats events
		unsigned char hatsState_[MaxNumHats];
		/// Old state used to simulate joystick axes events
		float axesValuesState_[MaxNumAxes];
	};

	/// Class for dispatching UWP input events
	class UwpInputManager : public IInputManager
	{
		friend class UwpJoystickState;

	public:
		UwpInputManager(winrtWUC::CoreWindow window);
		~UwpInputManager() override;

		/// Updates joystick state structures and simulates events
		static void updateJoystickStates();

		const MouseState& mouseState() const override { return mouseState_; }
		inline const KeyboardState& keyboardState() const override { return keyboardState_; }

		bool isJoyPresent(int joyId) const override
		{
			ASSERT(joyId >= 0);
			return (joyId < MaxNumJoysticks && _gamepads[joyId].Connected);
		}
		
		const char* joyName(int joyId) const override { return "Windows.Gaming.Input"; }
		const JoystickGuid joyGuid(int joyId) const override { return JoystickGuidType::Xinput; }
		int joyNumButtons(int joyId) const override { return UwpJoystickState::MaxNumButtons; }
		int joyNumHats(int joyId) const override { return UwpJoystickState::MaxNumHats; }
		int joyNumAxes(int joyId) const override { return UwpJoystickState::MaxNumAxes; }
		
		const JoystickState& joystickState(int joyId) const override
		{
			if (isJoyPresent(joyId)) {
				return _gamepads[joyId].State;
			} else {
				return nullJoystickState_;
			}
		}
		
		bool joystickRumble(int joyId, float lowFrequency, float highFrequency, uint32_t durationMs) override;
		bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override;

		void setCursor(Cursor cursor) override;

	private:
		static const int MaxNumJoysticks = 8;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct UwpGamepadInfo
		{
			UwpGamepadInfo() : Gamepad(nullptr), Connected(false),
				RumbleLowFrequency(0.0f), RumbleHighFrequency(0.0f), RumbleLeftTrigger(0.0f), RumbleRightTrigger(0.0f),
				RumbleExpiration(0), RumbleTriggersExpiration(0) { }

			winrtWGI::Gamepad Gamepad;
			UwpJoystickState State;
			bool Connected;
			float RumbleLowFrequency;
			float RumbleHighFrequency;
			float RumbleLeftTrigger;
			float RumbleRightTrigger;
			uint64_t RumbleExpiration;
			uint64_t RumbleTriggersExpiration;
		};
#endif

		static UwpMouseState mouseState_;
		static UwpKeyboardState keyboardState_;
		static KeyboardEvent keyboardEvent_;
		static TextInputEvent textInputEvent_;
		static UwpJoystickState nullJoystickState_;
		static JoyConnectionEvent joyConnectionEvent_;

		//static winrtWUTC::CoreTextEditContext _editContext;
		static UwpGamepadInfo _gamepads[MaxNumJoysticks];
		static ReadWriteLock _gamepadsSync;

		static Keys keySymValueToEnum(winrtWS::VirtualKey virtualKey);

		void OnKey(const winrtWUC::CoreWindow& sender, const winrtWUC::KeyEventArgs& args);
		void OnCharacterReceived(const winrtWUC::CoreWindow& sender, const winrtWUC::CharacterReceivedEventArgs& args);
		void OnAcceleratorKeyActivated(const winrtWUC::CoreDispatcher& sender, const winrtWUC::AcceleratorKeyEventArgs& args);
		void OnGamepadAdded(const winrtWF::IInspectable& sender, const winrtWGI::Gamepad& gamepad);
		void OnGamepadRemoved(const winrtWF::IInspectable& sender, const winrtWGI::Gamepad& gamepad);
	};
}
