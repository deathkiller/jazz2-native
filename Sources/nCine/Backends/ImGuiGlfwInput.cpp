// Based on "imgui/backends/imgui_impl_glfw.cpp"
#if defined(WITH_IMGUI) && defined(WITH_GLFW)

#include "ImGuiGlfwInput.h"
#include "../Input/ImGuiJoyMappedInput.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten.h>
#	include <emscripten/html5.h>
#elif defined(DEATH_TARGET_APPLE)
#	if !defined(GLFW_EXPOSE_NATIVE_COCOA)
#		define GLFW_EXPOSE_NATIVE_COCOA
#	endif
#elif defined(DEATH_TARGET_WINDOWS)
#	undef APIENTRY
#	if !defined(GLFW_EXPOSE_NATIVE_WIN32)
#		define GLFW_EXPOSE_NATIVE_WIN32
#	endif
#endif
#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_WINDOWS)
#	if defined(__HAS_LOCAL_GLFW)
#		include "GL/glfw3native.h"
#	else
#		include <glfw3native.h>	// for glfwGetCocoaWindow() / glfwGetWin32Window()
#	endif
#endif

// We gather version tests as define in order to easily see which features are version-dependent.
#define GLFW_VERSION_COMBINED			(GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
#define GLFW_HAS_WINDOW_TOPMOST			(GLFW_VERSION_COMBINED >= 3200)	// 3.2+ GLFW_FLOATING
#define GLFW_HAS_WINDOW_HOVERED			(GLFW_VERSION_COMBINED >= 3300)	// 3.3+ GLFW_HOVERED
#define GLFW_HAS_WINDOW_ALPHA			(GLFW_VERSION_COMBINED >= 3300)	// 3.3+ glfwSetWindowOpacity
#define GLFW_HAS_PER_MONITOR_DPI		(GLFW_VERSION_COMBINED >= 3300)	// 3.3+ glfwGetMonitorContentScale
#define GLFW_HAS_FOCUS_WINDOW			(GLFW_VERSION_COMBINED >= 3200)	// 3.2+ glfwFocusWindow
#define GLFW_HAS_FOCUS_ON_SHOW			(GLFW_VERSION_COMBINED >= 3300)	// 3.3+ GLFW_FOCUS_ON_SHOW
#define GLFW_HAS_MONITOR_WORK_AREA		(GLFW_VERSION_COMBINED >= 3300)	// 3.3+ glfwGetMonitorWorkarea
#define GLFW_HAS_OSX_WINDOW_POS_FIX		(GLFW_VERSION_COMBINED >= 3301)	// 3.3.1+ Fixed: Resizing window repositions it on MacOS #1553
#if defined(GLFW_RESIZE_NESW_CURSOR)	// Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2019-11-29 (cursors defines) // FIXME: Remove when GLFW 3.4 is released?
#	define GLFW_HAS_NEW_CURSORS			(GLFW_VERSION_COMBINED >= 3400) // 3.4+ GLFW_RESIZE_ALL_CURSOR, GLFW_RESIZE_NESW_CURSOR, GLFW_RESIZE_NWSE_CURSOR, GLFW_NOT_ALLOWED_CURSOR
#else
#	define GLFW_HAS_NEW_CURSORS			(0)
#endif
#if defined(GLFW_MOUSE_PASSTHROUGH)		// Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2020-07-17 (passthrough)
#	define GLFW_HAS_MOUSE_PASSTHROUGH	(GLFW_VERSION_COMBINED >= 3400)	// 3.4+ GLFW_MOUSE_PASSTHROUGH
#else
#	define GLFW_HAS_MOUSE_PASSTHROUGH	(0)
#endif
#define GLFW_HAS_GAMEPAD_API			(GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetGamepadState() new api
#define GLFW_HAS_GETKEYNAME				(GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwGetKeyName()
#define GLFW_HAS_GETERROR				(GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetError()

#if defined(IMGUI_HAS_VIEWPORT)
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif

namespace nCine
{
	namespace
	{
		struct ViewportData
		{
			GLFWwindow* Window;
			bool WindowOwned;
			int IgnoreWindowPosEventFrame;
			int IgnoreWindowSizeEventFrame;
#if defined(DEATH_TARGET_WINDOWS)
			WNDPROC PrevWndProc;
#endif

			ViewportData()
				: Window(nullptr), WindowOwned(false), IgnoreWindowPosEventFrame(false), IgnoreWindowSizeEventFrame(false)
#if defined(DEATH_TARGET_WINDOWS)
					, PrevWndProc(NULL)
#endif
			{
			}

			~ViewportData()
			{
				IM_ASSERT(Window == nullptr);
			}
		};

		// Chain GLFW callbacks: our callbacks will call the user's previously installed callbacks, if any.
		GLFWwindowfocusfun prevUserCallbackWindowFocus = nullptr;
		GLFWcursorposfun prevUserCallbackCursorPos = nullptr;
		GLFWcursorenterfun prevUserCallbackCursorEnter = nullptr;
		GLFWmousebuttonfun prevUserCallbackMousebutton = nullptr;
		GLFWscrollfun prevUserCallbackScroll = nullptr;
		GLFWkeyfun prevUserCallbackKey = nullptr;
		GLFWcharfun prevUserCallbackChar = nullptr;
		GLFWmonitorfun prevUserCallbackMonitor = nullptr;
#if defined(DEATH_TARGET_WINDOWS)
		WNDPROC glfwWndProc = nullptr;

#	if defined(IMGUI_HAS_VIEWPORT)
		HICON windowIcon = NULL;
		HICON windowIconSmall = NULL;
#	endif
#endif

		const char* clipboardText(void* userData)
		{
			return glfwGetClipboardString(reinterpret_cast<GLFWwindow*>(userData));
		}

		void setClipboardText(void* userData, const char* text)
		{
			glfwSetClipboardString(reinterpret_cast<GLFWwindow*>(userData), text);
		}

