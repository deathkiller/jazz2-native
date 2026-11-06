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
	/** @brief Information about UWP mouse state */
	class UwpMouseState : public MouseState
	{
	public:
		bool IsButtonDown(MouseButton button) const override
		{
			// TODO
			return false;
		}
	};

	/** @brief Information about UWP keyboard state */
	class UwpKeyboardState : public KeyboardState
	{
		friend class UwpInputManager;

	public:
		UwpKeyboardState() {
			std::memset(_pressedKeys, 0, sizeof(_pressedKeys));
		}

		inline bool IsKeyDown(Keys key) const override {
			return (key >= (Keys)0 && key < Keys::Count ? _pressedKeys[(int)key] : false);
		}

	private:
		bool _pressedKeys[(int)Keys::Count];
	};

	/** @brief Information about UWP joystick state */
	class UwpJoystickState : public JoystickState
	{
	public:
		static constexpr unsigned int MaxNumButtons = 11;
		static constexpr unsigned int MaxNumHats = 1;
		static constexpr unsigned int MaxNumAxes = 6;

		UwpJoystickState();

		bool IsButtonPressed(int buttonId) const override;
		unsigned char GetHatState(int hatId) const override;
		float GetAxisValue(int axisId) const override;

		void ResetJoystickState(int joyId);
		void SimulateButtonsEvents(winrtWGI::GamepadButtons buttons);
		void SimulateHatsEvents(winrtWGI::GamepadButtons buttons);
		void SimulateAxisEvent(int axisId, float value);

	private:
		/** @brief Minimum difference between two axis readings in order to trigger an event */
		static constexpr float AxisEventTolerance = 0.001f;

		static JoyButtonEvent _joyButtonEvent;
		static JoyHatEvent _joyHatEvent;
		static JoyAxisEvent _joyAxisEvent;

		int _joyId;
		/** @brief Old state used to simulate joystick buttons events */
		bool _buttonsState[MaxNumButtons];
		/** @brief Old state used to simulate joystick hats events */
		unsigned char _hatsState[MaxNumHats];
		/** @brief Old state used to simulate joystick axes events */
		float _axesValuesState[MaxNumAxes];
	};

	/**
		@brief Class for dispatching UWP input events
		
		Handles keyboard and mouse events through the `CoreWindow` and gamepads through
		the `Windows.Gaming.Input` API.
	*/
	class UwpInputManager : public IInputManager
	{
		friend class UwpJoystickState;

	public:
		UwpInputManager(winrtWUC::CoreWindow window);
		~UwpInputManager() override;

		/** @brief Updates joystick state structures and simulates events */
		static void UpdateJoystickStates();

		const MouseState& GetMouseState() const override { return _mouseState; }
		inline const KeyboardState& GetKeyboardState() const override { return _keyboardState; }

		bool IsJoyPresent(int joyId) const override
		{
			DEATH_ASSERT(joyId >= 0);
			return (joyId < MaxNumJoysticks && _gamepads[joyId].Connected);
		}
		
		const char* GetJoyName(int joyId) const override { return "Windows.Gaming.Input"; }
		const JoystickGuid GetJoyGuid(int joyId) const override { return JoystickGuidType::Xinput; }
		int GetJoyButtonCount(int joyId) const override { return UwpJoystickState::MaxNumButtons; }
		int GetJoyHatCount(int joyId) const override { return UwpJoystickState::MaxNumHats; }
		int GetJoyAxisCount(int joyId) const override { return UwpJoystickState::MaxNumAxes; }
		
		const JoystickState& GetJoystickState(int joyId) const override
		{
			if (IsJoyPresent(joyId)) {
				return _gamepads[joyId].State;
			} else {
				return _nullJoystickState;
			}
		}
		
		bool JoystickRumble(int joyId, float lowFrequency, float highFrequency, uint32_t durationMs) override;
		bool JoystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override;

		void SetCursor(Cursor cursor) override;

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
			std::uint64_t RumbleExpiration;
			std::uint64_t RumbleTriggersExpiration;
		};
#endif

		static UwpMouseState _mouseState;
		static UwpKeyboardState _keyboardState;
		static KeyboardEvent _keyboardEvent;
		static TextInputEvent _textInputEvent;
		static UwpJoystickState _nullJoystickState;
		static JoyConnectionEvent _joyConnectionEvent;

		//static winrtWUTC::CoreTextEditContext _editContext;
		static UwpGamepadInfo _gamepads[MaxNumJoysticks];
		static ReadWriteLock _gamepadsSync;

		static Keys KeySymValueToEnum(winrtWS::VirtualKey virtualKey);

		void OnKey(const winrtWUC::CoreWindow& sender, const winrtWUC::KeyEventArgs& args);
		void OnCharacterReceived(const winrtWUC::CoreWindow& sender, const winrtWUC::CharacterReceivedEventArgs& args);
		void OnAcceleratorKeyActivated(const winrtWUC::CoreDispatcher& sender, const winrtWUC::AcceleratorKeyEventArgs& args);
		void OnGamepadAdded(const winrtWF::IInspectable& sender, const winrtWGI::Gamepad& gamepad);
		void OnGamepadRemoved(const winrtWF::IInspectable& sender, const winrtWGI::Gamepad& gamepad);
	};
}
