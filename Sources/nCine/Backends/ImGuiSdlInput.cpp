// Based on "imgui/backends/imgui_impl_sdl.cpp"
#if defined(WITH_IMGUI) && defined(WITH_SDL)

#include "ImGuiSdlInput.h"
#include "SdlGfxDevice.h"
#include "../Input/ImGuiJoyMappedInput.h"

#if defined(__HAS_LOCAL_SDL)
#	include "SDL2/SDL_syswm.h"
#else
#	include <SDL_syswm.h>
#endif

#if SDL_VERSION_ATLEAST(2, 0, 4) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_ANDROID) && !(defined(DEATH_TARGET_APPLE) && TARGET_OS_IOS)
#	define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE 1
#else
#	define SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE 0
#endif
#define SDL_HAS_PER_MONITOR_DPI	SDL_VERSION_ATLEAST(2, 0, 4)
#define SDL_HAS_ALWAYS_ON_TOP	SDL_VERSION_ATLEAST(2, 0, 5)
#define SDL_HAS_USABLE_DISPLAY_BOUNDS	SDL_VERSION_ATLEAST(2, 0, 5)
#define SDL_HAS_VULKAN			SDL_VERSION_ATLEAST(2, 0, 6)
#define SDL_HAS_WINDOW_ALPHA	SDL_VERSION_ATLEAST(2, 0, 5)
#define SDL_HAS_DISPLAY_EVENT	SDL_VERSION_ATLEAST(2, 0, 9)
#define SDL_HAS_SHOW_WINDOW_ACTIVATION_HINT	SDL_VERSION_ATLEAST(2, 0, 18)

#if SDL_HAS_VULKAN
extern "C" { extern DECLSPEC void SDLCALL SDL_Vulkan_GetDrawableSize(SDL_Window* window, int* w, int* h); }
#endif

#if defined(IMGUI_HAS_VIEWPORT) && defined(DEATH_TARGET_WINDOWS)
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif

namespace nCine::Backends
{
	namespace
	{
		struct ViewportData
		{
			SDL_Window* Window;
			Uint32 WindowID;       // Stored in ImGuiViewport::PlatformHandle. Use SDL_GetWindowFromID() to get SDL_Window* from Uint32 WindowID.
			bool WindowOwned;
			SDL_GLContext GLContext;

			ViewportData() : Window(nullptr), WindowID(0), WindowOwned(false), GLContext(nullptr)
			{
			}

			~ViewportData()
			{
				IM_ASSERT(Window == nullptr && GLContext == nullptr);
			}
		};

#if defined(IMGUI_HAS_VIEWPORT)
#	if defined(DEATH_TARGET_WINDOWS)
		HICON windowIcon = NULL;
		HICON windowIconSmall = NULL;
#	endif

		static ImGuiViewport* getViewportForWindowID(Uint32 windowId)
		{
			return ImGui::FindViewportByPlatformHandle((void*)(intptr_t)windowId);
		}
#endif

		void setClipboardText(ImGuiContext* ctx, const char* text)
		{
			SDL_SetClipboardText(text);
		}

		// Note: native IME will only display if user calls SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1") _before_ SDL_CreateWindow().
		void setPlatformImeData(ImGuiContext* ctx, ImGuiViewport* viewport, ImGuiPlatformImeData* data)
		{
			if (data->WantVisible) {
				SDL_Rect r;
				r.x = static_cast<int>(data->InputPos.x);
				r.y = static_cast<int>(data->InputPos.y);
				r.w = 1;
				r.h = static_cast<int>(data->InputLineHeight);
				SDL_SetTextInputRect(&r);
			}
		}

