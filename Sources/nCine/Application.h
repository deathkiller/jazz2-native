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

	/** @brief Main entry point and handler for nCine applications */
	class Application
	{
	public:
		/** @brief Rendering settings that can be changed at run-time */
		struct RenderingSettings
		{
			RenderingSettings()
				: batchingEnabled(true), batchingWithIndices(false), cullingEnabled(true), minBatchSize(4), maxBatchSize(585) { }

			/** @brief `true` if batching is enabled */
			bool batchingEnabled;
			/** @brief `true` if using indices for vertex batching */
			bool batchingWithIndices;
			/** @brief `true` if node culling is enabled */
			bool cullingEnabled;
			/** @brief Minimum size for a batch to be collected */
			unsigned int minBatchSize;
			/** @brief Maximum size for a batch before a forced split */
			unsigned int maxBatchSize;
		};

#if defined(WITH_IMGUI)
		/** @brief GUI settings (for ImGui) that can be changed at run-time */
		struct GuiSettings
		{
			GuiSettings();

			/** @brief ImGui drawable node layer */
			uint16_t imguiLayer;

			/**
				@brief ImGui viewport
			
				The viewport should mirror the screen dimensions or mouse input would not work. Setting `nullptr` is the same as setting the screen
			*/
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

		/** @brief Can be used in AttachTraceTarget() to attach to a console (if exists) */
		static constexpr char const* ConsoleTarget = "\n";

		/** @brief Returns the configuration used to initialize the application */
		inline const AppConfiguration& GetAppConfiguration() const { return appCfg_; }
		/** @brief Returns the run-time rendering settings */
		inline RenderingSettings& GetRenderingSettings() { return renderingSettings_; }
#if defined(WITH_IMGUI)
		/** @brief Returns the run-time GUI settings */
		inline GuiSettings& GetGuiSettings() { return guiSettings_; }
		/** @brief Returns the debug overlay object, if any */
		inline IDebugOverlay::DisplaySettings& GetDebugOverlaySettings() { return (debugOverlay_ != nullptr ? debugOverlay_->settings() : debugOverlayNullSettings_); }
#endif
#if defined(NCINE_PROFILING)
		/** @brief Returns all timings */
		inline const float* GetTimings() const { return timings_; }
#endif

		/** @brief Returns the graphics device instance */
		inline IGfxDevice& GetGfxDevice() { return *gfxDevice_; }
		/** @brief Returns the root of the transformation graph */
		inline SceneNode& GetRootNode() { return *rootNode_; }
		/** @brief Returns the screen viewport */
		Viewport& GetScreenViewport();
		/** @brief Returns the input manager instance */
		inline IInputManager& GetInputManager() { return *inputManager_; }

		/** @brief Returns the total number of frames already rendered */
		unsigned long int GetFrameCount() const;
		/** @brief Returns a factor that represents how long the last frame took relative to the desired frame time */
		float GetTimeMult() const;
		/** @brief Returns the frame timer interface */
		const FrameTimer& GetFrameTimer() const;

		/** @brief Returns the drawable screen width as an integer number */
		inline std::int32_t GetWidth() const { return gfxDevice_->drawableWidth(); }
		/** @brief Returns the drawable screen height as an integer number */
		inline std::int32_t GetHeight() const { return gfxDevice_->drawableHeight(); }
		/** @brief Returns the drawable screen resolution as a `Vector2i` object */
		inline Vector2i GetResolution() const { return gfxDevice_->drawableResolution(); }

		/** @brief Resizes the screen viewport, if exists */
		void ResizeScreenViewport(std::int32_t width, std::int32_t height);

		bool ShouldSuspend();

		/** @brief Returns the value of the auto-suspension flag (the application will be suspended when it loses focus) */
		inline bool GetAutoSuspension() const {
			return autoSuspension_;
		}
		/** @brief Sets the auto-suspension flag value */
		inline void SetAutoSuspension(bool autoSuspension) {
			autoSuspension_ = autoSuspension;
		}

		/** @brief Raises the quit flag */
		inline void Quit() {
			shouldQuit_ = true;
		}
		/** @brief Returns the quit flag value */
		inline bool ShouldQuit() const {
			return shouldQuit_;
		}

		/** @brief Returns the focus flag value */
		inline bool HasFocus() const {
			return hasFocus_;
		}

		inline const String& GetDataPath() const {
			return appCfg_.dataPath();
		}

		void AttachTraceTarget(Containers::StringView targetPath);

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
		/** @brief Must be called before giving control to the application */
		void InitCommon();
		/** @brief A single step of the game loop made to render a frame */
		void Step();
		/** @brief Must be called before exiting to shut down the application */
		void ShutdownCommon();

		/** @brief Called when the application gets suspended */
		void Suspend();
		/** @brief Called when the application resumes execution */
		void Resume();

		/** @brief Sets the focus flag */
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

		void PreInitTrace();
		void InitTrace();
		void ShutdownTrace();

#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		bool CreateTraceConsole(const StringView& title, bool& hasVirtualTerminal);
		void DestroyTraceConsole();
#	endif
#endif
	};

	extern Application& theApplication();

}
