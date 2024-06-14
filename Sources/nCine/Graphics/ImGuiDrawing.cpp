#if defined(WITH_IMGUI)

#include "ImGuiDrawing.h"
#include "RenderQueue.h"
#include "RenderCommandPool.h"
#include "RenderResources.h"
#include "GL/GLTexture.h"
#include "GL/GLShaderProgram.h"
#include "GL/GLScissorTest.h"
#include "GL/GLBlending.h"
#include "GL/GLDepthTest.h"
#include "GL/GLCullFace.h"
#include "../Application.h"
#include "../Input/IInputManager.h"

// TODO: This include should be probably removed
#include "../Backends/GlfwGfxDevice.h"

#include <imgui.h>

#include <IO/FileSystem.h>

#if defined(WITH_GLFW)
#	include "../Backends/ImGuiGlfwInput.h"
#elif defined(WITH_SDL)
#	include "../Backends/ImGuiSdlInput.h"
#elif defined(WITH_QT5)
#	include "../Backends/ImGuiQt5Input.h"
#elif defined(DEATH_TARGET_ANDROID)
#	include "../Backends/Android/ImGuiAndroidInput.h"
#endif

#if defined(WITH_EMBEDDED_SHADERS)
#	include "shader_strings.h"
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

// TODO: Move this whole section probably to ImGuiGlfwInput
#if defined(WITH_GLFW)

#if defined(DEATH_TARGET_WINDOWS)
#	if !defined(GLFW_EXPOSE_NATIVE_WIN32)
#		define GLFW_EXPOSE_NATIVE_WIN32
#	endif
#	include <glfw3native.h>   // for glfwGetWin32Window()
#endif
#if defined(DEATH_TARGET_APPLE)
#	if !defined(GLFW_EXPOSE_NATIVE_COCOA)
#		define GLFW_EXPOSE_NATIVE_COCOA
#	endif
#	include <glfw3native.h>   // for glfwGetCocoaWindow()
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
#	define GLFW_HAS_NEW_CURSORS			(GLFW_VERSION_COMBINED >= 3400)	// 3.4+ GLFW_RESIZE_ALL_CURSOR, GLFW_RESIZE_NESW_CURSOR, GLFW_RESIZE_NWSE_CURSOR, GLFW_NOT_ALLOWED_CURSOR
#else
#	define GLFW_HAS_NEW_CURSORS			(0)
#endif
#if defined(GLFW_MOUSE_PASSTHROUGH)		// Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2020-07-17 (passthrough)
#	define GLFW_HAS_MOUSE_PASSTHROUGH	(GLFW_VERSION_COMBINED >= 3400)	// 3.4+ GLFW_MOUSE_PASSTHROUGH
#else
#	define GLFW_HAS_MOUSE_PASSTHROUGH	(0)
#endif
#define GLFW_HAS_GAMEPAD_API			(GLFW_VERSION_COMBINED >= 3300)	// 3.3+ glfwGetGamepadState() new api
#define GLFW_HAS_GETKEYNAME				(GLFW_VERSION_COMBINED >= 3200)	// 3.2+ glfwGetKeyName()
#define GLFW_HAS_GETERROR				(GLFW_VERSION_COMBINED >= 3300)	// 3.3+ glfwGetError()

// GLFW data
struct ImGui_ImplGlfw_Data
{
	GLFWwindow* Window;
	double                  Time;
	GLFWwindow* MouseWindow;
	GLFWcursor* MouseCursors[ImGuiMouseCursor_COUNT];
	ImVec2                  LastValidMousePos;
	GLFWwindow* KeyOwnerWindows[GLFW_KEY_LAST];
	bool                    InstalledCallbacks;
	bool                    CallbacksChainForAllWindows;
	bool                    WantUpdateMonitors;
#if defined(DEATH_TARGET_EMSCRIPTEN)
	const char* CanvasSelector;
#endif

	// Chain GLFW callbacks: our callbacks will call the user's previously installed callbacks, if any.
	GLFWwindowfocusfun      PrevUserCallbackWindowFocus;
	GLFWcursorposfun        PrevUserCallbackCursorPos;
	GLFWcursorenterfun      PrevUserCallbackCursorEnter;
	GLFWmousebuttonfun      PrevUserCallbackMousebutton;
	GLFWscrollfun           PrevUserCallbackScroll;
	GLFWkeyfun              PrevUserCallbackKey;
	GLFWcharfun             PrevUserCallbackChar;
	GLFWmonitorfun          PrevUserCallbackMonitor;
#if defined(DEATH_TARGET_WINDOWS)
	WNDPROC                 PrevWndProc;
#endif

	ImGui_ImplGlfw_Data() {
		std::memset((void*)this, 0, sizeof(*this));
	}
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// - Because glfwPollEvents() process all windows and some events may be called outside of it, you will need to register your own callbacks
//   (passing install_callbacks=false in ImGui_ImplGlfw_InitXXX functions), set the current dear imgui context and then call our callbacks.
// - Otherwise we may need to store a GLFWWindow* -> ImGuiContext* map and handle this in the backend, adding a little bit of extra complexity to it.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
static ImGui_ImplGlfw_Data* ImGui_ImplGlfw_GetBackendData()
{
	return ImGui::GetCurrentContext() ? (ImGui_ImplGlfw_Data*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

// Forward Declarations
static void ImGui_ImplGlfw_UpdateMonitors();
static void ImGui_ImplGlfw_InitPlatformInterface();
static void ImGui_ImplGlfw_ShutdownPlatformInterface();

// Functions
static ImGuiKey ImGui_ImplGlfw_KeyToImGuiKey(int key)
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
		case GLFW_KEY_F13: return ImGuiKey_F13;
		case GLFW_KEY_F14: return ImGuiKey_F14;
		case GLFW_KEY_F15: return ImGuiKey_F15;
		case GLFW_KEY_F16: return ImGuiKey_F16;
		case GLFW_KEY_F17: return ImGuiKey_F17;
		case GLFW_KEY_F18: return ImGuiKey_F18;
		case GLFW_KEY_F19: return ImGuiKey_F19;
		case GLFW_KEY_F20: return ImGuiKey_F20;
		case GLFW_KEY_F21: return ImGuiKey_F21;
		case GLFW_KEY_F22: return ImGuiKey_F22;
		case GLFW_KEY_F23: return ImGuiKey_F23;
		case GLFW_KEY_F24: return ImGuiKey_F24;
		default: return ImGuiKey_None;
	}
}

// X11 does not include current pressed/released modifier key in 'mods' flags submitted by GLFW
// See https://github.com/ocornut/imgui/issues/6034 and https://github.com/glfw/glfw/issues/1630
static void ImGui_ImplGlfw_UpdateKeyModifiers(GLFWwindow* window)
{
	ImGuiIO& io = ImGui::GetIO();
	io.AddKeyEvent(ImGuiMod_Ctrl, (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));
	io.AddKeyEvent(ImGuiMod_Shift, (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS));
	io.AddKeyEvent(ImGuiMod_Alt, (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS));
	io.AddKeyEvent(ImGuiMod_Super, (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS) || (glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS));
}

static bool ImGui_ImplGlfw_ShouldChainCallback(GLFWwindow* window)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	return bd->CallbacksChainForAllWindows ? true : (window == bd->Window);
}

// TODO: Should be the same as ImGuiGlfwInput::mouseButtonCallback
void ImGui_ImplGlfw_MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if (bd->PrevUserCallbackMousebutton != nullptr && ImGui_ImplGlfw_ShouldChainCallback(window))
		bd->PrevUserCallbackMousebutton(window, button, action, mods);

	ImGui_ImplGlfw_UpdateKeyModifiers(window);

	ImGuiIO& io = ImGui::GetIO();
	if (button >= 0 && button < ImGuiMouseButton_COUNT)
		io.AddMouseButtonEvent(button, action == GLFW_PRESS);
}

// TODO: Should be the same as ImGuiGlfwInput::scrollCallback
void ImGui_ImplGlfw_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if (bd->PrevUserCallbackScroll != nullptr && ImGui_ImplGlfw_ShouldChainCallback(window))
		bd->PrevUserCallbackScroll(window, xoffset, yoffset);

#if defined(DEATH_TARGET_EMSCRIPTEN)
	// Ignore GLFW events: will be processed in ImGui_ImplEmscripten_WheelCallback().
	return;
#endif

	ImGuiIO& io = ImGui::GetIO();
	io.AddMouseWheelEvent((float)xoffset, (float)yoffset);
}

static int ImGui_ImplGlfw_TranslateUntranslatedKey(int key, int scancode)
{
#if GLFW_HAS_GETKEYNAME && !defined(DEATH_TARGET_EMSCRIPTEN)
	// GLFW 3.1+ attempts to "untranslate" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
	// (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
	// See https://github.com/glfw/glfw/issues/1502 for details.
	// Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
	// This won't cover edge cases but this is at least going to cover common cases.
	if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
		return key;
	GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
	const char* key_name = glfwGetKeyName(key, scancode);
	glfwSetErrorCallback(prev_error_callback);
#if GLFW_HAS_GETERROR && !defined(DEATH_TARGET_EMSCRIPTEN) // Eat errors (see #5908)
	(void)glfwGetError(nullptr);
#endif
	if (key_name && key_name[0] != 0 && key_name[1] == 0) {
		const char char_names[] = "`-=[]\\,;\'./";
		const int char_keys[] = { GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
		IM_ASSERT(IM_ARRAYSIZE(char_names) == IM_ARRAYSIZE(char_keys));
		if (key_name[0] >= '0' && key_name[0] <= '9') {
			key = GLFW_KEY_0 + (key_name[0] - '0');
		} else if (key_name[0] >= 'A' && key_name[0] <= 'Z') {
			key = GLFW_KEY_A + (key_name[0] - 'A');
		} else if (key_name[0] >= 'a' && key_name[0] <= 'z') {
			key = GLFW_KEY_A + (key_name[0] - 'a');
		} else if (const char* p = strchr(char_names, key_name[0])) {
			key = char_keys[p - char_names];
		}
	}
	// if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
#else
	IM_UNUSED(scancode);
#endif
	return key;
}

// TODO: ImGuiGlfwInput::keyCallback must be changed
void ImGui_ImplGlfw_KeyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if (bd->PrevUserCallbackKey != nullptr && ImGui_ImplGlfw_ShouldChainCallback(window))
		bd->PrevUserCallbackKey(window, keycode, scancode, action, mods);

	if (action != GLFW_PRESS && action != GLFW_RELEASE)
		return;

	ImGui_ImplGlfw_UpdateKeyModifiers(window);

	if (keycode >= 0 && keycode < IM_ARRAYSIZE(bd->KeyOwnerWindows))
		bd->KeyOwnerWindows[keycode] = (action == GLFW_PRESS) ? window : nullptr;

	keycode = ImGui_ImplGlfw_TranslateUntranslatedKey(keycode, scancode);

	ImGuiIO& io = ImGui::GetIO();
	ImGuiKey imgui_key = ImGui_ImplGlfw_KeyToImGuiKey(keycode);
	io.AddKeyEvent(imgui_key, (action == GLFW_PRESS));
	io.SetKeyEventNativeData(imgui_key, keycode, scancode); // To support legacy indexing (<1.87 user code)
}

