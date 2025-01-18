#pragma once

namespace nCine
{
	class AppConfiguration;
	class Viewport;

	/** @brief Interface for handling nCine application events */
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
	};

	inline IAppEventHandler::~IAppEventHandler() {}

}