		ImGuiKey sdlKeycodeToImGuiKey(int keycode)
		{
			switch (keycode) {
				case SDLK_TAB: return ImGuiKey_Tab;
				case SDLK_LEFT: return ImGuiKey_LeftArrow;
				case SDLK_RIGHT: return ImGuiKey_RightArrow;
				case SDLK_UP: return ImGuiKey_UpArrow;
				case SDLK_DOWN: return ImGuiKey_DownArrow;
				case SDLK_PAGEUP: return ImGuiKey_PageUp;
				case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
				case SDLK_HOME: return ImGuiKey_Home;
				case SDLK_END: return ImGuiKey_End;
				case SDLK_INSERT: return ImGuiKey_Insert;
				case SDLK_DELETE: return ImGuiKey_Delete;
				case SDLK_BACKSPACE: return ImGuiKey_Backspace;
				case SDLK_SPACE: return ImGuiKey_Space;
				case SDLK_RETURN: return ImGuiKey_Enter;
				case SDLK_ESCAPE: return ImGuiKey_Escape;
				case SDLK_QUOTE: return ImGuiKey_Apostrophe;
				case SDLK_COMMA: return ImGuiKey_Comma;
				case SDLK_MINUS: return ImGuiKey_Minus;
				case SDLK_PERIOD: return ImGuiKey_Period;
				case SDLK_SLASH: return ImGuiKey_Slash;
				case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
				case SDLK_EQUALS: return ImGuiKey_Equal;
				case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
				case SDLK_BACKSLASH: return ImGuiKey_Backslash;
				case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
				case SDLK_BACKQUOTE: return ImGuiKey_GraveAccent;
				case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
				case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
				case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
				case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
				case SDLK_PAUSE: return ImGuiKey_Pause;
				case SDLK_KP_0: return ImGuiKey_Keypad0;
				case SDLK_KP_1: return ImGuiKey_Keypad1;
				case SDLK_KP_2: return ImGuiKey_Keypad2;
				case SDLK_KP_3: return ImGuiKey_Keypad3;
				case SDLK_KP_4: return ImGuiKey_Keypad4;
				case SDLK_KP_5: return ImGuiKey_Keypad5;
				case SDLK_KP_6: return ImGuiKey_Keypad6;
				case SDLK_KP_7: return ImGuiKey_Keypad7;
				case SDLK_KP_8: return ImGuiKey_Keypad8;
				case SDLK_KP_9: return ImGuiKey_Keypad9;
				case SDLK_KP_PERIOD: return ImGuiKey_KeypadDecimal;
				case SDLK_KP_DIVIDE: return ImGuiKey_KeypadDivide;
				case SDLK_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
				case SDLK_KP_MINUS: return ImGuiKey_KeypadSubtract;
				case SDLK_KP_PLUS: return ImGuiKey_KeypadAdd;
				case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
				case SDLK_KP_EQUALS: return ImGuiKey_KeypadEqual;
				case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
				case SDLK_LSHIFT: return ImGuiKey_LeftShift;
				case SDLK_LALT: return ImGuiKey_LeftAlt;
				case SDLK_LGUI: return ImGuiKey_LeftSuper;
				case SDLK_RCTRL: return ImGuiKey_RightCtrl;
				case SDLK_RSHIFT: return ImGuiKey_RightShift;
				case SDLK_RALT: return ImGuiKey_RightAlt;
				case SDLK_RGUI: return ImGuiKey_RightSuper;
				case SDLK_APPLICATION: return ImGuiKey_Menu;
				case SDLK_0: return ImGuiKey_0;
				case SDLK_1: return ImGuiKey_1;
				case SDLK_2: return ImGuiKey_2;
				case SDLK_3: return ImGuiKey_3;
				case SDLK_4: return ImGuiKey_4;
				case SDLK_5: return ImGuiKey_5;
				case SDLK_6: return ImGuiKey_6;
				case SDLK_7: return ImGuiKey_7;
				case SDLK_8: return ImGuiKey_8;
				case SDLK_9: return ImGuiKey_9;
				case SDLK_a: return ImGuiKey_A;
				case SDLK_b: return ImGuiKey_B;
				case SDLK_c: return ImGuiKey_C;
				case SDLK_d: return ImGuiKey_D;
				case SDLK_e: return ImGuiKey_E;
				case SDLK_f: return ImGuiKey_F;
				case SDLK_g: return ImGuiKey_G;
				case SDLK_h: return ImGuiKey_H;
				case SDLK_i: return ImGuiKey_I;
				case SDLK_j: return ImGuiKey_J;
				case SDLK_k: return ImGuiKey_K;
				case SDLK_l: return ImGuiKey_L;
				case SDLK_m: return ImGuiKey_M;
				case SDLK_n: return ImGuiKey_N;
				case SDLK_o: return ImGuiKey_O;
				case SDLK_p: return ImGuiKey_P;
				case SDLK_q: return ImGuiKey_Q;
				case SDLK_r: return ImGuiKey_R;
				case SDLK_s: return ImGuiKey_S;
				case SDLK_t: return ImGuiKey_T;
				case SDLK_u: return ImGuiKey_U;
				case SDLK_v: return ImGuiKey_V;
				case SDLK_w: return ImGuiKey_W;
				case SDLK_x: return ImGuiKey_X;
				case SDLK_y: return ImGuiKey_Y;
				case SDLK_z: return ImGuiKey_Z;
				case SDLK_F1: return ImGuiKey_F1;
				case SDLK_F2: return ImGuiKey_F2;
				case SDLK_F3: return ImGuiKey_F3;
				case SDLK_F4: return ImGuiKey_F4;
				case SDLK_F5: return ImGuiKey_F5;
				case SDLK_F6: return ImGuiKey_F6;
				case SDLK_F7: return ImGuiKey_F7;
				case SDLK_F8: return ImGuiKey_F8;
				case SDLK_F9: return ImGuiKey_F9;
				case SDLK_F10: return ImGuiKey_F10;
				case SDLK_F11: return ImGuiKey_F11;
				case SDLK_F12: return ImGuiKey_F12;
			}
			return ImGuiKey_None;
		}

		void updateKeyModifiers(SDL_Keymod sdlKeyMods)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, (sdlKeyMods & KMOD_CTRL) != 0);
			io.AddKeyEvent(ImGuiMod_Shift, (sdlKeyMods & KMOD_SHIFT) != 0);
			io.AddKeyEvent(ImGuiMod_Alt, (sdlKeyMods & KMOD_ALT) != 0);
			io.AddKeyEvent(ImGuiMod_Super, (sdlKeyMods & KMOD_GUI) != 0);
		}

		void updateGamepadButton(ImVector<SDL_GameController*>& gamepads, ImGuiIO& io, ImGuiKey key, SDL_GameControllerButton buttonNo)
		{
			bool mergedValue = false;
			for (SDL_GameController* gamepad : gamepads)
				mergedValue |= SDL_GameControllerGetButton(gamepad, buttonNo) != 0;
			io.AddKeyEvent(key, mergedValue);
		}

		float saturate(float v)
		{
			return (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
		}

		void updateGamepadAnalog(ImVector<SDL_GameController*>& gamepads, ImGuiIO& io, ImGuiKey key, SDL_GameControllerAxis axisNo, float v0, float v1)
		{
			float mergedValue = 0.0f;
			for (SDL_GameController* gamepad : gamepads) {
				const float vn = saturate(static_cast<float>(SDL_GameControllerGetAxis(gamepad, axisNo) - v0) / static_cast<float>(v1 - v0));
				if (mergedValue < vn) {
					mergedValue = vn;
				}
			}
			io.AddKeyAnalogEvent(key, mergedValue > 0.1f, mergedValue);
		}
	}

	bool ImGuiSdlInput::inputEnabled_ = true;
	SDL_Window* ImGuiSdlInput::window_ = nullptr;
	unsigned long int ImGuiSdlInput::time_ = 0;
	char* ImGuiSdlInput::clipboardTextData_ = nullptr;
	bool ImGuiSdlInput::wantUpdateMonitors_ = false;
	unsigned int ImGuiSdlInput::mouseWindowID_ = 0;
	int ImGuiSdlInput::mouseButtonsDown_ = 0;
	SDL_Cursor* ImGuiSdlInput::mouseCursors_[ImGuiMouseCursor_COUNT] = {};
	SDL_Cursor* ImGuiSdlInput::mouseLastCursor_ = nullptr;
	unsigned int ImGuiSdlInput::mouseLastLeaveFrame_ = 0;
	bool ImGuiSdlInput::mouseCanUseGlobalState_ = false;

	ImVector<SDL_GameController*> ImGuiSdlInput::gamepads_;
	ImGuiSdlInput::GamepadMode ImGuiSdlInput::gamepadMode_ = ImGuiSdlInput::GamepadMode::AUTO_FIRST;
	bool ImGuiSdlInput::wantUpdateGamepadsList_ = false;

	void ImGuiSdlInput::init(SDL_Window* window, SDL_GLContext glContextHandle)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		ImGuiPlatformIO& pio = ImGui::GetPlatformIO();

		// Check and store if we are on a SDL backend that supports global mouse position
		// ("wayland" and "rpi" don't support it, but we chose to use a white-list instead of a black-list)
		mouseCanUseGlobalState_ = false;
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
		const char* sdlBackend = SDL_GetCurrentVideoDriver();
		static const char* globalMouseWhitelist[] = { "windows", "cocoa", "x11", "DIVE", "VMAN" };
		for (int n = 0; n < IM_ARRAYSIZE(globalMouseWhitelist); n++) {
			if (strncmp(sdlBackend, globalMouseWhitelist[n], strlen(globalMouseWhitelist[n])) == 0) {
				mouseCanUseGlobalState_ = true;
				break;
			}
		}