// TODO: Should be the same as ImGuiGlfwInput::windowFocusCallback
void ImGui_ImplGlfw_WindowFocusCallback(GLFWwindow* window, int focused)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if (bd->PrevUserCallbackWindowFocus != nullptr && ImGui_ImplGlfw_ShouldChainCallback(window))
		bd->PrevUserCallbackWindowFocus(window, focused);

	ImGuiIO& io = ImGui::GetIO();
	io.AddFocusEvent(focused != 0);
}

// TODO: ImGuiGlfwInput::cursorPosCallback must be changed
void ImGui_ImplGlfw_CursorPosCallback(GLFWwindow* window, double x, double y)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if (bd->PrevUserCallbackCursorPos != nullptr && ImGui_ImplGlfw_ShouldChainCallback(window))
		bd->PrevUserCallbackCursorPos(window, x, y);

	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		int window_x, window_y;
		glfwGetWindowPos(window, &window_x, &window_y);
		x += window_x;
		y += window_y;
	}
	io.AddMousePosEvent((float)x, (float)y);
	bd->LastValidMousePos = ImVec2((float)x, (float)y);
}

// TODO: ImGuiGlfwInput::cursorEnterCallback must be changed
// Workaround: X11 seems to send spurious Leave/Enter events which would make us lose our position,
// so we back it up and restore on Leave/Enter (see https://github.com/ocornut/imgui/issues/4984)
void ImGui_ImplGlfw_CursorEnterCallback(GLFWwindow* window, int entered)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if (bd->PrevUserCallbackCursorEnter != nullptr && ImGui_ImplGlfw_ShouldChainCallback(window))
		bd->PrevUserCallbackCursorEnter(window, entered);

	ImGuiIO& io = ImGui::GetIO();
	if (entered) {
		bd->MouseWindow = window;
		io.AddMousePosEvent(bd->LastValidMousePos.x, bd->LastValidMousePos.y);
	} else if (!entered && bd->MouseWindow == window) {
		bd->LastValidMousePos = io.MousePos;
		bd->MouseWindow = nullptr;
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
	}
}

// TODO: Should be the same as ImGuiGlfwInput::charCallback
void ImGui_ImplGlfw_CharCallback(GLFWwindow* window, unsigned int c)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if (bd->PrevUserCallbackChar != nullptr && ImGui_ImplGlfw_ShouldChainCallback(window))
		bd->PrevUserCallbackChar(window, c);

	ImGuiIO& io = ImGui::GetIO();
	io.AddInputCharacter(c);
}

// TODO: ImGuiGlfwInput::monitorCallback must be changed
void ImGui_ImplGlfw_MonitorCallback(GLFWmonitor*, int)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	bd->WantUpdateMonitors = true;
}

#ifdef _WIN32
static LRESULT CALLBACK ImGui_ImplGlfw_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

void ImGui_ImplGlfw_InstallCallbacks(GLFWwindow* window)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	IM_ASSERT(bd->InstalledCallbacks == false && "Callbacks already installed!");
	IM_ASSERT(bd->Window == window);

	//bd->PrevUserCallbackWindowFocus = glfwSetWindowFocusCallback(window, ImGui_ImplGlfw_WindowFocusCallback);
	bd->PrevUserCallbackCursorEnter = glfwSetCursorEnterCallback(window, ImGui_ImplGlfw_CursorEnterCallback);
	bd->PrevUserCallbackCursorPos = glfwSetCursorPosCallback(window, ImGui_ImplGlfw_CursorPosCallback);
	//bd->PrevUserCallbackMousebutton = glfwSetMouseButtonCallback(window, ImGui_ImplGlfw_MouseButtonCallback);
	//bd->PrevUserCallbackScroll = glfwSetScrollCallback(window, ImGui_ImplGlfw_ScrollCallback);
	bd->PrevUserCallbackKey = glfwSetKeyCallback(window, ImGui_ImplGlfw_KeyCallback);
	//bd->PrevUserCallbackChar = glfwSetCharCallback(window, ImGui_ImplGlfw_CharCallback);
	bd->PrevUserCallbackMonitor = glfwSetMonitorCallback(ImGui_ImplGlfw_MonitorCallback);
	bd->InstalledCallbacks = true;
}

void ImGui_ImplGlfw_RestoreCallbacks(GLFWwindow* window)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	IM_ASSERT(bd->InstalledCallbacks == true && "Callbacks not installed!");
	IM_ASSERT(bd->Window == window);

	//glfwSetWindowFocusCallback(window, bd->PrevUserCallbackWindowFocus);
	glfwSetCursorEnterCallback(window, bd->PrevUserCallbackCursorEnter);
	glfwSetCursorPosCallback(window, bd->PrevUserCallbackCursorPos);
	//glfwSetMouseButtonCallback(window, bd->PrevUserCallbackMousebutton);
	//glfwSetScrollCallback(window, bd->PrevUserCallbackScroll);
	glfwSetKeyCallback(window, bd->PrevUserCallbackKey);
	//glfwSetCharCallback(window, bd->PrevUserCallbackChar);
	glfwSetMonitorCallback(bd->PrevUserCallbackMonitor);
	bd->InstalledCallbacks = false;
	//bd->PrevUserCallbackWindowFocus = nullptr;
	bd->PrevUserCallbackCursorEnter = nullptr;
	bd->PrevUserCallbackCursorPos = nullptr;
	//bd->PrevUserCallbackMousebutton = nullptr;
	//bd->PrevUserCallbackScroll = nullptr;
	bd->PrevUserCallbackKey = nullptr;
	//bd->PrevUserCallbackChar = nullptr;
	bd->PrevUserCallbackMonitor = nullptr;
}

// Set to 'true' to enable chaining installed callbacks for all windows (including secondary viewports created by backends or by user.
// This is 'false' by default meaning we only chain callbacks for the main viewport.
// We cannot set this to 'true' by default because user callbacks code may be not testing the 'window' parameter of their callback.
// If you set this to 'true' your user callback code will need to make sure you are testing the 'window' parameter.
/*void ImGui_ImplGlfw_SetCallbacksChainForAllWindows(bool chain_for_all_windows)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	bd->CallbacksChainForAllWindows = chain_for_all_windows;
}*/

static bool ImGui_ImplGlfw_Init(GLFWwindow* window, bool install_callbacks)
{
	ImGuiIO& io = ImGui::GetIO();
	IMGUI_CHECKVERSION();
	IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

	// Setup backend capabilities flags
	ImGui_ImplGlfw_Data* bd = IM_NEW(ImGui_ImplGlfw_Data)();
	io.BackendPlatformUserData = (void*)bd;
	io.BackendPlatformName = "imgui_impl_glfw";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
#if !defined(DEATH_TARGET_EMSCRIPTEN)
	io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;    // We can create multi-viewports on the Platform side (optional)

	// TODO: Cannot be set in ImGui_ImplOpenGL3_CreateDeviceObjects, too late
	io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
#endif
#if GLFW_HAS_MOUSE_PASSTHROUGH || GLFW_HAS_WINDOW_HOVERED
	io.BackendFlags |= ImGuiBackendFlags_HasMouseHoveredViewport; // We can call io.AddMouseViewportEvent() with correct data (optional)
#endif

	bd->Window = window;
	bd->Time = 0.0;
	bd->WantUpdateMonitors = true;

	// TODO: Already done in ImGuiGlfwInput
	// Create mouse cursors
	// (By design, on X11 cursors are user configurable and some cursors may be missing. When a cursor doesn't exist,
	// GLFW will emit an error which will often be printed by the app, so we temporarily disable error reporting.
	// Missing cursors will return nullptr and our _UpdateMouseCursor() function will use the Arrow cursor instead.)
	GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
	bd->MouseCursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
#if GLFW_HAS_NEW_CURSORS
	bd->MouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
#else
	bd->MouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
	bd->MouseCursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
#endif
	glfwSetErrorCallback(prev_error_callback);
#if GLFW_HAS_GETERROR && !defined(DEATH_TARGET_EMSCRIPTEN) // Eat errors (see #5908)
	(void)glfwGetError(nullptr);
#endif

	// Chain GLFW callbacks: our callbacks will call the user's previously installed callbacks, if any.
	if (install_callbacks)
		ImGui_ImplGlfw_InstallCallbacks(window);

	// Update monitors the first time (note: monitor callback are broken in GLFW 3.2 and earlier, see github.com/glfw/glfw/issues/784)
	ImGui_ImplGlfw_UpdateMonitors();
	glfwSetMonitorCallback(ImGui_ImplGlfw_MonitorCallback);

	// Set platform dependent data in viewport
	ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	main_viewport->PlatformHandle = (void*)bd->Window;
#if defined(DEATH_TARGET_WINDOWS)
	main_viewport->PlatformHandleRaw = glfwGetWin32Window(bd->Window);
#elif defined(DEATH_TARGET_APPLE)
	main_viewport->PlatformHandleRaw = (void*)glfwGetCocoaWindow(bd->Window);
#else
	IM_UNUSED(main_viewport);
#endif
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		ImGui_ImplGlfw_InitPlatformInterface();

	// Windows: register a WndProc hook so we can intercept some messages.
#if defined(DEATH_TARGET_WINDOWS)
	bd->PrevWndProc = (WNDPROC)::GetWindowLongPtrW((HWND)main_viewport->PlatformHandleRaw, GWLP_WNDPROC);
	IM_ASSERT(bd->PrevWndProc != nullptr);
	::SetWindowLongPtrW((HWND)main_viewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)ImGui_ImplGlfw_WndProc);