		ImGuiKey glfwKeyToImGuiKey(int key)
		{
			switch (key) {
				case GLFW_KEY_TAB: return ImGuiKey_Tab;
				case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
				case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
				case GLFW_KEY_UP: return ImGuiKey_UpArrow;
				case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
				case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
				case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
				case GLFW_KEY_HOME: return ImGuiKey_Home;
				case GLFW_KEY_END: return ImGuiKey_End;
				case GLFW_KEY_INSERT: return ImGuiKey_Insert;
				case GLFW_KEY_DELETE: return ImGuiKey_Delete;
				case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
				case GLFW_KEY_SPACE: return ImGuiKey_Space;
				case GLFW_KEY_ENTER: return ImGuiKey_Enter;
				case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
				case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
				case GLFW_KEY_COMMA: return ImGuiKey_Comma;
				case GLFW_KEY_MINUS: return ImGuiKey_Minus;
				case GLFW_KEY_PERIOD: return ImGuiKey_Period;
				case GLFW_KEY_SLASH: return ImGuiKey_Slash;
				case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
				case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
				case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
				case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
				case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
				case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
				case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
				case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
				case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
				case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
				case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
				case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
				case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
				case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
				case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
				case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
				case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
				case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
				case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
				case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
				case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
				case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
				case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
				case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
				case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
				case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
				case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
				case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
				case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
				case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
				case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
				case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
				case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
				case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
				case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
				case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
				case GLFW_KEY_MENU: return ImGuiKey_Menu;
				case GLFW_KEY_0: return ImGuiKey_0;
				case GLFW_KEY_1: return ImGuiKey_1;
				case GLFW_KEY_2: return ImGuiKey_2;
				case GLFW_KEY_3: return ImGuiKey_3;
				case GLFW_KEY_4: return ImGuiKey_4;
				case GLFW_KEY_5: return ImGuiKey_5;
				case GLFW_KEY_6: return ImGuiKey_6;
				case GLFW_KEY_7: return ImGuiKey_7;
				case GLFW_KEY_8: return ImGuiKey_8;
				case GLFW_KEY_9: return ImGuiKey_9;
				case GLFW_KEY_A: return ImGuiKey_A;
				case GLFW_KEY_B: return ImGuiKey_B;
				case GLFW_KEY_C: return ImGuiKey_C;
				case GLFW_KEY_D: return ImGuiKey_D;
				case GLFW_KEY_E: return ImGuiKey_E;
				case GLFW_KEY_F: return ImGuiKey_F;
				case GLFW_KEY_G: return ImGuiKey_G;
				case GLFW_KEY_H: return ImGuiKey_H;
				case GLFW_KEY_I: return ImGuiKey_I;
				case GLFW_KEY_J: return ImGuiKey_J;
				case GLFW_KEY_K: return ImGuiKey_K;
				case GLFW_KEY_L: return ImGuiKey_L;
				case GLFW_KEY_M: return ImGuiKey_M;
				case GLFW_KEY_N: return ImGuiKey_N;
				case GLFW_KEY_O: return ImGuiKey_O;
				case GLFW_KEY_P: return ImGuiKey_P;
				case GLFW_KEY_Q: return ImGuiKey_Q;
				case GLFW_KEY_R: return ImGuiKey_R;
				case GLFW_KEY_S: return ImGuiKey_S;
				case GLFW_KEY_T: return ImGuiKey_T;
				case GLFW_KEY_U: return ImGuiKey_U;
				case GLFW_KEY_V: return ImGuiKey_V;
				case GLFW_KEY_W: return ImGuiKey_W;
				case GLFW_KEY_X: return ImGuiKey_X;
				case GLFW_KEY_Y: return ImGuiKey_Y;
				case GLFW_KEY_Z: return ImGuiKey_Z;
				case GLFW_KEY_F1: return ImGuiKey_F1;
				case GLFW_KEY_F2: return ImGuiKey_F2;
				case GLFW_KEY_F3: return ImGuiKey_F3;
				case GLFW_KEY_F4: return ImGuiKey_F4;
				case GLFW_KEY_F5: return ImGuiKey_F5;
				case GLFW_KEY_F6: return ImGuiKey_F6;
				case GLFW_KEY_F7: return ImGuiKey_F7;
				case GLFW_KEY_F8: return ImGuiKey_F8;
				case GLFW_KEY_F9: return ImGuiKey_F9;
				case GLFW_KEY_F10: return ImGuiKey_F10;
				case GLFW_KEY_F11: return ImGuiKey_F11;
				case GLFW_KEY_F12: return ImGuiKey_F12;
				default: return ImGuiKey_None;
			}
		}

		// X11 does not include current pressed/released modifier key in 'mods' flags submitted by GLFW
		// See https://github.com/ocornut/imgui/issues/6034 and https://github.com/glfw/glfw/issues/1630
		void updateKeyModifiers(GLFWwindow* window)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));
			io.AddKeyEvent(ImGuiMod_Shift, (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS));
			io.AddKeyEvent(ImGuiMod_Alt, (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS));
			io.AddKeyEvent(ImGuiMod_Super, (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS));
		}

		int translateUntranslatedKey(int key, int scancode)
		{
#if GLFW_HAS_GETKEYNAME && !defined(DEATH_TARGET_EMSCRIPTEN)
			// GLFW 3.1+ attempts to "untranslate" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
			// (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
			// See https://github.com/glfw/glfw/issues/1502 for details.
			// Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
			// This won't cover edge cases but this is at least going to cover common cases.
			if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL) {
				return key;
			}
			GLFWerrorfun prevErrorCallback = glfwSetErrorCallback(nullptr);
			const char* keyName = glfwGetKeyName(key, scancode);
			glfwSetErrorCallback(prevErrorCallback);
#if GLFW_HAS_GETERROR && !defined(DEATH_TARGET_EMSCRIPTEN) // Eat errors (see #5908)
			(void)glfwGetError(nullptr);
#endif
			if (keyName && keyName[0] != 0 && keyName[1] == 0) {
				const char charNames[] = "`-=[]\\,;\'./";
				const int charKeys[] = { GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
				IM_ASSERT(IM_ARRAYSIZE(charNames) == IM_ARRAYSIZE(charKeys));
				if (keyName[0] >= '0' && keyName[0] <= '9')
					key = GLFW_KEY_0 + (keyName[0] - '0');
				else if (keyName[0] >= 'A' && keyName[0] <= 'Z')
					key = GLFW_KEY_A + (keyName[0] - 'A');
				else if (keyName[0] >= 'a' && keyName[0] <= 'z')
					key = GLFW_KEY_A + (keyName[0] - 'a');
				else if (const char* p = strchr(charNames, keyName[0]))
					key = charKeys[p - charNames];
			}
#else
			IM_UNUSED(scancode);
#endif
			return key;
		}

		float saturate(float v)
		{
			return (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
		}

#if defined(DEATH_TARGET_EMSCRIPTEN)
		EM_BOOL emscriptenWheelCallback(int eventType, const EmscriptenWheelEvent* wheelEvent, void* userData)
		{
			// Mimic emscriptenHandleWheel() in SDL.
			// Corresponding equivalent in GLFW JS emulation layer has incorrect quantizing preventing small values. See #6096
			float multiplier = 0.0f;

			if (wheelEvent->deltaMode == DOM_DELTA_PIXEL) {
				multiplier = 1.0f / 100.0f;		// 100 pixels make up a step
			} else if (wheelEvent->deltaMode == DOM_DELTA_LINE) {
				multiplier = 1.0f / 3.0f;		// 3 lines make up a step
			} else if (wheelEvent->deltaMode == DOM_DELTA_PAGE) {
				multiplier = 80.0f;				// A page makes up 80 steps
			}
			float wheelX = wheelEvent->deltaX * -multiplier;
			float wheelY = wheelEvent->deltaY * -multiplier;

			ImGuiIO& io = ImGui::GetIO();
			io.AddMouseWheelEvent(wheelX, wheelY);

			return EM_TRUE;
		}
#endif

#if defined(DEATH_TARGET_WINDOWS)
		// GLFW doesn't allow to distinguish Mouse vs TouchScreen vs Pen.
		// Add support for Win32 (based on imgui_impl_win32), because we rely on _TouchScreen info to trickle inputs differently.
		ImGuiMouseSource getMouseSourceFromMessageExtraInfo()
		{
			LPARAM extra_info = ::GetMessageExtraInfo();
			if ((extra_info & 0xFFFFFF80) == 0xFF515700) {
				return ImGuiMouseSource_Pen;
			} else if ((extra_info & 0xFFFFFF80) == 0xFF515780) {
				return ImGuiMouseSource_TouchScreen;
			} else {
				return ImGuiMouseSource_Mouse;
			}
		}

		LRESULT CALLBACK wndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			WNDPROC prevWndproc = glfwWndProc;
#if defined(IMGUI_HAS_VIEWPORT)
			ImGuiViewport* viewport = (ImGuiViewport*)::GetPropA(hWnd, "IMGUI_VIEWPORT");
			if (viewport != NULL) {
				if (ViewportData* vd = (ViewportData*)viewport->PlatformUserData) {
					prevWndproc = vd->PrevWndProc;
				}
			}
#endif

			switch (msg) {
				case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
				case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
				case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
				case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
				case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
					ImGui::GetIO().AddMouseSourceEvent(getMouseSourceFromMessageExtraInfo());
					break;

#if defined(IMGUI_HAS_VIEWPORT) && !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED
				// We have submitted https://github.com/glfw/glfw/pull/1568 to allow GLFW to support "transparent inputs".
				// In the meanwhile we implement custom per-platform workarounds here (FIXME-VIEWPORT: Implement same work-around for Linux/OSX!)
				case WM_NCHITTEST: {
					// Let mouse pass-through the window. This will allow the backend to call io.AddMouseViewportEvent() properly (which is OPTIONAL).
					// The ImGuiViewportFlags_NoInputs flag is set while dragging a viewport, as want to detect the window behind the one we are dragging.
					// If you cannot easily access those viewport flags from your windowing/event code: you may manually synchronize its state e.g. in
					// your main loop after calling UpdatePlatformWindows(). Iterate all viewports/platform windows and pass the flag to your windowing system.
					if (viewport != nullptr && (viewport->Flags & ImGuiViewportFlags_NoInputs)) {
						return HTTRANSPARENT;
					}
					break;
				}
#endif
			}
			return ::CallWindowProc(prevWndproc, hWnd, msg, wParam, lParam);
		}