#endif

		// Setup backend capabilities flags
		io.BackendPlatformName = "nCine_SDL2";
		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors; // We can honor GetMouseCursor() values (optional)
		io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos; // We can honor io.WantSetMousePos requests (optional, rarely used)

		window_ = window;

		pio.Platform_SetClipboardTextFn = setClipboardText;
		pio.Platform_GetClipboardTextFn = clipboardText;
		pio.Platform_ClipboardUserData = nullptr;
		pio.Platform_SetImeDataFn = setPlatformImeData;

#if defined(IMGUI_HAS_DOCK)
		// Update monitor a first time during init
		updateMonitors();
#endif

		// Gamepad handling
		gamepadMode_ = GamepadMode::AUTO_FIRST;
		wantUpdateGamepadsList_ = true;

		// Load mouse cursors
		mouseCursors_[ImGuiMouseCursor_Arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
		mouseCursors_[ImGuiMouseCursor_TextInput] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
		mouseCursors_[ImGuiMouseCursor_ResizeAll] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
		mouseCursors_[ImGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
		mouseCursors_[ImGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
		mouseCursors_[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
		mouseCursors_[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
		mouseCursors_[ImGuiMouseCursor_Hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
		mouseCursors_[ImGuiMouseCursor_NotAllowed] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NO);

		// Set platform dependent data in viewport
		// Our mouse update function expect PlatformHandle to be filled for the main viewport
		ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		mainViewport->PlatformHandleRaw = nullptr;
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);
		if (SDL_GetWindowWMInfo(window, &info)) {
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
			mainViewport->PlatformHandleRaw = (void*)info.info.win.window;
#elif defined(DEATH_TARGET_APPLE) && defined(SDL_VIDEO_DRIVER_COCOA)
			mainViewport->PlatformHandleRaw = (void*)info.info.cocoa.window;
#endif
		}

		// From 2.0.5: Set SDL hint to receive mouse click events on window focus, otherwise SDL doesn't emit the event.
		// Without this, when clicking to gain focus, our widgets wouldn't activate even though they showed as hovered.
		// (This is unfortunately a global SDL setting, so enabling it might have a side-effect on your application.
		// It is unlikely to make a difference, but if your app absolutely needs to ignore the initial on-focus click:
		// you can ignore SDL_MOUSEBUTTONDOWN events coming right after a SDL_WINDOWEVENT_FOCUS_GAINED)
#if defined(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH)
		SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

		// From 2.0.18: Enable native IME.
		// IMPORTANT: This is used at the time of SDL_CreateWindow() so this will only affects secondary windows, if any.
		// For the main window to be affected, your application needs to call this manually before calling SDL_CreateWindow().
#if defined(SDL_HINT_IME_SHOW_UI)
		SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

		// From 2.0.22: Disable auto-capture, this is preventing drag and drop across multiple windows (see #5710)
#if defined(SDL_HINT_MOUSE_AUTO_CAPTURE)
		SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
#endif

#if defined(IMGUI_HAS_VIEWPORT)
		if (mouseCanUseGlobalState_) {
			io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;

			// Register platform interface (will be coupled with a renderer interface)
			pio.Platform_CreateWindow = onCreateWindow;
			pio.Platform_DestroyWindow = onDestroyWindow;
			pio.Platform_ShowWindow = onShowWindow;
			pio.Platform_SetWindowPos = onSetWindowPos;
			pio.Platform_GetWindowPos = onGetWindowPos;
			pio.Platform_SetWindowSize = onSetWindowSize;
			pio.Platform_GetWindowSize = onGetWindowSize;
			pio.Platform_GetWindowFramebufferScale = onGetWindowFramebufferScale;
			pio.Platform_SetWindowFocus = onSetWindowFocus;
			pio.Platform_GetWindowFocus = onGetWindowFocus;
			pio.Platform_GetWindowMinimized = onGetWindowMinimized;
			pio.Platform_SetWindowTitle = onSetWindowTitle;
			pio.Platform_RenderWindow = onRenderWindow;
			pio.Platform_SwapBuffers = onSwapBuffers;
#	if SDL_HAS_WINDOW_ALPHA
			pio.Platform_SetWindowAlpha = onSetWindowAlpha;
#	endif
#	if SDL_HAS_VULKAN
			//pio.Platform_CreateVkSurface = onCreateVkSurface;
#	endif

			// Register main window handle (which is owned by the main application, not by us)
			// This is mostly for simplicity and consistency, so that our code (e.g. mouse handling etc.) can use same logic for main and secondary viewports.
			ImGuiViewport* mainViewport = ImGui::GetMainViewport();
			ViewportData* vd = IM_NEW(ViewportData)();
			vd->Window = window;
			vd->WindowID = SDL_GetWindowID(window);
			vd->WindowOwned = false;
			vd->GLContext = glContextHandle;
			mainViewport->PlatformUserData = vd;
			mainViewport->PlatformHandle = (void*)(std::intptr_t)vd->WindowID;

#	if defined(DEATH_TARGET_WINDOWS)
			HINSTANCE inst = ((HINSTANCE)&__ImageBase);
			windowIcon = (HICON)::LoadImage(inst, L"IMGUI_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
			windowIconSmall = (HICON)::LoadImage(inst, L"IMGUI_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
#	endif
		}
#endif
	}

	void ImGuiSdlInput::shutdown()
	{
#if defined(IMGUI_HAS_VIEWPORT)
		ImGui::DestroyPlatformWindows();
#endif

		window_ = nullptr;

		// Destroy last known clipboard data
		if (clipboardTextData_)
			SDL_free(clipboardTextData_);
		clipboardTextData_ = nullptr;

		// Destroy SDL mouse cursors
		for (ImGuiMouseCursor i = 0; i < ImGuiMouseCursor_COUNT; i++)
			SDL_FreeCursor(mouseCursors_[i]);
		closeGamepads();

		ImGuiIO& io = ImGui::GetIO();
		io.BackendPlatformName = nullptr;
		io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad);

		ImGui::DestroyContext();
	}

	void ImGuiSdlInput::newFrame()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Setup main viewport size (every frame to accommodate for window resizing)
		getWindowSizeAndFramebufferScale(window_, nullptr, &io.DisplaySize, &io.DisplayFramebufferScale);

#if defined(IMGUI_HAS_DOCK)
#	if !defined(DEATH_TARGET_WINDOWS)
		// Keep polling under Windows to handle changes of work area when resizing taskbar
		if (wantUpdateMonitors_)
#	endif
			updateMonitors();
#endif

		// Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
		// (Accept SDL_GetPerformanceCounter() not returning a monotonically increasing value. Happens in VMs and Emscripten, see #6189, #6114, #3644)
		static Uint64 frequency = SDL_GetPerformanceFrequency();
		Uint64 currentTime = SDL_GetPerformanceCounter();
		if (currentTime <= time_)
			currentTime = time_ + 1;
		io.DeltaTime = time_ > 0 ? static_cast<float>((static_cast<double>(currentTime - time_) / frequency)) : static_cast<float>(1.0f / 60.0f);
		time_ = currentTime;

		if (mouseLastLeaveFrame_ && mouseLastLeaveFrame_ >= ImGui::GetFrameCount() && mouseButtonsDown_ == 0) {
			mouseWindowID_ = 0;
			mouseLastLeaveFrame_ = 0;
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		}

		updateMouseData();
		updateMouseCursor();

		// Update game controllers (if enabled and available)
		updateGamepads();
	}

	void ImGuiSdlInput::endFrame()
	{
#if defined(IMGUI_HAS_VIEWPORT)
		// Update and render additional Platform Windows
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

			// Restore the OpenGL rendering context to the main window DC, since platform windows might have changed it.
			ImGuiViewport* mainViewport = ImGui::GetMainViewport();
			ViewportData* mainViewportData = (ViewportData*)mainViewport->PlatformUserData;
			SDL_GL_MakeCurrent(mainViewportData->Window, mainViewportData->GLContext);
		}
#endif
	}

	// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
	// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
	// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
	// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
	// If you have multiple SDL events and some of them are not meant to be used by dear imgui, you may need to filter events based on their windowID field.
	bool ImGuiSdlInput::processEvent(const SDL_Event* event)
	{
		if (!inputEnabled_)
			return false;

		ImGuiIO& io = ImGui::GetIO();
		switch (event->type) {
			case SDL_MOUSEMOTION: {
				if (getViewportForWindowID(event->motion.windowID) == nullptr)
					return false;
				ImVec2 mousePos(static_cast<float>(event->motion.x), static_cast<float>(event->motion.y));
#if defined(IMGUI_HAS_VIEWPORT)
				if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
					int windowX, windowY;
					SDL_GetWindowPosition(SDL_GetWindowFromID(event->motion.windowID), &windowX, &windowY);
					mousePos.x += windowX;
					mousePos.y += windowY;
				}
#endif
				io.AddMouseSourceEvent(event->motion.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
				io.AddMousePosEvent(mousePos.x, mousePos.y);
				return true;
			}
			case SDL_MOUSEWHEEL: {
				if (getViewportForWindowID(event->wheel.windowID) == nullptr)
					return false;
#if SDL_VERSION_ATLEAST(2, 0, 18) // If this fails to compile on Emscripten: update to latest Emscripten!
				float wheelX = -event->wheel.preciseX;
				const float wheelY = event->wheel.preciseY;
#else
				float wheelX = static_cast<float>(-event->wheel.x);
				const float wheelY = static_cast<float>(event->wheel.y);
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
				wheelX /= 100.0f;
#endif
				io.AddMouseSourceEvent(event->wheel.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
				io.AddMouseWheelEvent(wheelX, wheelY);
				return true;
			}
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP: {
				if (getViewportForWindowID(event->button.windowID) == nullptr)
					return false;
				int mouseButton;
				switch (event->button.button) {
					case SDL_BUTTON_LEFT: mouseButton = 0; break;
					case SDL_BUTTON_RIGHT: mouseButton = 1; break;
					case SDL_BUTTON_MIDDLE: mouseButton = 2; break;
					case SDL_BUTTON_X1: mouseButton = 3; break;
					case SDL_BUTTON_X2: mouseButton = 4; break;
					default: mouseButton = -1; break;
				}
				if (mouseButton == -1) {
					break;
				}

				io.AddMouseSourceEvent(event->wheel.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
				io.AddMouseButtonEvent(mouseButton, (event->type == SDL_MOUSEBUTTONDOWN));
				mouseButtonsDown_ = (event->type == SDL_MOUSEBUTTONDOWN) ? (mouseButtonsDown_ | (1 << mouseButton)) : (mouseButtonsDown_ & ~(1 << mouseButton));
				return true;
			}
			case SDL_TEXTINPUT: {
				if (getViewportForWindowID(event->text.windowID) == nullptr)
					return false;
				io.AddInputCharactersUTF8(event->text.text);
				return true;
			}
			case SDL_KEYDOWN:
			case SDL_KEYUP: {
				if (getViewportForWindowID(event->key.windowID) == nullptr)
					return false;
				updateKeyModifiers(static_cast<SDL_Keymod>(event->key.keysym.mod));
				const ImGuiKey key = sdlKeycodeToImGuiKey(event->key.keysym.sym);
				io.AddKeyEvent(key, (event->type == SDL_KEYDOWN));
				// To support legacy indexing (<1.87 user code). Legacy backend uses SDLK_*** as indices to IsKeyXXX() functions.
				io.SetKeyEventNativeData(key, event->key.keysym.sym, event->key.keysym.scancode, event->key.keysym.scancode);
				return true;
			}
#if SDL_HAS_DISPLAY_EVENT
			case SDL_DISPLAYEVENT:
			{
				// 2.0.26 has SDL_DISPLAYEVENT_CONNECTED/SDL_DISPLAYEVENT_DISCONNECTED/SDL_DISPLAYEVENT_ORIENTATION,
				// so change of DPI/Scaling are not reflected in this event. (SDL3 has it)
				wantUpdateMonitors_ = true;
				return true;
			}
#endif
			case SDL_WINDOWEVENT: {
				// - When capturing mouse, SDL will send a bunch of conflicting LEAVE/ENTER event on every mouse move, but the final ENTER tends to be right.
				// - However we won't get a correct LEAVE event for a captured window.
				// - In some cases, when detaching a window from main viewport SDL may send SDL_WINDOWEVENT_ENTER one frame too late,
				//   causing SDL_WINDOWEVENT_LEAVE on previous frame to interrupt drag operation by clear mouse position. This is why
				//   we delay process the SDL_WINDOWEVENT_LEAVE events by one frame. See issue #5012 for details.
#if defined(IMGUI_HAS_VIEWPORT)
				ImGuiViewport* viewport = getViewportForWindowID(event->window.windowID);
				if (viewport == nullptr) {
					return false;
				}
#endif
				const Uint8 windowEvent = event->window.event;
				if (windowEvent == SDL_WINDOWEVENT_ENTER) {
					mouseWindowID_ = event->window.windowID;
					mouseLastLeaveFrame_ = 0;
				} else if (windowEvent == SDL_WINDOWEVENT_LEAVE) {
					mouseLastLeaveFrame_ = ImGui::GetFrameCount() + 1;
				} else if (windowEvent == SDL_WINDOWEVENT_FOCUS_GAINED) {
					io.AddFocusEvent(true);
				} else if (windowEvent == SDL_WINDOWEVENT_FOCUS_LOST) {
					io.AddFocusEvent(false);
				}
#if defined(IMGUI_HAS_VIEWPORT)
				else if (windowEvent == SDL_WINDOWEVENT_CLOSE) {
					viewport->PlatformRequestClose = true;
				} else if (windowEvent == SDL_WINDOWEVENT_MOVED) {
					viewport->PlatformRequestMove = true;
				} else if (windowEvent == SDL_WINDOWEVENT_RESIZED) {
					viewport->PlatformRequestResize = true;
				}
#endif
				return true;
			}
			case SDL_CONTROLLERDEVICEADDED:
			case SDL_CONTROLLERDEVICEREMOVED: {
				wantUpdateGamepadsList_ = true;
				return true;
			}
		}
		return false;
	}

	const char* ImGuiSdlInput::clipboardText(ImGuiContext* ctx)
	{
		if (clipboardTextData_ != nullptr) {
			SDL_free(clipboardTextData_);
		}
		clipboardTextData_ = SDL_GetClipboardText();
		return clipboardTextData_;
	}

	void ImGuiSdlInput::updateMouseData()
	{
		ImGuiIO& io = ImGui::GetIO();

		// We forward mouse input when hovered or captured (via SDL_MOUSEMOTION) or when focused (below)
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
		// - SDL_CaptureMouse() let the OS know e.g. that our drags can extend outside of parent boundaries (we want updated position) and shouldn't trigger other operations outside.
		// - Debuggers under Linux tends to leave captured mouse on break, which may be very inconvenient, so to mitigate the issue we wait until mouse has moved to begin capture.
		if (mouseCanUseGlobalState_) {
			bool wantCapture = false;
			for (int buttonN = 0; buttonN < ImGuiMouseButton_COUNT && !wantCapture; buttonN++) {
				if (ImGui::IsMouseDragging(buttonN, 1.0f))
					wantCapture = true;
			}
			SDL_CaptureMouse(wantCapture ? SDL_TRUE : SDL_FALSE);
		}

		SDL_Window* focusedWindow = SDL_GetKeyboardFocus();
		const bool isAppFocused = (window_ == focusedWindow);
#else
		SDL_Window* focusedWindow = window_;
		const bool isAppFocused = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0; // SDL 2.0.3 and non-windowed systems: single-viewport only
#endif

		if (isAppFocused) {
			// (Optional) Set OS mouse position from Dear ImGui if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
			if (io.WantSetMousePos) {
#if SDL_HAS_CAPTURE_AND_GLOBAL_MOUSE
				if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
					SDL_WarpMouseGlobal((int)io.MousePos.x, (int)io.MousePos.y);
				else
#endif
					SDL_WarpMouseInWindow(window_, (int)io.MousePos.x, (int)io.MousePos.y);
			}

			// (Optional) Fallback to provide mouse position when focused (SDL_MOUSEMOTION already provides this when hovered or captured)
			const bool isRelativeMouseMode = SDL_GetRelativeMouseMode() != 0;
			if (mouseCanUseGlobalState_ && mouseButtonsDown_ == 0 && !isRelativeMouseMode) {
				int windowX, windowY, mouseXGlobal, mouseYGlobal;
				SDL_GetGlobalMouseState(&mouseXGlobal, &mouseYGlobal);
				SDL_GetWindowPosition(window_, &windowX, &windowY);
#if defined(IMGUI_HAS_VIEWPORT)
				if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
					SDL_GetWindowPosition(focusedWindow, &windowX, &windowY);
					mouseXGlobal -= windowX;
					mouseYGlobal -= windowY;
				}
#endif
				io.AddMousePosEvent(static_cast<float>(mouseXGlobal), static_cast<float>(mouseYGlobal));
			}
		}

#if defined(IMGUI_HAS_VIEWPORT)
		if (io.BackendFlags & ImGuiBackendFlags_HasMouseHoveredViewport) {
			ImGuiID mouseViewportId = 0;
			if (ImGuiViewport* mouseViewport = getViewportForWindowID(mouseWindowID_)) {
				mouseViewportId = mouseViewport->ID;
			}
			io.AddMouseViewportEvent(mouseViewportId);
		}
#endif
	}

	void ImGuiSdlInput::updateMouseCursor()
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) {
			return;
		}

		ImGuiMouseCursor imguiCursor = ImGui::GetMouseCursor();
		if (io.MouseDrawCursor || imguiCursor == ImGuiMouseCursor_None) {
			// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
			SDL_ShowCursor(SDL_FALSE);
		} else {
			// Show OS mouse cursor
			SDL_Cursor* expectedCursor = (mouseCursors_[imguiCursor] ? mouseCursors_[imguiCursor] : mouseCursors_[ImGuiMouseCursor_Arrow]);
			if (mouseLastCursor_ != expectedCursor) {
				SDL_SetCursor(expectedCursor); // SDL function doesn't have an early out (see #6113)
				mouseLastCursor_ = expectedCursor;
			}
			SDL_ShowCursor(SDL_TRUE);
		}
	}

	// - On Windows the process needs to be marked DPI-aware!! SDL2 doesn't do it by default. You can call ::SetProcessDPIAware() or call ImGui_ImplWin32_EnableDpiAwareness() from Win32 backend.
	// - Apple platforms use FramebufferScale so we always return 1.0f.
	// - Some accessibility applications are declaring virtual monitors with a DPI of 0.0f, see #7902. We preserve this value for caller to handle.
	float ImGuiSdlInput::getContentScaleForWindow(SDL_Window* window)
	{
		return getContentScaleForDisplay(SDL_GetWindowDisplayIndex(window));
	}

	float ImGuiSdlInput::getContentScaleForDisplay(int displayIndex)
	{
#if SDL_HAS_PER_MONITOR_DPI
#	if !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_ANDROID)
		float dpi = 0.0f;
		if (SDL_GetDisplayDPI(displayIndex, &dpi, nullptr, nullptr) == 0) {
			return dpi / 96.0f;
		}
#	endif
#endif
		IM_UNUSED(displayIndex);
		return 1.0f;
	}

	void ImGuiSdlInput::closeGamepads()
	{
		if (gamepadMode_ != GamepadMode::MANUAL) {
			for (SDL_GameController* gamepad : gamepads_) {
				SDL_GameControllerClose(gamepad);
			}
			gamepads_.resize(0);
		}
	}

	void ImGuiSdlInput::setGamepadMode(GamepadMode mode, SDL_GameController** manualGamepadsArray, unsigned int manualGamepadsCount)
	{
		closeGamepads();
		if (mode == GamepadMode::MANUAL) {
			IM_ASSERT(manualGamepadsArray != nullptr && manualGamepadsCount > 0);
			for (unsigned int n = 0; n < manualGamepadsCount; n++) {
				gamepads_.push_back(manualGamepadsArray[n]);
			}
		} else {
			IM_ASSERT(manualGamepadsArray == nullptr && manualGamepadsCount == 0);
			wantUpdateGamepadsList_ = true;
		}
		gamepadMode_ = mode;
	}

	void ImGuiSdlInput::updateGamepads()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Update list of controller(s) to use
		if (wantUpdateGamepadsList_ && gamepadMode_ != GamepadMode::MANUAL) {
			closeGamepads();
			const int joystickCount = SDL_NumJoysticks();
			for (int n = 0; n < joystickCount; n++) {
				if (SDL_IsGameController(n)) {
					if (SDL_GameController* gamepad = SDL_GameControllerOpen(n)) {
						gamepads_.push_back(gamepad);
						if (gamepadMode_ == GamepadMode::AUTO_FIRST)
							break;
					}
				}
			}
			wantUpdateGamepadsList_ = false;
		}

		// FIXME: Technically feeding gamepad shouldn't depend on this now that they are regular inputs.
		if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0)
			return;
		io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
		if (gamepads_.Size == 0)
			return;
		io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

		// No need to call `imGuiJoyMappedInput()` as the backend is using `SDL_GameController`

		// Update gamepad inputs
		// clang-format off
		const int thumbDeadZone = 8000; // SDL_gamecontroller.h suggests using this value.
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadStart, SDL_CONTROLLER_BUTTON_START);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadBack, SDL_CONTROLLER_BUTTON_BACK);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadFaceLeft, SDL_CONTROLLER_BUTTON_X);		// Xbox X, PS Square
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadFaceRight, SDL_CONTROLLER_BUTTON_B);		// Xbox B, PS Circle
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadFaceUp, SDL_CONTROLLER_BUTTON_Y);		// Xbox Y, PS Triangle
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadFaceDown, SDL_CONTROLLER_BUTTON_A);		// Xbox A, PS Cross
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadDpadLeft, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadDpadRight, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadDpadUp, SDL_CONTROLLER_BUTTON_DPAD_UP);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadDpadDown, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadL1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadR1, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadL2, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0.0f, 32767);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadR2, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.0f, 32767);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadL3, SDL_CONTROLLER_BUTTON_LEFTSTICK);
		updateGamepadButton(gamepads_, io, ImGuiKey_GamepadR3, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadLStickLeft, SDL_CONTROLLER_AXIS_LEFTX, -thumbDeadZone, -32768);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadLStickRight, SDL_CONTROLLER_AXIS_LEFTX, +thumbDeadZone, +32767);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadLStickUp, SDL_CONTROLLER_AXIS_LEFTY, -thumbDeadZone, -32768);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadLStickDown, SDL_CONTROLLER_AXIS_LEFTY, +thumbDeadZone, +32767);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadRStickLeft, SDL_CONTROLLER_AXIS_RIGHTX, -thumbDeadZone, -32768);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadRStickRight, SDL_CONTROLLER_AXIS_RIGHTX, +thumbDeadZone, +32767);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadRStickUp, SDL_CONTROLLER_AXIS_RIGHTY, -thumbDeadZone, -32768);
		updateGamepadAnalog(gamepads_, io, ImGuiKey_GamepadRStickDown, SDL_CONTROLLER_AXIS_RIGHTY, +thumbDeadZone, +32767);
		// clang-format on
	}

	void ImGuiSdlInput::getWindowSizeAndFramebufferScale(SDL_Window* window, SDL_Renderer* renderer, ImVec2* outSize, ImVec2* outFramebufferScale)
	{
		int w, h;
		int displayW, displayH;
		SDL_GetWindowSize(window, &w, &h);
		if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
			w = h = 0;
		if (renderer != nullptr)
			SDL_GetRendererOutputSize(renderer, &displayW, &displayH);
#if SDL_HAS_VULKAN
		else if (SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN)
			SDL_Vulkan_GetDrawableSize(window, &displayW, &displayH);
#endif
		else
			SDL_GL_GetDrawableSize(window, &displayW, &displayH);

		if (outSize != nullptr)
			*outSize = ImVec2(static_cast<float>(w), static_cast<float>(h));
		if (outFramebufferScale != nullptr)
			*outFramebufferScale = (w > 0 && h > 0) ? ImVec2(static_cast<float>(displayW) / w, static_cast<float>(displayH) / h) : ImVec2(1.0f, 1.0f);
	}

