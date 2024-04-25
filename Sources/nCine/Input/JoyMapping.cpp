#include "JoyMapping.h"
#include "IInputManager.h"
#include "IInputEventHandler.h"
#include "../Primitives/Vector2.h"

#include <cstring>	// for memcpy()

#include <Containers/SmallVector.h>
#include <Containers/StringUtils.h>
#include <Containers/StringView.h>
#include <IO/FileSystem.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	namespace
	{
#include "JoyMappingDb.h"

#if defined(DEATH_TARGET_ANDROID)
		constexpr AxisName AndroidAxisNameMapping[] = {
			AxisName::LeftX,
			AxisName::LeftY,
			AxisName::RightX,
			AxisName::RightY,
			AxisName::LeftTrigger,
			AxisName::RightTrigger
		};

		constexpr ButtonName AndroidDpadButtonNameMapping[] = {
			ButtonName::Up,
			ButtonName::Right,
			ButtonName::Down,
			ButtonName::Left
		};
#endif

		// TODO: Implement CRC
		//static uint16_t crc16(uint16_t crc, const void* data, size_t len)
		//{
		//	for (size_t i = 0; i < len; ++i) {
		//		uint8_t r = (uint8_t)crc ^ ((const uint8_t*)data)[i];
		//		uint16_t crcByte = 0;
		//		for (int j = 0; j < 8; ++j) {
		//			crcByte = ((crcByte ^ r) & 1 ? 0xA001 : 0) ^ crcByte >> 1;
		//			r >>= 1;
		//		}
		//		crc = crcByte ^ crc >> 8;
		//	}
		//	return crc;
		//}
	}

	const unsigned int JoyMapping::MaxNameLength;

	const char* JoyMapping::AxesStrings[] = {
		"leftx",
		"lefty",
		"rightx",
		"righty",
		"lefttrigger",
		"righttrigger"
	};

	const char* JoyMapping::ButtonsStrings[] = {
		"a",
		"b",
		"x",
		"y",
		"back",
		"guide",
		"start",
		"leftstick",
		"rightstick",
		"leftshoulder",
		"rightshoulder",
		"dpup",
		"dpdown",
		"dpleft",
		"dpright",
		"misc1",
		"paddle1",
		"paddle2",
		"paddle3",
		"paddle4"
	};

	JoyMappedState JoyMapping::nullMappedJoyState_;
	SmallVector<JoyMappedState, JoyMapping::MaxNumJoysticks> JoyMapping::mappedJoyStates_(JoyMapping::MaxNumJoysticks);
	JoyMappedButtonEvent JoyMapping::mappedButtonEvent_;
	JoyMappedAxisEvent JoyMapping::mappedAxisEvent_;

	JoyMapping::MappingDescription::MappingDescription()
	{
		for (unsigned int i = 0; i < MaxNumAxes; i++) {
			axes[i].name = AxisName::Unknown;
		}
		for (unsigned int i = 0; i < MaxNumAxes; i++) {
			buttonAxes[i] = AxisName::Unknown;
		}
		for (unsigned int i = 0; i < MaxNumButtons; i++) {
			buttons[i] = ButtonName::Unknown;
		}
		for (unsigned int i = 0; i < MaxHatButtons; i++) {
			hats[i] = ButtonName::Unknown;
		}
	}

	JoyMapping::MappedJoystick::MappedJoystick()
	{
		name[0] = '\0';
	}

	JoyMapping::JoyMapping()
		: inputManager_(nullptr), inputEventHandler_(nullptr)
	{
		for (unsigned int i = 0; i < MaxNumJoysticks; i++) {
			assignedMappings_[i].isValid = false;
		}
	}

	void JoyMapping::init(const IInputManager* inputManager)
	{
		ASSERT(inputManager != nullptr);
		inputManager_ = inputManager;

		unsigned int numStrings = 0;

		// Add mappings from the database, without searching for duplicates
		const char** mappingStrings = ControllerMappings;
		while (*mappingStrings) {
			numStrings++;
			MappedJoystick mapping;
			const bool parsed = parseMappingFromString(*mappingStrings, mapping);
			if (parsed) {
				mappings_.emplace_back(std::move(mapping));
			}
			mappingStrings++;
		}

		LOGI("Found %u mappings in %u lines", mappings_.size(), numStrings);

		checkConnectedJoystics();
	}

	bool JoyMapping::addMappingFromString(const char* mappingString)
	{
		ASSERT(mappingString != nullptr);

		MappedJoystick newMapping;
		const bool parsed = parseMappingFromString(mappingString, newMapping);
		if (parsed) {
			int index = findMappingByGuid(newMapping.guid);
			// if GUID is not found then mapping has to be added, not replaced
			if (index < 0) {
				mappings_.emplace_back(std::move(newMapping));
			} else {
				mappings_[index] = std::move(newMapping);
			}
		}
		checkConnectedJoystics();

		return parsed;
	}

	void JoyMapping::addMappingsFromStrings(const char** mappingStrings)
	{
		ASSERT(mappingStrings != nullptr);

		while (*mappingStrings) {
			MappedJoystick newMapping;
			const bool parsed = parseMappingFromString(*mappingStrings, newMapping);
			if (parsed) {
				int index = findMappingByGuid(newMapping.guid);
				// if GUID is not found then mapping has to be added, not replaced
				if (index < 0) {
					mappings_.emplace_back(std::move(newMapping));
				} else {
					mappings_[index] = std::move(newMapping);
				}
			}
			mappingStrings++;
		}

		checkConnectedJoystics();
	}

	void JoyMapping::addMappingsFromFile(const StringView& path)
	{
		std::unique_ptr<Stream> fileHandle = fs::Open(path, FileAccessMode::Read);
		std::int64_t fileSize = fileHandle->GetSize();
		if (fileSize == 0 || fileSize > 32 * 1024 * 1024) {
			return;
		}

		unsigned int fileLine = 0;
		std::unique_ptr<char[]> fileBuffer = std::make_unique<char[]>(fileSize + 1);
		fileHandle->Read(fileBuffer.get(), fileSize);
		fileHandle.reset(nullptr);
		fileBuffer[fileSize] = '\0';

		unsigned int numParsed = 0;
		const char* buffer = fileBuffer.get();
		do {
			fileLine++;

			MappedJoystick newMapping;
			const bool parsed = parseMappingFromString(buffer, newMapping);
			if (parsed) {
				numParsed++;
				int index = findMappingByGuid(newMapping.guid);
				// if GUID is not found then mapping has to be added, not replaced
				if (index < 0) {
					mappings_.emplace_back(std::move(newMapping));
				} else {
					mappings_[index] = std::move(newMapping);
				}
			}

		} while (strchr(buffer, '\n') && (buffer = strchr(buffer, '\n') + 1) < fileBuffer.get() + fileSize);

		LOGI("Joystick mapping file \"%s\" parsed: %u mappings in %u lines", String::nullTerminatedView(path).data(), numParsed, fileLine);

		fileBuffer.reset(nullptr);

		checkConnectedJoystics();
	}

	void JoyMapping::onJoyButtonPressed(const JoyButtonEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Button pressed - joyId: %d, buttonId: %d", event.joyId, event.buttonId);
#endif

		if (inputEventHandler_ == nullptr) {
			return;
		}

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.buttonId >= 0 && event.buttonId < static_cast<int>(MappingDescription::MaxNumButtons));
		if (mappingIsValid) {
			mappedButtonEvent_.joyId = event.joyId;
			mappedButtonEvent_.buttonName = mapping.desc.buttons[event.buttonId];
			if (mappedButtonEvent_.buttonName != ButtonName::Unknown) {
				const int buttonId = static_cast<int>(mappedButtonEvent_.buttonName);
#if defined(NCINE_INPUT_DEBUGGING)
				LOGI("Button press mapped as button %d", buttonId);
#endif
				mappedJoyStates_[event.joyId].buttons_[buttonId] = true;
				inputEventHandler_->OnJoyMappedButtonPressed(mappedButtonEvent_);
			} else {
				// Check if the button is mapped as an axis
				const AxisName axisName = mapping.desc.buttonAxes[event.buttonId];
				if (axisName != AxisName::Unknown) {
					mappedAxisEvent_.joyId = event.joyId;
					mappedAxisEvent_.axisName = axisName;
					mappedAxisEvent_.value = 1.0f;
#if defined(NCINE_INPUT_DEBUGGING)
					LOGI("Button press mapped as axis %d", static_cast<int>(axisName));
#endif
					mappedJoyStates_[event.joyId].axesValues_[static_cast<int>(axisName)] = mappedAxisEvent_.value;
					inputEventHandler_->OnJoyMappedAxisMoved(mappedAxisEvent_);
				} else {
#if defined(NCINE_INPUT_DEBUGGING)
					LOGW("Button release has incorrect mapping");
#endif
				}
			}
		} else {
#if defined(NCINE_INPUT_DEBUGGING)
			LOGW("Button press has no mapping");
#endif
		}
	}

	void JoyMapping::onJoyButtonReleased(const JoyButtonEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Button released - joyId: %d, buttonId: %d", event.joyId, event.buttonId);
#endif

		if (inputEventHandler_ == nullptr) {
			return;
		}

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.buttonId >= 0 && event.buttonId < static_cast<int>(MappingDescription::MaxNumButtons));
		if (mappingIsValid) {
			mappedButtonEvent_.joyId = event.joyId;
			mappedButtonEvent_.buttonName = mapping.desc.buttons[event.buttonId];
			if (mappedButtonEvent_.buttonName != ButtonName::Unknown) {
				const int buttonId = static_cast<int>(mappedButtonEvent_.buttonName);
#if defined(NCINE_INPUT_DEBUGGING)
				LOGI("Button release mapped as button %d", buttonId);
#endif
				mappedJoyStates_[event.joyId].buttons_[buttonId] = false;
				inputEventHandler_->OnJoyMappedButtonReleased(mappedButtonEvent_);
			} else {
				// Check if the button is mapped as an axis
				const AxisName axisName = mapping.desc.buttonAxes[event.buttonId];
				if (axisName != AxisName::Unknown) {
					mappedAxisEvent_.joyId = event.joyId;
					mappedAxisEvent_.axisName = axisName;
					mappedAxisEvent_.value = 0.0f;
#if defined(NCINE_INPUT_DEBUGGING)
					LOGI("Button release mapped as axis %d", static_cast<int>(axisName));
#endif
					mappedJoyStates_[event.joyId].axesValues_[static_cast<int>(axisName)] = mappedAxisEvent_.value;
					inputEventHandler_->OnJoyMappedAxisMoved(mappedAxisEvent_);
				} else {
#if defined(NCINE_INPUT_DEBUGGING)
					LOGW("Button release has incorrect mapping");
#endif
				}
			}
		} else {
#if defined(NCINE_INPUT_DEBUGGING)
			LOGW("Button release has no mapping");
#endif
		}
	}

	void JoyMapping::onJoyHatMoved(const JoyHatEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Hat moved - joyId: %d, hatId: %d, hatState: 0x%02x", event.joyId, event.hatId, event.hatState);
#endif

		if (inputEventHandler_ == nullptr) {
			return;
		}

		const auto& mapping = assignedMappings_[event.joyId];
		// Only the first gamepad hat is mapped
		const bool mappingIsValid = (mapping.isValid && event.hatId == 0 && mappedJoyStates_[event.joyId].lastHatState_ != event.hatState);
		if (mappingIsValid) {
			mappedButtonEvent_.joyId = event.joyId;

			const unsigned char oldHatState = mappedJoyStates_[event.joyId].lastHatState_;
			const unsigned char newHatState = event.hatState;

			constexpr unsigned char FirstHatValue = HatState::Up;
			constexpr unsigned char LastHatValue = HatState::Left;
			for (unsigned char hatValue = FirstHatValue; hatValue <= LastHatValue; hatValue *= 2) {
				if ((oldHatState & hatValue) != (newHatState & hatValue)) {
					int hatIndex = hatStateToIndex(hatValue);
					mappedButtonEvent_.buttonName = mapping.desc.hats[hatIndex];
					if (mappedButtonEvent_.buttonName != ButtonName::Unknown) {
						const int buttonId = static_cast<int>(mappedButtonEvent_.buttonName);
						if (newHatState & hatValue) {
#if defined(NCINE_INPUT_DEBUGGING)
							LOGI("Hat move mapped as button press %d", buttonId);
#endif
							mappedJoyStates_[event.joyId].buttons_[buttonId] = true;
							inputEventHandler_->OnJoyMappedButtonPressed(mappedButtonEvent_);
						} else {
#if defined(NCINE_INPUT_DEBUGGING)
							LOGI("Hat move mapped as button release %d", buttonId);
#endif
							mappedJoyStates_[event.joyId].buttons_[buttonId] = false;
							inputEventHandler_->OnJoyMappedButtonReleased(mappedButtonEvent_);
						}
					}
				}
			}
			mappedJoyStates_[event.joyId].lastHatState_ = event.hatState;
		} else {
#if defined(NCINE_INPUT_DEBUGGING)
			if (!mapping.isValid || event.hatId != 0) {
				LOGW("Hat move has no mapping");
			}
#endif
		}
	}

	void JoyMapping::onJoyAxisMoved(const JoyAxisEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Axis moved - joyId: %d, axisId: %d, value: %f", event.joyId, event.axisId, event.value);
#endif

		if (inputEventHandler_ == nullptr) {
			return;
		}

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.axisId >= 0 && event.axisId < static_cast<int>(MappingDescription::MaxNumAxes));
		if (mappingIsValid) {
			const auto& axis = mapping.desc.axes[event.axisId];

			mappedAxisEvent_.joyId = event.joyId;
			mappedAxisEvent_.axisName = axis.name;
			if (mappedAxisEvent_.axisName != AxisName::Unknown) {
				const float value = (event.value + 1.0f) * 0.5f;
				mappedAxisEvent_.value = axis.min + value * (axis.max - axis.min);
#if defined(NCINE_INPUT_DEBUGGING)
				LOGI("Axis move mapped as axis %d (value: %f, normalized: %f, min: %f, max: %f)", static_cast<int>(axis.name), mappedAxisEvent_.value, value, axis.min, axis.max);
#endif
				mappedJoyStates_[event.joyId].axesValues_[static_cast<int>(axis.name)] = mappedAxisEvent_.value;
				inputEventHandler_->OnJoyMappedAxisMoved(mappedAxisEvent_);
			} else {
#if defined(NCINE_INPUT_DEBUGGING)
				LOGW("Axis move has incorrect mapping");
#endif
			}
		} else {
#if defined(NCINE_INPUT_DEBUGGING)
			LOGW("Axis move has no mapping");
#endif
		}
	}

	bool JoyMapping::onJoyConnected(const JoyConnectionEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Controller connected - joyId: %d", event.joyId);
#endif

		const char* joyName = inputManager_->joyName(event.joyId);
		const JoystickGuid joyGuid = inputManager_->joyGuid(event.joyId);

		auto& mapping = assignedMappings_[event.joyId];
		mapping.isValid = false;

		if (joyGuid.isValid()) {
			const int index = findMappingByGuid(joyGuid);
			if (index != -1) {
				mapping.isValid = true;
				mapping.desc = mappings_[index].desc;

				const uint8_t* g = joyGuid.data;
				LOGI("Joystick mapping found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d), also known as \"%s\"", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId, mappings_[index].name);
			}
		}

#if defined(DEATH_TARGET_ANDROID)
		// Never search by name on Android, it can lead to wrong mapping
		if (!mapping.isValid) {
			auto joyNameLower = StringUtils::lowercase(StringView(joyName));
			// Don't assign Android default mapping to internal NVIDIA Shield devices, WSA devices and mice (detected as gamepads)
			if (joyNameLower == "virtual-search"_s || joyNameLower == "shield-ask-remote"_s || joyNameLower == "virtual_keyboard"_s || joyNameLower.contains("mouse"_s)) {
				return false;
			}

			const uint8_t* g = joyGuid.data;
			LOGI("Joystick mapping not found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d), using Android default mapping", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId);

			mapping.isValid = true;

			for (int i = 0; i < countof(AndroidAxisNameMapping); i++) {
				mapping.desc.axes[i].name = AndroidAxisNameMapping[i];
				if (mapping.desc.axes[i].name == AxisName::LeftTrigger || mapping.desc.axes[i].name == AxisName::RightTrigger) {
					mapping.desc.axes[i].min = 0.0f;
				} else {
					mapping.desc.axes[i].min = -1.0f;
				}
				mapping.desc.axes[i].max = 1.0f;
			}

			constexpr int AndroidButtonCount = (int)ButtonName::Misc1;
			for (int i = 0; i < AndroidButtonCount; i++) {
				mapping.desc.buttons[i] = (ButtonName)i;
			}
			for (int i = AndroidButtonCount; i < countof(mapping.desc.buttons); i++) {
				mapping.desc.buttons[i] = ButtonName::Unknown;
			}

			for (int i = 0; i < countof(AndroidDpadButtonNameMapping); i++) {
				mapping.desc.hats[i] = AndroidDpadButtonNameMapping[i];
			}
		}
#elif !defined(DEATH_TARGET_EMSCRIPTEN)
		if (!mapping.isValid) {
			const int index = findMappingByName(joyName);
			if (index != -1) {
				mapping.isValid = true;
				mapping.desc = mappings_[index].desc;

				const uint8_t* g = joyGuid.data;
				LOGI("Joystick mapping found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d)", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId);
			}
		}

		if (!mapping.isValid) {
#	if defined(DEATH_TARGET_UNIX)
			// Keyboards, mice and touchpads (SynPS/2 Synaptics TouchPad), and also VMware virtual devices on Linux/BSD
			// are incorrectly recognized as joystick in some cases, don't assign XInput mapping to them
			auto joyNameLower = StringUtils::lowercase(StringView(joyName));
			if (joyNameLower.contains("keyboard"_s) || joyNameLower.contains("mouse"_s) ||
				(joyNameLower.contains("razer "_s) && joyNameLower.contains("deathadder"_s)) ||
				(joyNameLower == "synps/2 synaptics touchpad"_s) ||
				(joyNameLower == "vmware virtual usb mouse"_s)) {
				return false;
			}
#	endif
			const int index = findMappingByGuid(JoystickGuidType::Xinput);
			if (index != -1) {
				mapping.isValid = true;
				mapping.desc = mappings_[index].desc;

				const uint8_t* g = joyGuid.data;
				LOGI("Joystick mapping not found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d), using XInput mapping", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId);
			}
		}
#endif

		return mapping.isValid;
	}

	void JoyMapping::onJoyDisconnected(const JoyConnectionEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Controller disconnected - joyId: %d", event.joyId);
#endif

#if defined(WITH_SDL)
		// Compacting the array of mapping indices
		for (int i = event.joyId; i < MaxNumJoysticks - 1; i++) {
			assignedMappings_[i] = assignedMappings_[i + 1];
		}
		assignedMappings_[MaxNumJoysticks - 1].isValid = false;
#else
		assignedMappings_[event.joyId].isValid = false;
#endif
	}

	bool JoyMapping::isJoyMapped(int joyId) const
	{
		return (joyId >= 0 && joyId < MaxNumJoysticks && assignedMappings_[joyId].isValid);
	}

	const JoyMappedState& JoyMapping::joyMappedState(int joyId) const
	{
		if (joyId < 0 || joyId >= MaxNumJoysticks) {
			return nullMappedJoyState_;
		} else {
			return mappedJoyStates_[joyId];
		}
	}

	void JoyMapping::deadZoneNormalize(Vector2f& joyVector, float deadZoneValue) const
	{
		deadZoneValue = std::clamp(deadZoneValue, 0.0f, 1.0f);

		if (joyVector.Length() <= deadZoneValue) {
			joyVector = Vector2f::Zero;
		} else {
			float normalizedLength = (joyVector.Length() - deadZoneValue) / (1.0f - deadZoneValue);
			normalizedLength = std::clamp(normalizedLength, 0.0f, 1.0f);
			joyVector = joyVector.Normalize() * normalizedLength;
		}
	}

	JoystickGuid JoyMapping::createJoystickGuid(uint16_t bus, uint16_t vendor, uint16_t product, uint16_t version, const StringView& name, uint8_t driverSignature, uint8_t driverData)
	{
		JoystickGuid guid;
		uint16_t* guid16 = reinterpret_cast<uint16_t*>(guid.data);

		*guid16++ = bus;
		// TODO: Implement CRC
		//*guid16++ = crc16(0, name.data(), name.size());
		*guid16++ = 0;

		if (vendor != 0 && product != 0) {
			*guid16++ = vendor;
			*guid16++ = 0;
			*guid16++ = product;
			*guid16++ = 0;
			*guid16++ = version;
			guid.data[14] = driverSignature;
			guid.data[15] = driverData;
		} else {
			size_t availableSpace = sizeof(guid.data) - 4;

			if (driverSignature != 0) {
				availableSpace -= 2;
				guid.data[14] = driverSignature;
				guid.data[15] = driverData;
			}

			if (!name.empty()) {
				std::memcpy(guid16, name.data(), std::min(name.size(), availableSpace));
			}
		}
		return guid;
	}

	void JoyMapping::checkConnectedJoystics()
	{
		if (inputManager_ == nullptr) {
			return;
		}

		for (int i = 0; i < MaxNumJoysticks; i++) {
			if (inputManager_->isJoyPresent(i)) {
				JoyConnectionEvent event;
				event.joyId = i;
				onJoyConnected(event);
			}
		}
	}

	int JoyMapping::findMappingByGuid(const JoystickGuid& guid) const
	{
		int index = -1;

		const unsigned int size = mappings_.size();
		for (unsigned int i = 0; i < size; i++) {
			if (mappings_[i].guid == guid) {
				index = static_cast<int>(i);
				break;
			}
		}

		return index;
	}

	int JoyMapping::findMappingByName(const char* name) const
	{
		int index = -1;

		const unsigned int size = mappings_.size();
		for (unsigned int i = 0; i < size; i++) {
			if (strncmp(mappings_[i].name, name, MaxNameLength) == 0) {
				index = static_cast<int>(i);
				break;
			}
		}

		return index;
	}

	bool JoyMapping::parseMappingFromString(const char* mappingString, MappedJoystick& map)
	{
		// Early out if the string is empty or a comment
		if (mappingString[0] == '\0' || mappingString[0] == '\n' || mappingString[0] == '#') {
			return false;
		}

		const char* end = mappingString + strlen(mappingString);
		const char* subStartUntrimmed = mappingString;
		const char* subEndUntrimmed = strchr(subStartUntrimmed, ',');

		const char* subStart = subStartUntrimmed;
		const char* subEnd = subEndUntrimmed;
		trimSpaces(&subStart, &subEnd);

		if (subEndUntrimmed == nullptr) {
			LOGE("Invalid mapping string");
			return false;
		}
		unsigned int subLength = static_cast<unsigned int>(subEnd - subStart);
		map.guid.fromString(StringView(subStart, subLength));

		subStartUntrimmed = subEndUntrimmed + 1; // GUID plus the following ',' character
		subEndUntrimmed = strchr(subStartUntrimmed, ',');
		if (subEndUntrimmed == nullptr) {
			LOGE("Invalid mapping string");
			return false;
		}
		subStart = subStartUntrimmed;
		subEnd = subEndUntrimmed;
		trimSpaces(&subStart, &subEnd);

		subLength = static_cast<unsigned int>(subEnd - subStart);
		memcpy(map.name, subStart, std::min(subLength, MaxNameLength));
		map.name[std::min(subLength, MaxNameLength)] = '\0';

		subStartUntrimmed = subEndUntrimmed + 1; // name plus the following ',' character
		subEndUntrimmed = strchr(subStartUntrimmed, ',');
		while (subStartUntrimmed < end && *subStartUntrimmed != '\n') {
			subStart = subStartUntrimmed;
			subEnd = subEndUntrimmed;
			trimSpaces(&subStart, &subEnd);

			const char* subMid = strchr(subStart, ':');
			if (subMid == nullptr || subEnd == nullptr) {
				LOGE("Invalid mapping string");
				return false;
			}

			if (parsePlatformKeyword(subStart, subMid)) {
				if (!parsePlatformName(subMid + 1, subEnd)) {
					return false;
				}
			} else {
				const int axisIndex = parseAxisName(subStart, subMid);
				if (axisIndex != -1) {
					MappingDescription::Axis axis;
					axis.name = static_cast<AxisName>(axisIndex);
					const int axisMapping = parseAxisMapping(subMid + 1, subEnd, axis);
					if (axisMapping != -1 && axisMapping < MappingDescription::MaxNumAxes) {
						map.desc.axes[axisMapping] = axis;
					} else {
						// The same parsing method for buttons will be used for button axes
						const int buttonAxisMapping = parseButtonMapping(subMid + 1, subEnd);
						if (buttonAxisMapping != -1 && buttonAxisMapping < MappingDescription::MaxNumAxes) {
							map.desc.buttonAxes[buttonAxisMapping] = static_cast<AxisName>(axisIndex);
						}
					}
				} else {
					const int buttonIndex = parseButtonName(subStart, subMid);
					if (buttonIndex != -1) {
						const int buttonMapping = parseButtonMapping(subMid + 1, subEnd);
						if (buttonMapping != -1 && buttonMapping < MappingDescription::MaxNumButtons)
							map.desc.buttons[buttonMapping] = static_cast<ButtonName>(buttonIndex);
						else {
							const int hatMapping = parseHatMapping(subMid + 1, subEnd);
							if (hatMapping != -1 && hatMapping < MappingDescription::MaxHatButtons)
								map.desc.hats[hatMapping] = static_cast<ButtonName>(buttonIndex);
						}
					}
				}
			}

			subStartUntrimmed = subEndUntrimmed + 1;
			if (subStartUntrimmed < end) {
				subEndUntrimmed = strchr(subStartUntrimmed, ',');
				if (subEndUntrimmed == nullptr)
					subEndUntrimmed = end;
			}
		}

		return true;
	}

	bool JoyMapping::parsePlatformKeyword(const char* start, const char* end) const
	{
		return (strncmp(start, "platform", end - start) == 0);
	}

	bool JoyMapping::parsePlatformName(const char* start, const char* end) const
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WINDOWS_RT)
		return false;