#endif
	}

	bool ImGuiGlfwInput::inputEnabled_ = true;
	GLFWwindow* ImGuiGlfwInput::window_ = nullptr;
	GLFWwindow* ImGuiGlfwInput::mouseWindow_ = nullptr;
	double ImGuiGlfwInput::time_ = 0.0;
	GLFWcursor* ImGuiGlfwInput::mouseCursors_[ImGuiMouseCursor_COUNT] = {};
	ImVec2 ImGuiGlfwInput::lastValidMousePos_ = { FLT_MAX, FLT_MAX };
	bool ImGuiGlfwInput::installedCallbacks_ = false;
	bool ImGuiGlfwInput::wantUpdateMonitors_ = false;
	GLFWwindow* ImGuiGlfwInput::keyOwnerWindows_[GLFW_KEY_LAST] = {};

	void ImGuiGlfwInput::init(GLFWwindow* window, bool withCallbacks)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();

		// Setup backend capabilities flags
		io.BackendPlatformName = "nCine_GLFW";
		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;			// We can honor GetMouseCursor() values (optional)
		io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;			// We can honor io.WantSetMousePos requests (optional, rarely used)
#if defined(IMGUI_HAS_VIEWPORT)
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;		// We can create multi-viewports on the Platform side (optional)
#	endif
#	if GLFW_HAS_MOUSE_PASSTHROUGH || GLFW_HAS_WINDOW_HOVERED
		io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport;	// We can call io.AddMouseViewportEvent() with correct data (optional)
