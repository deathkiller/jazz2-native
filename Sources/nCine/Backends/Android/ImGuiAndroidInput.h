#pragma once

#if defined(WITH_IMGUI)

struct AInputEvent;
struct ANativeWindow;

namespace nCine::Backends
{
	/** @brief Handles Android input for ImGui */
	class ImGuiAndroidInput
	{
	public:
		static void Init(ANativeWindow* window);
		static void Shutdown();
		static void NewFrame();
		static bool ProcessEvent(const AInputEvent* event);

		static inline void SetInputEnabled(bool inputEnabled) {
			_inputEnabled = inputEnabled;
		}

	private:
		static ANativeWindow* _window;
		static bool _inputEnabled;
	};
}

#endif