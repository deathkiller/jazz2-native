#include "../Common.h"

#if defined(DEATH_TARGET_WINDOWS)
extern "C"
{
	_declspec(dllexport) unsigned long int NvOptimusEnablement = 0x00000001;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;
};
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	pragma comment(lib, "opengl32.lib")
#	if defined(_M_X64)
#		if defined(WITH_GLEW)
#			pragma comment(lib, "../Libs/Windows/x64/glew32.lib")
#		endif
#		if defined(WITH_GLFW)
#			pragma comment(lib, "../Libs/Windows/x64/glfw3dll.lib")
#		endif
#		if defined(WITH_SDL)
#			pragma comment(lib, "../Libs/Windows/x64/SDL2.lib")
#		endif
#		if defined(WITH_AUDIO)
#			pragma comment(lib, "../Libs/Windows/x64/OpenAL32.lib")
#		endif
#		pragma comment(lib, "../Libs/Windows/x64/libdeflate.lib")
#	elif defined(_M_IX86)
#		if defined(WITH_GLEW)
#			pragma comment(lib, "../Libs/Windows/x86/glew32.lib")
#		endif
#		if defined(WITH_GLFW)
#			pragma comment(lib, "../Libs/Windows/x86/glfw3dll.lib")
#		endif
#		if defined(WITH_SDL)
#			pragma comment(lib, "../Libs/Windows/x86/SDL2.lib")
#		endif
#		if defined(WITH_AUDIO)
#			pragma comment(lib, "../Libs/Windows/x86/OpenAL32.lib")
#		endif
#		pragma comment(lib, "../Libs/Windows/x86/libdeflate.lib")
#	else
#		error Unsupported architecture
#	endif
#endif

#include "Application.h"
#include "Base/Algorithms.h"
#include "Base/Random.h"
#include "IAppEventHandler.h"
#include "ArrayIndexer.h"
#include "Graphics/GfxCapabilities.h"
#include "Graphics/RenderResources.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/ScreenViewport.h"
#include "Graphics/GL/GLDebug.h"
#include "Base/Timer.h"
#include "Base/FrameTimer.h"
#include "Graphics/SceneNode.h"
#include "Input/IInputManager.h"
#include "Input/JoyMapping.h"
#include "ServiceLocator.h"
#include "tracy.h"
#include "tracy_opengl.h"

#include <Containers/StringView.h>
#include <IO/FileSystem.h>

#if defined(WITH_AUDIO)
#	include "Audio/ALAudioDevice.h"
#endif

#if defined(WITH_THREADS)
#	include "Threading/ThreadPool.h"
#endif

#if defined(WITH_LUA)
#	include "LuaStatistics.h"
#endif

#if defined(WITH_IMGUI)
#	include "Graphics/ImGuiDrawing.h"
#	include "Graphics/ImGuiDebugOverlay.h"
#endif

#if defined(WITH_RENDERDOC)
#	include "Graphics/RenderDocCapture.h"
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

#if defined(DEATH_TRACE)

#if defined(DEATH_TARGET_WINDOWS)
#	include <Utf8.h>
#elif defined(DEATH_TARGET_ANDROID)
#	include <stdarg.h>
#	include <time.h>
#	include <android/log.h>
#else
#	include <cstdarg>
#	if defined(DEATH_TARGET_SWITCH)
#		include <time.h>
#		include <switch.h>
#	endif
#endif

#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_SWITCH)
#	include <IO/FileStream.h>
extern std::unique_ptr<Death::IO::Stream> __logFile;
#endif
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
extern bool __showLogConsole;
#endif
#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT))
extern bool __hasVirtualTerminal;
#endif