#	endif
#endif

		window_ = window;
		time_ = 0.0;
		wantUpdateMonitors_ = true;

		io.SetClipboardTextFn = setClipboardText;
		io.GetClipboardTextFn = clipboardText;
		io.ClipboardUserData = window_;

		// Create mouse cursors
		// (By design, on X11 cursors are user configurable and some cursors may be missing. When a cursor doesn't exist,
		// GLFW will emit an error which will often be printed by the app, so we temporarily disable error reporting.
		// Missing cursors will return NULL and our _UpdateMouseCursor() function will use the Arrow cursor instead.)
		const GLFWerrorfun prevErrorCallback = glfwSetErrorCallback(nullptr);
		mouseCursors_[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
		mouseCursors_[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
		mouseCursors_[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
		mouseCursors_[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
		mouseCursors_[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
#if GLFW_HAS_NEW_CURSORS
		mouseCursors_[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
		mouseCursors_[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
		mouseCursors_[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
		mouseCursors_[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
#else
		mouseCursors_[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
		mouseCursors_[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
		mouseCursors_[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
		mouseCursors_[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
#endif
		glfwSetErrorCallback(prevErrorCallback);
#if GLFW_HAS_GETERROR && !defined(DEATH_TARGET_EMSCRIPTEN) // Eat errors (see #5908)
		(void)glfwGetError(nullptr);
#endif

		// Chain GLFW callbacks: our callbacks will call the user's previously installed callbacks, if any.
		if (withCallbacks) {
			installCallbacks(window);
		}

		// Update monitors the first time (note: monitor callback are broken in GLFW 3.2 and earlier, see https://github.com/glfw/glfw/issues/784)
		updateMonitors();

		// Register Emscripten Wheel callback to workaround issue in Emscripten GLFW Emulation (#6096)
		// We intentionally do not check 'if (install_callbacks)' here, as some users may set it to false and call GLFW callback themselves.
		// FIXME: May break chaining in case user registered their own Emscripten callback?
#if defined(DEATH_TARGET_EMSCRIPTEN)
		emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, NULL, false, emscriptenWheelCallback);
#endif

		// Set platform dependent data in viewport
		ImGuiViewport* mainViewport = ImGui::GetMainViewport();
#if defined(DEATH_TARGET_WINDOWS)
		mainViewport->PlatformHandleRaw = glfwGetWin32Window(window_);
#elif defined(DEATH_TARGET_APPLE)
		mainViewport->PlatformHandleRaw = (void*)glfwGetCocoaWindow(window_);
#else
		(void)mainViewport;
#endif

#if defined(IMGUI_HAS_VIEWPORT)
		// Register platform interface (will be coupled with a renderer interface)
		ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
		platformIo.Platform_CreateWindow = onCreateWindow;
		platformIo.Platform_DestroyWindow = onDestroyWindow;
		platformIo.Platform_ShowWindow = onShowWindow;
		platformIo.Platform_SetWindowPos = onSetWindowPos;
		platformIo.Platform_GetWindowPos = onGetWindowPos;
		platformIo.Platform_SetWindowSize = onSetWindowSize;
		platformIo.Platform_GetWindowSize = onGetWindowSize;
		platformIo.Platform_SetWindowFocus = onSetWindowFocus;
		platformIo.Platform_GetWindowFocus = onGetWindowFocus;
		platformIo.Platform_GetWindowMinimized = onGetWindowMinimized;
		platformIo.Platform_SetWindowTitle = onSetWindowTitle;
		platformIo.Platform_RenderWindow = onRenderWindow;
		platformIo.Platform_SwapBuffers = onSwapBuffers;
#	if GLFW_HAS_WINDOW_ALPHA
		platformIo.Platform_SetWindowAlpha = onSetWindowAlpha;
#	endif

		// Register main window handle (which is owned by the main application, not by us)
		// This is mostly for simplicity and consistency, so that our code (e.g. mouse handling etc.) can use same logic for main and secondary viewports.
		ViewportData* vd = new ViewportData();
		vd->Window = window_;
		vd->WindowOwned = false;
		mainViewport->PlatformUserData = vd;
		mainViewport->PlatformHandle = (void*)window_;

#	if defined(DEATH_TARGET_WINDOWS)
		HINSTANCE inst = ((HINSTANCE)&__ImageBase);
		windowIcon = (HICON)::LoadImage(inst, L"IMGUI_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
		windowIconSmall = (HICON)::LoadImage(inst, L"IMGUI_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
#	endif
#endif

		// Windows: Register a WndProc hook so we can intercept some messages.
#if defined(DEATH_TARGET_WINDOWS)
		glfwWndProc = (WNDPROC)::GetWindowLongPtr((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC);
		IM_ASSERT(glfwWndProc != nullptr);
		::SetWindowLongPtr((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)wndProcHook);
#endif
	}

	void ImGuiGlfwInput::shutdown()
	{
		ImGuiIO& io = ImGui::GetIO();

#if defined(IMGUI_HAS_VIEWPORT)
		ImGui::DestroyPlatformWindows();

#	if defined(DEATH_TARGET_WINDOWS)
		if (windowIcon != NULL) {
			::DestroyIcon(windowIcon);
			windowIcon = NULL;
		}
		if (windowIconSmall != NULL) {
			::DestroyIcon(windowIconSmall);
			windowIconSmall = NULL;
		}
#	endif
#endif

		if (installedCallbacks_) {
			restoreCallbacks(window_);
		}

		for (ImGuiMouseCursor i = 0; i < ImGuiMouseCursor_COUNT; i++) {
			glfwDestroyCursor(mouseCursors_[i]);
		}

		// Windows: register a WndProc hook so we can intercept some messages.
#if defined(DEATH_TARGET_WINDOWS)
		ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		::SetWindowLongPtr((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)glfwWndProc);
		glfwWndProc = nullptr;
#endif

		io.BackendPlatformName = nullptr;
		io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad);
#if defined(IMGUI_HAS_VIEWPORT)
		io.BackendFlags &= ~(ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseHoveredViewport);
#endif

		ImGui::DestroyContext();
	}

	void ImGuiGlfwInput::newFrame()
	{
		ImGuiIO& io = ImGui::GetIO();
		IM_ASSERT(io.Fonts->IsBuilt() && "Font atlas not built! Missing call to ImGuiDrawing::buildFonts() function?");

		// Setup display size (every frame to accommodate for window resizing)
		int w, h;
		int displayW, displayH;
		glfwGetWindowSize(window_, &w, &h);
		glfwGetFramebufferSize(window_, &displayW, &displayH);
		io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
		if (w > 0 && h > 0) {
			io.DisplayFramebufferScale = ImVec2(static_cast<float>(displayW) / w, static_cast<float>(displayH) / h);
		}
#if defined(IMGUI_HAS_DOCK)
		if (wantUpdateMonitors_) {
			updateMonitors();
		}
#endif

		// Setup time step
		// (Accept glfwGetTime() not returning a monotonically increasing value. Seems to happens on disconnecting peripherals and probably on VMs and Emscripten, see #6491, #6189, #6114, #3644)
		double currentTime = glfwGetTime();
		if (currentTime <= time_) {
			currentTime = time_ + 0.00001f;
		}
		io.DeltaTime = (time_ > 0.0 ? static_cast<float>(currentTime - time_) : static_cast<float>(1.0f / 60.0f));
		time_ = currentTime;

		updateMouseData();
		updateMouseCursor();

		// Update game controllers (if enabled and available)
		updateGamepads();
	}

	void ImGuiGlfwInput::endFrame()
	{
#if defined(IMGUI_HAS_VIEWPORT)
		// Update and render additional Platform Windows
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

			// Restore the OpenGL rendering context to the main window DC, since platform windows might have changed it.
			glfwMakeContextCurrent(window_);
		}
#endif
	}

	void ImGuiGlfwInput::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
	{
		if (prevUserCallbackMousebutton != nullptr && window == window_) {
			prevUserCallbackMousebutton(window, button, action, mods);
		}

		if (!inputEnabled_) {
			return;
		}

		updateKeyModifiers(window);
		ImGuiIO& io = ImGui::GetIO();
		if (button >= 0 && button < ImGuiMouseButton_COUNT) {
			io.AddMouseButtonEvent(button, action == GLFW_PRESS);
		}
	}

	void ImGuiGlfwInput::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
	{
		if (prevUserCallbackScroll != nullptr && window == window_) {
			prevUserCallbackScroll(window, xoffset, yoffset);
		}

		if (!inputEnabled_) {
			return;
		}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Ignore GLFW events on Emscripten - it will be processed in emscriptenWheelCallback().
		ImGuiIO& io = ImGui::GetIO();
		io.AddMouseWheelEvent(static_cast<float>(xoffset), static_cast<float>(yoffset));
#endif
	}

	void ImGuiGlfwInput::keyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods)
	{
		if (prevUserCallbackKey != nullptr && window == window_) {
			prevUserCallbackKey(window, keycode, scancode, action, mods);
		}

		if (!inputEnabled_ || (action != GLFW_PRESS && action != GLFW_RELEASE)) {
			return;
		}

		updateKeyModifiers(window);

		if (keycode >= 0 && keycode < IM_ARRAYSIZE(keyOwnerWindows_)) {
			keyOwnerWindows_[keycode] = (action == GLFW_PRESS ? window : nullptr);
		}

		keycode = translateUntranslatedKey(keycode, scancode);

		ImGuiIO& io = ImGui::GetIO();
		ImGuiKey imguiKey = glfwKeyToImGuiKey(keycode);
		io.AddKeyEvent(imguiKey, (action == GLFW_PRESS));
		io.SetKeyEventNativeData(imguiKey, keycode, scancode); // To support legacy indexing (<1.87 user code)
	}

	void ImGuiGlfwInput::windowFocusCallback(GLFWwindow* window, int focused)
	{
		if (prevUserCallbackWindowFocus != nullptr && window == window_) {
			prevUserCallbackWindowFocus(window, focused);
		}

		ImGuiIO& io = ImGui::GetIO();
		io.AddFocusEvent(focused != 0);
	}

	void ImGuiGlfwInput::cursorPosCallback(GLFWwindow* window, double x, double y)
	{
		if (prevUserCallbackCursorPos != nullptr && window == window_) {
			prevUserCallbackCursorPos(window, x, y);
		}

		if (!inputEnabled_) {
			return;
		}

		ImGuiIO& io = ImGui::GetIO();
#if defined(IMGUI_HAS_VIEWPORT)
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			int windowX, windowY;
			glfwGetWindowPos(window, &windowX, &windowY);
			x += windowX;
			y += windowY;
		}
#endif
		io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
		lastValidMousePos_ = ImVec2(static_cast<float>(x), static_cast<float>(y));
	}

	void ImGuiGlfwInput::cursorEnterCallback(GLFWwindow* window, int entered)
	{
		if (prevUserCallbackCursorEnter != nullptr && window == window_) {
			prevUserCallbackCursorEnter(window, entered);
		}

		ImGuiIO& io = ImGui::GetIO();
		if (entered) {
			mouseWindow_ = window;
			io.AddMousePosEvent(lastValidMousePos_.x, lastValidMousePos_.y);
		} else if (!entered && mouseWindow_ == window) {
			lastValidMousePos_ = io.MousePos;
			mouseWindow_ = nullptr;
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
		}
	}

	void ImGuiGlfwInput::charCallback(GLFWwindow* window, unsigned int c)
	{
		if (prevUserCallbackChar != nullptr && window == window_) {
			prevUserCallbackChar(window, c);
		}

		if (!inputEnabled_) {
			return;
		}

		ImGuiIO& io = ImGui::GetIO();
		io.AddInputCharacter(c);
	}

	void ImGuiGlfwInput::monitorCallback(GLFWmonitor* monitor, int event)
	{
		if (prevUserCallbackMonitor != nullptr) {
			prevUserCallbackMonitor(monitor, event);
		}

		wantUpdateMonitors_ = true;
	}

#if defined(IMGUI_HAS_VIEWPORT)
	void ImGuiGlfwInput::windowCloseCallback(GLFWwindow* window)
	{
		if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window)) {
			viewport->PlatformRequestClose = true;
		}
	}

	// GLFW may dispatch window pos/size events after calling glfwSetWindowPos()/glfwSetWindowSize().
	// However: depending on the platform the callback may be invoked at different time:
	// - on Windows it appears to be called within the glfwSetWindowPos()/glfwSetWindowSize() call
	// - on Linux it is queued and invoked during glfwPollEvents()
	// Because the event doesn't always fire on glfwSetWindowXXX() we use a frame counter tag to only
	// ignore recent glfwSetWindowXXX() calls.
	void ImGuiGlfwInput::windowPosCallback(GLFWwindow* window, int, int)
	{
		if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window)) {
			if (ViewportData* vd = (ViewportData*)viewport->PlatformUserData) {
				bool ignoreEvent = (ImGui::GetFrameCount() <= vd->IgnoreWindowPosEventFrame + 1);
				if (ignoreEvent) {
					return;
				}
			}
			viewport->PlatformRequestMove = true;
		}
	}

	void ImGuiGlfwInput::windowSizeCallback(GLFWwindow* window, int, int)
	{
		if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window)) {
			if (ViewportData* vd = (ViewportData*)viewport->PlatformUserData) {
				bool ignoreEvent = (ImGui::GetFrameCount() <= vd->IgnoreWindowSizeEventFrame + 1);
				if (ignoreEvent) {
					return;
				}
			}
			viewport->PlatformRequestResize = true;
		}
	}
#endif

	void ImGuiGlfwInput::installCallbacks(GLFWwindow* window)
	{
		IM_ASSERT(!installedCallbacks_ && "Callbacks already installed!");
		IM_ASSERT(window_ == window);

		prevUserCallbackWindowFocus = glfwSetWindowFocusCallback(window, windowFocusCallback);
		prevUserCallbackCursorEnter = glfwSetCursorEnterCallback(window, cursorEnterCallback);
		prevUserCallbackCursorPos = glfwSetCursorPosCallback(window, cursorPosCallback);
		prevUserCallbackMousebutton = glfwSetMouseButtonCallback(window, mouseButtonCallback);
		prevUserCallbackScroll = glfwSetScrollCallback(window, scrollCallback);
		prevUserCallbackKey = glfwSetKeyCallback(window, keyCallback);
		prevUserCallbackChar = glfwSetCharCallback(window, charCallback);
		prevUserCallbackMonitor = glfwSetMonitorCallback(monitorCallback);
		installedCallbacks_ = true;
	}

	void ImGuiGlfwInput::restoreCallbacks(GLFWwindow* window)
	{
		IM_ASSERT(installedCallbacks_ && "Callbacks not installed!");
		IM_ASSERT(window_ == window);

		glfwSetWindowFocusCallback(window, prevUserCallbackWindowFocus);
		glfwSetCursorEnterCallback(window, prevUserCallbackCursorEnter);
		glfwSetCursorPosCallback(window, prevUserCallbackCursorPos);
		glfwSetMouseButtonCallback(window, prevUserCallbackMousebutton);
		glfwSetScrollCallback(window, prevUserCallbackScroll);
		glfwSetKeyCallback(window, prevUserCallbackKey);
		glfwSetCharCallback(window, prevUserCallbackChar);
		glfwSetMonitorCallback(prevUserCallbackMonitor);

		installedCallbacks_ = false;
		prevUserCallbackWindowFocus = nullptr;
		prevUserCallbackCursorEnter = nullptr;
		prevUserCallbackCursorPos = nullptr;
		prevUserCallbackMousebutton = nullptr;
		prevUserCallbackScroll = nullptr;
		prevUserCallbackKey = nullptr;
		prevUserCallbackChar = nullptr;
		prevUserCallbackMonitor = nullptr;
	}

	void ImGuiGlfwInput::updateMouseData()
	{
		ImGuiIO& io = ImGui::GetIO();

#if defined(IMGUI_HAS_VIEWPORT)
		ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
		ImGuiID mouseViewportId = 0;
		const ImVec2 mousePosPrev = io.MousePos;
		for (int n = 0; n < platformIo.Viewports.Size; n++) {
			ImGuiViewport* viewport = platformIo.Viewports[n];
			GLFWwindow* window = (GLFWwindow*)viewport->PlatformHandle;

#	if defined(DEATH_TARGET_EMSCRIPTEN)
			const bool isWindowFocused = true;
#	else
			const bool isWindowFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
#	endif
			if (isWindowFocused) {
				// (Optional) Set OS mouse position from Dear ImGui if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
				// When multi-viewports are enabled, all Dear ImGui positions are same as OS positions.
				if (io.WantSetMousePos) {
					glfwSetCursorPos(window, static_cast<double>(mousePosPrev.x - viewport->Pos.x), static_cast<double>(mousePosPrev.y - viewport->Pos.y));
				}

				// (Optional) Fallback to provide mouse position when focused (ImGui_ImplGlfw_CursorPosCallback already provides this when hovered or captured)
				if (mouseWindow_ == nullptr) {
					double mouseX, mouseY;
					glfwGetCursorPos(window, &mouseX, &mouseY);
					if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
						// Single viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
						// Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
						int windowX, windowY;
						glfwGetWindowPos(window, &windowX, &windowY);
						mouseX += windowX;
						mouseY += windowY;
					}
					lastValidMousePos_ = ImVec2(static_cast<float>(mouseX), static_cast<float>(mouseY));
					io.AddMousePosEvent(static_cast<float>(mouseX), static_cast<float>(mouseY));
				}
			}

			// (Optional) When using multiple viewports: call io.AddMouseViewportEvent() with the viewport the OS mouse cursor is hovering.
			// If ImGuiBackendFlags_HasMouseHoveredViewport is not set by the backend, Dear imGui will ignore this field and infer the information using its flawed heuristic.
			// - [X] GLFW >= 3.3 backend ON WINDOWS ONLY does correctly ignore viewports with the _NoInputs flag (since we implement hit via our WndProc hook)
			//       On other platforms we rely on the library fallbacking to its own search when reporting a viewport with _NoInputs flag.
			// - [!] GLFW <= 3.2 backend CANNOT correctly ignore viewports with the _NoInputs flag, and CANNOT reported Hovered Viewport because of mouse capture.
			//       Some backend are not able to handle that correctly. If a backend report an hovered viewport that has the _NoInputs flag (e.g. when dragging a window
			//       for docking, the viewport has the _NoInputs flag in order to allow us to find the viewport under), then Dear ImGui is forced to ignore the value reported
			//       by the backend, and use its flawed heuristic to guess the viewport behind.
			// - [X] GLFW backend correctly reports this regardless of another viewport behind focused and dragged from (we need this to find a useful drag and drop target).
			// FIXME: This is currently only correct on Win32. See what we do below with the WM_NCHITTEST, missing an equivalent for other systems.
			// See https://github.com/glfw/glfw/issues/1236 if you want to help in making this a GLFW feature.
#	if GLFW_HAS_MOUSE_PASSTHROUGH
			const bool windowNoInput = (viewport->Flags & ImGuiViewportFlags_NoInputs) != 0;
			glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, windowNoInput);
#	endif
#	if GLFW_HAS_MOUSE_PASSTHROUGH || GLFW_HAS_WINDOW_HOVERED
			if (glfwGetWindowAttrib(window, GLFW_HOVERED)) {
				mouseViewportId = viewport->ID;
			}
#	else
			// We cannot use mouseWindow_ maintained from CursorEnter/Leave callbacks, because it is locked to the window capturing mouse.
#	endif
	}

	if (io.BackendFlags & ImGuiBackendFlags_HasMouseHoveredViewport) {
		io.AddMouseViewportEvent(mouseViewportId);
	}
#else
#	if defined(DEATH_TARGET_EMSCRIPTEN)
	const bool isWindowFocused = true;
#	else
	const bool isWindowFocused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
#	endif
	if (isWindowFocused) {
		// (Optional) Set OS mouse position from Dear ImGui if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
		if (io.WantSetMousePos) {
			glfwSetCursorPos(window_, static_cast<double>(io.MousePos.x), static_cast<double>(io.MousePos.y));
		}

		// (Optional) Fallback to provide mouse position when focused (ImGui_ImplGlfw_CursorPosCallback already provides this when hovered or captured)
		if (window_ == nullptr) {
			double mouseX, mouseY;
			glfwGetCursorPos(window_, &mouseX, &mouseY);
			lastValidMousePos_ = ImVec2(static_cast<float>(mouseX), static_cast<float>(mouseY));
			io.AddMousePosEvent(static_cast<float>(mouseX), static_cast<float>(mouseY));
		}
	}
#endif
	}

	void ImGuiGlfwInput::updateMouseCursor()
	{
		ImGuiIO& io = ImGui::GetIO();
		if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(window_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
			return;
		}

		ImGuiMouseCursor imguiCursor = ImGui::GetMouseCursor();
#if defined(IMGUI_HAS_VIEWPORT)
		ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
		for (int n = 0; n < platformIo.Viewports.Size; n++) {
			GLFWwindow* window = (GLFWwindow*)platformIo.Viewports[n]->PlatformHandle;
			if (imguiCursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
				// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
			} else {
				// Show OS mouse cursor
				// FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works here.
				glfwSetCursor(window, mouseCursors_[imguiCursor] ? mouseCursors_[imguiCursor] : mouseCursors_[ImGuiMouseCursor_Arrow]);
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			}
		}
#else
		if (imguiCursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
			// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
			glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
		} else {
			// Show OS mouse cursor
			// FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works here.
			glfwSetCursor(window_, mouseCursors_[imguiCursor] ? mouseCursors_[imguiCursor] : mouseCursors_[ImGuiMouseCursor_Arrow]);
			glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
#endif
	}

	void ImGuiGlfwInput::updateGamepads()
	{
		ImGuiIO& io = ImGui::GetIO();
		if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0) { // FIXME: Technically feeding gamepad shouldn't depend on this now that they are regular inputs.
			return;
		}

		const bool joyMappedInput = imGuiJoyMappedInput();

		if (!joyMappedInput) {
			io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
#if GLFW_HAS_GAMEPAD_API && !defined(DEATH_TARGET_EMSCRIPTEN)
			GLFWgamepadstate gamepad;
			if (!glfwGetGamepadState(GLFW_JOYSTICK_1, &gamepad)) {
				return;
			}

			// clang-format off
			#define MAP_BUTTON(KEY_NO, BUTTON_NO, _UNUSED)          do { io.AddKeyEvent(KEY_NO, gamepad.buttons[BUTTON_NO] != 0); } while (0)
			#define MAP_ANALOG(KEY_NO, AXIS_NO, _UNUSED, V0, V1)    do { float v = gamepad.axes[AXIS_NO]; v = (v - V0) / (V1 - V0);\
																			io.AddKeyAnalogEvent(KEY_NO, v > 0.10f, saturate(v)); } while (0)
			// clang-format on
#else
			int axesCount = 0, buttonsCount = 0;
			const float* axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &axesCount);
			const unsigned char* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &buttonsCount);
			if (axesCount == 0 || buttonsCount == 0) {
				return;
			}

			// clang-format off
			#define MAP_BUTTON(KEY_NO, _UNUSED, BUTTON_NO)          do { io.AddKeyEvent(KEY_NO, (buttonsCount > BUTTON_NO && buttons[BUTTON_NO] == GLFW_PRESS)); } while (0)
			#define MAP_ANALOG(KEY_NO, _UNUSED, AXIS_NO, V0, V1)    do { float v = (axesCount > AXIS_NO) ? axes[AXIS_NO] : V0; v = (v - V0) / (V1 - V0);\
																			io.AddKeyAnalogEvent(KEY_NO, v > 0.10f, saturate(v)); } while (0)
			// clang-format on
