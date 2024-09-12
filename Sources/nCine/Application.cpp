#include "Application.h"

#if defined(DEATH_TARGET_WINDOWS)
extern "C"
{
	_declspec(dllexport) unsigned long int NvOptimusEnablement = 0x00000001;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;
};
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	pragma comment(lib, "opengl32.lib")
#	pragma comment(lib, "winmm.lib")
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
#	else
#		error Unsupported architecture
#	endif
#endif

#include "Base/Algorithms.h"
#include "Base/Random.h"
#include "IAppEventHandler.h"
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

#if defined(WITH_BACKWARD)
#	if !defined(CMAKE_BUILD)
#		include "../backward/backward.h"
#	else
#		include <backward.h>
#	endif
backward::SignalHandling sh;
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

#if defined(DEATH_TRACE)

#if defined(DEATH_TARGET_WINDOWS)
#	include <Utf8.h>
#elif defined(DEATH_TARGET_ANDROID)
#	include <stdarg.h>
#	include <time.h>
#	include <unistd.h>
#	include <android/log.h>
#else
#	include <cstdarg>
#	if defined(DEATH_TARGET_SWITCH)
#		include <time.h>
#		include <switch.h>
#	endif
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
#	include <IO/FileStream.h>
std::unique_ptr<Death::IO::Stream> __logFile;
#endif
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
extern HANDLE __consoleHandleOut;
extern SHORT __consoleCursorY;
extern bool __showLogConsole;
#endif
#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT))
extern bool __hasVirtualTerminal;
#endif

struct TraceDateTime {
	std::uint32_t Milliseconds;
	std::uint32_t Seconds;
	std::uint32_t Minutes;
	std::uint32_t Hours;
	std::uint32_t Days;
	std::uint32_t Months;
	std::uint32_t Years;
};

static TraceDateTime GetTraceDateTime()
{
	TraceDateTime result;

#if defined(DEATH_TARGET_WINDOWS)
	SYSTEMTIME systemTime;
	::GetLocalTime(&systemTime);

	result.Milliseconds = systemTime.wMilliseconds;
	result.Seconds = systemTime.wSecond;
	result.Minutes = systemTime.wMinute;
	result.Hours = systemTime.wHour;
	result.Days = systemTime.wDay;
	result.Months = systemTime.wMonth;
	result.Years = systemTime.wYear;
#else
	struct timespec currentTime;
	clock_gettime(CLOCK_REALTIME, &currentTime);

	time_t seconds = currentTime.tv_sec;
	struct tm localTime;
	localtime_r(&seconds, &localTime);

	result.Milliseconds = (currentTime.tv_nsec / 1000000) % 1000;
	result.Seconds = localTime.tm_sec;
	result.Minutes = localTime.tm_min;
	result.Hours = localTime.tm_hour;
	result.Days = localTime.tm_mday;
	result.Months = localTime.tm_mon;
	result.Years = localTime.tm_year;
#endif

	return result;
}

