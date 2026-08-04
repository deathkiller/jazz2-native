#pragma once

#if defined(WITH_SDL2) || defined(WITH_SDL3) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Input/IInputManager.h"

#include <Containers/SmallVector.h>

#if !defined(CMAKE_BUILD) && defined(__has_include)
#	if defined(WITH_SDL3) && __has_include("SDL3/SDL.h")
#		define __HAS_LOCAL_SDL3
#	elif __has_include("SDL2/SDL.h")
#		define __HAS_LOCAL_SDL
#	endif
#endif
#if defined(WITH_SDL3)
#	if defined(__HAS_LOCAL_SDL3)
#		include "SDL3/SDL_events.h"
#		include "SDL3/SDL_mouse.h"
#	else
#		include <SDL3/SDL_events.h>
#		include <SDL3/SDL_mouse.h>
#	endif
#elif defined(__HAS_LOCAL_SDL)
#	include "SDL2/SDL_events.h"
#	include "SDL2/SDL_mouse.h"
#else
#	include <SDL_events.h>
#	include <SDL_mouse.h>
#endif

using namespace Death::Containers;

namespace nCine::Backends
{
	/**
		@brief Utility functions to convert between engine key enumerations and SDL2 ones
	*/
	class SdlKeys
	{
	public:
		static Keys keySymValueToEnum(int keysym);
		static int keyModMaskToEnumMask(int keymod);
		static int enumToKeysValue(Keys keysym);
		static int enumToScancode(Keys keysym);
	};

	/**
		@brief Information about SDL2 mouse state
	*/
	class SdlMouseState : public MouseState
	{
	public:
		SdlMouseState();

		bool isButtonDown(MouseButton button) const override;

	private:
		unsigned int _buttons;

		friend class SdlInputManager;
	};

	/**
		@brief Information about an SDL2 scroll event
	*/
	class SdlScrollEvent : public ScrollEvent
	{
	public:
		SdlScrollEvent() {}

		friend class SdlInputManager;
	};

	/**
		@brief Information about SDL2 keyboard state
	*/
	class SdlKeyboardState : public KeyboardState
	{
	public:
		SdlKeyboardState()
		{
			_keyState = SDL_GetKeyboardState(nullptr);
		}

		inline bool isKeyDown(Keys key) const override
		{
			const std::int32_t sdlKey = SdlKeys::enumToScancode(key);
			if (sdlKey == SDL_SCANCODE_UNKNOWN) {
				return false;
			} else {
				return _keyState[sdlKey] != 0;
			}
		}

		friend class SdlInputManager;

	private:
#if defined(WITH_SDL3)
		// SDL3: SDL_GetKeyboardState returns a const bool array (was const Uint8 in SDL2)
		const bool *_keyState;
#else
		const unsigned char *_keyState;
#endif
	};

	/**
		@brief Information about SDL2 joystick state
	*/
	class SdlJoystickState : public JoystickState
	{
	public:
		SdlJoystickState()
			: _sdlJoystick(nullptr) {}

		/** @brief Maximum absolute value reported by a joystick axis */
		static constexpr short int MaxAxisValue = 32767;

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

	private:
		SDL_Joystick *_sdlJoystick;

		friend class SdlInputManager;
	};

	/**
		@brief SDL2-based input manager
		
		Parses and dispatches SDL2 input events for keyboard, mouse, touch and joysticks.
	*/
	class SdlInputManager : public IInputManager
	{
	public:
		/** @brief The constructor takes care of opening available joysticks */
		SdlInputManager();
		/** @brief The destructor releases every opened joystick */
		~SdlInputManager();

		/** @brief Returns `true` if the application should quit in response to a quit request */
		static bool shouldQuitOnRequest();
		/**
		 * @brief Parses a single SDL2 event and dispatches it to the registered input handler
		 *
		 * @param event		SDL2 event to process
		 */
		static void parseEvent(const SDL_Event &event);

		inline const MouseState& mouseState() const override
		{
#if defined(WITH_SDL3)
			// SDL3: SDL_GetMouseState reports the cursor position as float
			float mouseX = 0.0f, mouseY = 0.0f;
			_mouseState._buttons = SDL_GetMouseState(&mouseX, &mouseY);
			_mouseState.x = static_cast<int>(mouseX);
			_mouseState.y = static_cast<int>(mouseY);
#else
			_mouseState._buttons = SDL_GetMouseState(&_mouseState.x, &_mouseState.y);
#endif
			return _mouseState;
		}

		inline const KeyboardState& keyboardState() const override { return _keyboardState; }

		String getClipboardText() const override;
		bool setClipboardText(StringView text) override;
		StringView getKeyName(Keys key) const override;

		bool isJoyPresent(int joyId) const override;
		const char *joyName(int joyId) const override;
		const JoystickGuid joyGuid(int joyId) const override;
		int joyNumButtons(int joyId) const override;
		int joyNumHats(int joyId) const override;
		int joyNumAxes(int joyId) const override;
		const JoystickState &joystickState(int joyId) const override;
		bool joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) override;
		bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override;

		void setCursor(Cursor cursor) override;

	private:
		static constexpr int MaxNumJoysticks = 16;

		static TouchEvent _touchEvent;
		static SdlMouseState _mouseState;
		static MouseEvent _mouseEvent;
		static SdlScrollEvent _scrollEvent;
		static SdlKeyboardState _keyboardState;
		static KeyboardEvent _keyboardEvent;
		static TextInputEvent _textInputEvent;

		static SDL_Joystick *_sdlJoysticks[MaxNumJoysticks];
		static SmallVector<SdlJoystickState, MaxNumJoysticks> _joystickStates;
		static JoyButtonEvent _joyButtonEvent;
		static JoyHatEvent _joyHatEvent;
		static JoyAxisEvent _joyAxisEvent;
		static JoyConnectionEvent _joyConnectionEvent;

		static char _joyGuidString[33];

		/** @brief Deleted copy constructor */
		SdlInputManager(const SdlInputManager &) = delete;
		/** @brief Deleted assignment operator */
		SdlInputManager &operator=(const SdlInputManager &) = delete;

		static void handleJoyDeviceEvent(const SDL_Event &event);
		static int joyInstanceIdToDeviceIndex(SDL_JoystickID instanceId);
	};
}

#endif