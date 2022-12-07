#pragma once

#include "../../Input/IInputManager.h"
#include "../../Threading/ThreadSync.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

namespace winrtWF = winrt::Windows::Foundation;
namespace winrtWGI = winrt::Windows::Gaming::Input;
namespace winrtWS = winrt::Windows::System;
namespace winrtWUC = winrt::Windows::UI::Core;

namespace nCine
{
	class UwpMouseState : public MouseState
	{
	public:
		inline bool isLeftButtonDown() const override
		{
			return false;
		}
		inline bool isMiddleButtonDown() const override
		{
			return false;
		}
		inline bool isRightButtonDown() const override
		{
			return false;
		}
		inline bool isFourthButtonDown() const override
		{
			return false;
		}
		inline bool isFifthButtonDown() const override
		{
			return false;
		}
	};

	class UwpKeyboardState : public KeyboardState
	{
		friend class UwpInputManager;

	public:
		UwpKeyboardState() {
			std::memset(_pressedKeys, 0, sizeof(_pressedKeys));
		}

		inline bool isKeyDown(KeySym key) const override {
			return (key >= (KeySym)0 && key < KeySym::COUNT ? _pressedKeys[(int)key] : false);
		}

	private:
		bool _pressedKeys[(int)KeySym::COUNT];
	};

	class UwpJoystickState : public JoystickState
	{
	public:
		bool isButtonPressed(int buttonId) const override {
			return false;
		}
		unsigned char hatState(int hatId) const override {
			return 0;
		}
		short int axisValue(int axisId) const override {
			return 0;
		}
		float axisNormValue(int axisId) const override {
			return 0.0f;
		}
	};

	/// The class for parsing and dispatching UWP input events
	class UwpInputManager : public IInputManager
	{
	public:
		UwpInputManager(winrtWUC::CoreWindow window);
		~UwpInputManager() override;

		/// Updates joystick state structures and simulates events
		static void updateJoystickStates();

		const MouseState& mouseState() const override {
			return mouseState_;
		}
		inline const KeyboardState& keyboardState() const override {
			return keyboardState_;
		}

		bool isJoyPresent(int joyId) const override {
			return (joyId < MaxNumJoysticks && _gamepads[joyId].Connected);
		}
		const char* joyName(int joyId) const override {
			return "Windows.Gaming.Input";
		}
		const JoystickGuid joyGuid(int joyId) const override {
			return JoystickGuidType::Xinput;
		}
		int joyNumButtons(int joyId) const override {
			return MaxNumButtons;
		}
		int joyNumHats(int joyId) const override {
			return MaxNumHats;
		}
		int joyNumAxes(int joyId) const override {
			return MaxNumAxes;
		}
		const JoystickState& joystickState(int joyId) const override {
			if (isJoyPresent(joyId)) {
				return _gamepads[joyId].State;
			} else {
				return nullJoystickState_;
			}
		}

		void setCursor(Cursor cursor) override;

	private:
		static const int MaxNumJoysticks = 8;

		static const unsigned int MaxNumButtons = 11;
		static const unsigned int MaxNumHats = 1;
		static const unsigned int MaxNumAxes = 6;

		struct UwpGamepadInfo
		{
			UwpGamepadInfo() : Gamepad(nullptr), Connected(false) { }

			winrtWGI::Gamepad Gamepad;
			UwpJoystickState State;
			bool Connected;
		};

		class JoystickEventsSimulator
		{
		public:
			JoystickEventsSimulator();
			void resetJoystickState(int joyId);
			void simulateButtonsEvents(int joyId, winrtWGI::GamepadButtons buttons);
			void simulateHatsEvents(int joyId, winrtWGI::GamepadButtons buttons);
			void simulateAxisEvent(int joyId, int axisId, float value);

		private:
			/// Minimum difference between two axis readings in order to trigger an event
			static const float AxisEventTolerance;

			/// Old state used to simulate joystick buttons events
			bool buttonsState_[MaxNumJoysticks][MaxNumButtons];
			/// Old state used to simulate joystick hats events
			unsigned char hatsState_[MaxNumJoysticks][MaxNumHats];
			/// Old state used to simulate joystick axes events
			float axesValuesState_[MaxNumJoysticks][MaxNumAxes];
		};

		static UwpMouseState mouseState_;
		static UwpKeyboardState keyboardState_;
		static KeyboardEvent keyboardEvent_;
		static UwpJoystickState nullJoystickState_;
		static JoyButtonEvent joyButtonEvent_;
		static JoyHatEvent joyHatEvent_;
		static JoyAxisEvent joyAxisEvent_;
		static JoyConnectionEvent joyConnectionEvent_;
		static JoystickEventsSimulator joyEventsSimulator_;

		static UwpGamepadInfo _gamepads[MaxNumJoysticks];
		static Mutex _gamepadsSync;

		static KeySym keySymValueToEnum(winrtWS::VirtualKey virtualKey);

		void OnKey(const winrtWUC::CoreWindow& sender, const winrtWUC::KeyEventArgs& args);
		void OnAcceleratorKeyActivated(const winrtWUC::CoreDispatcher& sender, const winrtWUC::AcceleratorKeyEventArgs& args);
	};
}
