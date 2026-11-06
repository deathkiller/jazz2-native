#if defined(WITH_SDL2) || defined(WITH_SDL3)

#include "SdlInputManager.h"
#include "SdlGfxDevice.h"
#include "../Base/Algorithms.h"
#include "../Input/IInputEventHandler.h"
#include "../Input/JoyMapping.h"
#include "../Application.h"
#include "../../Main.h"

#if defined(WITH_SDL3)
#	if defined(__HAS_LOCAL_SDL3)
#		include "SDL3/SDL.h"
#	else
#		include <SDL3/SDL.h>
#	endif
#elif defined(__HAS_LOCAL_SDL)
#	include "SDL2/SDL.h"
#else
#	include <SDL.h>
#endif

#include <cstring>

#if defined(WITH_IMGUI)
#	include "ImGuiSdlInput.h"
#endif

#if defined(WITH_SDL3)
// SDL2 -> SDL3 renamed most of the joystick API and the event-type enum, but kept the signatures identical.
// These shims map the old names onto the new ones so the shared body below only needs explicit #if forks where
// the semantics (return convention, arguments, device-index vs instance-id model) actually changed.
// SDL3 ships these same old->new mappings in <SDL3/SDL_oldnames.h>, but as error sentinels when old names are
// disabled; #undef each before redefining so our mapping to the real SDL3 name installs without a redefinition warning.
#ifdef SDL_JoystickGUID
#	undef SDL_JoystickGUID
#endif
using SDL_JoystickGUID = SDL_GUID;
#undef SDL_JoystickClose
#undef SDL_JoystickGetPlayerIndex
#undef SDL_JoystickGetGUID
#undef SDL_JoystickGetGUIDString
#undef SDL_JoystickName
#undef SDL_JoystickNumHats
#undef SDL_JoystickNumAxes
#undef SDL_JoystickNumButtons
#undef SDL_JoystickNumBalls
#undef SDL_JoystickGetButton
#undef SDL_JoystickGetHat
#undef SDL_JoystickGetAxis
#undef SDL_JoystickGetAttached
#undef SDL_JoystickInstanceID
#undef SDL_JOYDEVICEADDED
#undef SDL_JOYDEVICEREMOVED
#undef SDL_KEYDOWN
#undef SDL_KEYUP
#undef SDL_TEXTINPUT
#undef SDL_MOUSEBUTTONDOWN
#undef SDL_MOUSEBUTTONUP
#undef SDL_MOUSEMOTION
#undef SDL_MOUSEWHEEL
#undef SDL_JOYBUTTONDOWN
#undef SDL_JOYBUTTONUP
#undef SDL_JOYAXISMOTION
#undef SDL_JOYHATMOTION
#undef SDL_FINGERDOWN
#undef SDL_FINGERMOTION
#undef SDL_FINGERUP
#define SDL_JoystickClose            SDL_CloseJoystick
#define SDL_JoystickGetPlayerIndex   SDL_GetJoystickPlayerIndex
#define SDL_JoystickGetGUID          SDL_GetJoystickGUID
#define SDL_JoystickGetGUIDString    SDL_GUIDToString
#define SDL_JoystickName             SDL_GetJoystickName
#define SDL_JoystickNumHats          SDL_GetNumJoystickHats
#define SDL_JoystickNumAxes          SDL_GetNumJoystickAxes
#define SDL_JoystickNumButtons       SDL_GetNumJoystickButtons
#define SDL_JoystickNumBalls         SDL_GetNumJoystickBalls
#define SDL_JoystickGetButton        SDL_GetJoystickButton
#define SDL_JoystickGetHat           SDL_GetJoystickHat
#define SDL_JoystickGetAxis          SDL_GetJoystickAxis
#define SDL_JoystickGetAttached      SDL_JoystickConnected
#define SDL_JoystickInstanceID       SDL_GetJoystickID
// Event-type enum renames (the switch/case bodies stay shared)
#define SDL_JOYDEVICEADDED           SDL_EVENT_JOYSTICK_ADDED
#define SDL_JOYDEVICEREMOVED         SDL_EVENT_JOYSTICK_REMOVED
#define SDL_KEYDOWN                  SDL_EVENT_KEY_DOWN
#define SDL_KEYUP                    SDL_EVENT_KEY_UP
#define SDL_TEXTINPUT                SDL_EVENT_TEXT_INPUT
#define SDL_MOUSEBUTTONDOWN          SDL_EVENT_MOUSE_BUTTON_DOWN
#define SDL_MOUSEBUTTONUP            SDL_EVENT_MOUSE_BUTTON_UP
#define SDL_MOUSEMOTION              SDL_EVENT_MOUSE_MOTION
#define SDL_MOUSEWHEEL               SDL_EVENT_MOUSE_WHEEL
#define SDL_JOYBUTTONDOWN            SDL_EVENT_JOYSTICK_BUTTON_DOWN
#define SDL_JOYBUTTONUP              SDL_EVENT_JOYSTICK_BUTTON_UP
#define SDL_JOYAXISMOTION            SDL_EVENT_JOYSTICK_AXIS_MOTION
#define SDL_JOYHATMOTION             SDL_EVENT_JOYSTICK_HAT_MOTION
#define SDL_FINGERDOWN               SDL_EVENT_FINGER_DOWN
#define SDL_FINGERMOTION             SDL_EVENT_FINGER_MOTION
#define SDL_FINGERUP                 SDL_EVENT_FINGER_UP
#endif

