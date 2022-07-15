#pragma once

namespace nCine
{
	class AppConfiguration;

	/// The interface class for handling nCine application events
	class IAppEventHandler
	{
	public:
		/// Pure virtual destructor in order to make the class abstract
		virtual ~IAppEventHandler() = 0;

		/// Called once before initialization to setup the application
		/*! \warning At this stage it is only safe to modify the `config` object.
		 *  No other engine API calls should be made. */
		virtual void onPreInit(AppConfiguration& config) { }
		/// Called once on application initialization
		virtual void onInit() { }
		/// Called at the start of each frame
		virtual void onFrameStart() { }
		/// Called every time the scenegraph has been traversed and all nodes have been transformed
		virtual void onPostUpdate() { }
		/// Called at the end of each frame, just before swapping buffers
		virtual void onFrameEnd() { }
		/// Called once on application shutdown
		virtual void onShutdown() { }
		/// Called every time the application needs to be suspended
		virtual void onSuspend() { }
		/// Called every time the application resumes from suspension
		virtual void onResume() { }
		/// Called every time the root viewport (window) is resized
		virtual void onRootViewportResized(int width, int height) { }
	};

	inline IAppEventHandler::~IAppEventHandler() { }

}
