#include "JoyMapping.h"
#include "IInputManager.h"
#include "IInputEventHandler.h"
#include "../Base/Algorithms.h"
#include "../Primitives/Vector2.h"

#include <cstring>	// for memcpy()

#include <Containers/SmallVector.h>
#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Containers/StringView.h>
#include <IO/FileSystem.h>

#if defined(DEATH_TARGET_WINDOWS)
#	include <CommonWindows.h>
#	include <Utf8.h>
#endif

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
		"paddle4",
		"touchpad"
	};

	JoyMappedState JoyMapping::nullMappedJoyState_;
	SmallVector<JoyMappedState, JoyMapping::MaxNumJoysticks> JoyMapping::mappedJoyStates_(JoyMapping::MaxNumJoysticks);
	JoyMappedButtonEvent JoyMapping::mappedButtonEvent_;
	JoyMappedAxisEvent JoyMapping::mappedAxisEvent_;

	JoyMapping::MappingDescription::MappingDescription()
	{
		for (std::int32_t i = 0; i < MaxNumAxes; i++) {
			axes[i].name = AxisName::Unknown;
			axes[i].buttonNamePositive = ButtonName::Unknown;
			axes[i].buttonNameNegative = ButtonName::Unknown;
		}
		for (std::int32_t i = 0; i < MaxNumAxes; i++) {
			buttonAxes[i] = AxisName::Unknown;
		}
		for (std::int32_t i = 0; i < MaxNumButtons; i++) {
			buttons[i] = ButtonName::Unknown;
		}
		for (std::int32_t i = 0; i < MaxHatButtons; i++) {
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
		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			assignedMappings_[i].isValid = false;
		}
	}

	void JoyMapping::Init(const IInputManager* inputManager)
	{
		ASSERT(inputManager != nullptr);
		inputManager_ = inputManager;

		// Add mappings from the database, without searching for duplicates
		for (const char* line : ControllerMappings) {
			MappedJoystick mapping;
			const bool parsed = ParseMappingFromString(line, mapping);
			DEATH_DEBUG_ASSERT(parsed);

			if (parsed) {
				mappings_.emplace_back(std::move(mapping));
			}
		}

		LOGI("Added %u internal gamepad mappings for current platform", mappings_.size());

#if defined(DEATH_TARGET_WINDOWS)
		DWORD envLength = ::GetEnvironmentVariable(L"SDL_GAMECONTROLLERCONFIG", nullptr, 0);
		if (envLength > 0) {
			Array<wchar_t> envGameControllerConfig(NoInit, envLength);
			envLength = ::GetEnvironmentVariable(L"SDL_GAMECONTROLLERCONFIG", envGameControllerConfig, envGameControllerConfig.size());
			if (envLength > 0) {
				AddMappingsFromStringInternal(Utf8::FromUtf16(envGameControllerConfig, envLength), "SDL_GAMECONTROLLERCONFIG variable"_s);
			}
		}

#	if defined(DEATH_TRACE)
		wchar_t envAllowSteamVirtualGamepad[2] = {};
		envLength = ::GetEnvironmentVariable(L"SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", envAllowSteamVirtualGamepad, 2);
		if (envLength == 1 && envAllowSteamVirtualGamepad[0] == L'1') {
			LOGI("Steam Input detected");
		}
#	endif
#elif !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
		StringView envGameControllerConfig = ::getenv("SDL_GAMECONTROLLERCONFIG");
		if (envGameControllerConfig != nullptr) {
			AddMappingsFromStringInternal(envGameControllerConfig, "SDL_GAMECONTROLLERCONFIG variable"_s);
		}

#	if defined(DEATH_TRACE)
		StringView envAllowSteamVirtualGamepad = ::getenv("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD");
		if (envAllowSteamVirtualGamepad == "1"_s) {
			LOGI("Steam Input detected");
		}
#	endif
#endif

		CheckConnectedJoystics();
	}

	bool JoyMapping::AddMappingsFromStringInternal(StringView mappingString, StringView traceSource)
	{
		std::int32_t parsedMappings = 0;

		StringView rest = mappingString;
		while (!rest.empty()) {
			auto split = rest.partition('\n');
			rest = split[2];

			MappedJoystick newMapping;
			if (ParseMappingFromString(split[0], newMapping)) {
				std::int32_t index = FindMappingByGuid(newMapping.guid);
				// if GUID is not found then mapping has to be added, not replaced
				if (index < 0) {
					mappings_.emplace_back(std::move(newMapping));
				} else {
					mappings_[index] = std::move(newMapping);
				}
				parsedMappings++;
			}
		}

		if (!traceSource.empty()) {
			LOGI("Added %u gamepad mappings from %s", parsedMappings, String::nullTerminatedView(traceSource).data());
		} else {
			LOGI("Added %u gamepad mappings", parsedMappings);
		}

		return (parsedMappings > 0);
	}

	bool JoyMapping::AddMappingsFromString(StringView mappingString)
	{
		bool mappingsAdded = AddMappingsFromStringInternal(mappingString, {});

		if (mappingsAdded) {
			CheckConnectedJoystics();
		}

		return mappingsAdded;
	}

	bool JoyMapping::AddMappingsFromFile(StringView path)
	{
		std::unique_ptr<Stream> fileHandle = fs::Open(path, FileAccess::Read);
		std::int64_t fileSize = fileHandle->GetSize();
		if (fileSize == 0 || fileSize > 32 * 1024 * 1024) {
			return false;
		}

		std::int32_t parsedMappings = 0;

		std::unique_ptr<char[]> fileBuffer = std::make_unique<char[]>(fileSize + 1);
		fileHandle->Read(fileBuffer.get(), fileSize);
		fileHandle.reset(nullptr);
		fileBuffer[fileSize] = '\0';

		bool mappingsAdded = AddMappingsFromStringInternal(fileBuffer.get(), String("file \""_s + path + "\""_s));

		fileBuffer = nullptr;

		if (mappingsAdded) {
			CheckConnectedJoystics();
		}

		return mappingsAdded;
	}

	void JoyMapping::OnJoyButtonPressed(const JoyButtonEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Button pressed - joyId: %d, buttonId: %d", event.joyId, event.buttonId);
#endif

		if (inputEventHandler_ == nullptr || event.joyId >= MaxNumJoysticks) {
			return;
		}

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.buttonId >= 0 && event.buttonId < static_cast<std::int32_t>(MappingDescription::MaxNumButtons));
		if (mappingIsValid) {
			if (mapping.desc.buttons[event.buttonId] != ButtonName::Unknown) {
				mappedButtonEvent_.joyId = event.joyId;
				mappedButtonEvent_.buttonName = mapping.desc.buttons[event.buttonId];
				const std::int32_t buttonId = static_cast<std::int32_t>(mappedButtonEvent_.buttonName);
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
					LOGI("Button press mapped as axis %d", static_cast<std::int32_t>(axisName));
#endif
					mappedJoyStates_[event.joyId].axesValues_[static_cast<std::int32_t>(axisName)] = mappedAxisEvent_.value;
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

	void JoyMapping::OnJoyButtonReleased(const JoyButtonEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Button released - joyId: %d, buttonId: %d", event.joyId, event.buttonId);
#endif

		if (inputEventHandler_ == nullptr || event.joyId >= MaxNumJoysticks) {
			return;
		}

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.buttonId >= 0 && event.buttonId < static_cast<std::int32_t>(MappingDescription::MaxNumButtons));
		if (mappingIsValid) {
			// Standard button
			if (mapping.desc.buttons[event.buttonId] != ButtonName::Unknown) {
				mappedButtonEvent_.joyId = event.joyId;
				mappedButtonEvent_.buttonName = mapping.desc.buttons[event.buttonId];
				const std::int32_t buttonId = static_cast<std::int32_t>(mappedButtonEvent_.buttonName);
#if defined(NCINE_INPUT_DEBUGGING)
				LOGI("Button release mapped as button %d", buttonId);
#endif
				mappedJoyStates_[event.joyId].buttons_[buttonId] = false;
				inputEventHandler_->OnJoyMappedButtonReleased(mappedButtonEvent_);
			} else {
				// Button mapped as axis
				const AxisName axisName = mapping.desc.buttonAxes[event.buttonId];
				if (axisName != AxisName::Unknown) {
					mappedAxisEvent_.joyId = event.joyId;
					mappedAxisEvent_.axisName = axisName;
					mappedAxisEvent_.value = 0.0f;
#if defined(NCINE_INPUT_DEBUGGING)
					LOGI("Button release mapped as axis %d", static_cast<std::int32_t>(axisName));
#endif
					mappedJoyStates_[event.joyId].axesValues_[static_cast<std::int32_t>(axisName)] = mappedAxisEvent_.value;
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

	void JoyMapping::OnJoyHatMoved(const JoyHatEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Hat moved - joyId: %d, hatId: %d, hatState: 0x%02x", event.joyId, event.hatId, event.hatState);
#endif

		if (inputEventHandler_ == nullptr || event.joyId >= MaxNumJoysticks) {
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
					std::int32_t hatIndex = HatStateToIndex(hatValue);
					mappedButtonEvent_.buttonName = mapping.desc.hats[hatIndex];
					if (mappedButtonEvent_.buttonName != ButtonName::Unknown) {
						const std::int32_t buttonId = static_cast<std::int32_t>(mappedButtonEvent_.buttonName);
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

	void JoyMapping::OnJoyAxisMoved(const JoyAxisEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Axis moved - joyId: %d, axisId: %d, value: %f", event.joyId, event.axisId, event.value);
#endif

		if (inputEventHandler_ == nullptr || event.joyId >= MaxNumJoysticks) {
			return;
		}

		const auto& mapping = assignedMappings_[event.joyId];
		const bool mappingIsValid = (mapping.isValid && event.axisId >= 0 && event.axisId < static_cast<int>(MappingDescription::MaxNumAxes));
		if (mappingIsValid) {
			const auto& axis = mapping.desc.axes[event.axisId];

			// Standard axis
			if (axis.name != AxisName::Unknown) {
				mappedAxisEvent_.joyId = event.joyId;
				mappedAxisEvent_.axisName = axis.name;
				const float value = (event.value + 1.0f) * 0.5f;
				mappedAxisEvent_.value = axis.min + value * (axis.max - axis.min);
#if defined(NCINE_INPUT_DEBUGGING)
				LOGI("Axis move mapped as axis %d (value: %f, normalized: %f, min: %f, max: %f)", static_cast<int>(axis.name), mappedAxisEvent_.value, value, axis.min, axis.max);
#endif
				mappedJoyStates_[event.joyId].axesValues_[static_cast<int>(axis.name)] = mappedAxisEvent_.value;
				inputEventHandler_->OnJoyMappedAxisMoved(mappedAxisEvent_);
			}

			// Axis mapped as button
			if (axis.buttonNamePositive != ButtonName::Unknown) {
				mappedButtonEvent_.joyId = event.joyId;
				mappedButtonEvent_.buttonName = axis.buttonNamePositive;
				const std::int32_t buttonId = static_cast<std::int32_t>(mappedButtonEvent_.buttonName);
				bool newState = (event.value >= IInputManager::AnalogButtonDeadZone);
				bool prevState = mappedJoyStates_[event.joyId].buttons_[buttonId];
				if (newState != prevState) {
					mappedJoyStates_[event.joyId].buttons_[buttonId] = newState;
					if (newState) {
#if defined(NCINE_INPUT_DEBUGGING)
						LOGI("Axis positive move mapped as button press %d", buttonId);
#endif
						inputEventHandler_->OnJoyMappedButtonPressed(mappedButtonEvent_);
					} else {
#if defined(NCINE_INPUT_DEBUGGING)
						LOGI("Axis positive move mapped as button release %d", buttonId);
#endif
						inputEventHandler_->OnJoyMappedButtonReleased(mappedButtonEvent_);
					}
				}
			}
			if (axis.buttonNameNegative != ButtonName::Unknown) {
				mappedButtonEvent_.joyId = event.joyId;
				mappedButtonEvent_.buttonName = axis.buttonNameNegative;
				const std::int32_t buttonId = static_cast<std::int32_t>(mappedButtonEvent_.buttonName);
				bool newState = (event.value <= -IInputManager::AnalogButtonDeadZone);
				bool prevState = mappedJoyStates_[event.joyId].buttons_[buttonId];
				if (newState != prevState) {
					mappedJoyStates_[event.joyId].buttons_[buttonId] = newState;
					if (newState) {
#if defined(NCINE_INPUT_DEBUGGING)
						LOGI("Axis negative move mapped as button press %d", buttonId);
#endif
						inputEventHandler_->OnJoyMappedButtonPressed(mappedButtonEvent_);
					} else {
#if defined(NCINE_INPUT_DEBUGGING)
						LOGI("Axis negative move mapped as button release %d", buttonId);
#endif
						inputEventHandler_->OnJoyMappedButtonReleased(mappedButtonEvent_);
					}
				}
			}

#if defined(NCINE_INPUT_DEBUGGING)
			if (mappedAxisEvent_.axisName == AxisName::Unknown && axis.buttonNamePositive == ButtonName::Unknown) {
				LOGW("Axis move has incorrect mapping");
			}
#endif
		} else {
#if defined(NCINE_INPUT_DEBUGGING)
			LOGW("Axis move has no mapping");
#endif
		}
	}

	bool JoyMapping::OnJoyConnected(const JoyConnectionEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Gamepad connected - joyId: %d", event.joyId);
#endif

		if (event.joyId >= MaxNumJoysticks) {
			LOGW("Maximum number of gamepads reached, skipping newly connected (%i)", event.joyId);
			return false;
		}

		const char* joyName = inputManager_->joyName(event.joyId);
		const JoystickGuid joyGuid = inputManager_->joyGuid(event.joyId);

		auto& mapping = assignedMappings_[event.joyId];
		mapping.isValid = false;

		if (joyGuid.isValid()) {
			const std::int32_t index = FindMappingByGuid(joyGuid);
			if (index != -1) {
				mapping.isValid = true;
				mapping.desc = mappings_[index].desc;

				const std::uint8_t* g = joyGuid.data;
				LOGI("Gamepad mapping found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d), also known as \"%s\"", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId, mappings_[index].name);
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

			const std::uint8_t * g = joyGuid.data;
			LOGI("Gamepad mapping not found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d), using Android default mapping", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId);

			mapping.isValid = true;

			for (std::int32_t i = 0; i < static_cast<std::int32_t>(arraySize(AndroidAxisNameMapping)); i++) {
				mapping.desc.axes[i].name = AndroidAxisNameMapping[i];
				if (mapping.desc.axes[i].name == AxisName::LeftTrigger || mapping.desc.axes[i].name == AxisName::RightTrigger) {
					mapping.desc.axes[i].min = 0.0f;
				} else {
					mapping.desc.axes[i].min = -1.0f;
				}
				mapping.desc.axes[i].max = 1.0f;
			}

			constexpr std::int32_t AndroidButtonCount = (std::int32_t)ButtonName::Misc1;
			for (std::int32_t i = 0; i < AndroidButtonCount; i++) {
				mapping.desc.buttons[i] = (ButtonName)i;
			}
			for (std::int32_t i = AndroidButtonCount; i < static_cast<std::int32_t>(arraySize(mapping.desc.buttons)); i++) {
				mapping.desc.buttons[i] = ButtonName::Unknown;
			}

			for (std::int32_t i = 0; i < static_cast<std::int32_t>(arraySize(AndroidDpadButtonNameMapping)); i++) {
				mapping.desc.hats[i] = AndroidDpadButtonNameMapping[i];
			}
		}
#elif !defined(DEATH_TARGET_EMSCRIPTEN)
		if (!mapping.isValid) {
			const std::int32_t index = FindMappingByName(joyName);
			if (index != -1) {
				mapping.isValid = true;
				mapping.desc = mappings_[index].desc;

				const std::uint8_t* g = joyGuid.data;
				LOGI("Gamepad mapping found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d)", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId);
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
			const std::int32_t index = FindMappingByGuid(JoystickGuidType::Xinput);
			if (index != -1) {
				mapping.isValid = true;
				mapping.desc = mappings_[index].desc;

				const uint8_t* g = joyGuid.data;
				LOGI("Gamepad mapping not found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d), using XInput mapping", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId);
			}

			if (!mapping.isValid) {
				const uint8_t* g = joyGuid.data;
				LOGI("Gamepad mapping not found for \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] (%d), please provide correct mapping in \"gamecontrollerdb.txt\" file, otherwise the joystick will not work properly", joyName, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15], event.joyId);
			}
		}
#endif

		return mapping.isValid;
	}

	void JoyMapping::OnJoyDisconnected(const JoyConnectionEvent& event)
	{
#if defined(NCINE_INPUT_DEBUGGING)
		LOGI("Gamepad disconnected - joyId: %d", event.joyId);
#endif

		if (event.joyId >= MaxNumJoysticks) {
			return;
		}

#if defined(WITH_SDL)
		// Compacting the array of mapping indices
		for (std::int32_t i = event.joyId; i < MaxNumJoysticks - 1; i++) {
			assignedMappings_[i] = assignedMappings_[i + 1];
		}
		assignedMappings_[MaxNumJoysticks - 1].isValid = false;
#else
		assignedMappings_[event.joyId].isValid = false;
#endif
	}

	bool JoyMapping::IsJoyMapped(std::int32_t joyId) const
	{
		return (joyId >= 0 && joyId < MaxNumJoysticks && assignedMappings_[joyId].isValid);
	}

	const JoyMappedState& JoyMapping::GetMappedState(std::int32_t joyId) const
	{
		if (joyId < 0 || joyId >= MaxNumJoysticks) {
			return nullMappedJoyState_;
		} else {
			return mappedJoyStates_[joyId];
		}
	}

	void JoyMapping::DeadZoneNormalize(Vector2f& joyVector, float deadZoneValue) const
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

	JoystickGuid JoyMapping::CreateJoystickGuid(std::uint16_t bus, std::uint16_t vendor, std::uint16_t product, std::uint16_t version, const StringView& name, std::uint8_t driverSignature, std::uint8_t driverData)
	{
		JoystickGuid guid;
		std::uint16_t* guid16 = reinterpret_cast<std::uint16_t*>(guid.data);

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
			std::size_t availableSpace = sizeof(guid.data) - 4;

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

	void JoyMapping::CheckConnectedJoystics()
	{
		if (inputManager_ == nullptr) {
			return;
		}

		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			if (inputManager_->isJoyPresent(i)) {
				JoyConnectionEvent event;
				event.joyId = i;
				OnJoyConnected(event);
			}
		}
	}

	std::int32_t JoyMapping::FindMappingByGuid(const JoystickGuid& guid) const
	{
		std::int32_t index = -1;

		std::int32_t size = mappings_.size();
		for (std::int32_t i = 0; i < size; i++) {
			if (mappings_[i].guid == guid) {
				index = static_cast<std::int32_t>(i);
				break;
			}
		}

		return index;
	}

	std::int32_t JoyMapping::FindMappingByName(const char* name) const
	{
		std::int32_t index = -1;

		std::int32_t size = mappings_.size();
		for (std::int32_t i = 0; i < size; i++) {
			if (strncmp(mappings_[i].name, name, MaxNameLength) == 0) {
				index = static_cast<std::int32_t>(i);
				break;
			}
		}

		return index;
	}

	bool JoyMapping::ParseMappingFromString(StringView mappingString, MappedJoystick& map)
	{
		if (mappingString.empty() || mappingString[0] == '#') {
			return false;
		}

		auto sub = mappingString.partition(',');
		sub[0] = sub[0].trimmed();
		if (sub[0].empty()) {
			// Ignore malformed string comming from Steam Input (2024/08/13)
			if (!mappingString.hasPrefix(",platform:"_s)) {
				LOGE("Invalid mapping string \"%s\"", String::nullTerminatedView(mappingString).data());
			}
			return false;
		}

		map.guid.fromString(sub[0]);

		sub = sub[2].partition(',');
		sub[0] = sub[0].trimmed();
		if (sub[0].empty()) {
			LOGE("Invalid mapping string \"%s\"", String::nullTerminatedView(mappingString).data());
			return false;
		}

		copyStringFirst(map.name, sub[0]);

		while (!sub[2].empty()) {
			sub = sub[2].partition(',');

			auto keyValue = sub[0].partition(':');
			keyValue[0] = keyValue[0].trimmed();
			keyValue[2] = keyValue[2].trimmed();
			if (keyValue[0].empty()) {
				LOGE("Invalid mapping string \"%s\"", String::nullTerminatedView(mappingString).data());
				return false;
			}

			if (keyValue[0] == "platform"_s) {
				// Platform name
				if (!ParsePlatformName(keyValue[2])) {
					return false;
				}
			} else if (keyValue[0] != "crc"_s && keyValue[0] != "hint"_s) {
				// Axis
				const std::int32_t axisIndex = ParseAxisName(keyValue[0]);
				if (axisIndex != -1) {
					MappingDescription::Axis axis;
					axis.name = static_cast<AxisName>(axisIndex);
					const std::int32_t axisMapping = ParseAxisMapping(keyValue[2], axis);
					if (axisMapping != -1 && axisMapping < MappingDescription::MaxNumAxes) {
						map.desc.axes[axisMapping].name = axis.name;
						map.desc.axes[axisMapping].min = axis.min;
						map.desc.axes[axisMapping].max = axis.max;
					} else {
						// The same parsing method for buttons will be used for button axes
						const std::int32_t buttonAxisMapping = ParseButtonMapping(keyValue[2]);
						if (buttonAxisMapping != -1 && buttonAxisMapping < MappingDescription::MaxNumAxes) {
							map.desc.buttonAxes[buttonAxisMapping] = static_cast<AxisName>(axisIndex);
						} else if (!keyValue[2].empty()) {
							// It's empty sometimes
							const uint8_t* g = map.guid.data;
							LOGI("Unsupported assignment in mapping source \"%s\" in \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x]", String::nullTerminatedView(keyValue[0]).data(), map.name, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
						}
					}
				} else {
					// Button
					const std::int32_t buttonIndex = ParseButtonName(keyValue[0]);
					if (buttonIndex != -1) {
						const std::int32_t buttonMapping = ParseButtonMapping(keyValue[2]);
						if (buttonMapping != -1 && buttonMapping < MappingDescription::MaxNumButtons) {
							map.desc.buttons[buttonMapping] = static_cast<ButtonName>(buttonIndex);
						} else {
							const std::int32_t hatMapping = ParseHatMapping(keyValue[2]);
							if (hatMapping != -1 && hatMapping < MappingDescription::MaxHatButtons) {
								map.desc.hats[hatMapping] = static_cast<ButtonName>(buttonIndex);
							} else {
								MappingDescription::Axis axis;
								const std::int32_t axisMapping = ParseAxisMapping(keyValue[2], axis);
								if (axisMapping != -1 && axisMapping < MappingDescription::MaxNumAxes) {
									if (axis.max > 0.0f) {
										map.desc.axes[axisMapping].buttonNamePositive = static_cast<ButtonName>(buttonIndex);
									} else if (axis.max < 0.0f) {
										map.desc.axes[axisMapping].buttonNameNegative = static_cast<ButtonName>(buttonIndex);
									} else {
										const uint8_t* g = map.guid.data;
										LOGI("Unsupported axis value \"%s\" for button mapping in \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x]", String::nullTerminatedView(keyValue[2]).data(), map.name, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
									}
								} else if (!keyValue[2].empty()) { // It's empty sometimes
									const uint8_t* g = map.guid.data;
									LOGI("Unsupported assignment in mapping source \"%s\" in \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x]", String::nullTerminatedView(keyValue[0]).data(), map.name, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
								}
							}
						}
					} else {
						// Unknown key
						const uint8_t* g = map.guid.data;
						LOGD("Unsupported mapping source \"%s\" in \"%s\" [%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x]", String::nullTerminatedView(keyValue[0]).data(), map.name, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
					}
				}
			}
		}

		return true;
	}

	bool JoyMapping::ParsePlatformName(StringView value) const
	{
#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WINDOWS_RT)
		return false;
#else
#	if defined(DEATH_TARGET_WINDOWS)
		return (value == "Windows"_s);
#	elif defined(DEATH_TARGET_ANDROID)
		return (value == "Android"_s);
#	elif defined(DEATH_TARGET_IOS)
		return (value == "iOS"_s);
#	elif defined(DEATH_TARGET_APPLE)
		return (value == "Mac OS X"_s);
#	else
		return (value == "Linux"_s);
#	endif
#endif
	}

	std::int32_t JoyMapping::ParseAxisName(StringView value) const
	{
		std::int32_t axisIndex = -1;

		for (std::int32_t i = 0; i < static_cast<std::int32_t>(arraySize(AxesStrings)); i++) {
			if (value == AxesStrings[i]) {
				axisIndex = i;
				break;
			}
		}

		return axisIndex;
	}

	std::int32_t JoyMapping::ParseButtonName(StringView value) const
	{
		std::int32_t buttonIndex = -1;

		for (std::int32_t i = 0; i < static_cast<std::int32_t>(arraySize(ButtonsStrings)); i++) {
			if (value == ButtonsStrings[i]) {
				buttonIndex = i;
				break;
			}
		}

		return buttonIndex;
	}

	std::int32_t JoyMapping::ParseAxisMapping(StringView value, MappingDescription::Axis& axis) const
	{
		std::int32_t axisMapping = -1;

		axis.max = 1.0f;
		axis.min = -1.0f;

		if (!value.empty() && value.size() <= 5 && (value[0] == 'a' || value[1] == 'a')) {
			const char* digits = &value[1];

			if (axis.name == AxisName::LeftTrigger || axis.name == AxisName::RightTrigger) {
				axis.min = 0.0f;
				axis.max = 1.0f;
			}

			if (value[0] == '+') {
				axis.min = 0.0f;
				axis.max = 1.0f;
				digits++;
			} else if (value[0] == '-') {
				axis.min = 0.0f;
				axis.max = -1.0f;
				digits++;
			}

			axisMapping = stou32(digits, value.size() - (digits - value.begin()));

			if (value[value.size() - 1] == '~') {
				const float temp = axis.min;
				axis.min = axis.max;
				axis.max = temp;
			}
		}

		return axisMapping;
	}

	std::int32_t JoyMapping::ParseButtonMapping(StringView value) const
	{
		std::int32_t buttonMapping = -1;

		if (!value.empty() && value.size() <= 3 && value[0] == 'b') {
			buttonMapping = stou32(value.begin() + 1, value.size() - 1);
		}

		return buttonMapping;
	}

	std::int32_t JoyMapping::ParseHatMapping(StringView value) const
	{
		std::int32_t hatMapping = -1;

		std::int32_t parsedHatMapping = -1;
		if (!value.empty() && value.size() <= 4 && value[0] == 'h') {
			parsedHatMapping = stou32(value.begin() + 3, value.size() - 3);
		}

		// `h0.0` is not considered a valid mapping
		if (parsedHatMapping > 0) {
			hatMapping = HatStateToIndex(parsedHatMapping);
		}

		return hatMapping;
	}

	std::int32_t JoyMapping::HatStateToIndex(std::int32_t hatState) const
	{
		std::int32_t hatIndex;

		switch (hatState) {
			case 1: hatIndex = 0; break;
			case 2: hatIndex = 1; break;
			case 4: hatIndex = 2; break;
			case 8: hatIndex = 3; break;
			default: hatIndex = -1; break;
		}

		return hatIndex;
	}
}
