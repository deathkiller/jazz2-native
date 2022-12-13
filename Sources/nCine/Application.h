#pragma once

#include "Graphics/IGfxDevice.h"
#include "AppConfiguration.h"
#include "Base/TimeStamp.h"

#include <memory>

namespace nCine
{
	class FrameTimer;
	class SceneNode;
	class Viewport;
	class ScreenViewport;
	class IInputManager;
	class IAppEventHandler;

	/// Main entry point and handler for nCine applications
	class Application
	{
	public:
		/// Rendering settings that can be changed at run-time
		struct RenderingSettings
		{
			RenderingSettings()
				: batchingEnabled(true), batchingWithIndices(false), cullingEnabled(true), minBatchSize(4), maxBatchSize(500) { }

			/// True if batching is enabled
			bool batchingEnabled;
			/// True if using indices for vertex batching
			bool batchingWithIndices;
			/// True if node culling is enabled
			bool cullingEnabled;
			/// Minimum size for a batch to be collected
			unsigned int minBatchSize;
			/// Maximum size for a batch before a forced split
			unsigned int maxBatchSize;
		};

		enum class Timings
		{
			PreInit,
			InitCommon,
			AppInit,
			FrameStart,
			UpdateVisitDraw,
			Update,
			PostUpdate,
			Visit,
			Draw,
			FrameEnd,

			Count
		};

		/// Returns the configuration used to initialize the application
		inline const AppConfiguration& appConfiguration() const {
			return appCfg_;
		}
		/// Returns the run-time rendering settings
		inline RenderingSettings& renderingSettings() {
			return renderingSettings_;
		}
#if defined(NCINE_PROFILING)
		/// Returns all timings
		inline const float* timings() const {
			return timings_;
		}
#endif

		/// Returns the graphics device instance
		inline IGfxDevice& gfxDevice() {
			return *gfxDevice_;
		}
		/// Returns the root of the transformation graph
		inline SceneNode& rootNode() {
			return *rootNode_;
		}
		/// Returns the screen viewport
		Viewport& screenViewport();
		/// Returns the input manager instance
		inline IInputManager& inputManager() {
			return *inputManager_;
		}

		/// Returns the total number of frames already rendered
		unsigned long int numFrames() const;
		/// Returns the average FPS during the update interval
		float averageFps() const;
		/// Returns a factor that represents how long the last frame took relative to the desired frame time
		float timeMult() const;

		/// Returns the drawable screen width as an integer number
		inline int width() const { return gfxDevice_->drawableWidth(); }
		/// Returns the drawable screen height as an integer number
		inline int height() const { return gfxDevice_->drawableHeight(); }
		/// Returns the drawable screen resolution as a `Vector2i` object
		inline Vector2i resolution() const { return gfxDevice_->drawableResolution(); }

		/// Resizes the screen viewport, if exists
		void resizeScreenViewport(int width, int height);

		/// Returns the value of the suspension flag
		/*! If `true` the application is suspended, it will neither update nor receive events */
		inline bool isSuspended() const {
			return isSuspended_;
		}
		/// Sets the suspension flag value
		inline void setSuspended(bool suspended) {
			isSuspended_ = suspended;
		}

		/// Returns the value of the auto-suspension flag
		/*! If `true` the application will be suspended when it loses focus */
		inline bool autoSuspension() const {
			return autoSuspension_;
		}
		/// Sets the auto-suspension flag value
		inline void setAutoSuspension(bool autoSuspension) {
			autoSuspension_ = autoSuspension;
		}

		/// Raises the quit flag
		inline void quit() {
			shouldQuit_ = true;
		}
		/// Returns the quit flag value
		inline bool shouldQuit() const {
			return shouldQuit_;
		}

		/// Returns the focus flag value
		inline bool hasFocus() const {
			return hasFocus_;
		}

	protected:
		bool isSuspended_;
		bool autoSuspension_;
		bool hasFocus_;
		bool shouldQuit_;
		AppConfiguration appCfg_;
		RenderingSettings renderingSettings_;
#if defined(NCINE_PROFILING)
		float timings_[(int)Timings::Count];
#endif

		TimeStamp profileStartTime_;
		std::unique_ptr<FrameTimer> frameTimer_;
		std::unique_ptr<IGfxDevice> gfxDevice_;
		std::unique_ptr<SceneNode> rootNode_;
		std::unique_ptr<ScreenViewport> screenViewport_;
		std::unique_ptr<IInputManager> inputManager_;
		std::unique_ptr<IAppEventHandler> appEventHandler_;

		Application();
		~Application();

		/// Must be called before giving control to the application
		void initCommon();
		/// A single step of the game loop made to render a frame
		void step();
		/// Must be called before exiting to shut down the application
		void shutdownCommon();

		/// Called when the application gets suspended
		void suspend();
		/// Called when the application resumes execution
		void resume();

		bool shouldSuspend();

		/// Sets the focus flag
		virtual void setFocus(bool hasFocus);

	private:
		/// Deleted copy constructor
		Application(const Application&) = delete;
		/// Deleted assignment operator
		Application& operator=(const Application&) = delete;

		friend class MainApplication;
#if defined(DEATH_TARGET_ANDROID)
		friend class AndroidApplication;
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
		friend class IGfxDevice;
#endif
		friend class Viewport;
	};

	// Meyers' Singleton
	extern Application& theApplication();
}
