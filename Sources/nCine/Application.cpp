﻿#include "Application.h"

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
#	include <Core/Backward.h>
Backward::ExceptionHandling __eh(Backward::Flags::UseStdError | Backward::Flags::IncludeSnippet | Backward::Flags::CreateMemoryDump);
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

#if defined(DEATH_TRACE)

#if defined(DEATH_TARGET_WINDOWS)
#	include <Environment.h>
#	include <Utf8.h>
#elif defined(DEATH_TARGET_ANDROID)
#	include <stdarg.h>
#	include <time.h>
#	include <unistd.h>
#	include <android/log.h>
#else
#	include <cstdarg>
#	include <unistd.h>
#	if defined(DEATH_TARGET_SWITCH)
#		include <time.h>
#		include <switch.h>
#	endif
#endif

#if defined(DEATH_TRACE_ASYNC) && (!defined(WITH_THREADS) || defined(DEATH_TARGET_EMSCRIPTEN))
#	pragma message("DEATH_TRACE_ASYNC is not supported on this platform")
#	undef DEATH_TRACE_ASYNC
#endif

static constexpr std::int32_t MaxLogEntryLength = 4096;

static const char ColorReset[] = "\x1B[0m";
static const char ColorBold[] = "\x1B[1m";
static const char ColorFaint[] = "\x1B[2m";
static const char ColorDarkGray[] = "\x1B[90m";
static const char ColorBrightRed[] = "\x1B[91m";
static const char ColorBrightYellow[] = "\x1B[93m";
static const char ColorBrightMagenta[] = "\x1B[95m";
static const char ColorString[] = "\x1B[0;38;2;211;161;129m";
static const char ColorDimString[] = "\x1B[0;38;2;177;150;132m";

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#else
#	include <IO/FileStream.h>
static std::unique_ptr<Death::IO::Stream> __logFile;
#endif

enum class ConsoleType {
	None,
	Redirect,
	WinApi,
	EscapeCodes,
	EscapeCodes8bit,
	EscapeCodes24bit
};

static ConsoleType __consoleType = ConsoleType::None;

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	if !defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
#		define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#	endif

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HANDLE __consoleHandleOut;
static SHORT __consoleCursorY;
static Array<wchar_t> __consolePrompt;

static bool EnableVirtualTerminalProcessing(HANDLE consoleHandleOut)
{
	if (consoleHandleOut == INVALID_HANDLE_VALUE || !Environment::IsWindows10()) {
		return false;
	}

	DWORD dwMode = 0;
	return (::GetConsoleMode(consoleHandleOut, &dwMode) &&
			::SetConsoleMode(consoleHandleOut, dwMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING));
}
#endif

namespace nCine
{
	struct TraceDateTime {
		std::uint32_t Milliseconds;
		std::uint32_t Seconds;
		std::uint32_t Minutes;
		std::uint32_t Hours;
		std::uint32_t Days;
		std::uint32_t Months;
		std::uint32_t Years;
	};

	static DEATH_ALWAYS_INLINE TraceDateTime GetTraceDateTime()
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

	template<std::int32_t N>
	static DEATH_ALWAYS_INLINE void AppendPart(char* dest, std::int32_t& length, const char(&newPart)[N])
	{
		length += nCine::copyStringFirst(dest + length, MaxLogEntryLength - length - 1, newPart, N - 1);
	}

	static DEATH_ALWAYS_INLINE void AppendPart(char* dest, std::int32_t& length, const char* newPart, std::int32_t newPartLength)
	{
		length += nCine::copyStringFirst(dest + length, MaxLogEntryLength - length - 1, newPart, newPartLength);
	}

