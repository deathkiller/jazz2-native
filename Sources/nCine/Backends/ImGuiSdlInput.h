#pragma once

#if defined(WITH_IMGUI) && (defined(WITH_SDL2) || defined(WITH_SDL3))

#include <imgui.h>

struct SDL_Renderer;
struct SDL_Window;
union SDL_Event;
struct SDL_Cursor;
#if defined(WITH_SDL3)
// SDL3 renamed the game-controller subsystem to "gamepad"; alias the SDL2 spelling to the SDL3 struct so the
// shared class/implementation keeps using the SDL_GameController name (a pointer to the same opaque type)
typedef struct SDL_Gamepad SDL_GameController;
#else
typedef struct _SDL_GameController SDL_GameController;
#endif
typedef void* SDL_GLContext;

namespace nCine::Backends
{
	/**
		@brief Handles SDL2 input for ImGui
		
		Bridges SDL2 window and input events into the ImGui backend.
	*/
	class ImGuiSdlInput
	{
	public:
		/**
		 * @brief Initializes the ImGui SDL2 input backend
		 *
		 * @param window			SDL2 window to receive input from
		 * @param glContextHandle	OpenGL context associated with the window
		 */
		static void init(SDL_Window* window, SDL_GLContext glContextHandle);
		/** @brief Shuts down the ImGui SDL2 input backend */
		static void shutdown();
		/** @brief Begins a new ImGui input frame */
		static void newFrame();
		/** @brief Ends the current ImGui input frame */
		static void endFrame();
		/**
		 * @brief Processes a single SDL2 event for ImGui
		 *
		 * @param event		SDL2 event to process
		 * @return `true` if the event was consumed by ImGui
		 */
		static bool processEvent(const SDL_Event* event);

		/** @brief Enables or disables input processing */
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