#endif

	return true;
}

void ImGui_ImplGlfw_Shutdown()
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplGlfw_ShutdownPlatformInterface();

	if (bd->InstalledCallbacks)
		ImGui_ImplGlfw_RestoreCallbacks(bd->Window);

	for (ImGuiMouseCursor cursor_n = 0; cursor_n < ImGuiMouseCursor_COUNT; cursor_n++)
		glfwDestroyCursor(bd->MouseCursors[cursor_n]);

	// Windows: restore our WndProc hook
#if defined(DEATH_TARGET_WINDOWS)
	ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	::SetWindowLongPtrW((HWND)main_viewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)bd->PrevWndProc);
	bd->PrevWndProc = nullptr;
#endif

	io.BackendPlatformName = nullptr;
	io.BackendPlatformUserData = nullptr;
	io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_HasMouseHoveredViewport);
	IM_DELETE(bd);
}

// TODO: ImGuiGlfwInput::updateMouseData must be changed
static void ImGui_ImplGlfw_UpdateMouseData()
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	ImGuiIO& io = ImGui::GetIO();
	ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

	ImGuiID mouse_viewport_id = 0;
	const ImVec2 mouse_pos_prev = io.MousePos;
	for (int n = 0; n < platform_io.Viewports.Size; n++) {
		ImGuiViewport* viewport = platform_io.Viewports[n];
		GLFWwindow* window = (GLFWwindow*)viewport->PlatformHandle;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		const bool is_window_focused = true;
#else
		const bool is_window_focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
#endif
		if (is_window_focused) {
			// (Optional) Set OS mouse position from Dear ImGui if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
			// When multi-viewports are enabled, all Dear ImGui positions are same as OS positions.
			if (io.WantSetMousePos)
				glfwSetCursorPos(window, (double)(mouse_pos_prev.x - viewport->Pos.x), (double)(mouse_pos_prev.y - viewport->Pos.y));

			// (Optional) Fallback to provide mouse position when focused (ImGui_ImplGlfw_CursorPosCallback already provides this when hovered or captured)
			if (bd->MouseWindow == nullptr) {
				double mouse_x, mouse_y;
				glfwGetCursorPos(window, &mouse_x, &mouse_y);
				if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
					// Single viewport mode: mouse position in client window coordinates (io.MousePos is (0,0) when the mouse is on the upper-left corner of the app window)
					// Multi-viewport mode: mouse position in OS absolute coordinates (io.MousePos is (0,0) when the mouse is on the upper-left of the primary monitor)
					int window_x, window_y;
					glfwGetWindowPos(window, &window_x, &window_y);
					mouse_x += window_x;
					mouse_y += window_y;
				}
				bd->LastValidMousePos = ImVec2((float)mouse_x, (float)mouse_y);
				io.AddMousePosEvent((float)mouse_x, (float)mouse_y);
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
#if GLFW_HAS_MOUSE_PASSTHROUGH
		const bool window_no_input = (viewport->Flags & ImGuiViewportFlags_NoInputs) != 0;
		glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, window_no_input);
#endif
#if GLFW_HAS_MOUSE_PASSTHROUGH || GLFW_HAS_WINDOW_HOVERED
		if (glfwGetWindowAttrib(window, GLFW_HOVERED))
			mouse_viewport_id = viewport->ID;
#else
		// We cannot use bd->MouseWindow maintained from CursorEnter/Leave callbacks, because it is locked to the window capturing mouse.
#endif
	}

	if (io.BackendFlags & ImGuiBackendFlags_HasMouseHoveredViewport)
		io.AddMouseViewportEvent(mouse_viewport_id);
}

// TODO: ImGuiGlfwInput::updateMouseCursor must be changed
static void ImGui_ImplGlfw_UpdateMouseCursor()
{
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(bd->Window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
		return;

	ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
	ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
	for (int n = 0; n < platform_io.Viewports.Size; n++) {
		GLFWwindow* window = (GLFWwindow*)platform_io.Viewports[n]->PlatformHandle;
		if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
			// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
		} else {
			// Show OS mouse cursor
			// FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works here.
			glfwSetCursor(window, bd->MouseCursors[imgui_cursor] ? bd->MouseCursors[imgui_cursor] : bd->MouseCursors[ImGuiMouseCursor_Arrow]);
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}
}

// Update gamepad inputs
static inline float Saturate(float v) {
	return v < 0.0f ? 0.0f : v  > 1.0f ? 1.0f : v;
}

// TODO: ImGuiGlfwInput::updateGamepads must be changed
static void ImGui_ImplGlfw_UpdateGamepads()
{
	ImGuiIO& io = ImGui::GetIO();
	if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0) // FIXME: Technically feeding gamepad shouldn't depend on this now that they are regular inputs.
		return;

	io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
#if GLFW_HAS_GAMEPAD_API && !defined(__EMSCRIPTEN__)
	GLFWgamepadstate gamepad;
	if (!glfwGetGamepadState(GLFW_JOYSTICK_1, &gamepad))
		return;
#define MAP_BUTTON(KEY_NO, BUTTON_NO, _UNUSED)          do { io.AddKeyEvent(KEY_NO, gamepad.buttons[BUTTON_NO] != 0); } while (0)
#define MAP_ANALOG(KEY_NO, AXIS_NO, _UNUSED, V0, V1)    do { float v = gamepad.axes[AXIS_NO]; v = (v - V0) / (V1 - V0); io.AddKeyAnalogEvent(KEY_NO, v > 0.10f, Saturate(v)); } while (0)
#else
	int axes_count = 0, buttons_count = 0;
	const float* axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &axes_count);
	const unsigned char* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &buttons_count);
	if (axes_count == 0 || buttons_count == 0)
		return;
#define MAP_BUTTON(KEY_NO, _UNUSED, BUTTON_NO)          do { io.AddKeyEvent(KEY_NO, (buttons_count > BUTTON_NO && buttons[BUTTON_NO] == GLFW_PRESS)); } while (0)
#define MAP_ANALOG(KEY_NO, _UNUSED, AXIS_NO, V0, V1)    do { float v = (axes_count > AXIS_NO) ? axes[AXIS_NO] : V0; v = (v - V0) / (V1 - V0); io.AddKeyAnalogEvent(KEY_NO, v > 0.10f, Saturate(v)); } while (0)
#endif
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
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
}

// TODO: ImGuiGlfwInput::monitorCallback must be changed and ImGuiGlfwInput::updateMonitors must be added
static void ImGui_ImplGlfw_UpdateMonitors()
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
	bd->WantUpdateMonitors = false;

	int monitors_count = 0;
	GLFWmonitor** glfw_monitors = glfwGetMonitors(&monitors_count);
	if (monitors_count == 0) // Preserve existing monitor list if there are none. Happens on macOS sleeping (#5683)
		return;

	platform_io.Monitors.resize(0);
	for (int n = 0; n < monitors_count; n++) {
		ImGuiPlatformMonitor monitor;
		int x, y;
		glfwGetMonitorPos(glfw_monitors[n], &x, &y);
		const GLFWvidmode* vid_mode = glfwGetVideoMode(glfw_monitors[n]);
		if (vid_mode == nullptr)
			continue; // Failed to get Video mode (e.g. Emscripten does not support this function)
		monitor.MainPos = monitor.WorkPos = ImVec2((float)x, (float)y);
		monitor.MainSize = monitor.WorkSize = ImVec2((float)vid_mode->width, (float)vid_mode->height);
#if GLFW_HAS_MONITOR_WORK_AREA
		int w, h;
		glfwGetMonitorWorkarea(glfw_monitors[n], &x, &y, &w, &h);
		if (w > 0 && h > 0) // Workaround a small GLFW issue reporting zero on monitor changes: https://github.com/glfw/glfw/pull/1761
		{
			monitor.WorkPos = ImVec2((float)x, (float)y);
			monitor.WorkSize = ImVec2((float)w, (float)h);
		}
#endif
#if GLFW_HAS_PER_MONITOR_DPI
		// Warning: the validity of monitor DPI information on Windows depends on the application DPI awareness settings, which generally needs to be set in the manifest or at runtime.
		float x_scale, y_scale;
		glfwGetMonitorContentScale(glfw_monitors[n], &x_scale, &y_scale);
		monitor.DpiScale = x_scale;
#endif
		monitor.PlatformHandle = (void*)glfw_monitors[n]; // [...] GLFW doc states: "guaranteed to be valid only until the monitor configuration changes"
		platform_io.Monitors.push_back(monitor);
	}
}

void ImGui_ImplGlfw_NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplGlfw_InitForXXX()?");

	// Setup display size (every frame to accommodate for window resizing)
	int w, h;
	int display_w, display_h;
	glfwGetWindowSize(bd->Window, &w, &h);
	glfwGetFramebufferSize(bd->Window, &display_w, &display_h);
	io.DisplaySize = ImVec2((float)w, (float)h);
	if (w > 0 && h > 0)
		io.DisplayFramebufferScale = ImVec2((float)display_w / (float)w, (float)display_h / (float)h);
	if (bd->WantUpdateMonitors)
		ImGui_ImplGlfw_UpdateMonitors();

	// Setup time step
	// (Accept glfwGetTime() not returning a monotonically increasing value. Seems to happens on disconnecting peripherals and probably on VMs and Emscripten, see #6491, #6189, #6114, #3644)
	double current_time = glfwGetTime();
	if (current_time <= bd->Time)
		current_time = bd->Time + 0.00001f;
	io.DeltaTime = bd->Time > 0.0 ? (float)(current_time - bd->Time) : (float)(1.0f / 60.0f);
	bd->Time = current_time;

	ImGui_ImplGlfw_UpdateMouseData();
	ImGui_ImplGlfw_UpdateMouseCursor();

	// Update game controllers (if enabled and available)
	ImGui_ImplGlfw_UpdateGamepads();
}

