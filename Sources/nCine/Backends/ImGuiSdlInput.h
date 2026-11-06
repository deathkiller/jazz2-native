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
		static void Init(SDL_Window* window, SDL_GLContext glContextHandle);
		/** @brief Shuts down the ImGui SDL2 input backend */
		static void Shutdown();
		/** @brief Begins a new ImGui input frame */
		static void NewFrame();
		/** @brief Ends the current ImGui input frame */
		static void EndFrame();
		/**
		 * @brief Processes a single SDL2 event for ImGui
		 *
		 * @param event		SDL2 event to process
		 * @return `true` if the event was consumed by ImGui
		 */
		static bool ProcessEvent(const SDL_Event* event);

		/** @brief Enables or disables input processing */
		static inline void SetInputEnabled(bool inputEnabled) {
			_inputEnabled = inputEnabled;
		}

#if defined(IMGUI_HAS_VIEWPORT)
		/**
		 * @brief Returns the SDL window backing a multi-viewport platform window, as an opaque pointer
		 *
		 * A renderer that has to create a presentation object per platform window (the Vulkan backend needs a
		 * `VkSurfaceKHR`) gets the window from here; `ImGuiViewport::PlatformHandle` only carries its SDL id.
		 */
		static void* GetPlatformWindowHandle(ImGuiViewport* viewport);
#endif

	private:
		enum class GamepadMode
		{
			AutoFirst,
			AutoAll,
			Manual
		};

		static bool _inputEnabled;

		static SDL_Window* _window;
		static unsigned long int _time;
		static char* _clipboardTextData;
		static bool _wantUpdateMonitors;

		// Mouse handling
		static unsigned int _mouseWindowID;
		static int _mouseButtonsDown;
		static SDL_Cursor* _mouseCursors[ImGuiMouseCursor_COUNT];
		static SDL_Cursor* _mouseLastCursor;
		static unsigned int _mouseLastLeaveFrame;
		static bool _mouseCanUseGlobalState;

		// Gamepad handling
		static ImVector<SDL_GameController*> _gamepads;
		static GamepadMode _gamepadMode;
		static bool _wantUpdateGamepadsList;

		static const char* GetClipboardText(ImGuiContext* ctx);
		static void UpdateMouseData();
		static void UpdateMouseCursor();
		static float GetContentScaleForWindow(SDL_Window* window);
		static float GetContentScaleForDisplay(int displayIndex);
		static void CloseGamepads();
		static void SetGamepadMode(GamepadMode mode, SDL_GameController** manualGamepadsArray, unsigned int manualGamepadsCount);
		static void UpdateGamepads();
		static void GetWindowSizeAndFramebufferScale(SDL_Window* window, SDL_Renderer* renderer, ImVec2* outSize, ImVec2* outFramebufferScale);

#if defined(IMGUI_HAS_DOCK)
		static void UpdateMonitors();
#endif

#if defined(IMGUI_HAS_VIEWPORT)
		static void OnCreateWindow(ImGuiViewport* viewport);
		static void OnDestroyWindow(ImGuiViewport* viewport);
		static void OnShowWindow(ImGuiViewport* viewport);
		static ImVec2 OnGetWindowPos(ImGuiViewport* viewport);
		static void OnSetWindowPos(ImGuiViewport* viewport, ImVec2 pos);
		static ImVec2 OnGetWindowSize(ImGuiViewport* viewport);
		static ImVec2 OnGetWindowFramebufferScale(ImGuiViewport* viewport);
		static void OnSetWindowSize(ImGuiViewport* viewport, ImVec2 size);
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