#pragma once

namespace nCine
{
	class AppConfiguration;
	class Viewport;

	/// The interface class for handling nCine application events
	class IAppEventHandler
	{
	public:
		/// Pure virtual destructor in order to make the class abstract
		virtual ~IAppEventHandler() = 0;

		/// Called once before initialization to setup the application
		/*! \warning At this stage it is only safe to modify the `config` object.
		 *  No other engine API calls should be made. */
		virtual void OnPreInitialize(AppConfiguration& config) {}
		/// Called once on application initialization
		virtual void OnInitialize() {}
		/// Called at the start of each frame
		virtual void OnBeginFrame() {}
		/// Called every time the scenegraph has been traversed and all nodes have been transformed
		virtual void OnPostUpdate() {}
		/// Called every time a viewport is going to be drawn
		virtual void OnDrawViewport(Viewport& viewport) {}
		/// Called at the end of each frame, just before swapping buffers
		virtual void OnEndFrame() {}
		/// Called every time the window is resized (by the system or the user)
		virtual void OnResizeWindow(std::int32_t width, std::int32_t height) {}
		/// Called once on application shutdown
		virtual void OnShutdown() {}
		/// Called every time the application needs to be suspended
		virtual void OnSuspend() {}
		/// Called every time the application resumes from suspension
		virtual void OnResume() {}
	};

	inline IAppEventHandler::~IAppEventHandler() {}

}