void DEATH_TRACE(TraceLevel level, const char* fmt, ...)
{
	constexpr std::int32_t MaxEntryLength = 4096;
	char logEntry[MaxEntryLength];
	char logEntryWithColors[MaxEntryLength + 24];
	const char* logEntryWithoutLevel = logEntry;
	std::int32_t length;

#if defined(DEATH_TARGET_ANDROID)
	android_LogPriority priority;
	switch (level) {
		case TraceLevel::Fatal:		priority = ANDROID_LOG_FATAL; break;
		case TraceLevel::Assert:	// Android doesn't support this priority, use ANDROID_LOG_ERROR instead
		case TraceLevel::Error:		priority = ANDROID_LOG_ERROR; break;
		case TraceLevel::Warning:	priority = ANDROID_LOG_WARN; break;
		case TraceLevel::Info:		priority = ANDROID_LOG_INFO; break;
		default:					priority = ANDROID_LOG_DEBUG; break;
	}

	va_list args;
	va_start(args, fmt);
	length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	std::int32_t result = __android_log_write(priority, NCINE_APP, logEntry);
	std::int32_t n = 0;
	while (result == -11 /*EAGAIN*/ && n < 2) {
		::usleep(5000); // in microseconds
		result = __android_log_write(priority, NCINE_APP, logEntry);
		n++;
	}
#elif defined(DEATH_TARGET_SWITCH)
	va_list args;
	va_start(args, fmt);
	length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	svcOutputDebugString(logEntry, length);
#elif defined(DEATH_TARGET_WINDOWS_RT)
	char levelIdentifier;
	switch (level) {
		case TraceLevel::Fatal:		levelIdentifier = 'F'; break;
		case TraceLevel::Assert:	levelIdentifier = 'A'; break;
		case TraceLevel::Error:		levelIdentifier = 'E'; break;
		case TraceLevel::Warning:	levelIdentifier = 'W'; break;
		case TraceLevel::Info:		levelIdentifier = 'I'; break;
		default:					levelIdentifier = 'D'; break;
	}

#	if defined(WITH_THREADS)
	length = snprintf(logEntry, MaxEntryLength, "[%c]%u} ", levelIdentifier, static_cast<std::uint32_t>(nCine::Thread::GetCurrentId()));
#	else
	length = snprintf(logEntry, MaxEntryLength, "[%c] ", levelIdentifier);
#	endif
	logEntryWithoutLevel += length;

	va_list args;
	va_start(args, fmt);
	std::int32_t partLength = vsnprintf(logEntry + length, MaxEntryLength - length, fmt, args);
	va_end(args);
	if (partLength <= 0) {
		return;
	}

	length += partLength;
	if (length >= MaxEntryLength - 2) {
		length = MaxEntryLength - 2;
	}

	// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
	/*wchar_t logEntryW[MaxEntryLength];
	std::int32_t lengthW = Death::Utf8::ToUtf16(logEntryW, logEntry, length);
	if (lengthW > 0) {
		logEntryW[lengthW++] = '\n';
		logEntryW[lengthW] = '\0';
		::OutputDebugStringW(logEntryW);
	}*/
	logEntry[length] = '\n';
	logEntry[length + 1] = '\0';
	::OutputDebugStringA(logEntry);
	logEntry[length] = '\0';
#else
	static const char Reset[] = "\033[0m";
	static const char Bold[] = "\033[1m";
	static const char Faint[] = "\033[2m";
	static const char DarkGray[] = "\033[90m";
	static const char BrightRed[] = "\033[91m";
	static const char BrightYellow[] = "\033[93m";
	static const char BrightMagenta[] = "\033[95m";

#	if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_DEBUG)
	if (__showLogConsole) {
#	endif

	va_list args;
	va_start(args, fmt);
	length = vsnprintf(logEntry, MaxEntryLength, fmt, args);
	va_end(args);

	// Colorize the output
#	if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS)
	const bool hasVirtualTerminal = __hasVirtualTerminal;
#	else
	constexpr bool hasVirtualTerminal = false;
#	endif

	std::int32_t logMsgFuncLength = 1;
	while (true) {
		if (logEntry[logMsgFuncLength] == '\0') {
			logMsgFuncLength = -1;
			break;
		}
		if (logEntry[logMsgFuncLength] == '#' && logEntry[logMsgFuncLength + 1] == '>') {
			logMsgFuncLength += 2;
			break;
		}
		logMsgFuncLength++;
	}

	std::int32_t length2 = 0;
	if (logMsgFuncLength > 0) {
		if (hasVirtualTerminal) {
			length2 += nCine::copyStringFirst(logEntryWithColors, MaxEntryLength - 1, Faint, static_cast<std::int32_t>(arraySize(Faint)) - 1);

			switch (level) {
				case TraceLevel::Error:
				case TraceLevel::Fatal:
					length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightRed, static_cast<std::int32_t>(arraySize(BrightRed)) - 1);
					break;
				case TraceLevel::Assert:
					length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightMagenta, static_cast<std::int32_t>(arraySize(BrightMagenta)) - 1);
					break;
				case TraceLevel::Warning:
					length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightYellow, static_cast<std::int32_t>(arraySize(BrightYellow)) - 1);
					break;
#	if defined(DEATH_TARGET_EMSCRIPTEN)
				case TraceLevel::Debug:
					length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, DarkGray, static_cast<std::int32_t>(arraySize(DarkGray)) - 1);
					break;
#	endif
			}
		}

		length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, logEntry, logMsgFuncLength);
	}

	if (hasVirtualTerminal) {
#	if defined(DEATH_TARGET_EMSCRIPTEN)
		if (level != TraceLevel::Warning && level != TraceLevel::Debug)
#	endif
		{
			length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, Reset, static_cast<std::int32_t>(arraySize(Reset)) - 1);
		}

		switch (level) {
			case TraceLevel::Error:
			case TraceLevel::Fatal:
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightRed, static_cast<std::int32_t>(arraySize(BrightRed)) - 1);
				if (level == TraceLevel::Fatal) {
					length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, Bold, static_cast<std::int32_t>(arraySize(Bold)) - 1);
				}
				break;
			case TraceLevel::Assert:
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightMagenta, static_cast<std::int32_t>(arraySize(BrightMagenta)) - 1);
				break;