namespace nCine
{
	const std::int32_t IInputManager::MaxNumJoysticks = 16;
}

namespace nCine::Backends
{
	namespace
	{
		// A touch is delivered as a touch event and again as a mouse event synthesized from it, the latter is dropped
		// so a handler of both doesn't see every tap twice. Where finger events aren't dispatched (see below), the
		// synthesized mouse events are the only thing a touch produces, so they're kept there.
		inline bool IsMouseEventFromTouch(std::uint32_t which)
		{
#if defined(NCINE_HAS_TOUCH_CONTROLS)
			return (which == std::uint32_t(SDL_TOUCH_MOUSEID));
#else
			static_cast<void>(which);
			return false;
#endif
		}
	}

	TouchEvent SdlInputManager::_touchEvent;
	SdlMouseState SdlInputManager::_mouseState;
	MouseEvent SdlInputManager::_mouseEvent;
	SdlScrollEvent SdlInputManager::_scrollEvent;
	SdlKeyboardState SdlInputManager::_keyboardState;
	KeyboardEvent SdlInputManager::_keyboardEvent;
	TextInputEvent SdlInputManager::_textInputEvent;

	SDL_Joystick* SdlInputManager::_sdlJoysticks[SdlInputManager::MaxNumJoysticks];
	SmallVector<SdlJoystickState, SdlInputManager::MaxNumJoysticks> SdlInputManager::_joystickStates(SdlInputManager::MaxNumJoysticks);
	JoyButtonEvent SdlInputManager::_joyButtonEvent;
	JoyHatEvent SdlInputManager::_joyHatEvent;
	JoyAxisEvent SdlInputManager::_joyAxisEvent;
	JoyConnectionEvent SdlInputManager::_joyConnectionEvent;

	char SdlInputManager::_joyGuidString[33];

	namespace {

		MouseButton sdlToNcineMouseButton(int button)
		{
			if (button == SDL_BUTTON_LEFT)
				return MouseButton::Left;
			else if (button == SDL_BUTTON_RIGHT)
				return MouseButton::Right;
			else if (button == SDL_BUTTON_MIDDLE)
				return MouseButton::Middle;
			else if (button == SDL_BUTTON_X1)
				return MouseButton::Fourth;
			else if (button == SDL_BUTTON_X2)
				return MouseButton::Fifth;
			else
				return MouseButton::Left;
		}

		int ncineToSdlMouseButtonMask(MouseButton button)
		{
			switch (button) {
				case MouseButton::Left: return SDL_BUTTON_LMASK;
				case MouseButton::Right: return SDL_BUTTON_RMASK;
				case MouseButton::Middle: return SDL_BUTTON_MMASK;
				case MouseButton::Fourth: return SDL_BUTTON_X1MASK;
				case MouseButton::Fifth: return SDL_BUTTON_X2MASK;
				default: return SDL_BUTTON_LMASK;
			}
		}
	}