void DEATH_TRACE(TraceLevel level, const char* fmt, ...)
{
	constexpr std::int32_t MaxEntryLength = 4096;
	char logEntry[MaxEntryLength];

#if defined(DEATH_TARGET_ANDROID)
	android_LogPriority priority;

	// clang-format off
	switch (level) {
		case TraceLevel::Fatal:		priority = ANDROID_LOG_FATAL; break;
		case TraceLevel::Error:		priority = ANDROID_LOG_ERROR; break;
		case TraceLevel::Warning:	priority = ANDROID_LOG_WARN; break;
		case TraceLevel::Info:		priority = ANDROID_LOG_INFO; break;
		default:					priority = ANDROID_LOG_DEBUG; break;
	}
	// clang-format on

	va_list args;
	va_start(args, fmt);
	/*std::int32_t length =*/ vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	__android_log_write(priority, "jazz2", logEntry);

	if (__logFile != nullptr) {
		const char* levelIdentifier;
		switch (level) {
			case TraceLevel::Fatal:		levelIdentifier = "F"; break;
			case TraceLevel::Error:		levelIdentifier = "E"; break;
			case TraceLevel::Warning:	levelIdentifier = "W"; break;
			case TraceLevel::Info:		levelIdentifier = "I"; break;
			default:					levelIdentifier = "D"; break;
		}

		struct timespec currentTime;
		clock_gettime(CLOCK_REALTIME, &currentTime);

		time_t seconds = currentTime.tv_sec;
		long milliseconds = (currentTime.tv_nsec / 1000000) % 1000;

		struct tm localTime;
		localtime_r(&seconds, &localTime);

		FileStream* s = static_cast<FileStream*>(__logFile.get());
		fprintf(s->GetHandle(), "%02d:%02d:%02d.%03ld [%s] %s\n", localTime.tm_hour, localTime.tm_min, localTime.tm_sec, milliseconds, levelIdentifier, logEntry);
		fflush(s->GetHandle());
	}
#elif defined(DEATH_TARGET_SWITCH)
	va_list args;
	va_start(args, fmt);
	std::int32_t length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	svcOutputDebugString(logEntry, length);

	if (__logFile != nullptr) {
		const char* levelIdentifier;
		switch (level) {
			case TraceLevel::Fatal:		levelIdentifier = "F"; break;
			case TraceLevel::Error:		levelIdentifier = "E"; break;
			case TraceLevel::Warning:	levelIdentifier = "W"; break;
			case TraceLevel::Info:		levelIdentifier = "I"; break;
			default:					levelIdentifier = "D"; break;
		}

		struct timespec currentTime;
		clock_gettime(CLOCK_REALTIME, &currentTime);

		time_t seconds = currentTime.tv_sec;
		long milliseconds = (currentTime.tv_nsec / 1000000) % 1000;

		struct tm localTime;
		localtime_r(&seconds, &localTime);

		FileStream* s = static_cast<FileStream*>(__logFile.get());
		fprintf(s->GetHandle(), "%02d:%02d:%02d.%03ld [%s] %s\n", localTime.tm_hour, localTime.tm_min, localTime.tm_sec, milliseconds, levelIdentifier, logEntry);
		fflush(s->GetHandle());
	}
#elif defined(DEATH_TARGET_WINDOWS_RT)
	logEntry[0] = '[';
	switch (level) {
		case TraceLevel::Fatal:		logEntry[1] = 'F'; break;
		case TraceLevel::Error:		logEntry[1] = 'E'; break;
		case TraceLevel::Warning:	logEntry[1] = 'W'; break;
		case TraceLevel::Info:		logEntry[1] = 'I'; break;
		default:					logEntry[1] = 'D'; break;
	}
	logEntry[2] = ']';
	logEntry[3] = ' ';

	va_list args;
	va_start(args, fmt);
	std::int32_t length = vsnprintf(logEntry + 4, MaxEntryLength - 4, fmt, args) + 4;
	va_end(args);

	if (length >= MaxEntryLength - 2) {
		length = MaxEntryLength - 2;
	}

	logEntry[length++] = '\n';
	logEntry[length] = '\0';

	::OutputDebugString(Death::Utf8::ToUtf16(logEntry));
#else
	static const char Reset[] = "\033[0m";
	static const char Bold[] = "\033[1m";
	static const char Faint[] = "\033[2m";
	static const char DarkGray[] = "\033[90m";
	static const char BrightRed[] = "\033[91m";
	static const char BrightYellow[] = "\033[93m";

#	if defined(DEATH_TARGET_WINDOWS)
	if (__showLogConsole) {
#	endif

	char logEntryWithColors[MaxEntryLength];
	logEntryWithColors[0] = '\0';
	logEntryWithColors[MaxEntryLength - 1] = '\0';

	va_list args;
	va_start(args, fmt);
	std::int32_t length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	// Colorize the output
#	if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS)
	const bool hasVirtualTerminal = __hasVirtualTerminal;
#	else
	constexpr bool hasVirtualTerminal = false;
#	endif

	std::int32_t logMsgFuncLength = 0;
	while (true) {
		if (logEntry[logMsgFuncLength] == '\0') {
			logMsgFuncLength = -1;
			break;
		}
		if (logMsgFuncLength > 0 && logEntry[logMsgFuncLength - 1] == '-' && logEntry[logMsgFuncLength] == '>') {
			break;
		}
		logMsgFuncLength++;
	}
	logMsgFuncLength++; // Skip '>' character

	std::int32_t length2 = 0;
	if (logMsgFuncLength > 0) {
		if (hasVirtualTerminal) {
			length2 += nCine::copyStringFirst(logEntryWithColors, MaxEntryLength - 1, Faint, countof(Faint) - 1);
			if (level == TraceLevel::Error || level == TraceLevel::Fatal) {
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightRed, countof(BrightRed) - 1);
			} else if (level == TraceLevel::Warning) {
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightYellow, countof(BrightYellow) - 1);
			}
#	if defined(DEATH_TARGET_EMSCRIPTEN)
			else if (level == TraceLevel::Debug) {
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, DarkGray, countof(DarkGray) - 1);
			}
#	endif
		}

		length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, logEntry, logMsgFuncLength);
	}

	if (hasVirtualTerminal) {
#	if defined(DEATH_TARGET_EMSCRIPTEN)
		if (level != TraceLevel::Warning && level != TraceLevel::Debug) {
#	else
		if (level != TraceLevel::Debug) {
#	endif
			length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, Reset, countof(Reset) - 1);
		}
		if (level == TraceLevel::Error || level == TraceLevel::Fatal) {
			length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightRed, countof(BrightRed) - 1);
			if (level == TraceLevel::Fatal) {
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, Bold, countof(Bold) - 1);
			}
		}