#if defined(DEATH_TARGET_EMSCRIPTEN)
static EM_BOOL ImGui_ImplGlfw_OnCanvasSizeChange(int event_type, const EmscriptenUiEvent* event, void* user_data)
{
	ImGui_ImplGlfw_Data* bd = (ImGui_ImplGlfw_Data*)user_data;
	double canvas_width, canvas_height;
	emscripten_get_element_css_size(bd->CanvasSelector, &canvas_width, &canvas_height);
	glfwSetWindowSize(bd->Window, (int)canvas_width, (int)canvas_height);
	return true;
}

static EM_BOOL ImGui_ImplEmscripten_FullscreenChangeCallback(int event_type, const EmscriptenFullscreenChangeEvent* event, void* user_data)
{
	ImGui_ImplGlfw_Data* bd = (ImGui_ImplGlfw_Data*)user_data;
	double canvas_width, canvas_height;
	emscripten_get_element_css_size(bd->CanvasSelector, &canvas_width, &canvas_height);
	glfwSetWindowSize(bd->Window, (int)canvas_width, (int)canvas_height);
	return true;
}

// 'canvas_selector' is a CSS selector. The event listener is applied to the first element that matches the query.
// STRING MUST PERSIST FOR THE APPLICATION DURATION. PLEASE USE A STRING LITERAL OR ENSURE POINTER WILL STAY VALID.
void ImGui_ImplGlfw_InstallEmscriptenCanvasResizeCallback(const char* canvas_selector)
{
	IM_ASSERT(canvas_selector != nullptr);
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplGlfw_InitForXXX()?");

	bd->CanvasSelector = canvas_selector;
	emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, bd, false, ImGui_ImplGlfw_OnCanvasSizeChange);
	emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, bd, false, ImGui_ImplEmscripten_FullscreenChangeCallback);

	// Change the size of the GLFW window according to the size of the canvas
	ImGui_ImplGlfw_OnCanvasSizeChange(EMSCRIPTEN_EVENT_RESIZE, {}, bd);
}
#endif


//--------------------------------------------------------------------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
// This is an _advanced_ and _optional_ feature, allowing the backend to create and handle multiple viewports simultaneously.
// If you are new to dear imgui or creating a new binding for dear imgui, it is recommended that you completely ignore this section first..
//--------------------------------------------------------------------------------------------------------

// Helper structure we store in the void* RendererUserData field of each ImGuiViewport to easily retrieve our backend data.
struct ImGui_ImplGlfw_ViewportData
{
	GLFWwindow* Window;
	bool        WindowOwned;
	int         IgnoreWindowPosEventFrame;
	int         IgnoreWindowSizeEventFrame;
#ifdef _WIN32
	WNDPROC     PrevWndProc;
#endif

	ImGui_ImplGlfw_ViewportData() {
		memset(this, 0, sizeof(*this)); IgnoreWindowSizeEventFrame = IgnoreWindowPosEventFrame = -1;
	}
	~ImGui_ImplGlfw_ViewportData() {
		IM_ASSERT(Window == nullptr);
	}
};

static void ImGui_ImplGlfw_WindowCloseCallback(GLFWwindow* window)
{
	if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window))
		viewport->PlatformRequestClose = true;
}

// GLFW may dispatch window pos/size events after calling glfwSetWindowPos()/glfwSetWindowSize().
// However: depending on the platform the callback may be invoked at different time:
// - on Windows it appears to be called within the glfwSetWindowPos()/glfwSetWindowSize() call
// - on Linux it is queued and invoked during glfwPollEvents()
// Because the event doesn't always fire on glfwSetWindowXXX() we use a frame counter tag to only
// ignore recent glfwSetWindowXXX() calls.
static void ImGui_ImplGlfw_WindowPosCallback(GLFWwindow* window, int, int)
{
	if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window)) {
		if (ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData) {
			bool ignore_event = (ImGui::GetFrameCount() <= vd->IgnoreWindowPosEventFrame + 1);
			//data->IgnoreWindowPosEventFrame = -1;
			if (ignore_event)
				return;
		}
		viewport->PlatformRequestMove = true;
	}
}

static void ImGui_ImplGlfw_WindowSizeCallback(GLFWwindow* window, int, int)
{
	if (ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window)) {
		if (ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData) {
			bool ignore_event = (ImGui::GetFrameCount() <= vd->IgnoreWindowSizeEventFrame + 1);
			//data->IgnoreWindowSizeEventFrame = -1;
			if (ignore_event)
				return;
		}
		viewport->PlatformRequestResize = true;
	}
}

static void ImGui_ImplGlfw_CreateWindow(ImGuiViewport* viewport)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	ImGui_ImplGlfw_ViewportData* vd = IM_NEW(ImGui_ImplGlfw_ViewportData)();
	viewport->PlatformUserData = vd;

	// GLFW 3.2 unfortunately always set focus on glfwCreateWindow() if GLFW_VISIBLE is set, regardless of GLFW_FOCUSED
	// With GLFW 3.3, the hint GLFW_FOCUS_ON_SHOW fixes this problem
	glfwWindowHint(GLFW_VISIBLE, false);
	glfwWindowHint(GLFW_FOCUSED, false);
#if GLFW_HAS_FOCUS_ON_SHOW
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, false);
#endif
	glfwWindowHint(GLFW_DECORATED, (viewport->Flags & ImGuiViewportFlags_NoDecoration) ? false : true);
#if GLFW_HAS_WINDOW_TOPMOST
	glfwWindowHint(GLFW_FLOATING, (viewport->Flags & ImGuiViewportFlags_TopMost) ? true : false);
#endif
	GLFWwindow* share_window = bd->Window;
	vd->Window = glfwCreateWindow((int)viewport->Size.x, (int)viewport->Size.y, "No Title Yet", nullptr, share_window);
	vd->WindowOwned = true;
	viewport->PlatformHandle = (void*)vd->Window;
#if defined(DEATH_TARGET_WINDOWS)
	viewport->PlatformHandleRaw = glfwGetWin32Window(vd->Window);
#elif defined(DEATH_TARGET_APPLE)
	viewport->PlatformHandleRaw = (void*)glfwGetCocoaWindow(vd->Window);
#endif
	glfwSetWindowPos(vd->Window, (int)viewport->Pos.x, (int)viewport->Pos.y);

	// Install GLFW callbacks for secondary viewports
	glfwSetWindowFocusCallback(vd->Window, ImGui_ImplGlfw_WindowFocusCallback);
	glfwSetCursorEnterCallback(vd->Window, ImGui_ImplGlfw_CursorEnterCallback);
	glfwSetCursorPosCallback(vd->Window, ImGui_ImplGlfw_CursorPosCallback);
	glfwSetMouseButtonCallback(vd->Window, ImGui_ImplGlfw_MouseButtonCallback);
	glfwSetScrollCallback(vd->Window, ImGui_ImplGlfw_ScrollCallback);
	glfwSetKeyCallback(vd->Window, ImGui_ImplGlfw_KeyCallback);
	glfwSetCharCallback(vd->Window, ImGui_ImplGlfw_CharCallback);
	glfwSetWindowCloseCallback(vd->Window, ImGui_ImplGlfw_WindowCloseCallback);
	glfwSetWindowPosCallback(vd->Window, ImGui_ImplGlfw_WindowPosCallback);
	glfwSetWindowSizeCallback(vd->Window, ImGui_ImplGlfw_WindowSizeCallback);

	glfwMakeContextCurrent(vd->Window);
	glfwSwapInterval(0);
}

static void ImGui_ImplGlfw_DestroyWindow(ImGuiViewport* viewport)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	if (ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData) {
		if (vd->WindowOwned) {
#if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED && defined(DEATH_TARGET_WINDOWS)
			HWND hwnd = (HWND)viewport->PlatformHandleRaw;
			::RemovePropA(hwnd, "IMGUI_VIEWPORT");
#endif

			// Release any keys that were pressed in the window being destroyed and are still held down,
			// because we will not receive any release events after window is destroyed.
			for (int i = 0; i < IM_ARRAYSIZE(bd->KeyOwnerWindows); i++)
				if (bd->KeyOwnerWindows[i] == vd->Window)
					ImGui_ImplGlfw_KeyCallback(vd->Window, i, 0, GLFW_RELEASE, 0); // Later params are only used for main viewport, on which this function is never called.

			glfwDestroyWindow(vd->Window);
		}
		vd->Window = nullptr;
		IM_DELETE(vd);
	}
	viewport->PlatformUserData = viewport->PlatformHandle = nullptr;
}

static void ImGui_ImplGlfw_ShowWindow(ImGuiViewport* viewport)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;

#if defined(DEATH_TARGET_WINDOWS)
	// GLFW hack: Hide icon from task bar
	HWND hwnd = (HWND)viewport->PlatformHandleRaw;
	if (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) {
		LONG ex_style = ::GetWindowLong(hwnd, GWL_EXSTYLE);
		ex_style &= ~WS_EX_APPWINDOW;
		ex_style |= WS_EX_TOOLWINDOW;
		::SetWindowLong(hwnd, GWL_EXSTYLE, ex_style);
	}

	// GLFW hack: install hook for WM_NCHITTEST message handler
#	if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED && defined(DEATH_TARGET_WINDOWS)
	::SetPropA(hwnd, "IMGUI_VIEWPORT", viewport);
	vd->PrevWndProc = (WNDPROC)::GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
	::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)ImGui_ImplGlfw_WndProc);
#	endif

#	if !GLFW_HAS_FOCUS_ON_SHOW
	// GLFW hack: GLFW 3.2 has a bug where glfwShowWindow() also activates/focus the window.
	// The fix was pushed to GLFW repository on 2018/01/09 and should be included in GLFW 3.3 via a GLFW_FOCUS_ON_SHOW window attribute.
	// See https://github.com/glfw/glfw/issues/1189
	// FIXME-VIEWPORT: Implement same work-around for Linux/OSX in the meanwhile.
	if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing) {
		::ShowWindow(hwnd, SW_SHOWNA);
		return;
	}
