#pragma once

#include "../Main.h"
#include "Graphics/IGfxDevice.h"
#include "Graphics/IDebugOverlay.h"
#include "AppConfiguration.h"
#include "Base/TimeStamp.h"

#include <memory>

#include <Containers/StringView.h>
#include <Core/ITraceSink.h>

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

	/** @brief Delegate type that creates an instance of @ref IAppEventHandler */
	using CreateAppEventHandlerDelegate = std::unique_ptr<IAppEventHandler>(*)();

	/** @brief Base class for main entry points of nCine applications */
	class Application
#if defined(DEATH_TRACE)
		: public ITraceSink
#endif
	{
	public:
		/** @brief Rendering settings that can be changed at run-time */
		struct RenderingSettings
		{
			RenderingSettings()
				: batchingEnabled(true), batchingWithIndices(false), cullingEnabled(true), minBatchSize(4), maxBatchSize(585) {}

			/** @brief Whether batching is enabled */
			bool batchingEnabled;
			/** @brief Whether using indices for vertex batching */
			bool batchingWithIndices;
			/** @brief Whether node culling is enabled */
			bool cullingEnabled;
			/** @brief Minimum size for a batch to be collected */
			std::uint32_t minBatchSize;
			/** @brief Maximum size for a batch before a forced split */
			std::uint32_t maxBatchSize;
		};

#if defined(WITH_IMGUI) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief GUI settings (for ImGui) that can be changed at run-time */
		struct GuiSettings
		{
			GuiSettings();

			/** @brief ImGui drawable node layer */
			std::uint16_t imguiLayer;

			/**
				@brief ImGui viewport
			
				The viewport should mirror the screen dimensions or mouse input would not work. Setting `nullptr` is the same as setting the screen
			*/
			Viewport* imguiViewport;
		};
#endif

		/** @brief Timings for profiling */
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

		/** @brief Can be used in @ref AttachTraceTarget() to attach to a console */
		static constexpr char const* ConsoleTarget = "\n";

		/** @brief Returns the configuration used to initialize the application */
		inline const AppConfiguration& GetAppConfiguration() const { return appCfg_; }
		/** @brief Returns the run-time rendering settings */
		inline RenderingSettings& GetRenderingSettings() { return renderingSettings_; }
#if defined(WITH_IMGUI) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Returns run-time GUI settings */
		inline GuiSettings& GetGuiSettings() { return guiSettings_; }
		/** @brief Returns debug overlay settings */
		inline IDebugOverlay::DisplaySettings& GetDebugOverlaySettings() { return (debugOverlay_ != nullptr ? debugOverlay_->settings() : debugOverlayNullSettings_); }
#endif
#if defined(NCINE_PROFILING) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Returns all timings */
		inline StaticArrayView<(std::int32_t)Timings::Count, const float> GetTimings() const { return timings_; }
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
		std::uint32_t GetFrameCount() const;
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

		/** @brief Returns whether the application should currently be suspended */
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
		virtual void Quit();

		/** @brief Returns the quit flag value */
		inline bool ShouldQuit() const {
			return shouldQuit_;
		}

		/** @brief Returns the focus flag value */
		inline bool HasFocus() const {
			return hasFocus_;
		}

		/** @brief Returns the path for the application to load data from */
		inline const String& GetDataPath() const {
			return appCfg_.dataPath();
		}

		/** @brief Switches PS4 and PS5 controllers to use extended protocol which enables rumble and other features */
		virtual bool EnablePlayStationExtendedSupport(bool enable);
		/** @brief Returns the username of the logged-in user */
		virtual String GetUserName();
		/** @brief Opens the specified URL in a default web browser */
		virtual bool OpenUrl(StringView url);

		/** @brief Adds the specified target as a sink for tracing */
		void AttachTraceTarget(Containers::StringView targetPath);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
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
#endif

		Application();
		~Application();

		/** @brief Must be called as early as possible during the application startup */
		void PreInitCommon(std::unique_ptr<IAppEventHandler> appEventHandler);
		/** @brief Must be called before giving control to the application */
		void InitCommon();
		/** @brief Processes a single step of the game loop and renders a frame */
		void Step();
		/** @brief Must be called before exiting to shut down the application */
		void ShutdownCommon();

		/** @brief Called when the application gets suspended */
		void Suspend();
		/** @brief Called when the application resumes execution */
		void Resume();

		/** @brief Sets the focus flag */
		virtual void SetFocus(bool hasFocus);

#if defined(DEATH_TRACE)
		// ITraceSink interface
		void OnTraceReceived(TraceLevel level, std::uint64_t timestamp, StringView threadId, StringView message) override;
		void OnTraceFlushed() override;
#endif

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
		void InitializeTrace();
		void ShutdownTrace();

#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		bool CreateTraceConsole(StringView title, bool& hasVirtualTerminal);
		void DestroyTraceConsole();
#	endif
#endif
	};

	extern Application& theApplication();

}