#endif
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
			// clang-format off
			MAP_BUTTON(ImGuiKey_GamepadStart, GLFW_GAMEPAD_BUTTON_START, 7);
			MAP_BUTTON(ImGuiKey_GamepadBack, GLFW_GAMEPAD_BUTTON_BACK, 6);
			MAP_BUTTON(ImGuiKey_GamepadFaceLeft, GLFW_GAMEPAD_BUTTON_X, 2);     // Xbox X, PS Square
			MAP_BUTTON(ImGuiKey_GamepadFaceRight, GLFW_GAMEPAD_BUTTON_B, 1);     // Xbox B, PS Circle
			MAP_BUTTON(ImGuiKey_GamepadFaceUp, GLFW_GAMEPAD_BUTTON_Y, 3);     // Xbox Y, PS Triangle
			MAP_BUTTON(ImGuiKey_GamepadFaceDown, GLFW_GAMEPAD_BUTTON_A, 0);     // Xbox A, PS Cross
			MAP_BUTTON(ImGuiKey_GamepadDpadLeft, GLFW_GAMEPAD_BUTTON_DPAD_LEFT, 13);
			MAP_BUTTON(ImGuiKey_GamepadDpadRight, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, 11);
			MAP_BUTTON(ImGuiKey_GamepadDpadUp, GLFW_GAMEPAD_BUTTON_DPAD_UP, 10);
			MAP_BUTTON(ImGuiKey_GamepadDpadDown, GLFW_GAMEPAD_BUTTON_DPAD_DOWN, 12);
			MAP_BUTTON(ImGuiKey_GamepadL1, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, 4);
			MAP_BUTTON(ImGuiKey_GamepadR1, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, 5);
			MAP_ANALOG(ImGuiKey_GamepadL2, GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, 4, -0.75f, +1.0f);
			MAP_ANALOG(ImGuiKey_GamepadR2, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, 5, -0.75f, +1.0f);
			MAP_BUTTON(ImGuiKey_GamepadL3, GLFW_GAMEPAD_BUTTON_LEFT_THUMB, 8);
			MAP_BUTTON(ImGuiKey_GamepadR3, GLFW_GAMEPAD_BUTTON_RIGHT_THUMB, 9);
			MAP_ANALOG(ImGuiKey_GamepadLStickLeft, GLFW_GAMEPAD_AXIS_LEFT_X, 0, -0.25f, -1.0f);
			MAP_ANALOG(ImGuiKey_GamepadLStickRight, GLFW_GAMEPAD_AXIS_LEFT_X, 0, +0.25f, +1.0f);
			MAP_ANALOG(ImGuiKey_GamepadLStickUp, GLFW_GAMEPAD_AXIS_LEFT_Y, 1, -0.25f, -1.0f);
			MAP_ANALOG(ImGuiKey_GamepadLStickDown, GLFW_GAMEPAD_AXIS_LEFT_Y, 1, +0.25f, +1.0f);
			MAP_ANALOG(ImGuiKey_GamepadRStickLeft, GLFW_GAMEPAD_AXIS_RIGHT_X, 2, -0.25f, -1.0f);
			MAP_ANALOG(ImGuiKey_GamepadRStickRight, GLFW_GAMEPAD_AXIS_RIGHT_X, 2, +0.25f, +1.0f);
			MAP_ANALOG(ImGuiKey_GamepadRStickUp, GLFW_GAMEPAD_AXIS_RIGHT_Y, 3, -0.25f, -1.0f);
			MAP_ANALOG(ImGuiKey_GamepadRStickDown, GLFW_GAMEPAD_AXIS_RIGHT_Y, 3, +0.25f, +1.0f);
			#undef MAP_BUTTON
			#undef MAP_ANALOG
			// clang-format on
		}
	}

	void ImGuiGlfwInput::updateMonitors()
	{
#if defined(IMGUI_HAS_DOCK)
		ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
		wantUpdateMonitors_ = false;

		int monitorsCount = 0;
		GLFWmonitor** glfwMonitors = glfwGetMonitors(&monitorsCount);
		if (monitorsCount == 0) { // Preserve existing monitor list if there are none. Happens on macOS sleeping (#5683)
			return;
		}

		platformIo.Monitors.resize(0);
		for (int n = 0; n < monitorsCount; n++) {
			ImGuiPlatformMonitor monitor;
			int x, y;
			glfwGetMonitorPos(glfwMonitors[n], &x, &y);
			const GLFWvidmode* vidMode = glfwGetVideoMode(glfwMonitors[n]);
			if (vidMode == nullptr) {
				continue; // Failed to get Video mode (e.g. Emscripten does not support this function)
			}
			monitor.MainPos = monitor.WorkPos = ImVec2(static_cast<float>(x), static_cast<float>(y));
			monitor.MainSize = monitor.WorkSize = ImVec2(static_cast<float>(vidMode->width), static_cast<float>(vidMode->height));
#	if GLFW_HAS_MONITOR_WORK_AREA
			int w, h;
			glfwGetMonitorWorkarea(glfwMonitors[n], &x, &y, &w, &h);
			if (w > 0 && h > 0) { // Workaround a small GLFW issue reporting zero on monitor changes: https://github.com/glfw/glfw/pull/1761
				monitor.WorkPos = ImVec2(static_cast<float>(x), static_cast<float>(y));
				monitor.WorkSize = ImVec2(static_cast<float>(w), static_cast<float>(h));
			}
#	endif
#	if GLFW_HAS_PER_MONITOR_DPI
			// Warning: the validity of monitor DPI information on Windows depends on the application DPI awareness settings, which generally needs to be set in the manifest or at runtime.
			float xScale, yScale;
			glfwGetMonitorContentScale(glfwMonitors[n], &xScale, &yScale);
			monitor.DpiScale = xScale;
#	endif
			monitor.PlatformHandle = static_cast<void*>(glfwMonitors[n]); // [...] GLFW doc states: "guaranteed to be valid only until the monitor configuration changes"
			platformIo.Monitors.push_back(monitor);
		}
#endif
	}

