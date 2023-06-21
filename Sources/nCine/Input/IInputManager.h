#pragma once

#include "InputEvents.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class IInputEventHandler;
	class JoyMapping;
	template <class T> class Vector2;
	using Vector2f = Vector2<float>;

	enum class JoystickGuidType {
		Unknown,
		Standard,
		Default,
		Hidapi,
		Xinput
	};

	class JoystickGuid
	{
	public:
		JoystickGuid();
		inline JoystickGuid(JoystickGuidType type) {
			fromType(type);
		}
		inline JoystickGuid(const StringView& string) {
			fromString(string);
		}
		void fromType(JoystickGuidType type);
		void fromString(const StringView& string);
		bool isValid() const;

		bool operator==(const JoystickGuid& guid) const;

		uint8_t data[16];
	};

	/// The interface class for parsing and dispatching input events
	class IInputManager
	{
	public:
		enum class Cursor
		{
			/// Mouse cursor behaves normally with default Arrow
			Arrow,
			/// Mouse cursor is hidden but behaves normally
			Hidden,
			/// Mouse cursor is hidden and locked to the window
			/*! \note Mouse movement will be relative if supported (with no acceleration and no scaling) */
			HiddenLocked
		};

		static const int MaxNumJoysticks;

		// From `XInput.h` in DirectX SDK
		static constexpr float LeftStickDeadZone = 7849 / 32767.0f;
		static constexpr float RightStickDeadZone = 8689 / 32767.0f;
		static constexpr float TriggerDeadZone = 30 / 32767.0f;

		IInputManager() {}
		virtual ~IInputManager() {}
		/// Gets the current input event handler for the manager
		static inline IInputEventHandler* handler() {
			return inputEventHandler_;
		}
		/// Sets the input event handler for the manager
		static void setHandler(IInputEventHandler* inputEventHandler);

		/// Returns current mouse state
		virtual const MouseState& mouseState() const = 0;
		/// Returns current keyboard state
		virtual const KeyboardState& keyboardState() const = 0;

		/// Returns true if the specified joystick is connected
		virtual bool isJoyPresent(int joyId) const = 0;
		/// Returns the name of the specified joystick
		virtual const char* joyName(int joyId) const = 0;
		/// Returns the GUID of the specified joystick
		virtual const JoystickGuid joyGuid(int joyId) const = 0;
		/// Returns the number of available buttons for the specified joystick
		virtual int joyNumButtons(int joyId) const = 0;
		/// Returns the number of available hats for the specified joystick
		virtual int joyNumHats(int joyId) const = 0;
		/// Returns the number of available axes for the specified joystick
		virtual int joyNumAxes(int joyId) const = 0;
		/// Returns the state of the joystick
		virtual const JoystickState& joystickState(int joyId) const = 0;
		/// Starts a main rumble effect with specified duration
		virtual bool joystickRumble(int joyId, float lowFrequency, float highFrequency, uint32_t durationMs) = 0;
		/// Starts a rumble effect on triggers with specified duration
		virtual bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) = 0;

		/// Returns `true` if the joystick has a valid mapping configuration
		bool isJoyMapped(int joyId) const;
		/// Returns the state of the mapped joystick
		const JoyMappedState& joyMappedState(int joyId) const;
		/// Modify the joystick axis vector to account for a dead zone
		void deadZoneNormalize(Vector2f& joyVector, float deadZoneValue) const;

		/// Adds joystick mapping configurations from a text file
		void addJoyMappingsFromFile(const StringView& path);
		/// Adds joystick mapping configurations from a strings array terminated by a `nullptr`
		void addJoyMappingsFromStrings(const char** mappingStrings);
		/// Returns the current number of valid joystick mappings
		unsigned int numJoyMappings() const;
		/// Returns true if mapping exists for specified joystick by GUID
		bool hasMappingByGuid(const JoystickGuid& guid) const;
		/// Returns true if mapping exists for specified joystick by name
		bool hasMappingByName(const char* name) const;

		/// Returns current mouse cursor mode
		inline Cursor cursor() const {
			return cursor_;
		}
		/// Sets the mouse cursor mode
		virtual void setCursor(Cursor cursor);

	protected:
		static IInputEventHandler* inputEventHandler_;
		static Cursor cursor_;

		static JoyMapping joyMapping_;
	};

}
