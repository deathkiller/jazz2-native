#pragma once

#if defined(WITH_IMGUI)

struct AInputEvent;
struct ANativeWindow;

namespace nCine
{
	/// The class that handles Android input for ImGui
	class ImGuiAndroidInput
	{
	public:
		static void init(ANativeWindow* window);
		static void shutdown();
		static void newFrame();
		static bool processEvent(const AInputEvent* event);

		static inline void setInputEnabled(bool inputEnabled) {
			inputEnabled_ = inputEnabled;
		}

	private:
		static ANativeWindow* window_;
		static bool inputEnabled_;
	};
}

#endif