#	if defined(DEATH_TARGET_EMSCRIPTEN)
			case TraceLevel::Info:
			case TraceLevel::Warning:
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, Bold, static_cast<std::int32_t>(arraySize(Bold)) - 1);
				break;
#	else
			case TraceLevel::Warning:
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, BrightYellow, static_cast<std::int32_t>(arraySize(BrightYellow)) - 1);
				break;
			case TraceLevel::Debug:
				length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, DarkGray, static_cast<std::int32_t>(arraySize(DarkGray)) - 1);
				break;
#	endif
		}
	}

	length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, logEntry + logMsgFuncLength, length - logMsgFuncLength);

	if (hasVirtualTerminal) {
		if (level == TraceLevel::Debug || level == TraceLevel::Warning || level == TraceLevel::Error || level == TraceLevel::Assert || level == TraceLevel::Fatal) {
			length2 += nCine::copyStringFirst(logEntryWithColors + length2, MaxEntryLength - length2 - 1, Reset, static_cast<std::int32_t>(arraySize(Reset)) - 1);
		}
	}

	if (length2 >= MaxEntryLength - 2) {
		length2 = MaxEntryLength - 2;
	}

	logEntryWithColors[length2++] = '\n';
	logEntryWithColors[length2] = '\0';

#	if defined(DEATH_TARGET_WINDOWS)
	// Try to restore previous cursor position (this doesn't work correctly in Windows Terminal v1.19)
	if (__consoleHandleOut != NULL) {
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
			if (__consoleCursorY <= csbi.dwCursorPosition.Y) {
				::SetConsoleCursorPosition(__consoleHandleOut, { 0, __consoleCursorY });
			}
		}
	}
	if (hasVirtualTerminal && length2 < MaxEntryLength) {
		// Console can be shared with parent process, so clear the rest of the line (using "\x1b[0K" sequence)
		length2--;
		logEntryWithColors[length2++] = '\x1b';
		logEntryWithColors[length2++] = '[';
		logEntryWithColors[length2++] = '0';
		logEntryWithColors[length2++] = 'K';
		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
	}

	fputs(logEntryWithColors, level == TraceLevel::Error || level == TraceLevel::Fatal ? stderr : stdout);

	// Save the last cursor position for later
	if (__consoleHandleOut != NULL) {
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
			__consoleCursorY = csbi.dwCursorPosition.Y;
		}
	}
#	else
	fputs(logEntryWithColors, level == TraceLevel::Error || level == TraceLevel::Fatal ? stderr : stdout);
#	endif

#	if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_DEBUG)
	} else {
		char levelIdentifier;
		switch (level) {
			case TraceLevel::Fatal:		levelIdentifier = 'F'; break;
			case TraceLevel::Assert:	levelIdentifier = 'A'; break;
			case TraceLevel::Error:		levelIdentifier = 'E'; break;
			case TraceLevel::Warning:	levelIdentifier = 'W'; break;
			case TraceLevel::Info:		levelIdentifier = 'I'; break;
			default:					levelIdentifier = 'D'; break;
		}
#		if defined(WITH_THREADS)
		length = snprintf(logEntry, MaxEntryLength, "[%c]%u} ", levelIdentifier, static_cast<std::uint32_t>(nCine::Thread::GetCurrentId()));
#		else
		length = snprintf(logEntry, MaxEntryLength, "[%c] ", levelIdentifier);
#		endif
		logEntryWithoutLevel += length;

		va_list args;
		va_start(args, fmt);
		std::int32_t partLength = vsnprintf(logEntry + length, MaxEntryLength - length, fmt, args);
		va_end(args);
		if (partLength <= 0) {
			return;
		}

		length += partLength;
		if (length >= MaxEntryLength - 2) {
			length = MaxEntryLength - 2;
		}

		// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
		/*wchar_t logEntryW[MaxEntryLength];
		std::int32_t lengthW = Death::Utf8::ToUtf16(logEntryW, logEntry, length);
		if (lengthW > 0) {
			logEntryW[lengthW++] = '\n';
			logEntryW[lengthW] = '\0';
			::OutputDebugStringW(logEntryW);
		}*/
		logEntry[length] = '\n';
		logEntry[length + 1] = '\0';
		::OutputDebugStringA(logEntry);
		logEntry[length] = '\0';
	}