#if defined(IMGUI_HAS_DOCK)
	void ImGuiSdlInput::updateMonitors()
	{
		ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
		pio.Monitors.resize(0);
		wantUpdateMonitors_ = false;
		int display_count = SDL_GetNumVideoDisplays();
		for (int n = 0; n < display_count; n++) {
			// Warning: the validity of monitor DPI information on Windows depends on the application DPI awareness settings, which generally needs to be set in the manifest or at runtime.
			ImGuiPlatformMonitor monitor;
			SDL_Rect r;
			SDL_GetDisplayBounds(n, &r);
			monitor.MainPos = monitor.WorkPos = ImVec2((float)r.x, (float)r.y);
			monitor.MainSize = monitor.WorkSize = ImVec2((float)r.w, (float)r.h);
#if SDL_HAS_USABLE_DISPLAY_BOUNDS
			if (SDL_GetDisplayUsableBounds(n, &r) == 0 && r.w > 0 && r.h > 0) {
				monitor.WorkPos = ImVec2((float)r.x, (float)r.y);
				monitor.WorkSize = ImVec2((float)r.w, (float)r.h);
			}
#endif
			float dpi_scale = getContentScaleForDisplay(n);
			if (dpi_scale <= 0.0f)
				continue; // Some accessibility applications are declaring virtual monitors with a DPI of 0, see #7902.
			monitor.DpiScale = dpi_scale;
			monitor.PlatformHandle = (void*)(intptr_t)n;
			pio.Monitors.push_back(monitor);
		}
	}
