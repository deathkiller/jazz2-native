#pragma once

#include <cstdint>

namespace nCine
{
	/**
		@brief In-application integration of the RenderDoc graphics debugger
		
		Wraps the RenderDoc in-application API, allowing frame captures to be triggered and
		configured at runtime when the application is launched through RenderDoc. All methods
		are no-ops or return default values when the RenderDoc API is not available.
	*/
	class RenderDocCapture
	{
		friend class Application;

	public:
		/** @brief Returns `true` if the RenderDoc API is loaded and available */
		static bool IsAvailable();
		/** @brief Returns `true` if a RenderDoc target control connection is established */
		static bool IsTargetControlConnected();
		/** @brief Returns `true` if a frame capture is currently in progress */
		static bool IsFrameCapturing();

		/** @brief Retrieves the version of the loaded RenderDoc API */
		static void GetApiVersion(std::int32_t* major, std::int32_t* minor, std::int32_t* patch);
		/** @brief Returns `true` if the RenderDoc overlay is enabled */
		static bool IsOverlayEnabled();
		/** @brief Enables or disables the RenderDoc overlay */
		static void EnableOverlay(bool enabled);

		/** @brief Schedules a capture of the next frame */
		static void TriggerCapture();
		/** @brief Schedules a capture spanning the specified number of frames */
		static void TriggerMultiFrameCapture(std::uint32_t numFrames);
		/** @brief Ends the current frame capture, returning `true` on success */
		static bool EndFrameCapture();
		/** @brief Discards the current frame capture without saving it, returning `true` on success */
		static bool DiscardFrameCapture();

		/** @brief Returns the number of captures made so far */
		static std::uint32_t GetCaptureCount();
		/** @brief Retrieves the file name and timestamp of the capture at the specified index */
		static std::uint32_t GetCaptureInfo(std::uint32_t idx, char* filename, std::uint32_t* pathlength, std::uint64_t* timestamp);
		/** @brief Returns the template used to build capture file paths */
		static const char* GetCaptureFilePathTemplate();
		/** @brief Sets the template used to build capture file paths */
		static void SetCaptureFilePathTemplate(const char* pathTemplate);
		/** @brief Sets the comments embedded in the capture file at the specified path */
		static void SetCaptureFileComments(const char* filePath, const char* comments);

		/** @brief Launches the RenderDoc replay UI, optionally connecting it through target control */
		static std::uint32_t LaunchReplayUI(std::uint32_t connectTargetControl, const char* cmdLine);
		/** @brief Unloads the RenderDoc crash handler */
		static void UnloadCrashHandler();

	private:
		static void Init();
		static void RemoveHooks();

		static void StartFrameCapture();
	};

}