	static void AppendMessagePrefixIfAny(char* dest, std::int32_t& length, const char* message, std::int32_t& logMsgFuncLength, TraceLevel level)
	{
		std::int32_t messageBegin = logMsgFuncLength;
		logMsgFuncLength++;

		while (true) {
			if (message[logMsgFuncLength] == '\0') {
				logMsgFuncLength = -1;
				break;
			}
			if (message[logMsgFuncLength] == '#' && message[logMsgFuncLength + 1] == '>') {
				logMsgFuncLength += 2;
				break;
			}
			logMsgFuncLength++;
		}

		if (logMsgFuncLength > 0) {
			if (__consoleType >= ConsoleType::EscapeCodes) {
				AppendPart(dest, length, ColorFaint);

				switch (level) {
					case TraceLevel::Error:
					case TraceLevel::Fatal:
						AppendPart(dest, length, ColorBrightRed);
						break;
					case TraceLevel::Assert:
						AppendPart(dest, length, ColorBrightMagenta);
						break;
					case TraceLevel::Warning:
						AppendPart(dest, length, ColorBrightYellow);
						break;
#if defined(DEATH_TARGET_EMSCRIPTEN)
					case TraceLevel::Debug:
						AppendPart(dest, length, ColorDarkGray);
						break;
#endif
				}
			}

			AppendPart(dest, length, message + messageBegin, logMsgFuncLength - messageBegin);
		} else {
			logMsgFuncLength = messageBegin;
		}
	}

	static void AppendMessageColor(char* dest, std::int32_t& length, TraceLevel level, bool resetBefore)
	{
		if (resetBefore) {
			AppendPart(dest, length, ColorReset);
		}

		switch (level) {
			case TraceLevel::Error:
			case TraceLevel::Fatal:
				AppendPart(dest, length, ColorBrightRed);
				if (level == TraceLevel::Fatal) {
					AppendPart(dest, length, ColorBold);
				}
				break;
			case TraceLevel::Assert:
				length += nCine::copyStringFirst(dest + length, MaxLogEntryLength - length - 1, ColorBrightMagenta, static_cast<std::int32_t>(arraySize(ColorBrightMagenta)) - 1);
				break;
#if defined(DEATH_TARGET_EMSCRIPTEN)
			case TraceLevel::Info:
			case TraceLevel::Warning:
				AppendPart(dest, length, ColorBold);
				break;
#else
			case TraceLevel::Warning:
				AppendPart(dest, length, ColorBrightYellow);
				break;
			case TraceLevel::Debug:
				AppendPart(dest, length, ColorDarkGray);
				break;
#endif
		}
	}

