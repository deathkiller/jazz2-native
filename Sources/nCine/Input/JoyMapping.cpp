#include "JoyMapping.h"
#include "IInputManager.h"
#include <cstring> // for memcpy()
#include <cstdlib> // for strtoul()
#include "IInputEventHandler.h"
#include "../IO/FileSystem.h"
#include "../Primitives/Vector2.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	namespace
	{
#include "JoyMappingDb.h"

#if defined(DEATH_TARGET_ANDROID)
		constexpr AxisName AndroidAxisNameMapping[] = {
			AxisName::LX,
			AxisName::LY,
			AxisName::RX,
			AxisName::RY,
			AxisName::LTRIGGER,
			AxisName::RTRIGGER
		};

		constexpr ButtonName AndroidDpadButtonNameMapping[] = {
			ButtonName::DPAD_UP,
			ButtonName::DPAD_RIGHT,
			ButtonName::DPAD_DOWN,
			ButtonName::DPAD_LEFT
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

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

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
		/*"misc1",
		"paddle1",
		"paddle2",
		"paddle3",
		"paddle4"*/
	};

	JoyMappedStateImpl JoyMapping::nullMappedJoyState_;
	SmallVector<JoyMappedStateImpl, JoyMapping::MaxNumJoysticks> JoyMapping::mappedJoyStates_(JoyMapping::MaxNumJoysticks);
	JoyMappedButtonEvent JoyMapping::mappedButtonEvent_;
	JoyMappedAxisEvent JoyMapping::mappedAxisEvent_;

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS AND DESTRUCTOR
	///////////////////////////////////////////////////////////

	JoyMapping::MappingDescription::MappingDescription()
	{
		for (unsigned int i = 0; i < MaxNumAxes; i++)
			axes[i].name = AxisName::UNKNOWN;
		for (unsigned int i = 0; i < MaxNumAxes; i++)
			buttonAxes[i] = AxisName::UNKNOWN;
		for (unsigned int i = 0; i < MaxNumButtons; i++)
			buttons[i] = ButtonName::UNKNOWN;
		for (unsigned int i = 0; i < MaxHatButtons; i++)
			hats[i] = ButtonName::UNKNOWN;
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

		unsigned int numStrings = 0;

		// Add mappings from the database, without searching for duplicates
		const char** mappingStrings = ControllerMappings;
		while (*mappingStrings) {
			numStrings++;
			MappedJoystick mapping;
			const bool parsed = parseMappingFromString(*mappingStrings, mapping);
			if (parsed) {
				mappings_.push_back(mapping);
			}
			mappingStrings++;
		}

		LOGI_X("Parsed %u strings for %u mappings", numStrings, mappings_.size());
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void JoyMapping::init(const IInputManager* inputManager)
	{
		ASSERT(inputManager);
		inputManager_ = inputManager;
		checkConnectedJoystics();
	}

	bool JoyMapping::addMappingFromString(const char* mappingString)
	{
		ASSERT(mappingString);

		MappedJoystick newMapping;
		const bool parsed = parseMappingFromString(mappingString, newMapping);
		if (parsed) {
			int index = findMappingByGuid(newMapping.guid);
			// if GUID is not found then mapping has to be added, not replaced
			if (index < 0) {
				index = mappings_.size();
			}
			mappings_[index] = newMapping;
		}
		checkConnectedJoystics();

		return parsed;
	}

	void JoyMapping::addMappingsFromStrings(const char** mappingStrings)
	{
		ASSERT(mappingStrings);

		while (*mappingStrings) {
			MappedJoystick newMapping;
			const bool parsed = parseMappingFromString(*mappingStrings, newMapping);
			if (parsed) {
				int index = findMappingByGuid(newMapping.guid);
				// if GUID is not found then mapping has to be added, not replaced
				if (index < 0)
					index = mappings_.size();
				mappings_[index] = newMapping;
			}
			mappingStrings++;
		}

		checkConnectedJoystics();
	}

	void JoyMapping::addMappingsFromFile(const StringView& filename)
	{
		std::unique_ptr<IFileStream> fileHandle = fs::Open(filename, FileAccessMode::Read);
		const long int fileSize = fileHandle->GetSize();
		if (fileSize == 0) {
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
					mappings_.push_back(newMapping);
				} else {
					mappings_[index] = newMapping;
				}
			}

		} while (strchr(buffer, '\n') && (buffer = strchr(buffer, '\n') + 1) < fileBuffer.get() + fileSize);

		LOGI_X("Joystick mapping file parsed: %u mappings in %u lines", numParsed, fileLine);

		fileBuffer.reset(nullptr);

		checkConnectedJoystics();
	}

	void JoyMapping::onJoyButtonPressed(const JoyButtonEvent& event)
	{
		if (inputEventHandler_ == nullptr)
			return;

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.buttonId >= 0 && event.buttonId < static_cast<int>(MappingDescription::MaxNumButtons));
		if (mappingIsValid) {
			mappedButtonEvent_.joyId = event.joyId;
			mappedButtonEvent_.buttonName = mapping.desc.buttons[event.buttonId];
			if (mappedButtonEvent_.buttonName != ButtonName::UNKNOWN) {
				const int buttonId = static_cast<int>(mappedButtonEvent_.buttonName);
				mappedJoyStates_[event.joyId].buttons_[buttonId] = true;
				inputEventHandler_->onJoyMappedButtonPressed(mappedButtonEvent_);
			} else {
				// Check if the button is mapped as an axis
				const AxisName axisName = mapping.desc.buttonAxes[event.buttonId];
				if (axisName != AxisName::UNKNOWN) {
					mappedAxisEvent_.joyId = event.joyId;
					mappedAxisEvent_.axisName = axisName;
					mappedAxisEvent_.value = 1.0f;

					mappedJoyStates_[event.joyId].axesValues_[static_cast<int>(axisName)] = mappedAxisEvent_.value;
					inputEventHandler_->onJoyMappedAxisMoved(mappedAxisEvent_);
				}
			}
		}
	}

	void JoyMapping::onJoyButtonReleased(const JoyButtonEvent& event)
	{
		if (inputEventHandler_ == nullptr)
			return;

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.buttonId >= 0 && event.buttonId < static_cast<int>(MappingDescription::MaxNumButtons));
		if (mappingIsValid) {
			mappedButtonEvent_.joyId = event.joyId;
			mappedButtonEvent_.buttonName = mapping.desc.buttons[event.buttonId];
			if (mappedButtonEvent_.buttonName != ButtonName::UNKNOWN) {
				const int buttonId = static_cast<int>(mappedButtonEvent_.buttonName);
				mappedJoyStates_[event.joyId].buttons_[buttonId] = false;
				inputEventHandler_->onJoyMappedButtonReleased(mappedButtonEvent_);
			} else {
				// Check if the button is mapped as an axis
				const AxisName axisName = mapping.desc.buttonAxes[event.buttonId];
				if (axisName != AxisName::UNKNOWN) {
					mappedAxisEvent_.joyId = event.joyId;
					mappedAxisEvent_.axisName = axisName;
					mappedAxisEvent_.value = 0.0f;

					mappedJoyStates_[event.joyId].axesValues_[static_cast<int>(axisName)] = mappedAxisEvent_.value;
					inputEventHandler_->onJoyMappedAxisMoved(mappedAxisEvent_);
				}
			}
		}
	}

	void JoyMapping::onJoyHatMoved(const JoyHatEvent& event)
	{
		if (inputEventHandler_ == nullptr)
			return;

		const auto& mapping = assignedMappings_[event.joyId];
		// Only the first gamepad hat is mapped
		const bool mappingIsValid = (mapping.isValid && event.hatId == 0 && mappedJoyStates_[event.joyId].lastHatState_ != event.hatState);
		if (mappingIsValid) {
			mappedButtonEvent_.joyId = event.joyId;

			const unsigned char oldHatState = mappedJoyStates_[event.joyId].lastHatState_;
			const unsigned char newHatState = event.hatState;

			const unsigned char firstHatValue = HatState::UP;
			const unsigned char lastHatValue = HatState::LEFT;
			for (unsigned char hatValue = firstHatValue; hatValue <= lastHatValue; hatValue *= 2) {
				if ((oldHatState & hatValue) != (newHatState & hatValue)) {
					int hatIndex = hatStateToIndex(hatValue);

					mappedButtonEvent_.buttonName = mapping.desc.hats[hatIndex];
					if (mappedButtonEvent_.buttonName != ButtonName::UNKNOWN) {
						const int buttonId = static_cast<int>(mappedButtonEvent_.buttonName);
						if (newHatState & hatValue) {
							mappedJoyStates_[event.joyId].buttons_[buttonId] = true;
							inputEventHandler_->onJoyMappedButtonPressed(mappedButtonEvent_);
						} else {
							mappedJoyStates_[event.joyId].buttons_[buttonId] = false;
							inputEventHandler_->onJoyMappedButtonReleased(mappedButtonEvent_);
						}
					}
				}
				mappedJoyStates_[event.joyId].lastHatState_ = event.hatState;
			}
		}
	}

	void JoyMapping::onJoyAxisMoved(const JoyAxisEvent& event)
	{
		if (inputEventHandler_ == nullptr)
			return;

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.axisId >= 0 && event.axisId < static_cast<int>(MappingDescription::MaxNumAxes));
		if (mappingIsValid) {
			const auto& axis = mapping.desc.axes[event.axisId];

			mappedAxisEvent_.joyId = event.joyId;
			mappedAxisEvent_.axisName = axis.name;
			if (mappedAxisEvent_.axisName != AxisName::UNKNOWN) {
				const float value = (event.normValue + 1.0f) * 0.5f;
				mappedAxisEvent_.value = axis.min + value * (axis.max - axis.min);
				mappedJoyStates_[event.joyId].axesValues_[static_cast<int>(axis.name)] = mappedAxisEvent_.value;
				inputEventHandler_->onJoyMappedAxisMoved(mappedAxisEvent_);

				// Map some axes also as button presses
				/*ButtonName buttonName;
				switch (mappedAxisEvent_.axisName) {
					case AxisName::LTRIGGER: buttonName = ButtonName::LTRIGGER; break;
					case AxisName::RTRIGGER: buttonName = ButtonName::RTRIGGER; break;
					default: buttonName = ButtonName::UNKNOWN; break;
				}
				if (buttonName != ButtonName::UNKNOWN) {
					bool isPressed = (mappedAxisEvent_.value > 0.5f);
					const int buttonId = static_cast<int>(buttonName);
					if (mappedJoyStates_[event.joyId].buttons_[buttonId] != isPressed) {
						mappedButtonEvent_.joyId = event.joyId;
						mappedButtonEvent_.buttonName = buttonName;
						mappedJoyStates_[event.joyId].buttons_[buttonId] = isPressed;
						if (isPressed) {
							inputEventHandler_->onJoyMappedButtonPressed(mappedButtonEvent_);
						} else {
							inputEventHandler_->onJoyMappedButtonReleased(mappedButtonEvent_);
						}
					}
				}*/
			}
		}
	}

	bool JoyMapping::onJoyConnected(const JoyConnectionEvent& event)
	{
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
				LOGI_X("Joystick mapping found for \"%s\" with GUID \"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\" also known as \"%s\" (%d)", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], mappings_[index].name, event.joyId);
			}
		}

		if (!mapping.isValid) {
			const int index = findMappingByName(joyName);
			if (index != -1) {
				mapping.isValid = true;
				mapping.desc = mappings_[index].desc;
				LOGI_X("Joystick mapping found for \"%s\" (%d)", joyName, event.joyId);
			}
		}