#if defined(IMGUI_HAS_VIEWPORT)
	ImGuiViewport* ImGuiGlfwInput::getParentViewport(ImGuiViewport* viewport)
	{
		return (viewport->ParentViewportId ? ImGui::FindViewportByID(viewport->ParentViewportId) : nullptr);
	}

	void ImGuiGlfwInput::addParentToView(ImGuiViewport* viewport, ImGuiViewport* parentViewport)
	{
#	if defined(DEATH_TARGET_WINDOWS)
		::SetWindowLongPtr(reinterpret_cast<HWND>(viewport->PlatformHandleRaw), GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(parentViewport->PlatformHandleRaw));
#	endif
	}

	void ImGuiGlfwInput::onCreateWindow(ImGuiViewport* viewport)
	{
		ViewportData* vd = new ViewportData();
		viewport->PlatformUserData = vd;

		// GLFW 3.2 unfortunately always set focus on glfwCreateWindow() if GLFW_VISIBLE is set, regardless of GLFW_FOCUSED
		// With GLFW 3.3, the hint GLFW_FOCUS_ON_SHOW fixes this problem
		glfwWindowHint(GLFW_VISIBLE, false);
		glfwWindowHint(GLFW_FOCUSED, false);
#	if GLFW_HAS_FOCUS_ON_SHOW
		glfwWindowHint(GLFW_FOCUS_ON_SHOW, false);
#	endif
		glfwWindowHint(GLFW_DECORATED, (viewport->Flags & ImGuiViewportFlags_NoDecoration ? false : true));
#	if GLFW_HAS_WINDOW_TOPMOST
		glfwWindowHint(GLFW_FLOATING, (viewport->Flags & ImGuiViewportFlags_TopMost ? true : false));
#	endif
		GLFWwindow* shareWindow = window_;
		vd->Window = glfwCreateWindow(static_cast<int>(viewport->Size.x), static_cast<int>(viewport->Size.y), "", nullptr, shareWindow);
		vd->WindowOwned = true;
		viewport->PlatformHandle = static_cast<void*>(vd->Window);
#	if defined(DEATH_TARGET_WINDOWS)
		HWND hwnd = glfwGetWin32Window(vd->Window);
		viewport->PlatformHandleRaw = hwnd;
#	elif defined(DEATH_TARGET_APPLE)
		viewport->PlatformHandleRaw = static_cast<void*>(glfwGetCocoaWindow(vd->Window));
#	endif
		glfwSetWindowPos(vd->Window, static_cast<int>(viewport->Pos.x), static_cast<int>(viewport->Pos.y));

#	if defined(DEATH_TARGET_WINDOWS)
		if (windowIconSmall != NULL) ::SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)windowIconSmall);
		if (windowIcon != NULL) ::SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)windowIcon);
