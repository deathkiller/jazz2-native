#if defined(WITH_SDL2) || defined(WITH_SDL3)

#include "SdlInputManager.h"
#include "SdlGfxDevice.h"
#include "../Base/Algorithms.h"
#include "../Input/IInputEventHandler.h"
#include "../Input/JoyMapping.h"
#include "../Application.h"

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
using SDL_JoystickGUID = SDL_GUID;
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
	TouchEvent SdlInputManager::touchEvent_;
	SdlMouseState SdlInputManager::mouseState_;
	MouseEvent SdlInputManager::mouseEvent_;
	SdlScrollEvent SdlInputManager::scrollEvent_;
	SdlKeyboardState SdlInputManager::keyboardState_;
	KeyboardEvent SdlInputManager::keyboardEvent_;
	TextInputEvent SdlInputManager::textInputEvent_;

	SDL_Joystick* SdlInputManager::sdlJoysticks_[SdlInputManager::MaxNumJoysticks];
	SmallVector<SdlJoystickState, SdlInputManager::MaxNumJoysticks> SdlInputManager::joystickStates_(SdlInputManager::MaxNumJoysticks);
	JoyButtonEvent SdlInputManager::joyButtonEvent_;
	JoyHatEvent SdlInputManager::joyHatEvent_;
	JoyAxisEvent SdlInputManager::joyAxisEvent_;
	JoyConnectionEvent SdlInputManager::joyConnectionEvent_;

	char SdlInputManager::joyGuidString_[33];

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
		: buttons_(0)
	{
	}

	bool SdlMouseState::isButtonDown(MouseButton button) const
	{
		const int sdlButtonMask = ncineToSdlMouseButtonMask(button);
		return (buttons_ & sdlButtonMask) != 0;
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

		std::memset(sdlJoysticks_, 0, sizeof(SDL_Joystick*) * MaxNumJoysticks);

		// Opening attached joysticks
#if defined(WITH_SDL3)
		// SDL3 enumerates connected joysticks as an array of opaque instance IDs (SDL2 used a 0-based count)
		int numJoysticks = 0;
		SDL_JoystickID* joystickIds = SDL_GetJoysticks(&numJoysticks);
		for (std::int32_t i = 0; i < numJoysticks && i < MaxNumJoysticks; i++) {
			sdlJoysticks_[i] = SDL_OpenJoystick(joystickIds[i]);
#	if defined(DEATH_TRACE)
			if (sdlJoysticks_[i] != nullptr) {
				SDL_Joystick* joy = sdlJoysticks_[i];
				std::int32_t playerIndex = SDL_JoystickGetPlayerIndex(joy);
				const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(joy);
				SDL_JoystickGetGUIDString(joystickGuid, joyGuidString_, 33);
				LOGI("Gamepad {} [{}] \"{}\" [{}] has been connected - {} hats, {} axes, {} buttons, {} balls",
						i, playerIndex, SDL_JoystickName(joy), joyGuidString_, SDL_JoystickNumHats(joy),
						SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy), SDL_JoystickNumBalls(joy));
			}
#	endif
		}
		SDL_free(joystickIds);
#else
		const std::int32_t numJoysticks = SDL_NumJoysticks();
		for (std::int32_t i = 0; i < numJoysticks; i++) {
			sdlJoysticks_[i] = SDL_JoystickOpen(i);
#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
			if (sdlJoysticks_[i] != nullptr) {
				SDL_Joystick* joy = sdlJoysticks_[i];
				std::int32_t playerIndex = SDL_JoystickGetPlayerIndex(joy);
				const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(joy);
				SDL_JoystickGetGUIDString(joystickGuid, joyGuidString_, 33);
				LOGI("Gamepad {} [{}] \"{}\" [{}] has been connected - {} hats, {} axes, {} buttons, {} balls",
						i, playerIndex, SDL_JoystickName(joy), joyGuidString_, SDL_JoystickNumHats(joy),
						SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy), SDL_JoystickNumBalls(joy));
			}
#endif
		}
#endif

		joyMapping_.Init(this);

#if defined(WITH_IMGUI)
		ImGuiSdlInput::init(SdlGfxDevice::windowHandle(), SdlGfxDevice::glContextHandle());