	void WriteTraceItem(TraceLevel level, std::uint32_t threadId, const char* logEntry, std::int32_t length, std::int32_t levelOffset, std::int32_t messageOffset)
	{
		char logEntryWithColors[MaxLogEntryLength + 24];

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

		const char* logEntryWithoutLevel = logEntry + messageOffset;
		std::int32_t result = __android_log_write(priority, NCINE_APP, logEntryWithoutLevel);
		std::int32_t n = 0;
		while (result == -11 /*EAGAIN*/ && n < 2) {
			::usleep(2000); // in microseconds
			result = __android_log_write(priority, NCINE_APP, logEntryWithoutLevel);
			n++;
		}
#elif defined(DEATH_TARGET_SWITCH)
		svcOutputDebugString(logEntry + levelOffset, length - levelOffset);
#elif defined(DEATH_TARGET_WINDOWS_RT)
		// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
		std::int32_t length2 = 0;
		AppendPart(logEntryWithColors, length2, logEntry + levelOffset, length - levelOffset);
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
		::OutputDebugStringA(logEntryWithColors);
#else
#	if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_DEBUG)
		if (__consoleType >= ConsoleType::Redirect) {
#	endif
			// Colorize the output
			std::int32_t length2 = 0;
			std::int32_t logMsgFuncLength = messageOffset;
			AppendMessagePrefixIfAny(logEntryWithColors, length2, logEntry, logMsgFuncLength, level);

			if (__consoleType >= ConsoleType::EscapeCodes) {
#	if defined(DEATH_TARGET_EMSCRIPTEN)
				bool shouldResetBefore = (level != TraceLevel::Warning && level != TraceLevel::Debug);
#	else
				bool shouldResetBefore = true;
#	endif
				bool shouldResetAfter = (level == TraceLevel::Debug || level == TraceLevel::Warning || level == TraceLevel::Error || level == TraceLevel::Assert || level == TraceLevel::Fatal);

				if (level < TraceLevel::Error && __consoleType >= ConsoleType::EscapeCodes24bit) {
					std::int32_t prevState = 0;
					StringView message = StringView(logEntry + logMsgFuncLength, length - logMsgFuncLength);
					do {
						StringView quotesBegin = message.find('"');
						if (!quotesBegin) {
							break;
						}
						StringView quotesEnd = message.suffix(quotesBegin.end()).find('"');
						if (!quotesEnd) {
							break;
						}

						StringView prefix = message.prefix(quotesBegin.begin());
						if (!prefix.empty()) {
							AppendMessageColor(logEntryWithColors, length2, level, prevState == 2 || shouldResetBefore);
							shouldResetBefore = false;
							prevState = 1;

							AppendPart(logEntryWithColors, length2, prefix.data(), prefix.size());
						}

						if (prevState != 2) {
							if (level == TraceLevel::Debug) {
								AppendPart(logEntryWithColors, length2, ColorDimString);
							} else {
								AppendPart(logEntryWithColors, length2, ColorString);
							}
							prevState = 2;
						}
					
						StringView inner = message.suffix(quotesBegin.begin()).prefix(quotesEnd.end());
						AppendPart(logEntryWithColors, length2, inner.data(), inner.size());

						message = message.suffix(quotesEnd.end());
					} while (!message.empty());

					if (!message.empty()) {
						AppendMessageColor(logEntryWithColors, length2, level, prevState == 2 || shouldResetBefore);
						AppendPart(logEntryWithColors, length2, message.data(), message.size());
					} else if (prevState == 2) {
						// Always reset color after quotes
						shouldResetAfter = true;
					}
				} else {
					AppendMessageColor(logEntryWithColors, length2, level, shouldResetBefore);
					AppendPart(logEntryWithColors, length2, logEntry + logMsgFuncLength, length - logMsgFuncLength);
				}

				if (shouldResetAfter) {
					AppendPart(logEntryWithColors, length2, ColorReset);
				}
			} else {
				AppendPart(logEntryWithColors, length2, logEntry + logMsgFuncLength, length - logMsgFuncLength);
			}

			if (length2 >= MaxLogEntryLength - 2) {
				length2 = MaxLogEntryLength - 2;
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
			if (__consoleType >= ConsoleType::EscapeCodes && length2 < MaxLogEntryLength) {
				// Console can be shared with parent process, so clear the rest of the line (using "\x1b[0K" sequence)
				length2--;
				logEntryWithColors[length2++] = '\x1b';
				logEntryWithColors[length2++] = '[';
				logEntryWithColors[length2++] = '0';
				logEntryWithColors[length2++] = 'K';
				logEntryWithColors[length2++] = '\n';
				logEntryWithColors[length2] = '\0';
			}

			::fputs(logEntryWithColors, level == TraceLevel::Error || level == TraceLevel::Fatal ? stderr : stdout);

			// Save the last cursor position for later
			if (__consoleHandleOut != NULL) {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
					__consoleCursorY = csbi.dwCursorPosition.Y;
				}
			}
#	else
			::fputs(logEntryWithColors, level == TraceLevel::Error || level == TraceLevel::Fatal ? stderr : stdout);
#	endif

#	if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_DEBUG)
		} else {
			// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
			std::int32_t length2 = 0;
			AppendPart(logEntryWithColors, length2, logEntry + levelOffset, length - levelOffset);
			if (length2 >= MaxLogEntryLength - 2) {
				length2 = MaxLogEntryLength - 2;
			}
			logEntryWithColors[length2++] = '\n';
			logEntryWithColors[length2] = '\0';
			::OutputDebugStringA(logEntryWithColors);
		}
#	endif
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// Allow to attach custom target using Application::AttachTraceTarget()
		if (__logFile != nullptr) {
			auto* s = static_cast<FileStream*>(__logFile.get());
			::fprintf(s->GetHandle(), "%s\n", logEntry);
			if (level >= TraceLevel::Error) {
				// Flush immediately only Error/Assert/Fatal messages
				s->Flush();
			}
		}
#endif

#if defined(WITH_IMGUI)
		auto* debugOverlay = theApplication().debugOverlay_.get();
		if (debugOverlay != nullptr) {
			std::int32_t length3 = 0;
			StringView dateTimeString = StringView(logEntry, messageOffset);
			if (StringView firstSpace = dateTimeString.find(' ')) {
				std::size_t dateTimeLength = (firstSpace.begin() - logEntry);
				if (dateTimeLength == 12) {	// Expected length of date time string
					AppendPart(logEntryWithColors, length3, logEntry, dateTimeLength);
				}
			}

			if (length3 == 0) {
				// This shouldn't happen, but try to handle also non-standard messages
				TraceDateTime dateTime = GetTraceDateTime();
				snprintf(logEntryWithColors, MaxLogEntryLength, "%02u:%02u:%02u.%03u", dateTime.Hours,
					dateTime.Minutes, dateTime.Seconds, dateTime.Milliseconds);
			}

			debugOverlay->log(level, logEntryWithColors, threadId, StringView(logEntry + messageOffset, length - messageOffset));
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

		TracyMessageC(logEntry + messageOffset, length - messageOffset, colorTracy);
#endif
	}
}

#if defined(DEATH_TRACE_ASYNC)
#	include "Base/ConcurentQueue/concurrentqueue.h"
#	include <Threading/Event.h>
#	include <Threading/Interlocked.h>

using namespace Death::Threading;

namespace nCine
{
	struct TraceItem {
		std::string Buffer;
		TraceLevel Level;
		std::uint32_t ThreadId;
		std::int32_t LevelOffset;
		std::int32_t MessageOffset;