#	endif
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
	// Allow to attach custom target using Application::AttachTraceTarget()
	if (__logFile != nullptr) {
		char levelIdentifier;
		switch (level) {
			case TraceLevel::Fatal:		levelIdentifier = 'F'; break;
			case TraceLevel::Assert:	levelIdentifier = 'A'; break;
			case TraceLevel::Error:		levelIdentifier = 'E'; break;
			case TraceLevel::Warning:	levelIdentifier = 'W'; break;
			case TraceLevel::Info:		levelIdentifier = 'I'; break;
			default:					levelIdentifier = 'D'; break;
		}

		TraceDateTime dateTime = GetTraceDateTime();
		FileStream* s = static_cast<FileStream*>(__logFile.get());

#	if defined(WITH_THREADS)
		std::int32_t length2 = snprintf(logEntryWithColors, MaxEntryLength, "%02u:%02u:%02u.%03u [%c]%u}", dateTime.Hours, dateTime.Minutes,
			dateTime.Seconds, dateTime.Milliseconds, levelIdentifier, static_cast<std::uint32_t>(nCine::Thread::GetCurrentId()));

		while (length2 < 23) {
			logEntryWithColors[length2++] = ' ';
		}
		logEntryWithColors[length2++] = ' ';

		std::int32_t partLength = std::min(length - (std::int32_t)(logEntryWithoutLevel - logEntry), MaxEntryLength - length2 - 2);
		std::memcpy(&logEntryWithColors[length2], logEntryWithoutLevel, partLength);
		length2 += partLength;

		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
		fputs(logEntryWithColors, s->GetHandle());
#	else
		fprintf(s->GetHandle(), "%02u:%02u:%02u.%03u [%c] %s\n", dateTime.Hours, dateTime.Minutes,
			dateTime.Seconds, dateTime.Milliseconds, levelIdentifier, logEntryWithoutLevel);
#	endif
		s->Flush();
	}
#endif

#if defined(WITH_IMGUI)
	auto* debugOverlay = nCine::theApplication().debugOverlay_.get();
	if (debugOverlay != nullptr) {
#	if defined(WITH_THREADS)
		std::uint32_t tid = static_cast<std::uint32_t>(nCine::Thread::GetCurrentId());
#	else
		std::uint32_t tid = 0;
#	endif

		TraceDateTime dateTime = GetTraceDateTime();
		snprintf(logEntryWithColors, MaxEntryLength, "%02u:%02u:%02u.%03u", dateTime.Hours,
			dateTime.Minutes, dateTime.Seconds, dateTime.Milliseconds);

		debugOverlay->log(level, logEntryWithColors, tid, logEntryWithoutLevel);
	}
#endif