	SdlMouseState::SdlMouseState()
		: _buttons(0)
	{
	}

	bool SdlMouseState::IsButtonDown(MouseButton button) const
	{
		const int sdlButtonMask = ncineToSdlMouseButtonMask(button);
		return (_buttons & sdlButtonMask) != 0;
	}

	SdlInputManager::SdlInputManager()
	{
		const std::uint32_t ret = SDL_WasInit(SDL_INIT_VIDEO);
		FATAL_ASSERT_MSG(ret != 0, "SDL video subsystem is not initialized");

		// Initializing the joystick subsystem
		SDL_InitSubSystem(SDL_INIT_JOYSTICK);
		// Enabling joystick event processing
#if defined(WITH_SDL3)
		SDL_SetJoystickEventsEnabled(true);
#else
		SDL_JoystickEventState(SDL_ENABLE);
#endif

		std::memset(_sdlJoysticks, 0, sizeof(SDL_Joystick*) * MaxNumJoysticks);

		// Opening attached joysticks
#if defined(WITH_SDL3)
		// SDL3 enumerates connected joysticks as an array of opaque instance IDs (SDL2 used a 0-based count)
		int numJoysticks = 0;
		SDL_JoystickID* joystickIds = SDL_GetJoysticks(&numJoysticks);
		for (std::int32_t i = 0; i < numJoysticks && i < MaxNumJoysticks; i++) {
			_sdlJoysticks[i] = SDL_OpenJoystick(joystickIds[i]);
#	if defined(DEATH_TRACE)
			if (_sdlJoysticks[i] != nullptr) {
				SDL_Joystick* joy = _sdlJoysticks[i];
				std::int32_t playerIndex = SDL_JoystickGetPlayerIndex(joy);
				const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(joy);
				SDL_JoystickGetGUIDString(joystickGuid, _joyGuidString, 33);
				LOGI("Gamepad {} [{}] \"{}\" [{}] has been connected - {} hats, {} axes, {} buttons, {} balls",
						i, playerIndex, SDL_JoystickName(joy), _joyGuidString, SDL_JoystickNumHats(joy),
						SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy), SDL_JoystickNumBalls(joy));
			}
#	endif
		}
		SDL_free(joystickIds);
#else
		const std::int32_t numJoysticks = SDL_NumJoysticks();
		for (std::int32_t i = 0; i < numJoysticks; i++) {
			_sdlJoysticks[i] = SDL_JoystickOpen(i);
#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
			if (_sdlJoysticks[i] != nullptr) {
				SDL_Joystick* joy = _sdlJoysticks[i];
				std::int32_t playerIndex = SDL_JoystickGetPlayerIndex(joy);
				const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(joy);
				SDL_JoystickGetGUIDString(joystickGuid, _joyGuidString, 33);
				LOGI("Gamepad {} [{}] \"{}\" [{}] has been connected - {} hats, {} axes, {} buttons, {} balls",
						i, playerIndex, SDL_JoystickName(joy), _joyGuidString, SDL_JoystickNumHats(joy),
						SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy), SDL_JoystickNumBalls(joy));
			}
#endif
		}
#endif

		_joyMapping.Init(this);

#if defined(WITH_IMGUI)
		ImGuiSdlInput::Init(SdlGfxDevice::GetWindowHandle(), SdlGfxDevice::GetGlContextHandle());
