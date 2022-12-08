#include "../Common.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	pragma comment(lib, "opengl32.lib")
#	if defined(_M_X64)
#		ifdef WITH_GLEW
#			pragma comment(lib, "../Libs/Windows/x64/glew32.lib")
#		endif
#		ifdef WITH_GLFW
#			pragma comment(lib, "../Libs/Windows/x64/glfw3dll.lib")
#		endif
#		ifdef WITH_SDL
#			pragma comment(lib, "../Libs/Windows/x64/SDL2.lib")
#		endif
#		ifdef WITH_AUDIO
#			pragma comment(lib, "../Libs/Windows/x64/OpenAL32.lib")
#		endif
#		pragma comment(lib, "../Libs/Windows/x64/libdeflate.lib")
#	elif defined(_M_IX86)
#		ifdef WITH_GLEW
#			pragma comment(lib, "../Libs/Windows/x86/glew32.lib")
#		endif
#		ifdef WITH_GLFW
#			pragma comment(lib, "../Libs/Windows/x86/glfw3dll.lib")
#		endif
#		ifdef WITH_SDL
#			pragma comment(lib, "../Libs/Windows/x86/SDL2.lib")
#		endif
#		ifdef WITH_AUDIO
#			pragma comment(lib, "../Libs/Windows/x86/OpenAL32.lib")
#		endif
#		pragma comment(lib, "../Libs/Windows/x86/libdeflate.lib")
#	else
#		error Unsupported architecture
#	endif

extern "C"
{
	_declspec(dllexport) unsigned long int NvOptimusEnablement = 0x00000001;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
};

#endif

#include "Application.h"
#include "Base/Random.h"
#include "IAppEventHandler.h"
#include "IO/FileSystem.h"
#include "ArrayIndexer.h"
#include "Graphics/GfxCapabilities.h"
#include "Graphics/RenderResources.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/ScreenViewport.h"
#include "Graphics/GL/GLDebug.h"
#include "Base/Timer.h" // for `sleep()`
#include "Base/FrameTimer.h"
#include "Graphics/SceneNode.h"
#include "Input/IInputManager.h"
#include "Input/JoyMapping.h"
#include "ServiceLocator.h"
#include "tracy.h"
#include "tracy_opengl.h"

#if defined(WITH_AUDIO)
#	include "Audio/ALAudioDevice.h"
#endif

#if defined(WITH_THREADS)
#	include "Threading/ThreadPool.h"
#endif

#if defined(WITH_LUA)
#	include "LuaStatistics.h"
#endif

#if defined(WITH_RENDERDOC)
#	include "Graphics/RenderDocCapture.h"
#endif

#if defined(NCINE_LOG)

#if defined(DEATH_TARGET_WINDOWS)
#	include <Utf8.h>
#elif defined(DEATH_TARGET_ANDROID)
#	include <stdarg.h>
#	include <android/log.h>
extern std::unique_ptr<nCine::IFileStream> __logFile;
#else
#	include <cstdarg>
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
extern bool showLogConsole_;
#endif