#	endif
#endif

	glfwShowWindow(vd->Window);
}

static ImVec2 ImGui_ImplGlfw_GetWindowPos(ImGuiViewport* viewport)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
	int x = 0, y = 0;
	glfwGetWindowPos(vd->Window, &x, &y);
	return ImVec2((float)x, (float)y);
}

static void ImGui_ImplGlfw_SetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
	vd->IgnoreWindowPosEventFrame = ImGui::GetFrameCount();
	glfwSetWindowPos(vd->Window, (int)pos.x, (int)pos.y);
}

static ImVec2 ImGui_ImplGlfw_GetWindowSize(ImGuiViewport* viewport)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
	int w = 0, h = 0;
	glfwGetWindowSize(vd->Window, &w, &h);
	return ImVec2((float)w, (float)h);
}

static void ImGui_ImplGlfw_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
#if defined(DEATH_TARGET_APPLE) && !GLFW_HAS_OSX_WINDOW_POS_FIX
	// Native OS windows are positioned from the bottom-left corner on macOS, whereas on other platforms they are
	// positioned from the upper-left corner. GLFW makes an effort to convert macOS style coordinates, however it
	// doesn't handle it when changing size. We are manually moving the window in order for changes of size to be based
	// on the upper-left corner.
	int x, y, width, height;
	glfwGetWindowPos(vd->Window, &x, &y);
	glfwGetWindowSize(vd->Window, &width, &height);
	glfwSetWindowPos(vd->Window, x, y - height + size.y);
#endif
	vd->IgnoreWindowSizeEventFrame = ImGui::GetFrameCount();
	glfwSetWindowSize(vd->Window, (int)size.x, (int)size.y);
}

static void ImGui_ImplGlfw_SetWindowTitle(ImGuiViewport* viewport, const char* title)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
	glfwSetWindowTitle(vd->Window, title);
}

static void ImGui_ImplGlfw_SetWindowFocus(ImGuiViewport* viewport)
{
#if GLFW_HAS_FOCUS_WINDOW
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
	glfwFocusWindow(vd->Window);
#else
	// FIXME: What are the effect of not having this function? At the moment imgui doesn't actually call SetWindowFocus - we set that up ahead, will answer that question later.
	(void)viewport;
#endif
}

static bool ImGui_ImplGlfw_GetWindowFocus(ImGuiViewport* viewport)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
	return glfwGetWindowAttrib(vd->Window, GLFW_FOCUSED) != 0;
}

static bool ImGui_ImplGlfw_GetWindowMinimized(ImGuiViewport* viewport)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
	return glfwGetWindowAttrib(vd->Window, GLFW_ICONIFIED) != 0;
}

#if GLFW_HAS_WINDOW_ALPHA
static void ImGui_ImplGlfw_SetWindowAlpha(ImGuiViewport* viewport, float alpha)
{
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;
	glfwSetWindowOpacity(vd->Window, alpha);
}
#endif

static void ImGui_ImplGlfw_RenderWindow(ImGuiViewport* viewport, void*)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;

	glfwMakeContextCurrent(vd->Window);
}

static void ImGui_ImplGlfw_SwapBuffers(ImGuiViewport* viewport, void*)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData;

	glfwMakeContextCurrent(vd->Window);
	glfwSwapBuffers(vd->Window);
}

static void ImGui_ImplGlfw_InitPlatformInterface()
{
	// Register platform interface (will be coupled with a renderer interface)
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
	platform_io.Platform_CreateWindow = ImGui_ImplGlfw_CreateWindow;
	platform_io.Platform_DestroyWindow = ImGui_ImplGlfw_DestroyWindow;
	platform_io.Platform_ShowWindow = ImGui_ImplGlfw_ShowWindow;
	platform_io.Platform_SetWindowPos = ImGui_ImplGlfw_SetWindowPos;
	platform_io.Platform_GetWindowPos = ImGui_ImplGlfw_GetWindowPos;
	platform_io.Platform_SetWindowSize = ImGui_ImplGlfw_SetWindowSize;
	platform_io.Platform_GetWindowSize = ImGui_ImplGlfw_GetWindowSize;
	platform_io.Platform_SetWindowFocus = ImGui_ImplGlfw_SetWindowFocus;
	platform_io.Platform_GetWindowFocus = ImGui_ImplGlfw_GetWindowFocus;
	platform_io.Platform_GetWindowMinimized = ImGui_ImplGlfw_GetWindowMinimized;
	platform_io.Platform_SetWindowTitle = ImGui_ImplGlfw_SetWindowTitle;
	platform_io.Platform_RenderWindow = ImGui_ImplGlfw_RenderWindow;
	platform_io.Platform_SwapBuffers = ImGui_ImplGlfw_SwapBuffers;
#if GLFW_HAS_WINDOW_ALPHA
	platform_io.Platform_SetWindowAlpha = ImGui_ImplGlfw_SetWindowAlpha;
#endif

	// Register main window handle (which is owned by the main application, not by us)
	// This is mostly for simplicity and consistency, so that our code (e.g. mouse handling etc.) can use same logic for main and secondary viewports.
	ImGuiViewport* main_viewport = ImGui::GetMainViewport();
	ImGui_ImplGlfw_ViewportData* vd = IM_NEW(ImGui_ImplGlfw_ViewportData)();
	vd->Window = bd->Window;
	vd->WindowOwned = false;
	main_viewport->PlatformUserData = vd;
	main_viewport->PlatformHandle = (void*)bd->Window;
}

static void ImGui_ImplGlfw_ShutdownPlatformInterface()
{
	ImGui::DestroyPlatformWindows();
}

//-----------------------------------------------------------------------------

// WndProc hook (declared here because we will need access to ImGui_ImplGlfw_ViewportData)
#if defined(DEATH_TARGET_WINDOWS)
static ImGuiMouseSource GetMouseSourceFromMessageExtraInfo()
{
	LPARAM extra_info = ::GetMessageExtraInfo();
	if ((extra_info & 0xFFFFFF80) == 0xFF515700)
		return ImGuiMouseSource_Pen;
	if ((extra_info & 0xFFFFFF80) == 0xFF515780)
		return ImGuiMouseSource_TouchScreen;
	return ImGuiMouseSource_Mouse;
}
static LRESULT CALLBACK ImGui_ImplGlfw_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
	WNDPROC prev_wndproc = bd->PrevWndProc;
	ImGuiViewport* viewport = (ImGuiViewport*)::GetPropA(hWnd, "IMGUI_VIEWPORT");
	if (viewport != NULL)
		if (ImGui_ImplGlfw_ViewportData* vd = (ImGui_ImplGlfw_ViewportData*)viewport->PlatformUserData)
			prev_wndproc = vd->PrevWndProc;

	switch (msg) {
		// GLFW doesn't allow to distinguish Mouse vs TouchScreen vs Pen.
		// Add support for Win32 (based on imgui_impl_win32), because we rely on _TouchScreen info to trickle inputs differently.
		case WM_MOUSEMOVE: case WM_NCMOUSEMOVE:
		case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
		case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
		case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
		case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
			ImGui::GetIO().AddMouseSourceEvent(GetMouseSourceFromMessageExtraInfo());
			break;

			// We have submitted https://github.com/glfw/glfw/pull/1568 to allow GLFW to support "transparent inputs".
			// In the meanwhile we implement custom per-platform workarounds here (FIXME-VIEWPORT: Implement same work-around for Linux/OSX!)
#if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED
		case WM_NCHITTEST:
		{
			// Let mouse pass-through the window. This will allow the backend to call io.AddMouseViewportEvent() properly (which is OPTIONAL).
			// The ImGuiViewportFlags_NoInputs flag is set while dragging a viewport, as want to detect the window behind the one we are dragging.
			// If you cannot easily access those viewport flags from your windowing/event code: you may manually synchronize its state e.g. in
			// your main loop after calling UpdatePlatformWindows(). Iterate all viewports/platform windows and pass the flag to your windowing system.
			if (viewport && (viewport->Flags & ImGuiViewportFlags_NoInputs))
				return HTTRANSPARENT;
			break;
		}
#endif
	}
	return ::CallWindowProcW(prev_wndproc, hWnd, msg, wParam, lParam);
}
#endif // #ifdef _WIN32

#endif
// TODO: End of section that needs to be moved to ImGuiGlfwInput


#if !defined(WITH_OPENGLES)
#	define IMGUI_IMPL_OPENGL_HAS_EXTENSIONS        // has glGetIntegerv(GL_NUM_EXTENSIONS)
#	define IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE // may have glPolygonMode()
#endif

// Desktop GL 2.1+ and GL ES 3.0+ have glBindBuffer() with GL_PIXEL_UNPACK_BUFFER target.
//#if !defined(IMGUI_IMPL_OPENGL_ES2)
#define IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_BUFFER_PIXEL_UNPACK
//#endif

// Desktop GL 3.1+ has GL_PRIMITIVE_RESTART state
#if !defined(WITH_OPENGLES) && defined(GL_VERSION_3_1)
#	define IMGUI_IMPL_OPENGL_MAY_HAVE_PRIMITIVE_RESTART
#endif

// Desktop GL 3.2+ has glDrawElementsBaseVertex() which GL ES and WebGL don't have.
#if !defined(WITH_OPENGLES) && defined(GL_VERSION_3_2)
#	define IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
#endif

// Desktop GL 3.3+ and GL ES 3.0+ have glBindSampler()
#if defined(WITH_OPENGLES) || defined(GL_VERSION_3_3)
#	define IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
#endif


//#define GL_CALL(_CALL) _CALL
#define GL_CALL(_CALL) do { _CALL; GLenum gl_err = glGetError(); if (gl_err != 0) LOGE("GL error 0x%x returned from '%s'.\n", gl_err, #_CALL); } while (0)  // Call with error check

static nCine::ImGuiDrawing* ImGui_ImplOpenGL3_GetBackendData()
{
	return (ImGui::GetCurrentContext() ? (nCine::ImGuiDrawing*)ImGui::GetIO().BackendRendererUserData : nullptr);
}