#if defined(DEATH_TARGET_ANDROID)
		if (!mapping.isValid) {
			LOGI_X("Joystick mapping not found for \"%s\" (%d), using Android default mapping", joyName, event.joyId);
			mapping.isValid = true;

			for (int i = 0; i < _countof(AndroidAxisNameMapping); i++) {
				mapping.desc.axes[i].name = AndroidAxisNameMapping[i];
				mapping.desc.axes[i].min = -1.0f;
				mapping.desc.axes[i].max = 1.0f;
			}
			
			constexpr int AndroidButtonCount = (int)ButtonName::/*MISC1*/COUNT;
			for (int i = 0; i < AndroidButtonCount; i++) {
				mapping.desc.buttons[i] = (ButtonName)i;
			}
			for (int i = AndroidButtonCount; i < _countof(mapping.desc.buttons); i++) {
				mapping.desc.buttons[i] = ButtonName::UNKNOWN;
			}

			for (int i = 0; i < _countof(AndroidDpadButtonNameMapping); i++) {
				mapping.desc.hats[i] = AndroidDpadButtonNameMapping[i];
			}
		}
#endif

		if (!mapping.isValid) {
			const int index = findMappingByGuid(JoystickGuidType::Xinput);
			if (index != -1) {
				mapping.isValid = true;
				mapping.desc = mappings_[index].desc;
				LOGI_X("Joystick mapping not found for \"%s\" (%d), using XInput", joyName, event.joyId);
			}
		}

		return mapping.isValid;
	}

	void JoyMapping::onJoyDisconnected(const JoyConnectionEvent& event)
	{
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

	const JoyMappedStateImpl& JoyMapping::joyMappedState(int joyId) const
	{
		if (joyId < 0 || joyId > MaxNumJoysticks) {
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

		if (vendor && product) {
			*guid16++ = vendor;
			*guid16++ = 0;
			*guid16++ = product;
			*guid16++ = 0;
			*guid16++ = version;
			guid.data[14] = driverSignature;
			guid.data[15] = driverData;
		} else {
			size_t availableSpace = sizeof(guid.data) - 4;

			if (driverSignature) {
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

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void JoyMapping::checkConnectedJoystics()
	{
		for (int i = 0; i < MaxNumJoysticks; i++) {
			if (inputManager_->isJoyPresent(i)) {
				JoyConnectionEvent event;
				event.joyId = i;
				onJoyConnected(event);
			}
		}
	}

	int JoyMapping::findMappingByGuid(const JoystickGuid& guid)
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

	int JoyMapping::findMappingByName(const char* name)
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
					if (axisMapping != -1 && axisMapping < MappingDescription::MaxNumAxes)
						map.desc.axes[axisMapping] = axis;
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
#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_WINDOWS_RT)
		return false;
#else
#	if defined(DEATH_TARGET_WINDOWS)
		constexpr char platformName[] = "Windows";
#	elif defined(DEATH_TARGET_ANDROID)
		constexpr char platformName[] = "Android";
#	elif defined(DEATH_TARGET_IOS)
		constexpr char platformName[] = "iOS";
#	elif defined(DEATH_TARGET_APPLE)
		constexpr char platformName[] = "Mac OS X";
#	else
		constexpr char platformName[] = "Linux";
#	endif

		return (strncmp(start, platformName, end - start) == 0);
#endif
	}

	int JoyMapping::parseAxisName(const char* start, const char* end) const
	{
		int axisIndex = -1;

		for (unsigned int i = 0; i < _countof(AxesStrings); i++) {
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

		for (unsigned int i = 0; i < _countof(ButtonsStrings); i++) {
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

			if (axis.name == AxisName::LTRIGGER || axis.name == AxisName::RTRIGGER) {
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

		if (end - start <= 3 && start[0] == 'b')
			buttonMapping = atoi(&start[1]);

		return buttonMapping;
	}

	int JoyMapping::parseHatMapping(const char* start, const char* end) const
	{
		int hatMapping = -1;

		int parsedHatMapping = -1;
		if (end - start <= 4 && start[0] == 'h')
			parsedHatMapping = atoi(&start[3]);

		// `h0.0` is not considered a valid mapping
		if (parsedHatMapping > 0)
			hatMapping = hatStateToIndex(parsedHatMapping);

		return hatMapping;
	}

	int JoyMapping::hatStateToIndex(unsigned char hatState) const
	{
		int hatIndex = -1;

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
		while (**start == ' ') {
			(*start)++;
		}

		(*end)--;
		while (**end == ' ') {
			(*end)--;
		}
		(*end)++;
	}

}