#endif
	}

	SdlInputManager::~SdlInputManager()
	{
#if defined(WITH_IMGUI)
		ImGuiSdlInput::shutdown();
#endif

		// Close a joystick if opened
		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			if (isJoyPresent(i)) {
				SDL_JoystickClose(sdlJoysticks_[i]);
				sdlJoysticks_[i] = nullptr;
			}
		}
		
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	}

	bool SdlJoystickState::isButtonPressed(int buttonId) const
	{
		return (sdlJoystick_ != nullptr && SDL_JoystickGetButton(sdlJoystick_, buttonId) != 0);
	}

	unsigned char SdlJoystickState::hatState(int hatId) const
	{
		return (sdlJoystick_ != nullptr ? SDL_JoystickGetHat(sdlJoystick_, hatId) : 0);
	}

	float SdlJoystickState::axisValue(int axisId) const
	{
		// If the joystick is not present the returned value is zero
		short int rawValue = (sdlJoystick_ != nullptr ? SDL_JoystickGetAxis(sdlJoystick_, axisId) : 0);
		return rawValue / float(MaxAxisValue);
	}

	bool SdlInputManager::shouldQuitOnRequest()
	{
		return (inputEventHandler_ != nullptr && inputEventHandler_->OnQuitRequest());
	}

	void SdlInputManager::parseEvent(const SDL_Event& event)
	{
#if defined(WITH_IMGUI)
		ImGuiSdlInput::processEvent(&event);
#endif

		if (inputEventHandler_ == nullptr) {
			return;
		}
		if (event.type == SDL_JOYDEVICEADDED || event.type == SDL_JOYDEVICEREMOVED) {
			handleJoyDeviceEvent(event);
			return;
		}

		// Filling static event structures
		switch (event.type) {
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if (!SdlGfxDevice::isMainWindow(event.key.windowID)) {
					return;
				}
#if defined(WITH_SDL3)
				// SDL3 removed the nested SDL_Keysym; scancode/keycode/modifiers are now fields of the event
				keyboardEvent_.scancode = event.key.scancode;
				keyboardEvent_.sym = SdlKeys::keySymValueToEnum(event.key.key);
				keyboardEvent_.mod = SdlKeys::keyModMaskToEnumMask(event.key.mod);
#else
				keyboardEvent_.scancode = event.key.keysym.scancode;
				keyboardEvent_.sym = SdlKeys::keySymValueToEnum(event.key.keysym.sym);
				keyboardEvent_.mod = SdlKeys::keyModMaskToEnumMask(event.key.keysym.mod);
#endif
				break;
			case SDL_TEXTINPUT: {
				if (!SdlGfxDevice::isMainWindow(event.text.windowID)) {
					return;
				}
				textInputEvent_.length = copyStringFirst(textInputEvent_.text, event.text.text, 4);
				break;
			}
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				if (!SdlGfxDevice::isMainWindow(event.button.windowID)) {
					return;
				}
				// SDL3 reports mouse coordinates as float; the engine's event structures store integers
				mouseEvent_.x = static_cast<int>(event.button.x);
				mouseEvent_.y = static_cast<int>(event.button.y);
				mouseEvent_.button = sdlToNcineMouseButton(event.button.button);
				break;
			case SDL_MOUSEMOTION:
				if (!SdlGfxDevice::isMainWindow(event.motion.windowID)) {
					return;
				}
				if (cursor_ != Cursor::HiddenLocked) {
					mouseState_.x = static_cast<int>(event.motion.x);
					mouseState_.y = static_cast<int>(event.motion.y);
				} else {
					mouseState_.x += static_cast<int>(event.motion.xrel);
					mouseState_.y -= static_cast<int>(event.motion.yrel);
				}
				mouseState_.buttons_ = event.motion.state;
				break;
			case SDL_MOUSEWHEEL:
				if (!SdlGfxDevice::isMainWindow(event.wheel.windowID)) {
					return;
				}
				scrollEvent_.x = static_cast<float>(event.wheel.x);
				scrollEvent_.y = static_cast<float>(event.wheel.y);
				break;
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
				joyButtonEvent_.joyId = joyInstanceIdToDeviceIndex(event.jbutton.which);
				joyButtonEvent_.buttonId = event.jbutton.button;
				break;
			case SDL_JOYAXISMOTION:
				joyAxisEvent_.joyId = joyInstanceIdToDeviceIndex(event.jaxis.which);
				joyAxisEvent_.axisId = event.jaxis.axis;
				joyAxisEvent_.value = event.jaxis.value / float(SdlJoystickState::MaxAxisValue);
				break;
			case SDL_JOYHATMOTION:
				joyHatEvent_.joyId = joyInstanceIdToDeviceIndex(event.jhat.which);
				joyHatEvent_.hatId = event.jhat.hat;
				joyHatEvent_.hatState = event.jhat.value;
				break;
			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
			case SDL_FINGERUP:
				if (!SdlGfxDevice::isMainWindow(event.tfinger.windowID)) {
					return;
				}

#if defined(WITH_SDL3)
				// SDL3: SDL_GetNumTouchFingers + SDL_GetTouchFinger(index) -> SDL_GetTouchFingers (array of pointers)
				int fingerCount = 0;
				SDL_Finger** fingers = SDL_GetTouchFingers(event.tfinger.touchId, &fingerCount);
				touchEvent_.count = fingerCount;
#else
				touchEvent_.count = SDL_GetNumTouchFingers(event.tfinger.touchId);
#endif
				touchEvent_.actionIndex = (std::int32_t)event.tfinger.fingerId;

				switch (event.type) {
					case SDL_FINGERDOWN: touchEvent_.type = (touchEvent_.count == 1 ? TouchEventType::Down : TouchEventType::PointerDown); break;
					case SDL_FINGERMOTION: touchEvent_.type = TouchEventType::Move; break;
					case SDL_FINGERUP: touchEvent_.type = (touchEvent_.count == 0 ? TouchEventType::Up : TouchEventType::PointerUp); break;
				}

				for (unsigned int i = 0; i < touchEvent_.count; i++) {
#if defined(WITH_SDL3)
					SDL_Finger* finger = fingers[i];
#else
					SDL_Finger* finger = SDL_GetTouchFinger(event.tfinger.touchId, i);
#endif
					TouchEvent::Pointer& pointer = touchEvent_.pointers[i];
					pointer.id = static_cast<std::int32_t>(finger->id);
					pointer.x = finger->x;
					pointer.y = finger->y;
					pointer.pressure = finger->pressure;
				}
#if defined(WITH_SDL3)
				SDL_free(fingers);
#endif
				break;
		}

		// Calling the event handler method
		switch (event.type) {
			case SDL_KEYDOWN:
				inputEventHandler_->OnKeyPressed(keyboardEvent_);
				break;
			case SDL_KEYUP:
				inputEventHandler_->OnKeyReleased(keyboardEvent_);
				break;
			case SDL_TEXTINPUT:
				inputEventHandler_->OnTextInput(textInputEvent_);
				break;
			case SDL_MOUSEBUTTONDOWN:
				inputEventHandler_->OnMouseDown(mouseEvent_);
				break;
			case SDL_MOUSEBUTTONUP:
				inputEventHandler_->OnMouseUp(mouseEvent_);
				break;
			case SDL_MOUSEMOTION:
				inputEventHandler_->OnMouseMove(mouseState_);
				break;
			case SDL_MOUSEWHEEL:
				inputEventHandler_->OnMouseWheel(scrollEvent_);
				break;
			case SDL_JOYBUTTONDOWN:
				joyMapping_.OnJoyButtonPressed(joyButtonEvent_);
				inputEventHandler_->OnJoyButtonPressed(joyButtonEvent_);
				break;
			case SDL_JOYBUTTONUP:
				joyMapping_.OnJoyButtonReleased(joyButtonEvent_);
				inputEventHandler_->OnJoyButtonReleased(joyButtonEvent_);
				break;
			case SDL_JOYAXISMOTION:
				joyMapping_.OnJoyAxisMoved(joyAxisEvent_);
				inputEventHandler_->OnJoyAxisMoved(joyAxisEvent_);
				break;
			case SDL_JOYHATMOTION:
				joyMapping_.OnJoyHatMoved(joyHatEvent_);
				inputEventHandler_->OnJoyHatMoved(joyHatEvent_);
				break;
			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
			case SDL_FINGERUP:
				inputEventHandler_->OnTouchEvent(touchEvent_);
				break;
			default:
				break;
		}
	}

	String SdlInputManager::getClipboardText() const
	{
		char* clipboardText = SDL_GetClipboardText();
		String result(clipboardText);
		SDL_free(clipboardText);
		return result;
	}

	bool SdlInputManager::setClipboardText(StringView text)
	{
#if defined(WITH_SDL3)
		// SDL3: SDL_SetClipboardText returns a bool (true on success) instead of SDL2's int (0 on success)
		return SDL_SetClipboardText(String::nullTerminatedView(text).data());
#else
		return SDL_SetClipboardText(String::nullTerminatedView(text).data()) == 0;
#endif
	}

	StringView SdlInputManager::getKeyName(Keys key) const
	{
		// TODO
		//return SDL_GetKeyName(SdlKeys::enumToKeysValue(key));
		return IInputManager::getKeyName(key);
	}

	bool SdlInputManager::isJoyPresent(int joyId) const
	{
		DEATH_ASSERT(joyId >= 0);
		return (joyId < MaxNumJoysticks && sdlJoysticks_[joyId] && SDL_JoystickGetAttached(sdlJoysticks_[joyId]));
	}

	const char* SdlInputManager::joyName(int joyId) const
	{
		if (isJoyPresent(joyId)) {
			return SDL_JoystickName(sdlJoysticks_[joyId]);
		} else {
			return nullptr;
		}
	}

	const JoystickGuid SdlInputManager::joyGuid(int joyId) const
	{
		if (isJoyPresent(joyId)) {
#if defined(DEATH_TARGET_EMSCRIPTEN)
			return JoystickGuidType::Default;
#else
			const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(sdlJoysticks_[joyId]);
			SDL_JoystickGetGUIDString(joystickGuid, joyGuidString_, 33);
			return StringView(joyGuidString_);
#endif
		} else {
			return JoystickGuidType::Unknown;
		}
	}

	int SdlInputManager::joyNumButtons(int joyId) const
	{
		int numButtons = -1;

		if (isJoyPresent(joyId))
			numButtons = SDL_JoystickNumButtons(sdlJoysticks_[joyId]);

		return numButtons;
	}

	int SdlInputManager::joyNumHats(int joyId) const
	{
		int numHats = -1;

		if (isJoyPresent(joyId))
			numHats = SDL_JoystickNumHats(sdlJoysticks_[joyId]);

		return numHats;
	}

	int SdlInputManager::joyNumAxes(int joyId) const
	{
		int numAxes = -1;

		if (isJoyPresent(joyId))
			numAxes = SDL_JoystickNumAxes(sdlJoysticks_[joyId]);

		return numAxes;
	}

	const JoystickState& SdlInputManager::joystickState(int joyId) const
	{
		joystickStates_[joyId].sdlJoystick_ = nullptr;

		if (isJoyPresent(joyId))
			joystickStates_[joyId].sdlJoystick_ = sdlJoysticks_[joyId];

		return joystickStates_[joyId];
	}

	bool SdlInputManager::joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs)
	{
		if (!isJoyPresent(joyId))
			return false;

#if defined(WITH_SDL3)
		// SDL3: SDL_JoystickRumble -> SDL_RumbleJoystick, which reports success directly as a bool
		return SDL_RumbleJoystick(sdlJoysticks_[joyId],
			(std::uint16_t)(std::clamp(lowFreqIntensity, 0.0f, 1.0f) * UINT16_MAX),
			(std::uint16_t)(std::clamp(highFreqIntensity, 0.0f, 1.0f) * UINT16_MAX),
			durationMs);
#else
		return SDL_JoystickRumble(sdlJoysticks_[joyId],
			(std::uint16_t)(std::clamp(lowFreqIntensity, 0.0f, 1.0f) * UINT16_MAX),
			(std::uint16_t)(std::clamp(highFreqIntensity, 0.0f, 1.0f) * UINT16_MAX),
			durationMs) == 0;
#endif
	}

	bool SdlInputManager::joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs)
	{
#if defined(WITH_SDL3)
		if (!isJoyPresent(joyId))
			return false;

		// SDL3: SDL_JoystickRumbleTriggers -> SDL_RumbleJoystickTriggers, reporting success directly as a bool
		return SDL_RumbleJoystickTriggers(sdlJoysticks_[joyId],
			(std::uint16_t)(std::clamp(left, 0.0f, 1.0f) * UINT16_MAX),
			(std::uint16_t)(std::clamp(right, 0.0f, 1.0f) * UINT16_MAX),
			durationMs);
#elif SDL_VERSION_ATLEAST(2, 0, 14)
		if (!isJoyPresent(joyId))
			return false;

		return SDL_JoystickRumbleTriggers(sdlJoysticks_[joyId],
			(std::uint16_t)(std::clamp(left, 0.0f, 1.0f) * UINT16_MAX),
			(std::uint16_t)(std::clamp(right, 0.0f, 1.0f) * UINT16_MAX),
			durationMs) == 0;
#else
		return false;
#endif
	}

	void SdlInputManager::setCursor(Cursor cursor)
	{
		if (cursor != cursor_) {
			bool isChanged = true;
#if defined(WITH_SDL3)
			// SDL3: SDL_ShowCursor(SDL_ENABLE/DISABLE) -> SDL_ShowCursor()/SDL_HideCursor(); relative mouse mode
			// is now per-window via SDL_SetWindowRelativeMouseMode (returns bool)
			SDL_Window* window = SdlGfxDevice::windowHandle();
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
				IInputManager::setCursor(cursor);
				cursor_ = cursor;
			}
		}
	}

	void SdlInputManager::handleJoyDeviceEvent(const SDL_Event& event)
	{
		if (event.type == SDL_JOYDEVICEADDED) {
#if defined(WITH_SDL3)
			// SDL3: jdevice.which is the joystick's opaque instance ID (SDL2 gave a device index here). Place it
			// in the first free slot so the rest of the engine keeps addressing joysticks by compact 0-based ids.
			std::int32_t deviceIndex = -1;
			for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
				if (sdlJoysticks_[i] == nullptr) {
					deviceIndex = i;
					break;
				}
			}
			if (deviceIndex < 0) {
				return;
			}
			joyConnectionEvent_.joyId = deviceIndex;
			sdlJoysticks_[deviceIndex] = SDL_OpenJoystick(event.jdevice.which);

#	if defined(DEATH_TRACE)
			SDL_Joystick* joy = sdlJoysticks_[deviceIndex];
			std::int32_t playerIndex = SDL_JoystickGetPlayerIndex(joy);
			const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(joy);
			SDL_JoystickGetGUIDString(joystickGuid, joyGuidString_, 33);
			LOGI("Gamepad {} [{}] \"{}\" [{}] has been connected - {} hats, {} axes, {} buttons, {} balls",
					deviceIndex, playerIndex, SDL_JoystickName(joy), joyGuidString_,
					SDL_JoystickNumHats(joy), SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy), SDL_JoystickNumBalls(joy));