bool ImGui_ImplOpenGL3_CreateDeviceObjects()
{
	auto* bd = ImGui_ImplOpenGL3_GetBackendData();
	ImGuiIO& io = ImGui::GetIO();

	// Desktop or GLES 3
	const char* gl_version_str = (const char*)glGetString(GL_VERSION);
	GLint major = 0;
	GLint minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	if (major == 0 && minor == 0)
		sscanf(gl_version_str, "%d.%d", &major, &minor); // Query GL_VERSION in desktop GL 2.x, the string will start with "<major>.<minor>"
	bd->GlVersion = (GLuint)(major * 100 + minor * 10);
#if defined(GL_CONTEXT_PROFILE_MASK)
	if (bd->GlVersion >= 320)
		glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &bd->GlProfileMask);
	bd->GlProfileIsCompat = (bd->GlProfileMask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
#endif

#if defined(WITH_OPENGLES)
	bd->GlProfileIsES3 = true;
#else
	if (strncmp(gl_version_str, "OpenGL ES 3", 11) == 0)
		bd->GlProfileIsES3 = true;
#endif

	bd->UseBufferSubData = false;

	// TODO: Already in ImGuiDrawing constructor
/*#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
	if (bd->GlVersion >= 320)
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
#endif*/
	//io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;  // We can create multi-viewports on the Renderer side (optional)

	// Detect extensions we support
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE
	bd->HasPolygonMode = (!bd->GlProfileIsES2 && !bd->GlProfileIsES3);
#endif
	bd->HasClipOrigin = (bd->GlVersion >= 450);
#ifdef IMGUI_IMPL_OPENGL_HAS_EXTENSIONS
	GLint num_extensions = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
	for (GLint i = 0; i < num_extensions; i++) {
		const char* extension = (const char*)glGetStringi(GL_EXTENSIONS, i);
		if (extension != nullptr && strcmp(extension, "GL_ARB_clip_control") == 0)
			bd->HasClipOrigin = true;
	}
#endif



	// Backup GL state
	GLint last_texture, last_array_buffer;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_BUFFER_PIXEL_UNPACK
	GLint last_pixel_unpack_buffer = 0;
	if (bd->GlVersion >= 210) {
		glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &last_pixel_unpack_buffer); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	}
#endif

	GLint last_vertex_array;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

	// TODO: Use nCine shaders directly instead
	auto* shader = bd->imguiShaderProgram_.get();
	auto shaderHandle = shader->glHandle();

	bd->AttribLocationTex = glGetUniformLocation(shaderHandle, "uTexture");
	bd->AttribLocationProjMtx = glGetUniformLocation(shaderHandle, "uGuiProjection");
	bd->AttribLocationVtxPos = (GLuint)glGetAttribLocation(shaderHandle, "aPosition");
	bd->AttribLocationVtxUV = (GLuint)glGetAttribLocation(shaderHandle, "aTexCoords");
	bd->AttribLocationVtxColor = (GLuint)glGetAttribLocation(shaderHandle, "aColor");

	// Create buffers
	glGenBuffers(1, &bd->VboHandle);
	glGenBuffers(1, &bd->ElementsHandle);

	// Restore modified GL state
	glBindTexture(GL_TEXTURE_2D, last_texture);
	glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_BUFFER_PIXEL_UNPACK
	if (bd->GlVersion >= 210) {
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, last_pixel_unpack_buffer);
	}
#endif

	glBindVertexArray(last_vertex_array);

	return true;
}

void ImGui_ImplOpenGL3_DestroyDeviceObjects()
{
	auto* bd = ImGui_ImplOpenGL3_GetBackendData();
	if (bd->VboHandle) {
		glDeleteBuffers(1, &bd->VboHandle);
		bd->VboHandle = 0;
	}
	if (bd->ElementsHandle) {
		glDeleteBuffers(1, &bd->ElementsHandle);
		bd->ElementsHandle = 0;
	}
}

static void ImGui_ImplOpenGL3_SetupRenderState(ImDrawData* draw_data, int fb_width, int fb_height, GLuint vertex_array_object)
{
	auto* bd = ImGui_ImplOpenGL3_GetBackendData();

	// Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glEnable(GL_SCISSOR_TEST);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_PRIMITIVE_RESTART
	if (bd->GlVersion >= 310)
		glDisable(GL_PRIMITIVE_RESTART);
#endif
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE
	if (bd->HasPolygonMode)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

	// Support for GL 4.5 rarely used glClipControl(GL_UPPER_LEFT)
#if defined(GL_CLIP_ORIGIN)
	bool clip_origin_lower_left = true;
	if (bd->HasClipOrigin) {
		GLenum current_clip_origin = 0; glGetIntegerv(GL_CLIP_ORIGIN, (GLint*)&current_clip_origin);
		if (current_clip_origin == GL_UPPER_LEFT)
			clip_origin_lower_left = false;
	}
#endif

	// Setup viewport, orthographic projection matrix
	// Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
	GL_CALL(glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height));
	float L = draw_data->DisplayPos.x;
	float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
	float T = draw_data->DisplayPos.y;
	float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
#if defined(GL_CLIP_ORIGIN)
	if (!clip_origin_lower_left) {
		float tmp = T; T = B; B = tmp;
	} // Swap top and bottom if origin is upper left
#endif
	const float ortho_projection[4][4] =
	{
		{ 2.0f / (R - L),   0.0f,         0.0f,   0.0f },
		{ 0.0f,         2.0f / (T - B),   0.0f,   0.0f },
		{ 0.0f,         0.0f,        -1.0f,   0.0f },
		{ (R + L) / (L - R),  (T + B) / (B - T),  0.0f,   1.0f },
	};

	// TODO: Use nCine shaders directly instead
	auto* shader = bd->imguiShaderProgram_.get();
	auto shaderHandle = shader->glHandle();

	glUseProgram(shaderHandle);
	glUniform1i(bd->AttribLocationTex, 0);
	glUniformMatrix4fv(bd->AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);

#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
	if (bd->GlVersion >= 330 || bd->GlProfileIsES3)
		glBindSampler(0, 0); // We use combined texture/sampler state. Applications using GL 3.3 and GL ES 3.0 may set that otherwise.
#endif

	glBindVertexArray(vertex_array_object);

	// Bind vertex/index buffers and setup attributes for ImDrawVert
	GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, bd->VboHandle));
	GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bd->ElementsHandle));
	GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxPos));
	GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxUV));
	GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxColor));
	GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxPos, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, pos)));
	GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, uv)));
	GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)offsetof(ImDrawVert, col)));
}

void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data)
{
	// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
	int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
	int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
	if (fb_width <= 0 || fb_height <= 0)
		return;

	auto* bd = ImGui_ImplOpenGL3_GetBackendData();

	// TODO: It's probably not needed to backup & restore the context of other windows
	// Backup GL state
	/*GLenum last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);
	glActiveTexture(GL_TEXTURE0);
	GLuint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*)&last_program);
	GLuint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint*)&last_texture);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
	GLuint last_sampler; if (bd->GlVersion >= 330 || bd->GlProfileIsES3) {
		glGetIntegerv(GL_SAMPLER_BINDING, (GLint*)&last_sampler);
	} else {
		last_sampler = 0;
	}
#endif
	GLuint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, (GLint*)&last_array_buffer);
	GLuint last_vertex_array_object; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint*)&last_vertex_array_object);

#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE
	GLint last_polygon_mode[2]; if (bd->HasPolygonMode) {
		glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode);
	}
#endif
	GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
	GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
	GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
	GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
	GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
	GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
	GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
	GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
	GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
	GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
	GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
	GLboolean last_enable_stencil_test = glIsEnabled(GL_STENCIL_TEST);
	GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_PRIMITIVE_RESTART
	GLboolean last_enable_primitive_restart = (bd->GlVersion >= 310) ? glIsEnabled(GL_PRIMITIVE_RESTART) : GL_FALSE;
#endif
	*/

	// Setup desired GL state
	// Recreate the VAO every time (this is to easily allow multiple GL contexts to be rendered to. VAO are not shared among GL contexts)
	// The renderer would actually work without any VAO bound, but then our VertexAttrib calls would overwrite the default one currently bound.
	GLuint vertex_array_object = 0;
	GL_CALL(glGenVertexArrays(1, &vertex_array_object));
	ImGui_ImplOpenGL3_SetupRenderState(draw_data, fb_width, fb_height, vertex_array_object);

	// Will project scissor/clipping rectangles into framebuffer space
	ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
	ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

	// Render command lists
	for (int n = 0; n < draw_data->CmdListsCount; n++) {
		const ImDrawList* cmd_list = draw_data->CmdLists[n];

		// Upload vertex/index buffers
		// - OpenGL drivers are in a very sorry state nowadays....
		//   During 2021 we attempted to switch from glBufferData() to orphaning+glBufferSubData() following reports
		//   of leaks on Intel GPU when using multi-viewports on Windows.
		// - After this we kept hearing of various display corruptions issues. We started disabling on non-Intel GPU, but issues still got reported on Intel.
		// - We are now back to using exclusively glBufferData(). So bd->UseBufferSubData IS ALWAYS FALSE in this code.
		//   We are keeping the old code path for a while in case people finding new issues may want to test the bd->UseBufferSubData path.
		// - See https://github.com/ocornut/imgui/issues/4468 and please report any corruption issues.
		const GLsizeiptr vtx_buffer_size = (GLsizeiptr)cmd_list->VtxBuffer.Size * (int)sizeof(ImDrawVert);
		const GLsizeiptr idx_buffer_size = (GLsizeiptr)cmd_list->IdxBuffer.Size * (int)sizeof(ImDrawIdx);
		if (bd->UseBufferSubData) {
			if (bd->VertexBufferSize < vtx_buffer_size) {
				bd->VertexBufferSize = vtx_buffer_size;
				GL_CALL(glBufferData(GL_ARRAY_BUFFER, bd->VertexBufferSize, nullptr, GL_STREAM_DRAW));
			}
			if (bd->IndexBufferSize < idx_buffer_size) {
				bd->IndexBufferSize = idx_buffer_size;
				GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, bd->IndexBufferSize, nullptr, GL_STREAM_DRAW));
			}
			GL_CALL(glBufferSubData(GL_ARRAY_BUFFER, 0, vtx_buffer_size, (const GLvoid*)cmd_list->VtxBuffer.Data));
			GL_CALL(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_buffer_size, (const GLvoid*)cmd_list->IdxBuffer.Data));
		} else {
			GL_CALL(glBufferData(GL_ARRAY_BUFFER, vtx_buffer_size, (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW));
			GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size, (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW));
		}

		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback != nullptr) {
				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
				if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
					ImGui_ImplOpenGL3_SetupRenderState(draw_data, fb_width, fb_height, vertex_array_object);
				else
					pcmd->UserCallback(cmd_list, pcmd);
			} else {
				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
				ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
				if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
					continue;

				// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
				GL_CALL(glScissor((int)clip_min.x, (int)((float)fb_height - clip_max.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y)));

				// Bind texture, Draw
				//GL_CALL(glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID()));
				auto* tex = reinterpret_cast<nCine::GLTexture*>(pcmd->GetTexID());
				GL_CALL(glBindTexture(GL_TEXTURE_2D, tex->glHandle()));

#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_VTX_OFFSET
				if (bd->GlVersion >= 320)
					GL_CALL(glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)), (GLint)pcmd->VtxOffset));
				else