#	endif

		//if (ImGuiViewport* parentViewport = getParentViewport(viewport)) {
		//	addParentToView(viewport, parentViewport);
		//}

		// Install GLFW callbacks for secondary viewports
		glfwSetWindowFocusCallback(vd->Window, windowFocusCallback);
		glfwSetCursorEnterCallback(vd->Window, cursorEnterCallback);
		glfwSetCursorPosCallback(vd->Window, cursorPosCallback);
		glfwSetMouseButtonCallback(vd->Window, mouseButtonCallback);
		glfwSetScrollCallback(vd->Window, scrollCallback);
		glfwSetKeyCallback(vd->Window, keyCallback);
		glfwSetCharCallback(vd->Window, charCallback);
		glfwSetWindowCloseCallback(vd->Window, windowCloseCallback);
		glfwSetWindowPosCallback(vd->Window, windowPosCallback);
		glfwSetWindowSizeCallback(vd->Window, windowSizeCallback);

		glfwMakeContextCurrent(vd->Window);
		glfwSwapInterval(0);
	}

	void ImGuiGlfwInput::onDestroyWindow(ImGuiViewport* viewport)
	{
		if (ViewportData* vd = (ViewportData*)viewport->PlatformUserData) {
			if (vd->WindowOwned) {
#	if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED && defined(DEATH_TARGET_WINDOWS)
				HWND hwnd = (HWND)viewport->PlatformHandleRaw;
				::RemovePropA(hwnd, "IMGUI_VIEWPORT");
#	endif
				// Release any keys that were pressed in the window being destroyed and are still held down,
				// because we will not receive any release events after window is destroyed.
				for (int i = 0; i < IM_ARRAYSIZE(keyOwnerWindows_); i++) {
					if (keyOwnerWindows_[i] == vd->Window) {
						keyCallback(vd->Window, i, 0, GLFW_RELEASE, 0); // Later params are only used for main viewport, on which this function is never called.
					}
				}

				glfwDestroyWindow(vd->Window);
			}
			vd->Window = nullptr;
			delete vd;
		}
		viewport->PlatformUserData = nullptr;
		viewport->PlatformHandle = nullptr;
	}

	void ImGuiGlfwInput::onShowWindow(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;

#	if defined(DEATH_TARGET_WINDOWS)
		// GLFW hack: Hide icon from task bar
		HWND hwnd = (HWND)viewport->PlatformHandleRaw;
		if (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) {
			LONG exStyle = ::GetWindowLong(hwnd, GWL_EXSTYLE);
			exStyle &= ~WS_EX_APPWINDOW;
			exStyle |= WS_EX_TOOLWINDOW;
			::SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
		}

		// GLFW hack: install hook for WM_NCHITTEST message handler
#		if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED && defined(DEATH_TARGET_WINDOWS)
		::SetPropA(hwnd, "IMGUI_VIEWPORT", viewport);
		vd->PrevWndProc = (WNDPROC)::GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
		::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)wndProcHook);