#endif
	}

	SdlInputManager::~SdlInputManager()
	{
#if defined(WITH_IMGUI)
		ImGuiSdlInput::Shutdown();
#endif

		// Close a joystick if opened
		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			if (IsJoyPresent(i)) {
				SDL_JoystickClose(_sdlJoysticks[i]);
				_sdlJoysticks[i] = nullptr;
			}
		}
		
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	}

	bool SdlJoystickState::IsButtonPressed(int buttonId) const
	{
		return (_sdlJoystick != nullptr && SDL_JoystickGetButton(_sdlJoystick, buttonId) != 0);
	}

	unsigned char SdlJoystickState::GetHatState(int hatId) const
	{
		return (_sdlJoystick != nullptr ? SDL_JoystickGetHat(_sdlJoystick, hatId) : 0);
	}

	float SdlJoystickState::GetAxisValue(int axisId) const
	{
		// If the joystick is not present the returned value is zero
		short int rawValue = (_sdlJoystick != nullptr ? SDL_JoystickGetAxis(_sdlJoystick, axisId) : 0);
		return rawValue / float(MaxAxisValue);
	}

	bool SdlInputManager::ShouldQuitOnRequest()
	{
		// Only an installed handler can veto a quit; without one there is nobody to object, so the request stands.
		// Requiring a handler would make the window impossible to close whenever something else has temporarily
		// taken it away - ImGuiDrawing does exactly that for as long as ImGui wants the keyboard, which is from the
		// moment one of its windows is focused.
		return (_inputEventHandler == nullptr || _inputEventHandler->OnQuitRequest());
	}

	void SdlInputManager::ParseEvent(const SDL_Event& event)
	{
#if defined(WITH_IMGUI)
		ImGuiSdlInput::ProcessEvent(&event);
#endif

		if (_inputEventHandler == nullptr) {
			return;
		}
		if (event.type == SDL_JOYDEVICEADDED || event.type == SDL_JOYDEVICEREMOVED) {
			HandleJoyDeviceEvent(event);
			return;
		}

		// Filling static event structures
		switch (event.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				// Keys typed into an auxiliary window of the application are the application's as well
				if (!SdlGfxDevice::IsOwnWindow(event.key.windowID)) {
					return;
				}
#if defined(WITH_SDL3)
				// SDL3 removed the nested SDL_Keysym; scancode/keycode/modifiers are now fields of the event
				_keyboardEvent.Scancode = event.key.scancode;
				_keyboardEvent.Sym = SdlKeys::KeySymValueToEnum(event.key.key);
				_keyboardEvent.Mod = SdlKeys::KeyModMaskToEnumMask(event.key.mod);
#else
				_keyboardEvent.Scancode = event.key.keysym.scancode;
				_keyboardEvent.Sym = SdlKeys::KeySymValueToEnum(event.key.keysym.sym);
				_keyboardEvent.Mod = SdlKeys::KeyModMaskToEnumMask(event.key.keysym.mod);
#endif
				break;
			case SDL_TEXTINPUT: {
				if (!SdlGfxDevice::IsOwnWindow(event.text.windowID)) {
					return;
				}
				_textInputEvent.Length = copyStringFirst(_textInputEvent.Text, event.text.text, 4);
				break;
			}
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if (!SdlGfxDevice::IsOwnWindow(event.button.windowID) || IsMouseEventFromTouch(event.button.which)) {
					return;
				}
				{
					// SDL3 reports mouse coordinates as float; the engine's event structures store integers. Over an auxiliary
					// window, the position is made relative to the main window.
					Vector2i position = SdlGfxDevice::ToMainWindowPosition(event.button.windowID,
						Vector2i(static_cast<int>(event.button.x), static_cast<int>(event.button.y)));
					_mouseEvent.X = position.X;
					_mouseEvent.Y = position.Y;
					_mouseEvent.WindowId = (SdlGfxDevice::IsMainWindow(event.button.windowID) ? 0 : event.button.windowID);
				}
				_mouseEvent.Button = sdlToNcineMouseButton(event.button.button);
				break;
			case SDL_MOUSEMOTION:
				if (!SdlGfxDevice::IsOwnWindow(event.motion.windowID) || IsMouseEventFromTouch(event.motion.which)) {
					return;
				}
				_mouseState.WindowId = (SdlGfxDevice::IsMainWindow(event.motion.windowID) ? 0 : event.motion.windowID);
				if (_cursor != Cursor::HiddenLocked) {
					Vector2i position = SdlGfxDevice::ToMainWindowPosition(event.motion.windowID,
						Vector2i(static_cast<int>(event.motion.x), static_cast<int>(event.motion.y)));
					_mouseState.X = position.X;
					_mouseState.Y = position.Y;
				} else {
					_mouseState.X += static_cast<int>(event.motion.xrel);
					_mouseState.Y -= static_cast<int>(event.motion.yrel);
				}
				_mouseState._buttons = event.motion.state;
				break;
			case SDL_MOUSEWHEEL:
				if (!SdlGfxDevice::IsOwnWindow(event.wheel.windowID) || IsMouseEventFromTouch(event.wheel.which)) {
					return;
				}
				_scrollEvent.WindowId = (SdlGfxDevice::IsMainWindow(event.wheel.windowID) ? 0 : event.wheel.windowID);
#if !defined(WITH_SDL3) && SDL_VERSION_ATLEAST(2, 0, 18)
				// The fractional amounts of a touchpad scroll, SDL3 reports them in the plain fields
				_scrollEvent.X = event.wheel.preciseX;
				_scrollEvent.Y = event.wheel.preciseY;
#else
				_scrollEvent.X = static_cast<float>(event.wheel.x);
				_scrollEvent.Y = static_cast<float>(event.wheel.y);
#endif
				break;
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				_joyButtonEvent.JoyId = JoyInstanceIdToDeviceIndex(event.jbutton.which);
				_joyButtonEvent.ButtonId = event.jbutton.button;
				break;
			case SDL_JOYAXISMOTION:
				_joyAxisEvent.JoyId = JoyInstanceIdToDeviceIndex(event.jaxis.which);
				_joyAxisEvent.AxisId = event.jaxis.axis;
				_joyAxisEvent.Value = event.jaxis.value / float(SdlJoystickState::MaxAxisValue);
				break;
			case SDL_JOYHATMOTION:
				_joyHatEvent.JoyId = JoyInstanceIdToDeviceIndex(event.jhat.which);
				_joyHatEvent.HatId = event.jhat.hat;
				_joyHatEvent.HatState = event.jhat.value;
				break;
#if defined(NCINE_HAS_TOUCH_CONTROLS)
			// Where the macro is not defined the platform has a touch device the game doesn't use (the PS Vita's
			// screen and rear pad sit under the hands that hold it), so a finger event is neither translated here
			// nor dispatched below - see `NCINE_HAS_TOUCH_CONTROLS` in "Main.h"
			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
			case SDL_FINGERUP:
				if (!SdlGfxDevice::IsMainWindow(event.tfinger.windowID)) {
					return;
				}

#if defined(WITH_SDL3)
				// SDL3: SDL_GetNumTouchFingers + SDL_GetTouchFinger(index) -> SDL_GetTouchFingers (array of pointers)
				int fingerCount = 0;
				// SDL3 renamed the SDL_TouchFingerEvent members touchId/fingerId to touchID/fingerID
				SDL_Finger** fingers = SDL_GetTouchFingers(event.tfinger.touchID, &fingerCount);
				_touchEvent.Count = fingerCount;
				_touchEvent.ActionIndex = (std::int32_t)event.tfinger.fingerID;
#else
				_touchEvent.Count = SDL_GetNumTouchFingers(event.tfinger.touchId);
				_touchEvent.ActionIndex = (std::int32_t)event.tfinger.fingerId;
#endif

				switch (event.type) {
					case SDL_FINGERDOWN: _touchEvent.Type = (_touchEvent.Count == 1 ? TouchEventType::Down : TouchEventType::PointerDown); break;
					case SDL_FINGERMOTION: _touchEvent.Type = TouchEventType::Move; break;
					case SDL_FINGERUP: _touchEvent.Type = (_touchEvent.Count == 0 ? TouchEventType::Up : TouchEventType::PointerUp); break;
				}

				for (unsigned int i = 0; i < _touchEvent.Count; i++) {
#if defined(WITH_SDL3)
					SDL_Finger* finger = fingers[i];
#else
					SDL_Finger* finger = SDL_GetTouchFinger(event.tfinger.touchId, i);
#endif
					TouchEvent::Pointer& pointer = _touchEvent.Pointers[i];
					pointer.Id = static_cast<std::int32_t>(finger->id);
					pointer.X = finger->x;
					pointer.Y = finger->y;
					pointer.Pressure = finger->pressure;
				}
#if defined(WITH_SDL3)
				SDL_free(fingers);
#endif
				break;
#endif
		}

		// Calling the event handler method
		switch (event.type) {
			case SDL_KEYDOWN:
				_inputEventHandler->OnKeyPressed(_keyboardEvent);
				break;
			case SDL_KEYUP:
				_inputEventHandler->OnKeyReleased(_keyboardEvent);
				break;
			case SDL_TEXTINPUT:
				_inputEventHandler->OnTextInput(_textInputEvent);
				break;
			case SDL_MOUSEBUTTONDOWN:
				_inputEventHandler->OnMouseDown(_mouseEvent);
				break;
			case SDL_MOUSEBUTTONUP:
				_inputEventHandler->OnMouseUp(_mouseEvent);
				break;
			case SDL_MOUSEMOTION:
				_inputEventHandler->OnMouseMove(_mouseState);
				break;
			case SDL_MOUSEWHEEL:
				_inputEventHandler->OnMouseWheel(_scrollEvent);
				break;
			case SDL_JOYBUTTONDOWN:
				_joyMapping.OnJoyButtonPressed(_joyButtonEvent);
				_inputEventHandler->OnJoyButtonPressed(_joyButtonEvent);
				break;
			case SDL_JOYBUTTONUP:
				_joyMapping.OnJoyButtonReleased(_joyButtonEvent);
				_inputEventHandler->OnJoyButtonReleased(_joyButtonEvent);
				break;
			case SDL_JOYAXISMOTION:
				_joyMapping.OnJoyAxisMoved(_joyAxisEvent);
				_inputEventHandler->OnJoyAxisMoved(_joyAxisEvent);
				break;
			case SDL_JOYHATMOTION:
				_joyMapping.OnJoyHatMoved(_joyHatEvent);
				_inputEventHandler->OnJoyHatMoved(_joyHatEvent);
				break;
#if defined(NCINE_HAS_TOUCH_CONTROLS)
			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
			case SDL_FINGERUP:
				_inputEventHandler->OnTouchEvent(_touchEvent);
				break;
#endif
			default:
				break;
		}
	}

	String SdlInputManager::GetClipboardText() const
	{
		char* clipboardText = SDL_GetClipboardText();
		String result(clipboardText);
		SDL_free(clipboardText);
		return result;
	}

	bool SdlInputManager::SetClipboardText(StringView text)
	{
#if defined(WITH_SDL3)
		// SDL3: SDL_SetClipboardText returns a bool (true on success) instead of SDL2's int (0 on success)
		return SDL_SetClipboardText(String::nullTerminatedView(text).data());
#else
		return SDL_SetClipboardText(String::nullTerminatedView(text).data()) == 0;
#endif
	}

	StringView SdlInputManager::GetKeyName(Keys key) const
	{
		// TODO
		//return SDL_GetKeyName(SdlKeys::EnumToKeysValue(key));
		return IInputManager::GetKeyName(key);
	}

	bool SdlInputManager::IsJoyPresent(int joyId) const
	{
		DEATH_ASSERT(joyId >= 0);
		return (joyId < MaxNumJoysticks && _sdlJoysticks[joyId] && SDL_JoystickGetAttached(_sdlJoysticks[joyId]));
	}

	const char* SdlInputManager::GetJoyName(int joyId) const
	{
		if (IsJoyPresent(joyId)) {
			return SDL_JoystickName(_sdlJoysticks[joyId]);
		} else {
			return nullptr;
		}
	}

	const JoystickGuid SdlInputManager::GetJoyGuid(int joyId) const
	{
		if (IsJoyPresent(joyId)) {
#if defined(DEATH_TARGET_EMSCRIPTEN)
			return JoystickGuidType::Default;
#else
			const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(_sdlJoysticks[joyId]);
			SDL_JoystickGetGUIDString(joystickGuid, _joyGuidString, 33);
			return StringView(_joyGuidString);
#endif
		} else {
			return JoystickGuidType::Unknown;
		}
	}

	int SdlInputManager::GetJoyButtonCount(int joyId) const
	{
		int numButtons = -1;

		if (IsJoyPresent(joyId))
			numButtons = SDL_JoystickNumButtons(_sdlJoysticks[joyId]);

		return numButtons;
	}

	int SdlInputManager::GetJoyHatCount(int joyId) const
	{
		int numHats = -1;

		if (IsJoyPresent(joyId))
			numHats = SDL_JoystickNumHats(_sdlJoysticks[joyId]);

		return numHats;
	}

	int SdlInputManager::GetJoyAxisCount(int joyId) const
	{
		int numAxes = -1;

		if (IsJoyPresent(joyId))
			numAxes = SDL_JoystickNumAxes(_sdlJoysticks[joyId]);

		return numAxes;
	}

	const JoystickState& SdlInputManager::GetJoystickState(int joyId) const
	{
		_joystickStates[joyId]._sdlJoystick = nullptr;

		if (IsJoyPresent(joyId))
			_joystickStates[joyId]._sdlJoystick = _sdlJoysticks[joyId];

		return _joystickStates[joyId];
	}

	bool SdlInputManager::JoystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs)
	{
		if (!IsJoyPresent(joyId))
			return false;

#if defined(WITH_SDL3)
		// SDL3: SDL_JoystickRumble -> SDL_RumbleJoystick, which reports success directly as a bool
		return SDL_RumbleJoystick(_sdlJoysticks[joyId],
			(std::uint16_t)(std::clamp(lowFreqIntensity, 0.0f, 1.0f) * UINT16_MAX),
			(std::uint16_t)(std::clamp(highFreqIntensity, 0.0f, 1.0f) * UINT16_MAX),
			durationMs);
#else
		return SDL_JoystickRumble(_sdlJoysticks[joyId],
			(std::uint16_t)(std::clamp(lowFreqIntensity, 0.0f, 1.0f) * UINT16_MAX),
			(std::uint16_t)(std::clamp(highFreqIntensity, 0.0f, 1.0f) * UINT16_MAX),
			durationMs) == 0;
#endif
	}

	bool SdlInputManager::JoystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs)
	{
#if defined(WITH_SDL3)
		if (!IsJoyPresent(joyId))
			return false;

		// SDL3: SDL_JoystickRumbleTriggers -> SDL_RumbleJoystickTriggers, reporting success directly as a bool
		return SDL_RumbleJoystickTriggers(_sdlJoysticks[joyId],
			(std::uint16_t)(std::clamp(left, 0.0f, 1.0f) * UINT16_MAX),
			(std::uint16_t)(std::clamp(right, 0.0f, 1.0f) * UINT16_MAX),
			durationMs);
#elif SDL_VERSION_ATLEAST(2, 0, 14)
		if (!IsJoyPresent(joyId))
			return false;

		return SDL_JoystickRumbleTriggers(_sdlJoysticks[joyId],
			(std::uint16_t)(std::clamp(left, 0.0f, 1.0f) * UINT16_MAX),
			(std::uint16_t)(std::clamp(right, 0.0f, 1.0f) * UINT16_MAX),
			durationMs) == 0;
#else
		return false;
#endif
	}

	void SdlInputManager::SetCursor(Cursor cursor)
	{
		if (cursor != _cursor) {
			bool isChanged = true;
#if defined(WITH_SDL3)
			// SDL3: SDL_ShowCursor(SDL_ENABLE/DISABLE) -> SDL_ShowCursor()/SDL_HideCursor(); relative mouse mode
			// is now per-window via SDL_SetWindowRelativeMouseMode (returns bool)
			SDL_Window* window = SdlGfxDevice::GetWindowHandle();
			switch (cursor) {
				case Cursor::Arrow:
					SDL_ShowCursor();
					SDL_SetWindowRelativeMouseMode(window, false);
					break;
				case Cursor::Hidden:
					SDL_HideCursor();
					SDL_SetWindowRelativeMouseMode(window, false);
					break;
				case Cursor::HiddenLocked:
					isChanged = SDL_SetWindowRelativeMouseMode(window, true);
					break;
			}
#else
			switch (cursor) {
				case Cursor::Arrow:
					SDL_ShowCursor(SDL_ENABLE);
					SDL_SetRelativeMouseMode(SDL_FALSE);
					break;
				case Cursor::Hidden:
					SDL_ShowCursor(SDL_DISABLE);
					SDL_SetRelativeMouseMode(SDL_FALSE);
					break;
				case Cursor::HiddenLocked:
					std::int32_t supported = SDL_SetRelativeMouseMode(SDL_TRUE);
					isChanged = (supported == 0);
					break;
			}
#endif

			if (isChanged) {
				// Handling ImGui cursor changes
				IInputManager::SetCursor(cursor);
				_cursor = cursor;
			}
		}
	}

	void SdlInputManager::HandleJoyDeviceEvent(const SDL_Event& event)
	{
		if (event.type == SDL_JOYDEVICEADDED) {
#if defined(WITH_SDL3)
			// SDL3: jdevice.which is the joystick's opaque instance ID (SDL2 gave a device index here). Place it
			// in the first free slot so the rest of the engine keeps addressing joysticks by compact 0-based ids.
			std::int32_t deviceIndex = -1;
			for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
				if (_sdlJoysticks[i] == nullptr) {
					deviceIndex = i;
					break;
				}
			}
			if (deviceIndex < 0) {
				return;
			}
			_joyConnectionEvent.JoyId = deviceIndex;
			_sdlJoysticks[deviceIndex] = SDL_OpenJoystick(event.jdevice.which);

#	if defined(DEATH_TRACE)
			SDL_Joystick* joy = _sdlJoysticks[deviceIndex];
			std::int32_t playerIndex = SDL_JoystickGetPlayerIndex(joy);
			const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(joy);
			SDL_JoystickGetGUIDString(joystickGuid, _joyGuidString, 33);
			LOGI("Gamepad {} [{}] \"{}\" [{}] has been connected - {} hats, {} axes, {} buttons, {} balls",
					deviceIndex, playerIndex, SDL_JoystickName(joy), _joyGuidString,
					SDL_JoystickNumHats(joy), SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy), SDL_JoystickNumBalls(joy));
