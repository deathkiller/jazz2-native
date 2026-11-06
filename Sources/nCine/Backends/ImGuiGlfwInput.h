#pragma once

#if defined(WITH_IMGUI) && defined(WITH_GLFW)

#include "GlfwGfxDevice.h"

#include <imgui.h>

namespace nCine::Backends
{
	/**
		@brief Handles GLFW input for ImGui
		
		Bridges GLFW window, keyboard, mouse and gamepad events into the ImGui input state,
		optionally installing GLFW callbacks and supporting multi-viewport platform windows.
	*/
	class ImGuiGlfwInput
	{
	public:
		static void Init(GLFWwindow* window, bool withCallbacks);
		static void Shutdown();
		static void NewFrame();
		static void EndFrame();

		static inline void SetInputEnabled(bool inputEnabled) {
			_inputEnabled = inputEnabled;
		}

	private:
		static bool _inputEnabled;

		static GLFWwindow* _window;
		static GLFWwindow* _mouseWindow;
		static double _time;
		static GLFWcursor* _mouseCursors[ImGuiMouseCursor_COUNT];
		static ImVec2 _lastValidMousePos;
		static bool _installedCallbacks;
		static bool _wantUpdateMonitors;
		static GLFWwindow* _keyOwnerWindows[GLFW_KEY_LAST];

		static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
		static void KeyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods);
		static void WindowFocusCallback(GLFWwindow* window, int focused);
		static void CursorPosCallback(GLFWwindow* window, double x, double y);
		static void CursorEnterCallback(GLFWwindow* window, int entered);
		static void CharCallback(GLFWwindow* window, unsigned int c);
		static void MonitorCallback(GLFWmonitor* monitor, int event);
#if defined(IMGUI_HAS_VIEWPORT)
		static void WindowCloseCallback(GLFWwindow* window);
		static void WindowPosCallback(GLFWwindow* window, int, int);
		static void WindowSizeCallback(GLFWwindow* window, int, int);
#endif

		static void InstallCallbacks(GLFWwindow* window);
		static void RestoreCallbacks(GLFWwindow* window);

		static void UpdateMouseData();
		static void UpdateMouseCursor();
		static void UpdateGamepads();

		static float GetContentScaleForWindow(GLFWwindow* window);
		static float GetContentScaleForMonitor(GLFWmonitor* monitor);
		static void GetWindowSizeAndFramebufferScale(GLFWwindow* window, ImVec2* outSize, ImVec2* outFramebufferScale);

#if defined(IMGUI_HAS_DOCK)
		static void UpdateMonitors();
#endif

#if defined(IMGUI_HAS_VIEWPORT)
		static ImGuiViewport* GetParentViewport(ImGuiViewport* viewport);
		static void AddParentToView(ImGuiViewport* viewport, ImGuiViewport* parentViewport);

		static void OnCreateWindow(ImGuiViewport* viewport);
		static void OnDestroyWindow(ImGuiViewport* viewport);
		static void OnShowWindow(ImGuiViewport* viewport);
		static ImVec2 OnGetWindowPos(ImGuiViewport* viewport);
		static void OnSetWindowPos(ImGuiViewport* viewport, ImVec2 pos);
		static ImVec2 OnGetWindowSize(ImGuiViewport* viewport);
		static void OnSetWindowSize(ImGuiViewport* viewport, ImVec2 size);
		static ImVec2 OnGetWindowFramebufferScale(ImGuiViewport* viewport);
		static void OnSetWindowTitle(ImGuiViewport* viewport, const char* title);
		static bool OnGetWindowFocus(ImGuiViewport* viewport);
		static void OnSetWindowFocus(ImGuiViewport* viewport);
		static bool OnGetWindowMinimized(ImGuiViewport* viewport);
		static void OnSetWindowAlpha(ImGuiViewport* viewport, float alpha);
		static void OnRenderWindow(ImGuiViewport* viewport, void*);
		static void OnSwapBuffers(ImGuiViewport* viewport, void*);
#endif
	};
}

#endif