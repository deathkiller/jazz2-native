#pragma once

#if defined(WITH_LIBRETRO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Graphics/IGfxDevice.h"
#include "../Graphics/DisplayMode.h"
#include "../../libretro/libretro.h"

#include <vector>

namespace nCine::Backends
{
	/**
		@brief Graphics device presenting through the libretro frontend

		The frontend owns the video output, so there is no window to create or resize. With the
		software backend the CPU framebuffer is handed over to `retro_video_refresh`, with the
		OpenGL backend the frame is drawn straight into the framebuffer object of the frontend.
	*/
	class LibretroGfxDevice : public IGfxDevice
	{
	public:
		LibretroGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode);

		/** @brief Fills the structure describing the video and audio output to the frontend */
		static void FillSystemAvInfo(retro_system_av_info& info, std::int32_t width, std::int32_t height);
		/** @brief Sets the rate the game runs at: one `retro_run` advances one frame of `1/fps` seconds */
		static void SetTargetFps(double fps);
		/** @brief Returns the rate the game runs at */
		static double GetTargetFps();
		/** @brief Re-announces the AV info to the frontend with the last advertised geometry, after a timing change */
		static void ReannounceAvInfo();
		/** @brief Initializes the OpenGL function pointers, must be called with the context current */
		static bool InitializeGraphicsLibrary();
		/** @brief Sets the frontend callback returning the framebuffer object to render into */
		static void SetCurrentFramebufferCallback(retro_hw_get_current_framebuffer_t callback);

		/** @brief Repoints the engine at the current framebuffer of the frontend and resyncs the cached state */
		void beginFrame();

		void setSwapInterval(int interval) override {}
		void setResolution(bool fullscreen, int width = 0, int height = 0) override {}
		void setWindowPosition(int x, int y) override {}
		void setWindowTitle(StringView windowTitle) override {}
		void setWindowIcon(StringView iconFilename) override {}
		void setWindowSize(int width, int height) override {}

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override {
			return currentVideoMode_;
		}

	protected:
		void setResolutionInternal(int width, int height) override {}

	private:
		std::int32_t _lastWidth;
		std::int32_t _lastHeight;
		std::vector<std::uint32_t> _converted;

		void update() override;
	};
}

#endif
