#pragma once

#include "IInputManager.h"

namespace nCine
{
	/**
		@brief Interface for handling input events from keyboard, touch screen, mouse, accelerometer and joysticks
		
		An instance registers itself as the active handler on construction. Override the callbacks of interest;
		the default implementations do nothing.
	*/
	class IInputEventHandler
	{
	public:
		IInputEventHandler() {
			IInputManager::setHandler(this);
		}
		inline virtual ~IInputEventHandler() { }

		/** @brief Called when a key is pressed */
		inline virtual void OnKeyPressed(const KeyboardEvent& event) { }
		/** @brief Called when a key is released */
		inline virtual void OnKeyReleased(const KeyboardEvent& event) { }
		/** @brief Called when text input is generated */
		inline virtual void OnTextInput(const TextInputEvent& event) { }

		/** @brief Called when a touch event occurs */
		inline virtual void OnTouchEvent(const TouchEvent& event) { }

		/** @brief Called at a fixed rate with the latest accelerometer reading */
		inline virtual void OnAcceleration(const AccelerometerEvent& event) { }

		/** @brief Called when a mouse button is pressed */
		inline virtual void OnMouseDown(const MouseEvent& event) { }
		/** @brief Called when a mouse button is released */
		inline virtual void OnMouseUp(const MouseEvent& event) { }
		/** @brief Called when the mouse is moved */
		inline virtual void OnMouseMove(const MouseState& state) { }
		/** @brief Called when a scroll input occurs (mouse wheel, touchpad gesture, etc.) */
		inline virtual void OnMouseWheel(const ScrollEvent& event) { }

		/** @brief Called when a joystick button is pressed */
		inline virtual void OnJoyButtonPressed(const JoyButtonEvent& event) { }
		/** @brief Called when a joystick button is released */
		inline virtual void OnJoyButtonReleased(const JoyButtonEvent& event) { }
		/** @brief Called when a joystick hat is moved */
		inline virtual void OnJoyHatMoved(const JoyHatEvent& event) { }
		/** @brief Called when a joystick axis is moved */
		inline virtual void OnJoyAxisMoved(const JoyAxisEvent& event) { }

		/** @brief Called when a button of a mapped joystick is pressed */
		inline virtual void OnJoyMappedButtonPressed(const JoyMappedButtonEvent& event) { }
		/** @brief Called when a button of a mapped joystick is released */
		inline virtual void OnJoyMappedButtonReleased(const JoyMappedButtonEvent& event) { }
		/** @brief Called when an axis of a mapped joystick is moved */
		inline virtual void OnJoyMappedAxisMoved(const JoyMappedAxisEvent& event) { }

		/** @brief Called when a joystick is connected */
		inline virtual void OnJoyConnected(const JoyConnectionEvent& event) { }
		/** @brief Called when a joystick is disconnected */
		inline virtual void OnJoyDisconnected(const JoyConnectionEvent& event) { }

		/** @brief Called when the system requests to quit (e.g. the window close button); returns `true` to allow it */
		inline virtual bool OnQuitRequest() { return true; }
	};
}
