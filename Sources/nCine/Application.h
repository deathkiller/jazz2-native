#pragma once

#include "../Main.h"
#include "Graphics/IGfxDevice.h"
#include "Graphics/IDebugOverlay.h"
#include "AppConfiguration.h"
#include "Base/TimeStamp.h"

#include <memory>

#include <Containers/Function.h>
#include <Containers/StringView.h>
#include <Core/ITraceSink.h>
#include <IO/Stream.h>

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
#if defined(WITH_QT5)
	namespace Backends { class Qt5Widget; }
#endif

	/**
		@brief Delegate that creates an instance of @ref IAppEventHandler
		
		Passed to @ref MainApplication::Run() so the engine can instantiate the user's event handler.
	*/
	using CreateAppEventHandlerDelegate = std::unique_ptr<IAppEventHandler>(*)();

	/**
		@brief Base class for the main entry point of an nCine application
		
		Owns the engine subsystems (graphics device, scene graph, input manager, viewport) and drives
		the game loop. Backend-specific subclasses such as @ref MainApplication provide the platform glue.
	*/
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
#if defined(RHI_GL_PROFILE_ES2)
				// The OpenGL|ES 2.0 profile has no uniform buffer objects: per-instance data is uploaded as
				// plain uniforms per draw, so CPU sprite batching (which aggregates instances into a single
				// InstancesBlock UBO indexed by gl_VertexID) is disabled and every sprite draws individually
				// through its single-instance ESSL 100 program (corner attribute + loose uniforms).
				: batchingEnabled(false),
#else
				: batchingEnabled(true),
#endif
				  batchingWithIndices(false), cullingEnabled(true), minBatchSize(4),
#if defined(WITH_RHI_RSX)
				  // The PlayStation 3's batched shaders reach their instance array through the RSX's constant
				  // registers rather than a uniform buffer, so the batch is bounded by what fits there and by
				  // what the backend's batched corner stream covers - a far smaller number than the 585 a
				  // 64 KB UBO holds, and one the microcode has baked in (see RsxDevice::MaxBatchSize). A
				  // larger batch would draw its later sprites with instance data the shader cannot address.
				  maxBatchSize(32)
#else
				  maxBatchSize(585)
#endif
				  {}

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
			 * @brief ImGui viewport
			 *
			 * The viewport should mirror the screen dimensions or mouse input would not work. Setting `nullptr` is the same as setting the screen.
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

		/** @{ @name Constants */

		/** @brief Can be used in @ref AttachTraceTarget() to attach to a console */
		static constexpr char const* ConsoleTarget = "\n";

		/** @} */

		/** @brief Returns the configuration used to initialize the application */
		inline const AppConfiguration& GetAppConfiguration() const { return _appCfg; }
		/** @brief Returns the run-time rendering settings */
		inline RenderingSettings& GetRenderingSettings() { return _renderingSettings; }
#if defined(WITH_IMGUI) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Returns run-time GUI settings */
		inline GuiSettings& GetGuiSettings() { return _guiSettings; }
		/** @brief Returns debug overlay settings */
		inline IDebugOverlay::DisplaySettings& GetDebugOverlaySettings() { return (_debugOverlay != nullptr ? _debugOverlay->GetSettings() : _debugOverlayNullSettings); }
#endif
#if defined(NCINE_PROFILING) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Returns all timings */
		inline StaticArrayView<(std::int32_t)Timings::Count, const float> GetTimings() const { return _timings; }