		void Assign(TraceLevel level, std::uint32_t threadId, const char* logEntry, std::int32_t length, std::int32_t levelOffset, std::int32_t messageOffset);
	};

	void TraceItem::Assign(TraceLevel level, std::uint32_t threadId, const char* logEntry, std::int32_t length, std::int32_t levelOffset, std::int32_t messageOffset)
	{
		Level = level;
		Buffer.clear();
		Buffer.append(logEntry, length);
		ThreadId = threadId;
		LevelOffset = levelOffset;
		MessageOffset = messageOffset;
	}

	static constexpr std::int32_t AsyncTraceStateUninitialized = 0;
	static constexpr std::int32_t AsyncTraceStateReady = 1;
	static constexpr std::int32_t AsyncTraceStateReleasing = 2;
	static constexpr std::int32_t AsyncTraceStateReleased = 3;

	static nCine::Thread __asyncTraceThread;
	static AutoResetEvent __asyncTraceWait;
	static std::int32_t __asyncTraceState = AsyncTraceStateUninitialized;
	static std::int32_t __asyncTraceFlush = 0;
	static moodycamel::ConcurrentQueue<TraceItem> __asyncTraceQueuedItems;

	static void OnTraceThread(void* _)
	{
		nCine::Thread::SetCurrentName("Async Tracing");

		while (__asyncTraceState == AsyncTraceStateReady || __asyncTraceState == AsyncTraceStateReleasing) {
			TraceItem item;
			bool itemFound = false;
			while (__asyncTraceQueuedItems.try_dequeue(item)) {
				WriteTraceItem(item.Level, item.ThreadId, item.Buffer.c_str(),
					(std::int32_t)item.Buffer.size(), item.LevelOffset, item.MessageOffset);
				itemFound = true;
			}

			Interlocked::WriteRelease(&__asyncTraceFlush, 0);

			if (!itemFound && __asyncTraceState == AsyncTraceStateReleasing) {
				// Queue is empty, so the thread can be stopped
				break;
			}

			if (__asyncTraceState == AsyncTraceStateReady) {
				__asyncTraceWait.Wait();
			}
		}

		Interlocked::WriteRelease(&__asyncTraceState, AsyncTraceStateReleased);
	}

	static void InitializeAsyncTrace()
	{
		if (Interlocked::CompareExchange(&__asyncTraceState, AsyncTraceStateReady, AsyncTraceStateUninitialized) != AsyncTraceStateUninitialized) {
			return;
		}

		__asyncTraceThread.Run(OnTraceThread, nullptr);
	}

	static void CleanUpAsyncTrace()
	{
		if (Interlocked::Exchange(&__asyncTraceState, AsyncTraceStateReleasing) != AsyncTraceStateReady) {
			return;
		}

		__asyncTraceWait.SetEvent();
		__asyncTraceThread.Join();
	}

