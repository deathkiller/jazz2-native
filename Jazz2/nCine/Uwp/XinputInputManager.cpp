#include "XinputInputManager.h"

namespace nCine
{
	const int IInputManager::MaxNumJoysticks = 4;

	XinputMouseState XinputInputManager::mouseState_;
	XinputKeyboardState XinputInputManager::keyboardState_;
	XinputJoystickState XinputInputManager::joystickState_;
}