#	if defined(DEATH_TARGET_EMSCRIPTEN)
		else if (level == TraceLevel::Info || level == TraceLevel::Warning) {
			length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, Bold, countof(Bold) - 1);
		}
#	else
		else if (level == TraceLevel::Warning) {
			length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightYellow, countof(BrightYellow) - 1);
		}
#	endif
	}

	length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, logEntry + logMsgFuncLength, length - logMsgFuncLength);

	if (hasVirtualTerminal) {
		if (level == TraceLevel::Debug || level == TraceLevel::Warning || level == TraceLevel::Error || level == TraceLevel::Fatal) {
			length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, Reset, countof(Reset) - 1);
		}
	}

	if (length2 >= MaxEntryLength - 2) {
		length2 = MaxEntryLength - 2;
	}

	logEntryWithColors[length2++] = '\n';
	logEntryWithColors[length2] = '\0';

	if (level == TraceLevel::Error || level == TraceLevel::Fatal) {
		fputs(logEntryWithColors, stderr);
	} else {
		fputs(logEntryWithColors, stdout);
	}

#	if defined(DEATH_TARGET_WINDOWS)
	} else {
#		if defined(DEATH_DEBUG)
		logEntry[0] = '[';
		switch (level) {
			case TraceLevel::Fatal:		logEntry[1] = 'F'; break;
			case TraceLevel::Error:		logEntry[1] = 'E'; break;
			case TraceLevel::Warning:	logEntry[1] = 'W'; break;
			case TraceLevel::Info:		logEntry[1] = 'I'; break;
			default:					logEntry[1] = 'D'; break;
		}
		logEntry[2] = ']';
		logEntry[3] = ' ';

		va_list args;
		va_start(args, fmt);
		std::int32_t length = vsnprintf(logEntry + 4, MaxEntryLength - 4, fmt, args) + 4;
		va_end(args);

		if (length >= MaxEntryLength - 2) {
			length = MaxEntryLength - 2;
		}

		logEntry[length++] = '\n';
		logEntry[length] = '\0';

		wchar_t logEntryW[MaxEntryLength];
		Death::Utf8::ToUtf16(logEntryW, logEntry, length);
		::OutputDebugString(logEntryW);
#		endif
	}
#	endif
#endif