	static void QueueTraceItem(TraceLevel level, std::uint32_t threadId, const char* logEntry, std::int32_t length, std::int32_t levelOffset, std::int32_t messageOffset)
	{
		// Block the thread until queue is flushed, limit number of spins to avoid deadlock
		std::int32_t n = 0;
		while (__asyncTraceFlush != 0 && n++ < 10000) {
			nCine::Timer::sleep(0);
		}

		TraceItem item;
		item.Assign(level, threadId, logEntry, length, levelOffset, messageOffset);

		__asyncTraceQueuedItems.enqueue(std::move(item));

		__asyncTraceWait.SetEvent();

		if (level >= TraceLevel::Error && __asyncTraceState == AsyncTraceStateReady) {
			// Wait for flush Error/Assert/Fatal messages
			Interlocked::WriteRelease(&__asyncTraceFlush, 1);
			if (__asyncTraceState == AsyncTraceStateReady) {
				while (Interlocked::ReadAcquire(&__asyncTraceFlush) != 0) {
					nCine::Timer::sleep(0);
				}
			}
		}
	}
}
#endif

void DEATH_TRACE(TraceLevel level, const char* fmt, ...)
{
	char logEntry[MaxLogEntryLength + 1];

	nCine::TraceDateTime dateTime = nCine::GetTraceDateTime();
	std::int32_t length = snprintf(logEntry, MaxLogEntryLength, "%02u:%02u:%02u.%03u ",
		dateTime.Hours, dateTime.Minutes, dateTime.Seconds, dateTime.Milliseconds);
	std::int32_t levelOffset = length;

	char levelIdentifier;
	switch (level) {
		case TraceLevel::Fatal:		levelIdentifier = 'F'; break;
		case TraceLevel::Assert:	levelIdentifier = 'A'; break;
		case TraceLevel::Error:		levelIdentifier = 'E'; break;
		case TraceLevel::Warning:	levelIdentifier = 'W'; break;
		case TraceLevel::Info:		levelIdentifier = 'I'; break;
		default:					levelIdentifier = 'D'; break;
	}
#if defined(WITH_THREADS)
	std::uint32_t threadId = static_cast<std::uint32_t>(nCine::Thread::GetCurrentId());
	length += snprintf(logEntry + length, MaxLogEntryLength - length, "[%c]%u}", levelIdentifier, threadId);
#else
	std::uint32_t threadId = 0;
	length += snprintf(logEntry + length, MaxLogEntryLength - length, "[%c]", levelIdentifier);
#endif
	while (length < 23) {
		logEntry[length++] = ' ';
	}
	logEntry[length++] = ' ';
	std::int32_t messageOffset = length;

	va_list args;
	va_start(args, fmt);
	std::int32_t partLength = vsnprintf(logEntry + length, MaxLogEntryLength - length, fmt, args);
	va_end(args);
	if (partLength <= 0) {
		return;
	}

	length += partLength;
	if (length >= MaxLogEntryLength - 2) {
		length = MaxLogEntryLength - 2;
	}

#if defined(DEATH_TRACE_ASYNC)
	nCine::QueueTraceItem(level, threadId, logEntry, length, levelOffset, messageOffset);
#else
	nCine::WriteTraceItem(level, threadId, logEntry, length, levelOffset, messageOffset);
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
#if defined(DEATH_TRACE)
		InitializeTrace();
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

#if defined(DEATH_TRACE)
		ShutdownTrace();
#endif
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
#if defined(DEATH_TRACE) && defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (targetPath == ConsoleTarget) {
			if (__consoleType == ConsoleType::None) {
				bool hasVirtualTerminal = false;
				if (CreateTraceConsole(NCINE_APP_NAME " [Console]", hasVirtualTerminal)) {
					__consoleType = (hasVirtualTerminal ? ConsoleType::EscapeCodes24bit : ConsoleType::WinApi);

					CONSOLE_SCREEN_BUFFER_INFO csbi;
					__consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
					if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
						__consoleCursorY = csbi.dwCursorPosition.Y;
					} else {
						__consoleHandleOut = NULL;
					}
				} else {
					__consoleType = ConsoleType::Redirect;
				}

#	if defined(WITH_BACKWARD)
				if (__consoleType >= ConsoleType::EscapeCodes) {
					__eh.FeatureFlags |= Backward::Flags::ColorizeOutput;
				}
#	endif
			}
			return;
		}
#endif

