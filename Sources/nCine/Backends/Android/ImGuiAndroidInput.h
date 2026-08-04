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
		static void init(ANativeWindow* window);
		static void shutdown();
		static void newFrame();
		static bool processEvent(const AInputEvent* event);

		static inline void setInputEnabled(bool inputEnabled) {
			_inputEnabled = inputEnabled;
		}

	private:
		static ANativeWindow* _window;
		static bool _inputEnabled;
	};
}

#endif