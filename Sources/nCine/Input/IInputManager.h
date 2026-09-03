#pragma once

#include "InputEvents.h"
#include "../Primitives/Vector2.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	class IInputEventHandler;
	class JoyMapping;

	/**
		@brief Well-known joystick GUID layouts
		
		Identifies how the bytes of a @ref JoystickGuid are laid out so they can be parsed accordingly.
	*/
	enum class JoystickGuidType {
		Unknown,
		Standard,
		Default,
		Hidapi,
		Xinput
	};

	/**
		@brief Parsed 16-byte GUID that identifies a joystick model
		
		Used to match a connected joystick against a mapping configuration.
	*/
	class JoystickGuid
	{
	public:
		JoystickGuid();
		inline JoystickGuid(JoystickGuidType type) {
			fromType(type);
		}
		inline JoystickGuid(StringView string) {
			fromString(string);
		}
		/** @brief Fills the GUID for a well-known layout type */
		void fromType(JoystickGuidType type);
		/** @brief Parses the GUID from its hexadecimal string representation */
		void fromString(StringView string);
		/** @brief Returns `true` if the GUID is not all zeros */
		bool isValid() const;

		bool operator==(const JoystickGuid& guid) const;

		/**
		 * @brief Raw GUID bytes
		 *
		 * Over-aligned deliberately: `fromType()`, `isValid()` and @relativeref{nCine,JoyMapping::CreateJoystickGuid()}
		 * all read and write these bytes four or two at a time through a `std::uint32_t*` / `std::uint16_t*`,
		 * which a plain byte array does not guarantee is legal --- its own alignment is 1, so wherever the
		 * compiler happened to place a GUID could leave those accesses unaligned. That costs nothing on x86
		 * or ARM, but MIPS and SH-4 raise an address error rather than taking a slow path, which kills the
		 * process outright (see the event-parameter read in `Player::OnUpdate` that did exactly that on the
		 * PSP). One `alignas` makes every one of those accesses well defined instead of lucky.
		 */
		alignas(std::uint32_t) std::uint8_t data[16];
	};

	// Guards the alignment the casts above depend on: `data` is the only member, so over-aligning it
	// over-aligns the whole class, and a later member reordering that broke this would otherwise only show
	// up as a hardware fault on a console
	static_assert(alignof(JoystickGuid) >= alignof(std::uint32_t),
		"JoystickGuid is read four bytes at a time and must be aligned for it");

	/**
		@brief Interface for querying input state and dispatching input events
		
		Provides access to mouse, keyboard and joystick state, manages the active @ref IInputEventHandler
		and exposes the joystick mapping layer. Backends derive from it to supply platform-specific input.
	*/
	class IInputManager
	{
	public:
		/** @brief Mouse cursor mode */
		enum class Cursor
		{
			Arrow,			/**< Cursor is shown and behaves normally */
			Hidden,			/**< Cursor is hidden but behaves normally */
			HiddenLocked	/**< Cursor is hidden and locked to the window; movement is relative if supported (no acceleration or scaling) */
		};

		/** @{ @name Constants */

		/** @brief Maximum number of joysticks */
		static const std::int32_t MaxNumJoysticks;

		// From `XInput.h` in DirectX SDK
		/** @brief Dead zone radius applied to the left analog stick */
		static constexpr float LeftStickDeadZone = 7849 / 32767.0f;
		/** @brief Dead zone radius applied to the right analog stick */
		static constexpr float RightStickDeadZone = 8689 / 32767.0f;
		/** @brief Dead zone applied to analog triggers */
		static constexpr float TriggerDeadZone = 30 / 32767.0f;
		/** @brief Threshold above which an analog input is reported as a pressed button */
		static constexpr float AnalogInButtonDeadZone = 0.85f;
		/** @brief Threshold below which an analog input is reported as a released button */
		static constexpr float AnalogOutButtonDeadZone = 0.4f;
		/** @brief Threshold above which an analog trigger is reported as a pressed button */
		static constexpr float TriggerButtonDeadZone = 0.02f;

		/** @} */

		IInputManager() {}
		virtual ~IInputManager() {}
		/** @brief Returns the current input event handler */
		static inline IInputEventHandler* handler() {
			return _inputEventHandler;
		}
		/** @brief Sets the input event handler */
		static void setHandler(IInputEventHandler* inputEventHandler);

		/** @brief Returns the current mouse state */
		virtual const MouseState& mouseState() const = 0;
		/** @brief Returns the current keyboard state */
		virtual const KeyboardState& keyboardState() const = 0;

		/** @brief Returns the clipboard text, or empty if none */
		virtual String getClipboardText() const;
		/** @brief Sets the clipboard text */
		virtual bool setClipboardText(StringView text);
		/** @brief Returns the display name of the specified key */
		virtual StringView getKeyName(Keys key) const;

		/** @brief Returns `true` if the specified joystick is connected */
		virtual bool isJoyPresent(int joyId) const = 0;
		/** @brief Returns the name of the specified joystick */
		virtual const char* joyName(int joyId) const = 0;
		/** @brief Returns the GUID of the specified joystick */
		virtual const JoystickGuid joyGuid(int joyId) const = 0;
		/** @brief Returns the number of buttons of the specified joystick */
		virtual int joyNumButtons(int joyId) const = 0;
		/** @brief Returns the number of hats of the specified joystick */
		virtual int joyNumHats(int joyId) const = 0;
		/** @brief Returns the number of axes of the specified joystick */
		virtual int joyNumAxes(int joyId) const = 0;
		/** @brief Returns the raw state of the specified joystick */
		virtual const JoystickState& joystickState(int joyId) const = 0;
		/** @brief Starts a low/high frequency rumble effect for the specified duration in milliseconds */
		virtual bool joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) = 0;
		/** @brief Starts a left/right trigger rumble effect for the specified duration in milliseconds */
		virtual bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) = 0;

		/**
			@brief Returns `true` if the joystick has a valid mapping configuration
			
			@note A joystick stays mapped in the @ref IInputEventHandler::OnJoyConnected() and
			@ref IInputEventHandler::OnJoyDisconnected() callbacks.
		*/
		bool isJoyMapped(int joyId) const;
		/** @brief Returns the unified mapped state of the specified joystick */
		const JoyMappedState& joyMappedState(int joyId) const;
		/** @brief Normalizes a joystick axis vector, clamping values within the dead zone to zero */
		void deadZoneNormalize(Vector2f& joyVector, float deadZoneValue) const;

		/** @brief Adds joystick mapping configurations from a text file */
		void addJoyMappingsFromFile(StringView path);
		/** @brief Adds joystick mapping configurations from a string */
		void addJoyMappingsFromString(StringView mappingStrings);
		/** @brief Returns the current number of valid joystick mappings */
		unsigned int numJoyMappings() const;
		/** @brief Returns `true` if a mapping exists for the specified joystick GUID */
		bool hasMappingByGuid(const JoystickGuid& guid) const;
		/** @brief Returns `true` if a mapping exists for the specified joystick name */
		bool hasMappingByName(const char* name) const;

		/** @brief Returns the current mouse cursor mode */
		inline Cursor cursor() const {
			return _cursor;
		}
		/** @brief Sets the mouse cursor mode */
		virtual void setCursor(Cursor cursor);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		static IInputEventHandler* _inputEventHandler;
		static Cursor _cursor;

		static JoyMapping _joyMapping;