#endif

		/** @brief Returns the graphics device instance */
		inline IGfxDevice& GetGfxDevice() { return *_gfxDevice; }
		/** @brief Returns the root of the transformation graph */
		inline SceneNode& GetRootNode() { return *_rootNode; }
		/** @brief Returns the screen viewport */
		Viewport& GetScreenViewport();
		/** @brief Returns the input manager instance */
		inline IInputManager& GetInputManager() { return *_inputManager; }

		/** @brief Returns the total number of frames already rendered */
		std::uint32_t GetFrameCount() const;
		/** @brief Returns a factor that represents how long the last frame took relative to the desired frame time */
		float GetTimeMult() const;
		/** @brief Returns the frame timer interface */
		const FrameTimer& GetFrameTimer() const;

		/** @brief Returns the drawable screen width as an integer number */
		inline std::int32_t GetWidth() const { return _gfxDevice->drawableWidth(); }
		/** @brief Returns the drawable screen height as an integer number */
		inline std::int32_t GetHeight() const { return _gfxDevice->drawableHeight(); }
		/** @brief Returns the drawable screen resolution as a `Vector2i` object */
		inline Vector2i GetResolution() const { return _gfxDevice->drawableResolution(); }

		/** @brief Resizes the screen viewport, if exists */
		void ResizeScreenViewport(std::int32_t width, std::int32_t height);

		/** @brief Returns whether the application should currently be suspended */
		bool ShouldSuspend();

		/** @brief Returns the value of the auto-suspension flag (the application will be suspended when it loses focus) */
		inline bool GetAutoSuspension() const {
			return _autoSuspension;
		}
		/** @brief Sets the auto-suspension flag value */
		inline void SetAutoSuspension(bool autoSuspension) {
			_autoSuspension = autoSuspension;
		}

		/** @brief Raises the quit flag */
		virtual void Quit();

		/** @brief Returns the quit flag value */
		inline bool ShouldQuit() const {
			return _shouldQuit;
		}

		/** @brief Returns the focus flag value */
		inline bool HasFocus() const {
			return _hasFocus;
		}

		/** @brief Returns the path for the application to load data from */
		inline const String& GetDataPath() const {
			return _appCfg.dataPath();
		}

		/** @brief Switches PS4 and PS5 controllers to use extended protocol which enables rumble and other features */
		virtual bool EnablePlayStationExtendedSupport(bool enable);
		/** @brief Returns the username of the logged-in user */
		virtual String GetUserName();
		/**
		 * @brief Returns the name of the device the application is running on
		 *
		 * The host name wherever the platform has one - the computer name on Windows, `gethostname()` on
		 * the POSIX systems, the nickname the console was given in its settings on the Wii and the Switch.
		 * A console with no name of its own answers with another identifier that is stable for the device,
		 * formatted as text: the Android ID, the WLAN MAC address of the PlayStation Portable, the OpenPSID
		 * of the PS Vita and the PlayStation 3, the i.Link ID of the PlayStation 2, the system ID the BIOS
		 * keeps on the Dreamcast. Empty where nothing of the kind exists (GameCube, Nintendo 64, web) or
		 * when the query fails.
		 */
		static String GetDeviceHostname();
		/** @brief Opens the specified URL in a default web browser */
		virtual bool OpenUrl(StringView url);
		
		/** @brief Returns `true` if screen (software) keyboard is supported and @ref ShowScreenKeyboard() should succeed */
		virtual bool CanShowScreenKeyboard();
		/** @brief Returns `true` if the screen (software) keyboard is currently shown, always `false` if it cannot be queried */
		virtual bool IsScreenKeyboardVisible();
		/** @brief Toggles the screen (software) keyboard */
		virtual bool ToggleScreenKeyboard();
		/**
			@brief Shows the screen (software) keyboard

			Two kinds of platform answer this, and a caller that wants to work on both has to serve both. Where
			the keyboard is an overlay that feeds keystrokes (Windows, Android) it types into whatever has
			focus and the text arrives as @relativeref{nCine,IInputEventHandler::OnTextInput()} events, exactly
			as a real keyboard would; @p initialText and @p onCompleted are unused there. Where it is a modal
			editor that collects a whole string (the PS Vita's IME) there are no keystrokes to deliver:
			@p initialText seeds the editor with what the field already holds, and @p onCompleted is invoked
			once with the finished string, which REPLACES that field rather than appending to it. It is not
			invoked at all if the user cancels.

			So pass both, keep handling `OnTextInput`, and the field ends up right either way.
		*/
		virtual bool ShowScreenKeyboard(Containers::StringView initialText = {},
			Containers::Function<void(Containers::StringView)>&& onCompleted = {});
		/** @brief Hides the screen (software) keyboard */
		virtual bool HideScreenKeyboard();

		/** @brief Adds the specified target as a sink for tracing */
		void AttachTraceTarget(Containers::StringView targetPath);

		/** @brief Overrides the base directory where crash memory dumps are written; no-op if crash handling is not enabled */
		void SetCrashDumpDirectory(Containers::StringView path);

		/** @brief Vibrates the device for the specified duration in milliseconds; no-op if not supported */
		virtual void Vibrate(std::int32_t milliseconds);

		/** @brief Shows the system status bar (if supported) */
		virtual void ShowStatusBar();
		/** @brief Hides the system status bar (if supported) */
		virtual void HideStatusBar();

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		AppConfiguration _appCfg;
		RenderingSettings _renderingSettings;
		bool _isSuspended;
		bool _autoSuspension;
		bool _hasFocus;
		bool _shouldQuit;
#if defined(WITH_IMGUI)
		GuiSettings _guiSettings;
		IDebugOverlay::DisplaySettings _debugOverlayNullSettings;
#endif
#if defined(NCINE_PROFILING)
		float _timings[(std::int32_t)Timings::Count];
#endif
#if defined(DEATH_TARGET_WINDOWS)
		HANDLE _waitableTimer;
#endif

		TimeStamp _profileStartTime;
		std::unique_ptr<FrameTimer> _frameTimer;
		std::unique_ptr<IGfxDevice> _gfxDevice;
		std::unique_ptr<SceneNode> _rootNode;
		std::unique_ptr<ScreenViewport> _screenViewport;
		std::unique_ptr<IInputManager> _inputManager;
		std::unique_ptr<IAppEventHandler> _appEventHandler;
#if defined(WITH_IMGUI)
		std::unique_ptr<IDebugOverlay> _debugOverlay;
		std::unique_ptr<ImGuiDrawing> _imguiDrawing;
#endif
#if defined(DEATH_TRACE)
		std::uint32_t _mainThreadId;
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
		/**
			@brief Attaches the trace sink

			Idempotent, so an entry point should call it as early as it has an application object - everything
			it does afterwards can then report through the ordinary `LOG*` macros. @ref PreInitCommon() calls
			it as well, for the entry points that cannot get there any sooner.
		*/
		void InitializeTrace();
		/** @brief Detaches the trace sink and closes the log file */
		void ShutdownTrace();

		// ITraceSink interface
		void OnTraceReceived(TraceLevel level, std::uint64_t timestamp, StringView threadId, StringView functionName, StringView content) override;
		void OnTraceFlushed() override;
#endif

	private:
		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		friend class MainApplication;
#if defined(DEATH_TARGET_ANDROID)
		friend class AndroidApplication;
#endif
#if defined(WITH_QT5)
		// The widget drives the loop and the lifetime of the embedded application, like MainApplication does
		friend class Backends::Qt5Widget;
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
		friend class IGfxDevice;
#endif
		friend class Viewport;

#if defined(DEATH_TRACE)
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		void AppendLogFileHeader(IO::Stream& s);
#	endif
#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		bool CreateTraceConsole(StringView title, bool& hasVirtualTerminal);
		void DestroyTraceConsole();
#	endif
#endif
	};

	extern Application& theApplication();

}
