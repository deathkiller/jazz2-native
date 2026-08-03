#pragma once

#if defined(WITH_LIBRETRO) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Application.h"

#include <libretro/libretro.h>

namespace nCine::Backends
{
	/**
		@brief Application driven by a libretro frontend

		The frontend owns the main loop, the window and the input devices, so the entry points in
		`libretro.cpp` initialize the application once and then advance it by exactly one frame
		on every `retro_run()` call.
	*/
	class LibretroApplication : public Application
	{
	public:
		/** @brief Directories advertised by the frontend, filled in before the initialization */
		struct HostPaths
		{
			/** @brief Frontend's system directory */
			String System;
			/** @brief Frontend's core assets directory */
			String CoreAssets;
			/** @brief Frontend's save directory, where the application should write its own files */
			String Save;
			/** @brief Path of the content the frontend has been asked to load */
			String Content;
		};

		/** @brief Frontend callback to query and configure the environment */
		static retro_environment_t EnvironmentCallback;
		/** @brief Frontend callback to present a finished frame */
		static retro_video_refresh_t VideoRefreshCallback;
		/** @brief Frontend callback to poll the input devices */
		static retro_input_poll_t InputPollCallback;
		/** @brief Frontend callback to read the state of an input device */
		static retro_input_state_t InputStateCallback;
		/** @brief Whether the frontend is inside `retro_run()`, only then its video driver can be called */
		static bool IsInsideFrame;

		/** @brief Shows a short notice on the frontend's on-screen display */
		static void ShowMessage(const char* text);

		/** @brief Creates the graphics device and the input manager, then initializes the engine */
		bool Init(CreateAppEventHandlerDelegate createAppEventHandler);
		/** @brief Advances the engine by exactly one frame */
		void RunFrame();
		/** @brief Shuts the engine down, it cannot be initialized again afterwards */
		void Shutdown();

		/** @brief Enables or disables the internal frame limiter */
		void SetFrameLimiter(bool enabled);

		/** @brief Writes a snapshot of the current session to a stream, see @ref IAppEventHandler::OnSaveState() */
		bool SaveState(Death::IO::Stream& dest);
		/** @brief Restores a snapshot written by @ref SaveState(), see @ref IAppEventHandler::OnLoadState() */
		bool LoadState(std::shared_ptr<Death::IO::Stream> src);

		/** @brief Returns the directories advertised by the frontend */
		inline const HostPaths& GetHostPaths() const {
			return hostPaths_;
		}
		/** @brief Sets the directories advertised by the frontend, must be called before @ref Init() */
		inline void SetHostPaths(HostPaths&& hostPaths) {
			hostPaths_ = std::move(hostPaths);
		}

	private:
		HostPaths hostPaths_;
	};

	/** @brief Returns the application instance driven by the libretro frontend */
	LibretroApplication& theLibretroApplication();
}

#endif
