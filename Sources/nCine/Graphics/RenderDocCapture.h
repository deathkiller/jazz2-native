#pragma once

#include <cstdint>

namespace nCine
{
	/// In-application integration of RenderDoc
	class RenderDocCapture
	{
		friend class Application;

	public:
		static bool isAvailable();
		static bool isTargetControlConnected();
		static bool isFrameCapturing();

		static void apiVersion(std::int32_t* major, std::int32_t* minor, std::int32_t* patch);
		static bool isOverlayEnabled();
		static void enableOverlay(bool enabled);

		static void triggerCapture();
		static void triggerMultiFrameCapture(std::uint32_t numFrames);
		static bool endFrameCapture();
		static bool discardFrameCapture();

		static std::uint32_t numCaptures();
		static std::uint32_t captureInfo(std::uint32_t idx, char* filename, std::uint32_t* pathlength, std::uint64_t* timestamp);
		static const char* captureFilePathTemplate();
		static void setCaptureFilePathTemplate(const char* pathTemplate);
		static void setCaptureFileComments(const char* filePath, const char* comments);

		static std::uint32_t launchReplayUI(std::uint32_t connectTargetControl, const char* cmdLine);
		static void unloadCrashHandler();

	private:
		static void init();
		static void removeHooks();

		static void startFrameCapture();
	};

}
