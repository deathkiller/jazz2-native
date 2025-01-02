#pragma once

#include "IInputManager.h"

namespace nCine
{
	/// Interface for handling input events from keyboard, screen touches, mouse, accelerometer and joystick
	class IInputEventHandler
	{
	public:
		IInputEventHandler() {
			IInputManager::setHandler(this);
		}
		inline virtual ~IInputEventHandler() { }

		/// Callback function called every time a key is pressed
		inline virtual void OnKeyPressed(const KeyboardEvent& event) { }
		/// Callback function called every time a key is released
		inline virtual void OnKeyReleased(const KeyboardEvent& event) { }
		/// Callback function called every time a text input is generated
		inline virtual void OnTextInput(const TextInputEvent& event) { }

		/// Callback function called every time a touch event is made
		inline virtual void OnTouchEvent(const TouchEvent& event) { }

		/// Callback function called at fixed time with the updated reading from the accelerometer sensor
		inline virtual void OnAcceleration(const AccelerometerEvent& event) { }

		/// Callback function called every time a mouse button is pressed
		inline virtual void OnMouseDown(const MouseEvent& event) { }
		/// Callback function called every time a mouse button is released
		inline virtual void OnMouseUp(const MouseEvent& event) { }
		/// Callback function called every time the mouse is moved
		inline virtual void OnMouseMove(const MouseState& state) { }
		/// Callback function called every time a scroll input occurs (mouse wheel, touchpad gesture, etc.)
		inline virtual void OnMouseWheel(const ScrollEvent& event) { }

		/// Callback function called every time a joystick button is pressed
		inline virtual void OnJoyButtonPressed(const JoyButtonEvent& event) { }
		/// Callback function called every time a joystick button is released
		inline virtual void OnJoyButtonReleased(const JoyButtonEvent& event) { }
		/// Callback function called every time a joystick hat is moved
		inline virtual void OnJoyHatMoved(const JoyHatEvent& event) { }
		/// Callback function called every time a joystick axis is moved
		inline virtual void OnJoyAxisMoved(const JoyAxisEvent& event) { }

		/// Callback function called every time a button of a joystick with mapping is pressed
		inline virtual void OnJoyMappedButtonPressed(const JoyMappedButtonEvent& event) { }
		/// Callback function called every time a button of a joystick with mapping is released
		inline virtual void OnJoyMappedButtonReleased(const JoyMappedButtonEvent& event) { }
		/// Callback function called every time an axis of a joystick with mapping is moved
		inline virtual void OnJoyMappedAxisMoved(const JoyMappedAxisEvent& event) { }

		/// Callback function called every time a joystick is connected
		inline virtual void OnJoyConnected(const JoyConnectionEvent& event) { }
		/// Callback function called every time a joystick is disconnected
		inline virtual void OnJoyDisconnected(const JoyConnectionEvent& event) { }

		/// Callback function called when the system sends a quit event, for example when the user clicks the window close button
		/*! \returns True if the application should quit */
		inline virtual bool OnQuitRequest() { return true; }
	};
}