void __WriteLog(LogLevel level, const char* fmt, ...)
{
	constexpr int MaxEntryLength = 4 * 1024;
	char logEntry[MaxEntryLength];

#if defined(DEATH_TARGET_WINDOWS)
#	if defined(NCINE_DEBUG) || defined(DEATH_TARGET_WINDOWS_RT)
	// Use `OutputDebugString` for debug builds or if console cannot be created (WinRT)
	logEntry[0] = '[';
	switch (level) {
		case LogLevel::Fatal:	logEntry[1] = 'F'; break;
		case LogLevel::Error:	logEntry[1] = 'E'; break;
		case LogLevel::Warning:	logEntry[1] = 'W'; break;
		case LogLevel::Info:	logEntry[1] = 'I'; break;
		default:				logEntry[1] = 'D'; break;
	}
	logEntry[2] = ']';
	logEntry[3] = ' ';

	va_list args;
	va_start(args, fmt);
	unsigned int length = vsnprintf(logEntry + 4, MaxEntryLength - 4, fmt, args) + 4;
	va_end(args);

	if (length >= MaxEntryLength - 2) {
		length = MaxEntryLength - 2;
	}

	logEntry[length++] = '\n';
	logEntry[length] = '\0';

	::OutputDebugString(Death::Utf8::ToUtf16(logEntry));
#	else
	// If `/log` parameter was specified, write log to console window
	if (showLogConsole_) {
		constexpr char Reset[] = "\033[0m";
		constexpr char Bold[] = "\033[1m";
		constexpr char Faint[] = "\033[90m"; // Use dark color instead of `Faint` on Windows
		constexpr char Red[] = "\033[31m";
		constexpr char Yellow[] = "\033[33m";
		constexpr char BrightRed[] = "\033[91m";
		constexpr char BrightYellow[] = "\033[93m";

		char logEntryWithColors[MaxEntryLength];
		logEntryWithColors[0] = '\0';
		logEntryWithColors[MaxEntryLength - 1] = '\0';

		va_list args;
		va_start(args, fmt);
		unsigned int length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
		va_end(args);

		// Colorize the output
		unsigned int length2 = snprintf(logEntryWithColors, MaxEntryLength - 1, "%s", Faint);
		if (level == LogLevel::Error || level == LogLevel::Fatal) {
			length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Red);
		} else if (level == LogLevel::Warning) {
			length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Yellow);
		}

		unsigned int logMsgFuncLength = 0;
		while (logEntry[logMsgFuncLength] != '\0' && (logMsgFuncLength == 0 || !(logEntry[logMsgFuncLength - 1] == '-' && logEntry[logMsgFuncLength] == '>'))) {
			logMsgFuncLength++;
		}
		logMsgFuncLength++; // Skip '>' character

		strncpy_s(logEntryWithColors + length2, MaxEntryLength - length2 - 1, logEntry, logMsgFuncLength);
		length2 += logMsgFuncLength;

		if (level != LogLevel::Verbose) {
			length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Reset);
		}
		if (level == LogLevel::Error || level == LogLevel::Fatal) {
			length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", BrightRed);
			if (level == LogLevel::Fatal) {
				length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Bold);
			}
		} else if (level == LogLevel::Warning) {
			length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", BrightYellow);
		}

		strncpy_s(logEntryWithColors + length2, MaxEntryLength - length2 - 1, logEntry + logMsgFuncLength, length - logMsgFuncLength);
		length2 += length - logMsgFuncLength;

		if (level == LogLevel::Verbose || level == LogLevel::Warning || level == LogLevel::Error || level == LogLevel::Fatal) {
			length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Reset);
		}

		if (length2 >= MaxEntryLength - 2) {
			length2 = MaxEntryLength - 2;
		}

		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';

		if (level == LogLevel::Error || level == LogLevel::Fatal) {
			fputs(logEntryWithColors, stderr);
		} else {
			fputs(logEntryWithColors, stdout);
		}
	}
#	endif
#elif defined(DEATH_TARGET_ANDROID)
	android_LogPriority priority;

	// clang-format off
	switch (level) {
		case LogLevel::Fatal:	priority = ANDROID_LOG_FATAL; break;
		case LogLevel::Error:	priority = ANDROID_LOG_ERROR; break;
		case LogLevel::Warning:	priority = ANDROID_LOG_WARN; break;
		case LogLevel::Info:	priority = ANDROID_LOG_INFO; break;
		default:				priority = ANDROID_LOG_DEBUG; break;
	}
	// clang-format on

	va_list args;
	va_start(args, fmt);
	/*unsigned int length =*/ vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	__android_log_write(priority, "jazz2", logEntry);

	if (__logFile != nullptr) {
		const char* levelIdentifier;
		switch (level) {
			case LogLevel::Fatal:	levelIdentifier = "F"; break;
			case LogLevel::Error:	levelIdentifier = "E"; break;
			case LogLevel::Warning:	levelIdentifier = "W"; break;
			case LogLevel::Info:	levelIdentifier = "I"; break;
			default:				levelIdentifier = "D"; break;
		}

		fprintf(__logFile->Ptr(), "[%s] %s\n", levelIdentifier, logEntry);
		fflush(__logFile->Ptr());
	}
#else
	constexpr char Reset[] = "\033[0m";
	constexpr char Bold[] = "\033[1m";
	constexpr char Faint[] = "\033[2m";
	constexpr char Red[] = "\033[31m";
	constexpr char Yellow[] = "\033[33m";
	constexpr char BrightRed[] = "\033[91m";
	constexpr char BrightYellow[] = "\033[93m";

	char logEntryWithColors[MaxEntryLength];
	logEntryWithColors[0] = '\0';
	logEntryWithColors[MaxEntryLength - 1] = '\0';

	va_list args;
	va_start(args, fmt);
	unsigned int length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	// Colorize the output
	unsigned int length2 = snprintf(logEntryWithColors, MaxEntryLength - 1, "%s", Faint);
	if (level == LogLevel::Error || level == LogLevel::Fatal) {
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Red);
	}
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
	else if (level == LogLevel::Warning) {
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Yellow);
	}