#if defined(WITH_TRACY)
	uint32_t colorTracy;
	// clang-format off
	switch (level) {
		case TraceLevel::Fatal:		colorTracy = 0xEC3E40; break;
		case TraceLevel::Error:		colorTracy = 0xD85050; break;
		case TraceLevel::Warning:	colorTracy = 0xEBC77A; break;
		case TraceLevel::Info:		colorTracy = 0xD2D2D2; break;
		default:					colorTracy = 0x969696; break;
	}
	// clang-format on

	va_list argsTracy;
	va_start(argsTracy, fmt);
	std::int32_t lengthTracy = vsnprintf(logEntry, MaxEntryLength, fmt, argsTracy);
	va_end(argsTracy);

	TracyMessageC(logEntry, lengthTracy, colorTracy);
#endif
}

#endif

namespace nCine
{
	Application::Application()
		: isSuspended_(false), autoSuspension_(false), hasFocus_(true), shouldQuit_(false)
	{
	}

	Application::~Application() = default;

#if defined(WITH_IMGUI)
	Application::GuiSettings::GuiSettings()
		: imguiLayer(0xffff - 1024), imguiViewport(nullptr)
	{
	}
#endif

	Viewport& Application::screenViewport()
	{
		return *screenViewport_;
	}

	unsigned long int Application::numFrames() const
	{
		return frameTimer_->totalNumberFrames();
	}

	float Application::timeMult() const
	{
		return frameTimer_->timeMult();
	}

	const FrameTimer& Application::frameTimer() const
	{
		return *frameTimer_;
	}

	void Application::resizeScreenViewport(int width, int height)
	{
		if (screenViewport_ != nullptr) {
			bool sizeChanged = (width != screenViewport_->width_ || height != screenViewport_->height_);
			screenViewport_->resize(width, height);
			if (sizeChanged && width > 0 && height > 0) {
				appEventHandler_->OnResizeWindow(width, height);
			}
		}
	}