#		endif

#		if !GLFW_HAS_FOCUS_ON_SHOW
		// GLFW hack: GLFW 3.2 has a bug where glfwShowWindow() also activates/focus the window.
		// The fix was pushed to GLFW repository on 2018/01/09 and should be included in GLFW 3.3 via a GLFW_FOCUS_ON_SHOW window attribute.
		// See https://github.com/glfw/glfw/issues/1189
		// FIXME-VIEWPORT: Implement same work-around for Linux/OSX in the meanwhile.
		if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing) {
			::ShowWindow(hwnd, SW_SHOWNA);
			return;
		}
#		endif
#	endif

		glfwShowWindow(vd->Window);
	}

	ImVec2 ImGuiGlfwInput::onGetWindowPos(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		int x = 0, y = 0;
		glfwGetWindowPos(vd->Window, &x, &y);
		return ImVec2(static_cast<float>(x), static_cast<float>(y));
	}

	void ImGuiGlfwInput::onSetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		vd->IgnoreWindowPosEventFrame = ImGui::GetFrameCount();
		glfwSetWindowPos(vd->Window, static_cast<int>(pos.x), static_cast<int>(pos.y));
	}

	ImVec2 ImGuiGlfwInput::onGetWindowSize(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		int w = 0, h = 0;
		glfwGetWindowSize(vd->Window, &w, &h);
		return ImVec2(static_cast<float>(w), static_cast<float>(h));
	}

	void ImGuiGlfwInput::onSetWindowSize(ImGuiViewport* viewport, ImVec2 size)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
#	if defined(DEATH_TARGET_APPLE) && !GLFW_HAS_OSX_WINDOW_POS_FIX
		// Native OS windows are positioned from the bottom-left corner on macOS, whereas on other platforms they are
		// positioned from the upper-left corner. GLFW makes an effort to convert macOS style coordinates, however it
		// doesn't handle it when changing size. We are manually moving the window in order for changes of size to be based
		// on the upper-left corner.
		int x, y, width, height;
		glfwGetWindowPos(vd->Window, &x, &y);
		glfwGetWindowSize(vd->Window, &width, &height);
		glfwSetWindowPos(vd->Window, x, y - height + size.y);
#	endif
		vd->IgnoreWindowSizeEventFrame = ImGui::GetFrameCount();
		glfwSetWindowSize(vd->Window, static_cast<int>(size.x), static_cast<int>(size.y));
	}

	void ImGuiGlfwInput::onSetWindowTitle(ImGuiViewport* viewport, const char* title)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		glfwSetWindowTitle(vd->Window, title);
	}

	void ImGuiGlfwInput::onSetWindowFocus(ImGuiViewport* viewport)
	{
#	if GLFW_HAS_FOCUS_WINDOW
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		glfwFocusWindow(vd->Window);
#	else
		// FIXME: What are the effect of not having this function? At the moment imgui doesn't actually call SetWindowFocus - we set that up ahead, will answer that question later.
		(void)viewport;
#	endif
	}

	bool ImGuiGlfwInput::onGetWindowFocus(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		return glfwGetWindowAttrib(vd->Window, GLFW_FOCUSED) != 0;
	}

	bool ImGuiGlfwInput::onGetWindowMinimized(ImGuiViewport* viewport)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		return glfwGetWindowAttrib(vd->Window, GLFW_ICONIFIED) != 0;
	}

	void ImGuiGlfwInput::onSetWindowAlpha(ImGuiViewport* viewport, float alpha)
	{
#	if GLFW_HAS_WINDOW_ALPHA
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		glfwSetWindowOpacity(vd->Window, alpha);
#	endif
	}

	void ImGuiGlfwInput::onRenderWindow(ImGuiViewport* viewport, void*)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		glfwMakeContextCurrent(vd->Window);
	}

	void ImGuiGlfwInput::onSwapBuffers(ImGuiViewport* viewport, void*)
	{
		ViewportData* vd = (ViewportData*)viewport->PlatformUserData;
		glfwMakeContextCurrent(vd->Window);
		glfwSwapBuffers(vd->Window);
	}
#endif
}

#endif