#	endif

	unsigned int logMsgFuncLength = 0;
	while (logEntry[logMsgFuncLength] != '\0' && (logMsgFuncLength == 0 || !(logEntry[logMsgFuncLength - 1] == '-' && logEntry[logMsgFuncLength] == '>'))) {
		logMsgFuncLength++;
	}
	logMsgFuncLength++; // Skip '>' character

	strncpy(logEntryWithColors + length2, logEntry, std::min(logMsgFuncLength, MaxEntryLength - length2 - 1));
	length2 += logMsgFuncLength;

	if (level != LogLevel::Verbose) {
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Reset);
	}
	if (level == LogLevel::Error || level == LogLevel::Fatal) {
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", BrightRed);
		if (level == LogLevel::Fatal) {
			length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Bold);
		}
	} else if (level == LogLevel::Warning) {
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", BrightYellow);
#	else
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Bold);
#	endif
	}

	strncpy(logEntryWithColors + length2, logEntry + logMsgFuncLength, std::min(length - logMsgFuncLength, MaxEntryLength - length2 - 1));
	length2 += length - logMsgFuncLength;

	if (level == LogLevel::Verbose || level == LogLevel::Warning || level == LogLevel::Error || level == LogLevel::Fatal) {
		length2 += snprintf(logEntryWithColors + length2, MaxEntryLength - length2 - 1, "%s", Reset);
	}

	if (length2 >= MaxEntryLength - 2) {
		length2 = MaxEntryLength - 2;
	}

	logEntryWithColors[length2++] = '\n';
	logEntryWithColors[length2] = '\0';

	if (level == LogLevel::Error || level == LogLevel::Fatal) {
		fputs(logEntryWithColors, stderr);
	} else {
		fputs(logEntryWithColors, stdout);
	}
#endif

#if defined(WITH_TRACY)
	uint32_t colorTracy;
	// clang-format off
	switch (level) {
		case LogLevel::Fatal:	colorTracy = 0xec3e40; break;
		case LogLevel::Error:	colorTracy = 0xff9b2b; break;
		case LogLevel::Warning:	colorTracy = 0xf5d800; break;
		case LogLevel::Info:	colorTracy = 0x01a46d; break;
		default:				colorTracy = 0x377fc7; break;
	}
	// clang-format on

	va_list argsTracy;
	va_start(argsTracy, fmt);
	unsigned int lengthTracy = vsnprintf(logEntry, MaxEntryLength, fmt, argsTracy);
	va_end(argsTracy);

	TracyMessageC(logEntry, lengthTracy, colorTracy);
#endif
}

#endif

namespace nCine
{
	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	Application::Application()
		: isSuspended_(false), autoSuspension_(true), hasFocus_(true), shouldQuit_(false)
	{
	}

	Application::~Application() = default;

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	Viewport& Application::screenViewport()
	{
		return *screenViewport_;
	}

	unsigned long int Application::numFrames() const
	{
		return frameTimer_->totalNumberFrames();
	}

	float Application::averageFps() const
	{
		return frameTimer_->averageFps();
	}

