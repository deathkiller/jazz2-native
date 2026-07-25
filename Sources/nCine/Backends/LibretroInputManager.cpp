#include "LibretroInputManager.h"

#if defined(WITH_LIBRETRO)

#include "LibretroApplication.h"
#include "../Input/IInputEventHandler.h"
#include "../Input/JoyMapping.h"

namespace nCine::Backends
{
	namespace
	{
		// Raw button ids are the RETRO_DEVICE_ID_JOYPAD_* values, raw axes are left/right stick X/Y;
		// this mapping translates them to the engine's unified gamepad layout
		constexpr char RetroPadGuid[] = "030000004c6962726574726f00000000";
		constexpr char RetroPadMapping[] =
			"030000004c6962726574726f00000000,RetroPad,"
			"a:b0,b:b8,x:b1,y:b9,back:b2,start:b3,"
			"dpup:b4,dpdown:b5,dpleft:b6,dpright:b7,"
			"leftshoulder:b10,rightshoulder:b11,lefttrigger:b12,righttrigger:b13,"
			"leftstick:b14,rightstick:b15,"
			"leftx:a0,lefty:a1,rightx:a2,righty:a3,";
	}

	LibretroInputManager::LibretroInputManager()
	{
		joyMapping_.Init(this);
		joyMapping_.AddMappingsFromString(RetroPadMapping);
	}

	const JoystickGuid LibretroInputManager::joyGuid(int joyId) const
	{
		return JoystickGuid(StringView(RetroPadGuid));
	}

	void LibretroInputManager::connectJoystick()
	{
		for (std::int32_t joyId = 0; joyId < NumPads; joyId++) {
			JoyConnectionEvent event;
			event.joyId = joyId;
			joyMapping_.OnJoyConnected(event);
			if (inputEventHandler_ != nullptr) {
				inputEventHandler_->OnJoyConnected(event);
			}
		}
	}

	void LibretroInputManager::processFrame()
	{
		LibretroApplication::InputPollCallback();

		for (std::int32_t port = 0; port < NumPads; port++) {
			RetroJoystickState& joyState = joyStates_[port];

			for (std::int32_t id = 0; id < NumButtons; id++) {
				bool pressed = LibretroApplication::InputStateCallback((unsigned)port, RETRO_DEVICE_JOYPAD, 0, (unsigned)id) != 0;
				if (pressed != joyState.buttons[id]) {
					joyState.buttons[id] = pressed;
					JoyButtonEvent event;
					event.joyId = port;
					event.buttonId = id;
					if (pressed) {
						joyMapping_.OnJoyButtonPressed(event);
						if (inputEventHandler_ != nullptr) inputEventHandler_->OnJoyButtonPressed(event);
					} else {
						joyMapping_.OnJoyButtonReleased(event);
						if (inputEventHandler_ != nullptr) inputEventHandler_->OnJoyButtonReleased(event);
					}
				}
			}

			static const unsigned AnalogIndex[NumAxes] = {
				RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_INDEX_ANALOG_LEFT,
				RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_INDEX_ANALOG_RIGHT
			};
			static const unsigned AnalogId[NumAxes] = {
				RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_ID_ANALOG_Y,
				RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_ID_ANALOG_Y
			};
			for (std::int32_t id = 0; id < NumAxes; id++) {
				std::int16_t raw = LibretroApplication::InputStateCallback((unsigned)port, RETRO_DEVICE_ANALOG, AnalogIndex[id], AnalogId[id]);
				float value = (raw < 0 ? raw / 32768.0f : raw / 32767.0f);
				if (value != joyState.axes[id]) {
					joyState.axes[id] = value;
					JoyAxisEvent event;
					event.joyId = port;
					event.axisId = id;
					event.value = value;
					joyMapping_.OnJoyAxisMoved(event);
					if (inputEventHandler_ != nullptr) inputEventHandler_->OnJoyAxisMoved(event);
				}
			}
		}
	}
}

#endif