	void Application::initCommon()
	{
		TracyGpuContext;
		ZoneScopedC(0x81A861);
		// This timestamp is needed to initialize random number generator
		profileStartTime_ = TimeStamp::now();

		LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing...");
#if defined(WITH_TRACY)
		TracyAppInfo(NCINE_APP, sizeof(NCINE_APP) - 1);
		LOGW("Tracy integration is enabled");
#endif

		theServiceLocator().registerIndexer(std::make_unique<ArrayIndexer>());
#if defined(WITH_AUDIO)
		if (appCfg_.withAudio) {
			theServiceLocator().registerAudioDevice(std::make_unique<ALAudioDevice>());
		}
#endif
#if defined(WITH_THREADS)
		if (appCfg_.withThreads) {
			theServiceLocator().registerThreadPool(std::make_unique<ThreadPool>());
		}
#endif

		theServiceLocator().registerGfxCapabilities(std::make_unique<GfxCapabilities>());
		const auto& gfxCapabilities = theServiceLocator().gfxCapabilities();
		GLDebug::init(gfxCapabilities);

#if defined(DEATH_TARGET_ANDROID) && !(defined(WITH_FIXED_BATCH_SIZE) && WITH_FIXED_BATCH_SIZE > 0)
		const StringView vendor = gfxCapabilities.glInfoStrings().vendor;
		const StringView renderer = gfxCapabilities.glInfoStrings().renderer;
		// Some GPUs doesn't work with dynamic batch size, so disable it for now
		if (vendor == "Imagination Technologies"_s && (renderer == "PowerVR Rogue GE8300"_s || renderer == "PowerVR Rogue GE8320"_s)) {
			const StringView vendorPrefix = vendor.findOr(' ', vendor.end());
			if (renderer.hasPrefix(vendor.prefix(vendorPrefix.begin()))) {
				LOGW("Detected %s: Using fixed batch size", renderer.data());
			} else {
				LOGW("Detected %s %s: Using fixed batch size", vendor.data(), renderer.data());
			}
			appCfg_.fixedBatchSize = 10;
		}
#endif

#if defined(WITH_RENDERDOC)
		RenderDocCapture::init();
#endif

		// Swapping frame now for a cleaner API trace capture when debugging
		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;

		frameTimer_ = std::make_unique<FrameTimer>(appCfg_.frameTimerLogInterval, 0.2f);
#if defined(DEATH_TARGET_WINDOWS)
		_waitableTimer = ::CreateWaitableTimer(NULL, TRUE, NULL);
#endif

		LOGI("Creating rendering resources...");

		// Create a minimal set of render resources before compiling the first shader
		RenderResources::createMinimal(); // they are required for rendering even without a scenegraph
	
		if (appCfg_.withScenegraph) {
			gfxDevice_->setupGL();
			RenderResources::create();
			rootNode_ = std::make_unique<SceneNode>();
			screenViewport_ = std::make_unique<ScreenViewport>();
			screenViewport_->setRootNode(rootNode_.get());
		}

#if defined(WITH_IMGUI)
		imguiDrawing_ = std::make_unique<ImGuiDrawing>(appCfg_.withScenegraph);

		// Debug overlay is available even when scenegraph is not
		if (appCfg_.withDebugOverlay) {
			debugOverlay_ = std::make_unique<ImGuiDebugOverlay>(0.5f);	// 2 updates per second
		}
#endif

		// Initialization of the static random generator seeds
		Random().Initialize(static_cast<uint64_t>(TimeStamp::now().ticks()), static_cast<uint64_t>(profileStartTime_.ticks()));

		LOGI("Application initialized");
#if defined(NCINE_PROFILING)
		timings_[(int)Timings::InitCommon] = profileStartTime_.secondsSince();
#endif
		{
			ZoneScopedNC("onInit", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnInit();
#if defined(NCINE_PROFILING)
			timings_[(int)Timings::AppInit] = profileStartTime_.secondsSince();
#endif
			LOGI("IAppEventHandler::OnInit() invoked");
		}

#if defined(WITH_IMGUI)
		imguiDrawing_->buildFonts();
#endif

		// Swapping frame now for a cleaner API trace capture when debugging
		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;
	}

	void Application::step()
	{
		frameTimer_->addFrame();

#if defined(WITH_IMGUI)
		{
			ZoneScopedN("ImGui newFrame");
#	if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#	endif
			imguiDrawing_->newFrame();
#	if defined(NCINE_PROFILING)
			timings_[(int)Timings::ImGui] = profileStartTime_.secondsSince();
#	endif
		}
#endif
#if defined(WITH_LUA)
		LuaStatistics::update();
#endif

		{
			ZoneScopedNC("OnFrameStart", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnFrameStart();
#if defined(NCINE_PROFILING)
			timings_[(int)Timings::FrameStart] = profileStartTime_.secondsSince();
#endif
		}

#if defined(WITH_IMGUI)
		if (debugOverlay_ != nullptr) {
			debugOverlay_->update();
		}
#endif

		if (appCfg_.withScenegraph) {
			ZoneScopedNC("SceneGraph", 0x81A861);
			{
				ZoneScopedNC("Update", 0x81A861);
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				screenViewport_->update();
#if defined(NCINE_PROFILING)
				timings_[(int)Timings::Update] = profileStartTime_.secondsSince();
#endif
			}

			{
				ZoneScopedNC("OnPostUpdate", 0x81A861);
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				appEventHandler_->OnPostUpdate();
#if defined(NCINE_PROFILING)
				timings_[(int)Timings::PostUpdate] = profileStartTime_.secondsSince();
#endif
			}

			{
				ZoneScopedNC("Visit", 0x81A861);
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				screenViewport_->visit();
#if defined(NCINE_PROFILING)
				timings_[(int)Timings::Visit] = profileStartTime_.secondsSince();
#endif
			}

#if defined(WITH_IMGUI)
			{
				ZoneScopedN("ImGui endFrame");
#	if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#	endif
				RenderQueue* imguiRenderQueue = (guiSettings_.imguiViewport ? guiSettings_.imguiViewport->renderQueue_.get() : screenViewport_->renderQueue_.get());
				imguiDrawing_->endFrame(*imguiRenderQueue);
#	if defined(NCINE_PROFILING)
				timings_[(int)Timings::ImGui] += profileStartTime_.secondsSince();
#	endif
			}
#endif

			{
				ZoneScopedNC("Draw", 0x81A861);
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				screenViewport_->sortAndCommitQueue();
				screenViewport_->draw();
#if defined(NCINE_PROFILING)
				timings_[(int)Timings::Draw] = profileStartTime_.secondsSince();
#endif
			}
		} else {
#if defined(WITH_IMGUI)
			{
				ZoneScopedN("ImGui endFrame");
#	if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#	endif
				imguiDrawing_->endFrame();
#	if defined(NCINE_PROFILING)
				timings_[(int)Timings::ImGui] += profileStartTime_.secondsSince();
#	endif
			}
#endif
		}

		{
			theServiceLocator().audioDevice().updatePlayers();
		}

		{
			ZoneScopedNC("OnFrameEnd", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnFrameEnd();
#if defined(NCINE_PROFILING)
			timings_[(int)Timings::FrameEnd] = profileStartTime_.secondsSince();
#endif
		}

#if defined(WITH_IMGUI)
		if (debugOverlay_ != nullptr) {
			debugOverlay_->updateFrameTimings();
		}
#endif

		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;

		if (appCfg_.frameLimit > 0) {
			FrameMarkStart("Frame limiting");
#if defined(DEATH_TARGET_WINDOWS)
			const std::uint64_t clockFreq = static_cast<std::uint64_t>(clock().frequency());
			const std::uint64_t frameTimeDuration = (clockFreq / static_cast<std::uint64_t>(appCfg_.frameLimit));
			const std::int64_t remainingTime = (std::int64_t)frameTimeDuration - (std::int64_t)frameTimer_->frameDurationAsTicks();
			if (remainingTime > 0) {
				LARGE_INTEGER dueTime;
				dueTime.QuadPart = -(LONGLONG)((10000000ULL * remainingTime) / clockFreq);

				::SetWaitableTimer(_waitableTimer, &dueTime, 0, 0, 0, FALSE);
				::WaitForSingleObject(_waitableTimer, 1000);
				::CancelWaitableTimer(_waitableTimer);
			}
#else
			const float frameDuration = 1.0f / static_cast<float>(appCfg_.frameLimit);
			while (frameTimer_->frameDuration() < frameDuration) {
				Timer::sleep(0);
			}
#endif
			FrameMarkEnd("Frame limiting");
		}
	}

	void Application::shutdownCommon()
	{
		ZoneScopedC(0x81A861);
		appEventHandler_->OnShutdown();
		LOGI("IAppEventHandler::OnShutdown() invoked");
		appEventHandler_.reset();

#if defined(WITH_IMGUI)
		imguiDrawing_.reset(nullptr);
		debugOverlay_.reset(nullptr);
#endif
#if defined(WITH_RENDERDOC)
		RenderDocCapture::removeHooks();
#endif

		rootNode_.reset();
		RenderResources::dispose();
		frameTimer_.reset();
		inputManager_.reset();
		gfxDevice_.reset();

#if defined(DEATH_TARGET_WINDOWS)
		::CloseHandle(_waitableTimer);
#endif

		if (!theServiceLocator().indexer().empty()) {
			LOGW("The object indexer is not empty, %u object(s) left", theServiceLocator().indexer().size());
			//theServiceLocator().indexer().logReport();
		}

		LOGI("Application shut down");

		theServiceLocator().unregisterAll();
	}

	void Application::setFocus(bool hasFocus)
	{
#if defined(WITH_TRACY) && !defined(DEATH_TARGET_ANDROID)
		hasFocus = true;
#endif

		hasFocus_ = hasFocus;
	}

	void Application::suspend()
	{
		frameTimer_->suspend();
		if (appEventHandler_ != nullptr) {
			appEventHandler_->OnSuspend();
		}
		LOGI("IAppEventHandler::OnSuspend() invoked");
	}

	void Application::resume()
	{
		if (appEventHandler_ != nullptr) {
			appEventHandler_->OnResume();
		}
		const TimeStamp suspensionDuration = frameTimer_->resume();
		LOGD("Suspended for %.3f seconds", suspensionDuration.seconds());
#if defined(NCINE_PROFILING)
		profileStartTime_ += suspensionDuration;
#endif
		LOGI("IAppEventHandler::OnResume() invoked");
	}

	bool Application::shouldSuspend()
	{
		return ((!hasFocus_ && autoSuspension_) || isSuspended_);
	}
}