#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
		if (__logFile = fs::Open(targetPath, FileAccess::Write)) {
#	if defined(WITH_BACKWARD)
			// Try to save crash info to log file
			__eh.Destination = static_cast<FileStream*>(__logFile.get())->GetHandle();
#	endif
		} else {
			__logFile = nullptr;
		}
#endif
	}

#if defined(DEATH_TRACE)
	void Application::InitializeTrace()
	{
#	if defined(DEATH_TRACE_ASYNC)
		InitializeAsyncTrace();
#	endif

#	if defined(DEATH_TARGET_EMSCRIPTEN)
		char* userAgent = (char*)EM_ASM_PTR({
			return (typeof navigator !== 'undefined' && navigator !== null &&
					typeof navigator.userAgent !== 'undefined' && navigator.userAgent !== null
						? stringToNewUTF8(navigator.userAgent) : 0);
		});
		if (userAgent != nullptr) {
			// Only Chrome supports ANSI escape sequences for now
			__consoleType = (::strcasestr(userAgent, "chrome") != nullptr ? ConsoleType::EscapeCodes : ConsoleType::Redirect);
			std::free(userAgent);
		} else {
			__consoleType = ConsoleType::Redirect;
		}
#	elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#		if defined(DEATH_TARGET_UNIX)
		::setvbuf(stdout, nullptr, _IONBF, 0);
		::setvbuf(stderr, nullptr, _IONBF, 0);
#		endif

		// Xcode's console reports that it is a TTY, but it doesn't support colors, TERM is not defined in this case
		__consoleType = ConsoleType::Redirect;

		if (::isatty(1)) {
			StringView COLORTERM = ::getenv("COLORTERM");
			StringView TERM = ::getenv("TERM");

			if (!COLORTERM.empty()) {
				if (COLORTERM.contains("truecolor"_s) || COLORTERM.contains("24bit"_s)) {
					__consoleType = ConsoleType::EscapeCodes24bit;
				} else if (COLORTERM.contains("256color"_s) || COLORTERM.contains("rxvt-xpm"_s)) {
					__consoleType = ConsoleType::EscapeCodes8bit;
				}
			}

			if (__consoleType < ConsoleType::EscapeCodes8bit && !TERM.empty()) {
				if (TERM.contains("256color"_s) || TERM.contains("rxvt-xpm"_s)) {
					__consoleType = ConsoleType::EscapeCodes8bit;
				} else if (TERM.contains("xterm"_s) || TERM.contains("vt1"_s) || TERM.contains("linux"_s)) {
					__consoleType = ConsoleType::EscapeCodes;
				}
			}
		}
#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		HANDLE hStdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if (::GetFileType(hStdOut) != FILE_TYPE_UNKNOWN) {
			__consoleType = ConsoleType::Redirect;
		}
#	endif

#	if defined(WITH_BACKWARD) && (defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_UNIX))
		if (__consoleType >= ConsoleType::EscapeCodes) {
			__eh.FeatureFlags |= Backward::Flags::ColorizeOutput;
		}
#	endif
	}

	void Application::ShutdownTrace()
	{
#	if defined(DEATH_TRACE_ASYNC)
		CleanUpAsyncTrace();
#	endif
#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (__consoleType >= ConsoleType::WinApi) {
			DestroyTraceConsole();
		}
#	endif
	}

