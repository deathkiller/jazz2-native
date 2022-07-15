#include "IInputManager.h"
#include "JoyMapping.h"

#ifdef WITH_IMGUI
#include "imgui.h"
#endif

namespace nCine {

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	IInputEventHandler* IInputManager::inputEventHandler_ = nullptr;
	IInputManager::Cursor IInputManager::cursor_ = IInputManager::Cursor::Arrow;
	JoyMapping IInputManager::joyMapping_;

	// From `XInput.h` in DirectX SDK
	const float IInputManager::LeftStickDeadZone = 7849 / 32767.0f;
	const float IInputManager::RightStickDeadZone = 8689 / 32767.0f;
	const float IInputManager::TriggerDeadZone = 30 / 32767.0f;

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void IInputManager::setHandler(IInputEventHandler* inputEventHandler)
	{
		inputEventHandler_ = inputEventHandler;
		joyMapping_.setHandler(inputEventHandler);
	}

	/*! \note Joystick will stay mapped in the`onJoyConnected()` and `onJoyDisconnected()` callbacks */
	bool IInputManager::isJoyMapped(int joyId) const
	{
		return joyMapping_.isJoyMapped(joyId);
	}

	const JoyMappedState& IInputManager::joyMappedState(int joyId) const
	{
		return joyMapping_.joyMappedState(joyId);
	}

	void IInputManager::deadZoneNormalize(Vector2f& joyVector, float deadZoneValue) const
	{
		return joyMapping_.deadZoneNormalize(joyVector, deadZoneValue);
	}

	void IInputManager::addJoyMappingsFromFile(const char* filename)
	{
		joyMapping_.addMappingsFromFile(filename);
	}

	void IInputManager::addJoyMappingsFromStrings(const char** mappingStrings)
	{
		joyMapping_.addMappingsFromStrings(mappingStrings);
	}

	unsigned int IInputManager::numJoyMappings() const
	{
		return joyMapping_.numMappings();
	}

	void IInputManager::setCursor(Cursor cursor)
	{
#ifdef WITH_IMGUI
		ImGuiIO& io = ImGui::GetIO();
		if (cursor != Cursor::Arrow)
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		else
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
#endif
	}

}
