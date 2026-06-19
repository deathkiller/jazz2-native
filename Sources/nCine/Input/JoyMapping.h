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

	/**
		@brief Translation layer that maps raw gamepad and joystick inputs to a unified layout
		
		Parses SDL-style mapping strings, matches connected joysticks by GUID or name and converts their
		raw button/hat/axis events into @ref JoyMappedState and mapped events using unified @ref ButtonName
		and @ref AxisName values.
	*/
	class JoyMapping
	{
	public:
		/** @brief Maximum number of joysticks that can be mapped at once */
		static const std::int32_t MaxNumJoysticks = 6;

		JoyMapping();
		~JoyMapping() {}

		/** @brief Initializes the mapping layer with the specified input manager and loads the built-in mappings */
		void Init(const IInputManager* inputManager);

		/** @brief Sets the input event handler that receives mapped events */
		inline void SetHandler(IInputEventHandler* inputEventHandler) {
			inputEventHandler_ = inputEventHandler;
		}

		/** @brief Adds mapping configurations from a string and returns `true` on success */
		bool AddMappingsFromString(StringView mappingString);
		/** @brief Adds mapping configurations from a text file and returns `true` on success */
		bool AddMappingsFromFile(StringView path);

		/** @brief Returns the current number of loaded mappings */
		inline std::int32_t numMappings() const {
			return (std::int32_t)mappings_.size();
		}

		/** @brief Translates a raw button press and dispatches the mapped event */
		void OnJoyButtonPressed(const JoyButtonEvent& event);
		/** @brief Translates a raw button release and dispatches the mapped event */
		void OnJoyButtonReleased(const JoyButtonEvent& event);
		/** @brief Translates a raw hat movement and dispatches the mapped events */
		void OnJoyHatMoved(const JoyHatEvent& event);
		/** @brief Translates a raw axis movement and dispatches the mapped event */
		void OnJoyAxisMoved(const JoyAxisEvent& event);
		/** @brief Assigns a mapping to a newly connected joystick; returns `true` if a mapping was found */
		bool OnJoyConnected(const JoyConnectionEvent& event);
		/** @brief Clears the mapping assigned to a disconnected joystick */
		void OnJoyDisconnected(const JoyConnectionEvent& event);

		/** @brief Returns `true` if the specified joystick has a valid mapping assigned */
		bool IsJoyMapped(std::int32_t joyId) const;
		/** @brief Returns the unified mapped state of the specified joystick */
		const JoyMappedState& GetMappedState(std::int32_t joyId) const;
		/** @brief Normalizes a joystick axis vector, clamping values within the dead zone to zero */
		void DeadZoneNormalize(Vector2f& joyVector, float deadZoneValue = IInputManager::LeftStickDeadZone) const;
		/** @brief Builds a joystick GUID from its descriptor fields */
		static JoystickGuid CreateJoystickGuid(std::uint16_t bus, std::uint16_t vendor, std::uint16_t product, std::uint16_t version, StringView name, std::uint8_t driverSignature, std::uint8_t driverData);

		/** @brief Returns the index of the mapping matching the specified GUID, or -1 if none */
		std::int32_t FindMappingByGuid(const JoystickGuid& guid) const;
		/** @brief Returns the index of the mapping matching the specified name, or -1 if none */
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
				// Output range the logical axis is driven across (supports half-axis outputs like "+rightx")
				float min;
				float max;
				// Input range of the physical axis that is considered (supports half-axis inputs like "+a2" and "a2~")
				float inMin;
				float inMax;
			};

			// Physical button or hat direction driving a (half of a) logical axis
			struct AxisSource
			{
				AxisName name;
				float value; // Output value applied while the source is active (+1 or -1)
			};

			static const std::int32_t MaxNumAxes = 10;
			static const std::int32_t MaxNumButtons = 34;
			static const std::int32_t MaxHatButtons = 4; // The four directions

			Axis axes[MaxNumAxes];
			// Physical buttons mapped to logical axes (e.g., digital triggers when analog ones are missing).
			// Indexed by physical button id, so it must be sized like buttons[], not like axes[].
			AxisSource buttonAxes[MaxNumButtons];
			ButtonName buttons[MaxNumButtons];
			ButtonName hats[MaxHatButtons];
			// Hat directions mapped to logical axes (e.g., a D-pad used as a stick, like sideways Joy-Con)
			AxisSource hatAxes[MaxHatButtons];

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
		std::int32_t ParseAxisInput(StringView value, MappingDescription::Axis& axis) const;
		std::int32_t ParseButtonMapping(StringView value) const;
		std::int32_t ParseHatMapping(StringView value) const;
		std::int32_t HatStateToIndex(std::int32_t hatState) const;
	};

}