#else
#	if defined(DEATH_TARGET_WINDOWS)
		static const char platformName[] = "Windows";
#	elif defined(DEATH_TARGET_ANDROID)
		static const char platformName[] = "Android";
#	elif defined(DEATH_TARGET_IOS)
		static const char platformName[] = "iOS";
#	elif defined(DEATH_TARGET_APPLE)
		static const char platformName[] = "Mac OS X";
#	else
		static const char platformName[] = "Linux";
#	endif

		return (strncmp(start, platformName, end - start) == 0);
#endif
	}

	int JoyMapping::parseAxisName(const char* start, const char* end) const
	{
		int axisIndex = -1;

		for (unsigned int i = 0; i < countof(AxesStrings); i++) {
			if (strncmp(start, AxesStrings[i], end - start) == 0) {
				axisIndex = i;
				break;
			}
		}

		return axisIndex;
	}

	int JoyMapping::parseButtonName(const char* start, const char* end) const
	{
		int buttonIndex = -1;

		for (unsigned int i = 0; i < countof(ButtonsStrings); i++) {
			if (strncmp(start, ButtonsStrings[i], end - start) == 0) {
				buttonIndex = i;
				break;
			}
		}

		return buttonIndex;
	}

	int JoyMapping::parseAxisMapping(const char* start, const char* end, MappingDescription::Axis& axis) const
	{
		int axisMapping = -1;

		axis.max = 1.0f;
		axis.min = -1.0f;

		if (end - start <= 5 && (start[0] == 'a' || start[1] == 'a')) {
			const char* digits = &start[1];

			if (axis.name == AxisName::LeftTrigger || axis.name == AxisName::RightTrigger) {
				axis.min = 0.0f;
				axis.max = 1.0f;
			}

			if (start[0] == '+') {
				axis.min = 0.0f;
				axis.max = 1.0f;
				digits++;
			} else if (start[0] == '-') {
				axis.min = 0.0f;
				axis.max = -1.0f;
				digits++;
			}

			axisMapping = atoi(digits);

			if (end[-1] == '~') {
				const float temp = axis.min;
				axis.min = axis.max;
				axis.max = temp;
			}
		}

		return axisMapping;
	}

	int JoyMapping::parseButtonMapping(const char* start, const char* end) const
	{
		int buttonMapping = -1;

		if (end - start <= 3 && start[0] == 'b') {
			buttonMapping = atoi(&start[1]);
		}

		return buttonMapping;
	}

	int JoyMapping::parseHatMapping(const char* start, const char* end) const
	{
		int hatMapping = -1;

		int parsedHatMapping = -1;
		if (end - start <= 4 && start[0] == 'h')
			parsedHatMapping = atoi(&start[3]);

		// `h0.0` is not considered a valid mapping
		if (parsedHatMapping > 0) {
			hatMapping = hatStateToIndex(parsedHatMapping);
		}

		return hatMapping;
	}

	int JoyMapping::hatStateToIndex(unsigned char hatState) const
	{
		int hatIndex;

		switch (hatState) {
			case 1: hatIndex = 0; break;
			case 2: hatIndex = 1; break;
			case 4: hatIndex = 2; break;
			case 8: hatIndex = 3; break;
			default: hatIndex = -1; break;
		}

		return hatIndex;
	}

	void JoyMapping::trimSpaces(const char** start, const char** end) const
	{
		while (**start == ' ' || **end == '\t') {
			(*start)++;
		}

		(*end)--;
		while (**end == ' ' || **end == '\t') {
			(*end)--;
		}
		(*end)++;
	}
}
