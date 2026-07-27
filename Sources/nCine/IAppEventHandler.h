#pragma once

#if defined(WITH_LIBRETRO) || defined(DOXYGEN_GENERATING_OUTPUT)
#	include <memory>

namespace Death { namespace IO {
	class Stream;
}}
#endif

namespace nCine
{
	class AppConfiguration;
	class Viewport;

	/**
		@brief Interface for handling nCine application lifecycle and frame events
		
		User code derives from this interface and overrides the callbacks of interest. The instance is
		created by the @ref CreateAppEventHandlerDelegate passed to @ref MainApplication::Run().
	*/
	class IAppEventHandler
	{
	public:
		virtual ~IAppEventHandler() = 0;

		/** @brief Called once before initialization to setup the application */
		virtual void OnPreInitialize(AppConfiguration& config) {}
		/** @brief Called once on application initialization */
		virtual void OnInitialize() {}
		/** @brief Called at the start of each frame */
		virtual void OnBeginFrame() {}
		/** @brief Called every time the scenegraph has been traversed and all nodes have been transformed */
		virtual void OnPostUpdate() {}
		/** @brief Called every time a viewport is going to be drawn */
		virtual void OnDrawViewport(Viewport& viewport) {}
		/** @brief Called at the end of each frame, just before swapping buffers */
		virtual void OnEndFrame() {}
		/** @brief Called every time the window is resized (by the system or the user) */
		virtual void OnResizeWindow(std::int32_t width, std::int32_t height) {}
		/** @brief Called once on application shutdown */
		virtual void OnShutdown() {}
		/** @brief Called every time the application needs to be suspended */
		virtual void OnSuspend() {}
		/** @brief Called every time the application resumes from suspension */
		virtual void OnResume() {}
		/** @brief Called when the `Back` gesture is invoked */
		virtual void OnBackInvoked() {}
#if defined(WITH_LIBRETRO) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Called when the host frontend asks for a snapshot of the current session */
		virtual bool OnSaveState(Death::IO::Stream& dest) {
			return false;
		}
		/** @brief Called when the host frontend restores a snapshot written by @ref OnSaveState() */
		virtual bool OnLoadState(std::shared_ptr<Death::IO::Stream> src) {
			return false;
		}
#endif
	};

	inline IAppEventHandler::~IAppEventHandler() {}

}