#	endif
#else
			const std::int32_t deviceIndex = event.jdevice.which;
			_joyConnectionEvent.JoyId = deviceIndex;

			auto* prevInstance = _sdlJoysticks[deviceIndex];
			_sdlJoysticks[deviceIndex] = SDL_JoystickOpen(deviceIndex);

			if (prevInstance != nullptr) {
				SDL_JoystickClose(prevInstance);
			}

#	if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
			SDL_Joystick* joy = _sdlJoysticks[deviceIndex];
			std::int32_t playerIndex = SDL_JoystickGetPlayerIndex(joy);
			const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(joy);
			SDL_JoystickGetGUIDString(joystickGuid, _joyGuidString, 33);
			LOGI("Gamepad {} [{}] \"{}\" [{}] has been {} - {} hats, {} axes, {} buttons, {} balls",
					deviceIndex, playerIndex, SDL_JoystickName(joy), _joyGuidString, prevInstance != nullptr ? "reattached" : "connected",
					SDL_JoystickNumHats(joy), SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy), SDL_JoystickNumBalls(joy));
#	endif
#endif
			_joyMapping.OnJoyConnected(_joyConnectionEvent);
			_inputEventHandler->OnJoyConnected(_joyConnectionEvent);
		} else if (event.type == SDL_JOYDEVICEREMOVED) {
			const std::int32_t deviceIndex = JoyInstanceIdToDeviceIndex(event.jdevice.which);
			if (deviceIndex == -1) {
				return;
			}

			_joyConnectionEvent.JoyId = deviceIndex;
			SDL_JoystickClose(_sdlJoysticks[deviceIndex]);
			_sdlJoysticks[deviceIndex] = nullptr;

			// Compacting the array of SDL joystick pointers
			for (std::int32_t i = deviceIndex; i < MaxNumJoysticks - 1; i++) {
				_sdlJoysticks[i] = _sdlJoysticks[i + 1];
			}
			_sdlJoysticks[MaxNumJoysticks - 1] = nullptr;

			LOGI("Gamepad {} has been disconnected", deviceIndex);
			_inputEventHandler->OnJoyDisconnected(_joyConnectionEvent);
			_joyMapping.OnJoyDisconnected(_joyConnectionEvent);
		}
	}

	int SdlInputManager::JoyInstanceIdToDeviceIndex(SDL_JoystickID instanceId)
	{
		std::int32_t deviceIndex = -1;
		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			SDL_JoystickID id = SDL_JoystickInstanceID(_sdlJoysticks[i]);
			if (instanceId == id) {
				deviceIndex = i;
				break;
			}
		}
		return deviceIndex;
	}
}

#endif