	float Application::timeMult() const
	{
		return frameTimer_->timeMult();
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

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	void Application::initCommon()
	{
		TracyGpuContext;
		ZoneScoped;
		// This timestamp is needed to initialize random number generator
		profileStartTime_ = TimeStamp::now();

		LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing...");
#if defined(WITH_TRACY)
		TracyAppInfo(NCINE_APP, sizeof(NCINE_APP) - 1);
		LOGW("Tracy integration is enabled");
#endif

		renderingSettings_.windowScaling = appCfg_.windowScaling;

		//LOGI_X("Data path: \"%s\"", fs::GetDataPath().data());

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
		if (vendor == "Imagination Technologies"_s && renderer == "PowerVR Rogue GE8300"_s) {
			const StringView vendorPrefix = vendor.findOr(' ', vendor.end());
			if (renderer.hasPrefix(vendor.prefix(vendorPrefix.begin()))) {
				LOGW_X("Detected %s: Using fixed batch size", renderer.data());
			} else {
				LOGW_X("Detected %s %s: Using fixed batch size", vendor.data(), renderer.data());
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

		frameTimer_ = std::make_unique<FrameTimer>(appCfg_.frameTimerLogInterval, appCfg_.profileTextUpdateTime());

		if (appCfg_.withScenegraph) {
			gfxDevice_->setupGL();
			RenderResources::create();
			rootNode_ = std::make_unique<SceneNode>();
			screenViewport_ = std::make_unique<ScreenViewport>();
			screenViewport_->setRootNode(rootNode_.get());
		} else {
			RenderResources::createMinimal(); // some resources are still required for rendering
		}

		// Initialization of the static random generator seeds
		Random().Initialize(static_cast<uint64_t>(TimeStamp::now().ticks()), static_cast<uint64_t>(profileStartTime_.ticks()));

		LOGI("Application initialized");
#if defined(NCINE_PROFILING)
		timings_[(int)Timings::InitCommon] = profileStartTime_.secondsSince();
#endif
		{
			ZoneScopedN("onInit");
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnInit();
#if defined(NCINE_PROFILING)
			timings_[(int)Timings::AppInit] = profileStartTime_.secondsSince();
#endif
			LOGI("IAppEventHandler::OnInit() invoked");
		}

		// Swapping frame now for a cleaner API trace capture when debugging
		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;
	}

	void Application::step()
	{
		frameTimer_->addFrame();

#if defined(WITH_LUA)
		LuaStatistics::update();
#endif

		{
			ZoneScopedN("OnFrameStart");
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnFrameStart();
#if defined(NCINE_PROFILING)
			timings_[(int)Timings::FrameStart] = profileStartTime_.secondsSince();
#endif
		}

		if (appCfg_.withScenegraph) {
			ZoneScopedN("SceneGraph");
			{
				ZoneScopedN("Update");
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				screenViewport_->update();
#if defined(NCINE_PROFILING)
				timings_[(int)Timings::Update] = profileStartTime_.secondsSince();
#endif
			}

			{
				ZoneScopedN("OnPostUpdate");
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				appEventHandler_->OnPostUpdate();
#if defined(NCINE_PROFILING)
				timings_[(int)Timings::PostUpdate] = profileStartTime_.secondsSince();
#endif
			}

			{
				ZoneScopedN("Visit");
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				screenViewport_->visit();
#if defined(NCINE_PROFILING)
				timings_[(int)Timings::Visit] = profileStartTime_.secondsSince();
#endif
			}

			{
				ZoneScopedN("Draw");
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				screenViewport_->sortAndCommitQueue();
				screenViewport_->draw();
#if defined(NCINE_PROFILING)
				timings_[(int)Timings::Draw] = profileStartTime_.secondsSince();
#endif
			}
		}

		{
			theServiceLocator().audioDevice().updatePlayers();
		}

		{
			ZoneScopedN("OnFrameEnd");
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnFrameEnd();
#if defined(NCINE_PROFILING)
			timings_[(int)Timings::FrameEnd] = profileStartTime_.secondsSince();
#endif
		}

		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;

		if (appCfg_.frameLimit > 0) {
			const float frameTimeDuration = 1.0f / static_cast<float>(appCfg_.frameLimit);
			while (frameTimer_->frameInterval() < frameTimeDuration) {
				Timer::sleep(0.0f);
			}
		}
	}

	void Application::shutdownCommon()
	{
		ZoneScoped;
		appEventHandler_->OnShutdown();
		LOGI("IAppEventHandler::OnShutdown() invoked");
		appEventHandler_.reset(nullptr);

#if defined(WITH_RENDERDOC)
		RenderDocCapture::removeHooks();
#endif

		rootNode_.reset(nullptr);
		RenderResources::dispose();
		frameTimer_.reset(nullptr);
		inputManager_.reset(nullptr);
		gfxDevice_.reset(nullptr);

		if (!theServiceLocator().indexer().empty()) {
			LOGW_X("The object indexer is not empty, %u object(s) left", theServiceLocator().indexer().size());
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
		LOGV_X("Suspended for %.3f seconds", suspensionDuration.seconds());
#if defined(NCINE_PROFILING)
		profileStartTime_ += suspensionDuration;
#endif
		LOGI("IAppEventHandler::OnResume() invoked");
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	bool Application::shouldSuspend()
	{
		return ((!hasFocus_ && autoSuspension_) || isSuspended_);
	}
}
