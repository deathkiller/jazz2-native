#include "IInputManager.h"
#include "JoyMapping.h"

#if defined(WITH_IMGUI)
#	include <imgui.h>
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
					data[i] = (std::uint8_t)std::strtoul(component, nullptr, 16);
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
		joyMapping_.SetHandler(inputEventHandler);
	}

	StringView IInputManager::getKeyName(Keys key) const
	{
		switch (key) {
			case Keys::Backspace: return "Backspace"_s;
			case Keys::Tab: return "Tab"_s;
			case Keys::Return: return "Enter"_s;
			case Keys::Escape: return "Escape"_s;
			case Keys::Space: return "Space"_s;
			case Keys::Quote: return "'"_s;
			case Keys::Plus: return "+"_s;
			case Keys::Comma: return ","_s;
			case Keys::Minus: return "-"_s;
			case Keys::Period: return "."_s;
			case Keys::Slash: return "/"_s;
			case Keys::D0: return "0"_s;
			case Keys::D1: return "1"_s;
			case Keys::D2: return "2"_s;
			case Keys::D3: return "3"_s;
			case Keys::D4: return "4"_s;
			case Keys::D5: return "5"_s;
			case Keys::D6: return "6"_s;
			case Keys::D7: return "7"_s;
			case Keys::D8: return "8"_s;
			case Keys::D9: return "9"_s;
			case Keys::Semicolon: return ";"_s;
			case Keys::LeftBracket: return "["_s;
			case Keys::Backslash: return "\\"_s;
			case Keys::RightBracket: return "]"_s;
			case Keys::Backquote: return "`"_s;

			case Keys::A: return "A"_s;
			case Keys::B: return "B"_s;
			case Keys::C: return "C"_s;
			case Keys::D: return "D"_s;
			case Keys::E: return "E"_s;
			case Keys::F: return "F"_s;
			case Keys::G: return "G"_s;
			case Keys::H: return "H"_s;
			case Keys::I: return "I"_s;
			case Keys::J: return "J"_s;
			case Keys::K: return "K"_s;
			case Keys::L: return "L"_s;
			case Keys::M: return "M"_s;
			case Keys::N: return "N"_s;
			case Keys::O: return "O"_s;
			case Keys::P: return "P"_s;
			case Keys::Q: return "Q"_s;
			case Keys::R: return "R"_s;
			case Keys::S: return "S"_s;
			case Keys::T: return "T"_s;
			case Keys::U: return "U"_s;
			case Keys::V: return "V"_s;
			case Keys::W: return "W"_s;
			case Keys::X: return "X"_s;
			case Keys::Y: return "Y"_s;
			case Keys::Z: return "Z"_s;
			case Keys::Delete: return "Del"_s;

			case Keys::NumPad0: return "Keypad 0"_s;
			case Keys::NumPad1: return "Keypad 1"_s;
			case Keys::NumPad2: return "Keypad 2"_s;
			case Keys::NumPad3: return "Keypad 3"_s;
			case Keys::NumPad4: return "Keypad 4"_s;
			case Keys::NumPad5: return "Keypad 5"_s;
			case Keys::NumPad6: return "Keypad 6"_s;
			case Keys::NumPad7: return "Keypad 7"_s;
			case Keys::NumPad8: return "Keypad 8"_s;
			case Keys::NumPad9: return "Keypad 9"_s;
			case Keys::NumPadPeriod: return "Keypad ."_s;
			case Keys::NumPadDivide: return "Keypad /"_s;
			case Keys::NumPadMultiply: return "Keypad *"_s;
			case Keys::NumPadMinus: return "Keypad -"_s;
			case Keys::NumPadPlus: return "Keypad +"_s;
			case Keys::NumPadEnter: return "Keypad Enter"_s;
			case Keys::NumPadEquals: return "Keypad ="_s;

			case Keys::Up: return "Up"_s;
			case Keys::Down: return "Down"_s;
			case Keys::Right: return "Right"_s;
			case Keys::Left: return "Left"_s;
			case Keys::Insert: return "Ins"_s;
			case Keys::Home: return "Home"_s;
			case Keys::End: return "End"_s;
			case Keys::PageUp: return "PgUp"_s;
			case Keys::PageDown: return "PgDn"_s;

			case Keys::F1: return "F1"_s;
			case Keys::F2: return "F2"_s;
			case Keys::F3: return "F3"_s;
			case Keys::F4: return "F4"_s;
			case Keys::F5: return "F5"_s;
			case Keys::F6: return "F6"_s;
			case Keys::F7: return "F7"_s;
			case Keys::F8: return "F8"_s;
			case Keys::F9: return "F9"_s;
			case Keys::F10: return "F10"_s;
			case Keys::F11: return "F11"_s;
			case Keys::F12: return "F12"_s;
			case Keys::F13: return "F13"_s;
			case Keys::F14: return "F14"_s;
			case Keys::F15: return "F15"_s;

			case Keys::NumLock: return "Num Lock"_s;
			case Keys::CapsLock: return "Caps Lock"_s;
			case Keys::ScrollLock: return "Scroll Lock"_s;
			case Keys::RShift: return "R. Shift"_s;
			case Keys::LShift: return "Shift"_s;
			case Keys::RCtrl: return "R. Ctrl"_s;
			case Keys::LCtrl: return "Ctrl"_s;
			case Keys::RAlt: return "R. Alt"_s;
			case Keys::LAlt: return "Alt"_s;
			case Keys::Pause: return "Pause"_s;
			case Keys::Menu: return "Menu"_s;

			default: return {};
		}
	}

	/*! \note Joystick will stay mapped in the`OnJoyConnected()` and `OnJoyDisconnected()` callbacks */
	bool IInputManager::isJoyMapped(int joyId) const
	{
		return joyMapping_.IsJoyMapped(joyId);
	}

	const JoyMappedState& IInputManager::joyMappedState(int joyId) const
	{
		return joyMapping_.GetMappedState(joyId);
	}

	void IInputManager::deadZoneNormalize(Vector2f& joyVector, float deadZoneValue) const
	{
		return joyMapping_.DeadZoneNormalize(joyVector, deadZoneValue);
	}

	void IInputManager::addJoyMappingsFromFile(StringView path)
	{
		joyMapping_.AddMappingsFromFile(path);
	}

	void IInputManager::addJoyMappingsFromString(StringView mappingString)
	{
		joyMapping_.AddMappingsFromString(mappingString);
	}

	unsigned int IInputManager::numJoyMappings() const
	{
		return joyMapping_.numMappings();
	}

	bool IInputManager::hasMappingByGuid(const JoystickGuid& guid) const
	{
		return (joyMapping_.FindMappingByGuid(guid) != -1);
	}

	bool IInputManager::hasMappingByName(const char* name) const
	{
		return (joyMapping_.FindMappingByName(name) != -1);
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
