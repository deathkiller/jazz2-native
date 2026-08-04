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
		static void init(GLFWwindow* window, bool withCallbacks);
		static void shutdown();
		static void newFrame();
		static void endFrame();

		static inline void setInputEnabled(bool inputEnabled) {
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

		static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
		static void keyCallback(GLFWwindow* window, int keycode, int scancode, int action, int mods);
		static void windowFocusCallback(GLFWwindow* window, int focused);
		static void cursorPosCallback(GLFWwindow* window, double x, double y);
		static void cursorEnterCallback(GLFWwindow* window, int entered);
		static void charCallback(GLFWwindow* window, unsigned int c);
		static void monitorCallback(GLFWmonitor* monitor, int event);
#if defined(IMGUI_HAS_VIEWPORT)
		static void windowCloseCallback(GLFWwindow* window);
		static void windowPosCallback(GLFWwindow* window, int, int);
		static void windowSizeCallback(GLFWwindow* window, int, int);
#endif

		static void installCallbacks(GLFWwindow* window);
		static void restoreCallbacks(GLFWwindow* window);

		static void updateMouseData();
		static void updateMouseCursor();
		static void updateGamepads();

		static float getContentScaleForWindow(GLFWwindow* window);
		static float getContentScaleForMonitor(GLFWmonitor* monitor);
		static void getWindowSizeAndFramebufferScale(GLFWwindow* window, ImVec2* outSize, ImVec2* outFramebufferScale);

#if defined(IMGUI_HAS_DOCK)
		static void updateMonitors();
#endif

#if defined(IMGUI_HAS_VIEWPORT)
		static ImGuiViewport* getParentViewport(ImGuiViewport* viewport);
		static void addParentToView(ImGuiViewport* viewport, ImGuiViewport* parentViewport);

		static void onCreateWindow(ImGuiViewport* viewport);
		static void onDestroyWindow(ImGuiViewport* viewport);
		static void onShowWindow(ImGuiViewport* viewport);
		static ImVec2 onGetWindowPos(ImGuiViewport* viewport);
		static void onSetWindowPos(ImGuiViewport* viewport, ImVec2 pos);
		static ImVec2 onGetWindowSize(ImGuiViewport* viewport);
		static void onSetWindowSize(ImGuiViewport* viewport, ImVec2 size);
		static ImVec2 onGetWindowFramebufferScale(ImGuiViewport* viewport);
		static void onSetWindowTitle(ImGuiViewport* viewport, const char* title);
		static bool onGetWindowFocus(ImGuiViewport* viewport);
		static void onSetWindowFocus(ImGuiViewport* viewport);
		static bool onGetWindowMinimized(ImGuiViewport* viewport);
		static void onSetWindowAlpha(ImGuiViewport* viewport, float alpha);
		static void onRenderWindow(ImGuiViewport* viewport, void*);
		static void onSwapBuffers(ImGuiViewport* viewport, void*);
#endif
	};
}

#endif