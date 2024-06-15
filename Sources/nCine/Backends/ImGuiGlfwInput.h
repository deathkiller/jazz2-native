#pragma once

#if defined(WITH_IMGUI) && defined(WITH_GLFW)

#include "GlfwGfxDevice.h"

#include <imgui.h>

namespace nCine
{
	/// The class that handles GLFW input for ImGui
	class ImGuiGlfwInput
	{
	public:
		static void init(GLFWwindow* window, bool withCallbacks);
		static void shutdown();
		static void newFrame();
		static void endFrame();

		static inline void setInputEnabled(bool inputEnabled) {
			inputEnabled_ = inputEnabled;
		}

	private:
		static bool inputEnabled_;

		static GLFWwindow* window_;
		static GLFWwindow* mouseWindow_;
		static double time_;
		static GLFWcursor* mouseCursors_[ImGuiMouseCursor_COUNT];
		static ImVec2 lastValidMousePos_;
		static bool installedCallbacks_;
		static bool wantUpdateMonitors_;
		static GLFWwindow* keyOwnerWindows_[GLFW_KEY_LAST];

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
		static void updateMonitors();

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