#	endif
#else
			const std::int32_t deviceIndex = event.jdevice.which;
			joyConnectionEvent_.joyId = deviceIndex;

			auto* prevInstance = sdlJoysticks_[deviceIndex];
			sdlJoysticks_[deviceIndex] = SDL_JoystickOpen(deviceIndex);

			if (prevInstance != nullptr) {
				SDL_JoystickClose(prevInstance);
			}

#	if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
			SDL_Joystick* joy = sdlJoysticks_[deviceIndex];
			std::int32_t playerIndex = SDL_JoystickGetPlayerIndex(joy);
			const SDL_JoystickGUID joystickGuid = SDL_JoystickGetGUID(joy);
			SDL_JoystickGetGUIDString(joystickGuid, joyGuidString_, 33);
			LOGI("Gamepad {} [{}] \"{}\" [{}] has been {} - {} hats, {} axes, {} buttons, {} balls",
					deviceIndex, playerIndex, SDL_JoystickName(joy), joyGuidString_, prevInstance != nullptr ? "reattached" : "connected",
					SDL_JoystickNumHats(joy), SDL_JoystickNumAxes(joy), SDL_JoystickNumButtons(joy), SDL_JoystickNumBalls(joy));
#	endif
#endif
			joyMapping_.OnJoyConnected(joyConnectionEvent_);
			inputEventHandler_->OnJoyConnected(joyConnectionEvent_);
		} else if (event.type == SDL_JOYDEVICEREMOVED) {
			const std::int32_t deviceIndex = joyInstanceIdToDeviceIndex(event.jdevice.which);
			if (deviceIndex == -1) {
				return;
			}

			joyConnectionEvent_.joyId = deviceIndex;
			SDL_JoystickClose(sdlJoysticks_[deviceIndex]);
			sdlJoysticks_[deviceIndex] = nullptr;

			// Compacting the array of SDL joystick pointers
			for (std::int32_t i = deviceIndex; i < MaxNumJoysticks - 1; i++) {
				sdlJoysticks_[i] = sdlJoysticks_[i + 1];
			}
			sdlJoysticks_[MaxNumJoysticks - 1] = nullptr;

			LOGI("Gamepad {} has been disconnected", deviceIndex);
			inputEventHandler_->OnJoyDisconnected(joyConnectionEvent_);
			joyMapping_.OnJoyDisconnected(joyConnectionEvent_);
		}
	}

	int SdlInputManager::joyInstanceIdToDeviceIndex(SDL_JoystickID instanceId)
	{
		std::int32_t deviceIndex = -1;
		for (std::int32_t i = 0; i < MaxNumJoysticks; i++) {
			SDL_JoystickID id = SDL_JoystickInstanceID(sdlJoysticks_[i]);
			if (instanceId == id) {
				deviceIndex = i;
				break;
			}
		}
		return deviceIndex;
	}
}

#endif