#endif

#if defined(IMGUI_HAS_VIEWPORT)
	void ImGuiSdlInput::onCreateWindow(ImGuiViewport* viewport)
	{
		ViewportData* vd = new ViewportData();
		viewport->PlatformUserData = vd;

		ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		ViewportData* mainViewportData = (ViewportData*)mainViewport->PlatformUserData;

		// Share GL resources with main context
		bool useOpenGL = (mainViewportData->GLContext != nullptr);
		SDL_GLContext backupContext = nullptr;
		if (useOpenGL) {
			backupContext = SDL_GL_GetCurrentContext();
			SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
			SDL_GL_MakeCurrent(mainViewportData->Window, mainViewportData->GLContext);
		}

		Uint32 sdl_flags = 0;
		sdl_flags |= useOpenGL ? SDL_WINDOW_OPENGL : (/*bd->UseVulkan ? SDL_WINDOW_VULKAN :*/ 0);
		sdl_flags |= SDL_GetWindowFlags(window_) & SDL_WINDOW_ALLOW_HIGHDPI;
		sdl_flags |= SDL_WINDOW_HIDDEN;
		sdl_flags |= (viewport->Flags & ImGuiViewportFlags_NoDecoration) ? SDL_WINDOW_BORDERLESS : 0;
		sdl_flags |= (viewport->Flags & ImGuiViewportFlags_NoDecoration) ? 0 : SDL_WINDOW_RESIZABLE;
#	if !defined(DEATH_TARGET_WINDOWS)
		sdl_flags |= (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) ? SDL_WINDOW_SKIP_TASKBAR : 0;
#	endif
#	if SDL_HAS_ALWAYS_ON_TOP
		sdl_flags |= (viewport->Flags & ImGuiViewportFlags_TopMost) ? SDL_WINDOW_ALWAYS_ON_TOP : 0;
#	endif
		vd->Window = SDL_CreateWindow("ImGui", (int)viewport->Pos.x, (int)viewport->Pos.y, (int)viewport->Size.x, (int)viewport->Size.y, sdl_flags);
		vd->WindowOwned = true;
		if (useOpenGL) {
			vd->GLContext = SDL_GL_CreateContext(vd->Window);
			SDL_GL_SetSwapInterval(0);
		}
		if (useOpenGL && backupContext) {
			SDL_GL_MakeCurrent(vd->Window, backupContext);
		}

		viewport->PlatformHandle = (void*)(std::intptr_t)SDL_GetWindowID(vd->Window);
		viewport->PlatformHandleRaw = nullptr;
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);
		if (SDL_GetWindowWMInfo(vd->Window, &info)) {
#	if defined(SDL_VIDEO_DRIVER_WINDOWS)
			viewport->PlatformHandleRaw = info.info.win.window;
#	elif defined(DEATH_TARGET_APPLE) && defined(SDL_VIDEO_DRIVER_COCOA)
			viewport->PlatformHandleRaw = (void*)info.info.cocoa.window;
#	endif

#	if defined(DEATH_TARGET_WINDOWS)
			if (windowIconSmall != NULL) ::SendMessage(info.info.win.window, WM_SETICON, ICON_SMALL, (LPARAM)windowIconSmall);
			if (windowIcon != NULL) ::SendMessage(info.info.win.window, WM_SETICON, ICON_BIG, (LPARAM)windowIcon);
#	endif
		}
	}

	void ImGuiSdlInput::onDestroyWindow(ImGuiViewport* viewport)
	{
		if (ViewportData* vd = (ViewportData*)viewport->PlatformUserData) {
			if (vd->GLContext && vd->WindowOwned) {
				SDL_GL_DeleteContext(vd->GLContext);
			}
			if (vd->Window && vd->WindowOwned) {
				SDL_DestroyWindow(vd->Window);
			}
			vd->GLContext = nullptr;
			vd->Window = nullptr;
			IM_DELETE(vd);
		}
		viewport->PlatformUserData = nullptr;
		viewport->PlatformHandle = nullptr;
	}

	void ImGuiSdlInput::onShowWindow(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;

#	if defined(DEATH_TARGET_WINDOWS) && !(defined(WINAPI_FAMILY) && ((defined(WINAPI_FAMILY_APP) && WINAPI_FAMILY == WINAPI_FAMILY_APP) || (defined(WINAPI_FAMILY_GAMES) && WINAPI_FAMILY == WINAPI_FAMILY_GAMES)))
		HWND hwnd = (HWND)viewport->PlatformHandleRaw;

		// SDL hack: Hide icon from task bar
		// Note: SDL 2.0.6+ has a SDL_WINDOW_SKIP_TASKBAR flag which is supported under Windows but the way it create the window breaks our seamless transition.
		if (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) {
			LONG ex_style = ::GetWindowLong(hwnd, GWL_EXSTYLE);
			ex_style &= ~WS_EX_APPWINDOW;
			ex_style |= WS_EX_TOOLWINDOW;
			::SetWindowLong(hwnd, GWL_EXSTYLE, ex_style);
		}
#	endif

#	if SDL_HAS_SHOW_WINDOW_ACTIVATION_HINT
		SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing) ? "1" : "0");