#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
	bool Application::CreateTraceConsole(StringView title, bool& hasVirtualTerminal)
	{
		FILE* fDummy = nullptr;

		if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
			HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
			if (consoleHandleOut != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONOUT$", "w", stdout);
				::setvbuf(stdout, NULL, _IONBF, 0);
			}

			HANDLE consoleHandleError = ::GetStdHandle(STD_ERROR_HANDLE);
			if (consoleHandleError != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONOUT$", "w", stderr);
				::setvbuf(stderr, NULL, _IONBF, 0);
			}

			HANDLE consoleHandleIn = ::GetStdHandle(STD_INPUT_HANDLE);
			if (consoleHandleIn != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONIN$", "r", stdin);
				::setvbuf(stdin, NULL, _IONBF, 0);
			}

			hasVirtualTerminal = EnableVirtualTerminalProcessing(consoleHandleOut);

			// Try to get command prompt to be able to reprint it when the game exits
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			if (::GetConsoleScreenBufferInfo(consoleHandleOut, &csbi)) {
				DWORD dwConsoleColumnWidth = (DWORD)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
				SHORT xEnd = csbi.dwCursorPosition.X;
				SHORT yEnd = csbi.dwCursorPosition.Y;
				if (xEnd != 0 || yEnd != 0) {
					DWORD dwNumberOfChars;
					SHORT yBegin = yEnd;
					if (dwConsoleColumnWidth > 16) {
						Array<wchar_t> tmp(NoInit, dwConsoleColumnWidth);
						while (yBegin > 0) {
							COORD dwReadCoord = { 0, yBegin };
							if (!::ReadConsoleOutputCharacter(consoleHandleOut, tmp.data(), dwConsoleColumnWidth, dwReadCoord, &dwNumberOfChars)) {
								break;
							}

							for (DWORD i = dwNumberOfChars - 8; i < dwNumberOfChars; i++) {
								wchar_t wchar = tmp[i];
								if (wchar != L' ') {
									yBegin--;
									continue;
								}
							}

							if (yBegin < yEnd) {
								yBegin++;
							}
							break;
						}
					}

					DWORD promptLength = (yEnd - yBegin) * dwConsoleColumnWidth + xEnd;
					__consolePrompt = Array<wchar_t>(NoInit, promptLength);
					COORD dwPromptCoord = { 0, yEnd };
					if (::ReadConsoleOutputCharacter(consoleHandleOut, __consolePrompt.data(), promptLength, dwPromptCoord, &dwNumberOfChars)) {
						if (::SetConsoleCursorPosition(consoleHandleOut, dwPromptCoord)) {
							::FillConsoleOutputCharacter(consoleHandleOut, L' ', promptLength, dwPromptCoord, &dwNumberOfChars);
						}
					} else {
						__consolePrompt = {};
					}
				}
			}

			return true;
		} else if (::AllocConsole()) {
			::freopen_s(&fDummy, "CONOUT$", "w", stdout);
			::freopen_s(&fDummy, "CONOUT$", "w", stderr);
			::freopen_s(&fDummy, "CONIN$", "r", stdin);

			HANDLE consoleHandleOut = ::CreateFile(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			HANDLE consoleHandleIn = ::CreateFile(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			::SetStdHandle(STD_OUTPUT_HANDLE, consoleHandleOut);
			::SetStdHandle(STD_ERROR_HANDLE, consoleHandleOut);
			::SetStdHandle(STD_INPUT_HANDLE, consoleHandleIn);

			hasVirtualTerminal = EnableVirtualTerminalProcessing(consoleHandleOut);

			::SetConsoleTitle(Death::Utf8::ToUtf16(title));
			HWND hWnd = ::GetConsoleWindow();
			if (hWnd != nullptr) {
				HINSTANCE inst = ((HINSTANCE)&__ImageBase);
				HICON windowIcon = (HICON)::LoadImage(inst, L"WINDOW_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
				HICON windowIconSmall = (HICON)::LoadImage(inst, L"WINDOW_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
				if (windowIconSmall != NULL) ::SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)windowIconSmall);
				if (windowIcon != NULL) ::SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)windowIcon);
			}

			return true;
		} else {
			hasVirtualTerminal = false;
		}

		return false;
	}

	void Application::DestroyTraceConsole()
	{
		if (!__consolePrompt.empty()) {
			HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
			if (consoleHandleOut != INVALID_HANDLE_VALUE) {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				if (::GetConsoleScreenBufferInfo(consoleHandleOut, &csbi)) {
					DWORD xEnd = csbi.dwCursorPosition.X;
					DWORD yEnd = csbi.dwCursorPosition.Y;
					if (xEnd != 0 || yEnd != 0) {
						DWORD dwNumberOfCharsWritten;
						::WriteConsoleW(consoleHandleOut, L"\r\n", (DWORD)arraySize(L"\r\n") - 1, &dwNumberOfCharsWritten, NULL);
						::WriteConsoleW(consoleHandleOut, __consolePrompt.data(), (DWORD)__consolePrompt.size(), &dwNumberOfCharsWritten, NULL);
					}
				}
			}
		}

		::FreeConsole();
	}
#	endif
#endif
}
