#pragma once

#if defined(WITH_IMGUI) && defined(WITH_SDL)

#include <imgui.h>

struct SDL_Renderer;
struct SDL_Window;
union SDL_Event;
struct SDL_Cursor;
typedef struct _SDL_GameController SDL_GameController;
typedef void* SDL_GLContext;

namespace nCine::Backends
{
	/// The class that handles SDL2 input for ImGui
	class ImGuiSdlInput
	{
	public:
		static void init(SDL_Window* window, SDL_GLContext glContextHandle);
		static void shutdown();
		static void newFrame();
		static void endFrame();
		static bool processEvent(const SDL_Event* event);

		static inline void setInputEnabled(bool inputEnabled) {
			inputEnabled_ = inputEnabled;
		}

	private:
		enum class GamepadMode
		{
			AUTO_FIRST,
			AUTO_ALL,
			MANUAL
		};

		static bool inputEnabled_;

		static SDL_Window* window_;
		static unsigned long int time_;
		static char* clipboardTextData_;
		static bool wantUpdateMonitors_;

		// Mouse handling
		static unsigned int mouseWindowID_;
		static int mouseButtonsDown_;
		static SDL_Cursor* mouseCursors_[ImGuiMouseCursor_COUNT];
		static SDL_Cursor* mouseLastCursor_;
		static unsigned int mouseLastLeaveFrame_;
		static bool mouseCanUseGlobalState_;

		// Gamepad handling
		static ImVector<SDL_GameController*> gamepads_;
		static GamepadMode gamepadMode_;
		static bool wantUpdateGamepadsList_;

		static const char* clipboardText(ImGuiContext* ctx);
		static void updateMouseData();
		static void updateMouseCursor();
		static float getContentScaleForWindow(SDL_Window* window);
		static float getContentScaleForDisplay(int displayIndex);
		static void closeGamepads();
		static void setGamepadMode(GamepadMode mode, SDL_GameController** manualGamepadsArray, unsigned int manualGamepadsCount);
		static void updateGamepads();
		static void getWindowSizeAndFramebufferScale(SDL_Window* window, SDL_Renderer* renderer, ImVec2* outSize, ImVec2* outFramebufferScale);

#if defined(IMGUI_HAS_DOCK)
		static void updateMonitors();
#endif

#if defined(IMGUI_HAS_VIEWPORT)
		static void onCreateWindow(ImGuiViewport* viewport);
		static void onDestroyWindow(ImGuiViewport* viewport);
		static void onShowWindow(ImGuiViewport* viewport);
		static ImVec2 onGetWindowPos(ImGuiViewport* viewport);
		static void onSetWindowPos(ImGuiViewport* viewport, ImVec2 pos);
		static ImVec2 onGetWindowSize(ImGuiViewport* viewport);
		static ImVec2 onGetWindowFramebufferScale(ImGuiViewport* viewport);
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