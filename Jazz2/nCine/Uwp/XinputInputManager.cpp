#include "XinputInputManager.h"
#include "../Input/JoyMapping.h"

namespace nCine
{
	const int IInputManager::MaxNumJoysticks = 4;

	XinputMouseState XinputInputManager::mouseState_;
	XinputKeyboardState XinputInputManager::keyboardState_;
	XinputJoystickState XinputInputManager::joystickState_;

	XinputInputManager::XinputInputManager()
	{
		joyMapping_.init(this);
	}

	XinputInputManager::~XinputInputManager()
	{

	}
}