#endif
					GL_CALL(glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx))));
			}
		}
	}

	// Destroy the temporary VAO
	GL_CALL(glDeleteVertexArrays(1, &vertex_array_object));

	// TODO: It's probably not needed to backup & restore the context of other windows
	// Restore modified GL state
	// This "glIsProgram()" check is required because if the program is "pending deletion" at the time of binding backup, it will have been deleted by now and will cause an OpenGL error. See #6220.
	/*if (last_program == 0 || glIsProgram(last_program)) glUseProgram(last_program);
	glBindTexture(GL_TEXTURE_2D, last_texture);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_BIND_SAMPLER
	if (bd->GlVersion >= 330 || bd->GlProfileIsES3)
		glBindSampler(0, last_sampler);
#endif
	glActiveTexture(last_active_texture);
	glBindVertexArray(last_vertex_array_object);
	glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
	glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
	glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
	if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (last_enable_stencil_test) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
	if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_PRIMITIVE_RESTART
	if (bd->GlVersion >= 310) {
		if (last_enable_primitive_restart) glEnable(GL_PRIMITIVE_RESTART); else glDisable(GL_PRIMITIVE_RESTART);
	}
#endif

#ifdef IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE
	// Desktop OpenGL 3.0 and OpenGL 3.1 had separate polygon draw modes for front-facing and back-facing faces of polygons
	if (bd->HasPolygonMode) {
		if (bd->GlVersion <= 310 || bd->GlProfileIsCompat) {
			glPolygonMode(GL_FRONT, (GLenum)last_polygon_mode[0]); glPolygonMode(GL_BACK, (GLenum)last_polygon_mode[1]);
		} else {
			glPolygonMode(GL_FRONT_AND_BACK, (GLenum)last_polygon_mode[0]);
		}
	}
#endif // IMGUI_IMPL_OPENGL_MAY_HAVE_POLYGON_MODE

	glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
	glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
	*/
}

static void ImGui_ImplOpenGL3_RenderWindow(ImGuiViewport* viewport, void*)
{
	auto* bd = ImGui_ImplOpenGL3_GetBackendData();

	// TODO: Should be called only the first time
	if (!bd->VboHandle)
		ImGui_ImplOpenGL3_CreateDeviceObjects();

	if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear)) {
		ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
		glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	ImGui_ImplOpenGL3_RenderDrawData(viewport->DrawData);
}



namespace nCine
{
	ImGuiDrawing::ImGuiDrawing(bool withSceneGraph)
		: withSceneGraph_(withSceneGraph), appInputHandler_(nullptr), lastFrameWidth_(0), lastFrameHeight_(0), lastLayerValue_(0)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

#if defined(IMGUI_HAS_DOCK)
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;		// Enable Docking
#endif
#if defined(IMGUI_HAS_VIEWPORT)
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;		// Enable Multi-Viewport / Platform Windows

		auto& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().GetGfxDevice());
		ImGui_ImplGlfw_Init(gfxDevice.windowHandle(), true);

		// TODO: Move this to ImGuiGlfwInput
		ImGui::GetPlatformIO().Renderer_RenderWindow = ImGui_ImplOpenGL3_RenderWindow;
#endif

		io.BackendRendererUserData = this;
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		io.BackendRendererName = "nCine_OpenGL_ES";
#else
		io.BackendRendererName = "nCine_OpenGL";
#endif

/*#if defined(DEATH_TARGET_ANDROID)
		static char iniFilename[512];
		nctl::strncpy(iniFilename, fs::joinPath(fs::savePath(), "imgui.ini").data(), 512);
		io.IniFilename = iniFilename;
#endif*/

#if !(defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) && !defined(DEATH_TARGET_EMSCRIPTEN)
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;	// We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
#endif

		imguiShaderProgram_ = std::make_unique<GLShaderProgram>(GLShaderProgram::QueryPhase::Immediate);
#if !defined(WITH_EMBEDDED_SHADERS)
		imguiShaderProgram_->attachShaderFromFile(GL_VERTEX_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, "imgui_vs.glsl"_s }));
		imguiShaderProgram_->attachShaderFromFile(GL_FRAGMENT_SHADER, fs::CombinePath({ theApplication().GetDataPath(), "Shaders"_s, "imgui_fs.glsl"_s }));
#else
		imguiShaderProgram_->attachShaderFromString(GL_VERTEX_SHADER, ShaderStrings::imgui_vs);
		imguiShaderProgram_->attachShaderFromString(GL_FRAGMENT_SHADER, ShaderStrings::imgui_fs);
#endif
		imguiShaderProgram_->link(GLShaderProgram::Introspection::Enabled);
		FATAL_ASSERT(imguiShaderProgram_->status() != GLShaderProgram::Status::LinkingFailed);

		if (!withSceneGraph) {
			setupBuffersAndShader();
		}

		// Set custom style
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 4.0f;
		style.FrameRounding = 2.0f;
		style.FrameBorderSize = 1.0f;

		// Try to use system font
#if defined(DEATH_TARGET_WINDOWS)
		String systemFont = fs::CombinePath({ fs::GetWindowsDirectory(), "Fonts"_s, "SegoeUI.ttf"_s });
		if (fs::FileExists(systemFont)) {
			// Include the most of european latin characters
			static const ImWchar ranges[] = { 0x0020, 0x017E, 0 };
			io.Fonts->AddFontFromFileTTF(systemFont.data(), 17.0f, nullptr, ranges);
		}
