#include "IInputManager.h"
#include "JoyMapping.h"

#if defined(WITH_IMGUI)
#	include "imgui.h"
#endif

using namespace Death::Containers::Literals;

namespace nCine
{
	IInputEventHandler* IInputManager::inputEventHandler_ = nullptr;
	IInputManager::Cursor IInputManager::cursor_ = IInputManager::Cursor::Arrow;
	JoyMapping IInputManager::joyMapping_;

	JoystickGuid::JoystickGuid()
	{
		std::memset(data, 0, sizeof(data));
	}

	void JoystickGuid::fromType(JoystickGuidType type)
	{
		uint32_t* guid32 = (uint32_t*)data;
		switch (type) {
			case JoystickGuidType::Default:
			case JoystickGuidType::Hidapi:
			case JoystickGuidType::Xinput:
				guid32[0] = UINT32_MAX;	// Bus & CRC16 fields
				guid32[1] = (uint32_t)type;
				guid32[2] = 0;
				guid32[3] = 0;
				break;
			default:
				guid32[0] = 0;
				guid32[1] = 0;
				guid32[2] = 0;
				guid32[3] = 0;
				break;
		}
	}

	void JoystickGuid::fromString(const StringView& string)
	{
		if (string.empty()) {
			fromType(JoystickGuidType::Unknown);
		} else if (string == "default"_s) {
			fromType(JoystickGuidType::Default);
		} else if (string == "hidapi"_s) {
			fromType(JoystickGuidType::Hidapi);
		} else if (string == "xinput"_s) {
			fromType(JoystickGuidType::Xinput);
		} else {
			if (string.size() != sizeof(data) * 2) {
				fromType(JoystickGuidType::Unknown);
			} else {
				char component[3];
				component[2] = '\0';
				for (uint32_t i = 0, j = 0; i < sizeof(data); i++) {
					component[0] = string[j++];
					component[1] = string[j++];
					data[i] = std::strtoul(component, nullptr, 16);
				}
			}
		}
	}

	bool JoystickGuid::isValid() const
	{
		uint32_t* guid32 = (uint32_t*)data;
		return (guid32[0] != 0 || guid32[1] != 0 || guid32[2] != 0 || guid32[3] != 0);
	}

	bool JoystickGuid::operator==(const JoystickGuid& guid) const
	{
		return std::memcmp(data, guid.data, sizeof(data)) == 0;
	}

	void IInputManager::setHandler(IInputEventHandler* inputEventHandler)
	{
		inputEventHandler_ = inputEventHandler;
		joyMapping_.setHandler(inputEventHandler);
	}

	/*! \note Joystick will stay mapped in the`OnJoyConnected()` and `OnJoyDisconnected()` callbacks */
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

	void IInputManager::addJoyMappingsFromFile(const StringView& path)
	{
		joyMapping_.addMappingsFromFile(path);
	}

	void IInputManager::addJoyMappingsFromStrings(const char** mappingStrings)
	{
		joyMapping_.addMappingsFromStrings(mappingStrings);
	}

	unsigned int IInputManager::numJoyMappings() const
	{
		return joyMapping_.numMappings();
	}

	bool IInputManager::hasMappingByGuid(const JoystickGuid& guid) const
	{
		return (joyMapping_.findMappingByGuid(guid) != -1);
	}

	bool IInputManager::hasMappingByName(const char* name) const
	{
		return (joyMapping_.findMappingByName(name) != -1);
	}

	void IInputManager::setCursor(Cursor cursor)
	{
#if defined(WITH_IMGUI)
		ImGuiIO& io = ImGui::GetIO();
		if (cursor != Cursor::Arrow) {
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		} else {
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
		}
#endif
	}
}