#endif
	};

#ifndef DOXYGEN_GENERATING_OUTPUT
	/// A fake input manager which doesn't process any input
	class NullInputManager : public IInputManager
	{
	public:
		NullInputManager() : _mouseState{}, _keyState{}, _joyState{} {}

		virtual const MouseState& mouseState() const override { return _mouseState; }
		virtual const KeyboardState& keyboardState() const override { return _keyState; }

		String getClipboardText() const override { return {}; }
		StringView getKeyName(Keys key) const override { return {}; }

		bool isJoyPresent(int joyId) const override { return false; }
		const char* joyName(int joyId) const override { return nullptr; }
		const JoystickGuid joyGuid(int joyId) const override { return {}; }
		int joyNumButtons(int joyId) const override { return 0; }
		int joyNumHats(int joyId) const override { return 0; }
		int joyNumAxes(int joyId) const override { return 0; }

		const JoystickState& joystickState(int joyId) const override { return _joyState; }
		bool joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) override { return false; }
		bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override { return false; }

		void setCursor(Cursor cursor) override {}

	public:
		class NullMouseState : public MouseState
		{
		public:
			bool isButtonDown(MouseButton button) const override { return false; }
		};

		class NullKeyboardState : public KeyboardState
		{
		public:
			bool isKeyDown(Keys key) const override { return false; }
		};

		class NullJoystickState : public JoystickState
		{
		public:
			bool isButtonPressed(int buttonId) const override { return false; }
			unsigned char hatState(int hatId) const override { return 0; }
			float axisValue(int axisId) const override { return 0.0f; }
		};

	private:
		NullMouseState _mouseState;
		NullKeyboardState _keyState;
		NullJoystickState _joyState;
	};
#endif
}
