#pragma once

#include "IInputManager.h"
#include "../Primitives/Vector2.h"

#include <Containers/SmallVector.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class IInputManager;
	class JoyButtonEvent;
	class JoyAxisEvent;
	class JoyConnectionEvent;
	class IInputEventHandler;

	/// Provides translation layer for gamepads and joysticks to unified layout
	class JoyMapping
	{
	public:
		static const std::int32_t MaxNumJoysticks = 6;

		JoyMapping();
		~JoyMapping() {}

		void Init(const IInputManager* inputManager);

		inline void SetHandler(IInputEventHandler* inputEventHandler) {
			inputEventHandler_ = inputEventHandler;
		}

		bool AddMappingsFromString(StringView mappingString);
		bool AddMappingsFromFile(StringView path);

		inline std::int32_t numMappings() const {
			return (std::int32_t)mappings_.size();
		}

		void OnJoyButtonPressed(const JoyButtonEvent& event);
		void OnJoyButtonReleased(const JoyButtonEvent& event);
		void OnJoyHatMoved(const JoyHatEvent& event);
		void OnJoyAxisMoved(const JoyAxisEvent& event);
		bool OnJoyConnected(const JoyConnectionEvent& event);
		void OnJoyDisconnected(const JoyConnectionEvent& event);

		bool IsJoyMapped(std::int32_t joyId) const;
		const JoyMappedState& GetMappedState(std::int32_t joyId) const;
		void DeadZoneNormalize(Vector2f& joyVector, float deadZoneValue = IInputManager::LeftStickDeadZone) const;
		static JoystickGuid CreateJoystickGuid(std::uint16_t bus, std::uint16_t vendor, std::uint16_t product, std::uint16_t version, StringView name, std::uint8_t driverSignature, std::uint8_t driverData);
		
		std::int32_t FindMappingByGuid(const JoystickGuid& guid) const;
		std::int32_t FindMappingByName(const char* name) const;

	private:
		static constexpr std::uint32_t MaxNameLength = 64;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct MappingDescription
		{
			struct Axis
			{
				AxisName name;
				ButtonName buttonNamePositive;
				ButtonName buttonNameNegative;
				float min;
				float max;
			};

			static const std::int32_t MaxNumAxes = 10;
			static const std::int32_t MaxNumButtons = 34;
			static const std::int32_t MaxHatButtons = 4; // The four directions

			Axis axes[MaxNumAxes];
			/// Button axes (buttons mapped as axes, like when analog triggers are missing)
			AxisName buttonAxes[MaxNumAxes];
			ButtonName buttons[MaxNumButtons];
			ButtonName hats[MaxHatButtons];

			MappingDescription();
		};

		struct MappedJoystick
		{
			JoystickGuid guid;
			char name[MaxNameLength];
			MappingDescription desc;

			MappedJoystick();
		};

		struct AssignedMapping
		{
			bool isValid;
			MappingDescription desc;
		};
#endif

		static const char* AxesStrings[];
		static const char* ButtonsStrings[];

		SmallVector<MappedJoystick, 0> mappings_;
		AssignedMapping assignedMappings_[MaxNumJoysticks];

		static JoyMappedState nullMappedJoyState_;
		static SmallVector<JoyMappedState, MaxNumJoysticks> mappedJoyStates_;
		static JoyMappedButtonEvent mappedButtonEvent_;
		static JoyMappedAxisEvent mappedAxisEvent_;

		const IInputManager* inputManager_;
		IInputEventHandler* inputEventHandler_;

		bool AddMappingsFromStringInternal(StringView mappingString, StringView traceSource);
		void CheckConnectedJoystics();
		bool ParseMappingFromString(StringView mappingString, MappedJoystick& map, bool suppressErrors);
		bool ParsePlatformName(StringView value) const;
		std::int32_t ParseAxisName(StringView value) const;
		std::int32_t ParseButtonName(StringView value) const;
		std::int32_t ParseAxisMapping(StringView value, MappingDescription::Axis& axis) const;
		std::int32_t ParseButtonMapping(StringView value) const;
		std::int32_t ParseHatMapping(StringView value) const;
		std::int32_t HatStateToIndex(std::int32_t hatState) const;
	};

}