#if defined(WITH_TRACY)
	std::uint32_t colorTracy;
	switch (level) {
		case TraceLevel::Fatal:		colorTracy = 0xEC3E40; break;
		case TraceLevel::Assert:	colorTracy = 0xD651B0; break;
		case TraceLevel::Error:		colorTracy = 0xD85050; break;
		case TraceLevel::Warning:	colorTracy = 0xEBC77A; break;
		case TraceLevel::Info:		colorTracy = 0xD2D2D2; break;
		default:					colorTracy = 0x969696; break;
	}

	TracyMessageC(logEntryWithoutLevel, strlen(logEntryWithoutLevel), colorTracy);
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

	Viewport& Application::GetScreenViewport()
	{
		return *screenViewport_;
	}

	unsigned long int Application::GetFrameCount() const
	{
		return frameTimer_->GetTotalNumberFrames();
	}

	float Application::GetTimeMult() const
	{
		return frameTimer_->GetTimeMult();
	}

	const FrameTimer& Application::GetFrameTimer() const
	{
		return *frameTimer_;
	}

	void Application::ResizeScreenViewport(std::int32_t width, std::int32_t height)
	{
		if (screenViewport_ != nullptr) {
			bool sizeChanged = (width != screenViewport_->width_ || height != screenViewport_->height_);
			screenViewport_->resize(width, height);
			if (sizeChanged && width > 0 && height > 0) {
				appEventHandler_->OnResizeWindow(width, height);
			}
		}
	}

	bool Application::ShouldSuspend()
	{
		return ((!hasFocus_ && autoSuspension_) || isSuspended_);
	}

	void Application::PreInitCommon(std::unique_ptr<IAppEventHandler> appEventHandler)
	{
#if defined(WITH_BACKWARD)
#	if defined(DEATH_TARGET_ANDROID)
		// Try to save crash info to log file
		sh.destination() = __logFile->GetHandle();
#	elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_UNIX) || defined(DEATH_TARGET_WINDOWS)
		if (__hasVirtualTerminal) {
			sh.color_mode() = backward::ColorMode::always;
		}
#	endif
#endif

		appEventHandler_ = std::move(appEventHandler);
		appEventHandler_->OnPreInitialize(appCfg_);
		LOGI("IAppEventHandler::OnPreInitialize() invoked");
	}

	void Application::InitCommon()
	{
		TracyGpuContext;
		ZoneScopedC(0x81A861);
		// This timestamp is needed to initialize random number generator
		profileStartTime_ = TimeStamp::now();

#if defined(DEATH_TARGET_WINDOWS_RT)
		LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (UWP) initializing...");
#elif defined(WITH_GLFW)
		LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (GLFW) initializing...");
#elif defined(WITH_SDL)
		LOGI(NCINE_APP_NAME " v" NCINE_VERSION " (SDL2) initializing...");
#else
		LOGI(NCINE_APP_NAME " v" NCINE_VERSION " initializing...");
#endif
		
#if defined(WITH_TRACY)
		TracyAppInfo(NCINE_APP, sizeof(NCINE_APP) - 1);
		LOGW("Tracy integration is enabled");
#endif

#if defined(WITH_AUDIO)
		if (appCfg_.withAudio) {
			theServiceLocator().RegisterAudioDevice(std::make_unique<ALAudioDevice>());
		}
#endif
#if defined(WITH_THREADS)
		if (appCfg_.withThreads) {
			theServiceLocator().RegisterThreadPool(std::make_unique<ThreadPool>());
		}
#endif

		theServiceLocator().RegisterGfxCapabilities(std::make_unique<GfxCapabilities>());
		const auto& gfxCapabilities = theServiceLocator().GetGfxCapabilities();
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

		frameTimer_ = std::make_unique<FrameTimer>(appCfg_.frameTimerLogInterval, 0.2f);
#if 0 //defined(DEATH_TARGET_WINDOWS)
		_waitableTimer = ::CreateWaitableTimerW(NULL, TRUE, NULL);
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
		Random().Initialize(TimeStamp::now().ticks(), profileStartTime_.ticks());

		LOGI("Application initialized");
#if defined(NCINE_PROFILING)
		timings_[(std::int32_t)Timings::InitCommon] = profileStartTime_.secondsSince();
#endif
		{
			ZoneScopedNC("OnInitialize", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnInitialize();
#if defined(NCINE_PROFILING)
			timings_[(std::int32_t)Timings::AppInit] = profileStartTime_.secondsSince();
#endif
			LOGI("IAppEventHandler::OnInitialize() invoked");
		}

#if defined(WITH_IMGUI)
		imguiDrawing_->buildFonts();
#endif

		// Swapping frame now for a cleaner API trace capture when debugging
		gfxDevice_->update();
		FrameMark;
		TracyGpuCollect;
	}

	void Application::Step()
	{
		frameTimer_->AddFrame();

#if defined(WITH_IMGUI)
		{
			ZoneScopedN("ImGui newFrame");
#	if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#	endif
			imguiDrawing_->newFrame();
#	if defined(NCINE_PROFILING)
			timings_[(std::int32_t)Timings::ImGui] = profileStartTime_.secondsSince();
#	endif
		}
#endif
#if defined(WITH_LUA)
		LuaStatistics::update();
#endif

		{
			ZoneScopedNC("OnBeginFrame", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnBeginFrame();
#if defined(NCINE_PROFILING)
			timings_[(std::int32_t)Timings::BeginFrame] = profileStartTime_.secondsSince();
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
				timings_[(std::int32_t)Timings::Update] = profileStartTime_.secondsSince();
#endif
			}

			{
				ZoneScopedNC("OnPostUpdate", 0x81A861);
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				appEventHandler_->OnPostUpdate();
#if defined(NCINE_PROFILING)
				timings_[(std::int32_t)Timings::PostUpdate] = profileStartTime_.secondsSince();
#endif
			}

			{
				ZoneScopedNC("Visit", 0x81A861);
#if defined(NCINE_PROFILING)
				profileStartTime_ = TimeStamp::now();
#endif
				screenViewport_->visit();
#if defined(NCINE_PROFILING)
				timings_[(std::int32_t)Timings::Visit] = profileStartTime_.secondsSince();
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
				timings_[(std::int32_t)Timings::ImGui] += profileStartTime_.secondsSince();
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
				timings_[(std::int32_t)Timings::Draw] = profileStartTime_.secondsSince();
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
				timings_[(std::int32_t)Timings::ImGui] += profileStartTime_.secondsSince();
#	endif
			}
#endif
		}

		{
			theServiceLocator().GetAudioDevice().updatePlayers();
		}

		{
			ZoneScopedNC("OnFrameEnd", 0x81A861);
#if defined(NCINE_PROFILING)
			profileStartTime_ = TimeStamp::now();
#endif
			appEventHandler_->OnEndFrame();
#if defined(NCINE_PROFILING)
			timings_[(std::int32_t)Timings::EndFrame] = profileStartTime_.secondsSince();
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
#if 0 //defined(DEATH_TARGET_WINDOWS)
			// TODO: This code sometimes doesn't work properly
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
			while (frameTimer_->GetFrameDuration() < frameDuration) {
				Timer::sleep(0);
			}
#endif
			FrameMarkEnd("Frame limiting");
		}
	}

	void Application::ShutdownCommon()
	{
		ZoneScopedC(0x81A861);
		appEventHandler_->OnShutdown();
		LOGI("IAppEventHandler::OnShutdown() invoked");
		appEventHandler_ = nullptr;

#if defined(WITH_IMGUI)
		imguiDrawing_ = nullptr;
		debugOverlay_ = nullptr;
#endif
#if defined(WITH_RENDERDOC)
		RenderDocCapture::removeHooks();
#endif

		rootNode_ = nullptr;
		RenderResources::dispose();
		frameTimer_ = nullptr;
		inputManager_ = nullptr;
		gfxDevice_ = nullptr;

#if 0 //defined(DEATH_TARGET_WINDOWS)
		::CloseHandle(_waitableTimer);
#endif

		LOGI("Application shut down");

		theServiceLocator().UnregisterAll();
	}

	void Application::SetFocus(bool hasFocus)
	{
#if defined(WITH_TRACY) && !defined(DEATH_TARGET_ANDROID)
		hasFocus = true;
#endif

		hasFocus_ = hasFocus;
	}

	void Application::Suspend()
	{
		frameTimer_->Suspend();
		if (appEventHandler_ != nullptr) {
			appEventHandler_->OnSuspend();
		}
#if defined(WITH_AUDIO)
		IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
		audioDevice.suspendDevice();
#endif

		LOGI("IAppEventHandler::OnSuspend() invoked");
	}

	void Application::Resume()
	{
		if (appEventHandler_ != nullptr) {
			appEventHandler_->OnResume();
		}
#if defined(WITH_AUDIO)
		IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
		audioDevice.resumeDevice();
#endif

		const TimeStamp suspensionDuration = frameTimer_->Resume();
		LOGD("Suspended for %.3f seconds", suspensionDuration.seconds());
#if defined(NCINE_PROFILING)
		profileStartTime_ += suspensionDuration;
#endif
		LOGI("IAppEventHandler::OnResume() invoked");
	}

	void Application::AttachTraceTarget(Containers::StringView targetPath)
	{
#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
		__logFile = fs::Open(targetPath, FileAccess::Write);
		if (!__logFile->IsValid()) {
			__logFile = nullptr;
		}
#endif
	}
}