#	elif defined(DEATH_TARGET_WINDOWS)
		// SDL hack: SDL always activate/focus windows :/
		if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing) {
			::ShowWindow(hwnd, SW_SHOWNA);
			return;
		}
#	endif

		SDL_ShowWindow(vd->Window);
	}

	ImVec2 ImGuiSdlInput::onGetWindowPos(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		int x = 0, y = 0;
		SDL_GetWindowPosition(vd->Window, &x, &y);
		return ImVec2((float)x, (float)y);
	}

	void ImGuiSdlInput::onSetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		SDL_SetWindowPosition(vd->Window, (int)pos.x, (int)pos.y);
	}

	ImVec2 ImGuiSdlInput::onGetWindowSize(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		int w = 0, h = 0;
		SDL_GetWindowSize(vd->Window, &w, &h);
		return ImVec2((float)w, (float)h);
	}

	ImVec2 ImGuiSdlInput::onGetWindowFramebufferScale(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		ImVec2 framebufferScale;
		getWindowSizeAndFramebufferScale(vd->Window, nullptr, nullptr, &framebufferScale);
		return framebufferScale;
	}

	void ImGuiSdlInput::onSetWindowSize(ImGuiViewport* viewport, ImVec2 size)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		SDL_SetWindowSize(vd->Window, (int)size.x, (int)size.y);
	}

	void ImGuiSdlInput::onSetWindowTitle(ImGuiViewport* viewport, const char* title)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		SDL_SetWindowTitle(vd->Window, title);
	}

	void ImGuiSdlInput::onSetWindowFocus(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		SDL_RaiseWindow(vd->Window);
	}

	bool ImGuiSdlInput::onGetWindowFocus(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		return (SDL_GetWindowFlags(vd->Window) & SDL_WINDOW_INPUT_FOCUS) != 0;
	}

	bool ImGuiSdlInput::onGetWindowMinimized(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		return (SDL_GetWindowFlags(vd->Window) & SDL_WINDOW_MINIMIZED) != 0;
	}

	void ImGuiSdlInput::onSetWindowAlpha(ImGuiViewport* viewport, float alpha)
	{
#	if SDL_HAS_WINDOW_ALPHA
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		SDL_SetWindowOpacity(vd->Window, alpha);
#	endif
	}

	void ImGuiSdlInput::onRenderWindow(ImGuiViewport* viewport, void*)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		if (vd->GLContext) {
			SDL_GL_MakeCurrent(vd->Window, vd->GLContext);
		}
	}

	void ImGuiSdlInput::onSwapBuffers(ImGuiViewport* viewport, void*)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		if (vd->GLContext) {
			SDL_GL_MakeCurrent(vd->Window, vd->GLContext);
			SDL_GL_SwapWindow(vd->Window);
		}
	}
#endif
}

#endif