#endif

		// TODO: Add these lines to "ImGui::NavUpdateCancelRequest()"
		/*
			if (g.NavWindow && ((g.NavWindow->Flags & ImGuiWindowFlags_Popup) || !(g.NavWindow->Flags & ImGuiWindowFlags_ChildWindow))) {
				if (g.NavWindow->NavLastIds[0] != 0) {
					g.NavWindow->NavLastIds[0] = 0;
				} else {
					FocusWindow(NULL);
				}
			}
		*/
	}

	ImGuiDrawing::~ImGuiDrawing()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->SetTexID(nullptr);
		io.BackendRendererName = nullptr;
		io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;

		ImGui_ImplOpenGL3_DestroyDeviceObjects();
		// TODO: This needs to be called from ImGuiGlfwInput::shutdown()
		ImGui_ImplGlfw_Shutdown();
	}

	bool ImGuiDrawing::buildFonts()
	{
		ImGuiIO& io = ImGui::GetIO();

		unsigned char* pixels = nullptr;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		texture_ = std::make_unique<GLTexture>(GL_TEXTURE_2D);
		texture_->texParameteri(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		texture_->texParameteri(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		texture_->texImage2D(0, GL_RGBA, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		io.Fonts->TexID = reinterpret_cast<ImTextureID>(texture_.get());

		return (pixels != nullptr);
	}

	void ImGuiDrawing::newFrame()
	{
#if defined(WITH_GLFW)
		ImGuiGlfwInput::newFrame();
#elif defined(WITH_SDL)
		ImGuiSdlInput::newFrame();
#elif defined(WITH_QT5)
		ImGuiQt5Input::newFrame();
#elif defined(DEATH_TARGET_ANDROID)
		ImGuiAndroidInput::newFrame();
#endif

		// TODO: This should be merged into ImGuiGlfwInput
		ImGui_ImplGlfw_NewFrame();

		ImGuiIO& io = ImGui::GetIO();

		if (lastFrameWidth_ != io.DisplaySize.x || lastFrameHeight_ != io.DisplaySize.y) {
			// TODO: projectionMatrix_ must be recaltulated when the main window moves because of clipOff
			//projectionMatrix_ = Matrix4x4f::Ortho(0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, 1.0f);

			if (!withSceneGraph_) {
				//imguiShaderUniforms_->uniform(Material::GuiProjectionMatrixUniformName)->setFloatVector(projectionMatrix_.Data());
				imguiShaderUniforms_->uniform(Material::DepthUniformName)->setFloatValue(0.0f);
				imguiShaderUniforms_->commitUniforms();
			}
		}

		ImGui::NewFrame();
	}

	void ImGuiDrawing::endFrame(RenderQueue& renderQueue)
	{
		ImGui::EndFrame();
		ImGui::Render();

		ImGuiIO& io = ImGui::GetIO();

#if defined(IMGUI_HAS_VIEWPORT)
		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

			// Restore the OpenGL rendering context to the main window DC, since platform windows might have changed it.
			auto& gfxDevice = static_cast<GlfwGfxDevice&>(theApplication().GetGfxDevice());
			glfwMakeContextCurrent(gfxDevice.windowHandle());
		}
#endif

		if (io.WantCaptureKeyboard) {
			if (appInputHandler_ == nullptr) {
				appInputHandler_ = IInputManager::handler();
				IInputManager::setHandler(nullptr);
			}
		} else {
			if (appInputHandler_ != nullptr) {
				IInputManager::setHandler(appInputHandler_);
				appInputHandler_ = nullptr;
			}
		}

		draw(renderQueue);
	}

	void ImGuiDrawing::endFrame()
	{
		ImGui::EndFrame();
		ImGui::Render();

		draw();
	}

	RenderCommand* ImGuiDrawing::retrieveCommandFromPool()
	{
		bool commandAdded = false;
		RenderCommand* retrievedCommand = RenderResources::renderCommandPool().retrieveOrAdd(imguiShaderProgram_.get(), commandAdded);
		if (commandAdded) {
			setupRenderCmd(*retrievedCommand);
		}
		return retrievedCommand;
	}

	void ImGuiDrawing::setupRenderCmd(RenderCommand& cmd)
	{
		cmd.setType(RenderCommand::CommandTypes::ImGui);

		Material& material = cmd.material();
		material.setShaderProgram(imguiShaderProgram_.get());
		material.reserveUniformsDataMemory();
		material.uniform(Material::TextureUniformName)->setIntValue(0); // GL_TEXTURE0
		imguiShaderProgram_->attribute(Material::PositionAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
		imguiShaderProgram_->attribute(Material::TexCoordsAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setType(GL_UNSIGNED_BYTE);
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setNormalized(true);
		material.setBlendingEnabled(true);
		material.setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		cmd.geometry().setNumElementsPerVertex(sizeof(ImDrawVert) / sizeof(GLfloat));
		cmd.geometry().setDrawParameters(GL_TRIANGLES, 0, 0);
	}

	void ImGuiDrawing::draw(RenderQueue& renderQueue)
	{
		ImDrawData* drawData = ImGui::GetDrawData();

		const unsigned int numElements = sizeof(ImDrawVert) / sizeof(GLfloat);

		ImGuiIO& io = ImGui::GetIO();
		const int fbWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		const int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0) {
			return;
		}
		const ImVec2 clipOff = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;

		unsigned int numCmd = 0;
		for (int n = 0; n < drawData->CmdListsCount; n++) {
			const ImDrawList* imCmdList = drawData->CmdLists[n];

			RenderCommand& firstCmd = *retrieveCommandFromPool();

			// TODO: projectionMatrix_ must be recaltulated when the main window moves because of clipOff
			/*if (lastFrameWidth_ != static_cast<int>(io.DisplaySize.x) ||
				lastFrameHeight_ != static_cast<int>(io.DisplaySize.y)) {
				firstCmd.material().uniform(Material::GuiProjectionMatrixUniformName)->setFloatVector(projectionMatrix_.Data());
				lastFrameWidth_ = static_cast<int>(io.DisplaySize.x);
				lastFrameHeight_ = static_cast<int>(io.DisplaySize.y);
			}*/
			projectionMatrix_ = Matrix4x4f::Ortho(0.0f, io.DisplaySize.x, io.DisplaySize.y, 0.0f, -1.0f, 1.0f);
			projectionMatrix_.Translate(-clipOff.x, -clipOff.y, 0.0f);
			firstCmd.material().uniform(Material::GuiProjectionMatrixUniformName)->setFloatVector(projectionMatrix_.Data());

			firstCmd.geometry().shareVbo(nullptr);
			GLfloat* vertices = firstCmd.geometry().acquireVertexPointer(imCmdList->VtxBuffer.Size * numElements, numElements);
			memcpy(vertices, imCmdList->VtxBuffer.Data, imCmdList->VtxBuffer.Size * numElements * sizeof(GLfloat));
			firstCmd.geometry().releaseVertexPointer();

			firstCmd.geometry().shareIbo(nullptr);
			GLushort* indices = firstCmd.geometry().acquireIndexPointer(imCmdList->IdxBuffer.Size);
			memcpy(indices, imCmdList->IdxBuffer.Data, imCmdList->IdxBuffer.Size * sizeof(GLushort));
			firstCmd.geometry().releaseIndexPointer();

			if (lastLayerValue_ != theApplication().GetGuiSettings().imguiLayer) {
				// It is enough to set the uniform value once as every ImGui command share the same shader
				const float depth = RenderCommand::calculateDepth(theApplication().GetGuiSettings().imguiLayer, -1.0f, 1.0f);
				firstCmd.material().uniform(Material::DepthUniformName)->setFloatValue(depth);
				lastLayerValue_ = theApplication().GetGuiSettings().imguiLayer;
			}

			for (int cmdIdx = 0; cmdIdx < imCmdList->CmdBuffer.Size; cmdIdx++) {
				const ImDrawCmd* imCmd = &imCmdList->CmdBuffer[cmdIdx];
				RenderCommand& currCmd = (cmdIdx == 0) ? firstCmd : *retrieveCommandFromPool();

				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clipMin((imCmd->ClipRect.x - clipOff.x) * clipScale.x, (imCmd->ClipRect.y - clipOff.y) * clipScale.y);
				ImVec2 clipMax((imCmd->ClipRect.z - clipOff.x) * clipScale.x, (imCmd->ClipRect.w - clipOff.y) * clipScale.y);
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
					continue;

				// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
				currCmd.setScissor(static_cast<GLint>(clipMin.x), static_cast<GLint>(static_cast<float>(fbHeight) - clipMax.y),
								   static_cast<GLsizei>(clipMax.x - clipMin.x), static_cast<GLsizei>(clipMax.y - clipMin.y));

				if (cmdIdx > 0) {
					currCmd.geometry().shareVbo(&firstCmd.geometry());
					currCmd.geometry().shareIbo(&firstCmd.geometry());
				}

				currCmd.geometry().setNumIndices(imCmd->ElemCount);
				currCmd.geometry().setFirstIndex(imCmd->IdxOffset);
				currCmd.geometry().setFirstVertex(imCmd->VtxOffset);
				currCmd.setLayer(theApplication().GetGuiSettings().imguiLayer);
				currCmd.setVisitOrder(numCmd);
				currCmd.material().setTexture(reinterpret_cast<GLTexture*>(imCmd->GetTexID()));

				renderQueue.addCommand(&currCmd);
				numCmd++;
			}
		}
	}

	void ImGuiDrawing::setupBuffersAndShader()
	{
		vbo_ = std::make_unique<GLBufferObject>(GL_ARRAY_BUFFER);
		ibo_ = std::make_unique<GLBufferObject>(GL_ELEMENT_ARRAY_BUFFER);

		imguiShaderUniforms_ = std::make_unique<GLShaderUniforms>(imguiShaderProgram_.get());
		imguiShaderUniforms_->setUniformsDataPointer(uniformsBuffer_);
		imguiShaderUniforms_->uniform(Material::TextureUniformName)->setIntValue(0); // GL_TEXTURE0

		imguiShaderProgram_->attribute(Material::PositionAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
		imguiShaderProgram_->attribute(Material::TexCoordsAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setVboParameters(sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setType(GL_UNSIGNED_BYTE);
		imguiShaderProgram_->attribute(Material::ColorAttributeName)->setNormalized(true);
	}

	void ImGuiDrawing::draw()
	{
		ImDrawData* drawData = ImGui::GetDrawData();

		const int fbWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		const int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fbWidth <= 0 || fbHeight <= 0) {
			return;
		}

		const ImVec2 clipOff = drawData->DisplayPos;
		const ImVec2 clipScale = drawData->FramebufferScale;

		GLBlending::State blendingState = GLBlending::state();
		GLBlending::enable();
		GLBlending::setBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GLCullFace::State cullFaceState = GLCullFace::state();
		GLCullFace::disable();
		GLDepthTest::State depthTestState = GLDepthTest::state();
		GLDepthTest::disable();

		for (int n = 0; n < drawData->CmdListsCount; n++) {
			const ImDrawList* imCmdList = drawData->CmdLists[n];
			const ImDrawIdx* firstIndex = nullptr;

			// Always define vertex format (and bind VAO) before uploading data to buffers
			imguiShaderProgram_->defineVertexFormat(vbo_.get(), ibo_.get());
			vbo_->bufferData(static_cast<GLsizeiptr>(imCmdList->VtxBuffer.Size) * sizeof(ImDrawVert), static_cast<const GLvoid*>(imCmdList->VtxBuffer.Data), GL_STREAM_DRAW);
			ibo_->bufferData(static_cast<GLsizeiptr>(imCmdList->IdxBuffer.Size) * sizeof(ImDrawIdx), static_cast<const GLvoid*>(imCmdList->IdxBuffer.Data), GL_STREAM_DRAW);
			imguiShaderProgram_->use();

			for (int cmdIdx = 0; cmdIdx < imCmdList->CmdBuffer.Size; cmdIdx++) {
				const ImDrawCmd* imCmd = &imCmdList->CmdBuffer[cmdIdx];

				// Project scissor/clipping rectangles into framebuffer space
				ImVec2 clipMin((imCmd->ClipRect.x - clipOff.x) * clipScale.x, (imCmd->ClipRect.y - clipOff.y) * clipScale.y);
				ImVec2 clipMax((imCmd->ClipRect.z - clipOff.x) * clipScale.x, (imCmd->ClipRect.w - clipOff.y) * clipScale.y);
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
					continue;
				}

				// Apply scissor/clipping rectangle (Y is inverted in OpenGL)
				GLScissorTest::enable(static_cast<GLint>(clipMin.x), static_cast<GLint>(static_cast<float>(fbHeight) - clipMax.y),
									  static_cast<GLsizei>(clipMax.x - clipMin.x), static_cast<GLsizei>(clipMax.y - clipMin.y));

				// Bind texture, Draw
				GLTexture::bindHandle(GL_TEXTURE_2D, reinterpret_cast<GLTexture*>(imCmd->GetTexID())->glHandle());
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(imCmd->ElemCount), sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, firstIndex);
#else
				glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(imCmd->ElemCount), sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
										 reinterpret_cast<void*>(static_cast<intptr_t>(imCmd->IdxOffset * sizeof(ImDrawIdx))), static_cast<GLint>(imCmd->VtxOffset));
#endif
				firstIndex += imCmd->ElemCount;
			}
		}

		GLScissorTest::disable();
		GLDepthTest::setState(depthTestState);
		GLCullFace::setState(cullFaceState);
		GLBlending::setState(blendingState);
	}
}

#endif