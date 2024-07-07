#pragma once

#include "../Common.h"
#include "Graphics/IGfxDevice.h"
#include "Graphics/IDebugOverlay.h"
#include "AppConfiguration.h"
#include "Base/TimeStamp.h"

#include <memory>

#include <Containers/StringView.h>

#if defined(DEATH_TARGET_WINDOWS)
#	include <CommonWindows.h>
#endif

using namespace Death;

namespace nCine
{
	class FrameTimer;
	class SceneNode;
	class Viewport;
	class ScreenViewport;
	class IInputManager;
	class IAppEventHandler;
#if defined(WITH_IMGUI)
	class ImGuiDrawing;
#endif

	/// Main entry point and handler for nCine applications
	class Application
	{
	public:
		/// Rendering settings that can be changed at run-time
		struct RenderingSettings
		{
			RenderingSettings()
				: batchingEnabled(true), batchingWithIndices(false), cullingEnabled(true), minBatchSize(4), maxBatchSize(585) { }

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

#if defined(WITH_IMGUI)
		/// GUI settings (for ImGui) that can be changed at run-time
		struct GuiSettings
		{
			GuiSettings();

			/// ImGui drawable node layer
			uint16_t imguiLayer;
			/// ImGui viewport
			/*! \note The viewport should mirror the screen dimensions or mouse input would not work. Setting `nullptr` is the same as setting the screen */
			Viewport* imguiViewport;
		};
#endif

		enum class Timings
		{
			PreInit,
			InitCommon,
			AppInit,
			BeginFrame,
			UpdateVisitDraw,
			Update,
			PostUpdate,
			Visit,
			Draw,
			ImGui,
			EndFrame,

			Count
		};

		/// Can be used in AttachTraceTarget() to attach to a console (if exists)
		static constexpr char const* ConsoleTarget = "\n";

		/// Returns the configuration used to initialize the application
		inline const AppConfiguration& GetAppConfiguration() const {
			return appCfg_;
		}
		/// Returns the run-time rendering settings
		inline RenderingSettings& GetRenderingSettings() {
			return renderingSettings_;
		}
#if defined(WITH_IMGUI)
		/// Returns the run-time GUI settings
		inline GuiSettings& GetGuiSettings() {
			return guiSettings_;
		}
		/// Returns the debug overlay object, if any
		inline IDebugOverlay::DisplaySettings& GetDebugOverlaySettings() {
			return (debugOverlay_ != nullptr ? debugOverlay_->settings() : debugOverlayNullSettings_);
		}
#endif
#if defined(NCINE_PROFILING)
		/// Returns all timings
		inline const float* GetTimings() const {
			return timings_;
		}
#endif

		/// Returns the graphics device instance
		inline IGfxDevice& GetGfxDevice() {
			return *gfxDevice_;
		}
		/// Returns the root of the transformation graph
		inline SceneNode& GetRootNode() {
			return *rootNode_;
		}
		/// Returns the screen viewport
		Viewport& GetScreenViewport();
		/// Returns the input manager instance
		inline IInputManager& GetInputManager() {
			return *inputManager_;
		}

		/// Returns the total number of frames already rendered
		unsigned long int GetFrameCount() const;
		/// Returns a factor that represents how long the last frame took relative to the desired frame time
		float GetTimeMult() const;
		/// Returns the frame timer interface
		const FrameTimer& GetFrameTimer() const;

		/// Returns the drawable screen width as an integer number
		inline std::int32_t GetWidth() const { return gfxDevice_->drawableWidth(); }
		/// Returns the drawable screen height as an integer number
		inline std::int32_t GetHeight() const { return gfxDevice_->drawableHeight(); }
		/// Returns the drawable screen resolution as a `Vector2i` object
		inline Vector2i GetResolution() const { return gfxDevice_->drawableResolution(); }

		/// Resizes the screen viewport, if exists
		void ResizeScreenViewport(std::int32_t width, std::int32_t height);

		bool ShouldSuspend();

		/// Returns the value of the auto-suspension flag
		/*! If `true` the application will be suspended when it loses focus */
		inline bool GetAutoSuspension() const {
			return autoSuspension_;
		}
		/// Sets the auto-suspension flag value
		inline void SetAutoSuspension(bool autoSuspension) {
			autoSuspension_ = autoSuspension;
		}

		/// Raises the quit flag
		inline void Quit() {
			shouldQuit_ = true;
		}
		/// Returns the quit flag value
		inline bool ShouldQuit() const {
			return shouldQuit_;
		}

		/// Returns the focus flag value
		inline bool HasFocus() const {
			return hasFocus_;
		}

		inline const String& GetDataPath() const {
			return appCfg_.dataPath();
		}

		virtual void AttachTraceTarget(Containers::StringView targetPath);

	protected:
		AppConfiguration appCfg_;
		RenderingSettings renderingSettings_;
		bool isSuspended_;
		bool autoSuspension_;
		bool hasFocus_;
		bool shouldQuit_;
#if defined(WITH_IMGUI)
		GuiSettings guiSettings_;
		IDebugOverlay::DisplaySettings debugOverlayNullSettings_;
#endif
#if defined(NCINE_PROFILING)
		float timings_[(std::int32_t)Timings::Count];
#endif
#if 0 //defined(DEATH_TARGET_WINDOWS)
		HANDLE _waitableTimer;
#endif

		TimeStamp profileStartTime_;
		std::unique_ptr<FrameTimer> frameTimer_;
		std::unique_ptr<IGfxDevice> gfxDevice_;
		std::unique_ptr<SceneNode> rootNode_;
		std::unique_ptr<ScreenViewport> screenViewport_;
		std::unique_ptr<IInputManager> inputManager_;
		std::unique_ptr<IAppEventHandler> appEventHandler_;
#if defined(WITH_IMGUI)
		std::unique_ptr<IDebugOverlay> debugOverlay_;
		std::unique_ptr<ImGuiDrawing> imguiDrawing_;
#endif

		Application();
		~Application();

		void PreInitCommon(std::unique_ptr<IAppEventHandler> appEventHandler);
		/// Must be called before giving control to the application
		void InitCommon();
		/// A single step of the game loop made to render a frame
		void Step();
		/// Must be called before exiting to shut down the application
		void ShutdownCommon();

		/// Called when the application gets suspended
		void Suspend();
		/// Called when the application resumes execution
		void Resume();

		/// Sets the focus flag
		virtual void SetFocus(bool hasFocus);

	private:
		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		friend class MainApplication;
#if defined(DEATH_TARGET_ANDROID)
		friend class AndroidApplication;
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
		friend class IGfxDevice;
#endif
		friend class Viewport;

#if defined(DEATH_TRACE)
		friend void ::DEATH_TRACE(TraceLevel level, const char* fmt, ...);
#endif
	};

	// Meyers' Singleton
	extern Application& theApplication();
}
